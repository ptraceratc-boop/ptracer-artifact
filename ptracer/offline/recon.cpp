// recon.cpp -- see recon.h
#include <unordered_map>
#include "recon.h"
#include <cstring>
#include <chrono>
#include <algorithm>
#include <Zydis/Zydis.h>
#include <libvex_guest_offsets.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

static ZydisDecoder g_zd; static bool g_zd_init = false;
// Zydis register -> VEX guest offset (64-bit GPRs; size = operand register width in bytes)
static int zy_gpr_off(ZydisRegister r, int* size) {
    ZydisRegister big = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, r);
    static const int offs[16] = {OFFSET_amd64_RAX, OFFSET_amd64_RCX, OFFSET_amd64_RDX, OFFSET_amd64_RBX, OFFSET_amd64_RSP, OFFSET_amd64_RBP, OFFSET_amd64_RSI, OFFSET_amd64_RDI,
                                 OFFSET_amd64_R8, OFFSET_amd64_R9, OFFSET_amd64_R10, OFFSET_amd64_R11, OFFSET_amd64_R12, OFFSET_amd64_R13, OFFSET_amd64_R14, OFFSET_amd64_R15};
    if (big >= ZYDIS_REGISTER_RAX && big <= ZYDIS_REGISTER_R15) { if (size) *size = ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, r) / 8; return offs[big - ZYDIS_REGISTER_RAX]; }
    if (big == ZYDIS_REGISTER_RIP) { if (size) *size = 8; return OFFSET_amd64_RIP; }
    return -1;
}

Spec Spec::load(const std::string& path) {
    Spec s; if (path.empty()) return s; Json j = json_load(path);
    s.image = j["image"].str();
    for (auto& x : j["sites"].arr) { Site t; t.id = (int)x["id"].i64(); t.addr = x["addr"].u64(); t.when = x["when"].str(); t.kind = x["kind"].str(); t.reg = x["reg"].str(); t.size = (int)x["size"].i64(8);
        for (auto& r : x["regs"].arr) t.regs.push_back(r.str()); s.sites.push_back(t); }
    return s;
}
SiteMap SiteMap::load(const std::string& path) {
    SiteMap m; if (path.empty()) return m; Json j = json_load(path);
    m.present = true;
    m.sink = j["sink"].str(); if (m.sink.empty()) m.sink = "ptwrite";
    m.image = j["image"].str(); m.sync = j["sync"].u64();
    m.orig_image = j["orig_image"].str();
    for (auto& x : j["entries"].arr) { SiteMapEnt e;
        // SPEC_FORMAT section 2: `role' is "cv" (a critical value, consumed by
        // the reconstruction) or "gt" (a ground-truth address record of a
        // --gt-all build).  A gt logging instruction is ordinary instrumentation
        // as far as this file is concerned: it must produce no trace record,
        // consume no value, and change no state -- exactly what happens when it
        // is not in `sm_ent_'.
        if (x["role"].str() == "gt") { m.gt_sites++; continue; }
        e.tramp = x["tramp_addr"].u64(); e.site = (int)x["site"].i64(-1); e.orig = x["orig_addr"].u64();
        e.kind = x["kind"].str(); e.reg = x["reg"].str(); e.half = x["half"].str(); e.size = (int)x["size"].i64(0); e.payload_bits = (int)x["payload_bits"].i64(64);
        e.sink = x["sink"].str();      // mixed sink: per-entry override of the map's own `sink'
        e.delta = x["delta"].t == Json::BOOL ? x["delta"].b : false;
        if (e.delta) { m.delta = true; e.slot = x["slot"].u64(); e.cmp_addr = x["cmp_addr"].u64(); e.je_addr = x["je_addr"].u64(); e.join_addr = x["join_addr"].u64(); }
        e.kf_period = (long)x["keyframe"].i64(0);
        if (e.kf_period) { m.keyframe = true; e.counter = x["counter"].u64(); e.counter_gs = x["counter_gs"].i64(0) != 0; e.kf_branch = x["kf_branch_addr"].u64(); e.kf_join = x["kf_join_addr"].u64();
                           e.resync = x["resync"].t == Json::BOOL ? x["resync"].b : false; }
        m.entries.push_back(e); }
    for (auto& x : j["gt_site_addrs"].arr) m.gt_site_addrs.push_back(x.u64());
    for (auto& x : j["relocated"].arr) { RelocEnt r; r.tramp = x["tramp_addr"].u64(); r.orig = x["orig_addr"].u64(); r.len = (int)x["len"].i64(0); r.tramp_len = (int)x["tramp_len"].i64(0); m.relocated.push_back(r); }
    for (auto& x : j["sync_markers"].arr) m.sync_markers.push_back(x["tramp_addr"].u64());
    for (auto& x : j["trampolines"].arr) m.trampolines.push_back(x.u64());
    return m;
}

// Load every --spec / --sitemap group.  Each site map names the image it describes, so the groups
// are matched to the process's mappings by name rather than by position.
void Recon::load_specs() {
    for (auto& p : o_.specs) specs_.push_back(Spec::load(p));
    for (auto& p : o_.sitemaps) smaps_.push_back(SiteMap::load(p));
    // pre-rewrite ELF for an image: the explicit --orig-image of its group, else the site map's own
    // "orig_image" field (rewrite.py always records it).
    for (size_t i = 0; i < smaps_.size(); i++) {
        const std::string& img = smaps_[i].image;
        if (img.empty()) continue;
        std::string oi = (i < o_.orig_images.size() && !o_.orig_images[i].empty()) ? o_.orig_images[i] : smaps_[i].orig_image;
        if (!oi.empty()) orig_image_of_[img] = oi;
    }
    for (size_t i = 0; i < specs_.size(); i++) {
        const std::string& img = specs_[i].image;
        if (img.empty() || orig_image_of_.count(img)) continue;
        if (i < o_.orig_images.size() && !o_.orig_images[i].empty()) orig_image_of_[img] = o_.orig_images[i];
    }
    // A single --orig-image given with no matching image name (the M1 command line) applies to the
    // one spec/site map there is.
    if (orig_image_of_.empty() && o_.orig_images.size() == 1 && !o_.orig_images[0].empty()) {
        if (!smaps_.empty() && !smaps_[0].image.empty()) orig_image_of_[smaps_[0].image] = o_.orig_images[0];
        else if (!specs_.empty() && !specs_[0].image.empty()) orig_image_of_[specs_[0].image] = o_.orig_images[0];
    }
}

Recon::Recon(const ReconOptions& o) : o_(o) {
    if (!g_zd_init) { ZydisDecoderInit(&g_zd, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64); g_zd_init = true; }
    sb_ = Sideband::load(o.sideband);
    if (!o_.fs_base) o_.fs_base = sb_.fs_base;   // process-invariant TLS base from the sideband
    load_specs();
    tid_ = sb_.pid;                        // a per-task capture: the traced thread is the process
    // Original code regions: for each executable mapping, if the file is an ELF and the mapping lies inside
    // one of its PT_LOAD segments (by vaddr), it is original code; E9Patch's extra mappings (trampolines,
    // handlers, loader) fall outside the original PT_LOADs.
    //
    // This is done ONCE PER DISTINCT FILE, not once per mapping: whole-program CPython has ~17 000
    // mappings over ~15 files, and the old per-mapping loop (which also scanned all mappings again
    // for every PT_LOAD to find the bias) was O(#maps^2 * #loads) -- billions of iterations before
    // the first instruction was decoded.
    std::unordered_map<std::string, std::vector<size_t>> by_path;
    for (size_t i = 0; i < sb_.maps.size(); i++) by_path[sb_.maps[i].path].push_back(i);
    std::unordered_map<std::string, std::vector<std::pair<uint64_t, uint64_t>>> loads_of;
    // The FILE-offset ranges of the same PT_LOADs (for the load-base fallback below).
    std::unordered_map<std::string, std::vector<std::pair<uint64_t, uint64_t>>> floads_of;
    for (auto& kv : by_path) {
        const std::string& path = kv.first;
        bool any_exec = false; for (size_t i : kv.second) if (sb_.maps[i].exec()) any_exec = true;
        if (!any_exec) continue;
        // "Original code" = the ELF's own PT_LOADs.  When --orig-image is given for an image a
        // spec/site map describes, its (pre-rewrite) headers are used, because E9Patch APPENDS a
        // PT_LOAD for its own loader; that segment is instrumentation, not program code.  Without
        // --orig-image the same segment is recognised by `p_paddr != p_vaddr', which is how
        // E9Patch writes it and no toolchain writes a normal segment.
        std::string hdr_path = path;
        auto oi = orig_image_of_.find(path); if (oi != orig_image_of_.end()) hdr_path = oi->second;
        FILE* f = fopen(hdr_path.c_str(), "rb"); if (!f) continue;
        unsigned char eh[64]; if (fread(eh, 1, 64, f) != 64 || memcmp(eh, "\x7f" "ELF", 4)) { fclose(f); continue; }
        uint64_t phoff; uint16_t phentsize, phnum; memcpy(&phoff, eh + 32, 8); memcpy(&phentsize, eh + 54, 2); memcpy(&phnum, eh + 56, 2);
        // load bias: the mapping with file offset 0 gives (start - vaddr0) where vaddr0 = first PT_LOAD vaddr page
        uint64_t bias = 0; bool have_bias = false;
        std::vector<std::pair<uint64_t, uint64_t>> loads;
        for (int i = 0; i < phnum; i++) { unsigned char ph[56]; fseek(f, phoff + (uint64_t)i * phentsize, SEEK_SET); if (fread(ph, 1, 56, f) != 56) break;
            uint32_t type; memcpy(&type, ph, 4); if (type != 1) continue; uint64_t off, va, pa, fsz, msz; memcpy(&off, ph + 8, 8); memcpy(&va, ph + 16, 8); memcpy(&pa, ph + 24, 8); memcpy(&fsz, ph + 32, 8); memcpy(&msz, ph + 40, 8);
            if (pa != va) continue;                 // E9Patch's appended loader segment
            loads.push_back({va & ~0xfffull, (va + msz + 0xfff) & ~0xfffull});
            // ... and the same segment's FILE range.
            floads_of[path].push_back({off & ~0xfffull, (off + fsz + 0xfff) & ~0xfffull});
            if (!have_bias && off < 0x1000)
                for (size_t i2 : kv.second) { const MapEnt& mm = sb_.maps[i2]; if ((mm.off & ~0xfffull) == (off & ~0xfffull)) { bias = mm.start - (va & ~0xfffull); have_bias = true; break; } }
        }
        fclose(f);
        if (!have_bias) { for (size_t i2 : kv.second) if (sb_.maps[i2].off == 0) { bias = loads.empty() ? 0 : sb_.maps[i2].start - loads[0].first; have_bias = true; break; } }
        // Both rules above need a surviving mapping of the image's FIRST PT_LOAD at file offset 0
        // -- and for some E9Patch images there is none.  The loader MAP_FIXEDs the patched copy over the original text, and
        // pt_capture2's sideband gives the NEWEST executable mapping of a range, so when
        // the overlay covers the whole first segment the offset-0 entry is trimmed away.  The
        // dynamic loader rewritten with `--ld-so' is exactly that shape (so is libdl here): the
        // bias came out 0, every trampoline address then missed its mapping and the whole site map
        // was DROPPED ("addresses do not land in instrumentation mappings") -- silently costing
        // that image every logged value it had.
        // Any PT_LOAD will do, not only the first: a mapping whose FILE offset falls inside a
        // PT_LOAD's file range is a mapping of that segment, and E9Patch's own regions (the
        // trampolines and the patched copy) are APPENDED past every original segment, so they
        // cannot be mistaken for one.  The read-only data segment normally survives untouched.
        // Taking the FIRST (segment, mapping) pair is NOT sound, because a PAGE-ROUNDED file
        // range admits mappings that are not mappings of that segment.  Two ways, both worth the text/data gap (0x200000 with glibc):
        //   (a) two PT_LOADs SHARE A FILE PAGE whenever the second one's file offset is not
        //       page-aligned, which is the normal layout (ld-2.23.so: [0, 0x253b0) and
        //       [0x25bc0, 0x26fc0), so page 0x25000 belongs to both).  The loop scans segments
        //       low-to-high and credits the shared page to the LOWER one: bias 0x200000 too HIGH
        //       (ld-2.23.so, libdl).
        //   (b) the loader's reservation of the HOLE between the two segments is a mapping with a
        //       file offset of its own, which can land inside a segment's rounded range: bias
        //       0x200000 too LOW (libnss_dns, whose 2 MB reservation has offset 0x5000 = the
        //       first page of segment 1's [0x5000, 0x7000)).
        // Either way the whole site map was then dropped ("addresses do not land in
        // instrumentation mappings"): stores the runtime made and the reconstruction never
        // consumed, so the POSITIONAL cv cursor slipped until a sync marker realigned it.
        // So enumerate the candidates and take the one that explains the MOST of this image's
        // mappings -- a mapping is explained when it lies inside a PT_LOAD at `vaddr + bias' AND
        // its file offset agrees with its distance into that segment.  A wrong bias explains one
        // mapping (the one it was derived from); the right one explains every surviving segment.
        if (!have_bias) {
            const std::vector<std::pair<uint64_t, uint64_t>>& fl = floads_of[path];
            const size_t nl = std::min(loads.size(), fl.size());
            auto explains = [&](uint64_t b, const MapEnt& mm) {
                uint64_t mo = mm.off & ~0xfffull;
                for (size_t li = 0; li < nl; li++) {
                    if (mo < fl[li].first || mo >= fl[li].second) continue;
                    if (mm.start >= loads[li].first + b && mm.end <= loads[li].second + b &&
                        mm.start - (loads[li].first + b) == mo - fl[li].first) return true;
                }
                return false;
            };
            std::vector<uint64_t> cands;
            for (size_t li = 0; li < nl && cands.size() < 64; li++)
                for (size_t i2 : kv.second) {
                    const MapEnt& mm = sb_.maps[i2];
                    uint64_t mo = mm.off & ~0xfffull;
                    if (mo < fl[li].first || mo >= fl[li].second) continue;
                    uint64_t b = mm.start - (loads[li].first + (mo - fl[li].first));
                    if (std::find(cands.begin(), cands.end(), b) == cands.end()) { cands.push_back(b); if (cands.size() >= 64) break; }
                }
            long best = -1; uint64_t bestb = 0;
            for (uint64_t b : cands) {
                long sc = 0;
                for (size_t i2 : kv.second) if (explains(b, sb_.maps[i2])) sc++;
                if (sc > best) { best = sc; bestb = b; }
            }
            if (!cands.empty()) { bias = best > 0 ? bestb : cands[0]; have_bias = true; }
        }
        if (have_bias) bias_[path] = bias;
        // The rules above stop at the FIRST load base they find.  A sideband whose traced command exec'd more than once (`-- taskset
        // -c N <prog>' and friends) is a union over two address spaces, and an image mapped in both
        // -- a rewritten libc.so.6 reached through LD_LIBRARY_PATH always is, because the wrapper
        // links against it too -- then has TWO bases, of which the first is the DEAD one.  Collect
        // them all: the orig-code classification below uses every one (real program code must not
        // be mistaken for instrumentation just because it lives in the second space), and
        // build_site_map() falls back through them when its own sanity check rejects the first.
        {
            // Only the PRIMARY rule is replayed here -- one candidate per mapping of the image's
            // first PT_LOAD at file offset 0.  The file-range fallback is deliberately NOT replayed:
            // page-rounded PT_LOAD file ranges overlap, so it would invent a base a page off for
            // any image whose data segment is not page-congruent with its file offset.
            std::vector<uint64_t>& allb = biases_[path];
            if (!loads.empty())
                for (size_t i2 : kv.second) {
                    const MapEnt& mm = sb_.maps[i2];
                    if (mm.off != 0) continue;
                    uint64_t b = mm.start - loads[0].first;
                    if (std::find(allb.begin(), allb.end(), b) == allb.end()) allb.push_back(b);
                }
            if (have_bias) {   // whatever the rules above chose stays FIRST
                auto at = std::find(allb.begin(), allb.end(), bias);
                if (at == allb.end()) allb.insert(allb.begin(), bias);
                else std::rotate(allb.begin(), at, at + 1);
            }
            if (allb.size() > 1)
                fprintf(stderr, "warning: %s is mapped at %zu different load bases in this sideband "
                                "(the capture spans an execve; the maps of the dead address space are "
                                "still in it)\n", path.c_str(), allb.size());
        }
        loads_of[path] = loads;
        // PT_LOADs of the file AS IT IS ON DISK (used to read bytes back), which is the rewritten
        // file when the mapping names a rewritten image.
        if (!segs_.count(path)) { std::vector<Seg> v; FILE* f2 = fopen(path.c_str(), "rb"); if (f2) {
            unsigned char eh2[64]; uint64_t ph2off = 0; uint16_t ph2es = 0, ph2n = 0;
            if (fread(eh2, 1, 64, f2) == 64 && !memcmp(eh2, "\x7f" "ELF", 4)) { memcpy(&ph2off, eh2 + 32, 8); memcpy(&ph2es, eh2 + 54, 2); memcpy(&ph2n, eh2 + 56, 2); }
            for (int i = 0; i < ph2n; i++) { unsigned char ph[56]; fseek(f2, ph2off + (uint64_t)i * ph2es, SEEK_SET); if (fread(ph, 1, 56, f2) != 56) break; uint32_t type; memcpy(&type, ph, 4); if (type != 1) continue; Seg sg; memcpy(&sg.off, ph + 8, 8); memcpy(&sg.vaddr, ph + 16, 8); memcpy(&sg.filesz, ph + 32, 8); v.push_back(sg); } fclose(f2); } segs_[path] = v; }
    }
    for (auto& m : sb_.maps) {
        if (!m.exec()) continue;
        auto lit = loads_of.find(m.path); if (lit == loads_of.end()) continue;
        // Against EVERY load base of this file, not only the first one found.  With
        // two address spaces in the sideband the second space's real program text would otherwise
        // be classified INSTR, and the reconstructor skips instrumentation.
        std::vector<uint64_t> mbias;
        { auto bit = biases_.find(m.path); if (bit != biases_.end()) mbias = bit->second; }
        if (mbias.empty()) mbias.push_back(bias_.count(m.path) ? bias_[m.path] : 0);
        bool orig = false;
        for (uint64_t bias : mbias)
            for (auto& l : lit->second) if (m.start >= l.first + bias && m.end <= l.second + bias) orig = true;
        // e9patch: the original .text page is mapped from a different file offset but at the original vaddr -> still "orig"
        if (orig) orig_code_.push_back({m.start, m.end});
        if (o_.verbose > 1) fprintf(stderr, "map %#lx-%#lx %s off=%#lx %s\n", (unsigned long)m.start, (unsigned long)m.end, m.path.c_str(), (unsigned long)m.off, orig ? "ORIG" : "INSTR");
    }
    build_orig_index();
    build_imap();
    // build_site_map() FIRST: build_tramp_map() now snaps the origin of a trampoline entry to a
    // known ORIGINAL instruction address, and the site map is where those come from.
    build_site_map();
    build_tramp_map();
    gt_open();
    for (auto& m : sb_.maps) { ImgStat is; is.path = m.path; is.lo = m.start; is.hi = m.end; is.orig = in_orig_code(m.start); imgs_.push_back(is); }
    std::sort(imgs_.begin(), imgs_.end(), [](const ImgStat& a, const ImgStat& b) { return a.lo < b.lo; });
}

