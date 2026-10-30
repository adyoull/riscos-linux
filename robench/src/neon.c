/* ROBench: NEON (Advanced SIMD) tests. Copyright 2026 Andrew Youll.
   MIT licence (see LICENCE).

   Each test is the kind of inner loop real programs hand-vectorise:
   alpha blending and YUV->RGB (video players, desktop compositing), sum
   of absolute differences (video encoders), FIR filters and saxpy (audio,
   DSP), 4x4 transforms (3D). The ARM build uses NEON intrinsics; the
   native build and a CPU without NEON use plain C (labelled "scalar" by
   neon_mode()). Every test is checked against a plain C reference. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "bench.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define HAVE_NEON 1
#else
#define HAVE_NEON 0
#endif

#ifdef __riscos
#include <kernel.h>
#include <swis.h>
#endif

static int use_neon = -1;

static int
neon_present (void)
{
  if (use_neon >= 0)
    return use_neon;
  use_neon = 0;
#if HAVE_NEON
#ifdef __riscos
  /* VFPSupport_Features 0: r1 = MVFR0, r2 = MVFR1. MVFR1 bits 8-11 = SIMD
     integer, 16-19 = SIMD single precision. */
  int r0, r1, r2;
  if (!_swix (0x58EC8, _IN (0) | _OUT (0) | _OUT (1) | _OUT (2), 0,
              &r0, &r1, &r2)
      && ((unsigned) r2 >> 8 & 15) && ((unsigned) r2 >> 16 & 15))
    use_neon = 1;
#else
  use_neon = 1;
#endif
#endif
  if (getenv ("ROBENCH_NO_NEON"))
    use_neon = 0;
  return use_neon;
}

void
neon_disable (void)
{
  neon_present ();
  use_neon = 0;
}

const char *
neon_mode (void)
{
  return neon_present () ? "NEON intrinsics" : "scalar C (no NEON)";
}

/* ---- image buffers ------------------------------------------------- */

#define IW 640
#define IH 480
#define NPIX (IW * IH)

static uint8_t *im_a, *im_b, *im_out, *im_ref;
static uint8_t *yuv_y, *yuv_u, *yuv_v;

static int
img_setup (void)
{
  neon_present ();
  im_a = malloc (NPIX * 4), im_b = malloc (NPIX * 4);
  im_out = malloc (NPIX * 4), im_ref = malloc (NPIX * 4);
  yuv_y = malloc (NPIX), yuv_u = malloc (NPIX / 4), yuv_v = malloc (NPIX / 4);
  if (!im_a || !im_b || !im_out || !im_ref || !yuv_y || !yuv_u || !yuv_v)
    return 1;
  uint32_t s = 0x1234567;
  for (int i = 0; i < NPIX * 4; i++)
    im_a[i] = rng32 (&s) >> 24, im_b[i] = rng32 (&s) >> 24;
  /* a smooth-ish picture for the video tests (real frames are not noise) */
  for (int y = 0; y < IH; y++)
    for (int x = 0; x < IW; x++)
      yuv_y[y * IW + x] = (uint8_t) (16 + ((x * 3 + y * 5 + (rng32 (&s) >> 29)) % 220));
  for (int i = 0; i < NPIX / 4; i++)
    yuv_u[i] = 16 + (rng32 (&s) >> 24) % 225, yuv_v[i] = 16 + (rng32 (&s) >> 24) % 225;
  return 0;
}

static void
img_teardown (void)
{
  free (im_a), free (im_b), free (im_out), free (im_ref);
  free (yuv_y), free (yuv_u), free (yuv_v);
  im_a = im_b = im_out = im_ref = yuv_y = yuv_u = yuv_v = NULL;
}

/* ---- alpha blend: out = (a*alpha + b*(255-alpha) + 127) / 255 -------- */

static void
blend_c (uint8_t *o, const uint8_t *a, const uint8_t *b, int n)
{
  for (int i = 0; i < n; i++)
    {
      unsigned al = a[i * 4 + 3];
      for (int c = 0; c < 4; c++)
        {
          unsigned v = a[i * 4 + c] * al + b[i * 4 + c] * (255 - al) + 128;
          o[i * 4 + c] = (uint8_t) ((v + (v >> 8)) >> 8);
        }
    }
}

