/* t_fpchain's blocks, compiled once for ARM and once for Thumb (-DTHUMB).
   Copyright 2026 Andrew Youll. MIT licence. */
#include <stdint.h>
/* LONG: t_fpchain_long, a few blocks of 450 instructions, run without
   the warm-up below, so they are first translated with the inexact flag
   clear (slow paths inline): long enough for TCG to run out of temps
   partway through one and translate it again in smaller pieces. */
#ifdef LONG
#include "t_fpchain_long_blocks.h"
#else
#include "t_fpchain_blocks.h"
#endif
#ifdef THUMB
#define IT_(c) "it " #c "\n\t"
#define NAME(n) thumb_blk##n
#define TABLE thumb_blocks
#else
#define IT_(c)
#define NAME(n) arm_blk##n
#define TABLE arm_blocks
#endif
#define CLOBBERS "r12", "cc", "memory", \
  "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "d10", "d11", \
  "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21", \
  "d22", "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"
#define FN(n)                                                           \
  static void NAME(n) (uint64_t *io)                                    \
  {                                                                     \
    __asm__ volatile ("vldmia %0, {d0-d15}\n\t"                         \
                      "add r12, %0, #128\n\t"                           \
                      "vldmia r12, {d16-d31}\n\t"                       \
                      BLOCK##n                                          \
                      "vstmia %0, {d0-d15}\n\t"                         \
                      "add r12, %0, #128\n\t"                           \
                      "vstmia r12, {d16-d31}\n\t"                       \
                      : : "r" (io) : CLOBBERS);                         \
  }
BLOCKS(FN)
#define ENTRY(n) NAME(n),
void (*const TABLE[NBLOCKS]) (uint64_t *) = { BLOCKS(ENTRY) };
