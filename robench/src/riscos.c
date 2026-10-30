/* ROBench: RISC OS tests (SWIs, files, graphics) and machine details.
   Copyright 2026 Andrew Youll. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"

#define MB (1024.0 * 1024.0)

#ifndef __riscos

/* Other systems: no RISC OS tests; describe the host. */
#include <sys/utsname.h>
const test_t os_tests[] = { {0} };
const size_t n_os_tests = 0;
void gfx_begin (void) {}
void gfx_end (void) {}

void
machine_info (char *buf, size_t len, char *shortname, size_t sl)
{
  struct utsname u;
  char model[128] = "unknown CPU", line[256];
  FILE *f = fopen ("/proc/cpuinfo", "r");
  if (f)
    {
      while (fgets (line, sizeof line, f))
        if (!strncmp (line, "model name", 10) || !strncmp (line, "Model", 5))
          {
            char *p = strchr (line, ':');
            if (p)
              {
                snprintf (model, sizeof model, "%s", p + 2);
                model[strcspn (model, "\n")] = 0;
                break;
              }
          }
      fclose (f);
    }
  uname (&u);
  const char *name = getenv ("ROBENCH_MACHINE");
  snprintf (buf, len, "Host: %s %s %s, %s (native build, no RISC OS tests)\n",
            u.sysname, u.release, u.machine, model);
  snprintf (shortname, sl, "%s", name ? name : model);
}

#else /* __riscos */

#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>

/* file names are RISC OS names: pass them through unchanged */
int __riscosify_control = __RISCOSIFY_NO_PROCESS;
/* the heap in a dynamic area (the tests need up to about 60 MB) */
const char *const __dynamic_da_name = "ROBench Heap";
int __dynamic_da_max_size = 96 * 1024 * 1024;

#ifndef JPEG_Info
#define JPEG_Info 0x49980
#define JPEG_PlotScaled 0x49982
#endif

/* ------------------------------------------------------------------ */
/* SWI costs. On real hardware a SWI is a processor exception; on the
   Linux port it is a host signal, so this is where the two differ most. */

static uint32_t
swi_raw (uint32_t iters)
{
  uint32_t t = 0;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < 1000; i++)
      {
        __asm__ volatile ("swi 0x20042" : : : "r0", "cc", "memory");
        t++;
      }
  return t;
}

static uint32_t
swi_var (uint32_t iters)
{
  char val[256];
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < 1000; i++)
      {
        int n = 0;
        _swix (OS_ReadVarVal, _INR (0, 4) | _OUT (2), "Run$Path", val,
               (int) sizeof val - 1, 0, 0, &n);
        sum += (uint32_t) n;
      }
  return sum;
}

/* ------------------------------------------------------------------ */
/* Files: in <Wimp$ScrapDir>.ROBench (or -d). Includes the file
   system's caching, as programs see it. */

static char io_dir[256], io_big[300], io_bytes[300];
static unsigned char *io_buf;
#define IO_SIZE (8 * 1024 * 1024)
#define IO_CHUNK (64 * 1024)
#define IO_BYTES (256 * 1024)
#define IO_SMALL 50

static int
io_setup (void)
{
  if (io_buf)
    return 0;
  if (g_scratch)
    snprintf (io_dir, sizeof io_dir, "%s", g_scratch);
  else if (getenv ("Wimp$ScrapDir"))
    snprintf (io_dir, sizeof io_dir, "<Wimp$ScrapDir>.ROBench");
  else
    snprintf (io_dir, sizeof io_dir, "@.ROBenchTmp");
  _swix (OS_File, _INR (0, 1) | _IN (4), 8, io_dir, 0);       /* mkdir */
  snprintf (io_big, sizeof io_big, "%s.Big", io_dir);
  snprintf (io_bytes, sizeof io_bytes, "%s.Bytes", io_dir);
  io_buf = malloc (IO_SIZE);
  if (!io_buf)
    return -1;
  uint32_t s = 11;
  for (size_t i = 0; i < IO_SIZE; i += 4)
    *(uint32_t *) (io_buf + i) = rng32 (&s);
  /* make the big file once, so the read tests have it */
  FILE *f = fopen (io_big, "wb");
  if (!f)
    return -1;
  fwrite (io_buf, 1, IO_SIZE, f);
  fclose (f);
  return 0;
}