static void
blend_v (uint8_t *o, const uint8_t *a, const uint8_t *b, int n)
{
#if HAVE_NEON
  if (use_neon)
    {
      for (int i = 0; i < n; i += 8)
        {
          uint8x8x4_t pa = vld4_u8 (a + i * 4), pb = vld4_u8 (b + i * 4), po;
          uint8x8_t al = pa.val[3], ial = vmvn_u8 (al);
          for (int c = 0; c < 4; c++)
            {
              uint16x8_t v = vmull_u8 (pa.val[c], al);
              v = vmlal_u8 (v, pb.val[c], ial);
              v = vaddq_u16 (v, vdupq_n_u16 (128));
              po.val[c] = vshrn_n_u16 (vsraq_n_u16 (v, v, 8), 8);
            }
          vst4_u8 (o + i * 4, po);
        }
      return;
    }
#endif
  blend_c (o, a, b, n);
}

static uint32_t
blend_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t k = 0; k < iters; k++)
    {
      blend_v (im_out, im_a, im_b, NPIX);
      sum = sum * 31 + im_out[(k * 977) % (NPIX * 4)];
    }
  return sum;
}

static int
blend_verify (char *msg, size_t len)
{
  blend_v (im_out, im_a, im_b, NPIX);
  blend_c (im_ref, im_a, im_b, NPIX);
  if (memcmp (im_out, im_ref, NPIX * 4))
    {
      snprintf (msg, len, "blend differs from the C reference");
      return 0;
    }
  return 1;
}

/* ---- YUV 4:2:0 -> RGBA, BT.601, 5-bit fixed point --------------------- */

static inline uint8_t
clamp8 (int v)
{
  return v < 0 ? 0 : v > 255 ? 255 : (uint8_t) v;
}

static void
yuv_c (uint8_t *o)
{
  for (int y = 0; y < IH; y++)
    for (int x = 0; x < IW; x++)
      {
        int Y = (yuv_y[y * IW + x] - 16) * 37;          /* 1.164 * 32 */
        int U = yuv_u[(y / 2) * (IW / 2) + x / 2] - 128;
        int V = yuv_v[(y / 2) * (IW / 2) + x / 2] - 128;
        uint8_t *p = o + (y * IW + x) * 4;
        p[0] = clamp8 ((Y + 51 * V + 16) >> 5);
        p[1] = clamp8 ((Y - 13 * U - 26 * V + 16) >> 5);
        p[2] = clamp8 ((Y + 65 * U + 16) >> 5);
        p[3] = 255;
      }
}

static void
yuv_v_ (uint8_t *o)
{
#if HAVE_NEON
  if (use_neon)
    {
      const int16x8_t c16 = vdupq_n_s16 (16), c128 = vdupq_n_s16 (128);
      for (int y = 0; y < IH; y++)
        {
          const uint8_t *py = yuv_y + y * IW;
          const uint8_t *pu = yuv_u + (y / 2) * (IW / 2);
          const uint8_t *pv = yuv_v + (y / 2) * (IW / 2);
          uint8_t *po = o + y * IW * 4;
          for (int x = 0; x < IW; x += 8)
            {
              int16x8_t Y = vmulq_n_s16 (vsubq_s16 (vreinterpretq_s16_u16 (vmovl_u8 (vld1_u8 (py + x))), c16), 37);
              /* each chroma sample covers two pixels */
              uint8x8_t u4 = vld1_u8 (pu + x / 2), v4 = vld1_u8 (pv + x / 2);
              uint8x8_t uu = vzip_u8 (u4, u4).val[0], vv = vzip_u8 (v4, v4).val[0];
              int16x8_t U = vsubq_s16 (vreinterpretq_s16_u16 (vmovl_u8 (uu)), c128);
              int16x8_t V = vsubq_s16 (vreinterpretq_s16_u16 (vmovl_u8 (vv)), c128);
              uint8x8x4_t px;
              /* (Y + k*C + 16) >> 5, saturated: vqrshrun does round, shift and clamp */
              px.val[0] = vqrshrun_n_s16 (vmlaq_n_s16 (Y, V, 51), 5);
              px.val[1] = vqrshrun_n_s16 (vmlaq_n_s16 (vmlaq_n_s16 (Y, U, -13), V, -26), 5);
              px.val[2] = vqrshrun_n_s16 (vmlaq_n_s16 (Y, U, 65), 5);
              px.val[3] = vdup_n_u8 (255);
              vst4_u8 (po + x * 4, px);
            }
        }
      return;
    }
#endif
  yuv_c (o);
}

