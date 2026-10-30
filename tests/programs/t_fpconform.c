/* t_fpconform: VFP and NEON floating point, special cases and all, checked
   by hashing every result and the FPSCR flags it leaves.

   Each instruction is run on every combination of a set of values (zeros,
   denormals, FLT_MIN/MAX, infinities, quiet and signalling NaNs with
   payloads, integer boundaries, values whose product or sum underflows or
   overflows, and random values) under 16 FPSCR settings: round to nearest /
   zero / +inf / -inf, flush-to-zero, default NaN, and with the inexact flag
   already set or not (emulators take different paths for each). FPSCR is
   written before and read after every instruction, so each result's flags
   are its own.

   The output is one line per instruction: its name, the number of results
   and a CRC of them (bit patterns and flags). Two runs that should be the
   same (QEMU's exact emulation and its host FPU fast path, or this and real
   hardware) must give the same lines: diff them.

     t_fpconform               all the lines, then a CRC of all of them
     t_fpconform NAME MODE     every result of one instruction in one mode
                               (MODE 0-15), to find what differs

   Copyright 2026 Andrew Youll. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arm_neon.h>

/* ---- values ------------------------------------------------------- */

static const uint32_t sp_special[] = {
  0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff,
  0x00400000, 0x00800000, 0x80800000, 0x00800001, 0x01000000, 0x0c000000,
  0x1f800000, 0x20000000, 0x33800000, 0x34000000, 0x3eaaaaab, 0x3f000000,
  0x3f7fffff, 0x3f800000, 0xbf800000, 0x3f800001, 0x3fc00000, 0x3fffffff,
  0x40000000, 0x40400000, 0x4b000000, 0x4b7fffff, 0x4effffff, 0x4f000000,
  0xcf000000, 0xcf000001, 0x4f800000, 0x5f000000, 0x5f800000, 0x7e800000,
  0x7f000000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000,
  0x7fc12345, 0xffc00001, 0x7f800001, 0x7fa00000, 0xff812345, 0x3dcccccd,
  /* as integers: either side of 2^24 (exact as single), INT_MAX, -1 */
  0x00ffffff, 0x01000001, 0xff000000, 0xfeffffff, 0x7fffffff, 0xffffffff,
};
static const uint64_t dp_special[] = {
  0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000001ull,
  0x8000000000000001ull, 0x000fffffffffffffull, 0x800fffffffffffffull,
  0x0008000000000000ull, 0x0010000000000000ull, 0x8010000000000000ull,
  0x0010000000000001ull, 0x0020000000000000ull, 0x1ff0000000000000ull,
  0x2000000000000000ull, 0x3ca0000000000000ull, 0x3cb0000000000000ull,
  0x3fd5555555555555ull, 0x3fe0000000000000ull, 0x3fefffffffffffffull,
  0x3ff0000000000000ull, 0xbff0000000000000ull, 0x3ff0000000000001ull,
  0x3ff8000000000000ull, 0x3fffffffffffffffull, 0x4000000000000000ull,
  0x4008000000000000ull, 0x4330000000000000ull, 0x41dfffffffc00000ull,
  0x41e0000000000000ull, 0xc1e0000000000000ull, 0xc1e0000000200000ull,
  0x41f0000000000000ull, 0x43e0000000000000ull, 0x7fd0000000000000ull,
  0x7fe0000000000000ull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
  0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000000ull,
  0x7ff8000012345678ull, 0xfff8000000000001ull, 0x7ff0000000000001ull,
  0x7ff4000000000000ull, 0xfff0000123456789ull, 0x3fb999999999999aull,
  0x47efffffe0000000ull, 0x36a0000000000000ull, 0x3810000000000000ull,
  0x41efffffffe00000ull, 0xbfe0000000000000ull,   /* 2^32 - 1, -0.5 */
};

#define NSP_RAND 48
#define NSP (sizeof sp_special / 4 + NSP_RAND)
#define NDP (sizeof dp_special / 8 + NSP_RAND)
#define NTRI 24                         /* values for 3-operand tests */

static uint32_t sv[NSP];
static uint64_t dv[NDP];
static uint32_t sv3[NTRI];
static uint64_t dv3[NTRI];

