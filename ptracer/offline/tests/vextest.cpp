// vextest.cpp -- unit test of the VEX lifter+interpreter WITHOUT Intel PT: feed hand-assembled instruction
// bytes as a synthetic IP stream, seed registers, check reconstructed addresses and register values.
#include "../vexinterp.h"
#include <cstdio>
#include <cstring>
#include <libvex_guest_offsets.h>
struct T { const char* name; std::vector<uint8_t> bytes; };
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok: %s\n", msg); } while (0)
int main() {
    VexLifter L; VexInterp I;
    std::vector<MemAccess> acc; auto emit = [&](const MemAccess& m) { acc.push_back(m); };
    auto run = [&](uint64_t ip, std::vector<uint8_t> b) { auto blk = L.lift_one(ip, b.data(), b.size()); if (!blk->ok) { printf("lift fail @%lx: %s\n", (unsigned long)ip, blk->err.c_str()); fails++; return; } std::vector<MemAccess> acc; I.exec(*blk, &acc); for (auto& m : acc) emit(m); };   // compiled skeleton (PTRECON_INTERP=tree: tree walker)
    // 1. mov (%rdi),%rdi with rdi known -> load at rdi, rdi becomes unknown (value not logged)
    I.set_reg(OFFSET_amd64_RDI, 8, 0x1000);
    run(0x400000, {0x48, 0x8b, 0x3f});
    CHECK(acc.size() == 1 && acc[0].known && acc[0].addr == 0x1000 && acc[0].size == 8 && acc[0].op == 0, "chase load address");
    CHECK(!I.get_reg(OFFSET_amd64_RDI, 8).known(), "loaded value unknown without log");
    I.set_reg(OFFSET_amd64_RDI, 8, 0x2000);   // simulate the logged value (ptwrite after load)
    // 2. add 0x8(%rdi),%rax  (memop load; rax unknown after)
    acc.clear(); run(0x400003, {0x48, 0x03, 0x47, 0x08});
    CHECK(acc.size() == 1 && acc[0].known && acc[0].addr == 0x2008, "load-op address rdi+8");
    // 3. lea (%rsi,%rdx,8),%rdx ; mov %rax,(%rdx)  with rsi=0x3000 rdx=2 -> store at 0x3010
    I.set_reg(OFFSET_amd64_RSI, 8, 0x3000); I.set_reg(OFFSET_amd64_RDX, 8, 2);
    acc.clear(); run(0x400010, {0x48, 0x8d, 0x14, 0xd6}); run(0x400014, {0x48, 0x89, 0x02});
    CHECK(acc.size() == 1 && acc[0].op == 1 && acc[0].known && acc[0].addr == 0x3010, "lea + store");
    // 4. push/pop through the shadow stack: rsp known -> push rbx; pop rcx gives rcx == rbx
    I.set_reg(OFFSET_amd64_RSP, 8, 0x7fff0000); I.set_reg(OFFSET_amd64_RBX, 8, 0xdeadbeef);
    acc.clear(); run(0x400020, {0x53}); run(0x400021, {0x59});
    CHECK(acc.size() == 2 && acc[0].op == 1 && acc[0].addr == 0x7ffefff8 && acc[1].op == 0 && acc[1].addr == 0x7ffefff8, "push/pop addresses");
    CHECK(I.get_reg(OFFSET_amd64_RCX, 8).known() && I.get_reg(OFFSET_amd64_RCX, 8).u64() == 0xdeadbeef, "stack slot forwarding (spill/reload)");
    CHECK(I.get_reg(OFFSET_amd64_RSP, 8).u64() == 0x7fff0000, "rsp restored");
    // 5. cmov with flags: test %rcx,%rcx ; mov $0x404880,%edx ; cmove %rdx,%rsi   (rcx=0 -> rsi=0x404880)
    I.set_reg(OFFSET_amd64_RCX, 8, 0); I.set_reg(OFFSET_amd64_RSI, 8, 0x1111);
    run(0x400030, {0x48, 0x85, 0xc9}); run(0x400033, {0xba, 0x80, 0x48, 0x40, 0x00}); run(0x400038, {0x48, 0x0f, 0x44, 0xf2});
    CHECK(I.get_reg(OFFSET_amd64_RSI, 8).known() && I.get_reg(OFFSET_amd64_RSI, 8).u64() == 0x404880, "cmove taken (rcx==0) via flag helper");
    I.set_reg(OFFSET_amd64_RCX, 8, 5); I.set_reg(OFFSET_amd64_RSI, 8, 0x1111);
    run(0x400030, {0x48, 0x85, 0xc9}); run(0x400038, {0x48, 0x0f, 0x44, 0xf2});
    CHECK(I.get_reg(OFFSET_amd64_RSI, 8).u64() == 0x1111, "cmove not taken (rcx!=0)");
    // 6. cmp/ja style: cmp $0x10,%rax ; cmovb %rdx,%rsi  (rax=3 -> below -> taken)
    I.set_reg(OFFSET_amd64_RAX, 8, 3); I.set_reg(OFFSET_amd64_RSI, 8, 0x1111);
    run(0x400040, {0x48, 0x83, 0xf8, 0x10}); run(0x400044, {0x48, 0x0f, 0x42, 0xf2});
    CHECK(I.get_reg(OFFSET_amd64_RSI, 8).u64() == 0x404880, "cmovb after cmp (unsigned below)");
    // 7. partial register: mov $0x12,%al keeps upper bytes of rax known
    I.set_reg(OFFSET_amd64_RAX, 8, 0x1122334455667788ull); run(0x400050, {0xb0, 0x12});
    CHECK(I.get_reg(OFFSET_amd64_RAX, 8).known() && I.get_reg(OFFSET_amd64_RAX, 8).u64() == 0x1122334455667712ull, "sub-register write keeps upper bytes");
    // 8. 32-bit write zero-extends: mov $1,%ecx
    I.set_reg_unknown(OFFSET_amd64_RCX, 8); run(0x400052, {0xb9, 0x01, 0x00, 0x00, 0x00});
    CHECK(I.get_reg(OFFSET_amd64_RCX, 8).known() && I.get_reg(OFFSET_amd64_RCX, 8).u64() == 1, "32-bit write zero-extends to 64");
    // 9. unknown base -> unknown address flagged
    I.set_reg_unknown(OFFSET_amd64_R8, 8); acc.clear(); run(0x400060, {0x49, 0x03, 0x00});   // add (%r8),%rax
    CHECK(acc.size() == 1 && !acc[0].known, "unknown base reported, not guessed");
    // 10. xmm round trip: movq %rdi,%xmm0 ; movq %xmm0,%rsi
    I.set_reg(OFFSET_amd64_RDI, 8, 0xabcdef); run(0x400070, {0x66, 0x48, 0x0f, 0x6e, 0xc7}); run(0x400075, {0x66, 0x48, 0x0f, 0x7e, 0xc6});
    CHECK(I.get_reg(OFFSET_amd64_RSI, 8).known() && I.get_reg(OFFSET_amd64_RSI, 8).u64() == 0xabcdef, "pointer round-trip through xmm");
    // 11. rip-relative store: mov %rax,0x2e8b(%rip) at 0x4011ce -> 0x404060
    acc.clear(); run(0x4011ce, {0x48, 0x89, 0x05, 0x8b, 0x2e, 0x00, 0x00});
    CHECK(acc.size() == 1 && acc[0].known && acc[0].addr == 0x404060 && acc[0].op == 1, "rip-relative store address");
    // 12. lock cmpxchg -> CAS emits rmw access
    I.set_reg(OFFSET_amd64_RDI, 8, 0x5000); acc.clear(); run(0x400080, {0xf0, 0x48, 0x0f, 0xb1, 0x1f});
    CHECK(acc.size() == 1 && acc[0].op == 2 && acc[0].addr == 0x5000, "lock cmpxchg emits rmw at rdi");
    // 13. `rep stosq' expanded one iteration at a time: the store address must ADVANCE, which it
    //     only does if guest_DFLAG is known.  With DFLAG unknown the first
    //     iteration is right and every later one is an unknown address.
    I.set_reg(OFFSET_amd64_RDI, 8, 0x6000); I.set_reg(OFFSET_amd64_RCX, 8, 4); I.set_reg(OFFSET_amd64_RAX, 8, 0);
    acc.clear();
    for (int it = 0; it < 4; it++) run(0x400090, {0xf3, 0x48, 0xab});
    CHECK(acc.size() == 4 && acc[0].known && acc[1].known && acc[2].known && acc[3].known &&
          acc[0].addr == 0x6000 && acc[1].addr == 0x6008 && acc[2].addr == 0x6010 && acc[3].addr == 0x6018,
          "rep stosq advances %rdi across iterations (DFLAG)");
    // ... and it must still advance after a state loss that re-seeds the registers.
    I.all_unknown();
    I.set_reg(OFFSET_amd64_RDI, 8, 0x7000); I.set_reg(OFFSET_amd64_RCX, 8, 2); I.set_reg(OFFSET_amd64_RAX, 8, 0);
    acc.clear();
    for (int it = 0; it < 2; it++) run(0x400090, {0xf3, 0x48, 0xab});
    CHECK(acc.size() == 2 && acc[1].known && acc[1].addr == 0x7008, "DFLAG survives all_unknown()");
    printf("%s (%d failures)\n", fails ? "VEXTEST FAILED" : "VEXTEST PASSED", fails);
    return fails != 0;
}