static uint32_t
yuv_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t k = 0; k < iters; k++)
    {
      yuv_v_ (im_out);
      sum = sum * 31 + im_out[(k * 977) % (NPIX * 4)];
    }
  return sum;
}

static int
yuv_verify (char *msg, size_t len)
{
  /* 16-bit intermediates in the NEON version: the C reference is written
     to give identical results for every 8-bit input (|Y + kC| < 32768). */
  yuv_v_ (im_out);
  yuv_c (im_ref);
  if (memcmp (im_out, im_ref, NPIX * 4))
    {
      snprintf (msg, len, "YUV->RGB differs from the C reference");
      return 0;
    }
  return 1;
}

/* ---- SAD: 16x16 block search, +-4 pixels, as a video encoder's motion
   estimation does --------------------------------------------------- */

#define SAD_R 4

static unsigned
sad16_c (const uint8_t *a, const uint8_t *b)
{
  unsigned s = 0;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++)
      s += abs (a[y * IW + x] - b[y * IW + x]);
  return s;
}

static unsigned
sad16_v (const uint8_t *a, const uint8_t *b)
{
#if HAVE_NEON
  if (use_neon)
    {
      uint16x8_t acc = vdupq_n_u16 (0);
      for (int y = 0; y < 16; y++)
        {
          uint8x16_t va = vld1q_u8 (a + y * IW), vb = vld1q_u8 (b + y * IW);
          acc = vabal_u8 (acc, vget_low_u8 (va), vget_low_u8 (vb));
          acc = vabal_u8 (acc, vget_high_u8 (va), vget_high_u8 (vb));
        }
      uint32x4_t s4 = vpaddlq_u16 (acc);
      uint64x2_t s2 = vpaddlq_u32 (s4);
      return (unsigned) (vgetq_lane_u64 (s2, 0) + vgetq_lane_u64 (s2, 1));
    }
#endif
  return sad16_c (a, b);
}

/* one search over a band of blocks; returns the sum of best SADs */
static uint32_t
sad_search (unsigned (*sad) (const uint8_t *, const uint8_t *),
            const uint8_t *cur, const uint8_t *ref)
{
  uint32_t total = 0;
  for (int by = 1; by < 5; by++)
    for (int bx = 1; bx < IW / 16 - 1; bx++)
      {
        const uint8_t *c = cur + by * 16 * IW + bx * 16;
        unsigned best = ~0u;
        for (int dy = -SAD_R; dy <= SAD_R; dy++)
          for (int dx = -SAD_R; dx <= SAD_R; dx++)
            {
              unsigned s = sad (c, ref + (by * 16 + dy) * IW + bx * 16 + dx);
              if (s < best)
                best = s;
            }
        total += best;
      }
  return total;
}

#define SAD_BLOCKS (4 * (IW / 16 - 2) * (2 * SAD_R + 1) * (2 * SAD_R + 1))

static uint32_t
sad_run (uint32_t iters)
{
  uint32_t sum = 0;
  /* the reference frame: the picture moved by a few pixels */
  for (uint32_t k = 0; k < iters; k++)
    sum += sad_search (sad16_v, yuv_y + 3 * IW + 2, yuv_y);
  return sum;
}

static int
sad_verify (char *msg, size_t len)
{
  uint32_t a = sad_search (sad16_v, yuv_y + 3 * IW + 2, yuv_y);
  uint32_t b = sad_search (sad16_c, yuv_y + 3 * IW + 2, yuv_y);
  if (a != b)
    {
      snprintf (msg, len, "SAD %u, C reference %u", (unsigned) a, (unsigned) b);
      return 0;
    }
  return 1;
}

/* ---- float buffers ------------------------------------------------- */

#define FN 65536
#define FIR_TAPS 32

static float *f_x, *f_y, *f_ref, *f_h, *f_v, *f_vo;

static int
flt_setup (void)
{
  neon_present ();
  f_x = malloc ((FN + FIR_TAPS) * 4), f_y = malloc (FN * 4);
  f_ref = malloc (FN * 4), f_h = malloc (FIR_TAPS * 4);
  f_v = malloc (FN * 16), f_vo = malloc (FN * 16);
  if (!f_x || !f_y || !f_ref || !f_h || !f_v || !f_vo)
    return 1;
  uint32_t s = 99;
  /* full-precision values, as real samples and coordinates are (data
     that happens to be exact takes different paths in some emulators) */
  for (int i = 0; i < FN + FIR_TAPS; i++)
    f_x[i] = (float) ((int) (rng32 (&s) >> 8) - 0x800000) / 8388608.0f;
  for (int i = 0; i < FIR_TAPS; i++)
    f_h[i] = (float) (FIR_TAPS / 2 - abs (i - FIR_TAPS / 2) + 1) / 293.7f;
  for (int i = 0; i < FN * 4; i++)
    f_v[i] = (float) ((int) (rng32 (&s) >> 8) - 0x800000) / 65536.3f;
  return 0;
}