typedef union { float f; uint32_t u; } f32u;
typedef union { double f; uint64_t u; } f64u;

static uint32_t rs = 0x2545F491;
static uint32_t
rnd (void)
{
  rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5;
  return rs;
}

static void
make_values (void)
{
  size_t n = sizeof sp_special / 4, m = sizeof dp_special / 8;
  memcpy (sv, sp_special, sizeof sp_special);
  memcpy (dv, dp_special, sizeof dp_special);
  for (int i = 0; i < NSP_RAND; i++)
    {
      uint32_t r = rnd ();
      uint64_t q = (uint64_t) rnd () << 32 | rnd ();
      if (i % 3 == 1)   /* ordinary numbers, exponents near 1 */
        {
          r = (r & 0x807fffff) | ((uint32_t) (120 + (rnd () % 16)) << 23);
          q = (q & 0x800fffffffffffffull) | ((uint64_t) (1016 + (rnd () % 16)) << 52);
        }
      else if (i % 3 == 2)      /* short mantissas: many exact results */
        {
          int k = (int) (rnd () % 1024) - 512;
          f32u x = {.f = (float) k * (float) (1 << (rnd () % 12)) / 64.0f};
          f64u y = {.f = (double) k * (double) (1 << (rnd () % 20)) / 4096.0};
          r = x.u, q = y.u;
        }
      sv[n + i] = r;
      dv[m + i] = q;
    }
  /* a spread of the interesting ones for the three-operand tests */
  static const int pick[NTRI] = { 0, 1, 2, 5, 7, 9, 13, 16, 19, 20, 21, 23,
    33, 37, 39, 40, 41, 43, 44, 47, 49, 52, 55, 60 };
  for (int i = 0; i < NTRI; i++)
    sv3[i] = sv[pick[i] % NSP], dv3[i] = dv[pick[i] % NDP];
}

/* ---- FPSCR -------------------------------------------------------- */

static const uint32_t modes[16] = {
  0x00000000,                   /* round to nearest */
  0x01000000,                   /* FZ */
  0x02000000,                   /* DN */
  0x03000000,                   /* FZ DN ("RunFast") */
  0x00400000,                   /* round to +inf */
  0x00800000,                   /* round to -inf */
  0x00c00000,                   /* round to zero */
  0x01c00000,                   /* FZ, round to zero */
  0x00000010, 0x01000010, 0x02000010, 0x03000010,     /* the same, IXC set */
  0x00400010, 0x00800010, 0x00c00010, 0x03c00010,
};

static inline void
set_fpscr (uint32_t v)
{
  __asm__ volatile ("vmsr fpscr, %0" : : "r" (v) : "memory");
}

static inline uint32_t
get_fpscr (void)
{
  uint32_t v;
  __asm__ volatile ("vmrs %0, fpscr" : "=r" (v) : : "memory");
  return v;
}

/* ---- CRC ---------------------------------------------------------- */

static uint32_t crc_tab[256];
static void
crc_init (void)
{
  for (uint32_t i = 0; i < 256; i++)
    {
      uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = c & 1 ? 0xedb88320 ^ (c >> 1) : c >> 1;
      crc_tab[i] = c;
    }
}

static uint32_t
crc_word (uint32_t c, uint32_t w)
{
  for (int k = 0; k < 4; k++, w >>= 8)
    c = crc_tab[(c ^ w) & 0xff] ^ (c >> 8);
  return c;
}

/* ---- the instructions --------------------------------------------- */


/* scalar single: r = op(a, b, c); c is the accumulator for VMLA etc. */
#define S2(NAME, INSN)                                                  \
  static uint32_t NAME (uint32_t a, uint32_t b, uint32_t c)             \
  { f32u x = {.u = a}, y = {.u = b}, z; (void) c;                        \
    __asm__ volatile (INSN " %0, %1, %2" : "=t" (z.f) : "t" (x.f), "t" (y.f)); \
    return z.u; }
#define S3(NAME, INSN)                                                  \
  static uint32_t NAME (uint32_t a, uint32_t b, uint32_t c)             \
  { f32u x = {.u = a}, y = {.u = b}, z = {.u = c};                       \
    __asm__ volatile (INSN " %0, %1, %2" : "+t" (z.f) : "t" (x.f), "t" (y.f)); \
    return z.u; }