static void
io_teardown (void)
{
  _swix (OS_File, _INR (0, 1), 6, io_big);
  _swix (OS_File, _INR (0, 1), 6, io_bytes);
  _swix (OS_File, _INR (0, 1), 6, io_dir);
  free (io_buf);
  io_buf = NULL;
}

static uint32_t
io_fwrite (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      FILE *f = fopen (io_big, "wb");
      if (!f)
        return 0;
      for (size_t o = 0; o < IO_SIZE; o += IO_CHUNK)
        sum += (uint32_t) fwrite (io_buf + o, 1, IO_CHUNK, f);
      fclose (f);
    }
  return sum;
}

static uint32_t
io_fread (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      FILE *f = fopen (io_big, "rb");
      if (!f)
        return 0;
      size_t n;
      while ((n = fread (io_buf, 1, IO_CHUNK, f)) > 0)
        sum += (uint32_t) n + io_buf[n / 2];
      fclose (f);
    }
  return sum;
}

static uint32_t
io_save (uint32_t iters)
{
  uint32_t ok = 0;
  for (uint32_t it = 0; it < iters; it++)
    ok += !_swix (OS_File, _INR (0, 2) | _INR (4, 5), 10, io_big, 0xffd,
                  io_buf, io_buf + IO_SIZE);
  return ok;
}

static uint32_t
io_load (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    if (!_swix (OS_File, _INR (0, 3), 16, io_big, io_buf, 0))
      sum += io_buf[it % IO_SIZE];
  return sum;
}

static uint32_t
io_bput (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      int h = 0;
      if (_swix (OS_Find, _INR (0, 1) | _OUT (0), 0x83, io_bytes, &h) || !h)
        return 0;
      for (int i = 0; i < IO_BYTES; i++)
        _swix (OS_BPut, _INR (0, 1), i & 0xff, h);
      _swix (OS_Find, _INR (0, 1), 0, h);
      sum++;
    }
  return sum;
}

static uint32_t
io_bget (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      int h = 0, c = 0;
      if (_swix (OS_Find, _INR (0, 1) | _OUT (0), 0x43, io_bytes, &h) || !h)
        return 0;
      for (int i = 0; i < IO_BYTES; i++)
        {
          _swix (OS_BGet, _IN (1) | _OUT (0), h, &c);
          sum += (uint32_t) c;
        }
      _swix (OS_Find, _INR (0, 1), 0, h);
    }
  return sum;
}

static uint32_t
io_small (uint32_t iters)
{
  char name[320];
  uint32_t ok = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      for (int i = 0; i < IO_SMALL; i++)
        {
          snprintf (name, sizeof name, "%s.S%d", io_dir, i);
          ok += !_swix (OS_File, _INR (0, 2) | _INR (4, 5), 10, name, 0xfff,
                        io_buf, io_buf + 1024);
        }
      for (int i = 0; i < IO_SMALL; i++)
        {
          snprintf (name, sizeof name, "%s.S%d", io_dir, i);
          _swix (OS_File, _INR (0, 1), 6, name);
        }
    }
  return ok;
}

/* ------------------------------------------------------------------ */
/* Graphics: in the current screen mode, drawing over the whole screen
   (the desktop is redrawn afterwards). */

static int g_xeig, g_yeig, g_w, g_h, g_log2bpp;   /* w, h in OS units */
static int g_wpx, g_hpx;

void
gfx_begin (void)
{
  int xwl = 0, ywl = 0;
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 4, &g_xeig);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 5, &g_yeig);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 9, &g_log2bpp);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 11, &xwl);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 12, &ywl);
  g_wpx = xwl + 1, g_hpx = ywl + 1;
  g_w = g_wpx << g_xeig, g_h = g_hpx << g_yeig;
  _swix (OS_RemoveCursors, 0);
  _swix (OS_WriteC, _IN (0), 26);       /* default windows */
  _swix (ColourTrans_SetGCOL, _IN (0) | _INR (3, 4), 0x00000000, 128, 0);
  _swix (OS_WriteC, _IN (0), 16);       /* CLG */
}

