// e9phase.cpp -- see e9phase.h.  Classify the executable mappings of every E9Patch-rewritten
// file into the pre-init (original) and post-init (patched) view of that image.
#include "e9phase.h"
#include "ptdecode.h"
#include <cstdio>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <map>
#include <set>
#include <unistd.h>

namespace {
constexpr uint64_t kPage = 0x1000;
inline uint64_t pdown(uint64_t x) { return x & ~(kPage - 1); }
inline uint64_t pup(uint64_t x) { return (x + kPage - 1) & ~(kPage - 1); }

struct Seg { uint32_t type, flags; uint64_t off, vaddr, paddr, filesz, memsz; };

// The ELF program headers of `path' (x86-64, little endian, ELF64 -- the only thing we trace).
bool read_phdrs(const std::string& path, std::vector<Seg>& out) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    Elf64_Ehdr eh;
    if (pread(fd, &eh, sizeof eh, 0) != (ssize_t)sizeof eh ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_phentsize != sizeof(Elf64_Phdr) || !eh.e_phnum) { close(fd); return false; }
    std::vector<Elf64_Phdr> ph(eh.e_phnum);
    ssize_t want = (ssize_t)(sizeof(Elf64_Phdr) * eh.e_phnum);
    if (pread(fd, ph.data(), (size_t)want, (off_t)eh.e_phoff) != want) { close(fd); return false; }
    close(fd);
    for (auto& p : ph)
        out.push_back(Seg{p.p_type, p.p_flags, p.p_offset, p.p_vaddr, p.p_paddr, p.p_filesz, p.p_memsz});
    return true;
}
}  // namespace

void E9Phases::build(const std::vector<MapEnt>& maps) {
    imgs_.clear();
    // Files with at least one executable mapping, in first-appearance order.
    std::vector<std::string> paths;
    { std::set<std::string> seen;
      for (auto& m : maps) if (m.exec() && !m.path.empty() && m.path[0] == '/' && seen.insert(m.path).second)
          paths.push_back(m.path); }

    for (auto& path : paths) {
        std::vector<Seg> segs;
        if (!read_phdrs(path, segs)) continue;
        std::vector<const Seg*> orig_x, loader_x;
        for (auto& s : segs) {
            if (s.type != PT_LOAD || !(s.flags & PF_X)) continue;
            if (s.paddr != s.vaddr) loader_x.push_back(&s);   // E9Patch's appended loader segment
            else                    orig_x.push_back(&s);
        }
        if (loader_x.empty() || orig_x.empty()) continue;     // not an E9Patch-rewritten file

        // The load bias: a mapping whose file offset is the page-aligned p_offset of a PT_LOAD is
        // that PT_LOAD as `ld.so' (or the E9Patch loader) mapped it, so bias = start - p_vaddr.
        // The patched overlay is NOT one of these -- its file offset belongs to no PT_LOAD at all,
        // which is exactly why it cannot be mistaken for the original mapping.
        std::map<uint64_t, int> votes;
        for (auto& m : maps) {
            if (m.path != path) continue;
            for (auto& s : segs) {
                if (s.type != PT_LOAD) continue;
                if (m.off == pdown(s.off)) votes[m.start - pdown(s.vaddr)]++;
            }
        }
        if (votes.empty()) continue;
        uint64_t bias = 0; int best = -1;
        for (auto& v : votes) if (v.second > best) { best = v.second; bias = v.first; }

        E9ImageInfo im; im.path = path; im.bias = bias;
        for (const Seg* s : orig_x) im.orig.push_back(E9Sec{s->off, s->filesz, bias + s->vaddr});
        for (const Seg* s : loader_x)
            im.loader.push_back(E9Range{bias + pdown(s->vaddr), bias + pup(s->vaddr + s->memsz)});
        // Every executable mapping that overlaps an original executable PT_LOAD but does not come
        // from its file offset is the patched copy mapped over it.
        for (auto& m : maps) {
            if (!m.exec() || m.path != path) continue;
            for (const Seg* s : orig_x) {
                uint64_t lo = bias + s->vaddr, hi = bias + s->vaddr + s->memsz;
                if (m.start >= hi || m.end <= lo) continue;
                uint64_t expect = s->off + (m.start - lo);
                if (m.off != expect) {
                    im.patched.push_back(E9Sec{m.off, m.end - m.start, m.start});
                    overlay_[std::make_pair(m.start, m.off)] = (int)imgs_.size();
                }
                break;
            }
        }
        if (im.patched.empty()) continue;   // rewritten, but nothing was remapped: nothing to do
        imgs_.push_back(std::move(im));
    }
    for (auto& im : imgs_) for (auto& r : im.loader) { if (r.lo < ld_lo_) ld_lo_ = r.lo; if (r.hi > ld_hi_) ld_hi_ = r.hi; }
}

int E9Phases::patched_overlay_image(const MapEnt& m) const {
    auto it = overlay_.find(std::make_pair(m.start, m.off));
    return it == overlay_.end() ? -1 : it->second;
}

int E9Phases::loader_image(uint64_t ip) const {
    if (ip < ld_lo_ || ip >= ld_hi_) return -1;      // called per decoded instruction while any image is pre-init
    for (size_t i = 0; i < imgs_.size(); i++)
        for (auto& r : imgs_[i].loader)
            if (ip >= r.lo && ip < r.hi) return (int)i;
    return -1;
}
