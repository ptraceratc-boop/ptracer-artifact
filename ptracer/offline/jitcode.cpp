// jitcode.cpp -- see jitcode.h
#include "jitcode.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
#pragma pack(push, 1)
struct RecHdr {
    uint32_t magic;      // "JTR1"
    uint32_t len;        // total record length, 8-aligned
    uint64_t addr;       // code start (evt 4: the trampoline's address)
    uint64_t new_addr;   // evt 1: the destination; evt 4: the patched code address
    uint64_t tsc;        // rdtscp at the event
    uint32_t code_len;
    uint8_t  code_type;  // 0 BYTE_CODE, 1 JIT_CODE, 2 WASM_CODE, 3 VM stub / dynamic code
    uint8_t  evt;        // 0 ADDED, 1 MOVED, 2 REMOVED, 3 patched, 4 tramp, 5 slab, 6 native overlay
    uint16_t name_len;
};
#pragma pack(pop)
static const uint32_t JTR1 = 0x4a545231u;
}

bool JitTable::load(const std::string& path, std::string* err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf((size_t)(sz > 0 ? sz : 0));
    if (sz > 0 && fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); if (err) *err = "short read " + path; return false; }
    fclose(f);
    files_.push_back(std::move(buf));
    const std::vector<uint8_t>& d = files_.back();
    size_t off = 0;
    while (off + sizeof(RecHdr) <= d.size()) {
        RecHdr h; memcpy(&h, d.data() + off, sizeof h);
        if (h.magic != JTR1 || h.len < sizeof(RecHdr) || off + h.len > d.size()) {
            n_truncated++;
            if (err) *err = "bad record at offset " + std::to_string(off) + " of " + path;
            break;                                    // a truncated arena: keep what we parsed
        }
        if (h.name_len > h.len - sizeof(RecHdr) ||
            h.addr > UINT64_MAX - h.code_len || h.new_addr > UINT64_MAX - h.code_len) {
            n_truncated++;
            if (err) *err = "invalid record fields at offset " + std::to_string(off) + " of " + path;
            break;
        }
        const uint8_t* body = d.data() + off + sizeof(RecHdr);
        const uint8_t* code = body + h.name_len;
        size_t avail = h.len - sizeof(RecHdr) - h.name_len;
        bool has_code = (h.code_len > 0 && avail >= h.code_len);
        n_records++;
        switch (h.evt) {
            case 0: {                                  // CODE_ADDED
                n_added++;
                if (!has_code) break;
                kill_overlapping(h.addr, h.addr + h.code_len, h.tsc);
                Ver v; v.addr = h.addr; v.len = h.code_len; v.tsc = h.tsc; v.orig = code;
                v.code_type = h.code_type;
                vers_.push_back(v);
                code_bytes += h.code_len;
                break;
            }
            case 1: {                                  // CODE_MOVED
                n_moved++;
                // PTRECON_JIT_NOMOVE=1 is the ABLATION: do not apply the move, i.e. behave as
                // if V8 never issued CODE_REMOVED and the dump were a flat address -> bytes map.
                static const bool nomove = getenv("PTRECON_JIT_NOMOVE") != nullptr;
                if (nomove) break;
                // Everything live inside [addr, addr+len) is now at new_addr + the same offset,
                // with the bytes it was GENERATED with: the runtime restores its own windows in
                // the moved copy and re-patches it, emitting fresh evt 3 / evt 4 records.
                std::vector<Ver> born;
                for (auto& v : vers_) {
                    if (v.dead <= h.tsc) continue;
                    if (v.addr + v.len <= h.addr || v.addr >= h.addr + h.code_len) continue;
                    Ver nv = v;
                    nv.addr = h.new_addr + (v.addr - h.addr);
                    nv.tsc = h.tsc; nv.dead = ~0ull; nv.pimg = -1; nv.p_n = 0; nv.p_first = 0;
                    nv.p_lo = ~0ull; nv.p_hi = 0;
                    born.push_back(nv);
                    v.dead = h.tsc;
                }
                if (born.empty()) { n_move_unknown++; break; }
                for (auto& nv : born) kill_overlapping(nv.addr, nv.addr + nv.len, h.tsc);
                for (auto& nv : born) vers_.push_back(nv);
                break;
            }
            case 2:                                    // CODE_REMOVED (HotSpot CompiledMethodUnload)
                n_removed++;
                kill_overlapping(h.addr, h.addr + (h.code_len ? h.code_len : 1), h.tsc);
                break;
            case 3:                                    // a patched window: the bytes AFTER the jmp
                n_patched++;
                // Ownership (which code-object version this window belongs to) is resolved in
                // finalize(), when every version and the page index exist.
                if (has_code) raw_patches_.push_back({h.tsc, h.addr, h.code_len, code});
                break;
            case 4:                                    // a trampoline
                n_tramp_rec++;
                if (has_code) tramps_.push_back({h.addr, h.tsc, h.code_len, code});
                break;
            case 5:                                    // the whole trampoline slab, at exit
                n_slab_rec++;
                if (has_code) slabs_.push_back({h.addr, h.addr + h.code_len, code});
                break;
            case 6:                                    // explicitly recorded native ELF mutation
                n_native_patch++;
                if (has_code) native_patches_.push_back({h.tsc, h.addr, h.code_len, code});
                break;
            default: break;
        }
        off += h.len;
    }
    return true;
}

