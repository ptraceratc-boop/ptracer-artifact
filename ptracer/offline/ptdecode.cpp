// ptdecode.cpp -- see ptdecode.h
#include <algorithm>
#include "ptdecode.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>

Sideband Sideband::load(const std::string& path) {
    Json j = json_load(path); Sideband s;
    s.pid = (int)j["pid"].i64(); s.exit_code = (int)j["exit"].i64(); s.aux_bytes = j["aux_bytes"].u64(); s.wrapped = j["wrapped"].i64() != 0;
    s.mtc_period = (int)j["mtc_period"].i64(3);
    s.family = (int)j["cpu"]["family"].i64(6); s.model = (int)j["cpu"]["model"].i64(); s.stepping = (int)j["cpu"]["stepping"].i64();
    s.cpuid15_eax = (uint32_t)j["cpuid15"]["eax"].u64(); s.cpuid15_ebx = (uint32_t)j["cpuid15"]["ebx"].u64();
    s.time_mult = (uint32_t)j["time"]["mult"].u64(); s.time_shift = (uint32_t)j["time"]["shift"].u64(); s.time_zero = j["time"]["zero"].u64();
    s.fs_base = j["fs_base"].u64();
    for (auto& m : j["maps"].arr) { MapEnt e; e.start = m["start"].u64(); e.end = m["end"].u64(); e.off = m["offset"].u64(); e.perms = m["perms"].str(); e.path = m["path"].str(); s.maps.push_back(e); }
    // sideband version 2 (per-CPU capture): additive keys, absent from older files.
    s.sb_version = (int)j["sb_version"].i64(1);
    for (auto& c : j["cpus"].arr) { Sideband::Cpu e; e.cpu = (int)c["cpu"].i64(-1); e.aux = c["aux"].str(); e.switch_file = c["switch_file"].str();
        e.aux_bytes = c["aux_bytes"].u64(); e.lost = c["lost_bytes"].u64(); e.trunc = c["truncated"].u64(); e.n_switch = c["n_switch"].u64(); e.data_lost = c["data_lost"].u64();
        s.cpus.push_back(e); }
    for (auto& t : j["threads"].arr) { Sideband::Thread e; e.tid = (uint32_t)t["tid"].u64(); e.fs_base = t["fs_base"].u64(); s.threads.push_back(e); }
    return s;
}

uint64_t PtDecoder::aux_size(const std::string& auxfile) {
    struct stat st; if (stat(auxfile.c_str(), &st)) return 0; return (uint64_t)st.st_size;
}
// A PSB packet is 16 bytes of 02 82 repeated; it is the only packet a decoder can synchronise on,
// so it is where an AUX stream may be cut into independently decodable chunks.
std::vector<uint64_t> PtDecoder::psb_offsets(const std::string& auxfile) {
    std::vector<uint64_t> out;
    int fd = open(auxfile.c_str(), O_RDONLY); if (fd < 0) return out;
    struct stat st; if (fstat(fd, &st) || st.st_size < 16) { close(fd); return out; }
    size_t n = (size_t)st.st_size;
    const uint8_t* p = (const uint8_t*)mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return out;
    static const uint8_t PSB[16] = {0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82};
    for (size_t i = 0; i + 16 <= n; ) {
        const void* h = memmem(p + i, n - i, PSB, 16);
        if (!h) break;
        size_t off = (const uint8_t*)h - p;
        out.push_back(off);
        i = off + 16;
    }
    munmap((void*)p, n);
    return out;
}

