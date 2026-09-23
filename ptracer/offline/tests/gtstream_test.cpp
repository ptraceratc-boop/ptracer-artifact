#include "../gtstream.h"
#include <cassert>
#include <iostream>

static std::vector<GtRec> range(uint64_t n, uint64_t width, bool backward) {
    return {{0x10000, uint64_t(PTGT_REP_BASE)}, {n, uint64_t(PTGT_REP_COUNT)},
            {backward ? 1024u : 0u, uint64_t(PTGT_REP_FLAGS)}, {width, 0x1234},
            {0, uint64_t(PTGT_REP_COMMIT)}};
}
static void rejects(const std::vector<GtRec>& rows, unsigned version = 2) {
    GtStream stream;
    bool caught = false;
    try { stream.open(rows.data(), rows.size(), version); }
    catch (const std::runtime_error&) { caught = true; }
    assert(caught);
}
int main() {
    GtStream stream;
    for (uint64_t width : {1, 2, 4, 8}) for (bool back : {false, true})
        for (uint64_t n : {0, 1, 9, 1048576}) {
            auto rows = range(n, width, back);
            rows.insert(rows.begin(), {42, 0x5678});
            rows.push_back({43, 0x5679}); rows.resize(rows.size() + 8);
            stream.open(rows.data(), rows.size(), 2);
            assert(stream.size() == n + 2 && stream.ranges() == 1 && stream.physical() == 7);
            assert(stream[0].addr == 42 && stream[n + 1].addr == 43);
            if (n) for (uint64_t i : {uint64_t(0), n / 2, n - 1}) {
                assert(stream[i + 1].ip == 0x1234);
                assert(stream[i + 1].addr == (back ? 0x10000 - i * width : 0x10000 + i * width));
            }
        }
    std::vector<GtRec> old = {{42, 123}, {43, 124}, {0, 0}};
    for (unsigned v : {1, 2}) {
        stream.open(old.data(), old.size(), v);
        assert(stream.size() == 2 && stream[1].addr == 43 && !stream.ranges());
    }
    rejects(old, 3); rejects(range(1, 1, false), 1);
    for (size_t n = 1; n < 5; ++n) { auto r = range(8, 1, false); r.resize(n); rejects(r); }
    auto r = range(8, 1, false); r[4].addr = 1; rejects(r); // did not complete
    r = range(8, 1, false); r[2] = {42, 123}; rejects(r); // interleaving
    r = range(8, 3, false); rejects(r);
    old[1] = {0, 0}; old[2] = {43, 124}; rejects(old); // zero hole
    rejects({{42, 0}}); // half-written slot
    r = range(UINT64_MAX, 1, false); r.push_back({42, 123}); rejects(r); // overflow
    std::cout << "PASS GT stream: 32 ranges, legacy, sparse tail and rejection cases\n";
}