void JitTable::kill_overlapping(uint64_t lo, uint64_t hi, uint64_t tsc) {
    for (auto& v : vers_) {
        if (v.dead <= tsc) continue;
        if (v.addr + v.len <= lo || v.addr >= hi) continue;
        v.dead = tsc;
        n_reused_addr++;
    }
}

void JitTable::drop_file_backed(const std::vector<JitRange>& file_exec) {
    if (file_exec.empty()) return;
    auto covered = [&](uint64_t a) {
        for (auto& r : file_exec) if (a >= r.lo && a < r.hi) return true;
        return false;
    };
    std::vector<Ver> keep; keep.reserve(vers_.size());
    for (auto& v : vers_) {
        if (covered(v.addr)) { n_dropped_file++; continue; }
        keep.push_back(v);
    }
    vers_.swap(keep);
}

void JitTable::finalize() {
    if (final_) return;
    final_ = true;
    n_versions = vers_.size();
    // Sidecar runtime histories can be loaded after the instrumentation dump.
    // Byte updates must compose in timestamp order, not input-file order.
    auto by_time = [](const Patch& a, const Patch& b) { return a.tsc < b.tsc; };
    std::stable_sort(raw_patches_.begin(), raw_patches_.end(), by_time);
    std::stable_sort(native_patches_.begin(), native_patches_.end(), by_time);
    for (uint32_t i = 0; i < native_patches_.size(); ++i) {
        const auto& p = native_patches_[i];
        for (uint64_t pg = p.addr & ~0xfffull; pg < p.addr + p.len; pg += 4096)
            native_page_[pg].push_back(i);
    }
    // Page index over the versions (needed to attribute the patched windows).
    // `code_type 0' is BYTE_CODE -- a V8 bytecode array, which lives in the (non-executable) JS
    // heap and is never fetched by the CPU.  It is reported through the same CODE_ADDED /
    // CODE_MOVED events and is 85 % of the records of a churny workload, so it is indexed
    // nowhere: it must not claim heap addresses as program code nor be served as instructions.
    for (uint32_t i = 0; i < vers_.size(); i++) {
        if (vers_[i].code_type == 0) { n_bytecode++; continue; }
        for (uint64_t pg = vers_[i].addr & ~0xfffull; pg < vers_[i].addr + vers_[i].len; pg += 0x1000)
            vpage_[pg].push_back(i);
    }
    // Attribute each patched window (evt 3) to the code-object VERSION that was live at the
    // window's address at the window's own tsc.  A window is by construction inside one object.
    {
        std::vector<std::vector<uint32_t>> pofs(vers_.size());
        for (uint32_t k = 0; k < raw_patches_.size(); k++) {
            const Patch& p = raw_patches_[k];
            auto it = vpage_.find(p.addr & ~0xfffull);
            int best = -1; uint64_t best_tsc = 0;
            if (it != vpage_.end())
                for (uint32_t i : it->second) {
                    const Ver& v = vers_[i];
                    if (v.tsc > p.tsc || v.dead <= p.tsc) continue;
                    if (p.addr < v.addr || p.addr + p.len > v.addr + v.len) continue;
                    if (best < 0 || v.tsc >= best_tsc) { best = (int)i; best_tsc = v.tsc; }
                }
            if (best < 0) { n_orphan_patch++; continue; }
            pofs[best].push_back(k);
        }
        patches_.clear();
        for (size_t i = 0; i < vers_.size(); i++) {
            if (pofs[i].empty()) continue;
            vers_[i].p_first = (uint32_t)patches_.size();
            for (uint32_t k : pofs[i]) {
                Patch p = raw_patches_[k];
                p.off = (uint32_t)(p.addr - vers_[i].addr);
                patches_.push_back(p);
                if (p.tsc < vers_[i].p_lo) vers_[i].p_lo = p.tsc;
                if (p.tsc > vers_[i].p_hi) vers_[i].p_hi = p.tsc;
            }
            vers_[i].p_n = (uint32_t)(patches_.size() - vers_[i].p_first);
        }
    }
    // Materialise the fully patched image of every version that has patches.
    for (auto& v : vers_) {
        if (!v.p_n || !v.orig) continue;
        std::vector<uint8_t> img(v.orig, v.orig + v.len);
        for (uint32_t k = v.p_first; k < v.p_first + v.p_n; k++) {
            const Patch& p = patches_[k];
            if ((uint64_t)p.off + p.len <= v.len) memcpy(img.data() + p.off, p.bytes, p.len);
        }
        pimgs_.push_back(std::move(img));
        v.pimg = (int32_t)(pimgs_.size() - 1);
    }
    for (uint32_t i = 0; i < tramps_.size(); i++)
        for (uint64_t pg = tramps_[i].addr & ~0xfffull; pg < tramps_[i].addr + tramps_[i].len; pg += 0x1000)
            tpage_[pg].push_back(i);
    // Coalesced range lists.
    auto coalesce = [](std::vector<JitRange>& v) {
        std::sort(v.begin(), v.end(), [](const JitRange& a, const JitRange& b) { return a.lo < b.lo; });
        std::vector<JitRange> out;
        for (auto& r : v) { if (!out.empty() && r.lo <= out.back().hi) { if (r.hi > out.back().hi) out.back().hi = r.hi; } else out.push_back(r); }
        v.swap(out);
    };
    for (auto& v : vers_) if (v.code_type != 0) code_ranges_.push_back({v.addr, v.addr + v.len});
    coalesce(code_ranges_);
    for (auto& t : tramps_) tramp_ranges_.push_back({t.addr, t.addr + t.len});
    for (auto& s : slabs_) tramp_ranges_.push_back({s.lo, s.hi});
    coalesce(tramp_ranges_);
    all_ranges_ = code_ranges_;
    for (auto& r : tramp_ranges_) all_ranges_.push_back(r);
    for (const auto& p : native_patches_) {
        all_ranges_.push_back({p.addr, p.addr + p.len});
        native_ranges_.push_back({p.addr & ~0xfffull, (p.addr + p.len + 4095) & ~0xfffull});
    }
    coalesce(native_ranges_);
    coalesce(all_ranges_);
}