#define S1(NAME, INSN)                                                  \
  static uint32_t NAME (uint32_t a, uint32_t b, uint32_t c)             \
  { f32u x = {.u = a}, z; (void) b; (void) c;                            \
    __asm__ volatile (INSN " %0, %1" : "=t" (z.f) : "t" (x.f));          \
    return z.u; }
#define SCMP(NAME, INSN)                                                \
  static uint32_t NAME (uint32_t a, uint32_t b, uint32_t c)             \
  { f32u x = {.u = a}, y = {.u = b}; (void) c;                           \
    uint32_t apsr;                                                      \
    __asm__ volatile (INSN " %1, %2\n\tvmrs APSR_nzcv, fpscr\n\t"        \
                      "mrs %0, APSR" : "=r" (apsr) : "t" (x.f), "t" (y.f) : "cc"); \
    return apsr & 0xf0000000; }

S2 (s_add, "vadd.f32") S2 (s_sub, "vsub.f32") S2 (s_mul, "vmul.f32")
S2 (s_nmul, "vnmul.f32") S2 (s_div, "vdiv.f32")
S3 (s_mla, "vmla.f32") S3 (s_mls, "vmls.f32") S3 (s_nmla, "vnmla.f32")
S3 (s_nmls, "vnmls.f32") S3 (s_fma, "vfma.f32") S3 (s_fms, "vfms.f32")
S3 (s_fnma, "vfnma.f32") S3 (s_fnms, "vfnms.f32")
S1 (s_sqrt, "vsqrt.f32") S1 (s_abs, "vabs.f32") S1 (s_neg, "vneg.f32")
S1 (s_tosi, "vcvt.s32.f32") S1 (s_tosir, "vcvtr.s32.f32")
S1 (s_toui, "vcvt.u32.f32") S1 (s_touir, "vcvtr.u32.f32")
S1 (s_sito, "vcvt.f32.s32") S1 (s_uito, "vcvt.f32.u32")
SCMP (s_cmp, "vcmp.f32") SCMP (s_cmpe, "vcmpe.f32")

static uint32_t
s_cmpz (uint32_t a, uint32_t b, uint32_t c)     /* compare with #0 */
{
  f32u x = {.u = a};
  uint32_t apsr;
  (void) b, (void) c;
  __asm__ volatile ("vcmpe.f32 %1, #0\n\tvmrs APSR_nzcv, fpscr\n\t"
                    "mrs %0, APSR" : "=r" (apsr) : "t" (x.f) : "cc");
  return apsr & 0xf0000000;
}

static uint32_t
s_tod (uint32_t a, uint32_t b, uint32_t c)      /* f32 -> f64: two words */
{
  f32u x = {.u = a};
  f64u z;
  (void) b, (void) c;
  __asm__ volatile ("vcvt.f64.f32 %P0, %1" : "=w" (z.f) : "t" (x.f));
  return (uint32_t) z.u ^ (uint32_t) (z.u >> 32) * 0x9e3779b9u;
}

static uint32_t
s_fix (uint32_t a, uint32_t b, uint32_t c)      /* f32 -> s32 fixed point, 16 bits */
{
  f32u z = {.u = a};
  (void) b, (void) c;
  __asm__ volatile ("vcvt.s32.f32 %0, %0, #16" : "+t" (z.f));
  return z.u;
}

/* scalar double: results folded to 32 bits for the CRC (high word mixed in) */
#define FOLD(q) ((uint32_t) (q) ^ (uint32_t) ((q) >> 32) * 0x9e3779b9u)
#define D2(NAME, INSN)                                                  \
  static uint32_t NAME (uint64_t a, uint64_t b, uint64_t c)             \
  { f64u x = {.u = a}, y = {.u = b}, z; (void) c;                        \
    __asm__ volatile (INSN " %P0, %P1, %P2" : "=w" (z.f) : "w" (x.f), "w" (y.f)); \
    return FOLD (z.u); }
