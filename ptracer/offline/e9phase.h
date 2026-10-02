// e9phase.h -- TIME-AWARE DECODER IMAGES for E9Patch-rewritten files.
//
// An E9Patch-rewritten ELF contains BOTH versions of its executable segments:
//   * the ORIGINAL bytes, still at their original file offset and vaddr (the file's own PT_LOADs
//     are left untouched, so `ld.so' maps the unrewritten program exactly as it would have);
//   * a PATCHED copy, appended to the file at some other offset, together with the trampoline
//     pages and an E9Patch LOADER segment (an appended PT_LOAD with `p_paddr != p_vaddr', mapped
//     at a fixed high vaddr and reached through the image's `DT_INIT' / `e_entry').
// The loader `mmap's the patched copy over the original vaddr with MAP_FIXED when the image is
// initialised -- i.e. at `DT_INIT' for a shared library and at `e_entry' for the executable.
//
// Everything the image runs BEFORE that point therefore executes the ORIGINAL bytes: libc's IFUNC
// resolvers, which `ld.so' calls while it processes relocations, run long before any `DT_INIT'.
// The sideband's memory map is a union over time (pt_capture2's poll sees the patched mapping
// supersede the original one), so an image built from it alone is wrong for that window: the
// decoder follows patch jumps that did not exist yet and walks into trampolines that never ran.
// Measured on a memcached run: 7 libipt resyncs in the
// first 25 KB of the main thread's trace, with 0 lost AUX bytes and no OVF packet anywhere.
//
// This module classifies every executable mapping of every rewritten file into
//   orig     -- the original executable PT_LOADs (the pre-init bytes, read from the same file),
//   patched  -- the runtime mappings that overlay them (the post-init bytes), WITHHELD until the
//               image's loader executes,
//   loader   -- the vaddr range of the appended loader segment: executing an instruction in it
//               means this image has just installed (or is about to install) its patched copy.
// Everything else (trampoline pages, non-rewritten files) is unaffected and added as before.
//
// The swap is append-only: libipt's pt_image_add_* "shrink or split" existing sections that the
// new one overlaps, so adding the patched overlay on top of the original section at the swap
// point is enough -- no removal, and nothing to undo.  See ptdecode.cpp.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct MapEnt;

struct E9Sec { uint64_t off = 0, len = 0, vaddr = 0; };
struct E9Range { uint64_t lo = 0, hi = 0; };

struct E9ImageInfo {
    std::string path;
    uint64_t bias = 0;
    std::vector<E9Sec> orig;       // original executable PT_LOADs, at runtime vaddrs
    std::vector<E9Sec> patched;    // the runtime mappings that overlay them
    std::vector<E9Range> loader;   // runtime vaddr range(s) of the appended loader segment(s)
    bool swapped = false;          // patched sections already in the image?
    uint64_t swap_tsc = 0, swap_ip = 0;
};

class E9Phases {
public:
    // Classify the sideband's mappings.  Files without an appended `p_paddr != p_vaddr'
    // executable PT_LOAD are not E9Patch-rewritten and are ignored entirely.
    void build(const std::vector<MapEnt>& maps);
    bool empty() const { return imgs_.empty(); }
    size_t size() const { return imgs_.size(); }
    E9ImageInfo& operator[](size_t i) { return imgs_[i]; }
    const E9ImageInfo& operator[](size_t i) const { return imgs_[i]; }
    // Index of the image this executable mapping is a patched overlay of (it is withheld in the
    // pre-init phase and added at the swap), or -1 if the mapping is not an overlay at all.
    int patched_overlay_image(const MapEnt& m) const;
    // Index of the image whose loader segment contains `ip', or -1.
    int loader_image(uint64_t ip) const;
private:
    std::vector<E9ImageInfo> imgs_;
    std::map<std::pair<uint64_t, uint64_t>, int> overlay_;   // (mapping start, file offset) -> image
    uint64_t ld_lo_ = ~0ull, ld_hi_ = 0;                      // hull of every loader range (fast reject)
};
