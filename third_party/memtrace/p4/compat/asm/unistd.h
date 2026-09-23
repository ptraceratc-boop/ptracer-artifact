/* Pin 4.4 compatibility shim (2026-09-22, baselines_slice.md).
 * Pin 4.4's pinrt adaptor include path does not expose the kernel UAPI
 * headers that Pin 3.20's CRT did.  memtrace.cpp needs exactly one constant
 * from each; the x86-64 values are ABI-stable.  The TOOL SOURCE IS UNCHANGED. */
#ifndef PTRACER_COMPAT_ASM_UNISTD_H
#define PTRACER_COMPAT_ASM_UNISTD_H
#ifndef __NR_futex
#define __NR_futex 202          /* x86-64 */
#endif
#endif