void JitTable::add_background(uint64_t lo, uint64_t hi, const uint8_t* bytes, size_t n) {
    if (hi > lo + n) hi = lo + n;
    bg_.push_back({lo, hi, bytes});
}

bool JitTable::in_sorted(const std::vector<JitRange>& v, uint64_t a) {
    size_t lo = 0, hi = v.size();
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (v[mid].lo <= a) lo = mid + 1; else hi = mid; }
    return lo > 0 && a < v[lo - 1].hi;
}
bool JitTable::in_code(uint64_t a) const { return in_sorted(code_ranges_, a); }
bool JitTable::in_tramp(uint64_t a) const { return in_sorted(tramp_ranges_, a); }

bool JitTable::overlaps(uint64_t lo, uint64_t hi) const {
    for (auto& r : all_ranges_) { if (r.lo >= hi) break; if (r.hi > lo) return true; }
    return false;
}

std::vector<JitRange> JitTable::immutable_ranges(uint64_t lo, uint64_t hi) const {
    for (const auto* ranges : {&code_ranges_, &tramp_ranges_})
        for (const auto& r : *ranges) {
            if (r.lo >= hi) break;
            if (r.hi > lo) return {};
        }
    std::vector<JitRange> out;
    for (const auto& r : native_ranges_) {
        if (r.lo >= hi) break;
        if (r.hi <= lo) continue;
        if (lo < r.lo) out.push_back({lo, r.lo});
        lo = std::min(hi, r.hi);
    }
    if (lo < hi) out.push_back({lo, hi});
    return out;
}