// Paint the executable mappings into a disjoint sorted interval list; a later mapping wins over an
// earlier one on the bytes they share (that is the process's own semantics: MAP_FIXED overwrites).
void Recon::build_imap() {
    std::vector<IMap> cur;
    for (auto& m : sb_.maps) {
        if (!m.exec()) continue;
        std::vector<IMap> next; next.reserve(cur.size() + 2);
        for (auto& e : cur) {
            if (e.hi <= m.start || e.lo >= m.end) { next.push_back(e); continue; }
            if (e.lo < m.start) next.push_back({e.lo, m.start, e.m});
            if (e.hi > m.end)   next.push_back({m.end, e.hi, e.m});
        }
        next.push_back({m.start, m.end, &m});
        std::sort(next.begin(), next.end(), [](const IMap& a, const IMap& b) { return a.lo < b.lo; });
        cur.swap(next);
    }
    imap_.swap(cur);
    if (o_.verbose) fprintf(stderr, "mappings: %zu executable -> %zu disjoint intervals\n",
                            [&]{ size_t n = 0; for (auto& m : sb_.maps) if (m.exec()) n++; return n; }(), imap_.size());
}

const MapEnt* Recon::map_at(uint64_t addr) const {
    if (imap_.empty()) return nullptr;
    size_t i = imap_last_;                        // locality: the previous read is usually the same interval
    if (i < imap_.size() && addr >= imap_[i].lo && addr < imap_[i].hi) return imap_[i].m;
    size_t lo = 0, hi = imap_.size();
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (imap_[mid].lo <= addr) lo = mid + 1; else hi = mid; }
    if (!lo) return nullptr;
    const IMap& e = imap_[lo - 1];
    if (addr >= e.hi) return nullptr;
    imap_last_ = lo - 1;
    return e.m;
}

// orig_code_ coalesced into a sorted, disjoint list so in_orig_code() is a binary search: it used
// to be a linear scan and is called for every newly decoded instruction and every site-map entry.
void Recon::build_orig_index() {
    orig_sorted_ = orig_code_;
    std::sort(orig_sorted_.begin(), orig_sorted_.end(), [](const Range& a, const Range& b) { return a.lo < b.lo; });
    std::vector<Range> out;
    for (auto& r : orig_sorted_) { if (!out.empty() && r.lo <= out.back().hi) { if (r.hi > out.back().hi) out.back().hi = r.hi; } else out.push_back(r); }
    orig_sorted_.swap(out);
}

// The log-on-change table.  Everything else the constructor builds is a property of the PROCESS;
// this one depends on WHICH CHUNK of the trace is being reconstructed (a chunk that does not start
// at byte 0 inherits nothing), so it is the one thing retarget() has to redo.
void Recon::init_delta_table() {
    delta_val_.assign(flat_ent_.size(), 0);
    delta_valid_.assign(flat_ent_.size(), o_.skip_bytes ? 0 : 1);
    // ... unless the parent computed the EXACT table for this chunk's start (--delta-scan).
    if (!o_.delta_seed_in.empty()) {
        FILE* f = fopen(o_.delta_seed_in.c_str(), "rb");
        if (!f) { fprintf(stderr, "ptrecon: cannot open delta seed %s\n", o_.delta_seed_in.c_str()); }
        else {
            uint64_t n = 0;
            if (fread(&n, 8, 1, f) == 1 && n == flat_ent_.size()) {
                std::vector<uint64_t> v(n); std::vector<uint8_t> ok(n);
                if (fread(v.data(), 8, n, f) == n && fread(ok.data(), 1, n, f) == n) {
                    size_t nv = 0;
                    for (uint64_t i = 0; i < n; i++) { delta_val_[i] = v[i]; delta_valid_[i] = ok[i] ? 1 : 0; nv += ok[i] ? 1 : 0; }
                    if (o_.verbose) fprintf(stderr, "delta seed: %zu of %lu entries known\n", nv, (unsigned long)n);
                }
            } else fprintf(stderr, "ptrecon: delta seed %s does not match this site map\n", o_.delta_seed_in.c_str());
            fclose(f);
        }
    }
}

// --jobs: point an already-built Recon at one chunk of the same trace.
// Every chunk child used to construct its OWN Recon, which re-parsed both site-map JSONs, rebuilt
// the interval map over ~17 000 mappings and re-read the whole cv stream -- per worker.  The
// constructor's work is a function of the PROCESS being reconstructed, not of the chunk, so the
// parent now builds it once and the children inherit it through fork(): the same bytes, physically
// shared copy-on-write, and no repeated setup.  Only the options that NAME the chunk change, plus
// the one table (init_delta_table) that depends on them.  Nothing here touches the machine state,
// the caches or the counters: the parent never runs, so a child forks from a pristine Recon.
void Recon::retarget(const ReconOptions& co) {
    o_.jobs = co.jobs;
    o_.skip_bytes = co.skip_bytes; o_.end_bytes = co.end_bytes; o_.emit_from = co.emit_from;
    o_.seed_fs = co.seed_fs; o_.delta_seed_in = co.delta_seed_in;
    o_.out = co.out; o_.summary = co.summary; o_.text = co.text;
    o_.anchor_scan_out = co.anchor_scan_out; o_.delta_scan_out = co.delta_scan_out;
    o_.scan_snap_at = co.scan_snap_at;
    init_delta_table();
}