void
gfx_end (void)
{
  _swix (OS_WriteC, _IN (0), 26);
  _swix (OS_WriteC, _IN (0), 12);
  _swix (OS_RestoreCursors, 0);
}

static void
gcol (uint32_t *s)
{
  uint32_t c = rng32 (s) & 0xffffff00u;
  _swix (ColourTrans_SetGCOL, _IN (0) | _INR (3, 4), c, 0, 0);
}

#define RECT_PX 128
#define RECTS 100

static uint32_t
gfx_rect (uint32_t iters)
{
  uint32_t s = 1;
  int rw = RECT_PX << g_xeig, rh = RECT_PX << g_yeig;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < RECTS; i++)
      {
        int x = (int) (rng32 (&s) % (unsigned) (g_w - rw));
        int y = (int) (rng32 (&s) % (unsigned) (g_h - rh));
        gcol (&s);
        _swix (OS_Plot, _INR (0, 2), 4, x, y);
        _swix (OS_Plot, _INR (0, 2), 101, x + rw - 1, y + rh - 1);
      }
  return s;
}

static uint32_t
gfx_line (uint32_t iters)
{
  uint32_t s = 2;
  for (uint32_t it = 0; it < iters; it++)
    {
      gcol (&s);
      for (int i = 0; i < 1000; i++)
        {
          int x0 = (int) (rng32 (&s) % (unsigned) g_w), y0 = (int) (rng32 (&s) % (unsigned) g_h);
          int x1 = (int) (rng32 (&s) % (unsigned) g_w), y1 = (int) (rng32 (&s) % (unsigned) g_h);
          _swix (OS_Plot, _INR (0, 2), 4, x0, y0);
          _swix (OS_Plot, _INR (0, 2), 5, x1, y1);
        }
    }
  return s;
}

#define COPY_PX 256
#define COPIES 20

static uint32_t
gfx_copy (uint32_t iters)
{
  uint32_t s = 3;
  int cw = COPY_PX << g_xeig, ch = COPY_PX << g_yeig;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < COPIES; i++)
      {
        int x = (int) (rng32 (&s) % (unsigned) (g_w - cw));
        int y = (int) (rng32 (&s) % (unsigned) (g_h - ch));
        int dx = (int) (rng32 (&s) % (unsigned) (g_w - cw));
        int dy = (int) (rng32 (&s) % (unsigned) (g_h - ch));
        _swix (OS_Plot, _INR (0, 2), 4, x, y);
        _swix (OS_Plot, _INR (0, 2), 4, x + cw - 1, y + ch - 1);
        _swix (OS_Plot, _INR (0, 2), 190, dx, dy);      /* copy rectangle */
      }
  return s;
}

#define SPR_PX 128
#define SPRITES 100
static int *spr_area;

static int
spr_setup (void)
{
  if (spr_area)
    return 0;
  int size = SPR_PX * SPR_PX * 4 + 8192;
  spr_area = malloc (size);
  if (!spr_area)
    return -1;
  spr_area[0] = size, spr_area[1] = 0, spr_area[2] = 16, spr_area[3] = 16;
  /* draw a pattern, then grab it in the screen's own format */
  uint32_t s = 4;
  for (int i = 0; i < 16; i++)
    {
      gcol (&s);
      int q = SPR_PX / 4;
      int x = (i % 4) * (q << g_xeig), y = (i / 4) * (q << g_yeig);
      _swix (OS_Plot, _INR (0, 2), 4, x, y);
      _swix (OS_Plot, _INR (0, 2), 101, x + (q << g_xeig) - 1, y + (q << g_yeig) - 1);
    }
  if (_swix (OS_SpriteOp, _INR (0, 7), 256 + 16, spr_area, "robench", 0, 0, 0,
             (SPR_PX << g_xeig) - 1, (SPR_PX << g_yeig) - 1))
    {
      free (spr_area);
      spr_area = NULL;
      return -1;
    }
  return 0;
}

static void
spr_teardown (void)
{
  free (spr_area);
  spr_area = NULL;
}