// The time key.  An address covered by exactly ONE version in the whole run is served
// unconditionally -- trace time is consulted only where the address is ambiguous, which is
// where the dump's own reuse distance (milliseconds) dwarfs the timestamp granularity.
const JitTable::Ver* JitTable::find_ver(uint64_t a, uint64_t t, bool* ambiguous) {
    *ambiguous = false;
    if (last_ver_ < vers_.size()) {
        const Ver& v = vers_[last_ver_];
        if (a >= v.addr && a < v.addr + v.len && t >= v.tsc && t < v.dead) return &v;
    }
    auto it = vpage_.find(a & ~0xfffull);
    if (it == vpage_.end()) return nullptr;
    const Ver* best = nullptr; size_t besti = 0; int n_cover = 0;
    const Ver* earliest = nullptr;
    for (uint32_t i : it->second) {
        const Ver& v = vers_[i];
        if (a < v.addr || a >= v.addr + v.len) continue;
        n_cover++;
        if (!earliest || v.tsc < earliest->tsc) earliest = &v;
        if (v.tsc > t || v.dead <= t) continue;
        if (!best || v.tsc > best->tsc) { best = &v; besti = i; }
    }
    if (n_cover == 0) return nullptr;
    if (n_cover == 1) {
        // unique over the whole run: no time key needed (and none is trusted)
        for (uint32_t i : it->second) { const Ver& v = vers_[i]; if (a >= v.addr && a < v.addr + v.len) { last_ver_ = i; return &v; } }
    }
    *ambiguous = true; r_ambiguous++;
    if (best) { r_time_used++; last_ver_ = besti; return best; }
    // Trace time is behind every candidate (a lagging timestamp at the first execution of a
    // freshly installed object): take the earliest, and report the lag.
    t_behind++;
    uint64_t lag = earliest->tsc > t ? earliest->tsc - t : 0;
    if (lag > t_behind_max) t_behind_max = lag;
    return earliest;
}

const JitTable::Ver* JitTable::find_ver_const(uint64_t a, uint64_t t) const {
    auto it = vpage_.find(a & ~0xfffull);
    if (it == vpage_.end()) return nullptr;
    const Ver* best = nullptr; const Ver* earliest = nullptr;
    for (uint32_t i : it->second) {
        const Ver& v = vers_[i];
        if (a < v.addr || a >= v.addr + v.len) continue;
        if (!earliest || v.tsc < earliest->tsc) earliest = &v;
        if (v.tsc > t) continue;
        if (!best || v.tsc > best->tsc) best = &v;
    }
    return best ? best : earliest;
}