// ---------------------------------------------------------------------------------------------
// Which captures can be reconstructed in parallel at all.
//
// A chunk that does not start at byte 0 begins with an ALL-UNKNOWN machine state and, with the
// buffer sink, an unknown position in the positional value stream.  Two things in the stream can
// repair that: a KEYFRAME (a guarded site that logs its registers every K-th execution --
// SPEC_FORMAT section 3) re-defines the registers, and a SYNC MARKER (`ptwrite <running count>')
// re-aligns the cursor.  A build with neither can only be reconstructed from its own beginning:
// there is no point in the trace at which a second worker could join.
bool Recon::can_split(std::string& why) const {
    if (!n_kf_values_) {
        why = "this build has no keyframe sites (rewrite.py --keyframe K), so no mid-trace chunk "
              "can re-anchor its registers";
        return false;
    }
    bool buffer = false;
    for (const auto& e : flat_ent_) if (e.sink.empty() ? e.buffer : (e.sink == "buffer")) { buffer = true; break; }
    if (buffer && sm_sync_.empty()) {
        why = "this build uses the buffer sink with no sync markers (rewrite.py --sync 0), so no "
              "mid-trace chunk can re-align the positional value cursor";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// --jobs round 0: where may a chunk START?
//
// Decode [skip_bytes, end_bytes) and stop at the first instruction by which BOTH re-anchors have
// happened, with no state loss since either of them:
//   * a keyframe guard fired (its `jnz' did not land on the join label) AND the keyframe's own
//     logging site was then reached, which is where the register is re-defined;
//   * a buffer-sink sync marker's `ptwrite' payload was consumed, which is where the value cursor
//     is re-aligned.
// The decoder's own third requirement -- a PSB -- is handled by the caller, which starts every
// chunk at a PSB at or before the offset reported here.
//
// Nothing here depends on the machine state: both events are instruction-stream facts, so the
// answer is the same as it would be in a serial run.  No interpretation, no shadow, no records.
int Recon::run_anchor_scan() {
    PtDecoder dec(o_.aux, sb_, o_.skip_bytes, o_.end_bytes);
    dec.no_time = true;
    const bool need_sync = !sm_sync_.empty();
    uint64_t kf_off = 0, sync_off = 0, anchor = 0, n_kf = 0, n_sync = 0;
    long pend_kf = -1; uint64_t fired = 0; bool pend_sync = false;
    std::deque<uint64_t> q;
    PtEvents ev;
    ev.on_ptwrite = [&](uint64_t, int, uint64_t) {
        if (pend_sync) { pend_sync = false; sync_off = dec.offset(); n_sync++; return; }
        q.push_back(1);
    };
    // A PT overflow or a decoder resync throws the registers and the cursor away again: an anchor
    // before it does not carry across it.
    auto loss = [&]() { kf_off = sync_off = 0; pend_kf = -1; fired = 0; pend_sync = false; q.clear(); };
    ev.on_overflow = [&](uint64_t) { n_ovf_++; loss(); };
    ev.on_resync = [&](int, uint64_t) { loss(); };
    ev.on_enable = [&](uint64_t, bool) {};
    struct pt_insn insn; uint64_t tsc = 0;
    while (dec.next(insn, &tsc, ev)) {
        const uint64_t ip = insn.ip;
        if (pend_kf >= 0) { long idx = pend_kf; pend_kf = -1;
                            fired = (ip != flat_ent_[idx].kf_join) ? flat_ent_[idx].kf_branch : 0; }
        if (!in_orig_code(ip)) {
            auto kf = sm_kf_.find(ip);
            if (kf != sm_kf_.end()) pend_kf = (long)kf->second;
            else {
                auto se = sm_ent_.find(ip);
                if (se != sm_ent_.end()) {
                    const SiteMapEnt& e = flat_ent_[se->second];
                    if (e.kf_period && fired && e.kf_branch == fired) { kf_off = dec.offset(); n_kf++; }
                } else if (need_sync && sm_sync_.count(ip)) {
                    if (!q.empty()) { q.pop_front(); sync_off = dec.offset(); n_sync++; }
                    else pend_sync = true;
                }
            }
        }
        if (kf_off && (sync_off || !need_sync)) { anchor = dec.offset(); break; }
    }
    FILE* f = fopen(o_.anchor_scan_out.c_str(), "w");
    if (!f) { perror("anchor-scan-out"); return 1; }
    fprintf(f, "%d %lu %lu %lu %lu %lu %lu\n", anchor ? 1 : 0, (unsigned long)anchor,
            (unsigned long)o_.skip_bytes, (unsigned long)kf_off, (unsigned long)sync_off,
            (unsigned long)n_kf, (unsigned long)dec.n_insn);
    fclose(f);
    if (o_.verbose) fprintf(stderr, "anchor scan [%#lx,%#lx): anchor %#lx after %lu instructions\n",
                            (unsigned long)o_.skip_bytes, (unsigned long)o_.end_bytes,
                            (unsigned long)anchor, (unsigned long)dec.n_insn);
    return 0;
}

// SPEC_FORMAT section 2: site-map addresses are LINK-TIME vaddrs (offsets from the load base for
// a PIE, absolute for a non-PIE).  Add the runtime load base of the rewritten image, which is the
// bias computed from the sideband maps for the site map's own `image' (falling back to the spec's
// image, then to the first executable mapping).
void Recon::build_site_map() {
    smap_bases_.assign(smaps_.size(), 0);
    for (size_t mi = 0; mi < smaps_.size(); mi++) {
        SiteMap& sm = smaps_[mi];
        if (!sm.present) continue;
        // An image that is not mapped at all (an extension module this run never imported) is not
        // an error: the map is simply not needed.  Only a map whose image IS mapped but whose
        // rebased addresses do not land in instrumentation is a real disagreement.
        bool mapped = false;
        for (auto& m : sb_.maps) if (m.path == sm.image) { mapped = true; break; }
        if (!mapped) { sm.mapped = false;
            if (o_.verbose) fprintf(stderr, "site map %zu (%s): image not mapped in this run -- not needed\n", mi, sm.image.c_str());
            continue; }
        sm.mapped = true;
        uint64_t base0 = 0;
        auto it = bias_.find(sm.image);
        if (it != bias_.end()) base0 = it->second;
        if (!base0 && mi < specs_.size()) { auto i2 = bias_.find(specs_[mi].image); if (i2 != bias_.end()) base0 = i2->second; }
        if (!base0 && smaps_.size() == 1 && !bias_.empty()) {   // last resort: the lowest-mapped executable image
            uint64_t best = ~0ull; for (auto& m : sb_.maps) if (m.exec() && bias_.count(m.path) && m.start < best) { best = m.start; base0 = bias_[m.path]; }
        }
        // `base0' is whichever load base of this image the sideband mentions FIRST, and in a
        // capture that spans an execve that is the base of the address space the exec destroyed.
        // Honouring it would attribute the image's logged values to the dead space's pages --
        // which is exactly what the sanity check at the end of this loop body exists to stop, so
        // the map would be thrown away whole.  Try the other load bases the sideband offers
        // before giving up on the map.
        std::vector<uint64_t> cands; cands.push_back(base0);
        { auto bit = biases_.find(sm.image);
          if (bit != biases_.end()) for (uint64_t b : bit->second)
              if (std::find(cands.begin(), cands.end(), b) == cands.end()) cands.push_back(b); }
        uint64_t base = base0;
        for (size_t ci = 0; ci < cands.size(); ci++) {
        base = cands[ci];
        const bool last_cand = (ci + 1 == cands.size());
        smap_bases_[mi] = base;
        const bool buffer = (sm.sink == "buffer");
        size_t first = flat_ent_.size();
        for (auto& e : sm.entries) { SiteMapEnt c = e; c.tramp += base; c.orig += base; c.map = (int)mi; c.buffer = (e.sink.empty() ? buffer : e.sink == "buffer");
            if (c.delta) { c.slot += base; c.cmp_addr += base; c.je_addr += base; c.join_addr += base;
                           sm_je_[c.je_addr] = (uint32_t)flat_ent_.size(); n_delta_values_++; }
            // --kf-gs: the counter is a %gs displacement into the thread's OWN
            // array, not an address in this image -- rebasing it would be
            // meaningless.
            if (c.kf_period) { if (!c.counter_gs) c.counter += base; c.kf_branch += base; c.kf_join += base;
                               // Several logged values of a resync SITE share one counter and one
                               // branch; the map keeps the first, which is all the counting needs.
                               sm_kf_.emplace(c.kf_branch, (uint32_t)flat_ent_.size());
                               n_kf_values_++; if (c.resync) n_kf_resync_values_++; }
            sm_ent_[c.tramp] = (uint32_t)flat_ent_.size(); flat_ent_.push_back(c); }
        for (auto& r : sm.relocated) { RelocEnt c = r; c.tramp += base; c.orig += base; sm_rel_[c.tramp] = c; }
        for (auto& a : sm.sync_markers) sm_sync_.insert(a + base);
        for (auto& a : sm.trampolines) sm_tramp_.insert(a + base);
        // Sanity: every logging/relocated address must land in an INSTRUMENTATION mapping, otherwise
        // the base is wrong and honouring the map would corrupt the trace (this is how the old no-op
        // bug hid).  A sample is enough at CPython scale, and it is exact for small maps.
        uint64_t bad = 0, tot = 0;
        auto chk = [&](uint64_t a) { tot++; const MapEnt* m = map_at(a); if (!m || in_orig_code(a)) bad++; };
        for (size_t k = first; k < flat_ent_.size(); k++) chk(flat_ent_[k].tramp);
        for (auto& r : sm.relocated) chk(r.tramp + base);
        bool ok = (tot > 0 && bad == 0);
        // A candidate that fails is not a verdict on the map -- only the LAST one is.
        if (!ok && !last_cand) {
            if (o_.verbose)
                fprintf(stderr, "site map %zu (%s): base=%#lx rejected (%lu of %lu addresses miss "
                                "instrumentation) -- trying the next load base\n",
                        mi, sm.image.c_str(), (unsigned long)base, (unsigned long)bad, (unsigned long)tot);
        } else {
        if (ok) n_smap_ok_++; else n_smap_bad_++;
        sm.applied = ok;
        if (o_.verbose || !ok || ci > 0)
            fprintf(stderr, "site map %zu (%s): base=%#lx, %zu logged values (%zu ptwrite / %zu buffer), %zu relocated instructions, %zu sync markers, sink=%s%s\n",
                    mi, sm.image.c_str(), (unsigned long)base, sm.entries.size(),
                    sm.entries.size() - (size_t)std::count_if(sm.entries.begin(), sm.entries.end(), [&](const SiteMapEnt& e){ return e.sink.empty() ? buffer : e.sink == "buffer"; }),
                    (size_t)std::count_if(sm.entries.begin(), sm.entries.end(), [&](const SiteMapEnt& e){ return e.sink.empty() ? buffer : e.sink == "buffer"; }),
                    sm.relocated.size(), sm.sync_markers.size(), sm.sink.c_str(),
                    ok ? (ci ? "  [second load base: the sideband spans an execve]" : "")
                       : "  [IGNORED: addresses do not land in instrumentation mappings]");
        }
        if (!ok) {   // roll this map back out of the global tables
            for (size_t k = first; k < flat_ent_.size(); k++) { sm_ent_.erase(flat_ent_[k].tramp);
                if (flat_ent_[k].delta) { sm_je_.erase(flat_ent_[k].je_addr); n_delta_values_--; }
                if (flat_ent_[k].kf_period) { sm_kf_.erase(flat_ent_[k].kf_branch); n_kf_values_--;
                                              if (flat_ent_[k].resync) n_kf_resync_values_--; } }
            flat_ent_.resize(first);
            for (auto& r : sm.relocated) sm_rel_.erase(r.tramp + base);
            for (auto& a : sm.sync_markers) sm_sync_.erase(a + base);
            for (auto& a : sm.trampolines) sm_tramp_.erase(a + base);
        }
        if (ok) break;                      // this base is the live one; keep it
        }                                   // ... otherwise try the next load base
        if (!sm.applied) { base = base0; smap_bases_[mi] = base0; }   // no candidate worked: report as before
        // SAME-PROCESS GROUND TRUTH: rebase the gt site addresses of this image.  Done only once
        // the base is settled, so a rejected candidate leaves nothing behind.
        for (uint64_t a : sm.gt_site_addrs) gt_sites_.insert(a + base);
        // Everything else that keys off this image's load base must agree with the base actually
        // adopted, or the bytes read back for a page would come from the wrong address space.
        if (sm.applied && base != base0) bias_[sm.image] = base;
    }
    // The runtime's cache slots are mapped zero-filled, so the table starts at zero AND VALID:
    // a site whose first logged value is 0 legitimately logs nothing.
    //
    // ...but ONLY for a reconstruction that starts at the beginning of the trace.  A chunk of a
    // `--jobs N' run (and any `--skip-bytes' decode) starts mid-execution, where the runtime's
    // slots hold whatever the program logged before -- values this chunk has not seen.  Starting
    // that table at "zero and valid" makes every log-on-change guard that SKIPS before its site is
    // re-logged hand out a silently WRONG value, with `delta_unknown' = 0, i.e. not one of those
    // reads flagged.  A chunk therefore starts with the table INVALID and re-validates it exactly
    // the way a PT state loss does: from the chunk's warm-up, from the delta keyframes and from
    // ordinary value changes.  Reads that arrive first come out as unknown addresses, which is the
    // truth, and `delta_unknown' counts them.
    init_delta_table();
    if (n_delta_values_)
        fprintf(stderr, "delta logging: %lu of %zu logged values are log-on-change "
                        "(one shared cache slot each -- sound only for a single-threaded target)\n",
                (unsigned long)n_delta_values_, flat_ent_.size());
    if (n_kf_values_)
        fprintf(stderr, "keyframe logging: %lu of %zu logged values are logged every K-th execution "
                        "(%lu of them at `resync' loop-header sites), %zu counter guards\n",
                (unsigned long)n_kf_values_, flat_ent_.size(), (unsigned long)n_kf_resync_values_, sm_kf_.size());
    // A DROPPED site map is not a cosmetic loss.  Its image keeps executing its
    // instrumentation, keeps writing values into the thread's buffer and keeps emitting sync
    // markers -- and the reconstruction consumes none of them, so the POSITIONAL cv cursor runs
    // behind and every value it hands out afterwards belongs to another site until a marker
    // pulls it straight.  Say so where it cannot be missed.
    for (size_t mi = 0; mi < smaps_.size(); mi++) {
        const SiteMap& sm = smaps_[mi];
        if (!sm.present || !sm.mapped || sm.applied) continue;
        size_t nbuf = 0;
        const bool dflt = (sm.sink == "buffer");
        for (const auto& e : sm.entries) if (e.sink.empty() ? dflt : e.sink == "buffer") nbuf++;
        fprintf(stderr, "ptrecon: WARNING: the site map for %s was DROPPED -- its %zu logged "
                "values (%zu of them buffer-sink) and %zu sync markers are INVISIBLE to this "
                "reconstruction.  With the buffer sink that makes the positional value cursor "
                "SLIP: the image still writes those values and the replay never consumes them "
                "\n",
                sm.image.c_str(), sm.entries.size(), nbuf, sm.sync_markers.size());
    }
    smap_ok_ = (n_smap_ok_ > 0);
    smap_base_ = smap_bases_.empty() ? 0 : smap_bases_[0];
    // Site maps were given and NONE applies: every logged value of the build is invisible to this
    // reconstruction, which then silently degrades to unknown addresses (and, against a ground
    // truth, to a comparison in which nothing is a gt site).  A wrong `image' path in the map or
    // a sideband of a different build are the usual causes.  Said here unconditionally; run()
    // refuses outright when a ground truth is being compared.
    if (!smaps_.empty() && n_smap_ok_ == 0) {
        int n_absent = 0; for (auto& m : smaps_) if (m.present && !m.mapped) n_absent++;
        fprintf(stderr, "ptrecon: WARNING: none of the %zu site map(s) applies to this capture "
                        "(%d for images this run did not map, %d rejected): no logged value can be "
                        "consumed -- check the maps' \"image\" paths against the sideband\n",
                smaps_.size(), n_absent, n_smap_bad_);
    }
    if (smaps_.size() > 1) {
        int n_absent = 0; for (auto& m : smaps_) if (m.present && !m.mapped) n_absent++;
        fprintf(stderr, "site maps: %d applied, %d ignored, %d for images this run did not map, "
                        "%zu logged values, %zu relocated instructions total\n",
                n_smap_ok_, n_smap_bad_, n_absent, flat_ent_.size(), sm_rel_.size());
    }
}

const std::vector<uint8_t>* Recon::orig_bytes_at(uint64_t addr) {
    auto it = orig_raw_.find(addr); if (it != orig_raw_.end()) return it->second.empty() ? nullptr : &it->second;
    std::vector<uint8_t> v(16); bool ok = false;
    for (int n = 16; n >= 1; n--) { v.resize(n); if (read_orig_bytes(addr, v.data(), n)) { ok = true; break; } }
    if (!ok) v.clear();
    auto& slot = orig_raw_.emplace(addr, std::move(v)).first->second;
    return slot.empty() ? nullptr : &slot;
}

const std::vector<uint8_t>* Recon::file_bytes(const std::string& path) {
    auto it = file_cache_.find(path); if (it != file_cache_.end()) return &it->second;
    std::vector<uint8_t> v; FILE* f = fopen(path.c_str(), "rb"); if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); v.resize(sz); if (sz && fread(v.data(), 1, sz, f) != (size_t)sz) { fclose(f); return nullptr; } fclose(f);
    return &(file_cache_[path] = std::move(v));
}
// The bytes of one page, as a pointer into the cached file image.  This is what makes byte reads
// O(1) after the first touch of a page: the interval lookup and the PT_LOAD walk happen once per
// 4 KiB, not once per read (defect D-M3-9).  `avail' is how many bytes are valid from the page
// start (a page can be cut short by the end of a mapping or of a segment).
const Recon::PageRef* Recon::page_ref(uint64_t addr, bool orig) {
    const uint64_t pg = addr & ~0xfffull;
    auto& cache = orig ? page_orig_ : page_map_;
    auto it = cache.find(pg);
    if (it != cache.end()) return it->second.p ? &it->second : nullptr;
    PageRef pr;
    const MapEnt* m = map_at(pg);
    if (m) {
        std::string path = m->path;
        uint64_t bias = bias_.count(path) ? bias_[path] : 0;
        if (orig) { auto oi = orig_image_of_.find(path); if (oi != orig_image_of_.end()) path = oi->second; }
        if (orig && !segs_.count(path)) {   // an --orig-image that is not itself mapped: parse its phdrs
            std::vector<Seg> v; const std::vector<uint8_t>* fc = file_bytes(path);
            if (fc && fc->size() >= 64) {
                uint64_t phoff; uint16_t phentsize, phnum; memcpy(&phoff, fc->data() + 32, 8); memcpy(&phentsize, fc->data() + 54, 2); memcpy(&phnum, fc->data() + 56, 2);
                for (int i = 0; i < phnum; i++) { if (phoff + (uint64_t)(i + 1) * phentsize > fc->size()) break; const uint8_t* ph = fc->data() + phoff + (uint64_t)i * phentsize;
                    uint32_t type; memcpy(&type, ph, 4); if (type != 1) continue; Seg sg; memcpy(&sg.off, ph + 8, 8); memcpy(&sg.vaddr, ph + 16, 8); memcpy(&sg.filesz, ph + 32, 8); v.push_back(sg); }
            }
            segs_[path] = v;
        }
        const std::vector<uint8_t>* fc = file_bytes(path);
        if (fc) {
            if (orig) {
                // The ORIGINAL program's bytes, addressed through the file's PT_LOADs.
                for (auto& sg : segs_[path]) { uint64_t va = sg.vaddr + bias;
                    if (pg >= va && pg < va + sg.filesz) { uint64_t off = sg.off + (pg - va);
                        if (off < fc->size()) { pr.p = fc->data() + off; pr.avail = (size_t)std::min<uint64_t>(0x1000, std::min<uint64_t>(fc->size() - off, va + sg.filesz - pg)); }
                        break; } }
            } else {
                // The bytes as MAPPED at run time (E9Patch's patched pages and trampolines).
                uint64_t off = m->off + (pg - m->start);
                if (off < fc->size()) { pr.p = fc->data() + off; pr.avail = (size_t)std::min<uint64_t>(0x1000, std::min<uint64_t>(fc->size() - off, m->end - pg)); }
            }
        }
    }
    auto& slot = cache.emplace(pg, pr).first->second;
    return slot.p ? &slot : nullptr;
}

bool Recon::read_mapped_bytes(uint64_t addr, uint8_t* out, int n) {
    const PageRef* pr = page_ref(addr, false);
    size_t o = (size_t)(addr & 0xfff);
    if (pr && o + (size_t)n <= pr->avail) { memcpy(out, pr->p + o, n); return true; }
    // spans a page (or a mapping) boundary: byte-wise through the page cache
    for (int i = 0; i < n; i++) {
        const PageRef* q = page_ref(addr + i, false);
        size_t oo = (size_t)((addr + i) & 0xfff);
        if (!q || oo >= q->avail) return false;
        out[i] = q->p[oo];
    }
    return true;
}
// Original (pre-rewrite) bytes: from the group's --orig-image / the site map's `orig_image' when the
// mapping names a rewritten image, else from that file's own PT_LOAD program headers (E9Patch keeps
// the original segments in the file; only the runtime mapping is redirected).
bool Recon::read_orig_bytes(uint64_t addr, uint8_t* out, int n) {
    const PageRef* pr = page_ref(addr, true);
    size_t o = (size_t)(addr & 0xfff);
    if (pr && o + (size_t)n <= pr->avail) { memcpy(out, pr->p + o, n); return true; }
    for (int i = 0; i < n; i++) {
        const PageRef* q = page_ref(addr + i, true);
        size_t oo = (size_t)((addr + i) & 0xfff);
        if (!q || oo >= q->avail) return false;
        out[i] = q->p[oo];
    }
    return true;
}
// Map every E9Patch trampoline entry T to the original instruction address A it displaces: the rewritten code
// holds `jmp T` at A (E9Patch also redirects direct call/jump sites straight to T, so the jmp is not always
// executed and the stream alone cannot tell A). Sources: spec sites (exact), then a byte scan of the original
// code mappings for `e9 rel32` whose target lands in an instrumentation mapping.
// WHERE A TRAMPOLINE CAME FROM, TO THE BYTE.
//
// The fallback cursor (see run()) walks the ORIGINAL instruction stream from the address this map
// gives for a trampoline entry, so that address has to be an instruction BOUNDARY.  The generic
// scan below finds the patch jumps by looking for the byte `e9' and used to record the address OF
// THAT BYTE as the origin -- but E9Patch's patch jump is frequently the REX-punned `48 e9 <rel32>'
// or otherwise prefixed, and then the `e9' sits 1-3 bytes INTO the instruction.  The cursor would
// then start 1-3 bytes past the real site and every record the trampoline produced would come out
// at an ip that is not an instruction boundary at all.
//
// Both loops therefore snap the origin back to the nearest ORIGINAL instruction address the site map
// knows (every `entries[].orig_addr' and every `relocated[].orig_addr' -- 200 k boundaries per
// image, and by construction they cover exactly the patched sites), and the per-site loop accepts a
// prefixed `e9' instead of only a bare one.
void Recon::build_tramp_map() {
    auto is_instr = [&](uint64_t a) { return map_at(a) != nullptr && !in_orig_code(a); };
    // Known ORIGINAL instruction boundaries, from the site maps (build_site_map() has run).
    std::vector<uint64_t> starts;
    starts.reserve(flat_ent_.size() + sm_rel_.size());
    for (auto& e : flat_ent_) if (e.orig) starts.push_back(e.orig);
    for (auto& kv : sm_rel_) if (kv.second.orig) starts.push_back(kv.second.orig);
    for (auto& sp : specs_) { uint64_t sbias = bias_.count(sp.image) ? bias_[sp.image] : 0;
                              for (auto& st : sp.sites) starts.push_back(st.addr + sbias); }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    // The greatest known instruction start <= p, if it is within `kSnap' bytes (a patch jump is at
    // most a few prefixes long); otherwise p itself, which is the old behaviour.
    const uint64_t kSnap = 8;
    auto snap = [&](uint64_t p) -> uint64_t {
        auto it = std::upper_bound(starts.begin(), starts.end(), p);
        if (it == starts.begin()) return p;
        uint64_t a = *(it - 1);
        return (p - a <= kSnap) ? a : p;
    };
    for (auto& sp : specs_) {
        uint64_t sbias = bias_.count(sp.image) ? bias_[sp.image] : 0;
        for (auto& st : sp.sites) {
            uint8_t b[9]; uint64_t A = st.addr + sbias;
            if (!read_mapped_bytes(A, b, 9)) continue;
            for (int j = 0; j < 4; j++) {          // bare `e9', or one to three prefix bytes first
                if (b[j] != 0xe9) continue;
                int32_t rel; memcpy(&rel, b + j + 1, 4);
                uint64_t T = A + j + 5 + (int64_t)rel;
                if (is_instr(T)) tramp_entry_[T] = A;     // the SITE address: already a boundary
                break;
            }
        }
    }
    for (auto& r : orig_code_) {
        std::vector<uint8_t> buf(r.hi - r.lo); if (!read_mapped_bytes(r.lo, buf.data(), buf.size())) continue;
        for (size_t i = 0; i + 5 <= buf.size(); i++) if (buf[i] == 0xe9) { int32_t rel; memcpy(&rel, &buf[i + 1], 4); uint64_t T = r.lo + i + 5 + (int64_t)rel; if (is_instr(T) && !tramp_entry_.count(T)) tramp_entry_[T] = snap(r.lo + i); }
    }
    if (o_.verbose) fprintf(stderr, "trampoline entries: %zu (%zu known original instruction boundaries)\n", tramp_entry_.size(), starts.size());
}

bool Recon::in_orig_code(uint64_t ip) const {
    size_t lo = 0, hi = orig_sorted_.size();
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (orig_sorted_[mid].lo <= ip) lo = mid + 1; else hi = mid; }
    return lo > 0 && ip < orig_sorted_[lo - 1].hi;
}

// A trampoline the site map does not describe.  E9Patch displaces instructions on its own account
// (its T2/T3 "eviction" tactics), and a stale or partial site map can miss those; rather than
// silently dropping their accesses, fall back to the byte-matching cursor for that trampoline and
// count it (`tramp_unknown_origin' in the summary).  runtime/rewrite.py maps evictions, so this
// should stay at 0 -- a non-zero count means the site map and the binary disagree.
// NOTE: run() no longer calls this -- it uses the memoised sm_unmapped() (recon.h), which mirrors
// this logic bit for bit, counter included, off the per-ip dispatch cache.  Kept as the readable
// statement of the rule, and still the reference any change to sm_unmapped() must match.
bool Recon::tramp_unmapped(uint64_t ip) {
    if (sm_tramp_.empty()) return false;            // an older site map: no trampoline list to check against
    if (sm_tramp_.count(ip)) return false;          // described by the map
    // The map may describe a block without listing it in `trampolines' (a relocated copy,
    // a logging instruction or a sync marker at the very entry).  Anything the map names is
    // authoritative and must NOT be handed to the byte-matching cursor.
    if (sm_rel_.count(ip) || sm_ent_.count(ip) || sm_sync_.count(ip)) return false;
    if (!tramp_entry_.count(ip)) return false;      // not a trampoline entry: ordinary instrumentation
    n_tramp_unknown_++;
    return true;
}

// One pass over every site-map table for an ip, cached in its InsnInfo (see recon.h).  The
// tables are built by build_site_map()/build_tramp_map() before run() and are immutable for the
// whole of it, so this is a pure memoisation of what run() used to recompute per execution.
void Recon::sm_fill(const InsnInfo& ii, uint64_t ip) {
    if (ii.sm_flags & SMF_DONE) return;
    uint16_t f = SMF_DONE;
    auto kf = sm_kf_.find(ip);   if (kf != sm_kf_.end())   { f |= SMF_KF;   ii.sm_kf_idx  = kf->second; }
    auto je = sm_je_.find(ip);   if (je != sm_je_.end())   { f |= SMF_JE;   ii.sm_je_idx  = je->second; }
    auto en = sm_ent_.find(ip);  if (en != sm_ent_.end())  { f |= SMF_ENT;  ii.sm_ent_idx = en->second; }
    if (sm_sync_.count(ip)) f |= SMF_SYNC;
    auto re = sm_rel_.find(ip);  if (re != sm_rel_.end())  { f |= SMF_REL;  ii.sm_relp = &re->second; }
    if (sm_tramp_.count(ip)) f |= SMF_TRAMPSET;
    if (tramp_entry_.count(ip)) f |= SMF_TRAMPENT;
    ii.sm_flags = f;
}

