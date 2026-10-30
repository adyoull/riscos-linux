/* t_fpchain: long straight-line blocks of VFP and NEON floating point
   instructions (generated: t_fpchain_blocks.h, from fpchain_gen.py), whose results
   feed each other through the shared registers (singles in doubles in
   quads), with compares feeding conditional instructions (IT blocks in the
   Thumb build), run in ARM and Thumb on many register sets in 16 FPSCR
   modes. Every register and FPSCR after each run goes into a CRC: QEMU's
   exact soft float and its inline host floating point must agree (each
   block is first translated with the inexact flag set, then run in every
   mode, so its fast paths' gates fail as well as pass). It
   covers what t_fpconform, one instruction at a time, can't: translation
   blocks with many inline operations.

     t_fpchain        a CRC per block (ARM, Thumb), then of all of them

   Copyright 2026 Andrew Youll. MIT licence. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
/* LONG: t_fpchain_long, a few blocks of 450 instructions, run without
   the warm-up below, so they are first translated with the inexact flag
   clear (slow paths inline): long enough for TCG to run out of temps
   partway through one and translate it again in smaller pieces. */
#ifdef LONG
#include "t_fpchain_long_blocks.h"
#else
#include "t_fpchain_blocks.h"
#endif

extern void (*const arm_blocks[NBLOCKS]) (uint64_t *);
extern void (*const thumb_blocks[NBLOCKS]) (uint64_t *);

static const uint32_t modes[16] = {
  0x00000000, 0x01000000, 0x02000000, 0x03000000,
  0x00400000, 0x00800000, 0x00c00000, 0x01c00000,
  0x00000010, 0x01000010, 0x02000010, 0x03000010,
  0x00400010, 0x00800010, 0x00c00010, 0x03c00010,
};

static inline void set_fpscr (uint32_t v)
{ __asm__ volatile ("vmsr fpscr, %0" : : "r" (v) : "memory"); }
static inline uint32_t get_fpscr (void)
{ uint32_t v; __asm__ volatile ("vmrs %0, fpscr" : "=r" (v) : : "memory"); return v; }

static uint32_t crc_tab[256];
static uint32_t crc_word (uint32_t c, uint32_t w)
{
  for (int k = 0; k < 4; k++, w >>= 8)
    c = crc_tab[(c ^ w) & 0xff] ^ (c >> 8);
  return c;
}

static uint32_t rs = 0x9e3779b9;
static uint32_t rnd (void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static const uint32_t fsp[] = {
  0x00000000, 0x80000000, 0x00000001, 0x807fffff, 0x00800000, 0x80800000,
  0x7f7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0x7fa00001, 0x3f800000,
  0x4b800000, 0x4f000000, 0xcf000000, 0x01000000, 0x0d000000, 0x72000000,
};
static const uint64_t dsp[] = {
  0, 0x8000000000000000ull, 1, 0x0010000000000000ull, 0x7fefffffffffffffull,
  0x7ff0000000000000ull, 0x7ff8000000000000ull, 0x7ff4000000000001ull,
  0x3ff0000000000000ull, 0x41e0000000000000ull, 0xc1e0000000200000ull,
  0x47efffffe0000000ull, 0x36a0000000000000ull, 0x2000000000000000ull,
};

static uint32_t fval (void)
{
  uint32_t k = rnd () % 100;
  union { float f; uint32_t u; } v;
  if (k < 10)
    return fsp[rnd () % (sizeof fsp / 4)];
  if (k < 25)
    return (uint32_t) (int32_t) ((int32_t) (rnd () % 2001) - 1000);   /* as an int */
  if (k < 35)
    v.f = (float) ((int32_t) (rnd () % 201) - 100);                     /* exact */
  else
    v.f = ((float) (int32_t) (rnd () % 2000001) - 1000000) / (float) (1 + rnd () % 1000);
  return v.u;
}

static uint64_t dval (void)
{
  uint32_t k = rnd () % 100;
  union { double f; uint64_t u; } v;
  if (k < 10)
    return dsp[rnd () % (sizeof dsp / 8)];
  if (k < 50)                   /* two singles */
    return (uint64_t) fval () << 32 | fval ();
  if (k < 60)
    v.f = (double) ((int32_t) (rnd () % 201) - 100);
  else
    v.f = ((double) (int32_t) (rnd () % 2000001) - 1000000) / (double) (1 + rnd () % 1000);
  return v.u;
}

#define NSETS 24

int main (void)
{
  for (uint32_t i = 0; i < 256; i++)
    {
      uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = c & 1 ? 0xedb88320 ^ (c >> 1) : c >> 1;
      crc_tab[i] = c;
    }
  /* Translate the blocks first with the inexact flag set (the inline code
     then runs straight through, its gates generated once), in both FZ
     settings; the runs below with it clear then fail those gates. */
#ifndef LONG
  for (int t = 0; t < 2; t++)
    for (int b = 0; b < NBLOCKS; b++)
      for (int m = 8; m < 10; m++)
        {
          uint64_t io[32];
          for (int r = 0; r < 32; r++)
            io[r] = 0x3ff0000000000000ull + r;
          set_fpscr (modes[m]);
          (t ? thumb_blocks : arm_blocks)[b] (io);
        }
#endif
  uint32_t all = 0xffffffff;
  for (int t = 0; t < 2; t++)
    for (int b = 0; b < NBLOCKS; b++)
      {
        uint32_t crc = 0xffffffff;
        rs = 0x9e3779b9 + 977 * b;
        for (int m = 0; m < 16; m++)
          for (int n = 0; n < NSETS; n++)
            {
              uint64_t io[32];
              for (int r = 0; r < 32; r++)
                io[r] = dval ();
              set_fpscr (modes[m]);
              (t ? thumb_blocks : arm_blocks)[b] (io);
              uint32_t f = get_fpscr ();
              for (int r = 0; r < 32; r++)
                crc = crc_word (crc_word (crc, (uint32_t) io[r]), (uint32_t) (io[r] >> 32));
              crc = crc_word (crc, f);
            }
        set_fpscr (0);
        printf ("%s block %2d %08x\n", t ? "thumb" : "arm  ", b, ~crc);
        all = crc_word (all, ~crc);
      }
  printf ("t_fpchain: %d blocks x 2 x 16 modes x %d sets, CRC %08x\n", NBLOCKS, NSETS, ~all);
  return 0;
}