#define D3(NAME, INSN)                                                  \
  static uint32_t NAME (uint64_t a, uint64_t b, uint64_t c)             \
  { f64u x = {.u = a}, y = {.u = b}, z = {.u = c};                       \
    __asm__ volatile (INSN " %P0, %P1, %P2" : "+w" (z.f) : "w" (x.f), "w" (y.f)); \
    return FOLD (z.u); }
#define D1(NAME, INSN)                                                  \
  static uint32_t NAME (uint64_t a, uint64_t b, uint64_t c)             \
  { f64u x = {.u = a}, z; (void) b; (void) c;                            \
    __asm__ volatile (INSN " %P0, %P1" : "=w" (z.f) : "w" (x.f));        \
    return FOLD (z.u); }
#define DCMP(NAME, INSN)                                                \
  static uint32_t NAME (uint64_t a, uint64_t b, uint64_t c)             \
  { f64u x = {.u = a}, y = {.u = b}; (void) c;                           \
    uint32_t apsr;                                                      \
    __asm__ volatile (INSN " %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\t"      \
                      "mrs %0, APSR" : "=r" (apsr) : "w" (x.f), "w" (y.f) : "cc"); \
    return apsr & 0xf0000000; }
/* double -> single or integer: the result is in an S register */
#define D1S(NAME, INSN)                                                 \
  static uint32_t NAME (uint64_t a, uint64_t b, uint64_t c)             \
  { f64u x = {.u = a}; f32u z; (void) b; (void) c;                        \
    __asm__ volatile (INSN " %0, %P1" : "=t" (z.f) : "w" (x.f));         \
    return z.u; }

D2 (d_add, "vadd.f64") D2 (d_sub, "vsub.f64") D2 (d_mul, "vmul.f64")
D2 (d_nmul, "vnmul.f64") D2 (d_div, "vdiv.f64")
D3 (d_mla, "vmla.f64") D3 (d_mls, "vmls.f64") D3 (d_nmla, "vnmla.f64")
D3 (d_nmls, "vnmls.f64") D3 (d_fma, "vfma.f64") D3 (d_fms, "vfms.f64")
D3 (d_fnma, "vfnma.f64") D3 (d_fnms, "vfnms.f64")
D1 (d_sqrt, "vsqrt.f64") D1 (d_abs, "vabs.f64") D1 (d_neg, "vneg.f64")
D1S (d_tos, "vcvt.f32.f64") D1S (d_tosi, "vcvt.s32.f64") D1S (d_tosir, "vcvtr.s32.f64")
D1S (d_toui, "vcvt.u32.f64") D1S (d_touir, "vcvtr.u32.f64")
DCMP (d_cmp, "vcmp.f64") DCMP (d_cmpe, "vcmpe.f64")

static uint32_t
d_sito (uint64_t a, uint64_t b, uint64_t c)
{
  f32u x = {.u = (uint32_t) a};
  f64u z;
  (void) b, (void) c;
  __asm__ volatile ("vcvt.f64.s32 %P0, %1" : "=w" (z.f) : "t" (x.f));
  return FOLD (z.u);
}

static uint32_t
d_uito (uint64_t a, uint64_t b, uint64_t c)
{
  f32u x = {.u = (uint32_t) a};
  f64u z;
  (void) b, (void) c;
  __asm__ volatile ("vcvt.f64.u32 %P0, %1" : "=w" (z.f) : "t" (x.f));
  return FOLD (z.u);
}

static uint32_t
d_cmpz (uint64_t a, uint64_t b, uint64_t c)
{
  f64u x = {.u = a};
  uint32_t apsr;
  (void) b, (void) c;
  __asm__ volatile ("vcmp.f64 %P1, #0\n\tvmrs APSR_nzcv, fpscr\n\t"
                    "mrs %0, APSR" : "=r" (apsr) : "w" (x.f) : "cc");
  return apsr & 0xf0000000;
}

/* NEON, four lanes: a, b, c are four values each */
#define Q2(NAME, INSN)                                                  \
  static void NAME (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r) \
  { uint32x4_t x = vld1q_u32 (a), y = vld1q_u32 (b), z; (void) c;        \
    __asm__ volatile (INSN " %q0, %q1, %q2" : "=w" (z) : "w" (x), "w" (y)); \
    vst1q_u32 (r, z); }
