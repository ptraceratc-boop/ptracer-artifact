/* no_clone3: install a seccomp filter that makes the clone3 syscall return
 * ENOSYS, then exec the given command. glibc (>=2.34) tries clone3 for
 * pthread_create and, on ENOSYS, transparently falls back to the legacy clone
 * syscall -- which Pin 3.20 instruments correctly (clone3 it does not).
 * The filter is inherited across exec/fork, so it covers `pin` and the traced
 * JVM/threaded target. Non-root safe (PR_SET_NO_NEW_PRIVS). See README.
 *
 * Usage: no_clone3 <command> [args...]
 *   e.g. no_clone3 $PIN -ifeellucky -t memtrace.so -- java -jar renaissance.jar ...
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <linux/unistd.h>

#ifndef __NR_clone3
#define __NR_clone3 435
#endif

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: no_clone3 <cmd> [args...]\n"); return 2; }
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),          /* other arch: allow */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone3, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = { .len = sizeof(filter)/sizeof(filter[0]), .filter = filter };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) { perror("PR_SET_NO_NEW_PRIVS"); return 2; }
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog)) { perror("seccomp"); return 2; }
    execvp(argv[1], &argv[1]);
    perror("execvp"); return 127;
}