static void
flt_teardown (void)
{
  free (f_x), free (f_y), free (f_ref), free (f_h), free (f_v), free (f_vo);
  f_x = f_y = f_ref = f_h = f_v = f_vo = NULL;
}

/* bit-exact float comparison (both versions do the same operations in the
   same order, with no denormals, so NEON's flush-to-zero makes no
   difference) */
static int
fcmp (const float *a, const float *b, int n, const char *what, char *msg,
      size_t len)
{
  if (memcmp (a, b, n * 4))
    {
      for (int i = 0; i < n; i++)
        if (memcmp (a + i, b + i, 4))
          {
            snprintf (msg, len, "%s element %d: %.9g, C reference %.9g",
                      what, i, (double) a[i], (double) b[i]);
            break;
          }
      return 0;
    }
  return 1;
}

static uint32_t
fsum (const float *p, int n)
{
  return crc32_buf (0, p, n * 4);
}

/* ---- saxpy: y = a*x + y (separate multiply and add, as vmla does) --- */

static void
saxpy_c (float *y, const float *x, float a, int n)
{
  for (int i = 0; i < n; i++)
    {
      float t = a * x[i];
      y[i] = y[i] + t;
    }
}

static void
saxpy_v (float *y, const float *x, float a, int n)
{
#if HAVE_NEON
  if (use_neon)
    {
      for (int i = 0; i < n; i += 8)
        {
          float32x4_t y0 = vld1q_f32 (y + i), y1 = vld1q_f32 (y + i + 4);
          y0 = vmlaq_n_f32 (y0, vld1q_f32 (x + i), a);
          y1 = vmlaq_n_f32 (y1, vld1q_f32 (x + i + 4), a);
          vst1q_f32 (y + i, y0), vst1q_f32 (y + i + 4, y1);
        }
      return;
    }
#endif
  saxpy_c (y, x, a, n);
}

static uint32_t
saxpy_run (uint32_t iters)
{
  memset (f_y, 0, FN * 4);
  for (uint32_t k = 0; k < iters; k++)
    saxpy_v (f_y, f_x, (k & 1) ? 0.7071f : -0.7072f, FN);
  return fsum (f_y, FN);
}

static int
saxpy_verify (char *msg, size_t len)
{
  memset (f_y, 0, FN * 4), memset (f_ref, 0, FN * 4);
  for (int k = 0; k < 5; k++)
    saxpy_v (f_y, f_x, 0.75f, FN), saxpy_c (f_ref, f_x, 0.75f, FN);
  return fcmp (f_y, f_ref, FN, "saxpy", msg, len);
}

/* ---- FIR filter, 32 taps ------------------------------------------- */

static void
fir_c (float *y, const float *x, const float *h, int n)
{
  for (int i = 0; i < n; i++)
    {
      /* four partial sums, interleaved like the NEON lanes */
      float s[4] = { 0, 0, 0, 0 };
      for (int t = 0; t < FIR_TAPS; t++)
        {
          float p = x[i + t] * h[t];
          s[t & 3] = s[t & 3] + p;
        }
      y[i] = (s[0] + s[2]) + (s[1] + s[3]);      /* as the NEON lanes */
    }
}

static void
fir_v (float *y, const float *x, const float *h, int n)
{
#if HAVE_NEON
  if (use_neon)
    {
      for (int i = 0; i < n; i++)
        {
          float32x4_t acc = vdupq_n_f32 (0);
          for (int t = 0; t < FIR_TAPS; t += 4)
            acc = vmlaq_f32 (acc, vld1q_f32 (x + i + t), vld1q_f32 (h + t));
          float32x2_t s2 = vadd_f32 (vget_low_f32 (acc), vget_high_f32 (acc));
          y[i] = vget_lane_f32 (s2, 0) + vget_lane_f32 (s2, 1);
        }
      return;
    }
#endif
  fir_c (y, x, h, n);
}