#define Q3(NAME, INSN)                                                  \
  static void NAME (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r) \
  { uint32x4_t x = vld1q_u32 (a), y = vld1q_u32 (b), z = vld1q_u32 (c);  \
    __asm__ volatile (INSN " %q0, %q1, %q2" : "+w" (z) : "w" (x), "w" (y)); \
    vst1q_u32 (r, z); }
#define Q1(NAME, INSN)                                                  \
  static void NAME (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r) \
  { uint32x4_t x = vld1q_u32 (a), z; (void) b; (void) c;                 \
    __asm__ volatile (INSN " %q0, %q1" : "=w" (z) : "w" (x));            \
    vst1q_u32 (r, z); }

Q2 (q_add, "vadd.f32") Q2 (q_sub, "vsub.f32") Q2 (q_mul, "vmul.f32")
Q2 (q_abd, "vabd.f32") Q2 (q_max, "vmax.f32") Q2 (q_min, "vmin.f32")
Q2 (q_ceq, "vceq.f32") Q2 (q_cge, "vcge.f32") Q2 (q_cgt, "vcgt.f32")
Q2 (q_acge, "vacge.f32") Q2 (q_recps, "vrecps.f32") Q2 (q_rsqrts, "vrsqrts.f32")
Q3 (q_mla, "vmla.f32") Q3 (q_mls, "vmls.f32") Q3 (q_fma, "vfma.f32")
Q3 (q_fms, "vfms.f32")
Q1 (q_abs, "vabs.f32") Q1 (q_neg, "vneg.f32") Q1 (q_recpe, "vrecpe.f32")
Q1 (q_rsqrte, "vrsqrte.f32") Q1 (q_tosi, "vcvt.s32.f32") Q1 (q_toui, "vcvt.u32.f32")
Q1 (q_sito, "vcvt.f32.s32") Q1 (q_uito, "vcvt.f32.u32")
Q2 (q_acgt, "vacgt.f32")
#define Q1Z(NAME, INSN)                                                 \
  static void NAME (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r) \
  { uint32x4_t x = vld1q_u32 (a), z; (void) b; (void) c;                 \
    __asm__ volatile (INSN " %q0, %q1, #0" : "=w" (z) : "w" (x));        \
    vst1q_u32 (r, z); }
Q1Z (q_ceq0, "vceq.f32") Q1Z (q_cge0, "vcge.f32") Q1Z (q_cgt0, "vcgt.f32")
Q1Z (q_cle0, "vcle.f32") Q1Z (q_clt0, "vclt.f32")
/* 64-bit (D register) forms: the same instruction on each half */
#define D2H(NAME, INSN)                                                 \
  static void NAME (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r) \
  { uint32x4_t x = vld1q_u32 (a), y = vld1q_u32 (b), z; (void) c;        \
    __asm__ volatile (INSN " %e0, %e1, %e2\n\t" INSN " %f0, %f1, %f2"     \
                      : "=&w" (z) : "w" (x), "w" (y));                   \
    vst1q_u32 (r, z); }
#define D3H(NAME, INSN)                                                 \
  static void NAME (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r) \
  { uint32x4_t x = vld1q_u32 (a), y = vld1q_u32 (b), z = vld1q_u32 (c);  \
    __asm__ volatile (INSN " %e0, %e1, %e2\n\t" INSN " %f0, %f1, %f2"     \
                      : "+w" (z) : "w" (x), "w" (y));                    \
    vst1q_u32 (r, z); }
#define D1H(NAME, INSN)                                                 \
  static void NAME (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r) \
  { uint32x4_t x = vld1q_u32 (a), z; (void) b; (void) c;                 \
    __asm__ volatile (INSN " %e0, %e1\n\t" INSN " %f0, %f1"               \
                      : "=&w" (z) : "w" (x));                            \
    vst1q_u32 (r, z); }
D2H (h_add, "vadd.f32") D2H (h_mul, "vmul.f32") D2H (h_max, "vmax.f32")
D2H (h_cge, "vcge.f32") D3H (h_mla, "vmla.f32") D3H (h_mls, "vmls.f32")
D1H (h_tosi, "vcvt.s32.f32") D1H (h_sito, "vcvt.f32.s32")