// ---- A PSB+ header the hardware never finished ----------------------------------------------
//
// A PSB packet opens a PSB+ HEADER that must be closed by PSBEND; only PAD, TSC, TMA, CBR, MODE,
// PIP, VMCS and FUP may appear inside it.  A PSB that the periodic PSB counter emits while the
// trace is being stopped and restarted at an output-region boundary can be left OPEN: the header
// carries its TSC and TMA, the trace then stops, and when it resumes the hardware emits a fresh
// TSC/TMA/CBR -- but no second PSB and no PSBEND -- so the next MTC arrives INSIDE the header and
// libipt fails the whole stream with `unexpected packet context'.  Such headers sit at the
// output-region boundary (offset 0xfd0 of a 4 KiB page) with no OVF packet, no lost AUX bytes and
// no truncated region: nothing is missing from the byte stream, only the header's terminator.
//
// Nothing is recovered by resynchronising: `pt_insn_next' fails at the stray MTC, the recovery
// then cannot sync at that PSB either (its PSB+ is still unterminated) and skips to the NEXT PSB,
// which drops every instruction in between AND the whole machine state.  Instead close the
// header: write PSBEND over two PAD bytes of the private (copy-on-write) mapping, before the
// first packet that does not belong inside a header.  Everything after it -- the resumed TSC, TMA
// and CBR -- is a legal standalone packet, the time stays exact, and a PSB+ carries no flow
// information, so the decode continues with no loss at all.  Only PAD bytes are ever
// overwritten; a header with no PAD in it is left alone.
static uint64_t repair_psb_headers(uint8_t* p, size_t n) {
    if (!p || n < 18) return 0;
    static const uint8_t PSB[16] = {0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82};
    static const uint8_t PSBEND[2] = {0x02, 0x23};
    static const uint8_t IPBYTES[8] = {1, 3, 5, 7, 7, 1, 9, 1};   // 1 + payload, by IPBytes
    uint64_t fixed = 0;
    for (size_t i = 0; i + 16 <= n; ) {
        const void* h = memmem(p + i, n - i, PSB, 16);
        if (!h) break;
        const size_t psb = (const uint8_t*)h - p;
        i = psb + 16;
        // Terminated?  A well-formed header here is 36-70 bytes long, so a PSBEND within 128
        // bytes means there is nothing to fix.
        // The search is by bytes, not by packets: a false positive (0x02 0x23 inside a payload)
        // only means we leave the header alone, which is the safe direction.
        const size_t win = std::min((size_t)128, n - i);
        if (win >= 2 && memmem(p + i, win, PSBEND, 2)) continue;
        // Truncated.  Walk the header's packets as far as the OUTPUT REGION boundary that cut it
        // (the 4 KiB page the PSB started in) and put the PSBEND in the last PAD pair before it,
        // i.e. after the TSC and TMA the hardware did emit, so the header keeps its time.
        const size_t lim = std::min(n, std::min(i + 128, (psb | 0xfff) + 1));
        size_t k = i, pad2 = 0;
        while (k < lim) {
            uint8_t b = p[k];
            if (b == 0x00) { if (k + 1 < lim && p[k + 1] == 0x00) pad2 = k; k++; continue; }
            if (b == 0x02 && k + 1 < lim) {
                uint8_t c = p[k + 1];
                if (c == 0x73) { k += 7; continue; }                      // TMA
                if (c == 0x03) { k += 4; continue; }                      // CBR
                if (c == 0x43) { k += 8; continue; }                      // PIP
                if (c == 0xC8) { k += 7; continue; }                      // VMCS
                break;                                                     // not sizable here
            }
            if (b == 0x19) { k += 8; continue; }                          // TSC
            if (b == 0x59) { k += 2; continue; }                          // MTC
            if (b == 0x99) { k += 2; continue; }                          // MODE
            if ((b & 0x1f) == 0x1d) { k += IPBYTES[(b >> 5) & 7]; continue; }   // FUP
            break;                                                         // TNT/TIP/CYC: stop
        }
        if (!pad2) continue;                       // nowhere safe to write: leave it alone
        p[pad2] = 0x02; p[pad2 + 1] = 0x23;
        fixed++;
    }
    return fixed;
}