static uint32_t
gfx_sprite (uint32_t iters)
{
  uint32_t s = 5;
  int sw = SPR_PX << g_xeig, sh = SPR_PX << g_yeig;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < SPRITES; i++)
      {
        int x = (int) (rng32 (&s) % (unsigned) (g_w - sw));
        int y = (int) (rng32 (&s) % (unsigned) (g_h - sh));
        _swix (OS_SpriteOp, _INR (0, 5), 256 + 34, spr_area, "robench", x, y, 0);
      }
  return s;
}

static uint32_t
gfx_sprite_scaled (uint32_t iters)
{
  uint32_t s = 6;
  static const int scale[4] = { 2, 2, 1, 1 };
  int sw = 2 * SPR_PX << g_xeig, sh = 2 * SPR_PX << g_yeig;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < SPRITES / 4; i++)
      {
        int x = (int) (rng32 (&s) % (unsigned) (g_w - sw));
        int y = (int) (rng32 (&s) % (unsigned) (g_h - sh));
        _swix (OS_SpriteOp, _INR (0, 7), 256 + 52, spr_area, "robench", x, y, 0,
               scale, 0);
      }
  return s;
}

/* Font_Paint: anti-aliased outline text, 24 point Homerton.Medium */
static int font_h;
static const char font_text[] = "The quick brown fox jumps over the lazy dog";

static int
font_setup (void)
{
  if (font_h)
    return 0;
  if (_swix (Font_FindFont, _INR (1, 5) | _OUT (0), "Homerton.Medium",
             24 * 16, 24 * 16, 0, 0, &font_h))
    return -1;
  return 0;
}

static void
font_teardown (void)
{
  if (font_h)
    _swix (Font_LoseFont, _IN (0), font_h);
  font_h = 0;
}

static uint32_t
gfx_font (uint32_t iters)
{
  uint32_t s = 7;
  for (uint32_t it = 0; it < iters; it++)
    {
      _swix (ColourTrans_SetFontColours, _INR (0, 3), font_h, 0xffffff00u,
             rng32 (&s) & 0xffffff00u, 14);
      for (int i = 0; i < 50; i++)
        {
          int x = (int) (rng32 (&s) % (unsigned) (g_w / 2));
          int y = (int) (rng32 (&s) % (unsigned) (g_h - 64)) + 32;
          _swix (Font_Paint, _INR (0, 4), font_h, font_text, (1 << 4) | (1 << 8),
                 x, y);
        }
    }
  return s;
}

/* Draw: a 10-pointed star of Bezier curves, about 200 pixels across,
   filled, and stroked 2 pixels thick (Draw module, as !Draw renders) */
static int draw_path[200];

static int
draw_setup (void)
{
  int *p = draw_path, r = (100 << g_xeig) * 256;
  static const int pts[10][2] = { {0, 1000}, {294, 405}, {951, 309}, {476, -155},
    {588, -809}, {0, -500}, {-588, -809}, {-476, -155}, {-951, 309}, {-294, 405} };
  *p++ = 2, *p++ = pts[0][0] * r / 1000, *p++ = pts[0][1] * r / 1000;
  for (int i = 1; i <= 10; i++)
    {
      const int *a = pts[i - 1], *b = pts[i % 10];
      *p++ = 6;
      *p++ = (a[0] * 2 + b[0]) / 3 * r / 1000 + r / 8;
      *p++ = (a[1] * 2 + b[1]) / 3 * r / 1000 + r / 8;
      *p++ = (a[0] + b[0] * 2) / 3 * r / 1000 - r / 8;
      *p++ = (a[1] + b[1] * 2) / 3 * r / 1000 - r / 8;
      *p++ = b[0] * r / 1000, *p++ = b[1] * r / 1000;
    }
  *p++ = 5;
  *p++ = 0, *p++ = 0;
  return 0;
}

