/* nooptool.cpp -- the Pin JIT floor.
 *
 * A Pintool that registers NO instrumentation callbacks at all.  Running a
 * program under `pin -t nooptool.so` therefore measures exactly the cost of
 * Pin's dynamic binary translation: every basic block is decoded, recompiled
 * into the code cache, linked, and executed from there, with no analysis
 * routine ever inserted.  This is the lower bound on any JIT-mode Pintool,
 * the HiFi tool included.
 */
#include "pin.H"

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) {
        PIN_ERROR("nooptool: usage: pin -t nooptool.so -- <program>\n");
        return 1;
    }
    PIN_StartProgram();   /* never returns */
    return 0;
}