PtDecoder::PtDecoder(const std::string& auxfile, const Sideband& sb, uint64_t skip, uint64_t end) : maps_(sb.maps) {
    memset(&cfg_, 0, sizeof cfg_); cfg_.size = sizeof cfg_;
    int fd = open(auxfile.c_str(), O_RDONLY); if (fd < 0) throw std::runtime_error("cannot open aux " + auxfile);
    struct stat st; if (fstat(fd, &st)) { close(fd); throw std::runtime_error("cannot stat aux " + auxfile); }
    size_t n = (size_t)st.st_size;
    if (n) {
        void* m = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (m == MAP_FAILED) { close(fd); throw std::runtime_error("cannot mmap aux " + auxfile); }
        aux_map_ = m; aux_map_len_ = n;
        n_psb_repaired += repair_psb_headers((uint8_t*)m, n);   // copy-on-write: the file is untouched
    }
    close(fd);
    if (!end || end > n) end = n;
    if (skip > end) skip = end;
    aux_ = (const uint8_t*)aux_map_ + skip; aux_len_ = (size_t)(end - skip); skip_ = skip;
    cfg_.begin = (uint8_t*)aux_; cfg_.end = (uint8_t*)aux_ + aux_len_;
    cfg_.cpu.vendor = pcv_intel; cfg_.cpu.family = sb.family; cfg_.cpu.model = sb.model; cfg_.cpu.stepping = sb.stepping;
    pt_cpu_errata(&cfg_.errata, &cfg_.cpu);
    cfg_.mtc_freq = sb.mtc_period; cfg_.cpuid_0x15_eax = sb.cpuid15_eax; cfg_.cpuid_0x15_ebx = sb.cpuid15_ebx; cfg_.nom_freq = 0;
    image_ = pt_image_alloc("ptrecon");
    iscache_ = pt_iscache_alloc("ptrecon");
    // ---- The PRE-INIT view of every E9Patch-rewritten image (e9phase.h) ---------------------
    // The sideband's map is a union over time; an image that was rewritten runs its ORIGINAL
    // bytes until its own loader maps the patched copy over them (libc's IFUNC resolvers run
    // during `ld.so's relocation processing, before every DT_INIT).  A decoder that covers that
    // window therefore starts with the original executable PT_LOADs -- read from the same
    // rewritten file, which still carries them at their original offsets -- and swaps in the
    // patched overlay when the image's loader segment executes (step()).
    e9_.build(maps_);
    // Only a decoder that starts at byte 0 covers the process's start-up; a chunk decoder starts
    // after every DT_INIT has run and must use the patched bytes throughout.
    preinit_ = (skip == 0);
    if (e9_.empty()) preinit_ = false;
    if (preinit_) e9_pending_.resize(e9_.size());
    size_t n_exec = 0, n_added = 0, n_uncached = 0, n_failed = 0, n_withheld = 0;
    for (auto& m : maps_) if (m.exec()) n_exec++;
    // The original sections go in FIRST: nothing else overlaps them while the overlay is withheld.
    if (preinit_) {
        size_t n_orig = 0;
        for (size_t i = 0; i < e9_.size(); i++)
            for (auto& sec : e9_[i].orig) {
                int e = add_section(e9_[i].path.c_str(), sec.off, sec.len, sec.vaddr);
                if (e < 0) fprintf(stderr, "warning: e9phase: original section (%s @%#lx) not added: %s\n",
                                   e9_[i].path.c_str(), (unsigned long)sec.vaddr, pt_errstr(pt_errcode(e)));
                else n_orig++;
            }
        e9_unswapped_ = e9_.size();
        fprintf(stderr, "e9phase: %zu rewritten image(s), %zu original section(s) -- decoding the "
                        "pre-init window with the UNPATCHED bytes\n", e9_.size(), n_orig);
    }
    for (auto& m : maps_) {
        if (!m.exec()) continue;
        struct stat st; if (stat(m.path.c_str(), &st)) { fprintf(stderr, "warning: cannot stat %s (skipped from image)\n", m.path.c_str()); continue; }
        uint64_t len = m.end - m.start; if (m.off >= (uint64_t)st.st_size) continue; if (m.off + len > (uint64_t)st.st_size) len = st.st_size - m.off;
        // libipt's image-section CACHE identifies a section by a 16-bit `isid', so it holds at
        // most 65 535 sections and then refuses every further one with -pte_nomem.  An ordinary
        // E9Patch whole-program build has ~17 000 executable mappings and never notices; a
        // `--gt-all' build has ~78 000, so thousands of its trampoline pages would be MISSING FROM
        // THE IMAGE -- the decoder then cannot read the code it is following and resynchronises
        // instead.  Falling back to the uncached `pt_image_add_file' costs the section sharing
        // the cache provides and nothing else; there is no cap on the image's own section list.
        // The patched copy of a rewritten image is withheld until that image's loader runs.
        if (preinit_) {
            int ii = e9_.patched_overlay_image(m);
            if (ii >= 0) { e9_pending_[(size_t)ii].push_back(E9Sec{m.off, len, m.start}); n_withheld++; continue; }
        }
        size_t before_uncached = n_uncached_adds_;
        int e = add_section(m.path.c_str(), m.off, len, m.start);
        n_uncached += n_uncached_adds_ - before_uncached;
        if (e < 0) { n_failed++;
            if (n_failed <= 10) fprintf(stderr, "warning: image add (%s @%#lx): %s\n", m.path.c_str(), (unsigned long)m.start, pt_errstr(pt_errcode(e))); }
        else n_added++;
    }
    if (n_failed) fprintf(stderr, "warning: %zu of %zu executable mappings could NOT be added to "
                                  "the decoder image -- the trace will not decode there\n",
                          n_failed, n_exec);
    if (n_uncached) fprintf(stderr, "image: %zu of %zu executable mappings added, %zu of them "
                                    "uncached\n", n_added, n_exec, n_uncached);
    if (n_withheld) fprintf(stderr, "e9phase: %zu patched overlay mapping(s) withheld until their "
                                    "image's loader executes\n", n_withheld);
    alloc_decoder();
}
// One section of a file into the image, through the section cache while it still has room.
// (The cache identifies a section by a 16-bit isid, so it holds at most 65 535 of them; an
// uncached section costs the sharing the cache provides and nothing else.)
int PtDecoder::add_section(const char* path, uint64_t off, uint64_t len, uint64_t vaddr) {
    int isid = uncached_ ? -pte_nomem : pt_iscache_add_file(iscache_, path, off, len, vaddr);
    int e = isid < 0 ? isid : pt_image_add_cached(image_, iscache_, isid, nullptr);
    if (e == -pte_nomem) {
        if (!uncached_) {
            uncached_ = true;
            fprintf(stderr, "note: the libipt section cache is full (65 535 sections); the "
                            "remaining executable mappings are added uncached\n");
        }
        e = pt_image_add_file(image_, path, off, len, nullptr, vaddr);
        if (e >= 0) n_uncached_adds_++;
    }
    return e;
}
// This image's loader has run: put its patched copy into the image, on top of the original
// section (libipt shrinks or splits the sections an added one overlaps, so this is append-only
// and there is nothing to remove).  Monotone: an image never goes back to its original bytes.
void PtDecoder::e9_swap(size_t i, uint64_t ip, bool forced) {
    if (i >= e9_.size() || e9_[i].swapped) return;
    e9_[i].swapped = true; e9_[i].swap_ip = ip;
    uint64_t t = 0; uint32_t lm, lc; if (dec_ && pt_insn_time(dec_, &t, &lm, &lc) < 0) t = 0;
    e9_[i].swap_tsc = t;
    for (auto& sec : e9_pending_[i]) {
        int e = add_section(e9_[i].path.c_str(), sec.off, sec.len, sec.vaddr);
        if (e < 0) fprintf(stderr, "warning: e9phase: patched section (%s @%#lx) not added: %s\n",
                           e9_[i].path.c_str(), (unsigned long)sec.vaddr, pt_errstr(pt_errcode(e)));
    }
    n_e9_swap++; if (!forced) n_e9_swap_trig++;
    if (e9_unswapped_) e9_unswapped_--;
    fprintf(stderr, "e9phase: %s -> patched at ip %#lx (tsc %lu, after %lu instructions)\n",
            e9_[i].path.c_str(), (unsigned long)ip, (unsigned long)t, (unsigned long)n_e9_preinit_insn);
}
// SAFETY NET.  The pre-init phase is only correct for a decoder that really does cover the
// process's start-up; if one that does not is put in it, its very first instructions are read
// from the wrong (unpatched) bytes and the decoder loses sync at once.  Any sync loss while an
// image is still unpatched therefore ends the phase: from here on the decoder behaves exactly as
// it would without the pre-init view.
void PtDecoder::e9_abort(uint64_t off) {
    if (!preinit_ || !e9_unswapped_) return;
    e9_aborted = true; e9_abort_off = off;
    fprintf(stderr, "e9phase: sync lost at offset %lu with %zu image(s) still unpatched -- "
                    "ending the pre-init phase\n", (unsigned long)off, e9_unswapped_);
    for (size_t i = 0; i < e9_.size(); i++) if (!e9_[i].swapped) e9_swap(i, 0, true);
    e9_unswapped_ = 0;
}
std::string PtDecoder::e9_json() const {
    std::string s = "{\"preinit\":" + std::string(preinit_ ? "true" : "false") +
                    ",\"swaps\":" + std::to_string(n_e9_swap) +
                    ",\"swaps_triggered\":" + std::to_string(n_e9_swap_trig) +
                    ",\"preinit_insn\":" + std::to_string(n_e9_preinit_insn) +
                    ",\"aborted\":" + std::string(e9_aborted ? "true" : "false") +
                    ",\"abort_off\":" + std::to_string(e9_abort_off) + ",\"images\":[";
    for (size_t i = 0; i < e9_.size(); i++) {
        if (i) s += ",";
        s += "{\"path\":\"" + e9_[i].path + "\",\"swapped\":" + (e9_[i].swapped ? "true" : "false") +
             ",\"swap_ip\":" + std::to_string(e9_[i].swap_ip) +
             ",\"swap_tsc\":" + std::to_string(e9_[i].swap_tsc) + "}";
    }
    return s + "]}";
}
void PtDecoder::alloc_decoder() {
    if (dec_) { pt_insn_free_decoder(dec_); dec_ = nullptr; }
    dec_ = pt_insn_alloc_decoder(&cfg_); if (!dec_) throw std::runtime_error("pt_insn_alloc_decoder failed");
    pt_insn_set_image(dec_, image_);
    synced_ = false; eos_ = false; status_ = 0; sync_fail_run_ = 0;
}
PtDecoder::~PtDecoder() { if (dec_) pt_insn_free_decoder(dec_); if (image_) pt_image_free(image_); if (iscache_) pt_iscache_free(iscache_);
    if (aux_map_) munmap(aux_map_, aux_map_len_); }