static uint32_t
draw_common (uint32_t iters, int stroke)
{
  uint32_t s = 8;
  int m[6] = { 1 << 16, 0, 0, 1 << 16, 0, 0 };
  int capjoin[4] = { 0, 0, 0, 0 };
  int margin = 120 << g_xeig;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < 50; i++)
      {
        m[4] = (int) ((rng32 (&s) % (unsigned) (g_w - 2 * margin)) + margin) * 256;
        m[5] = (int) ((rng32 (&s) % (unsigned) (g_h - 2 * margin)) + margin) * 256;
        gcol (&s);
        if (stroke)
          _swix (Draw_Stroke, _INR (0, 6), draw_path, 0, m, 0,
                 (2 << g_xeig) * 256, capjoin, 0);
        else
          _swix (Draw_Fill, _INR (0, 3), draw_path, 0, m, 0);
      }
  return s;
}

static uint32_t gfx_drawfill (uint32_t it) { return draw_common (it, 0); }
static uint32_t gfx_drawstroke (uint32_t it) { return draw_common (it, 1); }

/* JPEG: SpriteExtend decoding and plotting a 640 x 480 photo-like
   image (JPEG_PlotScaled, as !Paint and browsers use it) */
#include "jpegdata.h"

static int
jpeg_setup (void)
{
  int w = 0, h = 0;
  if (_swix (JPEG_Info, _INR (0, 2) | _OUTR (2, 3), 1, jpeg_data,
             (int) sizeof jpeg_data, &w, &h) || w != JPEG_W || h != JPEG_H)
    return -1;
  return 0;
}

static uint32_t
gfx_jpeg (uint32_t iters)
{
  uint32_t s = 9;
  int jw = JPEG_W << g_xeig, jh = JPEG_H << g_yeig;
  for (uint32_t it = 0; it < iters; it++)
    {
      int x = g_w > jw ? (int) (rng32 (&s) % (unsigned) (g_w - jw)) : 0;
      int y = g_h > jh ? (int) (rng32 (&s) % (unsigned) (g_h - jh)) : 0;
      _swix (JPEG_PlotScaled, _INR (0, 5), jpeg_data, x, y, 0,
             (int) sizeof jpeg_data, 0);
    }
  return s;
}

const test_t os_tests[] = {
  {"os.swi", "SWI call (XOS_ReadMonotonicTime)", "kSWI/s", 1,
   NULL, swi_raw, NULL, NULL, T_RISCOS},
  {"os.readvar", "OS_ReadVarVal through _swix", "kcalls/s", 1,
   NULL, swi_var, NULL, NULL, T_RISCOS},
  {"io.fwrite", "fwrite 8 MB file (64 KB blocks)", "MB/s", IO_SIZE / MB,
   io_setup, io_fwrite, NULL, io_teardown, T_RISCOS | T_IO},
  {"io.fread", "fread 8 MB file (64 KB blocks)", "MB/s", IO_SIZE / MB,
   io_setup, io_fread, NULL, io_teardown, T_RISCOS | T_IO},
  {"io.save", "OS_File save 8 MB", "MB/s", IO_SIZE / MB,
   io_setup, io_save, NULL, io_teardown, T_RISCOS | T_IO},
  {"io.load", "OS_File load 8 MB", "MB/s", IO_SIZE / MB,
   io_setup, io_load, NULL, io_teardown, T_RISCOS | T_IO},
  {"io.bput", "OS_BPut byte stream out", "KB/s", IO_BYTES / 1024.0,
   io_setup, io_bput, NULL, io_teardown, T_RISCOS | T_IO},
  {"io.bget", "OS_BGet byte stream in", "KB/s", IO_BYTES / 1024.0,
   io_setup, io_bget, NULL, io_teardown, T_RISCOS | T_IO},
  {"io.smallfiles", "Create + delete 1 KB files", "files/s", IO_SMALL,
   io_setup, io_small, NULL, io_teardown, T_RISCOS | T_IO},
  {"gfx.rect", "Filled rectangles 128x128, ColourTrans", "Mpixel/s",
   RECTS * RECT_PX * RECT_PX / 1e6, NULL, gfx_rect, NULL, NULL,
   T_RISCOS | T_GFX},
  {"gfx.line", "Lines, random end points", "Klines/s", 1,
   NULL, gfx_line, NULL, NULL, T_RISCOS | T_GFX},
  {"gfx.copy", "Block copy 256x256 (PLOT 190)", "Mpixel/s",
   COPIES * COPY_PX * COPY_PX / 1e6, NULL, gfx_copy, NULL, NULL,
   T_RISCOS | T_GFX},
  {"gfx.sprite", "Sprite plot 128x128 (OS_SpriteOp 34)", "Mpixel/s",
   SPRITES * SPR_PX * SPR_PX / 1e6, spr_setup, gfx_sprite, NULL, spr_teardown,
   T_RISCOS | T_GFX},
  {"gfx.spritescale", "Sprite plot scaled x2 (OS_SpriteOp 52)", "Mpixel/s",
   SPRITES / 4 * 4.0 * SPR_PX * SPR_PX / 1e6, spr_setup, gfx_sprite_scaled,
   NULL, spr_teardown, T_RISCOS | T_GFX},
  {"gfx.font", "Font_Paint 24pt anti-aliased", "Kchars/s",
   50 * (sizeof font_text - 1) / 1e3, font_setup, gfx_font, NULL,
   font_teardown, T_RISCOS | T_GFX},
  {"gfx.drawfill", "Draw_Fill Bezier star, 200 px", "Kfills/s", 0.05,
   draw_setup, gfx_drawfill, NULL, NULL, T_RISCOS | T_GFX},
  {"gfx.drawstroke", "Draw_Stroke Bezier star, 2 px", "Kstrokes/s", 0.05,
   draw_setup, gfx_drawstroke, NULL, NULL, T_RISCOS | T_GFX},
  {"gfx.jpeg", "JPEG_PlotScaled 640x480 photo", "Mpixel/s",
   JPEG_W * JPEG_H / 1e6, jpeg_setup, gfx_jpeg, NULL, NULL, T_RISCOS | T_GFX},
};
const size_t n_os_tests = sizeof os_tests / sizeof os_tests[0];

