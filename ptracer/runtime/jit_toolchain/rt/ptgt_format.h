#ifndef PTGT_FORMAT_H
#define PTGT_FORMAT_H

/* PTGT v2 keeps 16-byte {addr, ip} slots. A completed 64-bit-address REP STOS
 * occupies five slots: {rdi, BASE}, {rcx, COUNT}, {rflags, FLAGS}, {width, ip},
 * then, AFTER the instruction, {rcx, COMMIT}. The last rcx must be zero.
 * Reserved negative IPs cannot name Linux user-mode executable instructions.
 * Ordinary records retain their v1 layout. Interrupted/interleaved/incomplete
 * descriptors are rejected, never expanded from a partial record. */
#define PTGT_VERSION 2u
#define PTGT_REP_BASE   (-1)
#define PTGT_REP_COUNT  (-2)
#define PTGT_REP_FLAGS  (-3)
#define PTGT_REP_COMMIT (-4)

#endif