bool PtDecoder::drain_events(PtEvents& ev) {
    while (status_ & pts_event_pending) {
        struct pt_event e; status_ = pt_insn_event(dec_, &e, sizeof e);
        if (status_ < 0) return false;
        switch (e.type) {
            case ptev_ptwrite: n_ptw++; if (ev.on_ptwrite) ev.on_ptwrite(e.variant.ptwrite.payload, e.variant.ptwrite.size, e.ip_suppressed ? 0 : e.variant.ptwrite.ip); break;
            case ptev_overflow: n_ovf++; if (ev.on_overflow) ev.on_overflow(e.has_tsc ? e.tsc : 0); break;
            case ptev_enabled: if (ev.on_enable) ev.on_enable(e.variant.enabled.ip, true); break;
            case ptev_disabled: case ptev_async_disabled: if (ev.on_enable) ev.on_enable(0, false); break;
            default: break;
        }
    }
    return true;
}

// First PSB strictly after `off' in the decoder's own buffer (both offsets relative to
// cfg_.begin).  Scanned by pattern, exactly like PtDecoder::psb_offsets().
uint64_t PtDecoder::next_psb_after(uint64_t off) const {
    static const uint8_t PSB[16] = {0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82};
    if (!aux_ || aux_len_ < 16) return ~0ull;
    uint64_t from = off + 1;
    if (from + 16 > aux_len_) return ~0ull;
    const void* h = memmem(aux_ + from, (size_t)(aux_len_ - from), PSB, 16);
    if (!h) return ~0ull;
    return (uint64_t)((const uint8_t*)h - aux_);
}