/* ------------------------------------------------------------------ */

static void
module_version (const char *name, char *out, size_t len)
{
  int base = 0;
  out[0] = 0;
  if (_swix (OS_Module, _INR (0, 1) | _OUT (3), 18, name, &base) || !base)
    {
      snprintf (out, len, "-");
      return;
    }
  const char *help = (const char *) base + *(int *) (base + 0x14);
  /* "Name\t\t0.91 (dd Mmm yyyy)": the version is after the tabs */
  const char *p = help;
  while (*p && *p != '\t')
    p++;
  while (*p == '\t' || *p == ' ')
    p++;
  size_t i = 0;
  while (p[i] && p[i] != ' ' && i + 1 < len)
    out[i] = p[i], i++;
  out[i] = 0;
}

void
machine_info (char *buf, size_t len, char *shortname, size_t sl)
{
  char os[128] = "RISC OS", vfp[16], aeabi[16], sul[16];
  _kernel_oserror *e = _swix (OS_Byte, _INR (0, 1), 0, 0);
  if (e)
    snprintf (os, sizeof os, "%s", e->errmess);
  os[strcspn (os, "\r\n")] = 0;
  int page = 0, pages = 0, xwl = 0, ywl = 0, l2bpp = 0;
  _swix (OS_ReadMemMapInfo, _OUTR (0, 1), &page, &pages);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 11, &xwl);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 12, &ywl);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 9, &l2bpp);
  module_version ("VFPSupport", vfp, sizeof vfp);
  module_version ("ARMEABISupport", aeabi, sizeof aeabi);
  module_version ("SharedUnixLibrary", sul, sizeof sul);
  const char *name = getenv ("ROBench$Machine");
  snprintf (buf, len,
            "Machine: %s\nOS: %s; %d MB RAM; screen %dx%d, %d bpp\n"
            "Modules: VFPSupport %s, ARMEABISupport %s, SharedUnixLibrary %s\n",
            name ? name : "(set ROBench$Machine to name it)", os,
            (int) ((long long) page * pages / (1024 * 1024)), xwl + 1, ywl + 1,
            1 << l2bpp, vfp, aeabi, sul);
  snprintf (shortname, sl, "%s", name ? name : os);
}

#endif /* __riscos */