static void
q_padd (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r)
{
  uint32x2_t x = vld1_u32 (a), y = vld1_u32 (b), z;
  (void) c;
  __asm__ volatile ("vpadd.f32 %P0, %P1, %P2" : "=w" (z) : "w" (x), "w" (y));
  vst1_u32 (r, z);
  r[2] = r[3] = 0;
}

static void
q_mlalane (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r)
{
  uint32x4_t x = vld1q_u32 (a), z = vld1q_u32 (c);
  uint32x2_t y = vld1_u32 (b);
  /* by-scalar multiply-accumulate: the scalar must be in d0-d15 */
  __asm__ volatile ("vmla.f32 %q0, %q1, %P2[1]" : "+w" (z) : "w" (x), "x" (y));
  vst1q_u32 (r, z);
}

static void
q_mullane (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r)
{
  uint32x4_t x = vld1q_u32 (a), z;
  uint32x2_t y = vld1_u32 (b);
  (void) c;
  __asm__ volatile ("vmul.f32 %q0, %q1, %P2[0]" : "=w" (z) : "w" (x), "x" (y));
  vst1q_u32 (r, z);
}

static void
q_mlslane (const uint32_t *a, const uint32_t *b, const uint32_t *c, uint32_t *r)
{
  uint32x4_t x = vld1q_u32 (a), z = vld1q_u32 (c);
  uint32x2_t y = vld1_u32 (b);
  __asm__ volatile ("vmls.f32 %q0, %q1, %P2[1]" : "+w" (z) : "w" (x), "x" (y));
  vst1q_u32 (r, z);
}

/* ---- the runner --------------------------------------------------- */

typedef uint32_t (*sfn) (uint32_t, uint32_t, uint32_t);
typedef uint32_t (*dfn) (uint64_t, uint64_t, uint64_t);
typedef void (*qfn) (const uint32_t *, const uint32_t *, const uint32_t *, uint32_t *);

typedef struct { const char *name; int kind, arity; void *fn; } op_t;
enum { K_S, K_D, K_Q };

