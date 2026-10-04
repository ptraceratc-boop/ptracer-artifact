/* ptrace_any.c -- exec wrapper: allow any process to ptrace()/attach to me.
 *
 * This container runs with kernel.yama.ptrace_scope = 1 ("restricted ptrace"),
 * under which only an ANCESTOR may ptrace a process -- so `pin -pid PID' from a
 * sibling shell fails with EPERM before it can inject anything.  The target
 * declaring PR_SET_PTRACER_ANY is the sanctioned way out and needs no
 * privilege, no global sysctl change and no cooperation from the program
 * itself.  (The alternative, `sudo sysctl -w kernel.yama.ptrace_scope=0', is a
 * machine-wide change; this wrapper is per-process.)
 *
 *   gcc -O2 -o ptrace_any ptrace_any.c
 *   ./ptrace_any ./gemm_loop 100000
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/prctl.h>
#include <unistd.h>
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif
#ifndef PR_SET_PTRACER_ANY
#define PR_SET_PTRACER_ANY ((unsigned long)-1)
#endif
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: ptrace_any CMD [ARGS...]\n"); return 2; }
    if (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0))
        perror("prctl(PR_SET_PTRACER_ANY)");   /* not fatal: scope may be 0 */
    execvp(argv[1], argv + 1);
    perror("execvp");
    return 127;
}
