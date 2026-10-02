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
#include <map>
#include <set>
#include <Zydis/Zydis.h>

Sideband Sideband::load(const std::string& path) {
    Json j = json_load(path); Sideband s;
    s.pid = (int)j["pid"].i64(); s.exit_code = (int)j["exit"].i64(); s.aux_bytes = j["aux_bytes"].u64(); s.wrapped = j["wrapped"].i64() != 0;
    s.mtc_period = (int)j["mtc_period"].i64(3);
    s.family = (int)j["cpu"]["family"].i64(6); s.model = (int)j["cpu"]["model"].i64(); s.stepping = (int)j["cpu"]["stepping"].i64();
    s.cpuid15_eax = (uint32_t)j["cpuid15"]["eax"].u64(); s.cpuid15_ebx = (uint32_t)j["cpuid15"]["ebx"].u64();
    s.time_mult = (uint32_t)j["time"]["mult"].u64(); s.time_shift = (uint32_t)j["time"]["shift"].u64(); s.time_zero = j["time"]["zero"].u64();
    s.fs_base = j["fs_base"].u64();
    for (auto& m : j["maps"].arr) { MapEnt e; e.start = m["start"].u64(); e.end = m["end"].u64(); e.off = m["offset"].u64(); e.perms = m["perms"].str(); e.path = m["path"].str(); s.maps.push_back(e); }
    // sideband version 2 (multi-threaded capture): additive keys, absent from older files.
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

// ---- a PSB+ header the hardware never finished -----------------------------------------------
//
// A PSB packet opens a PSB+ HEADER that must be closed by PSBEND; only PAD, TSC, TMA, CBR, MODE,
// PIP, VMCS and FUP may appear inside it.  On some processors a PSB that the periodic PSB
// counter emits while the trace is being stopped and restarted at an output-region boundary is
// left OPEN: the header carries its TSC and TMA, the trace then stops (here for 66 us of wall
// clock), and when it resumes the hardware emits a fresh TSC/TMA/CBR -- but no second PSB and no
// PSBEND -- so the next MTC arrives INSIDE the header and libipt fails the whole stream with
// `unexpected packet context'.  Measured on a set of per-CPU captures:
// 7 such headers in 8 AUX files (10 MB), EVERY ONE of them at file offset 0xfd0 of a 4 KiB page
// (the output-region boundary), with 0 OVF packets, 0 lost AUX bytes and 0 truncated regions --
// so nothing is missing from the byte stream, only the header's terminator.
//
// Nothing is recovered by resynchronising: `pt_insn_next' fails at the stray MTC, the recovery
// then cannot sync at that PSB either (its PSB+ is still unterminated) and skips to the NEXT PSB,
// which drops every instruction in between AND the thread's whole machine state.  Instead close
// the header: write PSBEND over two PAD bytes of the private (copy-on-write) mapping, before the
// first packet that does not belong inside a header.  Everything after it -- the resumed TSC, TMA
// and CBR -- is a legal standalone packet, the time stays exact, and a PSB+ carries no flow
// information, so the decode continues with no loss at all.  PTRECON_NO_PSB_REPAIR=1 is the
// ablation.  Only PAD bytes are ever overwritten; a header with no PAD in it is left alone.
static uint64_t repair_psb_headers(uint8_t* p, size_t n) {
    static const bool off = getenv("PTRECON_NO_PSB_REPAIR") != nullptr;
    if (off || !p || n < 18) return 0;
    static const uint8_t PSB[16] = {0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82,0x02,0x82};
    static const uint8_t PSBEND[2] = {0x02, 0x23};
    static const uint8_t IPBYTES[8] = {1, 3, 5, 7, 7, 1, 9, 1};   // 1 + payload, by IPBytes
    uint64_t fixed = 0;
    for (size_t i = 0; i + 16 <= n; ) {
        const void* h = memmem(p + i, n - i, PSB, 16);
        if (!h) break;
        const size_t psb = (const uint8_t*)h - p;
        i = psb + 16;
        // Terminated?  A well-formed header here is 36-70 bytes long (measured over 23 376 PSBs of
        // per-CPU captures), so a PSBEND within 128 bytes means there is nothing to fix.
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

// libipt consults this for exactly the addresses that are in no file section -- which, when a
// JIT code table is loaded, is every mapping the table describes (they are deliberately not added
// below).  The time key is the decoder's own TSC/MTC estimate; see jitcode.h.
static int jit_read_cb(uint8_t* buffer, size_t size, const struct pt_asid* asid, uint64_t ip, void* ctx) {
    (void)asid;
    PtDecoder* d = (PtDecoder*)ctx;
    if (!d->jit) return -pte_nomap;
    if (!d->jit->describes(ip)) return -pte_nomap;     // 6l: cheap reject (callback-first libipt)
#ifdef PTRECON_LIBIPT_S6L
    // 6l: the patched libipt's pt_image_find asks the callback a 1-byte "is this yours?" probe
    // before its section walk -- answer it without a (counted) fetch.  Only with that libipt: the
    // stock one never probes, and a genuine 1-byte read must be served for real.
    if (size == 1 && buffer) { buffer[0] = 0; return 1; }
#endif
    int n = d->jit->read(buffer, size, ip, d->cur_tsc);
    return n > 0 ? n : -pte_nomap;
}

PtDecoder::PtDecoder(const std::string& auxfile, const Sideband& sb, uint64_t skip, uint64_t end, JitTable* jitp, const ThreadPlan* plan, int preinit) : jit(jitp), plan_(plan), maps_(sb.maps) {
    if (const char* text = getenv("PTRECON_STALL_INSNS")) {
        char* endptr=nullptr; errno=0;
        const uint64_t limit=strtoull(text,&endptr,10);
        if (!*text || *endptr || errno ||
            !std::all_of(text, static_cast<const char*>(endptr), [](char c){return c>='0' && c<='9';}))
            throw std::runtime_error("invalid PTRECON_STALL_INSNS");
        progress_.limit=limit;
    }
    memset(&cfg_, 0, sizeof cfg_); cfg_.size = sizeof cfg_;
    if (plan_) {
        // Plan mode: the files are opened on demand (open_file), one at a time; the libipt image
        // below is shared by every per-file decoder.  `skip'/`end' do not apply.
        // An own copy, indexed, and clipped to the chunk's virtual range [skip, end)
        if (plan_->vbase.size() != plan_->segs.size()) { own_plan_ = *plan_; own_plan_.index(); }
        else own_plan_ = *plan_;
        if (skip || end) own_plan_ = own_plan_.slice(skip, end);
        plan_ = &own_plan_;
        pfiles_.resize(plan_->files.size());
        skip_ = 0;
    } else {
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
    }
    cfg_.cpu.vendor = pcv_intel; cfg_.cpu.family = sb.family; cfg_.cpu.model = sb.model; cfg_.cpu.stepping = sb.stepping;
    pt_cpu_errata(&cfg_.errata, &cfg_.cpu);
    // libipt 2.0.6 knows no errata for family 6 model 198, so pt_cpu_errata() leaves
    // the mask empty.  PTRECON_ERRATA=all turns on the ones that describe a trace whose TraceEn is
    // cleared and restored under it -- which is exactly what perf does at every AUX watermark
    // wakeup.
    if (const char* e = getenv("PTRECON_ERRATA")) {
        if (!strcmp(e, "all")) { cfg_.errata.bdm70 = cfg_.errata.bdm64 = cfg_.errata.skd007 =
            cfg_.errata.skd022 = cfg_.errata.skd010 = cfg_.errata.skl014 = cfg_.errata.apl12 =
            cfg_.errata.apl11 = cfg_.errata.skl168 = 1; }
        else if (!strcmp(e, "none")) memset(&cfg_.errata, 0, sizeof cfg_.errata);
        else if (!strcmp(e, "apl")) { cfg_.errata.apl11 = cfg_.errata.apl12 = 1; }
    }
    cfg_.mtc_freq = sb.mtc_period; cfg_.cpuid_0x15_eax = sb.cpuid15_eax; cfg_.cpuid_0x15_ebx = sb.cpuid15_ebx; cfg_.nom_freq = 0;
    // ---- the PRE-INIT view of every E9Patch-rewritten image (e9phase.h) ---------------------
    // The sideband's map is a union over time; an image that was rewritten runs its ORIGINAL
    // bytes until its own loader maps the patched copy over them (libc's IFUNC resolvers run
    // during `ld.so's relocation processing, before every DT_INIT).  A decoder that covers that
    // window therefore starts with the original executable PT_LOADs -- read from the same
    // rewritten file, which still carries them at their original offsets -- and swaps in the
    // patched overlay when the image's loader segment executes (step()).
    e9_.build(maps_);
    preinit_ = preinit >= 0 ? (preinit != 0)
                            : (plan ? (uint64_t)plan->tid == (uint64_t)sb.pid : skip == 0);
    // PTRECON_NO_PREINIT=1 is the ABLATION: build the image as
    // the union over time with the patched bytes everywhere.
    if (getenv("PTRECON_NO_PREINIT")) preinit_ = false;
    if (e9_.empty()) preinit_ = false;
    if (preinit_) e9_pending_.resize(e9_.size());
    // ONE image per process for every post-init decoder: building it adds every
    // executable mapping to libipt's section cache, ~1.3 s on whole-program CPython (20 000
    // mappings, pt_iscache_add_file is a linear search per add) -- and every --jobs chunk child,
    // boundary probe and delta-scan child would pay that again.  The parent builds it once
    // (prebuild_shared) before forking; a decoder that would build exactly the same image (no
    // pre-init view, no JIT callback, no plan, the same sideband) uses it read-only.
    const bool share = !plan_ && !jit && !preinit_ && s_sb_ == &sb;
    if (share && s_img_) { image_ = s_img_; iscache_ = s_isc_; shared_img_ = true; alloc_decoder(); return; }
    image_ = pt_image_alloc("ptrecon");
    iscache_ = pt_iscache_alloc("ptrecon");
    size_t n_jit_maps = 0;
    size_t n_exec = 0, n_added = 0, n_uncached = 0, n_failed = 0, n_withheld = 0;
    for (auto& m : maps_) if (m.exec()) n_exec++;
    // The trampoline pages of the rewritten images go in as a FEW merged sections, before
    // everything else (see merge_trampolines()); the mappings they cover are skipped below.
    std::vector<char> merged(maps_.size(), 0);
    const size_t n_merged = merge_trampolines(merged, n_exec);
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
    for (size_t mi = 0; mi < maps_.size(); mi++) {
        auto& m = maps_[mi];
        if (!m.exec() || merged[mi]) continue;
        struct stat st; if (stat(m.path.c_str(), &st)) { fprintf(stderr, "warning: cannot stat %s (skipped from image)\n", m.path.c_str()); continue; }
        uint64_t len = m.end - m.start; if (m.off >= (uint64_t)st.st_size) continue; if (m.off + len > (uint64_t)st.st_size) len = st.st_size - m.off;
        // A mapping the JIT code table describes is served by the callback instead: pt_capture2's
        // dump of it is one snapshot of a range whose contents change all run (a JIT code space,
        // and a trampoline slab that is filled long after its pages first appear).  The snapshot
        // is kept as the table's background layer so it can still answer where the table cannot.
        if (jit && jit->overlaps(m.start, m.end)) {
            n_jit_maps++;
            int bfd = open(m.path.c_str(), O_RDONLY);
            if (bfd >= 0) {
                std::vector<uint8_t> b((size_t)len);
                ssize_t got = pread(bfd, b.data(), (size_t)len, (off_t)m.off);
                close(bfd);
                if (got > 0) { b.resize((size_t)got); bgfiles_.push_back(std::move(b));
                               jit->add_background(m.start, m.start + (uint64_t)got, bgfiles_.back().data(), bgfiles_.back().size()); }
            }
            // A handful of explicitly recorded native hook pages must not turn
            // all of libjvm into slow callback reads. JIT mappings still return
            // no immutable ranges: their early snapshots are never trusted here.
            for (const auto& r : jit->immutable_ranges(m.start, m.start + len)) {
                const int e = add_section(m.path.c_str(), m.off + r.lo - m.start, r.hi - r.lo, r.lo);
                if (e < 0) throw std::runtime_error("cannot add immutable native image section: " + m.path);
            }
            continue;
        }
        // libipt's image-section CACHE identifies a section by a 16-bit `isid', so it holds at
        // most 65 535 sections and then refuses every further one with -pte_nomem.  An ordinary
        // E9Patch whole-program build has ~17 000 executable mappings and never notices; a
        // `--gt-all' build has 78 000, so 12 571 of its trampoline
        // pages would be MISSING FROM THE IMAGE -- the decoder then cannot read the
        // code it is following and resynchronises instead (measured: 406 154 resyncs, 3.2 M of
        // 134 M records decoded, 70 % of them with an unknown address).  Falling back to the
        // uncached `pt_image_add_file' costs the section sharing the cache provides and nothing
        // else; there is no cap on the image's own section list.
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
    n_added += n_merged;
    if (n_failed) fprintf(stderr, "warning: %zu of %zu executable mappings could NOT be added to "
                                  "the decoder image -- the trace will not decode there\n",
                          n_failed, n_exec);
    if (n_uncached) fprintf(stderr, "image: %zu of %zu executable mappings added, %zu of them "
                                    "uncached\n", n_added, n_exec, n_uncached);
    if (n_withheld) fprintf(stderr, "e9phase: %zu patched overlay mapping(s) withheld until their "
                                    "image's loader executes\n", n_withheld);
    if (jit) {
        pt_image_set_callback(image_, jit_read_cb, this);
        fprintf(stderr, "jitdump: %zu executable mappings served by the time-keyed code table\n", n_jit_maps);
    }
    if (share && s_publish_) { s_img_ = image_; s_isc_ = iscache_; shared_img_ = true; s_uncached_ = uncached_; }
    if (!plan_) alloc_decoder();
}
struct pt_image* PtDecoder::s_img_ = nullptr;
struct pt_image_section_cache* PtDecoder::s_isc_ = nullptr;
const Sideband* PtDecoder::s_sb_ = nullptr;
bool PtDecoder::s_publish_ = false;
bool PtDecoder::s_uncached_ = false;
void PtDecoder::prebuild_shared(const std::string& auxfile, const Sideband& sb) {
    if (s_img_) return;
    s_sb_ = &sb; s_publish_ = true;
    try { new PtDecoder(auxfile, sb, 1, 0, nullptr, nullptr, 0); }   // kept for the process lifetime
    catch (std::exception& e) { fprintf(stderr, "ptrecon: shared decoder image not built (%s)\n", e.what()); s_sb_ = nullptr; }
    s_publish_ = false;
}
// ---- merge the E9Patch TRAMPOLINE pages into a few sections ------------------------------
// A whole-program JVM build maps its trampolines page by page at scattered file offsets: one
// such capture has 97 431 executable mappings (libjvm.so alone 77 845).  libipt's section cache
// holds 65 535 (16-bit isid); the other ~32 000 would go in uncached, and every uncached lookup
// is a linear walk of the image's section list plus a map per read -- the decoder essentially
// stalls.  File offsets are not contiguous, so the mappings cannot
// be merged as file ranges.  Instead their BYTES are copied into one sparse memfd, laid out so that
// each group of trampoline mappings that are neighbours in the address space becomes ONE file range
// at one vaddr, and each group is added as one cached section.
//   * Candidates: executable mappings of an E9Patch-rewritten image (e9phase) that are neither the
//     image's original PT_LOADs, its patched overlay nor its loader segment, lie wholly inside the
//     file, and are not described by the JIT code table.
//   * A group breaks at any non-candidate executable mapping, at any JIT-table range, and at a gap
//     over 16 MB, so the zero-filled gap inside a group covers only memory that was never
//     executable.  Executable sideband mappings never overlap here (checked: none do on the capture
//     above), and the groups are added FIRST, so any later section still takes precedence.
//   * Default: on when the image has more executable mappings than the cache holds (the only case
//     that changed behaviour), PTRECON_TRAMP_MERGE=1 forces it on, =0 off.  The bytes decoded are
//     identical either way; offline/tests/run_t4_mt.sh checks the trace is byte-identical.
size_t PtDecoder::merge_trampolines(std::vector<char>& merged, size_t n_exec) {
    const char* env = getenv("PTRECON_TRAMP_MERGE");
    const int mode = env ? atoi(env) : -1;
    if (mode == 0 || e9_.empty()) return 0;
    // Default: on when the cache would overflow, and also
    // for every decoder with no JIT table and no thread plan (the block-mode decoders): with ~20 000
    // separate trampoline sections libipt's move-to-front section list walk (pt_image_find) and its
    // iscache LRU walk (pt_iscache_notify_map) were ~16 % of a whole-program CPython decode, and the
    // merged image decodes the identical instruction stream (PTRECON_DECHASH checked, py_nbody).
    // The merged sections are huge (a sparse span), and a STOCK libipt memsets a block-cache of
    // 4 bytes per section byte when the block decoder first maps one: only do this by default with
    // a patched libipt (its version extension carries "bcalloc").
    static const bool bcalloc = [] { struct pt_version v = pt_library_version(); return v.ext && strstr(v.ext, "bcalloc"); }();
    if (mode < 0 && n_exec <= 65535 && (jit || plan_ || !bcalloc)) return 0;
    auto ovl = [](uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a0 < b1 && b0 < a1; };
    std::map<std::string, std::vector<size_t>> img_of;          // path -> e9 image indices
    for (size_t i = 0; i < e9_.size(); i++) img_of[e9_[i].path].push_back(i);
    std::map<std::string, uint64_t> fsize;
    auto file_size = [&](const std::string& p) -> uint64_t {
        auto it = fsize.find(p); if (it != fsize.end()) return it->second;
        struct stat st; uint64_t z = stat(p.c_str(), &st) ? 0 : (uint64_t)st.st_size;
        return fsize[p] = z; };
    auto candidate = [&](const MapEnt& m) -> bool {
        auto it = img_of.find(m.path);
        if (it == img_of.end() || m.end <= m.start) return false;
        if (e9_.patched_overlay_image(m) >= 0) return false;
        if (jit && jit->overlaps(m.start, m.end)) return false;
        if (m.off + (m.end - m.start) > file_size(m.path)) return false;
        for (size_t i : it->second) {
            const E9ImageInfo& im = e9_[i];
            for (auto& s : im.orig)    if (ovl(m.start, m.end, s.vaddr & ~0xfffULL, (s.vaddr + s.len + 0xfff) & ~0xfffULL)) return false;
            for (auto& s : im.patched) if (ovl(m.start, m.end, s.vaddr, s.vaddr + s.len)) return false;
            for (auto& r : im.loader)  if (ovl(m.start, m.end, r.lo, r.hi)) return false;
        }
        return true; };
    std::vector<size_t> ex;
    for (size_t i = 0; i < maps_.size(); i++) if (maps_[i].exec()) ex.push_back(i);
    std::sort(ex.begin(), ex.end(), [&](size_t a, size_t b) { return maps_[a].start < maps_[b].start; });
    struct Group { uint64_t lo, hi; std::vector<size_t> mem; };
    std::vector<Group> groups; bool open_g = false;
    const uint64_t kMaxGap = 16ULL << 20;
    for (size_t i : ex) {
        const MapEnt& m = maps_[i];
        if (!candidate(m)) { open_g = false; continue; }
        if (open_g) {
            Group& g = groups.back();
            if (m.start >= g.hi && m.start - g.hi <= kMaxGap && !(jit && m.start > g.hi && jit->overlaps(g.hi, m.start))) {
                g.hi = m.end; g.mem.push_back(i); continue; }
        }
        groups.push_back(Group{m.start, m.end, {i}}); open_g = true;
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const Group& g) { return g.mem.size() < 2; }), groups.end());
    if (groups.empty()) return 0;
    // One sparse memfd per process for every decoder built from the same map (plan mode builds one
    // decoder per thread; --jobs children inherit it through fork()).
    static std::map<uint64_t, std::pair<std::string, std::vector<E9Sec>>> cache;
    uint64_t sig = 1469598103934665603ULL;
    for (auto& g : groups) for (size_t i : g.mem) {
        const MapEnt& m = maps_[i];
        for (uint64_t v : {m.start, m.end, m.off}) { sig ^= v; sig *= 1099511628211ULL; }
        for (char c : m.path) { sig ^= (uint8_t)c; sig *= 1099511628211ULL; } }
    auto hit = cache.find(sig);
    if (hit == cache.end()) {
        int fd = memfd_create("ptrecon-trampolines", 0);
        if (fd < 0) { fprintf(stderr, "warning: tramp-merge: memfd_create failed (%s); not merging\n", strerror(errno)); return 0; }
        std::vector<E9Sec> secs; uint64_t off = 0;
        for (auto& g : groups) { secs.push_back(E9Sec{off, g.hi - g.lo, g.lo}); off += (g.hi - g.lo + 0xfff) & ~0xfffULL; }
        if (ftruncate(fd, (off_t)off)) { fprintf(stderr, "warning: tramp-merge: ftruncate failed; not merging\n"); close(fd); return 0; }
        std::map<std::string, int> fds; std::vector<uint8_t> buf;
        for (size_t k = 0; k < groups.size(); k++) for (size_t i : groups[k].mem) {
            const MapEnt& m = maps_[i];
            int& sfd = fds[m.path]; if (!sfd) sfd = open(m.path.c_str(), O_RDONLY) + 1;
            buf.resize(m.end - m.start);
            if (sfd <= 0 || pread(sfd - 1, buf.data(), buf.size(), (off_t)m.off) != (ssize_t)buf.size() ||
                pwrite(fd, buf.data(), buf.size(), (off_t)(secs[k].off + (m.start - groups[k].lo))) != (ssize_t)buf.size())
                throw std::runtime_error("tramp-merge: cannot copy " + m.path);
        }
        for (auto& f : fds) if (f.second > 0) close(f.second - 1);
        hit = cache.emplace(sig, std::make_pair("/proc/self/fd/" + std::to_string(fd), std::move(secs))).first;
    }
    const std::string& path = hit->second.first;
    size_t n = 0, bytes = 0;
    for (auto& s : hit->second.second) {
        int e = add_section(path.c_str(), s.off, s.len, s.vaddr);
        if (e < 0) throw std::runtime_error(std::string("tramp-merge: cannot add merged section: ") + pt_errstr(pt_errcode(e)));
        bytes += s.len;
    }
    for (auto& g : groups) for (size_t i : g.mem) { merged[i] = 1; n++; }
    fprintf(stderr, "tramp-merge: %zu trampoline mappings merged into %zu section(s) (%.1f MB span, %zu executable mappings in all)\n",
            n, hit->second.second.size(), bytes / 1048576.0, n_exec);
    return n;
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
    uint64_t t = 0; if (insn_time(&t) < 0) t = 0;
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
// it did before this change.  `e9_preinit_useless()' tells the caller it was in the wrong phase
// from the start (nothing was swapped), so it can decode the stream again without it.
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
    if (bdec_) { pt_blk_free_decoder(bdec_); bdec_ = nullptr; }
    // Block mode is exact by construction only where every instruction byte comes from a cached
    // file section (so a block's `isid' names immutable bytes the walk cache can key on) and
    // nothing re-keys the image per instruction: no JIT callback (its time key is refreshed per
    // instruction), no plan (per-instruction segment membership), no uncached section (isid 0).
    {
        const char* want = getenv("PTRECON_DECODER");
        const bool uncached = shared_img_ ? s_uncached_ : uncached_;
        // A thread plan decodes in block mode too (next_plan's per-instruction segment test
        // sees the same offsets: a non-last instruction of a block reports the position before it,
        // as pt_insn does); PTRECON_PLAN_BLK=0 restores pt_insn for plans.
        const bool plan_blk = !(getenv("PTRECON_PLAN_BLK") && *getenv("PTRECON_PLAN_BLK") == '0');
        blk_ = !(want && !strcmp(want, "insn")) && (!plan_ || plan_blk) && !jit && !uncached;
        if (want && !strcmp(want, "blk") && !blk_)
            fprintf(stderr, "ptrecon: PTRECON_DECODER=blk not applicable here (JIT table, thread plan or uncached sections): using pt_insn\n");
        dec_hash_on = getenv("PTRECON_DECHASH") != nullptr;
    }
    if (blk_) {
        bdec_ = pt_blk_alloc_decoder(&cfg_); if (!bdec_) throw std::runtime_error("pt_blk_alloc_decoder failed");
        pt_blk_set_image(bdec_, image_);
        if (bfront_.empty()) bfront_.resize(1u << 18);
    } else {
        dec_ = pt_insn_alloc_decoder(&cfg_); if (!dec_) throw std::runtime_error("pt_insn_alloc_decoder failed");
        pt_insn_set_image(dec_, image_);
    }
    bn_ = bi_ = 0; bpend_err_ = 0;
    synced_ = false; eos_ = false; status_ = 0; sync_fail_run_ = 0;
}
uint64_t PtDecoder::live_offset() const {
    uint64_t o = 0;
    if (bdec_) { if (pt_blk_get_offset(bdec_, &o) < 0) o = 0; }
    else if (dec_) { if (pt_insn_get_offset(dec_, &o) < 0) o = 0; }
    return o;
}
int PtDecoder::live_time(uint64_t* t) const {
    uint32_t lm, lc;
    if (bdec_) return pt_blk_time(bdec_, t, &lm, &lc);
    if (dec_) return pt_insn_time(dec_, t, &lm, &lc);
    return -pte_invalid;
}
int PtDecoder::insn_time(uint64_t* t) const {
    if (blk_ && bi_ < bn_) { *t = btsc0_; return btsc0_ok_; }
    return live_time(t);
}
int PtDecoder::d_sync_forward() { return bdec_ ? pt_blk_sync_forward(bdec_) : pt_insn_sync_forward(dec_); }
int PtDecoder::d_sync_set(uint64_t off) { return bdec_ ? pt_blk_sync_set(bdec_, off) : pt_insn_sync_set(dec_, off); }
int PtDecoder::d_event(struct pt_event* e) { return bdec_ ? pt_blk_event(bdec_, e, sizeof *e) : pt_insn_event(dec_, e, sizeof *e); }
PtDecoder::~PtDecoder() {
    if (dec_hash_on) fprintf(stderr, "dechash: %s %016lx insn=%lu blocks=%lu walk_miss=%lu\n", decoder_kind(), (unsigned long)dec_hash,
                             (unsigned long)n_insn, (unsigned long)n_blocks, (unsigned long)n_walk_miss);
    if (bdec_) pt_blk_free_decoder(bdec_);
    if (dec_) pt_insn_free_decoder(dec_); if (image_ && !shared_img_) pt_image_free(image_); if (iscache_ && !shared_img_) pt_iscache_free(iscache_);
    if (aux_map_ && !plan_) munmap(aux_map_, aux_map_len_);
    for (auto& f : pfiles_) if (f.map) munmap(f.map, f.len); }

// ---- plan mode ---------------------------------------------------------------------------------
bool PtDecoder::open_file(int f) {
    if (f < 0 || (size_t)f >= pfiles_.size()) return false;
    PlanFile& pf = pfiles_[f];
    if (!pf.map) {
        const std::string& path = plan_->files[f];
        int fd = open(path.c_str(), O_RDONLY); if (fd < 0) { fprintf(stderr, "plan: cannot open %s\n", path.c_str()); return false; }
        struct stat st; if (fstat(fd, &st) || st.st_size == 0) { close(fd); return false; }
        void* m = mmap(nullptr, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0); close(fd);
        if (m == MAP_FAILED) return false;
        pf.map = m; pf.len = (size_t)st.st_size;
        n_psb_repaired += repair_psb_headers((uint8_t*)m, pf.len);
        pf.psb = psb_offsets(path);
    }
    aux_map_ = pf.map; aux_map_len_ = pf.len; aux_ = (const uint8_t*)pf.map; aux_len_ = pf.len; skip_ = 0;
    cfg_.begin = (uint8_t*)aux_; cfg_.end = (uint8_t*)aux_ + aux_len_;
    alloc_decoder();
    cur_file_ = f;
    return true;
}
// Position the decoder to decode segment `s': the PSB at or before s.begin in its file.  Anything
// decoded between that PSB and s.begin belongs to another thread and is skipped by next_plan().
bool PtDecoder::seek_seg(const AuxSeg& s) {
    if (cur_file_ != s.file && !open_file(s.file)) return false;
    const std::vector<uint64_t>& psb = pfiles_[s.file].psb;
    if (psb.empty()) return false;
    size_t lo = 0, hi = psb.size();
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (psb[mid] <= s.begin) lo = mid + 1; else hi = mid; }
    uint64_t at = lo ? psb[lo - 1] : psb[0];
    n_reseeks++;
    bn_ = bi_ = 0; bpend_err_ = 0;
    int rc = d_sync_set(at);
    if (rc < 0) {
        // fall back to a fresh decoder synchronising forward from that point
        alloc_decoder();
        rc = d_sync_set(at);
        if (rc < 0) return false;
    }
    status_ = rc; synced_ = true; eos_ = false; sync_fail_run_ = 0;
    return true;
}
bool PtDecoder::next_plan(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev) {
    if (!skipdbg_tried_) { skipdbg_tried_ = true; const char* p = getenv("PTRECON_SKIPDBG"); if (p && *p) skipdbg_ = fopen(p, "w"); }
    for (;;) {
        if (seg_i_ >= plan_->segs.size()) return false;
        const AuxSeg& s = plan_->segs[seg_i_];
        if (!positioned_) {
            // Same file and the segment starts a little ahead: just keep decoding forward (the
            // instructions in between are skipped); otherwise re-synchronise at the nearest PSB.
            bool cont = false;
            if (cur_file_ == s.file && synced_ && !eos_) { uint64_t off = offset(); cont = off <= s.begin && s.begin - off <= (64u << 10); }
            if (!cont && !seek_seg(s)) { n_segs_unsync++; seg_i_++; continue; }
            positioned_ = true; seg_first_ = true; n_segs++; if (s.lossy) n_segs_lossy++;
        }
        // Events are delivered only when the decoder's position is inside the segment; the
        // instruction only when its post-decode offset is.
        bool got = blk_ ? step_blk(insn, tsc, ev) : step(insn, tsc, ev, true);
        if (!got) {   // end of this file: the segment is over
            seg_i_++; positioned_ = false;
            continue;
        }
        uint64_t off = offset();
        if (off < s.begin) { n_skipped_insn++;
            // PTRECON_SKIPDBG=<file>: the instructions decoded just before a segment starts, to
            // tell "this thread's own instructions were lost at the boundary" (our bug) from
            // "they belong to the previous thread" (attribution).  Diagnostic only.
            if (skipdbg_) fprintf(skipdbg_, "skip file %d off %lu < begin %lu ip %#lx\n", s.file, (unsigned long)off, (unsigned long)s.begin, (unsigned long)insn.ip);
            continue; }
        if (off >= s.end) {
            // past this segment.  The instruction may belong to the NEXT segment of this thread
            // if that one is contiguous in the same file; otherwise it is another thread's.
            seg_i_++; positioned_ = false;
            if (seg_i_ < plan_->segs.size()) {
                const AuxSeg& t = plan_->segs[seg_i_];
                if (t.file == s.file && off >= t.begin && off < t.end) {
                    positioned_ = true; seg_first_ = true; n_segs++; if (t.lossy) n_segs_lossy++;
                    if (t.lossy) { if (ev.on_switch_loss) ev.on_switch_loss(tsc ? *tsc : 0); else if (ev.on_overflow) ev.on_overflow(tsc ? *tsc : 0); }
                    seg_first_ = false; n_insn++;
                    return true;
                }
            }
            n_skipped_insn++;
            continue;
        }
        if (seg_first_) {
            seg_first_ = false;
            if (s.lossy) { if (ev.on_switch_loss) ev.on_switch_loss(tsc ? *tsc : 0); else if (ev.on_overflow) ev.on_overflow(tsc ? *tsc : 0); }
        }
        n_insn++;
        return true;
    }
}

// In plan mode an event is delivered only when the decoder's position lies inside the current
// segment; elsewhere the packets belong to another thread and the event is consumed silently.
bool PtDecoder::drain_events(PtEvents& ev) {
    while (status_ & pts_event_pending) {
        struct pt_event e; status_ = d_event(&e);
        if (status_ < 0) return false;
        if (dec_hash_on) { hash_mix(0xe0000000ull + (uint64_t)e.type); hash_mix(offset());
                           if (e.type == ptev_ptwrite) hash_mix(e.variant.ptwrite.payload);
                           if (e.has_tsc) hash_mix(e.tsc); }
        bool deliver = true;
        if (plan_) { uint64_t off = offset(); const AuxSeg& s = plan_->segs[seg_i_ < plan_->segs.size() ? seg_i_ : plan_->segs.size() - 1];
                     deliver = positioned_ && off >= s.begin && off < s.end; }
        switch (e.type) {
            case ptev_ptwrite: if (!deliver) break; n_ptw++; if (ev.on_ptwrite) ev.on_ptwrite(e.variant.ptwrite.payload, e.variant.ptwrite.size, e.ip_suppressed ? 0 : e.variant.ptwrite.ip); break;
            case ptev_overflow:
                // The boundary is recorded at the FIRST INSTRUCTION AFTER the overflow (see step()),
                // not here: at this point the decoder's time is the last one BEFORE the gap, and
                // attributing the resumed region by that stale time handed the resumed thread's
                // instructions to whichever thread had the core before the gap.
                pending_boundary_ = true; pending_kind_ = 'O';
                if (!deliver) break;
                n_ovf++; if (ev.on_overflow) ev.on_overflow(e.has_tsc ? e.tsc : 0); break;
            case ptev_enabled:
                if (ev.on_boundary) { uint64_t t = 0; if (live_time(&t) < 0) t = 0; if (e.has_tsc && e.tsc) t = e.tsc; ev.on_boundary('E', offset(), t, e.variant.enabled.ip); }
                if (!deliver) break;
                if (ev.on_enable) ev.on_enable(e.variant.enabled.ip, true); break;
            case ptev_disabled: case ptev_async_disabled: if (!deliver) break; if (ev.on_enable) ev.on_enable(0, false); break;
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
    bool ok = plan_ ? next_plan(insn, tsc, ev) : blk_ ? step_blk(insn, tsc, ev) : step(insn, tsc, ev, true);
    if (ok && !plan_) n_insn++;
    if (ok && dec_hash_on) { hash_mix(insn.ip); hash_mix(insn.size); uint64_t w[2] = {0, 0}; memcpy(w, insn.raw, insn.size > 15 ? 15 : insn.size);
                             hash_mix(w[0]); hash_mix(w[1]); hash_mix(offset()); hash_mix(tsc ? *tsc : 1); }
    if (ok && progress_.limit && progress_.stalled(offset()))
        throw std::runtime_error("decoder nonprogress limit at AUX " + std::to_string(offset()) +
                                 ", IP " + std::to_string(insn.ip) + "; incomplete trace, no repair inferred");
    return ok;
}
bool PtDecoder::step(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev, bool deliver) {
    (void)deliver;
    for (;;) {
        if (eos_) return false;
        if (!synced_) {
            status_ = pt_insn_sync_forward(dec_);
            if (status_ < 0) {
                // Not every error ENDS THE TRACE: some are recoverable.  A `pt_insn_sync_forward'
                // that lands on a PSB it cannot use returns e.g. -pte_bad_opc or -pte_bad_packet,
                // and giving up there would throw away the rest
                // of a COMPLETE capture (measured: one whole-run nbody decode stopped at 23.2 M of
                // 1 615 M instructions with `trunc=0' on the capture side).  Only -pte_eos is
                // really the end; for anything else, step one byte past the offending sync point
                // and look for the next PSB, at most `kMaxResync' times in a row.
                if (status_ == -pte_eos) { eos_ = true; eos_why = "sync_forward: end of stream"; eos_off = offset(); eos_status = status_; return false; }
                uint64_t off = 0;
                if (pt_insn_get_offset(dec_, &off) < 0) { eos_ = true; eos_why = "sync_forward: no offset"; eos_status = status_; return false; }
                n_resync++; n_sync_fail++;
                if (ev.on_resync && ev_ok()) ev.on_resync(status_, off);
                pending_boundary_ = true;
                if (preinit_ && e9_unswapped_) e9_abort(skip_ + off);
                if (++sync_fail_run_ > kMaxResync) {
                    eos_ = true; eos_why = "too many consecutive sync failures";
                    eos_off = skip_ + off; eos_status = status_; return false;
                }
                // `pt_insn_sync_set(off + 1)' is NOT "look for the next PSB": libipt's
                // sync_set requires a PSB AT the offset it is given (pt_sync_set -> -pte_nosync
                // otherwise), so one byte past the offending PSB it fails essentially always and
                // the recovery would end the decode instead of continuing it.  Measured on a
                // memcached per-CPU capture: one failing PSB at 1 937 360 of 5 833 600 bytes
                // threw away the remaining 67 % of that core's trace, and the unsegmented tail
                // was then attributed, as one region, to whichever thread owned the last
                // boundary.  Step to the next REAL PSB instead (they are found by pattern, as
                // the chunk splitter and the plan-mode seek do), at most kMaxResync of them.
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
        if (!drain_events(ev)) { synced_ = false; n_resync++; pending_boundary_ = true; if (ev.on_resync && ev_ok()) ev.on_resync(status_, 0);
                                 if (preinit_ && e9_unswapped_) e9_abort(offset()); continue; }
        if (status_ & pts_eos) { eos_ = true; eos_why = "pts_eos after draining events"; eos_off = offset(); eos_status = status_; return false; }
        // Refresh the time key BEFORE decoding: pt_insn_next() reads the instruction's bytes
        // through the image (and, for a JIT address, through jit_read_cb above).
        // PTRECON_JIT_NOTIME=1 is the ABLATION: leave the key at "latest", i.e. serve the final
        // state of every JIT address, which is what a table without a time key would do.
        static const bool jit_notime = getenv("PTRECON_JIT_NOTIME") != nullptr;
        if (jit && !jit_notime) { uint64_t t = 0; uint32_t lm, lc; if (pt_insn_time(dec_, &t, &lm, &lc) >= 0 && t) cur_tsc = t; }
        status_ = pt_insn_next(dec_, &insn, sizeof insn);
        if (status_ < 0) {
            if (status_ == -pte_eos) { eos_ = true; eos_why = "pt_insn_next: end of stream"; eos_off = offset(); eos_status = status_; return false; }
            uint64_t off = 0; pt_insn_get_offset(dec_, &off);
            n_resync++; pending_boundary_ = true; if (ev.on_resync && ev_ok()) ev.on_resync(status_, off);
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
        // Segmentation hooks: the first instruction of the trace, and the first one decoded after
        // a resync (the decoder re-entered the stream at a PSB: a possible thread change).
        if (ev.on_boundary && (pending_boundary_ || !seen_insn_)) {
            uint64_t t = 0; uint32_t lm, lc; if (pt_insn_time(dec_, &t, &lm, &lc) < 0) t = 0;
            ev.on_boundary(pending_boundary_ ? pending_kind_ : 'S', offset(), t, insn.ip);   // a resync before the first instruction is still a resync
        }
        pending_boundary_ = false; pending_kind_ = 'R'; seen_insn_ = true;
        return true;
    }
}

// ---- BLOCK MODE ----------------------------------------------------------------------------------
// pt_insn_next() re-reads and re-decodes every instruction through the image (~23 M insn/s);
// pt_blk_next() returns a whole BLOCK -- a run of instructions executed without a trace
// query, following direct jumps and calls, inside ONE image section -- from libipt's per-section
// block cache.  The reconstructor still consumes one pt_insn at a time, with the SAME per-
// instruction offset and time the instruction decoder reported:
//   * a non-last instruction of a block needed no query (only the block's last one can: a
//     conditional/indirect branch, a compressed return, or an event binding), so the per-
//     instruction decoder's position after it is the position BEFORE the block;
//   * the last one sees the live position after pt_blk_next().
// The instruction bytes of a block are served from a walk cache keyed by (isid, first ip): a
// cached section's bytes never change, and the walk is determined by them (next = ip + size, plus
// the displacement of a direct jump/call -- the only branches a block continues through, exactly
// ptxed's block walk).  Every block's walk must end at the block's end_ip, or we stop: a mismatch
// is a decoder bug, not something to paper over.  PTRECON_DECHASH=1 hashes the delivered stream in
// both modes for the identity check.
static ZydisDecoder g_bzd; static bool g_bzd_init = false;
const PtDecoder::BInsn* PtDecoder::blk_walk(const struct pt_block& b) {
    if (b.isid <= 0) throw std::runtime_error("block decoder: block without a section id at ip " + std::to_string(b.ip) + " (rerun with PTRECON_DECODER=insn)");
    if (b.mode != ptem_64bit) throw std::runtime_error("block decoder: non-64-bit block at ip " + std::to_string(b.ip) + " (rerun with PTRECON_DECODER=insn)");
    const uint64_t key = b.ip | ((uint64_t)(uint32_t)b.isid << 48);
    BFront& fr = bfront_[(size_t)((key * 0x9E3779B97F4A7C15ull) >> 46)];   // 2^18 slots, multiplicative hash
    std::vector<BInsn>* v;
    if (fr.key == key) v = fr.v;
    else { v = &bwalk_[key]; fr.key = key; fr.v = v; }
    const size_t need = b.truncated ? (size_t)b.ninsn - 1 : (size_t)b.ninsn;   // a truncated last insn comes from the block
    if (v->size() < need) {
        n_walk_miss++;
        if (!g_bzd_init) { ZydisDecoderInit(&g_bzd, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64); g_bzd_init = true; }
        struct pt_image_section_cache* isc = iscache_;
        uint64_t ip = v->empty() ? b.ip : v->back().next;
        if (!v->empty() && v->back().kind == 2)
            throw std::runtime_error("block decoder: block continues past a branch it cannot follow at ip " + std::to_string(v->back().ip));
        while (v->size() < need) {
            BInsn bi; bi.ip = ip;
            int got = pt_iscache_read(isc, bi.raw, sizeof bi.raw, b.isid, ip);
            if (got <= 0) throw std::runtime_error("block decoder: cannot read instruction bytes at ip " + std::to_string(ip));
            ZydisDecodedInstruction zi;
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&g_bzd, nullptr, bi.raw, (ZyanUSize)got, &zi)))
                { char hx[64] = {0}; for (int q = 0; q < got && q < 15; q++) snprintf(hx + 2 * q, 3, "%02x", bi.raw[q]);
                  throw std::runtime_error("block decoder: undecodable instruction at ip " + std::to_string(ip) + " isid " + std::to_string(b.isid) + " block " + std::to_string(b.ip) + ".." + std::to_string(b.end_ip) + " n " + std::to_string(b.ninsn) + " bytes " + hx); }
            bi.size = zi.length;
            if (bi.size < sizeof bi.raw) memset(bi.raw + bi.size, 0, sizeof bi.raw - bi.size);
            // kind 0: falls through; 1: direct jmp/call (the block follows it); 2: anything else
            // that transfers control (a block ends there, so nothing may follow it in a walk).
            const auto cat = zi.meta.category;
            const bool rel = zi.raw.imm[0].is_relative;
            if ((cat == ZYDIS_CATEGORY_CALL || cat == ZYDIS_CATEGORY_UNCOND_BR) && rel) { bi.kind = 1; bi.next = ip + bi.size + (uint64_t)zi.raw.imm[0].value.s; }
            else if (cat == ZYDIS_CATEGORY_CALL || cat == ZYDIS_CATEGORY_UNCOND_BR || cat == ZYDIS_CATEGORY_COND_BR ||
                     cat == ZYDIS_CATEGORY_RET || cat == ZYDIS_CATEGORY_SYSCALL || cat == ZYDIS_CATEGORY_INTERRUPT ||
                     cat == ZYDIS_CATEGORY_SYSRET || zi.meta.branch_type == ZYDIS_BRANCH_TYPE_FAR) { bi.kind = 2; bi.next = ip + bi.size; }
            else { bi.kind = 0; bi.next = ip + bi.size; }
            v->push_back(bi);
            if (v->size() < need && bi.kind == 2)
                throw std::runtime_error("block decoder: block continues past a branch it cannot follow at ip " + std::to_string(ip));
            ip = bi.next;
        }
    }
    // the walk must land on the block's last instruction
    const uint64_t last_ip = b.truncated ? (need ? (*v)[need - 1].next : b.ip) : (*v)[need - 1].ip;
    if (last_ip != b.end_ip)
        throw std::runtime_error("block decoder: walk from " + std::to_string(b.ip) + " ends at " + std::to_string(last_ip) +
                                 ", block says " + std::to_string(b.end_ip) + " (rerun with PTRECON_DECODER=insn)");
    return v->data();
}

bool PtDecoder::step_blk(struct pt_insn& insn, uint64_t* tsc, PtEvents& ev) {
    if (bi_ >= bn_ && !fetch_blk(ev)) return false;
    {
        {
            // ---- deliver one instruction of the current block --------------------------------
            const bool last = bi_ + 1 == bn_;
            memset(&insn, 0, sizeof insn);
            insn.mode = blk_cur_.mode; insn.isid = blk_cur_.isid;
            if (last && blk_cur_.truncated) {
                insn.ip = blk_cur_.end_ip; insn.size = blk_cur_.size; memcpy(insn.raw, blk_cur_.raw, blk_cur_.size);
            } else {
                const BInsn& bi = bcur_[bi_];
                insn.ip = bi.ip; insn.size = bi.size; memcpy(insn.raw, bi.raw, sizeof bi.raw);
            }
            insn.iclass = last ? blk_cur_.iclass : ptic_other;
            insn.speculative = blk_cur_.speculative;
            bi_++;
            if (preinit_ && e9_unswapped_) {
                // A loader segment is a section of its own and a block never leaves its section,
                // so the rest of this block reads the same bytes before and after the swap; the
                // next block is decoded with the swapped image, exactly as pt_insn would.
                n_e9_preinit_insn++;
                int ii = e9_.loader_image(insn.ip);
                if (ii >= 0 && !e9_[(size_t)ii].swapped) e9_swap((size_t)ii, insn.ip);
            }
            if (tsc && !no_time) { if (insn_time(tsc) < 0) *tsc = 0; }
            else if (tsc) *tsc = 0;
            if (ev.on_boundary && (pending_boundary_ || !seen_insn_)) {
                uint64_t t = 0; if (insn_time(&t) < 0) t = 0;
                ev.on_boundary(pending_boundary_ ? pending_kind_ : 'S', offset(), t, insn.ip);
            }
            pending_boundary_ = false; pending_kind_ = 'R'; seen_insn_ = true;
            return true;
        }
    }
}

// The refill half of step_blk -- everything between two deliveries.
// Returns true with bi_ < bn_ (a block ready), false at the end of the stream.
bool PtDecoder::fetch_blk(PtEvents& ev) {
    for (;;) {
        if (bi_ < bn_) return true;
        if (bpend_err_) {
            // pt_blk_next failed after decoding the instructions just delivered: handle the error
            // where the per-instruction decoder would have seen it (its next pt_insn_next).
            status_ = bpend_err_; bpend_err_ = 0;
            if (status_ == -pte_eos) { eos_ = true; eos_why = "pt_insn_next: end of stream"; eos_off = offset(); eos_status = status_; return false; }
            uint64_t off = live_offset();
            n_resync++; pending_boundary_ = true; if (ev.on_resync) ev.on_resync(status_, off);
            if (preinit_ && e9_unswapped_) e9_abort(skip_ + off);
            synced_ = false; continue;
        }
        if (eos_) return false;
        if (!synced_) {
            status_ = d_sync_forward();
            if (status_ < 0) {
                // Identical to step().
                if (status_ == -pte_eos) { eos_ = true; eos_why = "sync_forward: end of stream"; eos_off = offset(); eos_status = status_; return false; }
                uint64_t off = live_offset();
                n_resync++; n_sync_fail++;
                if (ev.on_resync) ev.on_resync(status_, off);
                pending_boundary_ = true;
                if (preinit_ && e9_unswapped_) e9_abort(skip_ + off);
                if (++sync_fail_run_ > kMaxResync) { eos_ = true; eos_why = "too many consecutive sync failures"; eos_off = skip_ + off; eos_status = status_; return false; }
                bool resynced = false;
                for (uint64_t nxt = next_psb_after(off); nxt != ~0ull; nxt = next_psb_after(nxt)) {
                    if (d_sync_set(nxt) >= 0) { resynced = true; break; }
                    n_sync_fail++;
                    if (++sync_fail_run_ > kMaxResync) break;
                }
                if (!resynced) {
                    eos_ = true; eos_why = sync_fail_run_ > kMaxResync ? "too many consecutive sync failures" : "no usable PSB after the failing one";
                    eos_off = skip_ + off; eos_status = status_; return false;
                }
                continue;
            }
            synced_ = true; sync_fail_run_ = 0;
        }
        if (!drain_events(ev)) { synced_ = false; n_resync++; pending_boundary_ = true; if (ev.on_resync) ev.on_resync(status_, 0);
                                 if (preinit_ && e9_unswapped_) e9_abort(offset()); continue; }
        if (status_ & pts_eos) { eos_ = true; eos_why = "pts_eos after draining events"; eos_off = offset(); eos_status = status_; return false; }
        boff0_ = live_offset();
        btsc0_ = 0; btsc0_ok_ = live_time(&btsc0_);
        memset(&blk_cur_, 0, sizeof blk_cur_);
        int st = pt_blk_next(bdec_, &blk_cur_, sizeof blk_cur_);
        n_blocks++;
        bool walked = true;
        if (blk_cur_.ninsn) {
            // A walk that cannot follow libipt's block (bytes Zydis cannot decode, a walk that
            // misses end_ip) is a DECODE ERROR of this stream position, exactly where pt_insn_next would
            // report -pte_bad_insn -- in the E9 pre-init window libipt decodes whatever bytes the image
            // holds before the swap, including gap filler.  It does not abort the whole decode.
            try { bcur_ = blk_walk(blk_cur_); bn_ = blk_cur_.ninsn; bi_ = 0; }
            catch (std::runtime_error& e) {
                if (++n_walk_fail <= 3) fprintf(stderr, "ptdecode: %s -- treated as a decode error (resync)\n", e.what());
                bn_ = bi_ = 0; walked = false; }
        }
        else { bn_ = bi_ = 0; }
        if (!walked) bpend_err_ = -pte_bad_insn;
        else if (st < 0) bpend_err_ = st; else status_ = st;
    }
}

uint64_t PtDecoder::skip_nonlast(uint64_t max) {
    if (!blk_ || dec_hash_on || progress_.limit || bi_ >= bn_) return 0;
    uint64_t k = (uint64_t)(bn_ - 1 - bi_);
    if (k > max) k = max;
    if (!k) return 0;
    for (uint64_t j = 0; j < k && preinit_ && e9_unswapped_; j++) {   // exactly step_blk()'s per-instruction check
        const BInsn& bi = bcur_[bi_ + j];
        n_e9_preinit_insn++;
        int ii = e9_.loader_image(bi.ip);
        if (ii >= 0 && !e9_[(size_t)ii].swapped) e9_swap((size_t)ii, bi.ip);
    }
    bi_ += (uint32_t)k; n_insn += k;
    return k;
}
