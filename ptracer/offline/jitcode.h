// jitcode.h -- PTracer Stage 3: the TIME-KEYED CODE TABLE built from one or more jitdumps
// (`ptrecon --jitdump FILE`).
//
// A JIT address is only valid for a time interval: V8 and HotSpot both reuse the address of a
// dead code object, V8's mark-compact GC evacuates code objects (`evt 1` CODE_MOVED) and HotSpot
// unloads them (`evt 2` CODE_REMOVED).  pt_capture2 dumps every anonymous executable mapping,
// but it dumps each range ONCE, when it first sees it -- so for a JIT code space (and for the
// trampoline slab, which is filled long after its pages first appear) that snapshot is a
// single point in time and cannot be the decoder's image.  This table replaces it:
//
//   * `read()`  -- the bytes the CPU EXECUTED at an address at a given trace time: the object's
//                  `evt 0` bytes, moved by the `evt 1` records, with the `evt 3` patched windows
//                  applied; then the trampolines (`evt 4`) and the slab (`evt 5`); then, only as
//                  a fallback, the stale anonymous dump ("background").  Wired into libipt
//                  through pt_image_set_callback(), which is consulted for exactly the addresses
//                  that are in no file section.
//   * `orig_at()` -- the bytes the JIT GENERATED (unpatched), for the copies of original
//                  instructions inside a trampoline and for a `memop` site's effective address.
//                  Keyed by the site map's own `obj`/`tsc`, so it needs no trace time at all.
//
// THE TIME KEY.  Trace time is libipt's TSC/MTC estimate.
// It is only ever consulted to DISAMBIGUATE: an address covered by exactly one version in the
// whole run is served unconditionally.  Measured reuse distances are 3.7 ms (V8) and 74 ms
// (HotSpot) at their minimum, against an MTC granularity of a few microseconds, so the key has
// four to five orders of magnitude of margin for those code objects. The opt-in HotSpot
// history recorder also covers much shorter-lived inline-cache stubs, to which that margin
// does NOT apply.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

struct JitRange { uint64_t lo, hi; };

class JitTable {
public:
    // ---- construction ------------------------------------------------------------------
    bool load(const std::string& path, std::string* err);   // repeatable: one call per --jitdump
    // V8 reports its embedded builtins as CODE_ADDED although they live in the node ELF and are
    // decoded from it; anything inside a file-backed executable mapping is dropped here so that
    // the ELF stays the authority for it (and so that its mapping is not suppressed in libipt).
    void drop_file_backed(const std::vector<JitRange>& file_exec);
    void finalize();
    // The stale anonymous dump of a mapping this table covers: kept as a last-resort layer.
    void add_background(uint64_t lo, uint64_t hi, const uint8_t* bytes, size_t n);

    // ---- queries -----------------------------------------------------------------------
    bool empty() const { return vers_.empty() && tramps_.empty() && slabs_.empty() && native_patches_.empty(); }
    bool overlaps(uint64_t lo, uint64_t hi) const;      // does this table describe any of [lo,hi)?
    // If ONLY explicit native overlays touch this mapping, its other pages are
    // immutable and can retain libipt's fast file cache. Dynamic JIT maps return {}.
    std::vector<JitRange> immutable_ranges(uint64_t lo, uint64_t hi) const;
    // Bytes as EXECUTED at `addr` at trace time `tsc` (UINT64_MAX = "latest").  Returns the
    // number of bytes written to `buf` (may be < n at the end of an object), 0 = not described.
    int read(uint8_t* buf, size_t n, uint64_t addr, uint64_t tsc);
    // Bytes as GENERATED (unpatched) of the object version that covered `addr` at `tsc`.
    const uint8_t* orig_at(uint64_t addr, uint64_t tsc, size_t* avail) const;
    // Program code (all code-object versions, coalesced) and the trampoline/slab regions.
    const std::vector<JitRange>& code_ranges() const { return code_ranges_; }
    const std::vector<JitRange>& tramp_ranges() const { return tramp_ranges_; }
    bool in_code(uint64_t a) const;                      // binary search over code_ranges_
    // 6l: may read() answer for `a' at all?  (all_ranges_ + background dumps; O(log n)).  Lets the
    // decoder consult the table BEFORE libipt's linear section-list walk (private libipt patch).
    bool describes(uint64_t a) const { if (in_sorted(all_ranges_, a)) return true; for (auto& b : bg_) if (a >= b.lo && a < b.hi) return true; return false; }
    bool in_tramp(uint64_t a) const;