const Recon::InsnInfo& Recon::info(uint64_t ip, const uint8_t* raw, int len) {
    // Front cache: one std::unordered_map bucket walk per decoded
    // instruction was the single largest leaf of the whole-program CPython profile.  The slot is
    // only a shortcut to the SAME node the map would have found; every miss falls through to the
    // map unchanged, and every erase below drops the slot.
    ICacheEnt& ic = icache_[icache_slot(ip)];
    if (ic.p && ic.ip == ip && len > 0 && len <= 16 && ic.p->len == len &&
        !memcmp(ic.p->encoding, raw, len)) return *ic.p;
    auto it = insns_.find(ip);
    if (it != insns_.end()) {
        if (len > 0 && len <= 16 && it->second.len == len &&
            !memcmp(it->second.encoding, raw, len)) { ic.ip = ip; ic.p = &it->second; return it->second; }
        // Mutable runtime code can reuse an IP with different immediates, memory
        // operands, or instruction lengths. Its Zydis AND VEX caches must agree
        // with the bytes decoded for this event. Original site-map records have
        // separate per-record caches and are deliberately not invalidated here.
        insns_.erase(it);
        icache_drop(ip);
        blocks_.erase(ip);
    }
    InsnInfo ii; ii.len = len; ii.orig = in_orig_code(ip);
    if (len > 0) memcpy(ii.encoding, raw, std::min(len, 16));
    ZydisDecodedInstruction zi; ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
    int dpos = -1;
    if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&g_zd, raw, len, &zi, ops, ZYDIS_MAX_OPERAND_COUNT, 0))) {
        ii.ok = true;
        ii.rep = (zi.attributes & (ZYDIS_ATTRIB_HAS_REP | ZYDIS_ATTRIB_HAS_REPE | ZYDIS_ATTRIB_HAS_REPNE)) != 0;
        ii.ptwrite = zi.mnemonic == ZYDIS_MNEMONIC_PTWRITE;
        ii.state_save = zi.mnemonic == ZYDIS_MNEMONIC_XSAVE || zi.mnemonic == ZYDIS_MNEMONIC_XSAVE64 || zi.mnemonic == ZYDIS_MNEMONIC_XSAVEC || zi.mnemonic == ZYDIS_MNEMONIC_XSAVEC64 || zi.mnemonic == ZYDIS_MNEMONIC_XSAVES || zi.mnemonic == ZYDIS_MNEMONIC_XSAVES64 || zi.mnemonic == ZYDIS_MNEMONIC_XSAVEOPT || zi.mnemonic == ZYDIS_MNEMONIC_XSAVEOPT64 || zi.mnemonic == ZYDIS_MNEMONIC_FXSAVE || zi.mnemonic == ZYDIS_MNEMONIC_FXSAVE64;
        ii.branch = zi.meta.category == ZYDIS_CATEGORY_COND_BR || zi.meta.category == ZYDIS_CATEGORY_UNCOND_BR;
        ii.glue = zi.meta.category == ZYDIS_CATEGORY_CALL || zi.meta.category == ZYDIS_CATEGORY_RET || zi.meta.category == ZYDIS_CATEGORY_PUSH || zi.meta.category == ZYDIS_CATEGORY_POP;
        int n_mem = 0, n_mem_rmw = 0;
        for (int i = 0; i < zi.operand_count; i++) {
            const ZydisDecodedOperand& op = ops[i];
            const bool wr = (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0;
            const bool rd = (op.actions & ZYDIS_OPERAND_ACTION_MASK_READ) != 0;
            if (op.type == ZYDIS_OPERAND_TYPE_REGISTER && ii.ptwrite && op.visibility != ZYDIS_OPERAND_VISIBILITY_HIDDEN) ii.ptw_reg_off = zy_gpr_off(op.reg.value, &ii.ptw_reg_size);
            if (op.type == ZYDIS_OPERAND_TYPE_REGISTER && wr) {
                // Which 64-bit GP registers does this instruction PROVABLY write?  Used when VEX
                // cannot lift it: everything else keeps its value instead of going unknown.
                ZydisRegister big = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, op.reg.value);
                if (big >= ZYDIS_REGISTER_RAX && big <= ZYDIS_REGISTER_R15) ii.gpwrite |= 1u << (big - ZYDIS_REGISTER_RAX);
            }
            // A multi-byte NOP has a syntactic memory operand (even reported
            // READ by this Zydis), but architecturally performs no memory access.
            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && zi.mnemonic != ZYDIS_MNEMONIC_NOP &&
                (rd || wr) && op.mem.type != ZYDIS_MEMOP_TYPE_AGEN) {
                n_mem++; if (rd && wr) n_mem_rmw++;
                if (wr) ii.mem_write = true;
                // Keep EVERY memory operand, hidden ones included.  Zydis reports the
                // [%rdi]/[%rsi] operands of a string instruction as HIDDEN, so the code below
                // (which skips hidden operands) leaves `memsize' at 0 for `rep stos'/`rep movs'
                // and the `rep' expansion guard in run() would never see an unknown destination.
                if (ii.n_mem_ops < 2) {
                    MemDesc& d = ii.mem_ops[ii.n_mem_ops++];
                    d.base = op.mem.base == ZYDIS_REGISTER_NONE ? -1 : zy_gpr_off(op.mem.base, nullptr);
                    d.riprel = op.mem.base == ZYDIS_REGISTER_RIP;
                    d.index = op.mem.index == ZYDIS_REGISTER_NONE ? -1 : zy_gpr_off(op.mem.index, nullptr);
                    d.scale = op.mem.scale ? op.mem.scale : 1;
                    d.disp = op.mem.disp.has_displacement ? op.mem.disp.value : 0;
                    d.size = op.size / 8; if (!d.size) d.size = 8;
                    d.seg = op.mem.segment == ZYDIS_REGISTER_FS ? 1 : op.mem.segment == ZYDIS_REGISTER_GS ? 2 : 0;
                }
            }
            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && zi.mnemonic != ZYDIS_MNEMONIC_NOP &&
                (rd || wr) && ii.memsize == 0 && op.visibility != ZYDIS_OPERAND_VISIBILITY_HIDDEN && op.mem.type != ZYDIS_MEMOP_TYPE_AGEN) {
                if (ii.ptwrite) ii.ptw_mem = true;
                ii.base = op.mem.base == ZYDIS_REGISTER_NONE ? -1 : zy_gpr_off(op.mem.base, nullptr); ii.riprel = op.mem.base == ZYDIS_REGISTER_RIP;
                ii.index = op.mem.index == ZYDIS_REGISTER_NONE ? -1 : zy_gpr_off(op.mem.index, nullptr); ii.scale = op.mem.scale ? op.mem.scale : 1;
                ii.disp = op.mem.disp.has_displacement ? op.mem.disp.value : 0; ii.memsize = op.size / 8; if (!ii.memsize) ii.memsize = 8;
                // `%fs:'/`%gs:' operands.  Without the segment base the effective address of
                // `mov %fs:(%rax),%rax' would come out as %rax alone, which is where a logged
                // `memop' value would be filed -- never at the address the interpreter then reads.
                ii.seg = op.mem.segment == ZYDIS_REGISTER_FS ? 1 : op.mem.segment == ZYDIS_REGISTER_GS ? 2 : 0;
            }
        }
        // SPEC_FORMAT section 5: one MT_RMW record for an operand that is both read and written.
        ii.mem_rmw = (n_mem == 1 && n_mem_rmw == 1);
        if (ii.riprel && zi.raw.disp.size) dpos = zi.raw.disp.offset;
    }
    // E9Patch plants a PUNNED patch jump at an instrumented address -- `48 e9 <rel32>', a
    // `jmp rel32' with a redundant REX.W so that the low displacement bytes coincide with the
    // original following instruction.  VEX refuses REX.W + JMP rel32 (Ijk_NoDecode), so every
    // execution of one would be counted as a `lift_failure' (tens of millions on whole-program
    // CPython), which would make the counter useless as a health signal.
    // Recognise it here (bytes + a target outside the original code) and count it separately.
    if (ii.ok && len == 6 && raw[0] == 0x48 && raw[1] == 0xe9 && !(ip >> 63)) {
        int32_t rel; memcpy(&rel, raw + 2, 4);
        uint64_t tgt = ip + 6 + (int64_t)rel;
        if (!in_orig_code(tgt)) ii.patch_jump = true;
    }
    uint64_t h = 1469598103934665603ull;   // FNV of the bytes with a rip-relative disp masked (trampoline-copy matching)
    for (int i = 0; i < len; i++) { if (dpos >= 0 && i >= dpos && i < dpos + 4) continue; h = (h ^ raw[i]) * 1099511628211ull; }
    ii.hash = h;
    InsnInfo& slot = insns_.emplace(ip, ii).first->second;
    ic.ip = ip; ic.p = &slot;
    return slot;
}

const Recon::InsnInfo* Recon::orig_info(uint64_t addr) {
    auto it = orig_insns_.find(addr); if (it != orig_insns_.end()) return it->second.ok ? &it->second : nullptr;
    const std::vector<uint8_t>* ob = orig_bytes_at(addr);
    if (!ob) { orig_insns_.emplace(addr, InsnInfo{}); return nullptr; }
    ZydisDecodedInstruction zi;
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&g_zd, nullptr, ob->data(), ob->size(), &zi))) { orig_insns_.emplace(addr, InsnInfo{}); return nullptr; }
    const uint64_t key = addr ^ (1ull << 63);
    InsnInfo tmp = info(key, ob->data(), zi.length);
    insns_.erase(key); icache_drop(key);
    tmp.sm_flags = 0; tmp.sm_relp = nullptr; tmp.sm_ob = nullptr; tmp.sm_oi = nullptr;
    tmp.sm_blk_orig = nullptr; tmp.sm_blk_self = nullptr;
    tmp.orig = true;
    return &orig_insns_.emplace(addr, tmp).first->second;
}

// Effective address of one memory operand.  `seg' is the segment override: on x86-64 only %fs and
// %gs have a non-zero base.  VEX models `%fs:' as `guest_FS_CONST + ...', so the reconstructor has
// to do the same or the address it computes for a logged `memop' value is filed
// somewhere the interpreter never looks.  %gs has no loggable base (`gs_base' is not a spec
// register), so a %gs: operand is unconditionally unknown rather than silently wrong.
uint64_t Recon::ea_desc(const MemDesc& d, uint64_t ip, int len, bool* known) {
    uint64_t a = (uint64_t)d.disp; *known = true;
    if (d.riprel) a += ip + len;
    else if (d.base >= 0) { Val v = vi_.get_reg(d.base, 8); if (!v.known()) *known = false; a += v.u64(); }
    if (d.index >= 0) { Val v = vi_.get_reg(d.index, 8); if (!v.known()) *known = false; a += v.u64() * d.scale; }
    if (d.seg == 1) { Val v = vi_.get_reg(OFFSET_amd64_FS_CONST, 8); if (!v.known()) *known = false; a += v.u64(); }
    else if (d.seg == 2) *known = false;
    return a;
}

uint64_t Recon::ea(const InsnInfo& ii, uint64_t ip, bool* known) {
    MemDesc d; d.base = ii.base; d.index = ii.index; d.scale = ii.scale; d.disp = ii.disp; d.riprel = ii.riprel; d.seg = ii.seg; d.size = ii.memsize;
    return ea_desc(d, ip, ii.len, known);
}

void Recon::count_image(uint64_t ip, bool unknown) {
    // Binary search over the sorted sideband mappings, with a one-entry cache in front: whole
    // program CPython has 17 000 mappings, so the search alone is ~14 random memory touches per
    // record, and consecutive records almost always fall in the same mapping.
    if (img_last_ < imgs_.size() && ip >= imgs_[img_last_].lo && ip < imgs_[img_last_].hi) {
        imgs_[img_last_].records++; if (unknown) imgs_[img_last_].unknown++; return;
    }
    size_t lo = 0, hi = imgs_.size();
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (imgs_[mid].lo <= ip) lo = mid + 1; else hi = mid; }
    if (lo > 0 && ip < imgs_[lo - 1].hi) { img_last_ = lo - 1; imgs_[lo - 1].records++; if (unknown) imgs_[lo - 1].unknown++; return; }
    other_.records++; if (unknown) other_.unknown++;
}

void Recon::emit(uint64_t addr, bool known, int size, int op, uint64_t ip, uint64_t ts) {
    // SAME-PROCESS GROUND TRUTH.  The gt stream is consumed in
    // lockstep with the records this reconstruction produces -- both come from the same execution,
    // so the comparison is per dynamic access and needs no alignment.
    if (gt_on_) gt_step(addr, known, ip, size, op, ts);
    if (warming_) return;                 // chunk warm-up: this record belongs to the previous chunk
    mtrace_rec r; r.addr = known ? addr : 0; r.ip = ip; r.ts = ts; r.tid = tid_; r.op = op; r.size = size; r.flags = (known ? 0 : MTF_ADDR_UNKNOWN) | (in_ovf_ ? MTF_IN_OVERFLOW : 0);
    if (!known) n_unknown_++;
    if (!anchored_) n_unanchored_rec_++;
    if (op == MT_RMW) n_rmw_++;
    n_rec_++;
    count_image(ip, !known);
    if (out_ || o_.text) n_output_rec_++;
    if (out_) fwrite_unlocked(&r, sizeof r, 1, out_);
    if (o_.text) printf("%c %lx %d %lx%s\n", op == 0 ? 'L' : op == 1 ? 'S' : op == 2 ? 'M' : 'K', (unsigned long)r.addr, size, (unsigned long)ip, known ? "" : " ?");
}

// ===========================================================================================
// SAME-PROCESS GROUND TRUTH
// ===========================================================================================
// `--gt-in FILE' is the log the traced process wrote ITSELF, from trampolines a `--gt-all'
// rewrite planted at every memory-accessing instruction: 32-byte header, then 16-byte records
// { effective address, original ip } in program order.  Because it is the same execution the PT
// stream describes, the reconstruction and the ground truth can be walked together, one record at
// a time -- no marker, no alignment, no edit distance.
//
// Not every dynamic access is in the gt stream: E9Patch has no tactic for some instructions, the
// emitter refuses forms it cannot re-encode as exactly ONE record (`rep' string moves, `push
// (%rax)', %gs:-relative operands), and VEX occasionally cannot lift an instruction, in which case
// the reconstruction emits nothing where the ground truth has a record.  All three are counted:
//   identical  the reconstructed address equals the ground truth
//   unknown    the reconstruction says it does not know the address (MTF_ADDR_UNKNOWN)
//   wrong      it produced an address, and it is not the right one
//   excluded   the instruction is not a gt site, so there is nothing to compare against
//   gt_only    a gt record with no reconstructed record (VEX lift failure, suppressed output)
//   recon_only a reconstructed record whose gt record is missing (a gt site E9Patch left unpatched
//              but whose ip is still in `gt_site_addrs', or a `rep' expansion)
// The last two are recovered by a BOUNDED local resync (`--gt-lookahead', default 64 records):
// the streams cannot drift, so a disagreement is always a few records wide.
void Recon::gt_open() {
    if (o_.gt_in.empty()) return;
    gt_fd_ = ::open(o_.gt_in.c_str(), O_RDONLY);
    if (gt_fd_ < 0) throw std::runtime_error("cannot open --gt-in " + o_.gt_in);
    struct stat st{}; if (fstat(gt_fd_, &st) != 0) throw std::runtime_error("--gt-in: fstat");
    gt_mm_len_ = (size_t)st.st_size;
    if (gt_mm_len_ < 32) throw std::runtime_error("--gt-in: file shorter than its header");
    gt_mm_ = mmap(nullptr, gt_mm_len_, PROT_READ, MAP_PRIVATE, gt_fd_, 0);
    if (gt_mm_ == MAP_FAILED) throw std::runtime_error("--gt-in: mmap failed");
    const uint32_t* h = (const uint32_t*)gt_mm_;
    if (h[0] != 0x54475450u) throw std::runtime_error("--gt-in: bad magic (not a PTGT file)");
    if (h[3] != 16) throw std::runtime_error("--gt-in: unexpected record size");
    if ((gt_mm_len_ - 32) % sizeof(GtRec)) throw std::runtime_error("--gt-in: trailing partial record");
    gt_.open((const GtRec*)((const uint8_t*)gt_mm_ + 32),
             (gt_mm_len_ - 32) / sizeof(GtRec), h[1]);
    gt_n_ = gt_.size();
    if (o_.gt_continue && !gt_n_) gt_exhausted_ = true;
    gt_on_ = true;
    if (!o_.gt_out.empty()) {
        gt_out_ = fopen(o_.gt_out.c_str(), "wb");
        if (!gt_out_) throw std::runtime_error("cannot write --gt-out " + o_.gt_out);
        mtrace_hdr h2{}; h2.magic = MTRACE_MAGIC; h2.version = MTRACE_VERSION; h2.flags = 0; h2.tid_count = 1;
        fwrite(&h2, sizeof h2, 1, gt_out_);
    }
    fprintf(stderr, "gt: %zu ground-truth records from %s, %zu gt site addresses\n",
            gt_n_, o_.gt_in.c_str(), gt_sites_.size());
}

// A PT state loss happened; re-align the gt walk once the reconstruction has re-anchored.
void Recon::gt_arm_resync() {
    if (!gt_on_ || !o_.gt_resync_window) return;
    gt_resync_armed_ = 16; gt_resync_pending_ = true;
    gt_vals_at_loss_ = n_ptw_used_ - n_sync_used_ + n_cv_used_ + n_delta_skipped_;
    gt_sync_at_loss_ = n_sync_used_;
}

// A streak re-alignment candidate must agree with the CONTROL FLOW that led to it: the C
// reconstructed gt-site ips before this record must be the C gt ips before `k2'.  The ip
// sequence is what Intel PT delivers exactly; the ADDRESS is the quantity under test, so it is
// deliberately NOT part of the confirmation -- using it biases the pairing towards `identical'.
// A single (ip, addr) pair is not a dynamic instance: the same instruction touches the same
// address over and over, so an unconfirmed nearest-pair rule re-aligns on coincidences.
static const int kGtConfirm = 32;
bool Recon::gt_confirm_at(size_t k2) const {
    const int C = kGtConfirm;
    if (gt_hist_n_ < 8 || k2 < 8) return false;
    int n = 0, hits = 0;
    for (int j = 1; j <= C; j++) {
        if ((size_t)j > k2 || (size_t)j > gt_hist_n_ || j > 128) break;
        const uint64_t want = gt_hist_[(gt_hist_n_ - (size_t)j) & 127];
        n++;
        const uint64_t got = gt_[k2 - (size_t)j].ip;
        if (got == want) hits++;
    }
    return n >= 8 && hits * 4 >= n * 3;
}