static const op_t ops[] = {
  {"vadd.f32", K_S, 2, s_add}, {"vsub.f32", K_S, 2, s_sub},
  {"vmul.f32", K_S, 2, s_mul}, {"vnmul.f32", K_S, 2, s_nmul},
  {"vdiv.f32", K_S, 2, s_div}, {"vcmp.f32", K_S, 2, s_cmp},
  {"vcmpe.f32", K_S, 2, s_cmpe}, {"vcmpe.f32#0", K_S, 1, s_cmpz},
  {"vmla.f32", K_S, 3, s_mla}, {"vmls.f32", K_S, 3, s_mls},
  {"vnmla.f32", K_S, 3, s_nmla}, {"vnmls.f32", K_S, 3, s_nmls},
  {"vfma.f32", K_S, 3, s_fma}, {"vfms.f32", K_S, 3, s_fms},
  {"vfnma.f32", K_S, 3, s_fnma}, {"vfnms.f32", K_S, 3, s_fnms},
  {"vsqrt.f32", K_S, 1, s_sqrt}, {"vabs.f32", K_S, 1, s_abs},
  {"vneg.f32", K_S, 1, s_neg}, {"vcvt.s32.f32", K_S, 1, s_tosi},
  {"vcvtr.s32.f32", K_S, 1, s_tosir}, {"vcvt.u32.f32", K_S, 1, s_toui},
  {"vcvtr.u32.f32", K_S, 1, s_touir}, {"vcvt.f32.s32", K_S, 1, s_sito},
  {"vcvt.f32.u32", K_S, 1, s_uito}, {"vcvt.f64.f32", K_S, 1, s_tod},
  {"vcvt.s32.f32#16", K_S, 1, s_fix},
  {"vadd.f64", K_D, 2, d_add}, {"vsub.f64", K_D, 2, d_sub},
  {"vmul.f64", K_D, 2, d_mul}, {"vnmul.f64", K_D, 2, d_nmul},
  {"vdiv.f64", K_D, 2, d_div}, {"vcmp.f64", K_D, 2, d_cmp},
  {"vcmpe.f64", K_D, 2, d_cmpe},
  {"vmla.f64", K_D, 3, d_mla}, {"vmls.f64", K_D, 3, d_mls},
  {"vnmla.f64", K_D, 3, d_nmla}, {"vnmls.f64", K_D, 3, d_nmls},
  {"vfma.f64", K_D, 3, d_fma}, {"vfms.f64", K_D, 3, d_fms},
  {"vfnma.f64", K_D, 3, d_fnma}, {"vfnms.f64", K_D, 3, d_fnms},
  {"vsqrt.f64", K_D, 1, d_sqrt}, {"vabs.f64", K_D, 1, d_abs},
  {"vneg.f64", K_D, 1, d_neg}, {"vcvt.f32.f64", K_D, 1, d_tos},
  {"vcvt.s32.f64", K_D, 1, d_tosi}, {"vcvtr.s32.f64", K_D, 1, d_tosir},
  {"vcvt.u32.f64", K_D, 1, d_toui}, {"vcvtr.u32.f64", K_D, 1, d_touir},
  {"vcvt.f64.s32", K_D, 1, d_sito}, {"vcvt.f64.u32", K_D, 1, d_uito},
  {"vcmp.f64#0", K_D, 1, d_cmpz},
  {"q.vadd.f32", K_Q, 2, q_add}, {"q.vsub.f32", K_Q, 2, q_sub},
  {"q.vmul.f32", K_Q, 2, q_mul}, {"q.vabd.f32", K_Q, 2, q_abd},
  {"q.vmax.f32", K_Q, 2, q_max}, {"q.vmin.f32", K_Q, 2, q_min},
  {"q.vceq.f32", K_Q, 2, q_ceq}, {"q.vcge.f32", K_Q, 2, q_cge},
  {"q.vcgt.f32", K_Q, 2, q_cgt}, {"q.vacge.f32", K_Q, 2, q_acge},
  {"q.vrecps.f32", K_Q, 2, q_recps}, {"q.vrsqrts.f32", K_Q, 2, q_rsqrts},
  {"q.vpadd.f32", K_Q, 2, q_padd},
  {"q.vmla.f32", K_Q, 3, q_mla}, {"q.vmls.f32", K_Q, 3, q_mls},
  {"q.vfma.f32", K_Q, 3, q_fma}, {"q.vfms.f32", K_Q, 3, q_fms},
  {"q.vmla.f32[]", K_Q, 3, q_mlalane},
  {"q.vmul.f32[]", K_Q, 2, q_mullane}, {"q.vmls.f32[]", K_Q, 3, q_mlslane},
  {"q.vacgt.f32", K_Q, 2, q_acgt},
  {"q.vceq.f32#0", K_Q, 1, q_ceq0}, {"q.vcge.f32#0", K_Q, 1, q_cge0},
  {"q.vcgt.f32#0", K_Q, 1, q_cgt0}, {"q.vcle.f32#0", K_Q, 1, q_cle0},
  {"q.vclt.f32#0", K_Q, 1, q_clt0},
  {"d.vadd.f32", K_Q, 2, h_add}, {"d.vmul.f32", K_Q, 2, h_mul},
  {"d.vmax.f32", K_Q, 2, h_max}, {"d.vcge.f32", K_Q, 2, h_cge},
  {"d.vmla.f32", K_Q, 3, h_mla}, {"d.vmls.f32", K_Q, 3, h_mls},
  {"d.vcvt.s32.f32", K_Q, 1, h_tosi}, {"d.vcvt.f32.s32", K_Q, 1, h_sito},
  {"q.vabs.f32", K_Q, 1, q_abs}, {"q.vneg.f32", K_Q, 1, q_neg},
  {"q.vrecpe.f32", K_Q, 1, q_recpe}, {"q.vrsqrte.f32", K_Q, 1, q_rsqrte},
  {"q.vcvt.s32.f32", K_Q, 1, q_tosi}, {"q.vcvt.u32.f32", K_Q, 1, q_toui},
  {"q.vcvt.f32.s32", K_Q, 1, q_sito}, {"q.vcvt.f32.u32", K_Q, 1, q_uito},
};
#define NOPS (sizeof ops / sizeof ops[0])