static uint32_t
fir_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t k = 0; k < iters; k++)
    {
      fir_v (f_y, f_x + (k & 7), f_h, FN / 4);
      sum = sum * 31 + fsum (f_y + (k % 64) * 16, 16);
    }
  return sum;
}

static int
fir_verify (char *msg, size_t len)
{
  fir_v (f_y, f_x, f_h, FN);
  fir_c (f_ref, f_x, f_h, FN);
  return fcmp (f_y, f_ref, FN, "FIR", msg, len);
}

/* ---- 4x4 matrix x vec4 (vertex transform) ------------------------- */

/* a rotation, a slight scale and a translation */
static const float xf_m[16] = {
  0.80901699f, -0.58778525f, 0.0f, 0.0f,
  0.58778525f, 0.80901699f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.05f, 0.0f,
  10.3f, -5.7f, 2.9f, 1.0f
};

static void
xform_c (float *o, const float *v, int n)
{
  for (int i = 0; i < n; i++)
    for (int r = 0; r < 4; r++)
      {
        /* column-major M: o = c0*x + c1*y + c2*z + c3*w, in that order */
        float s = xf_m[r] * v[i * 4];
        s = s + xf_m[4 + r] * v[i * 4 + 1];
        s = s + xf_m[8 + r] * v[i * 4 + 2];
        s = s + xf_m[12 + r] * v[i * 4 + 3];
        o[i * 4 + r] = s;
      }
}

static void
xform_v (float *o, const float *v, int n)
{
#if HAVE_NEON
  if (use_neon)
    {
      float32x4_t c0 = vld1q_f32 (xf_m), c1 = vld1q_f32 (xf_m + 4);
      float32x4_t c2 = vld1q_f32 (xf_m + 8), c3 = vld1q_f32 (xf_m + 12);
      for (int i = 0; i < n; i++)
        {
          float32x4_t p = vld1q_f32 (v + i * 4);
          float32x4_t s = vmulq_lane_f32 (c0, vget_low_f32 (p), 0);
          s = vmlaq_lane_f32 (s, c1, vget_low_f32 (p), 1);
          s = vmlaq_lane_f32 (s, c2, vget_high_f32 (p), 0);
          s = vmlaq_lane_f32 (s, c3, vget_high_f32 (p), 1);
          vst1q_f32 (o + i * 4, s);
        }
      return;
    }
#endif
  xform_c (o, v, n);
}

static uint32_t
xform_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t k = 0; k < iters; k++)
    {
      xform_v (f_vo, f_v, FN);
      sum = sum * 31 + fsum (f_vo + (k % 1024) * 64, 64);
    }
  return sum;
}

static int
xform_verify (char *msg, size_t len)
{
  float *ref = malloc (FN * 16);
  if (!ref)
    return 1;
  xform_v (f_vo, f_v, FN);
  xform_c (ref, f_v, FN);
  int ok = fcmp (f_vo, ref, FN * 4, "transform", msg, len);
  free (ref);
  return ok;
}

/* ------------------------------------------------------------------ */

const test_t neon_tests[] = {
  {"neon.blend", "Alpha blend 640x480 RGBA (u8)", "Mpixel/s", NPIX / 1e6,
   img_setup, blend_run, blend_verify, img_teardown, 0},
  {"neon.yuv2rgb", "YUV 4:2:0 to RGBA 640x480 (s16)", "Mpixel/s", NPIX / 1e6,
   img_setup, yuv_run, yuv_verify, img_teardown, 0},
  {"neon.sad", "Motion search, 16x16 SAD (u8)", "Mblocks/s", SAD_BLOCKS / 1e6,
   img_setup, sad_run, sad_verify, img_teardown, 0},
  {"neon.saxpy", "saxpy 64K floats (f32)", "MFLOPS", 2.0 * FN / 1e6,
   flt_setup, saxpy_run, saxpy_verify, flt_teardown, 0},
  {"neon.fir", "FIR filter, 32 taps, 16K samples (f32)", "MFLOPS",
   2.0 * FIR_TAPS * (FN / 4) / 1e6, flt_setup, fir_run, fir_verify,
   flt_teardown, 0},
  {"neon.xform", "4x4 transform of 64K vertices (f32)", "Mvert/s", FN / 1e6,
   flt_setup, xform_run, xform_verify, flt_teardown, 0},
};
const size_t n_neon_tests = sizeof neon_tests / sizeof neon_tests[0];