// One reconstructed record against the ground-truth walk.
void Recon::gt_step(uint64_t addr, bool known, uint64_t ip, int size, int op, uint64_t ts) {
    const bool f_ovf = in_ovf_, f_unanch = !anchored_, f_warm = warming_;
    const uint64_t r_vals = n_ptw_used_ - n_sync_used_ + n_cv_used_ + n_delta_skipped_, r_syncs = n_sync_used_;
    const bool is_site = gt_sites_.count(ip) != 0;
    // What the two streams are matched on: the gt record's second word is the original ip.
    const uint64_t key = ip;
    uint64_t gaddr = 0; int cls;      // 0 identical 1 unknown 2 wrong 3 excluded 4 recon_only
    if (gt_exhausted_ && o_.gt_continue) {
        // EOF is not an alignment opportunity: never rewind into already
        // compared GT records. Count every later observation as unverified.
        g_after_end_++;
        if (is_site) { cls = 4; g_recon_only_++; }
        else { cls = 3; g_excluded_++; }
    }
    else if (!is_site) { cls = 3; g_excluded_++; }
    else {
        gt_hist_[gt_hist_n_++ & 127] = key;   // the confirmation window
        // After a PT overflow / decoder resync the gt stream holds records for every access the
        // reconstruction never saw, and inside a loop the ip-keyed lookahead cannot tell one
        // iteration from the next: it would match the first post-gap record against the first
        // LOST iteration and report every later record of the loop as `wrong'.
        // So for the first known records after a state loss, re-align on the (key, ADDRESS) pair
        // -- unique enough to cross the gap -- within `--gt-resync-window' records; what is
        // skipped is `gt_only' (accesses inside the gap), counted apart as `gt_skipped_after_loss'.
        // The re-alignment waits until the reconstruction is trustworthy again: at least one logged
        // value consumed since the loss and, with the buffer sink, a sync marker seen since it --
        // until the marker the positional cv cursor is off by the values logged inside the gap, so
        // the first "known" addresses after the loss are wrong and would re-align on a wrong record.
        if (gt_resync_armed_ && known && gt_resync_pending_ &&
            r_vals > gt_vals_at_loss_ &&
            (cv_.empty() || (r_syncs > gt_sync_at_loss_ &&
                             r_vals > gt_vals_at_sync_))) gt_resync_pending_ = false;
        if (gt_resync_armed_ && known && !gt_resync_pending_) {
            // The NEAREST (key, addr) match on either side of gt_pos_: inside a loop the same pair
            // recurs (tmthread's tab[i & 4095] every 4096 iterations), so "the first match ahead"
            // is wrong whenever the walk is already past the right record -- which it is when
            // the gap swallowed the records the reconstruction emitted as unknown.
            const size_t rlim = std::min(gt_n_, gt_pos_ + (size_t)o_.gt_resync_window);
            const size_t rlo = gt_pos_ > (size_t)o_.gt_resync_window ? gt_pos_ - (size_t)o_.gt_resync_window : 0;
            size_t k2 = gt_n_;
            for (size_t dlt = 0; dlt < (size_t)o_.gt_resync_window; dlt++) {
                size_t f = gt_pos_ + dlt, b = gt_pos_ - dlt;
                if (f < rlim && gt_[f].ip == key && gt_[f].addr == addr) { k2 = f; break; }
                if (dlt && gt_pos_ >= dlt && b >= rlo && gt_[b].ip == key && gt_[b].addr == addr) { k2 = b; break; }
                if (f >= rlim && (gt_pos_ < dlt || b < rlo)) break;
            }
            if (k2 < gt_n_) { if (k2 != gt_pos_) { if (k2 > gt_pos_) { g_gt_only_ += (uint64_t)(k2 - gt_pos_); g_gt_skipped_loss_ += (uint64_t)(k2 - gt_pos_); }
                                                   else g_gt_rewound_loss_ += (uint64_t)(gt_pos_ - k2);
                                                   g_gt_resync_loss_++; }
                             if (o_.verbose) fprintf(stderr, "gt re-align after loss: record %lu ip %#lx addr %#lx: gt %zu -> %zu (gt there: %#lx/%#lx)\n",
                                                     (unsigned long)n_rec_, (unsigned long)ip, (unsigned long)addr, gt_pos_, k2, (unsigned long)gt_[gt_pos_].addr, (unsigned long)gt_[gt_pos_].ip);
                             gt_pos_ = k2; gt_resync_armed_ = 0; }
            else if (--gt_resync_armed_ == 0) g_gt_resync_fail_++;
        }
        size_t j = gt_pos_;
        const size_t lim = gt_pos_ + (size_t)(o_.gt_lookahead > 0 ? o_.gt_lookahead : 0);
        while (j < gt_n_ && j < lim && gt_[j].ip != key) j++;
        bool matched = !(j >= gt_n_ || j >= lim || gt_[j].ip != key);
        // A streak of `recon_only' means the two streams have lost each other by more than the
        // lookahead -- a gap the loss-triggered re-alignment above did not cover.  Re-align on the nearest (key,
        // address) pair, exactly as after a state loss, and count it apart.
        if (!matched && known && o_.gt_resync_window && ++gt_streak_ >= 16) {
            const size_t rlim = std::min(gt_n_, gt_pos_ + (size_t)o_.gt_resync_window);
            const size_t rlo = gt_pos_ > (size_t)o_.gt_resync_window ? gt_pos_ - (size_t)o_.gt_resync_window : 0;
            size_t k2 = gt_n_;
            for (size_t dlt = 0; dlt < (size_t)o_.gt_resync_window; dlt++) {
                size_t f = gt_pos_ + dlt, b = gt_pos_ - dlt;
                if (f < rlim && gt_[f].ip == key && gt_[f].addr == addr) { k2 = f; break; }
                if (dlt && gt_pos_ >= dlt && b >= rlo && gt_[b].ip == key && gt_[b].addr == addr) { k2 = b; break; }
                if (f >= rlim && (gt_pos_ < dlt || b < rlo)) break;
            }
            if (k2 < gt_n_ && !gt_confirm_at(k2)) { k2 = gt_n_; g_gt_confirm_rejected_++; }
            if (k2 < gt_n_) { if (k2 > gt_pos_) { g_gt_only_ += (uint64_t)(k2 - gt_pos_); g_gt_skipped_streak_ += (uint64_t)(k2 - gt_pos_); }
                              else g_gt_rewound_loss_ += (uint64_t)(gt_pos_ - k2);
                              g_gt_resync_streak_++; gt_pos_ = j = k2; matched = true; gt_streak_ = 0; }
        }
        if (matched) gt_streak_ = 0;
        if (!matched) { cls = 4; g_recon_only_++; }
        else {
            g_gt_only_ += (uint64_t)(j - gt_pos_);
            gaddr = gt_[j].addr; gt_pos_ = j + 1;
            if (gt_pos_ > gt_hw_) gt_hw_ = gt_pos_;
            if (gt_pos_ >= gt_n_) { gt_exhausted_ = true; gt_stopped_at_ = n_rec_; }
            if (!known) { cls = 1; g_unknown_++; if (f_ovf) g_unknown_ovf_++; if (f_unanch) g_unknown_unanchored_++;
                          if (g_unknown_ip_.size() < 200000) g_unknown_ip_[ip]++; }
            else if (addr == gaddr) { cls = 0; g_identical_++; }
            else { cls = 2; g_wrong_++;
                   if (f_ovf) g_wrong_ovf_++;
                   if (f_unanch) g_wrong_unanchored_++;
                   if ((addr >> 12) == (gaddr >> 12)) g_wrong_samepage_++;
                   if (g_wrong_ip_.size() < 200000) { g_wrong_ip_[ip]++; g_wrong_ex_.emplace(ip, std::make_pair(addr, gaddr)); } }
        }
    }
    if (gt_out_ && !f_warm) {
        // Index-aligned with `-o': one record per reconstructed record, so a consumer can
        // compare the two files element by element.  A record with
        // no ground truth is written as MT_MARKER with the "unknown" flag: `excluded' when the
        // instruction is not a gt site, `recon_only' when its gt record is missing.
        mtrace_rec r{}; r.ip = ip; r.ts = ts; r.tid = tid_;
        if (cls == 3 || cls == 4) { r.addr = 0; r.op = MT_MARKER; r.size = (uint8_t)size; r.flags = MTF_ADDR_UNKNOWN | (cls == 4 ? MTF_IN_OVERFLOW : 0); }
        else { r.addr = gaddr; r.op = (uint8_t)op; r.size = (uint8_t)size; r.flags = 0; }
        fwrite(&r, sizeof r, 1, gt_out_);
    }
}

void Recon::gt_finish() {
    if (!gt_on_) return;
    if (gt_pos_ > gt_hw_) gt_hw_ = gt_pos_;
    // `gt_only' is NOT the sum of forward skip distances (a re-alignment that rewinds and
    // re-skips the same records would charge them again and again; that sum is reported as
    // `gt_skip_distance_raw').  The sound quantity is the number of ground-truth records inside
    // the COMPARED INTERVAL that no reconstructed record was paired with.
    g_gt_skipped_raw_ = g_gt_only_;
    { const uint64_t span = (uint64_t)gt_hw_;
      const uint64_t paired = g_identical_ + g_unknown_ + g_wrong_;
      g_gt_only_ = span > paired ? span - paired : 0; }
    g_gt_tail_ = (uint64_t)(gt_n_ - gt_hw_);
    if (gt_out_) { fclose(gt_out_); gt_out_ = nullptr; }
    if (gt_mm_) { munmap(gt_mm_, gt_mm_len_); gt_mm_ = nullptr; }
    if (gt_fd_ >= 0) { ::close(gt_fd_); gt_fd_ = -1; }
}

std::string Recon::gt_json() const {
    if (!gt_on_) return "null";
    auto top = [](const std::unordered_map<uint64_t, uint64_t>& m,
                  const std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>>* ex) {
        std::vector<std::pair<uint64_t, uint64_t>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end(), [](const std::pair<uint64_t,uint64_t>& a, const std::pair<uint64_t,uint64_t>& b) { return a.second > b.second; });
        std::string s = "[";
        for (size_t i = 0; i < v.size() && i < 40; i++) {
            if (i) s += ",";
            s += "{\"ip\":" + std::to_string(v[i].first) + ",\"n\":" + std::to_string(v[i].second);
            if (ex) { auto it = ex->find(v[i].first);
                      if (it != ex->end()) s += ",\"recon\":" + std::to_string(it->second.first) + ",\"gt\":" + std::to_string(it->second.second); }
            s += "}";
        }
        return s + "]";
    };
    const uint64_t cmp = g_identical_ + g_unknown_ + g_wrong_;
    char buf[512];
    snprintf(buf, sizeof buf, "%.6f", cmp ? 100.0 * (double)g_wrong_ / (double)cmp : 0.0);
    std::string wrongpct = buf;
    snprintf(buf, sizeof buf, "%.6f", cmp ? 100.0 * (double)g_identical_ / (double)cmp : 0.0);
    std::string idpct = buf;
    return std::string("{\"records_compared\":") + std::to_string(cmp) +
        ",\"identical\":" + std::to_string(g_identical_) +
        ",\"unknown\":" + std::to_string(g_unknown_) +
        ",\"wrong\":" + std::to_string(g_wrong_) +
        ",\"excluded\":" + std::to_string(g_excluded_) +
        ",\"gt_only\":" + std::to_string(g_gt_only_) +
        ",\"gt_skip_distance_raw\":" + std::to_string(g_gt_skipped_raw_) +
        ",\"gt_realign_rejected\":" + std::to_string(g_gt_confirm_rejected_) +
        ",\"recon_only\":" + std::to_string(g_recon_only_) +
        ",\"gt_tail\":" + std::to_string(g_gt_tail_) +
        ",\"gt_records\":" + std::to_string((uint64_t)gt_n_) +
        ",\"gt_physical_records\":" + std::to_string(gt_.physical()) +
        ",\"gt_rep_ranges\":" + std::to_string(gt_.ranges()) +
        ",\"gt_sites\":" + std::to_string((uint64_t)gt_sites_.size()) +
        ",\"resync_after_streak\":" + std::to_string(g_gt_resync_streak_) + ",\"gt_skipped_after_streak\":" + std::to_string(g_gt_skipped_streak_) + ",\"resync_after_loss\":" + std::to_string(g_gt_resync_loss_) + ",\"gt_skipped_after_loss\":" + std::to_string(g_gt_skipped_loss_) + ",\"gt_rewound_after_loss\":" + std::to_string(g_gt_rewound_loss_) + ",\"resync_after_loss_failed\":" + std::to_string(g_gt_resync_fail_) +
        ",\"gt_continue\":" + std::string(o_.gt_continue ? "true" : "false") +
        ",\"records_after_gt_end\":" + std::to_string(g_after_end_) +
        ",\"gt_exhausted\":" + std::string(gt_exhausted_ ? "true" : "false") +
        ",\"identical_pct\":" + idpct + ",\"wrong_pct\":" + wrongpct +
        ",\"wrong_in_overflow\":" + std::to_string(g_wrong_ovf_) +
        ",\"wrong_same_page\":" + std::to_string(g_wrong_samepage_) +
        ",\"wrong_unanchored\":" + std::to_string(g_wrong_unanchored_) +
        ",\"unknown_in_overflow\":" + std::to_string(g_unknown_ovf_) +
        ",\"unknown_unanchored\":" + std::to_string(g_unknown_unanchored_) +
        ",\"top_wrong_ips\":" + top(g_wrong_ip_, &g_wrong_ex_) +
        ",\"top_unknown_ips\":" + top(g_unknown_ip_, nullptr) + "}";
}

// SPEC_FORMAT section 5: an instruction whose single memory operand is both read and written
// produces ONE MT_RMW record (this is the ground-truth Pintool's convention); VEX models it as a
// load followed by a store, so the pair is coalesced here.
void Recon::flush_acc(const InsnInfo& ii, uint64_t ip, uint64_t ts) {
    // The test is NOT "load then store" (how VEX models a plain `add %rdx,(%rax)'): an ATOMIC
    // exchange is modelled differently -- `xchg %eax,(%rbx)' and `lock xadd %eax,(%rbx)' lift to
    // a LOAD of the old value followed by an `IRStmt CAS' (the atomic swap), i.e. ops (0, 2) --
    // and emitting TWO records where SPEC_FORMAT section 5 (and the gt trampoline, and the
    // Pintool) have ONE is not cosmetic against a same-process ground truth: the lockstep walk is
    // keyed on the ip, so the extra record consumes the gt record of the NEXT execution of that
    // ip and the two streams stay one execution apart until the bounded lookahead realigns them.
    // The rule: an instruction with exactly one memory operand that is read AND written
    // (`ii.mem_rmw') emits one MT_RMW record, whatever shape VEX gave it, provided every access
    // it made is to that one operand and at least one of them writes.
    if (ii.mem_rmw && acc_.size() >= 2) {
        bool same = true, wrote = false;
        for (const MemAccess& m : acc_) {
            if (m.size != acc_[0].size || m.known != acc_[0].known ||
                (acc_[0].known && m.addr != acc_[0].addr)) { same = false; break; }
            if (m.op != 0) wrote = true;
        }
        if (same && wrote) {
            emit(acc_[0].addr, acc_[0].known, acc_[0].size, MT_RMW, ip, ts);
            acc_.clear(); return;
        }
    }
    for (const MemAccess& m : acc_) emit(m.addr, m.known, m.size, m.op, ip, ts);
    acc_.clear();
}

// Point the pending-PTWRITE machinery at what the site map says this logging instruction logs.
// Returns false if the value cannot be applied (unknown effective address, unloggable register).
bool Recon::arm_value_site(const SiteMapEnt& e) {
    ptw_pending_ = true; ptw_kind_ = PTW_NONE; ptw_pend_off_ = -1; ptw_pend_size_ = 8; ptw_pend_memsize_ = 0;
    if (e.kind == "memop") {
        // The address is the ORIGINAL instruction's effective address: all instrumentation is
        // skipped, so the register state here is exactly the state at the original instruction.
        const InsnInfo* oi = orig_info(e.orig); if (!oi) return false;
        bool k; uint64_t a = ea(*oi, e.orig, &k); if (!k) return false;
        ptw_kind_ = PTW_MEM; ptw_pend_ea_ = a; ptw_pend_memsize_ = e.size ? e.size : (oi->memsize ? oi->memsize : 8);
        return true;
    }
    int sz = 8; int off = VexInterp::reg_off(e.reg, &sz);
    if (off < 0) return false;
    if (e.reg.compare(0, 3, "xmm") == 0) { off += (e.half == "hi" ? 8 : 0); sz = 8; }
    ptw_kind_ = PTW_REG; ptw_pend_off_ = off; ptw_pend_size_ = 8; (void)sz;
    return true;
}

// Buffer sink: one logged value straight out of the cv.*.bin stream.
void Recon::define_value(const SiteMapEnt& e, uint64_t v) {
    if (e.kind == "memop") {
        const InsnInfo* oi = orig_info(e.orig); if (!oi) return;
        bool k; uint64_t a = ea(*oi, e.orig, &k); if (!k) return;
        vi_.memop_define(a, v, e.size ? e.size : (oi->memsize ? oi->memsize : 8));
        return;
    }
    int sz = 8; int off = VexInterp::reg_off(e.reg, &sz);
    if (off < 0) return;
    if (e.reg.compare(0, 3, "xmm") == 0) { off += (e.half == "hi" ? 8 : 0); sz = 8; }
    vi_.set_reg(off, 8, v);
}