static int verbose;

static uint32_t
note (uint32_t crc, uint32_t r, uint32_t fpscr, unsigned i, unsigned j,
      unsigned k)
{
  if (verbose)
    printf ("%3u %3u %3u -> %08x fpscr %08x\n", i, j, k, r, fpscr);
  crc = crc_word (crc, r);
  return crc_word (crc, fpscr);
}

/* runs one instruction in one mode; returns the CRC, *count the results */
static uint32_t
run_op (const op_t *op, uint32_t mode, unsigned *count)
{
  uint32_t crc = 0xffffffff;
  unsigned n = 0;
  unsigned na = op->arity == 3 ? NTRI : op->kind == K_D ? NDP : NSP;
  unsigned nb = op->arity >= 2 ? na : 1, nc = op->arity == 3 ? na : 1;
  if (op->kind == K_S)
    {
      const uint32_t *v = op->arity == 3 ? sv3 : sv;
      sfn f = (sfn) op->fn;
      for (unsigned i = 0; i < na; i++)
        for (unsigned j = 0; j < nb; j++)
          for (unsigned k = 0; k < nc; k++)
            {
              set_fpscr (mode);
              uint32_t r = f (v[i], v[j], v[k]);
              crc = note (crc, r, get_fpscr (), i, j, k), n++;
            }
    }
  else if (op->kind == K_D)
    {
      const uint64_t *v = op->arity == 3 ? dv3 : dv;
      dfn f = (dfn) op->fn;
      for (unsigned i = 0; i < na; i++)
        for (unsigned j = 0; j < nb; j++)
          for (unsigned k = 0; k < nc; k++)
            {
              set_fpscr (mode);
              uint32_t r = f (v[i], v[j], v[k]);
              crc = note (crc, r, get_fpscr (), i, j, k), n++;
            }
    }
  else
    {
      /* lane l of operand b takes value j+l, so every pair is covered once
         per four lanes */
      const uint32_t *v = op->arity == 3 ? sv3 : sv;
      qfn f = (qfn) op->fn;
      for (unsigned i = 0; i < na; i++)
        for (unsigned j = 0; j < nb; j += (op->arity >= 2 ? 1 : 4))
          for (unsigned k = 0; k < nc; k += 4)
            {
              uint32_t a[4], b[4], c[4], r[4];
              for (int l = 0; l < 4; l++)
                {
                  a[l] = v[op->arity == 1 ? (i + l) % na : i];
                  b[l] = v[(j + l) % na];
                  c[l] = v[(k + l) % na];
                }
              set_fpscr (mode);
              f (a, b, c, r);
              uint32_t fp = get_fpscr ();
              for (int l = 0; l < 4; l++)
                crc = note (crc, r[l], l == 3 ? fp : 0, i, j + l, k + l), n++;
            }
    }
  *count = n;
  return ~crc;
}

int
main (int argc, char **argv)
{
  crc_init ();
  make_values ();
  if (argc == 3)
    {
      int m = atoi (argv[2]);
      if (m < 0 || m > 15)
        return 2;
      for (size_t o = 0; o < NOPS; o++)
        if (!strcmp (ops[o].name, argv[1]))
          {
            unsigned n;
            verbose = 1;
            run_op (&ops[o], modes[m], &n);
            set_fpscr (0);
            return 0;
          }
      printf ("no instruction %s\n", argv[1]);
      return 2;
    }
  uint32_t all = 0xffffffff;
  unsigned total = 0;
  for (size_t o = 0; o < NOPS; o++)
    {
      printf ("%-16s", ops[o].name);
      for (int m = 0; m < 16; m++)
        {
          unsigned n;
          uint32_t c = run_op (&ops[o], modes[m], &n);
          total += n;
          all = crc_word (all, c);
          printf (" %08x", c);
        }
      printf ("\n");
    }
  set_fpscr (0);
  printf ("t_fpconform: %u results in %u instructions x 16 modes, CRC %08x\n",
          total, (unsigned) NOPS, ~all);
  return 0;
}
