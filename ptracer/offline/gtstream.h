#ifndef PTRACER_GTSTREAM_H
#define PTRACER_GTSTREAM_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include "../runtime/rt/ptgt_format.h"

struct GtRec { uint64_t addr, ip; };

// Random access to the logical memory-access stream, without allocating one
// object per REP element. Index storage is O(number of ranges), not O(accesses).
// This reader uses ONLY recorded machine inputs and completion witnesses; it
// never consults the reconstruction's state or addresses.
class GtStream {
    struct Span { size_t begin, end, raw; bool range; };
    const GtRec* raw_ = nullptr;
    std::vector<Span> spans_;
    size_t size_ = 0, ranges_ = 0, physical_ = 0;
    static bool tag(uint64_t ip) { return ip >= uint64_t(PTGT_REP_COMMIT); }
    void add(size_t raw, uint64_t count, bool range) {
        if (count > std::numeric_limits<size_t>::max() - size_)
            throw std::runtime_error("PTGT: expanded access count overflow");
        if (count) spans_.push_back({size_, size_ + size_t(count), raw, range});
        size_ += size_t(count);
    }
public:
    void open(const GtRec* raw, size_t count, unsigned version) {
        if (version != 1 && version != PTGT_VERSION)
            throw std::runtime_error("PTGT: unsupported version");
        raw_ = raw; spans_.clear(); size_ = ranges_ = physical_ = 0;
        // A live sparse window has an all-zero tail. Half-written slots and
        // zero holes inside a nonzero stream are not valid tail padding.
        while (count && raw[count - 1].ip == 0 && raw[count - 1].addr == 0) --count;
        physical_ = count;
        for (size_t i = 0; i < count;) {
            if (!raw[i].ip) throw std::runtime_error("PTGT: zero IP inside stream / partial slot");
            if (!tag(raw[i].ip)) {
                const size_t begin = i++;
                while (i < count && raw[i].ip && !tag(raw[i].ip)) ++i;
                add(begin, i - begin, false);
                continue;
            }
            if (version == 1 || raw[i].ip != uint64_t(PTGT_REP_BASE))
                throw std::runtime_error("PTGT: invalid range tag");
            if (count - i < 5 || raw[i + 1].ip != uint64_t(PTGT_REP_COUNT) ||
                raw[i + 2].ip != uint64_t(PTGT_REP_FLAGS) || !raw[i + 3].ip || tag(raw[i + 3].ip) ||
                raw[i + 4].ip != uint64_t(PTGT_REP_COMMIT) || raw[i + 4].addr != 0)
                throw std::runtime_error("PTGT: incomplete, interleaved or uncompleted REP range");
            uint64_t width = raw[i + 3].addr;
            if (width != 1 && width != 2 && width != 4 && width != 8)
                throw std::runtime_error("PTGT: invalid REP element width");
            add(i, raw[i + 1].addr, true);
            ++ranges_; i += 5;
        }
    }
    size_t size() const { return size_; }
    size_t ranges() const { return ranges_; }
    size_t physical() const { return physical_; }
    GtRec operator[](size_t index) const {
        if (index >= size_) throw std::out_of_range("PTGT: logical index");
        auto it = std::upper_bound(spans_.begin(), spans_.end(), index,
            [](size_t i, const Span& s) { return i < s.end; });
        const size_t offset = index - it->begin;
        if (!it->range) return raw_[it->raw + offset];
        const GtRec* r = raw_ + it->raw;
        uint64_t delta = uint64_t(offset) * r[3].addr;
        return {r[0].addr + ((r[2].addr & (1u << 10)) ? -delta : delta), r[3].ip};
    }
};
#endif