// The guard's `je' skipped the logging instruction: the value is the one this site logged last,
// which is what the runtime's cache slot still holds.  An invalid entry (an overflow or a resync
// may have hidden executions that changed a slot; so does the start of a `--jobs' chunk) would
// make the value UNKNOWN and leave it there -- and a stale entry is only repaired by the site
// logging again, which for a cold site can be millions of records away even with a keyframe every
// 4096 executions.
//
// The repair: a TAKEN `je' is the runtime telling us `value == SLOT'.  So whenever the
// reconstructor already knows the value -- the register, or the shadowed memory word, which the
// resync keyframes re-anchor within one keyframe interval of any state loss -- it knows the slot
// too, and the entry re-validates WITHOUT a logged value.  No trace information is invented: the
// equality is a branch decision Intel PT recorded.
void Recon::delta_take_from_table(long idx) {
    const SiteMapEnt& e = flat_ent_[idx];
    if (delta_valid_[idx]) { define_value(e, delta_val_[idx]); n_delta_skipped_++; anchored_ = true; return; }
    if (e.kind == "memop") {
        const InsnInfo* oi = orig_info(e.orig);
        bool k = false; uint64_t a = oi ? ea(*oi, e.orig, &k) : 0;
        int msz = e.size ? e.size : (oi && oi->memsize ? oi->memsize : 8);
        if (oi && k) {
            Val v = vi_.shadow_load(a, msz);
            if (v.known()) {                       // the `je' proves SLOT == this word
                delta_val_[idx] = v.u64(); delta_valid_[idx] = 1;
                n_delta_reanchor_++; n_delta_skipped_++; anchored_ = true; return;
            }
        }
        n_delta_unknown_++;
        if (oi && k) vi_.shadow_forget(a, msz);
        else vi_.shadow_clear();
        return;
    }
    int sz = 8; int off = VexInterp::reg_off(e.reg, &sz);
    if (off < 0) { n_delta_unknown_++; return; }
    if (e.reg.compare(0, 3, "xmm") == 0) { off += (e.half == "hi" ? 8 : 0); sz = 8; }
    Val rv = vi_.get_reg(off, 8);
    if (rv.known()) {                              // the `je' proves SLOT == this register
        delta_val_[idx] = rv.u64(); delta_valid_[idx] = 1;
        n_delta_reanchor_++; n_delta_skipped_++; anchored_ = true; return;
    }
    n_delta_unknown_++;
    vi_.set_reg_unknown(off, 8);
}

void Recon::delta_invalidate() { delta_valid_.assign(delta_valid_.size(), 0); }

// A keyframe execution is about to (re)define this logged value.  If the register it defines is
// currently UNKNOWN, this keyframe is exactly the thing a PT overflow / decoder resync made
// necessary: it re-anchors a loop-carried register that would otherwise stay unknown for the rest
// of the run.  Counted so the mechanism can be shown to work.
void Recon::kf_note(uint32_t idx) {
    const SiteMapEnt& e = flat_ent_[idx];
    if (!e.kf_period || e.kind == "memop") return;
    int sz = 8; int off = VexInterp::reg_off(e.reg, &sz);
    if (off < 0) return;
    if (e.reg.compare(0, 3, "xmm") == 0) off += (e.half == "hi" ? 8 : 0);
    if (vi_.get_reg(off, 8).known()) return;
    n_kf_site_defs_++;                                  // any log at a keyframe-capable site
    // A keyframed LOG-ON-CHANGE value also logs whenever its value changes, and that is not a
    // keyframe: only count it when this guard's countdown reached zero for this execution.
    if (e.kf_branch == kf_fired_branch_) n_kf_reanchor_++;
}

void Recon::define_from_ptw(uint64_t payload, int size) {
    n_ptw_used_++; if (ptw_kind_ != PTW_SYNC) anchored_ = true;
    switch (ptw_kind_) {
        case PTW_SYNC:                                   // buffer sink: realign the value stream
            if (cv_audit_ && !warming_)
                fprintf(cv_audit_, "%llu %#llx %#llx %llu %llu %+lld %llu %llu %llu %llu\n",
                        (unsigned long long)n_sync_seen_, (unsigned long long)sync_off_,
                        (unsigned long long)sync_ip_, (unsigned long long)payload,
                        (unsigned long long)cv_pos_, (long long)((long long)payload - (long long)cv_pos_),
                        (unsigned long long)sync_nres_, (unsigned long long)sync_ninsn_,
                        (unsigned long long)n_tramp_unknown_, (unsigned long long)n_fb_logsite_);
            n_sync_seen_++;
            if (payload <= cv_.size()) {
                if (cv_pos_ != payload) {
                    n_sync_resync_++;
                    // The marker is emitted AFTER this site's stores, so any value this
                    // trampoline has already handed out came from the UNCORRECTED cursor.  The
                    // marker names the exact number of values the thread has written, so the
                    // correction is a uniform shift: re-read each of them `delta' slots earlier.
                    // Nothing is invented -- the values are read from the cv file at the positions
                    // the runtime itself wrote them to.
                    if (pre_first_sync_ && payload < cv_pos_ && !tramp_cv_.empty()) {
                        const size_t delta = cv_pos_ - (size_t)payload;
                        for (const auto& p : tramp_cv_) {
                            if (p.second < delta) continue;
                            const size_t slot = p.second - delta;
                            if (slot >= cv_.size()) continue;
                            const SiteMapEnt& re = flat_ent_[p.first];
                            define_value(re, cv_[slot]);
                            if (re.delta) { delta_val_[p.first] = cv_[slot]; delta_valid_[p.first] = 1; }
                            n_cv_redefined_++;
                        }
                    }
                }
                cv_pos_ = (size_t)payload;
            }
            pre_first_sync_ = false; tramp_cv_.clear();
            n_sync_used_++;
            gt_vals_at_sync_ = n_ptw_used_ - n_sync_used_ + n_cv_used_ + n_delta_skipped_;   // gt re-alignment gate
            break;
        case PTW_MEM: vi_.memop_define(ptw_pend_ea_, payload, ptw_pend_memsize_ ? ptw_pend_memsize_ : size); break;
        case PTW_REG: vi_.set_reg(ptw_pend_off_, size < ptw_pend_size_ ? size : ptw_pend_size_, payload); break;
        default:
            if (ptw_pend_mem_legacy_) vi_.memop_define(ptw_pend_ea_, payload, size);
            else if (ptw_pend_off_ >= 0) vi_.set_reg(ptw_pend_off_, size < ptw_pend_size_ ? size : ptw_pend_size_, payload);
            break;
    }
    if (ptw_pend_ent_ >= 0) {
        if (flat_ent_[ptw_pend_ent_].delta) { delta_val_[ptw_pend_ent_] = payload; delta_valid_[ptw_pend_ent_] = 1; }
        ptw_pend_ent_ = -1;
    }
    ptw_pending_ = false; ptw_kind_ = PTW_NONE;
}

void Recon::note_memory_omission(const char* kind, uint64_t ip, const InsnInfo& ii,
                                 const uint8_t* raw, int len, const Val& count, bool addr_known) {
    n_memory_omission_events_++;
    if (memory_omission_examples_.size() >= 64) return;
    std::string bytes, path;
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < len; i++) { bytes += hex[raw[i] >> 4]; bytes += hex[raw[i] & 15]; }
    for (const auto& m : sb_.maps) if (ip >= m.start && ip < m.end) { path = m.path; break; }
    std::string history = "[";
    uint64_t start = recent_original_count_ > 32 ? recent_original_count_ - 32 : 0;
    for (uint64_t i = start; i < recent_original_count_; i++)
        history += std::string(i > start ? "," : "") + std::to_string(recent_original_ips_[i % 32]);
    history += "]";
    memory_omission_examples_.push_back("{\"kind\":" + json_quote(kind) +
        ",\"ip\":" + std::to_string(ip) + ",\"image\":" + json_quote(path) +
        ",\"bytes\":" + json_quote(bytes) + ",\"decoded\":" + (ii.ok ? "true" : "false") +
        ",\"memory_operands\":" + std::to_string(ii.n_mem_ops) +
        ",\"state_save\":" + (ii.state_save ? "true" : "false") +
        ",\"count_known\":" + (count.known() ? "true" : "false") +
        ",\"count\":" + (count.known() ? std::to_string(count.u64()) : "null") +
        ",\"addresses_known\":" + (addr_known ? "true" : "false") +
        ",\"recent_original_ips\":" + history + "}");
}

// Chunk warm-up: everything decoded before `emit_from' belongs to the PREVIOUS chunk and has
// already been reported by it.  The machine state, the shadow, the delta table, the cv cursor
// and `anchored_' are exactly what the warm-up is for and are kept; every counter is zeroed so the
// parent's merged summary is a partition of the trace and not an overlapping sum.
void Recon::reset_counters() {
    n_rec_ = n_unknown_ = n_ovf_ = n_rep_unknown_ = n_ptw_used_ = n_lift_fail_ = n_instr_skipped_ = n_output_rec_ = 0;
    n_arch_prctl_fs_ = 0;
    n_rep_overlong_ = n_patch_jump_ = n_lift_fail_kept_ = n_cv_used_ = n_cv_missing_ = 0;
    n_lift_fail_memory_ = n_rep_collapse_unknown_count_ = n_memory_omission_events_ = 0;
    n_lifted_memory_shortfall_ = 0;
    memory_omission_examples_.clear();
    n_cv_redefined_ = 0;
    n_sync_used_ = n_sync_resync_ = n_rmw_ = n_unanchored_rec_ = 0;
    n_delta_skipped_ = n_delta_unknown_ = n_delta_values_ = n_delta_reanchor_ = 0;
    n_kf_taken_ = n_kf_reanchor_ = n_kf_site_defs_ = 0;
    n_tramp_mismatch_ = n_tramp_unknown_ = n_fb_logsite_ = 0;
    vi_.n_unknown_ops = vi_.n_ccall_known = vi_.n_ccall_unknown = 0;
    for (auto& is : imgs_) { is.records = 0; is.unknown = 0; }
    other_.records = 0; other_.unknown = 0;
}


// Load a buffer-sink critical-value file into `cv' (SPEC_FORMAT section 3).
//   "PTCV" (version 2): a per-thread file -- 16-byte header then raw 8-byte values.
//   "PTCW" (version 3): the MULTIPLEXED per-PROCESS file -- 16-byte header
//     then a sequence of blocks { u32 tid, u32 nbytes, <nbytes of 8-byte values> };
//     take only the blocks whose tid is `want_tid', in file order, which is the
//     program order of that thread's values (the drain writes each thread's buffers
//     in order).  Per-CPU/per-task Intel PT traces one thread, so the reconstructor
//     wants exactly that thread's values.
//   Anything else: treated as a legacy flat file (skip a 16-byte header).
static void load_cv_file(const std::string& path, std::vector<uint64_t>& cv,
                         uint32_t want_tid) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    unsigned char hdr[16];
    if (fread(hdr, 1, 16, f) != 16) { fclose(f); return; }
    uint32_t magic = hdr[0] | (hdr[1]<<8) | (hdr[2]<<16) | ((uint32_t)hdr[3]<<24);
    if (magic == 0x57435450u /* "PTCW" */) {
        uint32_t bh[2];
        while (fread(bh, 4, 2, f) == 2) {
            uint32_t tid = bh[0], nbytes = bh[1];
            size_t nvals = nbytes / 8;
            if (tid == want_tid) {
                size_t at = cv.size(); cv.resize(at + nvals);
                if (nvals && fread(&cv[at], 8, nvals, f) != nvals) { cv.resize(at); break; }
            } else if (fseek(f, nbytes, SEEK_CUR) != 0) break;
        }
    } else {
        // PTCV (v2) or legacy: flat 8-byte values after the 16-byte header.
        uint64_t v; while (fread(&v, 8, 1, f) == 1) cv.push_back(v);
    }
    fclose(f);
}

// Read the buffer sink's value stream.  Called by run(), and by ptrecon --jobs in the PARENT
// before it forks, so that all chunk children share the one copy instead of reading the file
// (167 MB on the whole-program CPython cell) once per worker.
void Recon::preload_values() {
    if (o_.cvfile.empty() || cv_loaded_) return;
    load_cv_file(o_.cvfile, cv_, tid_);
    cv_loaded_ = true;
}

// ---------------------------------------------------------------------------------------------
// --delta-scan round 1.  Decode [skip_bytes, end_bytes) for LOGGING EVENTS ONLY: which logged value was
// written last in this range, and at which instruction index; plus the instruction index of the
// last state loss (a PT overflow or a decoder resync), which invalidates every slot the chunk did
// not see written afterwards.  Nothing here depends on the machine state, so the answer is exactly
// what a serial run would have in its table -- which is what makes the fold in run_parallel() an
// EXACT reconstruction of the log-on-change table at any chunk boundary.
//
// This pass does no VEX lifting, no interpretation, no shadow memory and emits no records, so it
// costs about a `--decode-only' pass (PT decoding dominates).
int Recon::run_delta_scan() {
    PtDecoder dec(o_.aux, sb_, o_.skip_bytes, o_.end_bytes);
    dec.no_time = true;
    const bool cut_at_end = o_.end_bytes && o_.end_bytes < PtDecoder::aux_size(o_.aux);
    preload_values();
    const size_t N = flat_ent_.size();
    std::vector<uint64_t> val(N, 0), seq(N, 0), sval(N, 0), sseq(N, 0);
    uint64_t loss = 0, sloss = 0, nseq = 0;
    bool snapped = (o_.scan_snap_at == 0);
    std::deque<uint64_t> q;               // payloads seen before their logging instruction
    long pend = -1;                       // a logging instruction waiting for its payload
    bool pend_sync = false;               // a buffer-sink sync marker waiting for its payload
    PtEvents ev;
    ev.on_ptwrite = [&](uint64_t payload, int, uint64_t) {
        if (pend_sync) { if (payload <= cv_.size()) cv_pos_ = (size_t)payload; pend_sync = false; return; }
        if (pend >= 0) { val[pend] = payload; seq[pend] = nseq; pend = -1; return; }
        q.push_back(payload);
    };
    auto state_loss = [&]() { loss = nseq ? nseq : 1; q.clear(); pend = -1; pend_sync = false; };
    ev.on_overflow = [&](uint64_t) { n_ovf_++; state_loss(); };
    ev.on_resync = [&](int, uint64_t) { state_loss(); };
    ev.on_enable = [&](uint64_t, bool) {};
    struct pt_insn insn; uint64_t tsc = 0;
    while (dec.next(insn, &tsc, ev)) {
        if (cut_at_end && dec.offset() >= o_.end_bytes) break;
        nseq = dec.n_insn;
        if (!snapped && dec.offset() >= o_.scan_snap_at) { sval = val; sseq = seq; sloss = loss; snapped = true; }
        const uint64_t ip = insn.ip;
        if (in_orig_code(ip)) continue;                 // program code: nothing is logged there
        auto se = sm_ent_.find(ip);
        if (se == sm_ent_.end()) {
            if (!sm_sync_.empty() && sm_sync_.count(ip)) {
                if (!q.empty()) { uint64_t p = q.front(); q.pop_front(); if (p <= cv_.size()) cv_pos_ = (size_t)p; }
                else pend_sync = true;
            }
            continue;
        }
        const uint32_t idx = se->second;
        const SiteMapEnt& e = flat_ent_[idx];
        bool buf = e.sink.empty() ? e.buffer : (e.sink == "buffer");
        if (buf) {
            if (cv_pos_ < cv_.size()) { val[idx] = cv_[cv_pos_]; seq[idx] = nseq ? nseq : 1; }
            cv_pos_++;
        } else if (!q.empty()) { val[idx] = q.front(); q.pop_front(); seq[idx] = nseq ? nseq : 1; }
        else pend = (long)idx;
    }
    if (!snapped) { sval = val; sseq = seq; sloss = loss; }
    FILE* f = fopen(o_.delta_scan_out.c_str(), "wb");
    if (!f) { perror("delta-scan-out"); return 1; }
    uint64_t hdr[4] = { 0x50544453u /* "PTDS" */, (uint64_t)N, sloss, loss };
    fwrite(hdr, 8, 4, f);
    fwrite(sval.data(), 8, N, f); fwrite(sseq.data(), 8, N, f);
    fwrite(val.data(),  8, N, f); fwrite(seq.data(),  8, N, f);
    fclose(f);
    if (o_.verbose) fprintf(stderr, "delta scan [%#lx,%#lx): %lu instructions, %lu overflows\n",
                            (unsigned long)o_.skip_bytes, (unsigned long)o_.end_bytes,
                            (unsigned long)dec.n_insn, (unsigned long)n_ovf_);
    return 0;
}

