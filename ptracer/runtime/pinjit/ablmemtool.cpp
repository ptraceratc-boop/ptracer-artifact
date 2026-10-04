/*
 * ablmemtool.cpp -- Figure 6 "PTracer w/o PT and static analysis" for the JIT suites (Node.js, Java).
 *
 * Without the runtimes' code-event APIs (the JVMTI / V8 hooks and JIT trampolines PTracer uses), a tracer that logs
 * every memory address can reach code generated at run time only by dynamic binary translation -- what memorytracer
 * does.  So the whole process runs under Pin, and this tool emits the SAME record stream as the native arm
 * (rewrite.py --log-blocks + analyze.py --all-memops): one 8-byte block identifier (the block's address) per executed
 * basic block, then one 8-byte effective address per memory operand, in program order.  Records go through Pin's
 * buffering API (memorytracer's mechanism); a full buffer is written to /dev/null (-out), as the native arms' drain
 * does with PTLOG_DIR=/dev/null.  No static analysis, no selection, no plan.
 *
 * Knobs: -pages N (trace-buffer pages per thread, default 8192 = 32 MB), -out FILE (default /dev/null), -stats 1.
 */
#include "pin.H"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

KNOB<UINT32> KnobPages(KNOB_MODE_WRITEONCE, "pintool", "pages", "8192", "trace-buffer pages per thread");
KNOB<std::string> KnobOut(KNOB_MODE_WRITEONCE, "pintool", "out", "/dev/null", "where full buffers are written");
KNOB<BOOL> KnobStats(KNOB_MODE_WRITEONCE, "pintool", "stats", "0", "print the record count at exit");

static BUFFER_ID g_buf;
static int g_fd = -1;
static volatile UINT64 g_recs = 0;   // statistics only (updated per full buffer, racy by design)

static VOID *BufferFull(BUFFER_ID, THREADID, const CONTEXT *, VOID *buf, UINT64 n, VOID *)
{
    const char *p = (const char *)buf;
    size_t left = (size_t)n * 8;
    while (left > 0) {
        ssize_t w = write(g_fd, p, left);
        if (w <= 0) break;
        p += w; left -= (size_t)w;
    }
    g_recs += n;
    return buf;
}

static VOID Trace(TRACE trace, VOID *)
{
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        INS head = BBL_InsHead(bbl);
        INS_InsertFillBuffer(head, IPOINT_BEFORE, g_buf, IARG_ADDRINT, INS_Address(head), 0, IARG_END);
        for (INS ins = head; INS_Valid(ins); ins = INS_Next(ins)) {
            UINT32 n = INS_MemoryOperandCount(ins);
            for (UINT32 i = 0; i < n; i++)
                INS_InsertFillBufferPredicated(ins, IPOINT_BEFORE, g_buf, IARG_MEMORYOP_EA, i, 0, IARG_END);
        }
    }
}

static VOID Fini(INT32, VOID *)
{
    if (KnobStats.Value())
        fprintf(stderr, "[ablmemtool] pid %d: %llu records in full buffers\n", (int)getpid(),
                (unsigned long long)g_recs);
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) {
        fprintf(stderr, "ablmemtool: usage: pin -t ablmemtool.so [-pages N] [-out FILE] [-stats 1] -- prog\n");
        return 1;
    }
    g_fd = open(KnobOut.Value().c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (g_fd < 0) { fprintf(stderr, "ablmemtool: cannot open %s\n", KnobOut.Value().c_str()); return 1; }
    g_buf = PIN_DefineTraceBuffer(8, KnobPages.Value(), BufferFull, 0);
    if (g_buf == BUFFER_ID_INVALID) { fprintf(stderr, "ablmemtool: PIN_DefineTraceBuffer failed\n"); return 1; }
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
    return 0;
}