    // ---- statistics ------------------------------------------------------------------
    uint64_t n_records = 0, n_added = 0, n_moved = 0, n_removed = 0, n_patched = 0,
             n_tramp_rec = 0, n_slab_rec = 0, n_truncated = 0, n_dropped_file = 0,
             n_orphan_patch = 0, n_move_unknown = 0, n_reused_addr = 0, n_versions = 0,
             n_bytecode = 0, n_native_patch = 0, r_native_patch = 0;
    uint64_t code_bytes = 0;
    // read() accounting -- this is the JIT-code COVERAGE metric
    uint64_t r_obj = 0, r_tramp = 0, r_slab = 0, r_bg = 0, r_miss = 0, r_ambiguous = 0,
             r_time_used = 0, r_compose = 0;
    // how far libipt's time estimate lagged the jitdump stamp of the version it had to serve
    uint64_t t_behind = 0; uint64_t t_behind_max = 0;
    // Where the table could NOT answer: the first 32 distinct pages served from the stale
    // anonymous dump, and the first 32 that were unreadable.  Reported in the summary so a
    // coverage hole is a diagnosable address, not just a percentage.
    std::vector<uint64_t> bg_pages, miss_pages;
    std::string stats_json() const;

private:
    struct Patch { uint64_t tsc, addr; uint32_t len; const uint8_t* bytes; uint32_t off = 0; };
    struct Ver {
        uint64_t addr = 0, tsc = 0, dead = ~0ull;
        uint32_t len = 0;
        const uint8_t* orig = nullptr;      // evt-0 bytes (shared with the moved-from version)
        int32_t pimg = -1;                  // index into pimgs_ (fully patched image), -1 = none
        uint32_t p_first = 0, p_n = 0;      // range in patches_
        uint64_t p_lo = ~0ull, p_hi = 0;    // first / last patch tsc
        uint8_t code_type = 1;
    };
    struct Tramp { uint64_t addr, tsc; uint32_t len; const uint8_t* bytes; };
    struct Slab { uint64_t lo, hi; const uint8_t* bytes; };
    struct Bg { uint64_t lo, hi; const uint8_t* bytes; };

    std::vector<std::vector<uint8_t>> files_;   // the jitdump files, kept alive: bytes are pointers into them
    std::vector<Ver> vers_;
    std::vector<Patch> raw_patches_;            // evt 3 records, before ownership is resolved
    // evt 6: explicit native ELF byte overlays. Unlike CODE_ADDED, these survive
    // drop_file_backed(). The underlying ELF remains the background for other bytes.
    std::vector<Patch> native_patches_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> native_page_;
    std::vector<Patch> patches_;                // grouped per version by finalize()
    std::vector<std::vector<uint8_t>> pimgs_;
    std::vector<Tramp> tramps_;
    std::vector<Slab> slabs_;
    std::vector<Bg> bg_;
    std::vector<JitRange> code_ranges_, tramp_ranges_, all_ranges_, native_ranges_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> vpage_, tpage_;
    size_t last_ver_ = (size_t)-1;
    bool final_ = false;

    const Ver* find_ver(uint64_t a, uint64_t t, bool* ambiguous);
    int read_base(uint8_t* buf, size_t n, uint64_t addr, uint64_t tsc);
    const Ver* find_ver_const(uint64_t a, uint64_t t) const;
    void kill_overlapping(uint64_t lo, uint64_t hi, uint64_t tsc);
    static bool in_sorted(const std::vector<JitRange>& v, uint64_t a);
};