int Recon::run() {
    auto t0 = std::chrono::steady_clock::now();
    if (!o_.gt_in.empty() && !smaps_.empty() && n_smap_ok_ == 0) {
        fprintf(stderr, "ptrecon: REFUSING to compare against --gt-in: %zu site map(s) given and none "
                        "applied, so the reconstruction would consume no logged value and no gt site "
                        "would be recognised -- the comparison would be meaningless\n", smaps_.size());
        return 2;
    }
    PtDecoder dec(o_.aux, sb_, o_.skip_bytes, o_.end_bytes); dec.no_time = o_.no_time;
    // See the chunk-partition note in the decode loop.  The LAST chunk's `end_bytes' is the file
    // size, where `offset() == end_bytes' is the ordinary end of the trace and must not drop the
    // instructions decoded there.
    const bool cut_at_end = o_.end_bytes && o_.end_bytes < PtDecoder::aux_size(o_.aux);
    warming_ = o_.emit_from > o_.skip_bytes;
    // The load bases of every image already come from the sideband; the TLS base does not.
    // It is a process constant for a single-threaded target, so a chunk child may start with it.
    if (o_.no_seed_fs) o_.fs_base = 0;          // ablation: do not inherit the TLS base at all
    if (o_.seed_fs && o_.fs_base) vi_.set_reg(OFFSET_amd64_FS_CONST, 8, o_.fs_base);
    if (!o_.out.empty()) { out_ = fopen(o_.out.c_str(), "wb"); if (!out_) { perror("out"); return 1; } setvbuf(out_, nullptr, _IOFBF, 1 << 20); mtrace_hdr h{}; h.magic = MTRACE_MAGIC; h.version = MTRACE_VERSION; h.tid_count = 1; h.flags = 0; fwrite(&h, sizeof h, 1, out_); }
    preload_values();
    // --cv-audit: one line per sync marker, so a cursor drift can be localised.
    if (!o_.cv_audit.empty()) { cv_audit_ = fopen(o_.cv_audit.c_str(), "w");
        if (cv_audit_) fprintf(cv_audit_, "# marker aux_off ip payload cv_pos payload-cv_pos resyncs insns tramp_unknown fb_logsite\n"); }
    PtEvents ev;
    ev.on_ptwrite = [&](uint64_t payload, int size, uint64_t) { if (ptw_pending_) define_from_ptw(payload, size); else ptw_q_.push_back({payload, size}); };
    // A gap in the instruction stream may have changed any register, so the whole machine state
    // goes unknown -- with ONE exception: the TLS
    // base `guest_FS_CONST' is a PROCESS constant for a single-threaded target.  Nothing but
    // `arch_prctl(ARCH_SET_FS)' can change it, and that is a syscall the decoder sees (handled
    // below).  Keeping it is worth 4.2 % of the unknown records on whole-program CPython, and it
    // is what makes `%fs:' `memop' addresses survive an overflow.
    auto state_loss = [&]() {
        Val fs = vi_.get_reg(OFFSET_amd64_FS_CONST, 8);
        vi_.all_unknown();
        if (fs.known()) vi_.set_reg(OFFSET_amd64_FS_CONST, 8, fs.u64());
        else if (o_.fs_base) vi_.set_reg(OFFSET_amd64_FS_CONST, 8, o_.fs_base);
    };
    FILE* rlog = o_.resync_log.empty() ? nullptr : fopen(o_.resync_log.c_str(), "w");
    if (rlog) fprintf(rlog, "# kind aux_off insn ip err\n");
    uint64_t last_good_ip = 0, first_decoded_ip = 0;
    const char* stop_reason = nullptr;
    uint8_t last_decoded_bytes[15] = {}; int last_decoded_size = 0;
    ev.on_overflow = [&](uint64_t) { if (rlog) fprintf(rlog, "OVF %#lx %lu %#lx -\n", (unsigned long)dec.offset(), (unsigned long)dec.n_insn, (unsigned long)last_good_ip);
                                     if (o_.verbose) fprintf(stderr, "overflow at aux offset %#lx (insn %lu)\n", (unsigned long)dec.offset(), (unsigned long)dec.n_insn); n_ovf_++; in_ovf_ = true; gt_arm_resync(); state_loss(); ptw_q_.clear(); ptw_pending_ = false; ptw_pend_ent_ = -1; pending_delta_ = -1; pending_kf_ = -1; kf_fired_branch_ = 0; delta_invalidate(); };
    ev.on_resync = [&](int err, uint64_t off) { if (rlog) fprintf(rlog, "RESYNC %#lx %lu %#lx %s\n", (unsigned long)(off ? off : dec.offset()), (unsigned long)dec.n_insn, (unsigned long)last_good_ip, pt_errstr(pt_errcode(err)));
                                                 if (o_.verbose) fprintf(stderr, "resync: %s at aux offset %#lx\n", pt_errstr(pt_errcode(err)), (unsigned long)off); state_loss(); gt_arm_resync(); pending_delta_ = -1; pending_kf_ = -1; kf_fired_branch_ = 0; ptw_pend_ent_ = -1; delta_invalidate(); };
    ev.on_enable = [&](uint64_t, bool en) { (void)en; };
    struct pt_insn insn; uint64_t tsc = 0; uint64_t last_ip_orig = 0; bool in_tramp = false;
    uint64_t prev_end = 0;      // end of the previous instruction (fall-through address)
    uint64_t rip_ = 0, ts_ = 0; bool eo = true;
    std::function<void(const MemAccess&)> emitf = [&](const MemAccess& m) { if (eo) acc_.push_back(m); };
    while (dec.next(insn, &tsc, ev)) {
        if (!first_decoded_ip) first_decoded_ip = insn.ip;
        last_good_ip = insn.ip;
        last_decoded_size = insn.size;
        memcpy(last_decoded_bytes, insn.raw, insn.size);
        // ---- the chunk partition -----------------------------------------------------------
        // An instruction belongs to the chunk whose [aux_from, aux_to) contains the decoder's
        // offset AFTER it was decoded, and BOTH ends of that test have to be made, by both
        // neighbours, with the same predicate.  Only the start was: a chunk began emitting at
        // `dec.offset() >= emit_from' while its predecessor emitted everything its byte range let
        // it decode -- and libipt hands out the instructions of the TNT packet that ENDS at the
        // boundary PSB with `offset() == end_bytes', so the predecessor would emit them too.
        // Every chunk boundary would then DUPLICATE a handful of records, which shifts the whole
        // concatenated trace against the serial one.
        if (cut_at_end && dec.offset() >= o_.end_bytes) { stop_reason = "chunk_end"; break; }
        if (warming_ && dec.offset() >= o_.emit_from) {   // the chunk's own range starts here
            warming_ = false; warm_insn_ = dec.n_insn;
            dec.n_insn = dec.n_ovf = dec.n_resync = dec.n_ptw = 0;
            reset_counters();
        }
        if (o_.max_insn && dec.n_insn > o_.max_insn) { stop_reason = "max_insn"; break; }
        // The bounded ground truth ran out: everything after it is outside the compared window.
        if (gt_exhausted_ && !o_.gt_continue) { stop_reason = "oracle_end"; break; }
        if (o_.decode_only) continue;
        uint64_t ip = insn.ip;
        // Did control REACH this ip by a taken transfer, or by falling through?  Only a
        // taken transfer can start a new instrumentation block, so this keeps the block-entry
        // re-check below off every instruction inside a trampoline.
        const bool taken_here = (ip != prev_end);
        prev_end = ip + (uint64_t)insn.size;
        // A log-on-change guard's `je' was the previous instruction: if control landed on the
        // join label the logging instruction did NOT run and the value comes from the table.
        if (pending_delta_ >= 0) {
            long idx = pending_delta_; pending_delta_ = -1;
            if (ip == flat_ent_[idx].join_addr) delta_take_from_table(idx);
        }
        // A keyframe counter's `jnz' was the previous instruction: landing anywhere but on its join
        // label means the countdown reached zero and the logging path RAN.  Nothing has to be
        // replayed either way -- the logging instructions are in the stream when they execute --
        // so this only counts how often the mechanism fired.
        if (pending_kf_ >= 0) {
            long idx = pending_kf_; pending_kf_ = -1;
            if (ip != flat_ent_[idx].kf_join) { n_kf_taken_++; kf_fired_branch_ = flat_ent_[idx].kf_branch; }
            else kf_fired_branch_ = 0;
        }
        const InsnInfo& ii = info(ip, insn.raw, insn.size);
        bool orig = ii.orig;
        if (o_.verbose > 2) fprintf(stderr, "ip %#lx %s len=%d\n", (unsigned long)ip, orig ? "orig" : "INSTR", insn.size);
        uint64_t rec_ip = ip; bool emit_ok = true;
        // What to interpret: normally the instruction the PT stream just gave us; for a relocated
        // copy inside a trampoline, the ORIGINAL instruction at its ORIGINAL address instead (see
        // below), which is both semantically right and immune to E9Patch's re-encodings.
        const uint8_t* x_raw = insn.raw; int x_len = insn.size; uint64_t x_ip = ip; bool x_orig_bytes = false;
        // ---- The site map is authoritative for a LOGGING instruction wherever it appears.  `tramp_unmapped()' switches the WHOLE trampoline to the byte-matching cursor,
        // and E9Patch chains one trampoline into another without passing through original code, so
        // a trampoline the map does not describe would swallow the logging instructions of the
        // ones it jumps into.  With the PTWRITE sink that only loses those values; with the BUFFER
        // sink the cv cursor is POSITIONAL, so every swallowed store leaves the reconstructor one
        // value behind and it hands out other sites' values until the next sync marker pulls it
        // straight.  Entries, guards and sync markers are absolute addresses: consume them from the map and
        // leave the rest of the trampoline on the fallback cursor.
        // ---- `in_tramp' is NOT "still inside the block the site map described".
        // E9Patch chains one trampoline straight into the next, and it also parks instructions it
        // EVICTED on its own account (the T2/T3 tactics) in little blocks of their own -- the
        // evicted instruction plus a `jmp' back.  runtime/rewrite.py maps those by scanning
        // original code for a *bare* `e9 <rel32>' patch jump, so when the patch jump is the
        // REX-punned `48 e9 <rel32>' the candidate address is one byte inside the real
        // instruction, the byte confirmation fails, and the block is MISSING from the site map.
        // Reaching it by a chained jump would leave `in_tramp' true, the map path would keep its
        // authority, and the block would be skipped as "pure instrumentation" -- silently dropping
        // the effect of a real program instruction (a stack-pointer adjustment in an epilogue,
        // say, which then makes every later stack address wrong by that amount).
        // So re-evaluate at every KNOWN BLOCK ENTRY reached by a taken transfer.  `tramp_entry_'
        // holds exactly the targets of patch jumps found in ORIGINAL code (build_tramp_map), the
        // punned jump left behind at such a site is one of them, so the fallback byte-matching
        // cursor is seeded with the right origin and replays the instruction correctly.
        bool map_ok = false, fb_site = false, new_block = false;
        if (!orig && smap_ok_) {
            sm_fill(ii, ip);          // one memoised pass over the site-map tables, not nine lookups
            new_block = in_tramp && taken_here && (ii.sm_flags & SMF_TRAMPENT) != 0;
            map_ok = !((in_tramp && !new_block) ? tramp_fallback_ : sm_unmapped(ii));
            if (!map_ok && (ii.sm_flags & (SMF_ENT | SMF_SYNC | SMF_JE | SMF_KF))) {
                map_ok = true; fb_site = true; n_fb_logsite_++;
            }
        }
        // `tramp_cv_' only ever describes the trampoline being executed right now -- the
        // marker that can correct a value is inside the same trampoline as the store.  Values
        // consumed in EARLIER trampolines must not be touched: before the first marker most of
        // them are the decoder's phantoms (a trampoline whose patched page was not yet installed
        // when the code really ran), which correspond to no execution at all.
        if (orig) { in_tramp = false; tramp_fallback_ = false; cursor_valid_ = false; last_ip_orig = ip; in_ovf_ = false;
                    if (pre_first_sync_ && !tramp_cv_.empty()) tramp_cv_.clear(); }
        else if (map_ok) {
            // ---- Inside E9Patch instrumentation, with the site map (the normal case) ----------
            // The site map is authoritative: an address is either a logging instruction, a sync
            // marker, the (start of the) copy of an original instruction, or pure instrumentation.
            // Only copies of ORIGINAL instructions produce trace records; everything the rewriter
            // added (scratch push/pop, `mov %fs:0,%scr', the buffer store, E9Patch's own jump-back
            // and scratch spills) is skipped entirely -- the trampolines are register- and
            // memory-transparent by construction, so skipping them keeps the machine state exact.
            in_tramp = true; if (!fb_site) tramp_fallback_ = false;
            if (ii.sm_flags & SMF_KF) { n_instr_skipped_++; pending_kf_ = (long)ii.sm_kf_idx; continue; }
            if (ii.sm_flags & SMF_JE) { n_instr_skipped_++; pending_delta_ = (long)ii.sm_je_idx; continue; }
            if (ii.sm_flags & SMF_ENT) {
                const uint32_t se_idx = ii.sm_ent_idx;
                n_instr_skipped_++;
                const SiteMapEnt& e = flat_ent_[se_idx];
                if (e.kf_period) kf_note(se_idx);
                if (e.buffer) {                                     // value comes from cv.*.bin
                    if (cv_pos_ < cv_.size()) { define_value(e, cv_[cv_pos_]);
                        if (e.delta) { delta_val_[se_idx] = cv_[cv_pos_]; delta_valid_[se_idx] = 1; }
                        n_cv_used_++; anchored_ = true; }
                    else n_cv_missing_++;
                    if (pre_first_sync_) tramp_cv_.emplace_back((uint32_t)se_idx, cv_pos_);  // see tramp_cv_
                    cv_pos_++;
                } else {                                            // value comes from a PTW packet
                    ptw_pend_ent_ = (long)se_idx;
                    if (!arm_value_site(e)) { ptw_pending_ = true; ptw_kind_ = PTW_NONE; ptw_pend_off_ = -1; ptw_pend_mem_legacy_ = false; }
                    if (!ptw_q_.empty()) { auto p = ptw_q_.front(); ptw_q_.pop_front(); define_from_ptw(p.first, p.second); }
                }
                continue;
            }
            if (ii.sm_flags & SMF_SYNC) {                           // buffer sink realignment marker
                n_instr_skipped_++;
                sync_ip_ = ip; sync_off_ = dec.offset();                // --cv-audit
                sync_nres_ = dec.n_resync; sync_ninsn_ = dec.n_insn;
                ptw_pending_ = true; ptw_kind_ = PTW_SYNC;
                if (!ptw_q_.empty()) { auto p = ptw_q_.front(); ptw_q_.pop_front(); define_from_ptw(p.first, p.second); }
                continue;
            }
            if (!(ii.sm_flags & SMF_REL)) { n_instr_skipped_++; continue; }   // pure instrumentation
            const RelocEnt& rel = *ii.sm_relp;
            rec_ip = rel.orig;
            // orig_bytes_at() is itself a hash lookup keyed by an address derived from this ip;
            // memoise its answer in the same per-ip cache.
            if (!(ii.sm_flags & SMF_OB)) { ii.sm_ob = orig_bytes_at(rel.orig); ii.sm_flags |= SMF_OB; }
            const std::vector<uint8_t>* ob = ii.sm_ob;
            if (ob) { x_raw = ob->data(); x_len = (int)ob->size(); x_ip = rel.orig; x_orig_bytes = true; }
        }
        else {
            // ---- No usable site map (or a trampoline it does not describe): fall back to the
            // ---- byte-matching cursor heuristic ------------------------------------------------
            if (!in_tramp || new_block) { in_tramp = true; tramp_fallback_ = true; auto te = tramp_entry_.find(ip);
                if (te != tramp_entry_.end()) { cursor_ = te->second; cursor_valid_ = true; }
                else { const InsnInfo* li = nullptr; auto lit = insns_.find(last_ip_orig); if (lit != insns_.end()) li = &lit->second;
                    if (li && li->branch) { cursor_ = last_ip_orig; cursor_valid_ = true; } else { cursor_valid_ = false; n_tramp_unknown_++; } } }
            if (ii.ptwrite || ii.glue) emit_ok = false;
            else if (cursor_valid_) {
                uint8_t ob[16]; const InsnInfo* oi = nullptr;
                auto oit = orig_insns_.find(cursor_);
                if (oit != orig_insns_.end()) oi = &oit->second;
                else if (read_orig_bytes(cursor_, ob, 16)) { ZydisDecodedInstruction zi; if (ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&g_zd, nullptr, ob, 16, &zi))) { InsnInfo tmp = info(cursor_ ^ (1ull << 63), ob, zi.length); insns_.erase(cursor_ ^ (1ull << 63)); icache_drop(cursor_ ^ (1ull << 63)); tmp.sm_flags = 0; tmp.sm_relp = nullptr; tmp.sm_ob = nullptr; tmp.sm_oi = nullptr; tmp.sm_blk_orig = nullptr; tmp.sm_blk_self = nullptr; oi = &orig_insns_.emplace(cursor_, tmp).first->second; } }
                if (oi && oi->hash == ii.hash) { rec_ip = cursor_; cursor_ += oi->len; }
                else if (oi && ii.branch && oi->branch) { rec_ip = cursor_; cursor_ += oi->len; }
                else if (oi && ii.memsize > 0) { rec_ip = cursor_; cursor_ += oi->len; n_tramp_mismatch_++; }
                else emit_ok = (ii.memsize == 0);   // no record anyway
            } else if (ii.memsize > 0) { rec_ip = ip; n_tramp_mismatch_++; }
        }
        // --- logging instructions (self-describing PTWRITE, no site map) -----------
        if (ii.ptwrite && !x_orig_bytes) {
            ptw_pending_ = true; ptw_kind_ = PTW_NONE; ptw_pend_off_ = ii.ptw_reg_off; ptw_pend_size_ = ii.ptw_reg_size; ptw_pend_mem_legacy_ = ii.ptw_mem;
            if (ii.ptw_mem) { bool k; ptw_pend_ea_ = ea(ii, ip, &k); if (!k) { ptw_pend_mem_legacy_ = false; ptw_pend_off_ = -1; } }
            if (!ptw_q_.empty()) { auto p = ptw_q_.front(); ptw_q_.pop_front(); define_from_ptw(p.first, p.second); }
            continue;
        }
        // --- interpret -------------------------------------------------------------
        // E9Patch's punned `48 e9 <rel32>' patch jump.  VEX refuses REX.W + JMP rel32, so
        // every execution of one would be a `lift_failure'.  It is a jump: no memory operand, no register written, the real work is done by the
        // relocated copy in the trampoline.  Skip it and count it as what it is.
        if (!x_orig_bytes && ii.patch_jump) { n_patch_jump_++; continue; }
        const IRBlockC* blkp = nullptr;
        const InsnInfo* xip = nullptr;
        {
            // Both block caches are keyed by an address that is a function of this ip alone, and
            // a std::unordered_map node's target never moves, so the per-ip cache can hold the
            // pointer itself.  A byte change at `ip' throws the whole InsnInfo (and blocks_[ip])
            // away, which is exactly when these must be recomputed.
            const uint16_t want = x_orig_bytes ? SMF_BO : SMF_BS;
            const IRBlockC*& slot = x_orig_bytes ? ii.sm_blk_orig : ii.sm_blk_self;
            if (!(ii.sm_flags & want)) {
                auto& cache = x_orig_bytes ? orig_blocks_ : blocks_;
                auto bit = cache.find(x_ip);
                if (bit == cache.end()) bit = cache.emplace(x_ip, lifter_.lift_one(x_ip, x_raw, x_len)).first;
                slot = bit->second.get();
                ii.sm_flags |= want;
            }
            blkp = slot;
            if (x_orig_bytes) {
                if (!(ii.sm_flags & SMF_OI)) { ii.sm_oi = orig_info(x_ip); ii.sm_flags |= SMF_OI; }
                xip = ii.sm_oi;
            } else xip = &ii;
        }
        const IRBlockC& blk = *blkp;
        const InsnInfo& xi = xip ? *xip : ii;
        recent_original_ips_[recent_original_count_++ % 32] = rec_ip;
        if (!blk.ok) {
            // VEX cannot lift it (xsavec, AVX-512 EVEX, ...).  Do NOT throw the whole machine state
            // away: Zydis tells us exactly which 64-bit GP registers the instruction writes, so
            // everything else -- %rsp and the callee-saved registers in particular -- keeps its
            // value.  A state-save instruction writes no GP register at all.
            n_lift_fail_++;
            if (emit_ok && !warming_ && (!xi.ok || xi.n_mem_ops || xi.memsize)) {
                n_lift_fail_memory_++;
                bool known = xi.ok && xi.n_mem_ops;
                for (int mi = 0; mi < xi.n_mem_ops; mi++) {
                    bool k; (void)ea_desc(xi.mem_ops[mi], x_ip, xi.len, &k); known = known && k;
                }
                note_memory_omission("lift_failure", rec_ip, xi, x_raw, x_len, Val::U(8), known);
            }
            if (o_.verbose > 1) fprintf(stderr, "lift fail @%#lx: %s\n", (unsigned long)x_ip, blk.err.c_str());
            static const int GPOFF[16] = {OFFSET_amd64_RAX, OFFSET_amd64_RCX, OFFSET_amd64_RDX, OFFSET_amd64_RBX, OFFSET_amd64_RSP, OFFSET_amd64_RBP, OFFSET_amd64_RSI, OFFSET_amd64_RDI,
                                          OFFSET_amd64_R8, OFFSET_amd64_R9, OFFSET_amd64_R10, OFFSET_amd64_R11, OFFSET_amd64_R12, OFFSET_amd64_R13, OFFSET_amd64_R14, OFFSET_amd64_R15};
            if (!xi.ok) vi_.all_unknown();                 // not even decodable: nothing is provable
            else {
                n_lift_fail_kept_++;
                uint32_t w = xi.state_save ? 0u : xi.gpwrite;
                for (int r = 0; r < 16; r++) if (w & (1u << r)) vi_.set_reg_unknown(GPOFF[r], 8);
                if (xi.mem_write && !xi.state_save) {      // its stores may invalidate shadowed slots
                    bool k; uint64_t a = ea(xi, x_ip, &k);
                    if (k && xi.memsize > 0) vi_.shadow_forget(a, xi.memsize); else vi_.shadow_clear();
                }
            }
            continue; }
        rip_ = rec_ip; ts_ = tsc; eo = emit_ok;
        const uint64_t records_before = n_rec_;
        if (xi.rep) {
            // A `rep' string instruction is expanded one iteration per element.  The count comes
            // from %rcx and the address from %rdi/%rsi, and BOTH have to be trustworthy: a single
            // wrong logged value turns one instruction into an unbounded record storm.  Measured on
            // whole-program CPython (nbody, AUX offset 32 MB): a `rep stosb' in
            // __memset_avx2_unaligned_erms whose %rdi was unknown but whose %rcx was a stale
            // "known" value produced 231 MILLION records at one ip and made the run look like a
            // hang -- the old guard only stopped at 2^32 iterations.  So:
            //   * an unknown destination address means the expansion teaches us nothing -> emit one
            //     unknown record, exactly as an unknown %rcx does;
            //   * an implausible count (> --max-rep, default 2^20) is treated as not known either,
            //     and reported as `rep_overlong' so it cannot hide.
            Val rcx = vi_.get_reg(OFFSET_amd64_RCX, 8);
            // Check EVERY memory operand, including the ones Zydis marks hidden -- the [%rdi] of
            // `rep stos' and both [%rsi] and [%rdi] of `rep movs' are hidden, so a test on
            // `xi.memsize' alone would leave `addr_known' unconditionally true.
            bool addr_known = true;
            int rep_size = 0;
            for (int mi = 0; mi < xi.n_mem_ops; mi++) {
                bool k; (void)ea_desc(xi.mem_ops[mi], x_ip, xi.len, &k);
                if (!k) addr_known = false;
                if (!rep_size) rep_size = xi.mem_ops[mi].size;
            }
            if (!xi.n_mem_ops && xi.memsize > 0) { (void)ea(xi, x_ip, &addr_known); rep_size = xi.memsize; }
            uint64_t n = rcx.known() ? rcx.u64() : 0;
            bool overlong = rcx.known() && o_.max_rep && n > o_.max_rep;
            if (overlong) n_rep_overlong_++;
            if (!rcx.known() || !addr_known || overlong) {
                if (eo && !warming_) {
                    if (!rcx.known()) n_rep_collapse_unknown_count_++;
                    note_memory_omission("collapsed_rep", rip_, xi, x_raw, x_len, rcx, addr_known);
                }
                n_rep_unknown_++; if (eo) emit(0, false, rep_size ? rep_size : xi.memsize, MT_RMW, rip_, ts_);
                vi_.set_reg_unknown(OFFSET_amd64_RSI, 8); vi_.set_reg_unknown(OFFSET_amd64_RDI, 8);
                vi_.set_reg_unknown(OFFSET_amd64_RAX, 8); vi_.set_reg_unknown(OFFSET_amd64_RCX, 8);
            }
            else { for (uint64_t i = 0; i < n; i++) { vi_.exec(blk, emitf); flush_acc(xi, rip_, ts_); Val r2 = vi_.get_reg(OFFSET_amd64_RCX, 8); if (!r2.known() || r2.u64() == 0) break; } }
        } else {
            // The syscall NUMBER is in %rax BEFORE the instruction: read it first, because
            // `arch_prctl(ARCH_SET_FS)' is the only thing in a single-threaded process that can
            // change `guest_FS_CONST', which state_loss() above deliberately preserves.
            const bool is_sys = blk.jk == Ijk_Sys_syscall || blk.jk == Ijk_Sys_int128;
            Val sysno = is_sys ? vi_.get_reg(OFFSET_amd64_RAX, 8) : Val::U(8);
            // `arch_prctl' is not by itself a reason to forget the TLS base.  Its FIRST argument
            // (%rdi) says WHICH base it touches, and the buffer-sink runtime issues
            // `arch_prctl(ARCH_SET_GS)' once per thread (the trampolines' `%gs:0' cursor), through
            // glibc's generic `syscall()' wrapper.  Treating that as an ARCH_SET_FS would throw
            // `guest_FS_CONST' away for the REST of the thread, so every later `%fs:'-relative
            // access would be unknown.  Forget the base only for ARCH_SET_FS (0x1002) or an
            // unknown code.
            Val syscode = is_sys ? vi_.get_reg(OFFSET_amd64_RDI, 8) : Val::U(8);
            vi_.exec(blk, emitf);
            flush_acc(xi, rip_, ts_);
            if (is_sys) { vi_.set_reg_unknown(OFFSET_amd64_RAX, 8); vi_.set_reg_unknown(OFFSET_amd64_RCX, 8); vi_.set_reg_unknown(OFFSET_amd64_R11, 8);
                          if (sysno.known() && sysno.u64() == 158 /* arch_prctl */ &&
                              (!syscode.known() || syscode.u64() == 0x1002 /* ARCH_SET_FS */)) {
                              vi_.set_reg_unknown(OFFSET_amd64_FS_CONST, 8); n_arch_prctl_fs_++; } }
        }
        // Successful lifting need not imply memory emission (e.g. an unsupported
        // VEX dirty helper). Record such cases for instruction-level review.
        // Conditional memory operations/hints can legitimately emit nothing, so
        // this is evidence of a potential omission, not an assumed access count.
        if (eo && !warming_ && !xi.rep && n_rec_ - records_before < (uint64_t)xi.n_mem_ops) {
            n_lifted_memory_shortfall_++;
            note_memory_omission("lifted_memory_shortfall", rip_, xi, x_raw, x_len, Val::U(8), false);
        }
    }
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (rlog) fclose(rlog);
    if (out_) fclose(out_);
    if (cv_audit_) { fclose(cv_audit_); cv_audit_ = nullptr; }
    gt_finish();
    if (gt_on_)
        fprintf(stderr, "gt: compared %lu records: %lu identical (%.4f %%), %lu unknown, %lu wrong "
                "(%.4f %%); %lu excluded, %lu gt-only, %lu recon-only, %lu gt tail\n",
                (unsigned long)(g_identical_ + g_unknown_ + g_wrong_), (unsigned long)g_identical_,
                (g_identical_ + g_unknown_ + g_wrong_) ? 100.0 * (double)g_identical_ / (double)(g_identical_ + g_unknown_ + g_wrong_) : 0.0,
                (unsigned long)g_unknown_, (unsigned long)g_wrong_,
                (g_identical_ + g_unknown_ + g_wrong_) ? 100.0 * (double)g_wrong_ / (double)(g_identical_ + g_unknown_ + g_wrong_) : 0.0,
                (unsigned long)g_excluded_,
                (unsigned long)g_gt_only_, (unsigned long)g_recon_only_,
                (unsigned long)g_gt_tail_);
    // Per-image breakdown (by the record's ip, using the sideband maps), so a report can separate
    // the program's own code from libraries no stage of the pipeline has analysed, without heuristics.
    // One JSON entry per FILE, not per mapping: E9Patch gives an image thousands of mappings, and
    // the reader wants "how much of the trace was this library", not a page list.
    std::vector<ImgStat> agg;
    { std::unordered_map<std::string, size_t> at;
      for (auto& is : imgs_) { if (!is.records) continue;
          auto it = at.find(is.path);
          if (it == at.end()) { at[is.path] = agg.size(); ImgStat c = is; agg.push_back(c); }
          else { ImgStat& a = agg[it->second]; a.records += is.records; a.unknown += is.unknown;
                 if (is.lo < a.lo) a.lo = is.lo; if (is.hi > a.hi) a.hi = is.hi; a.orig = a.orig || is.orig; } } }
    std::sort(agg.begin(), agg.end(), [](const ImgStat& a, const ImgStat& b) { return a.records > b.records; });
    std::string imgjs = "["; bool first_img = true;
    auto add_img = [&](const ImgStat& is) {
        if (!is.records) return;
        imgjs += std::string(first_img ? "" : ",") + "{\"path\":" + json_quote(is.path) + ",\"start\":" + std::to_string(is.lo) +
                 ",\"end\":" + std::to_string(is.hi) + ",\"code\":" + (is.orig ? "\"original\"" : "\"instrumentation\"") +
                 ",\"records\":" + std::to_string(is.records) + ",\"unknown_addr\":" + std::to_string(is.unknown) + "}";
        first_img = false; };
    for (auto& is : agg) add_img(is);
    add_img(other_);
    imgjs += "]";
    std::string smapjs = "["; for (size_t i = 0; i < smaps_.size(); i++) {
        size_t nent = 0; for (auto& e : flat_ent_) if (e.map == (int)i) nent++;
        smapjs += std::string(i ? "," : "") + "{\"image\":" + json_quote(smaps_[i].image) + ",\"base\":" + std::to_string(smap_bases_[i]) +
                  ",\"sink\":" + json_quote(smaps_[i].sink) + ",\"status\":" + (smaps_[i].applied ? "\"applied\"" : (smaps_[i].mapped ? "\"ignored\"" : "\"image-not-mapped\"")) +
                  ",\"logged_values\":" + std::to_string(nent) + "}"; }
    smapjs += "]";
    std::string omissions = "[";
    for (size_t i = 0; i < memory_omission_examples_.size(); i++)
        omissions += (i ? "," : "") + memory_omission_examples_[i];
    omissions += "]";
    std::string tail_bytes;
    static const char hex_digits[] = "0123456789abcdef";
    for (int i = 0; i < last_decoded_size; i++) {
        tail_bytes += hex_digits[last_decoded_bytes[i] >> 4];
        tail_bytes += hex_digits[last_decoded_bytes[i] & 15];
    }
    std::string js = "{\"records\":" + std::to_string(n_rec_) + ",\"unknown_addr\":" + std::to_string(n_unknown_) + ",\"instructions\":" + std::to_string(dec.n_insn) +
        ",\"output_records\":" + std::to_string(n_output_rec_) +
        ",\"decode_window\":{\"first_ip\":" + std::to_string(first_decoded_ip) +
        ",\"last_ip\":" + std::to_string(last_good_ip) + ",\"last_bytes\":" + json_quote(tail_bytes) +
        ",\"stop_reason\":" + json_quote(stop_reason ? stop_reason : "decoder_end") +
        ",\"end_offset\":" + std::to_string(stop_reason ? dec.offset() : dec.eos_off) + ",\"end_status\":" + std::to_string(dec.eos_status) +
        ",\"end_reason\":" + json_quote(dec.eos_why) + "}" +
        ",\"memory_omission_events\":" + std::to_string(n_memory_omission_events_) +
        ",\"lift_failed_memory_instructions\":" + std::to_string(n_lift_fail_memory_) +
        ",\"lifted_memory_shortfall\":" + std::to_string(n_lifted_memory_shortfall_) +
        ",\"rep_collapsed_unknown_count\":" + std::to_string(n_rep_collapse_unknown_count_) +
        ",\"memory_omission_examples\":" + omissions +
        ",\"e9_swaps\":" + std::to_string(dec.n_e9_swap) + ",\"e9_swaps_triggered\":" + std::to_string(dec.n_e9_swap_trig) +
        ",\"e9_preinit_insn\":" + std::to_string(dec.n_e9_preinit_insn) + ",\"e9phase\":" + dec.e9_json() + ",\"pt_overflows\":" + std::to_string(dec.n_ovf) + ",\"resyncs\":" + std::to_string(dec.n_resync) + ",\"sync_failures\":" + std::to_string(dec.n_sync_fail) + ",\"psb_repaired\":" + std::to_string(dec.n_psb_repaired) + ",\"ptw_packets\":" + std::to_string(dec.n_ptw) + ",\"ptw_used\":" + std::to_string(n_ptw_used_) +
        ",\"ptw_unconsumed\":" + std::to_string(ptw_q_.size()) + ",\"lift_failures\":" + std::to_string(n_lift_fail_) + ",\"lift_fail_state_kept\":" + std::to_string(n_lift_fail_kept_) +
        ",\"patch_jumps\":" + std::to_string(n_patch_jump_) +
        ",\"warm_instructions\":" + std::to_string(warm_insn_) + ",\"aux_emit_from\":" + std::to_string(o_.emit_from) +
        ",\"arch_prctl_set_fs\":" + std::to_string(n_arch_prctl_fs_) + ",\"rep_unknown_count\":" + std::to_string(n_rep_unknown_) + ",\"rep_overlong\":" + std::to_string(n_rep_overlong_) + ",\"rmw_records\":" + std::to_string(n_rmw_) +
        ",\"sitemap\":" + (smaps_.empty() ? "\"absent\"" : (n_smap_bad_ == 0 ? "\"applied\"" : (n_smap_ok_ ? "\"partial\"" : "\"ignored\""))) + ",\"sitemap_base\":" + std::to_string(smap_base_) +
        ",\"sitemaps\":" + smapjs + ",\"sink\":" + json_quote(smaps_.empty() ? std::string("ptwrite") : smaps_[0].sink) +
        ",\"instr_skipped\":" + std::to_string(n_instr_skipped_) + ",\"cv_values\":" + std::to_string(cv_.size()) + ",\"cv_used\":" + std::to_string(n_cv_used_) +
        ",\"cv_missing\":" + std::to_string(n_cv_missing_) + ",\"cv_redefined\":" + std::to_string(n_cv_redefined_) + ",\"sync_markers\":" + std::to_string(n_sync_used_) + ",\"sync_realigned\":" + std::to_string(n_sync_resync_) +
        ",\"unknown_ops\":" + std::to_string(vi_.n_unknown_ops) + ",\"ccall_known\":" + std::to_string(vi_.n_ccall_known) + ",\"ccall_unknown\":" + std::to_string(vi_.n_ccall_unknown) +
        ",\"unanchored_records\":" + std::to_string(n_unanchored_rec_) +
        ",\"delta_values\":" + std::to_string(n_delta_values_) + ",\"delta_skipped\":" + std::to_string(n_delta_skipped_) +
        ",\"delta_unknown\":" + std::to_string(n_delta_unknown_) + ",\"delta_reanchored\":" + std::to_string(n_delta_reanchor_) +
        ",\"keyframe_values\":" + std::to_string(n_kf_values_) + ",\"keyframe_resync_values\":" + std::to_string(n_kf_resync_values_) +
        ",\"keyframes_taken\":" + std::to_string(n_kf_taken_) + ",\"keyframe_reanchors_after_resync\":" + std::to_string(n_kf_reanchor_) +
        ",\"keyframe_site_defs\":" + std::to_string(n_kf_site_defs_) +
        ",\"aux_skip_bytes\":" + std::to_string(o_.skip_bytes) + ",\"aux_end_bytes\":" + std::to_string(o_.end_bytes) +
        ",\"tid\":" + std::to_string(tid_) +
        ",\"fb_logsite\":" + std::to_string(n_fb_logsite_) + ",\"tramp_mismatch\":" + std::to_string(n_tramp_mismatch_) + ",\"tramp_unknown_origin\":" + std::to_string(n_tramp_unknown_) + ",\"distinct_insns\":" + std::to_string(blocks_.size() + orig_blocks_.size()) +
        ",\"jit\":null" +
        ",\"gt\":" + gt_json() +
        ",\"images\":" + imgjs + ",\"wall_s\":" + std::to_string(wall) + ",\"insn_per_s\":" + std::to_string(wall > 0 ? dec.n_insn / wall : 0) + "}\n";
    if (!o_.summary.empty()) { FILE* f = fopen(o_.summary.c_str(), "w"); if (f) { fputs(js.c_str(), f); fclose(f); } }
    fprintf(stderr, "%s", js.c_str());
    // A whole-run reconstruction (not a --jobs chunk) of a buffer-sink build that was given --cv
    // and consumed NOTHING from it: the value stream and the trace do not belong together (wrong
    // cv file, wrong thread, an empty file), and every buffer-sink value came out unknown.  The
    // summary above says so; the exit status makes sure a driver cannot miss it.
    if (!o_.cvfile.empty() && !o_.skip_bytes && !o_.end_bytes && n_cv_used_ == 0 && has_buffer_values()) {
        fprintf(stderr, "ptrecon: REFUSING the result: --cv %s was given for a buffer-sink build and cv_used == 0 "
                        "(%zu values in the file, %lu buffer-sink reads missed)\n",
                o_.cvfile.c_str(), cv_.size(), (unsigned long)n_cv_missing_);
        return 3;
    }
    return 0;
}