bool PtDecoder::next(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev) {
    bool ok = step(insn, tsc, ev);
    if (ok) n_insn++;
    return ok;
}
bool PtDecoder::step(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev) {
    for (;;) {
        if (eos_) return false;
        if (!synced_) {
            status_ = pt_insn_sync_forward(dec_);
            if (status_ < 0) {
                // Not every error ends the trace: a `pt_insn_sync_forward' that lands on a PSB it
                // cannot use returns e.g. -pte_bad_opc or -pte_bad_packet, and giving up there
                // would throw away the rest of a COMPLETE capture.  Only -pte_eos is really the
                // end; for anything else look for the next PSB, at most `kMaxResync' times in a
                // row.
                if (status_ == -pte_eos) { eos_ = true; eos_why = "sync_forward: end of stream"; eos_off = offset(); eos_status = status_; return false; }
                uint64_t off = 0;
                if (pt_insn_get_offset(dec_, &off) < 0) { eos_ = true; eos_why = "sync_forward: no offset"; eos_status = status_; return false; }
                n_resync++; n_sync_fail++;
                if (ev.on_resync) ev.on_resync(status_, off);
                if (preinit_ && e9_unswapped_) e9_abort(skip_ + off);
                if (++sync_fail_run_ > kMaxResync) {
                    eos_ = true; eos_why = "too many consecutive sync failures";
                    eos_off = skip_ + off; eos_status = status_; return false;
                }
                // `pt_insn_sync_set(off + 1)' is NOT "look for the next PSB": libipt's sync_set
                // requires a PSB AT the offset it is given (pt_sync_set -> -pte_nosync otherwise),
                // so one byte past the offending PSB it fails essentially always.  Step to the
                // next REAL PSB instead (found by pattern, as the chunk splitter does), at most
                // kMaxResync of them.
                bool resynced = false;
                for (uint64_t nxt = next_psb_after(off); nxt != ~0ull; nxt = next_psb_after(nxt)) {
                    if (pt_insn_sync_set(dec_, nxt) >= 0) { resynced = true; break; }
                    n_sync_fail++;
                    if (++sync_fail_run_ > kMaxResync) break;
                }
                if (!resynced) {
                    eos_ = true; eos_why = sync_fail_run_ > kMaxResync ? "too many consecutive sync failures"
                                                                      : "no usable PSB after the failing one";
                    eos_off = skip_ + off; eos_status = status_; return false;
                }
                continue;
            }
            synced_ = true; sync_fail_run_ = 0;
        }
        if (!drain_events(ev)) { synced_ = false; n_resync++; if (ev.on_resync) ev.on_resync(status_, 0);
                                 if (preinit_ && e9_unswapped_) e9_abort(offset()); continue; }
        if (status_ & pts_eos) { eos_ = true; eos_why = "pts_eos after draining events"; eos_off = offset(); eos_status = status_; return false; }
        status_ = pt_insn_next(dec_, &insn, sizeof insn);
        if (status_ < 0) {
            if (status_ == -pte_eos) { eos_ = true; eos_why = "pt_insn_next: end of stream"; eos_off = offset(); eos_status = status_; return false; }
            uint64_t off = 0; pt_insn_get_offset(dec_, &off);
            n_resync++; if (ev.on_resync) ev.on_resync(status_, off);
            if (preinit_ && e9_unswapped_) e9_abort(skip_ + off);
            synced_ = false; continue;
        }
        // This image's E9Patch loader is executing -- from its next instruction on, the
        // patched copy is mapped over the original text, so put it into the decoder's image.
        if (preinit_ && e9_unswapped_) {
            n_e9_preinit_insn++;
            int ii = e9_.loader_image(insn.ip);
            if (ii >= 0 && !e9_[(size_t)ii].swapped) e9_swap((size_t)ii, insn.ip);
        }
        if (tsc && !no_time) { uint32_t lm, lc; if (pt_insn_time(dec_, tsc, &lm, &lc) < 0) *tsc = 0; }
        else if (tsc) *tsc = 0;
        return true;
    }
}