// The bytes the CPU executed at `addr` at trace time `tsc`.
int JitTable::read(uint8_t* buf, size_t n, uint64_t a, uint64_t t) {
    // ~0 means "no timing information: take the newest version".  A live version's `dead' is ~0,
    // so the sentinel is clamped one below it to keep the half-open [tsc, dead) test working.
    if (t == ~0ull) t = ~0ull - 1;
    int count = read_base(buf, n, a, t);
    if (count <= 0 || native_patches_.empty()) return count;
    // Usually one page; a decoder may request bytes straddling a page boundary.
    std::vector<uint32_t> candidates;
    for (uint64_t pg = a & ~0xfffull; pg < a + (size_t)count; pg += 4096) {
        auto it = native_page_.find(pg);
        if (it != native_page_.end()) candidates.insert(candidates.end(), it->second.begin(), it->second.end());
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    bool changed = false;
    for (uint32_t i : candidates) {
        const auto& p = native_patches_[i];
        if (p.tsc > t) continue;
        const uint64_t lo = std::max(a, p.addr), hi = std::min(a + (size_t)count, p.addr + p.len);
        if (lo < hi) { memcpy(buf + lo - a, p.bytes + lo - p.addr, hi - lo); changed = true; }
    }
    if (changed) ++r_native_patch;
    return count;
}

int JitTable::read_base(uint8_t* buf, size_t n, uint64_t a, uint64_t t) {
    bool amb = false;
    const Ver* v = find_ver(a, t, &amb);
    if (v && v->orig) {
        size_t off = (size_t)(a - v->addr);
        size_t k = std::min(n, (size_t)(v->len - off));
        if (!v->p_n) memcpy(buf, v->orig + off, k);
        else if (t >= v->p_hi && v->pimg >= 0) memcpy(buf, pimgs_[v->pimg].data() + off, k);
        else if (t < v->p_lo) memcpy(buf, v->orig + off, k);
        else {                                  // inside the patch burst: compose exactly
            r_compose++;
            memcpy(buf, v->orig + off, k);
            for (uint32_t i = v->p_first; i < v->p_first + v->p_n; i++) {
                const Patch& p = patches_[i];
                if (p.tsc > t) continue;
                uint64_t plo = v->addr + p.off, phi = plo + p.len;
                uint64_t rlo = a > plo ? a : plo, rhi = (a + k) < phi ? (a + k) : phi;
                if (rlo < rhi) memcpy(buf + (rlo - a), p.bytes + (rlo - plo), (size_t)(rhi - rlo));
            }
        }
        r_obj++;
        return (int)k;
    }
    // trampolines: the slab is bump-allocated and never rewritten, so a trampoline's bytes are
    // the same at every time they can be executed and no time key is needed here.
    auto it = tpage_.find(a & ~0xfffull);
    if (it != tpage_.end()) {
        for (uint32_t i : it->second) {
            const Tramp& tr = tramps_[i];
            if (a < tr.addr || a >= tr.addr + tr.len) continue;
            size_t off = (size_t)(a - tr.addr);
            size_t k = std::min(n, (size_t)(tr.len - off));
            memcpy(buf, tr.bytes + off, k);
            r_tramp++;
            return (int)k;
        }
    }
    for (auto& s : slabs_) {
        if (a < s.lo || a >= s.hi) continue;
        size_t k = std::min(n, (size_t)(s.hi - a));
        memcpy(buf, s.bytes + (a - s.lo), k);
        r_slab++;
        return (int)k;
    }
    for (auto& b : bg_) {
        if (a < b.lo || a >= b.hi) continue;
        size_t k = std::min(n, (size_t)(b.hi - a));
        memcpy(buf, b.bytes + (a - b.lo), k);
        r_bg++;
        { uint64_t pg = a & ~0xfffull; bool seen = false;
          for (uint64_t x : bg_pages) if (x == pg) { seen = true; break; }
          if (!seen && bg_pages.size() < 32) bg_pages.push_back(pg); }
        return (int)k;
    }
    r_miss++;
    { uint64_t pg = a & ~0xfffull; bool seen = false;
      for (uint64_t x : miss_pages) if (x == pg) { seen = true; break; }
      if (!seen && miss_pages.size() < 32) miss_pages.push_back(pg); }
    return 0;
}

const uint8_t* JitTable::orig_at(uint64_t a, uint64_t tsc, size_t* avail) const {
    if (tsc == ~0ull) tsc = ~0ull - 1;
    const Ver* v = find_ver_const(a, tsc);
    if (!v || !v->orig) return nullptr;
    size_t off = (size_t)(a - v->addr);
    if (avail) *avail = v->len - off;
    return v->orig + off;
}

std::string JitTable::stats_json() const {
    char b[1400];
    snprintf(b, sizeof b,
        "{\"records\":%llu,\"added\":%llu,\"moved\":%llu,\"removed\":%llu,\"patched_windows\":%llu,"
        "\"trampolines\":%llu,\"slab_records\":%llu,\"versions\":%llu,\"bytecode_versions\":%llu,\"code_bytes\":%llu,"
        "\"dropped_file_backed\":%llu,\"orphan_patches\":%llu,\"moves_of_unknown_code\":%llu,"
        "\"superseded_versions\":%llu,\"truncated\":%llu,"
        "\"reads_object\":%llu,\"reads_trampoline\":%llu,\"reads_slab\":%llu,"
        "\"reads_anon_dump\":%llu,\"reads_unmapped\":%llu,\"reads_ambiguous\":%llu,"
        "\"reads_time_keyed\":%llu,\"reads_composed\":%llu,"
        "\"time_behind\":%llu,\"time_behind_max_tsc\":%llu}",
        (unsigned long long)n_records, (unsigned long long)n_added, (unsigned long long)n_moved,
        (unsigned long long)n_removed, (unsigned long long)n_patched, (unsigned long long)n_tramp_rec,
        (unsigned long long)n_slab_rec, (unsigned long long)n_versions, (unsigned long long)n_bytecode,
        (unsigned long long)code_bytes,
        (unsigned long long)n_dropped_file, (unsigned long long)n_orphan_patch,
        (unsigned long long)n_move_unknown, (unsigned long long)n_reused_addr,
        (unsigned long long)n_truncated,
        (unsigned long long)r_obj, (unsigned long long)r_tramp, (unsigned long long)r_slab,
        (unsigned long long)r_bg, (unsigned long long)r_miss, (unsigned long long)r_ambiguous,
        (unsigned long long)r_time_used, (unsigned long long)r_compose,
        (unsigned long long)t_behind, (unsigned long long)t_behind_max);
    std::string out(b);
    out.pop_back();
    auto pages = [](const std::vector<uint64_t>& v) {
        std::string r = "[";
        for (size_t i = 0; i < v.size(); i++) { if (i) r += ","; r += std::to_string(v[i]); }
        return r + "]";
    };
    out += ",\"native_overlay_records\":" + std::to_string(n_native_patch) +
           ",\"reads_native_overlay\":" + std::to_string(r_native_patch) +
           ",\"anon_dump_pages\":" + pages(bg_pages) + ",\"unmapped_pages\":" + pages(miss_pages) + "}";
    return out;
}
