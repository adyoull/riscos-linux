/* RiSH, the RISC Shell: a Linux terminal for the RISC OS desktop, on RISC
 * OS on Linux.
 *
 * Each window is a Linux terminal (xterm-256color as far as programs can
 * tell): a shell as RISC OS's own Linux user, or, from the icon bar menu,
 * a root shell after the root password. Full-screen programs (nano, vi,
 * top, less) work; the window can be resized, and what scrolls off the
 * top is kept (the scroll bar). Text is selected by dragging (Adjust
 * extends), copied to RISC OS's clipboard and pasted from it (the menu,
 * or Ctrl-Shift-C and Ctrl-Shift-V).
 *
 * The Linux end is rol-cmd's terminal job (images/fe/rol-cmd.c): a
 * request file in /riscos/cmd asks for a shell on a pty; what the pty
 * shows grows NAME.out, which RiSH reads as it grows; what is typed is
 * appended to NAME.in; NAME.size carries a new window size, NAME.trim how
 * much of NAME.out has been read (its disc space is given back), and
 * NAME.kill ends the shell when the window is closed. The files are
 * reached with Linux system calls (IXSupport_LinuxSyscall).
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

#define VERSION "0.04 (11 Nov 2026)"

#define IXSupport_LinuxSyscall	0x59EC0
#define NR_read		3
#define NR_write	4
#define NR_open		5
#define NR_close	6
#define NR_unlink	10
#define NR_getpid	20
#define NR_access	33
#define NR_rename	38
#define NR_pread64	180
#define LO_WRONLY	1
#define LO_CREAT	0100
#define LO_TRUNC	01000
#define LO_APPEND	02000
#define DIR		"/riscos/cmd/"
#define RC_AUTH		77
#define RC_NOPASS	78
#define RC_WAIT		79

#define MAXCOLS	250
#define MAXROWS	100
#define HMAX	1000		/* lines kept above the screen */
#define MAXTERMS 6

typedef struct { unsigned char ch, fg, bg, at; } Cell;
enum { A_BOLD = 1, A_UL = 2, A_REV = 4, A_DFG = 8, A_DBG = 16 };
/* ch 1 to 15: a box-drawing character, the bits its arms (left 1, right
   2, up 4, down 8); 16 a solid block, 17 a shaded one, 18 a bullet */

enum { P_GROUND, P_ESC, P_CSI, P_OSC, P_OSCESC, P_STR, P_STRESC, P_SKIP1, P_CHARSET };
enum { S_RUN, S_ASK, S_DEAD };

typedef struct Term
{
  int used, win, root;
  char title[100];
  int cols, rows;
  Cell *scr[MAXROWS], *alt[MAXROWS], **cur;
  Cell *hist[HMAX];
  int hhead, hcount;
  int cx, cy, wrapnext;
  Cell pen;
  int sx, sy;
  Cell spen;
  int top, bot;
  int m_wrap, m_insert, m_origin, m_curs, m_appkeys, m_alt, m_bpaste;
  /* the selection: from (line, column) to just before (line, column),
     lines numbered from the first ever shown (so they keep their numbers
     as they scroll into the history) */
  unsigned scrolled;		/* lines that have gone into the history */
  int sel, dragging;
  unsigned sa_l, se_l;
  int sa_c, se_c;
  int g[2], gl, csel;
  int st, par[16], npar, priv, inter;
  char osc[100];
  int oscn;
  int u8need;
  unsigned u8cp;
  int lastch;
  unsigned char drow[MAXROWS];
  int dall, grew, focus;
  /* the job */
  int state;
  char name[24];
  int outfd;
  long long pos, trimmed;
  unsigned started, lastrc;
  int gotout;
  /* the password prompts (a root terminal) */
  int ask;			/* 1 password, 2 new password, 3 again */
  char line[100], first[100];
  int linen, tries;
} Term;

static Term *terms[MAXTERMS];
static int task, iconbar = -1, quit;
static int xeig = 1, yeig = 1, scrw = 1280, scrh = 800;	/* (pixels) */
static int scale = 1, CW = 16, CH = 32, dx = 2, dy = 2;
static int maxrows = 40, maxcols = 80;
static char token[40];
static unsigned jobcount;
static int b[512];		/* the Wimp's block */
static int menu[80];
static Term *menu_term;
static unsigned pal[256];	/* &BBGGRR00 */
static char *clip;		/* the clipboard's text, while it is ours */
static int clipn, clip_ours;
static Term *paste_to;		/* the terminal a paste was asked for */
#define DEF_FG 0xC8C8C800u
#define DEF_BG 0x00000000u

/* --- Linux ---------------------------------------------------------------- */

static int
lsys (int nr, unsigned a, unsigned c, unsigned d, unsigned e, unsigned f, unsigned g)
{
  int r;
  if (_swix (IXSupport_LinuxSyscall, _INR (0, 7) | _OUT (0), a, c, d, e, f, g, 0, nr, &r))
    return -38;
  return r;
}

static unsigned
now_cs (void)
{
  unsigned t = 0;
  _swix (OS_ReadMonotonicTime, _OUT (0), &t);
  return t;
}

static void
jpath (char *p, const Term *t, const char *ext)
{
  sprintf (p, DIR "%s.%s", t->name, ext);
}

/* a small file, written whole: as NAME.TMP, then renamed NAME.EXT */
static void
put_file (const Term *t, const char *tmp, const char *ext, const char *s, int n)
{
  char a[80], c[80];
  jpath (a, t, tmp);
  jpath (c, t, ext);
  int f = lsys (NR_open, (unsigned) a, LO_WRONLY | LO_CREAT | LO_TRUNC, 0600, 0, 0, 0);
  if (f < 0)
    return;
  lsys (NR_write, (unsigned) f, (unsigned) s, (unsigned) n, 0, 0, 0);
  lsys (NR_close, (unsigned) f, 0, 0, 0, 0, 0);
  lsys (NR_rename, (unsigned) a, (unsigned) c, 0, 0, 0, 0);
}

/* what is typed, for the Linux terminal */
static void
send (Term *t, const char *s, int n)
{
  if (t->state != S_RUN || n <= 0)
    return;
  char p[80];
  jpath (p, t, "in");
  int f = lsys (NR_open, (unsigned) p, LO_WRONLY | LO_CREAT | LO_APPEND, 0644, 0, 0, 0);
  if (f < 0)
    return;
  lsys (NR_write, (unsigned) f, (unsigned) s, (unsigned) n, 0, 0, 0);
  lsys (NR_close, (unsigned) f, 0, 0, 0, 0, 0);
}

static void
sends (Term *t, const char *s)
{
  send (t, s, (int) strlen (s));
}

/* --- the screen ----------------------------------------------------------- */

static Cell
blank (const Term *t)
{
  Cell c = { ' ', t->pen.fg, t->pen.bg, (unsigned char) ((t->pen.at & A_DBG) | A_DFG) };
  return c;
}

static void
clear_cells (Term *t, Cell *l, int from, int to)	/* [from, to) */
{
  Cell c = blank (t);
  for (int i = from; i < to && i < MAXCOLS; i++)
    l[i] = c;
}

static Cell *
new_line (void)
{
  Cell *l = malloc (MAXCOLS * sizeof (Cell));
  if (l)
    {
      Cell c = { ' ', 0, 0, A_DFG | A_DBG };
      for (int i = 0; i < MAXCOLS; i++)
	l[i] = c;
    }
  return l;
}

static void
dirty (Term *t, int row)
{
  if (row >= 0 && row < MAXROWS)
    t->drow[row] = 1;
}

static void
scroll_up (Term *t, int top, int bot, int n)
{
  while (n-- > 0)
    {
      Cell *l = t->cur[top];
      if (t->cur == t->scr && top == 0)
	{
	  /* the top line goes into the history */
	  Cell *nl = t->hcount < HMAX ? new_line () : 0;
	  if (nl)
	    {
	      t->hist[(t->hhead + t->hcount) % HMAX] = l;
	      t->hcount++;
	      t->scrolled++;
	      l = nl;
	      t->grew = 1;
	    }
	  else if (t->hcount)
	    {
	      Cell *old = t->hist[t->hhead];
	      t->hist[t->hhead] = l;
	      t->hhead = (t->hhead + 1) % HMAX;
	      t->scrolled++;
	      l = old;
	    }
	}
      memmove (&t->cur[top], &t->cur[top + 1], (size_t) (bot - top) * sizeof (Cell *));
      t->cur[bot] = l;
      clear_cells (t, l, 0, MAXCOLS);
    }
  t->dall = 1;
}

static void
scroll_down (Term *t, int top, int bot, int n)
{
  while (n-- > 0)
    {
      Cell *l = t->cur[bot];
      memmove (&t->cur[top + 1], &t->cur[top], (size_t) (bot - top) * sizeof (Cell *));
      t->cur[top] = l;
      clear_cells (t, l, 0, MAXCOLS);
    }
  t->dall = 1;
}

static void
linefeed (Term *t)
{
  if (t->cy == t->bot)
    scroll_up (t, t->top, t->bot, 1);
  else if (t->cy < t->rows - 1)
    {
      dirty (t, t->cy);
      t->cy++;
      dirty (t, t->cy);
    }
}

static void
move_to (Term *t, int x, int y)
{
  int lo = t->m_origin ? t->top : 0, hi = t->m_origin ? t->bot : t->rows - 1;
  if (t->m_origin)
    y += t->top;
  if (x < 0)
    x = 0;
  if (x >= t->cols)
    x = t->cols - 1;
  if (y < lo)
    y = lo;
  if (y > hi)
    y = hi;
  dirty (t, t->cy);
  t->cx = x;
  t->cy = y;
  t->wrapnext = 0;
  dirty (t, y);
}

static void
put_cell (Term *t, unsigned ch)
{
  if (t->wrapnext && t->m_wrap)
    {
      t->cx = 0;
      linefeed (t);
    }
  t->wrapnext = 0;
  Cell *l = t->cur[t->cy];
  if (t->m_insert)
    memmove (&l[t->cx + 1], &l[t->cx], (size_t) (t->cols - t->cx - 1) * sizeof (Cell));
  Cell c = t->pen;
  c.ch = (unsigned char) ch;
  l[t->cx] = c;
  dirty (t, t->cy);
  t->lastch = (int) ch;
  if (t->cx == t->cols - 1)
    t->wrapnext = 1;
  else
    t->cx++;
}

static void
reset_term (Term *t)
{
  Cell d = { ' ', 0, 0, A_DFG | A_DBG };
  t->pen = t->spen = d;
  t->cur = t->scr;
  t->m_alt = 0;
  for (int r = 0; r < MAXROWS; r++)
    {
      clear_cells (t, t->scr[r], 0, MAXCOLS);
      clear_cells (t, t->alt[r], 0, MAXCOLS);
    }
  t->cx = t->cy = t->sx = t->sy = t->wrapnext = 0;
  t->top = 0;
  t->bot = t->rows - 1;
  t->m_wrap = 1;
  t->m_insert = t->m_origin = t->m_appkeys = t->m_bpaste = 0;
  t->sel = t->dragging = 0;
  t->m_curs = 1;
  t->g[0] = t->g[1] = t->gl = 0;
  t->st = P_GROUND;
  t->u8need = 0;
  t->dall = 1;
}

/* a character outside ASCII, as this font and the box characters have it */
static unsigned
map_cp (unsigned cp)
{
  static const unsigned char box[] = {	/* U+2500 to U+254B */
    3,3,12,12, 3,3,12,12, 3,3,12,12, 10,10,10,10, 9,9,9,9, 6,6,6,6, 5,5,5,5,
    14,14,14,14,14,14,14,14, 13,13,13,13,13,13,13,13, 11,11,11,11,11,11,11,11,
    7,7,7,7,7,7,7,7, 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15 };
  static const unsigned char dbl[] = {	/* U+2550 to U+2570 */
    3,12, 10,10,10, 9,9,9, 6,6,6, 5,5,5, 14,14,14, 13,13,13, 11,11,11, 7,7,7,
    15,15,15, 10,9,5,6 };
  if (cp < 0x80)
    return cp;
  if (cp >= 0xA0 && cp < 0x100)
    return cp;
  if (cp >= 0x2500 && cp <= 0x254B)
    return box[cp - 0x2500];
  if (cp >= 0x2550 && cp <= 0x2570)
    return dbl[cp - 0x2550];
  if (cp >= 0x2574 && cp <= 0x257F)
    return (cp & 1) ? 12 : 3;
  if (cp == 0x2588 || (cp >= 0x2580 && cp <= 0x2590))
    return 16;
  if (cp >= 0x2591 && cp <= 0x2593)
    return 17;
  switch (cp)
    {
    case 0x20AC: return 0x80;
    case 0x2026: return 0x8C;
    case 0x2122: return 0x8D;
    case 0x2022: case 0x25CF: case 0x25A0: case 0x25C6: return 18;
    case 0x2018: return 0x90;
    case 0x2019: return 0x91;
    case 0x201C: return 0x94;
    case 0x201D: return 0x95;
    case 0x2013: case 0x2012: return 0x97;
    case 0x2014: case 0x2015: return 0x98;
    case 0x2212: return '-';
    case 0x2190: return '<';
    case 0x2192: return '>';
    case 0x2191: case 0x25B2: return '^';
    case 0x2193: case 0x25BC: return 'v';
    case 0x2713: case 0x2714: return '+';
    case 0x00A0: return ' ';
    }
  return '?';
}

/* the VT100's line-drawing set (ESC ( 0) */
static unsigned
map_dec (unsigned c)
{
  switch (c)
    {
    case 'q': case 'o': case 'p': case 'r': case 's': return 3;
    case 'x': return 12;
    case 'l': return 10;
    case 'k': return 9;
    case 'm': return 6;
    case 'j': return 5;
    case 't': return 14;
    case 'u': return 13;
    case 'w': return 11;
    case 'v': return 7;
    case 'n': return 15;
    case 'a': case 'h': return 17;
    case '0': return 16;
    case '`': case '~': return 18;
    case 'f': return 0xB0;
    case 'g': return 0xB1;
    case 'y': case ',': return '<';
    case 'z': case '+': return '>';
    case '}': return 0xA3;
    case '-': return '^';
    case '.': return 'v';
    case '{': return 'p';
    case '|': return '#';
    case 'i': return '#';
    }
  return c;
}

static int
arg (const Term *t, int i, int def)
{
  return i < t->npar && t->par[i] > 0 ? t->par[i] : def;
}

static void
set_alt (Term *t, int on, int clear)
{
  if (on == t->m_alt)
    return;
  t->m_alt = on;
  t->sel = t->dragging = 0;
  t->cur = on ? t->alt : t->scr;
  if (on && clear)
    for (int r = 0; r < MAXROWS; r++)
      clear_cells (t, t->alt[r], 0, MAXCOLS);
  t->dall = 1;
}

static void
set_mode (Term *t, int on)
{
  for (int i = 0; i < t->npar; i++)
    {
      int p = t->par[i];
      if (t->priv == '?')
	switch (p)
	  {
	  case 1: t->m_appkeys = on; break;
	  case 6: t->m_origin = on; move_to (t, 0, 0); break;
	  case 7: t->m_wrap = on; break;
	  case 25: t->m_curs = on; dirty (t, t->cy); break;
	  case 2004: t->m_bpaste = on; break;
	  case 47: case 1047: set_alt (t, on, 1); break;
	  case 1048:
	    if (on)
	      t->sx = t->cx, t->sy = t->cy, t->spen = t->pen;
	    else
	      {
		t->pen = t->spen;
		move_to (t, t->sx, t->sy);
	      }
	    break;
	  case 1049:
	    if (on)
	      {
		t->sx = t->cx, t->sy = t->cy, t->spen = t->pen;
		set_alt (t, 1, 1);
	      }
	    else
	      {
		set_alt (t, 0, 0);
		t->pen = t->spen;
		t->m_origin = 0;
		move_to (t, t->sx, t->sy);
	      }
	    break;
	  }
      else if (p == 4)
	t->m_insert = on;
    }
}

static void
sgr (Term *t)
{
  if (t->npar == 0)
    t->par[t->npar++] = 0;
  for (int i = 0; i < t->npar; i++)
    {
      int p = t->par[i];
      if (p == 0)
	{
	  t->pen.at = A_DFG | A_DBG;
	  t->pen.fg = t->pen.bg = 0;
	}
      else if (p == 1)
	t->pen.at |= A_BOLD;
      else if (p == 4)
	t->pen.at |= A_UL;
      else if (p == 7)
	t->pen.at |= A_REV;
      else if (p == 22)
	t->pen.at &= (unsigned char) ~A_BOLD;
      else if (p == 24)
	t->pen.at &= (unsigned char) ~A_UL;
      else if (p == 27)
	t->pen.at &= (unsigned char) ~A_REV;
      else if (p >= 30 && p <= 37)
	t->pen.fg = (unsigned char) (p - 30), t->pen.at &= (unsigned char) ~A_DFG;
      else if (p >= 40 && p <= 47)
	t->pen.bg = (unsigned char) (p - 40), t->pen.at &= (unsigned char) ~A_DBG;
      else if (p >= 90 && p <= 97)
	t->pen.fg = (unsigned char) (p - 90 + 8), t->pen.at &= (unsigned char) ~A_DFG;
      else if (p >= 100 && p <= 107)
	t->pen.bg = (unsigned char) (p - 100 + 8), t->pen.at &= (unsigned char) ~A_DBG;
      else if (p == 39)
	t->pen.at |= A_DFG;
      else if (p == 49)
	t->pen.at |= A_DBG;
      else if (p == 38 || p == 48)
	{
	  int col = -1;
	  if (i + 2 < t->npar && t->par[i + 1] == 5)
	    {
	      col = t->par[i + 2] & 255;
	      i += 2;
	    }
	  else if (i + 4 < t->npar && t->par[i + 1] == 2)
	    {
	      int r = t->par[i + 2] & 255, g = t->par[i + 3] & 255, bl = t->par[i + 4] & 255;
	      col = 16 + 36 * ((r * 5 + 127) / 255) + 6 * ((g * 5 + 127) / 255) + (bl * 5 + 127) / 255;
	      i += 4;
	    }
	  else
	    break;
	  if (p == 38)
	    t->pen.fg = (unsigned char) col, t->pen.at &= (unsigned char) ~A_DFG;
	  else
	    t->pen.bg = (unsigned char) col, t->pen.at &= (unsigned char) ~A_DBG;
	}
    }
}

static void
csi (Term *t, int f)
{
  int n = arg (t, 0, 1);
  Cell *l = t->cur[t->cy];
  if (t->priv == '>' || t->priv == '=')
    {
      if (f == 'c' && t->priv == '>')
	sends (t, "\033[>0;10;0c");
      return;
    }
  if (t->inter)
    return;			/* (cursor shapes and the like) */
  switch (f)
    {
    case '@':
      if (n > t->cols - t->cx)
	n = t->cols - t->cx;
      memmove (&l[t->cx + n], &l[t->cx], (size_t) (t->cols - t->cx - n) * sizeof (Cell));
      clear_cells (t, l, t->cx, t->cx + n);
      dirty (t, t->cy);
      break;
    case 'A':
      {
	int lo = t->cy >= t->top ? t->top : 0, y = t->cy - n;
	dirty (t, t->cy);
	t->cy = y < lo ? lo : y;
	t->wrapnext = 0;
	dirty (t, t->cy);
      }
      break;
    case 'B': case 'e':
      {
	int hi = t->cy <= t->bot ? t->bot : t->rows - 1, y = t->cy + n;
	dirty (t, t->cy);
	t->cy = y > hi ? hi : y;
	t->wrapnext = 0;
	dirty (t, t->cy);
      }
      break;
    case 'C': case 'a':
      t->cx = t->cx + n >= t->cols ? t->cols - 1 : t->cx + n;
      t->wrapnext = 0;
      dirty (t, t->cy);
      break;
    case 'D':
      t->cx = t->cx - n < 0 ? 0 : t->cx - n;
      t->wrapnext = 0;
      dirty (t, t->cy);
      break;
    case 'E':
      move_to (t, 0, t->cy + n - (t->m_origin ? t->top : 0));
      break;
    case 'F':
      move_to (t, 0, t->cy - n - (t->m_origin ? t->top : 0));
      break;
    case 'G': case '`':
      t->cx = n > t->cols ? t->cols - 1 : n - 1;
      t->wrapnext = 0;
      dirty (t, t->cy);
      break;
    case 'd':
      move_to (t, t->cx, n - 1);
      break;
    case 'H': case 'f':
      move_to (t, arg (t, 1, 1) - 1, n - 1);
      break;
    case 'I':
      while (n-- > 0 && t->cx < t->cols - 1)
	t->cx = (t->cx | 7) + 1 >= t->cols ? t->cols - 1 : (t->cx | 7) + 1;
      dirty (t, t->cy);
      break;
    case 'Z':
      while (n-- > 0 && t->cx > 0)
	t->cx = (t->cx - 1) & ~7;
      dirty (t, t->cy);
      break;
    case 'J':
      n = arg (t, 0, 0);
      if (n == 0)
	{
	  clear_cells (t, l, t->cx, MAXCOLS);
	  for (int r = t->cy + 1; r < t->rows; r++)
	    clear_cells (t, t->cur[r], 0, MAXCOLS);
	}
      else if (n == 1)
	{
	  clear_cells (t, l, 0, t->cx + 1);
	  for (int r = 0; r < t->cy; r++)
	    clear_cells (t, t->cur[r], 0, MAXCOLS);
	}
      else if (n == 2)
	for (int r = 0; r < t->rows; r++)
	  clear_cells (t, t->cur[r], 0, MAXCOLS);
      else if (n == 3)
	{
	  /* forget the history */
	  for (int i = 0; i < t->hcount; i++)
	    free (t->hist[(t->hhead + i) % HMAX]);
	  t->hcount = t->hhead = 0;
	  t->sel = t->dragging = 0;
	  t->grew = 1;
	}
      t->dall = 1;
      break;
    case 'K':
      n = arg (t, 0, 0);
      clear_cells (t, l, n == 0 ? t->cx : 0, n == 1 ? t->cx + 1 : MAXCOLS);
      dirty (t, t->cy);
      break;
    case 'L':
      if (t->cy >= t->top && t->cy <= t->bot)
	scroll_down (t, t->cy, t->bot, n > t->bot - t->cy + 1 ? t->bot - t->cy + 1 : n);
      break;
    case 'M':
      if (t->cy >= t->top && t->cy <= t->bot)
	{
	  /* (lines deleted in the middle of the screen aren't history) */
	  Cell **keep = t->cur;
	  int k = n > t->bot - t->cy + 1 ? t->bot - t->cy + 1 : n;
	  if (t->cy == 0 && keep == t->scr)
	    {
	      while (k-- > 0)
		{
		  Cell *x = t->cur[0];
		  memmove (&t->cur[0], &t->cur[1], (size_t) t->bot * sizeof (Cell *));
		  t->cur[t->bot] = x;
		  clear_cells (t, x, 0, MAXCOLS);
		}
	      t->dall = 1;
	    }
	  else
	    scroll_up (t, t->cy, t->bot, k);
	}
      break;
    case 'P':
      if (n > t->cols - t->cx)
	n = t->cols - t->cx;
      memmove (&l[t->cx], &l[t->cx + n], (size_t) (t->cols - t->cx - n) * sizeof (Cell));
      clear_cells (t, l, t->cols - n, t->cols);
      dirty (t, t->cy);
      break;
    case 'S':
      scroll_up (t, t->top, t->bot, n > t->rows ? t->rows : n);
      break;
    case 'T':
      scroll_down (t, t->top, t->bot, n > t->rows ? t->rows : n);
      break;
    case 'X':
      clear_cells (t, l, t->cx, t->cx + n > t->cols ? t->cols : t->cx + n);
      dirty (t, t->cy);
      break;
    case 'b':
      if (t->lastch)
	{
	  int ch = t->lastch;
	  while (n-- > 0)
	    put_cell (t, (unsigned) ch);
	}
      break;
    case 'c':
      if (arg (t, 0, 0) == 0)
	sends (t, "\033[?1;2c");
      break;
    case 'h':
      set_mode (t, 1);
      break;
    case 'l':
      set_mode (t, 0);
      break;
    case 'm':
      sgr (t);
      break;
    case 'n':
      if (t->par[0] == 5)
	sends (t, "\033[0n");
      else if (t->par[0] == 6)
	{
	  char r[32];
	  sprintf (r, "\033[%d;%dR", t->cy - (t->m_origin ? t->top : 0) + 1, t->cx + 1);
	  sends (t, r);
	}
      break;
    case 'r':
      {
	int a = arg (t, 0, 1) - 1, c = arg (t, 1, t->rows) - 1;
	if (c >= t->rows)
	  c = t->rows - 1;
	if (a < c)
	  {
	    t->top = a;
	    t->bot = c;
	    move_to (t, 0, 0);
	  }
      }
      break;
    case 's':
      t->sx = t->cx, t->sy = t->cy, t->spen = t->pen;
      break;
    case 'u':
      t->pen = t->spen;
      move_to (t, t->sx, t->sy - (t->m_origin ? t->top : 0));
      break;
    }
}

static void set_title (Term *t, const char *s);

static void
osc_end (Term *t)
{
  t->osc[t->oscn] = 0;
  if ((t->osc[0] == '0' || t->osc[0] == '2') && t->osc[1] == ';')
    set_title (t, t->osc + 2);
}

static void
control (Term *t, unsigned c)
{
  switch (c)
    {
    case 7:
      _swix (OS_WriteI + 7, 0);
      break;
    case 8:
      if (t->cx > 0)
	t->cx--;
      t->wrapnext = 0;
      dirty (t, t->cy);
      break;
    case 9:
      t->cx = (t->cx | 7) + 1 >= t->cols ? t->cols - 1 : (t->cx | 7) + 1;
      dirty (t, t->cy);
      break;
    case 10: case 11: case 12:
      t->wrapnext = 0;
      linefeed (t);
      break;
    case 13:
      t->cx = 0;
      t->wrapnext = 0;
      dirty (t, t->cy);
      break;
    case 14:
      t->gl = 1;
      break;
    case 15:
      t->gl = 0;
      break;
    }
}

/* what the Linux terminal sent */
static void
term_write (Term *t, const unsigned char *s, int n)
{
  for (int i = 0; i < n; i++)
    {
      unsigned c = s[i];
      switch (t->st)
	{
	case P_GROUND:
	  if (c == 27)
	    {
	      t->st = P_ESC;
	      t->u8need = 0;
	    }
	  else if (c < 32)
	    control (t, c);
	  else if (c == 127)
	    ;
	  else if (t->u8need)
	    {
	      if ((c & 0xC0) == 0x80)
		{
		  t->u8cp = (t->u8cp << 6) | (c & 0x3F);
		  if (--t->u8need == 0)
		    {
		      /* (combining marks and zero-width characters: nothing) */
		      if (!((t->u8cp >= 0x300 && t->u8cp < 0x370) || (t->u8cp >= 0x200B && t->u8cp <= 0x200F) || t->u8cp == 0xFEFF || (t->u8cp >= 0xFE00 && t->u8cp <= 0xFE0F)))
			put_cell (t, map_cp (t->u8cp));
		    }
		}
	      else
		{
		  t->u8need = 0;
		  i--;		/* (not UTF-8 after all: this byte again) */
		}
	    }
	  else if (c >= 0xC2 && c <= 0xDF)
	    t->u8need = 1, t->u8cp = c & 0x1F;
	  else if (c >= 0xE0 && c <= 0xEF)
	    t->u8need = 2, t->u8cp = c & 0x0F;
	  else if (c >= 0xF0 && c <= 0xF4)
	    t->u8need = 3, t->u8cp = c & 0x07;
	  else if (c < 0x80)
	    put_cell (t, t->g[t->gl] ? map_dec (c) : c);
	  else
	    put_cell (t, '?');
	  break;
	case P_ESC:
	  t->st = P_GROUND;
	  switch (c)
	    {
	    case '[':
	      t->st = P_CSI;
	      t->npar = 0;
	      t->priv = t->inter = 0;
	      memset (t->par, 0, sizeof t->par);
	      break;
	    case ']':
	      t->st = P_OSC;
	      t->oscn = 0;
	      break;
	    case 'P': case '^': case '_': case 'X':
	      t->st = P_STR;
	      break;
	    case '(': case ')':
	      t->csel = c == ')';
	      t->st = P_CHARSET;
	      break;
	    case '*': case '+': case '#': case ' ': case '%':
	      t->st = P_SKIP1;
	      break;
	    case '7':
	      t->sx = t->cx, t->sy = t->cy, t->spen = t->pen;
	      break;
	    case '8':
	      t->pen = t->spen;
	      move_to (t, t->sx, t->sy - (t->m_origin ? t->top : 0));
	      break;
	    case 'D':
	      linefeed (t);
	      break;
	    case 'E':
	      t->cx = 0;
	      linefeed (t);
	      break;
	    case 'M':
	      t->wrapnext = 0;
	      if (t->cy == t->top)
		scroll_down (t, t->top, t->bot, 1);
	      else if (t->cy > 0)
		{
		  dirty (t, t->cy);
		  t->cy--;
		  dirty (t, t->cy);
		}
	      break;
	    case 'c':
	      reset_term (t);
	      break;
	    case 27:
	      t->st = P_ESC;
	      break;
	    }
	  break;
	case P_CSI:
	  if (c >= '0' && c <= '9')
	    {
	      if (t->npar == 0)
		t->npar = 1;
	      if (t->par[t->npar - 1] < 100000)
		t->par[t->npar - 1] = t->par[t->npar - 1] * 10 + (int) (c - '0');
	    }
	  else if (c == ';' || c == ':')
	    {
	      if (t->npar == 0)
		t->npar = 1;
	      if (t->npar < 16)
		t->npar++;
	    }
	  else if (c >= '<' && c <= '?')
	    t->priv = (int) c;
	  else if (c >= 0x20 && c <= 0x2F)
	    t->inter = (int) c;
	  else if (c >= 0x40 && c <= 0x7E)
	    {
	      t->st = P_GROUND;
	      csi (t, (int) c);
	    }
	  else if (c == 27)
	    t->st = P_ESC;
	  else if (c < 32)
	    control (t, c);
	  break;
	case P_OSC:
	  if (c == 7)
	    {
	      osc_end (t);
	      t->st = P_GROUND;
	    }
	  else if (c == 27)
	    t->st = P_OSCESC;
	  else if (t->oscn < (int) sizeof t->osc - 1)
	    t->osc[t->oscn++] = (char) c;
	  break;
	case P_OSCESC:
	  osc_end (t);
	  t->st = P_GROUND;
	  if (c != '\\')
	    i--, t->st = P_ESC;
	  break;
	case P_STR:
	  if (c == 27)
	    t->st = P_STRESC;
	  else if (c == 7)
	    t->st = P_GROUND;
	  break;
	case P_STRESC:
	  t->st = c == '\\' ? P_GROUND : P_STR;
	  break;
	case P_SKIP1:
	  t->st = P_GROUND;
	  break;
	case P_CHARSET:
	  t->g[t->csel] = c == '0';
	  t->st = P_GROUND;
	  break;
	}
    }
}

static void
term_puts (Term *t, const char *s)
{
  term_write (t, (const unsigned char *) s, (int) strlen (s));
}

/* --- the window ------------------------------------------------------------ */

static void
read_mode (void)
{
  int xl = 1279, yl = 799;
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 4, &xeig);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 5, &yeig);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 11, &xl);
  _swix (OS_ReadModeVariable, _INR (0, 1) | _OUT (2), -1, 12, &yl);
  scrw = xl + 1;
  scrh = yl + 1;
  dx = 1 << xeig;
  dy = 1 << yeig;
  CW = (8 * scale) << xeig;
  CH = ((yeig >= 2 ? 8 : 16) * scale) << yeig;
  maxcols = (scrw << xeig) / CW;
  if (maxcols > MAXCOLS)
    maxcols = MAXCOLS;
  maxrows = ((scrh << yeig) - 200) / CH;
  if (maxrows > MAXROWS)
    maxrows = MAXROWS;
  if (maxrows < 5)
    maxrows = 5;
}

static void
set_extent (Term *t)
{
  int e[4] = { 0, -(t->hcount + maxrows) * CH, maxcols * CW, 0 };
  _swix (Wimp_SetExtent, _INR (0, 1), t->win, e);
}

static void
set_title (Term *t, const char *s)
{
  int n = 0, k = 0;
  /* a root terminal always says so, whatever the program calls itself */
  if (t->root)
    {
      strcpy (t->title, "ROOT: ");
      n = 6;
    }
  for (; s[k] && n < (int) sizeof t->title - 1; n++, k++)
    t->title[n] = (unsigned char) s[k] < 32 || s[k] == 127 ? ' ' : s[k];
  t->title[n] = 0;
  if (t->win)
    _swix (Wimp_ForceRedraw, _INR (0, 2), t->win, 0x4B534154, 3);
}

static void
send_size (Term *t)
{
  char s[32];
  if (t->state != S_RUN)
    return;
  int n = sprintf (s, "%d %d\n", t->cols, t->rows);
  put_file (t, "snw", "size", s, n);
}

/* the terminal's size follows the window's */
static void
resize (Term *t, int cols, int rows)
{
  if (cols == t->cols && rows == t->rows)
    return;
  t->sel = t->dragging = 0;
  if (rows < t->rows)
    {
      /* lines above the cursor's new place go into the history */
      int over = t->cy - (rows - 1);
      Cell **cur = t->cur;
      if (over > 0)
	{
	  int oldbot = t->rows - 1;
	  t->cur = t->scr;
	  if (!t->m_alt)
	    scroll_up (t, 0, oldbot, over);
	  t->cur = cur;
	  if (t->m_alt)
	    for (int k = 0; k < over; k++)
	      {
		Cell *x = t->alt[0];
		memmove (&t->alt[0], &t->alt[1], (size_t) oldbot * sizeof (Cell *));
		t->alt[oldbot] = x;
	      }
	  t->cy -= over;
	}
      for (int r = rows; r < MAXROWS; r++)
	{
	  clear_cells (t, t->scr[r], 0, MAXCOLS);
	  clear_cells (t, t->alt[r], 0, MAXCOLS);
	}
    }
  t->cols = cols;
  t->rows = rows;
  t->top = 0;
  t->bot = rows - 1;
  if (t->cx >= cols)
    t->cx = cols - 1;
  if (t->cy >= rows)
    t->cy = rows - 1;
  if (t->sx >= cols)
    t->sx = cols - 1;
  if (t->sy >= rows)
    t->sy = rows - 1;
  t->wrapnext = 0;
  t->dall = 1;
  send_size (t);
}

static const Cell *
line_at (const Term *t, int l)	/* l: from the first line of history */
{
  if (l < 0)
    return 0;
  if (l < t->hcount)
    return t->hist[(t->hhead + l) % HMAX];
  l -= t->hcount;
  return l < t->rows ? t->cur[l] : 0;
}

static void
gcol (unsigned c)
{
  _swix (ColourTrans_SetGCOL, _IN (0) | _INR (3, 4), c, 0x100, 0);
}

static void
rect (int x0, int y0, int x1, int y1)	/* x0,y0 inclusive; x1,y1 exclusive */
{
  if (x1 <= x0 || y1 <= y0)
    return;
  _swix (OS_Plot, _INR (0, 2), 4, x0, y0);
  _swix (OS_Plot, _INR (0, 2), 96 + 5, x1 - dx, y1 - dy);
}

/* is the cell at this line (numbered as the selection's are) selected? */
static int
selected (const Term *t, unsigned line, int col)
{
  if (!t->sel)
    return 0;
  unsigned al = t->sa_l, el = t->se_l;
  int ac = t->sa_c, ec = t->se_c;
  if (al > el || (al == el && ac > ec))
    {
      al = t->se_l, el = t->sa_l;
      ac = t->se_c, ec = t->sa_c;
    }
  return (line > al || (line == al && col >= ac)) && (line < el || (line == el && col < ec));
}

static void
colours (const Term *t, Cell c, int cursor, unsigned *fg, unsigned *bg)
{
  if (cursor & 2)
    {
      /* selected text */
      *fg = 0x00000000u;
      *bg = 0xFFC8A000u;
      return;
    }
  unsigned f = (c.at & A_DFG) ? DEF_FG : pal[(c.at & A_BOLD) && c.fg < 8 ? c.fg + 8 : c.fg];
  unsigned g = (c.at & A_DBG) ? DEF_BG : pal[c.bg];
  if ((c.at & A_DFG) && (c.at & A_BOLD))
    f = 0xFFFFFF00u;
  if (c.at & A_REV)
    {
      unsigned x = f;
      f = g;
      g = x;
    }
  if (cursor)
    {
      unsigned x = f;
      f = g;
      g = x;
      if (f == g)
	f = DEF_BG, g = DEF_FG;
      if (t->root)
	f = 0xFFFFFF00u, g = 0x2020E000u;	/* a root terminal's cursor is red */
    }
  *fg = f;
  *bg = g;
}

static void
box_char (int ch, int x, int y)	/* y: the cell's bottom edge */
{
  int th = 2 * scale * dx > CW / 4 ? CW / 4 : 2 * scale * dx;
  int tv = (th / dx) * dy;
  if (th < dx)
    th = dx;
  if (tv < dy)
    tv = dy;
  int mx = x + (CW - th) / 2, my = y + (CH - tv) / 2;
  mx -= mx % dx;
  my -= my % dy;
  if (ch <= 15)
    {
      if (ch & 1)
	rect (x, my, mx + th, my + tv);
      if (ch & 2)
	rect (mx, my, x + CW, my + tv);
      if (ch & 4)
	rect (mx, my, mx + th, y + CH);
      if (ch & 8)
	rect (mx, y, mx + th, my + tv);
    }
  else if (ch == 16)
    rect (x, y, x + CW, y + CH);
  else if (ch == 17)
    for (int yy = y; yy < y + CH; yy += 2 * dy)
      rect (x, yy, x + CW, yy + dy);
  else
    rect (mx - th, my - tv, mx + 2 * th, my + 2 * tv);
}

static void
redraw_rect (Term *t, const int *r)
{
  int ox = r[1] - r[5], oy = r[4] - r[6];
  int l0 = (oy - r[10]) / CH, l1 = (oy - r[8]) / CH;
  int c0 = (r[7] - ox) / CW, c1 = (r[9] - ox) / CW;
  if (l0 < 0)
    l0 = 0;
  if (c0 < 0)
    c0 = 0;
  if (c1 >= MAXCOLS)
    c1 = MAXCOLS - 1;
  for (int l = l0; l <= l1; l++)
    {
      const Cell *line = line_at (t, l);
      int ytop = oy - l * CH, ybot = ytop - CH;
      int crow = l - t->hcount == t->cy && t->m_curs && t->state != S_DEAD ? t->cx : -1;
      unsigned lno = t->scrolled - (unsigned) t->hcount + (unsigned) l;
      if (!line)
	{
	  gcol (DEF_BG);
	  rect (ox + c0 * CW, ybot, ox + (c1 + 1) * CW, ytop);
	  continue;
	}
      int c = c0;
      while (c <= c1)
	{
	  if (c >= t->cols)
	    {
	      gcol (DEF_BG);
	      rect (ox + c * CW, ybot, ox + (c1 + 1) * CW, ytop);
	      break;
	    }
	  /* a run of cells that look alike */
	  unsigned fg, bg, f2, b2;
	  int cur = c == crow && t->focus;
	  int sl = t->sel && selected (t, lno, c) ? 2 : 0;
	  if (sl)
	    cur = 0;
	  colours (t, line[c], cur | sl, &fg, &bg);
	  int e = c + 1;
	  if (!cur)
	    while (e <= c1 && e < t->cols && (e != crow || sl))
	      {
		if ((t->sel && selected (t, lno, e) ? 2 : 0) != sl)
		  break;
		colours (t, line[e], sl, &f2, &b2);
		if (f2 != fg || b2 != bg || ((line[e].at ^ line[c].at) & A_UL))
		  break;
		e++;
	      }
	  int x = ox + c * CW;
	  gcol (bg);
	  rect (x, ybot, ox + e * CW, ytop);
	  gcol (fg);
	  char text[MAXCOLS + 1];
	  int n = 0, any = 0;
	  for (int k = c; k < e; k++)
	    {
	      unsigned ch = line[k].ch;
	      if (ch < 32)
		{
		  box_char ((int) ch, ox + k * CW, ybot);
		  ch = ' ';
		}
	      else if (ch == 127)
		ch = ' ';
	      if (ch != ' ')
		any = 1;
	      text[n++] = (char) ch;
	    }
	  if (any)
	    {
	      _swix (OS_Plot, _INR (0, 2), 4, x, ytop - dy);
	      _swix (OS_WriteN, _INR (0, 1), text, n);
	    }
	  if (line[c].at & A_UL)
	    rect (x, ybot, ox + e * CW, ybot + scale * dy);
	  if (c == crow && !t->focus && !sl)
	    {
	      /* no input focus: the cursor as an outline */
	      gcol (t->root ? 0x2020E000u : DEF_FG);
	      rect (x, ybot, x + CW, ybot + dy);
	      rect (x, ytop - dy, x + CW, ytop);
	      rect (x, ybot, x + dx, ytop);
	      rect (x + CW - dx, ybot, x + CW, ytop);
	    }
	  c = e;
	}
    }
}

/* VDU 5 characters' size (what = 2), spacing (4) or both (6), in pixels */
static void
char_size (int what, int w, int h)
{
  char v[10] = { 23, 17, 7, (char) what, (char) w, (char) (w >> 8), (char) h, (char) (h >> 8), 0, 0 };
  _swix (OS_WriteN, _INR (0, 1), v, 10);
}

static void
redraw (Term *t, int update, const int *area)
{
  int more = 0;
  if (update)
    {
      b[0] = t->win;
      b[1] = area[0]; b[2] = area[1]; b[3] = area[2]; b[4] = area[3];
      if (_swix (Wimp_UpdateWindow, _IN (1) | _OUT (0), b, &more))
	return;
    }
  else if (_swix (Wimp_RedrawWindow, _IN (1) | _OUT (0), b, &more))
    return;
  /* RiSH's characters are 8 by 16 pixels (times the scale); the size and
     spacing other programs' system font text is drawn with are put back
     afterwards, as they were */
  int in[5] = { 162, 163, 164, 165, -1 }, was[5] = { 8, 16, 8, 16, 0 };
  if (more)
    {
      _swix (OS_ReadVduVariables, _INR (0, 1), in, was);
      char_size (6, 8 * scale, (yeig >= 2 ? 8 : 16) * scale);
    }
  int any = more;
  while (more)
    {
      redraw_rect (t, b);
      if (_swix (Wimp_GetRectangle, _IN (1) | _OUT (0), b, &more))
	break;
    }
  if (any)
    {
      char_size (2, was[0], was[1]);
      char_size (4, was[2], was[3]);
    }
}

static void
open_at_bottom (Term *t)
{
  b[0] = t->win;
  if (_swix (Wimp_GetWindowState, _IN (1), b))
    return;
  if (!(b[8] & (1 << 16)))
    return;			/* (not open) */
  b[5] = 0;
  b[6] = -t->hcount * CH;
  _swix (Wimp_OpenWindow, _IN (1), b);
}

/* after the terminal has changed: redraw what did */
static void
refresh (Term *t)
{
  if (!t->win)
    return;
  if (t->grew)
    {
      /* the history grew: the screen is further down the work area */
      set_extent (t);
      open_at_bottom (t);	/* (output brings the view back to the screen) */
      t->grew = 0;
      t->dall = 1;
    }
  int base = -t->hcount * CH;
  if (t->dall)
    {
      int a[4] = { 0, base - t->rows * CH, t->cols * CW, base };
      redraw (t, 1, a);
    }
  else
    for (int r = 0; r < t->rows; r++)
      if (t->drow[r])
	{
	  int e = r;
	  while (e + 1 < t->rows && t->drow[e + 1])
	    e++;
	  int a[4] = { 0, base - (e + 1) * CH, t->cols * CW, base - r * CH };
	  redraw (t, 1, a);
	  r = e;
	}
  t->dall = 0;
  memset (t->drow, 0, sizeof t->drow);
}

static void
give_focus (Term *t)
{
  _swix (Wimp_SetCaretPosition, _INR (0, 5), t->win, -1, 0, 0, 1 << 25, 0);
}

/* --- the job --------------------------------------------------------------- */

static void
start_job (Term *t, const char *header)
{
  char body[400];
  int n = sprintf (body, "@tty %d %d xterm-256color\n%s\n", t->cols, t->rows, header);
  sprintf (t->name, "r%08x%04x", now_cs (), ++jobcount & 0xFFFF);
  put_file (t, "new", "req", body, n);
  memset (body, 0, sizeof body);
  t->state = S_RUN;
  t->pos = t->trimmed = 0;
  t->outfd = -1;
  t->gotout = 0;
  t->started = now_cs ();
}

static void
end_job_files (Term *t)
{
  char p[80];
  if (t->outfd >= 0)
    lsys (NR_close, (unsigned) t->outfd, 0, 0, 0, 0, 0);
  t->outfd = -1;
  static const char *ext[] = { "out", "rc", "in", "tok", "req", 0 };
  for (int i = 0; ext[i]; i++)
    {
      jpath (p, t, ext[i]);
      lsys (NR_unlink, (unsigned) p, 0, 0, 0, 0, 0);
    }
}

static void
ask (Term *t, int what)
{
  t->state = S_ASK;
  t->ask = what;
  t->linen = 0;
  term_puts (t, what == 1 ? "Root password: " : what == 2 ? "New root password: " : "Again: ");
}

static void
root_start (Term *t)
{
  char h[80] = "";
  if (token[0])
    sprintf (h, "@token %s", token);
  start_job (t, h);
}

static void close_term (Term *t);

/* after the password, rol-cmd leaves a token good for the next root
   terminals (until it has gone unused for five minutes) */
static void
take_token (Term *t)
{
  char p[80], tk[40];
  jpath (p, t, "tok");
  int f = lsys (NR_open, (unsigned) p, 0, 0, 0, 0, 0);
  if (f < 0)
    return;
  if (lsys (NR_read, (unsigned) f, (unsigned) tk, 32, 0, 0, 0) == 32)
    {
      memcpy (token, tk, 32);
      token[32] = 0;
    }
  lsys (NR_close, (unsigned) f, 0, 0, 0, 0, 0);
  lsys (NR_unlink, (unsigned) p, 0, 0, 0, 0, 0);
}

/* the job has ended with this exit status */
static void
job_ended (Term *t, int rc)
{
  take_token (t);
  end_job_files (t);
  if (t->root && !t->gotout && (rc == RC_AUTH || rc == RC_NOPASS || rc == RC_WAIT))
    {
      token[0] = 0;
      if (rc == RC_WAIT)
	{
	  term_puts (t, "Too many wrong passwords: wait a minute and try again.\r\n");
	  t->state = S_DEAD;
	}
      else if (rc == RC_NOPASS)
	{
	  term_puts (t, "A root terminal needs Linux's root password. It has none yet: choose one.\r\n");
	  ask (t, 2);
	}
      else if (t->tries >= 4)
	{
	  term_puts (t, "Wrong password.\r\n");
	  t->state = S_DEAD;
	}
      else
	{
	  if (t->tries > 0)
	    term_puts (t, "Wrong password.\r\n");
	  ask (t, 1);
	}
      refresh (t);
      return;
    }
  if (!t->gotout && rc != 0)
    {
      char m[80];
      sprintf (m, "The shell could not be started (status %d).\r\n", rc);
      term_puts (t, m);
      t->state = S_DEAD;
      refresh (t);
      return;
    }
  close_term (t);		/* the shell was left: the window goes */
}

static void
poll_job (Term *t)
{
  static unsigned char buf[16384];
  char p[80];
  if (t->state != S_RUN)
    return;
  if (t->outfd < 0)
    {
      jpath (p, t, "out");
      t->outfd = lsys (NR_open, (unsigned) p, 0, 0, 0, 0, 0);
    }
  int total = 0;
  if (t->outfd >= 0)
    for (;;)
      {
	int n = lsys (NR_pread64, (unsigned) t->outfd, (unsigned) buf, sizeof buf, 0,
		      (unsigned) t->pos, (unsigned) (t->pos >> 32));
	if (n <= 0)
	  break;
	t->pos += n;
	if (!t->gotout && t->root)
	  {
	    take_token (t);
	    term_puts (t, "\033[1;97;41m ROOT TERMINAL: every command here runs with full Linux privileges. \033[0m\r\n"
		       "\033[1mType exit when you have finished.\033[0m\r\n");
	  }
	t->gotout = 1;
	term_write (t, buf, n);
	total += n;
	if (n < (int) sizeof buf || total >= 256 * 1024)
	  break;
      }
  if (total)
    {
      refresh (t);
      if (t->pos - t->trimmed > 4 * 1024 * 1024)
	{
	  char s[32];
	  int n = sprintf (s, "%lld\n", t->pos);
	  put_file (t, "tnw", "trim", s, n);
	  t->trimmed = t->pos;
	}
      return;
    }
  /* nothing new: has it ended? (looked for every quarter second) */
  unsigned now = now_cs ();
  if (now - t->lastrc < 25)
    return;
  t->lastrc = now;
  jpath (p, t, "rc");
  int f = lsys (NR_open, (unsigned) p, 0, 0, 0, 0, 0);
  if (f >= 0)
    {
      char s[16];
      int n = lsys (NR_read, (unsigned) f, (unsigned) s, sizeof s - 1, 0, 0, 0);
      lsys (NR_close, (unsigned) f, 0, 0, 0, 0, 0);
      if (n > 0)
	{
	  s[n] = 0;
	  job_ended (t, atoi (s));
	}
      return;
    }
  if (!t->gotout && t->outfd < 0 && now - t->started > 500)
    {
      jpath (p, t, "req");
      if (lsys (NR_access, (unsigned) p, 0, 0, 0, 0, 0) == 0)
	{
	  end_job_files (t);
	  term_puts (t, "Linux isn't answering (rol-cmd isn't running): no shell.\r\n");
	  t->state = S_DEAD;
	  refresh (t);
	}
    }
}

/* --- windows --------------------------------------------------------------- */

static Term *
find (int win)
{
  for (int i = 0; i < MAXTERMS; i++)
    if (terms[i] && terms[i]->win == win)
      return terms[i];
  return 0;
}

static void
free_term (Term *t)
{
  for (int r = 0; r < MAXROWS; r++)
    {
      free (t->scr[r]);
      free (t->alt[r]);
    }
  for (int i = 0; i < t->hcount; i++)
    free (t->hist[(t->hhead + i) % HMAX]);
  free (t);
}

static void
close_term (Term *t)
{
  if (t->state == S_RUN)
    {
      put_file (t, "knw", "kill", "", 0);
      /* (rol-cmd writes NAME.rc as the shell ends; it tidies files a day
	 old itself, and these are taken now) */
      if (t->outfd >= 0)
	lsys (NR_close, (unsigned) t->outfd, 0, 0, 0, 0, 0);
      t->outfd = -1;
      char p[80];
      jpath (p, t, "in");
      lsys (NR_unlink, (unsigned) p, 0, 0, 0, 0, 0);
      jpath (p, t, "out");
      lsys (NR_unlink, (unsigned) p, 0, 0, 0, 0, 0);
    }
  if (t->win)
    {
      b[0] = t->win;
      _swix (Wimp_DeleteWindow, _IN (1), b);
    }
  for (int i = 0; i < MAXTERMS; i++)
    if (terms[i] == t)
      terms[i] = 0;
  if (paste_to == t)
    paste_to = 0;
  if (menu_term == t)
    menu_term = 0;
  memset (t->line, 0, sizeof t->line);
  memset (t->first, 0, sizeof t->first);
  free_term (t);
}

static void
report (const char *m)
{
  struct { int n; char s[252]; } e;
  e.n = 0;
  strncpy (e.s, m, sizeof e.s - 1);
  e.s[sizeof e.s - 1] = 0;
  _swix (Wimp_ReportError, _INR (0, 2), &e, 1 | 16, "RiSH");
}

static void
new_term (int root)
{
  int slot = -1, count = 0;
  for (int i = 0; i < MAXTERMS; i++)
    if (!terms[i])
      {
	if (slot < 0)
	  slot = i;
      }
    else
      count++;
  if (slot < 0)
    {
      report ("That's as many terminals as RiSH opens at once.");
      return;
    }
  if (lsys (NR_getpid, 0, 0, 0, 0, 0, 0) == -38)
    {
      report ("RiSH needs RISC OS on Linux: there is no Linux under this RISC OS.");
      return;
    }
  Term *t = calloc (1, sizeof (Term));
  int ok = t != 0;
  for (int r = 0; ok && r < MAXROWS; r++)
    {
      t->scr[r] = new_line ();
      t->alt[r] = new_line ();
      ok = t->scr[r] && t->alt[r];
    }
  if (!ok)
    {
      if (t)
	free_term (t);
      report ("Not enough memory for another terminal.");
      return;
    }
  t->used = 1;
  t->root = root;
  t->cols = 80 > maxcols ? maxcols : 80;
  t->rows = 24 > maxrows ? maxrows : 24;
  t->outfd = -1;
  reset_term (t);
  strcpy (t->title, root ? "ROOT: RiSH with full Linux privileges" : "RiSH");

  int *w = b;
  int sw = scrw << xeig, sh = scrh << yeig;
  int x0 = (sw - t->cols * CW) / 2 + (count - 1) * 48, y1 = sh - (sh - t->rows * CH) / 3 - count * 48;
  if (x0 < 0)
    x0 = 0;
  x0 -= x0 % dx;
  y1 -= y1 % dy;
  w[0] = x0; w[1] = y1 - t->rows * CH; w[2] = x0 + t->cols * CW; w[3] = y1;
  w[4] = 0; w[5] = 0; w[6] = -1;
  w[7] = (int) 0xBF000002;	/* moveable; back, close, title, toggle, vertical scroll, size */
  unsigned char *cb = (unsigned char *) &w[8];
  cb[0] = 7; cb[1] = 2; cb[2] = 7; cb[3] = 0xFF; cb[4] = 3; cb[5] = 1; cb[6] = 12; cb[7] = 0;
  if (root)
    cb[0] = 0, cb[1] = 11, cb[6] = 11;	/* a red title bar, white text */
  w[10] = 0; w[11] = -(maxrows) * CH; w[12] = maxcols * CW; w[13] = 0;
  w[14] = 0x119;		/* title: text, centred, indirected */
  w[15] = 3 << 12;		/* clicks */
  w[16] = 1;
  w[17] = 0;
  w[18] = (int) t->title; w[19] = -1; w[20] = sizeof t->title;
  w[21] = 0;
  if (_swix (Wimp_CreateWindow, _IN (1) | _OUT (0), b, &t->win))
    {
      free_term (t);
      report ("The window could not be made.");
      return;
    }
  terms[slot] = t;
  b[0] = t->win;
  b[1] = x0; b[2] = y1 - t->rows * CH; b[3] = x0 + t->cols * CW; b[4] = y1;
  b[5] = 0; b[6] = 0; b[7] = -1;
  _swix (Wimp_OpenWindow, _IN (1), b);
  give_focus (t);
  t->focus = 1;
  if (root)
    root_start (t);
  else
    start_job (t, "@user");
}

static void
open_request (Term *t)
{
  int cols = (b[3] - b[1]) / CW, rows = (b[4] - b[2]) / CH;
  if (cols < 20)
    cols = 20;
  if (cols > maxcols)
    cols = maxcols;
  if (rows < 5)
    rows = 5;
  if (rows > maxrows)
    rows = maxrows;
  b[3] = b[1] + cols * CW;
  b[2] = b[4] - rows * CH;
  b[5] = 0;
  if (cols != t->cols || rows != t->rows)
    {
      int save[8];
      memcpy (save, b, sizeof save);
      resize (t, cols, rows);
      set_extent (t);
      t->grew = 0;
      memcpy (b, save, sizeof save);
      b[6] = -t->hcount * CH;
    }
  else
    {
      if (b[6] < -t->hcount * CH)
	b[6] = -t->hcount * CH;
      b[6] -= b[6] % CH;
    }
  _swix (Wimp_OpenWindow, _IN (1), b);
  refresh (t);
}

/* --- the selection and the clipboard -------------------------------------- */

static void
redraw_lines (Term *t, int l0, int l1)	/* lines from the first of history */
{
  if (l0 > l1)
    {
      int x = l0;
      l0 = l1;
      l1 = x;
    }
  int a[4] = { 0, -(l1 + 1) * CH, maxcols * CW, -l0 * CH };
  redraw (t, 1, a);
}

static void
clear_selection (Term *t)
{
  if (!t->sel)
    return;
  int base = (int) (t->scrolled - (unsigned) t->hcount);
  int l0 = (int) t->sa_l - base, l1 = (int) t->se_l - base;
  t->sel = t->dragging = 0;
  redraw_lines (t, l0, l1);
}

/* where the pointer is in the terminal: the line (numbered as the
   selection's are) and the column boundary nearest it; *buttons as the
   Wimp gives them; *edge -1 above the window, 1 below, else 0 */
static void
pointer_cell (Term *t, unsigned *line, int *col, int *buttons, int *edge)
{
  int p[5], w[40];
  _swix (Wimp_GetPointerInfo, _IN (1), p);
  w[0] = t->win;
  _swix (Wimp_GetWindowState, _IN (1), w);
  int ox = w[1] - w[5], oy = w[4] - w[6];
  int c = (p[0] - ox + CW / 2) / CW, l = (oy - p[1] - 1) / CH;
  if (p[1] > oy)
    l = 0;
  *edge = p[1] >= w[4] ? -1 : p[1] < w[2] ? 1 : 0;
  if (c < 0)
    c = 0;
  if (c > t->cols)
    c = t->cols;
  if (l < 0)
    l = 0;
  if (l > t->hcount + t->rows - 1)
    l = t->hcount + t->rows - 1;
  *line = t->scrolled - (unsigned) t->hcount + (unsigned) l;
  *col = c;
  *buttons = p[2];
}

/* Select pressed in the window: a new selection starts here. Adjust: the
   one there is ends here instead. */
static void
select_click (Term *t, int adjust)
{
  unsigned l;
  int c, bt, edge;
  pointer_cell (t, &l, &c, &bt, &edge);
  if (adjust && t->sel)
    {
      int base = (int) (t->scrolled - (unsigned) t->hcount), old = (int) t->se_l - base;
      t->se_l = l;
      t->se_c = c;
      redraw_lines (t, old, (int) l - base);
    }
  else
    {
      clear_selection (t);
      t->sa_l = t->se_l = l;
      t->sa_c = t->se_c = c;
      t->sel = 1;
    }
  t->dragging = 1;
}

/* while a button is held: the selection's end follows the pointer */
static void
drag_poll (Term *t)
{
  unsigned l;
  int c, bt, edge;
  pointer_cell (t, &l, &c, &bt, &edge);
  if (!(bt & 5))
    {
      t->dragging = 0;
      if (t->sa_l == t->se_l && t->sa_c == t->se_c)
	t->sel = 0;		/* (a click, not a drag) */
      return;
    }
  if (edge)
    {
      /* past the top or bottom: the window scrolls a line */
      int w[40];
      w[0] = t->win;
      if (!_swix (Wimp_GetWindowState, _IN (1), w))
	{
	  w[6] -= edge * CH;
	  if (w[6] > 0)
	    w[6] = 0;
	  if (w[6] < -t->hcount * CH)
	    w[6] = -t->hcount * CH;
	  _swix (Wimp_OpenWindow, _IN (1), w);
	  pointer_cell (t, &l, &c, &bt, &edge);
	}
    }
  if (l == t->se_l && c == t->se_c)
    return;
  int base = (int) (t->scrolled - (unsigned) t->hcount), old = (int) t->se_l - base;
  t->se_l = l;
  t->se_c = c;
  redraw_lines (t, old, (int) l - base);
}

/* the selected text, as RISC OS text (Latin-1, lines ending LF); the
   spaces a line ends with aren't part of it */
static char *
selection_text (Term *t, int *len)
{
  unsigned al = t->sa_l, el = t->se_l;
  int ac = t->sa_c, ec = t->se_c;
  if (!t->sel)
    return 0;
  if (al > el || (al == el && ac > ec))
    {
      al = t->se_l, el = t->sa_l;
      ac = t->se_c, ec = t->sa_c;
    }
  unsigned base = t->scrolled - (unsigned) t->hcount;
  if (al < base)
    al = base, ac = 0;
  if (el < al)
    return 0;
  char *out = malloc ((size_t) (el - al + 1) * (MAXCOLS + 1) + 1);
  if (!out)
    return 0;
  int n = 0;
  for (unsigned l = al; l <= el; l++)
    {
      const Cell *line = line_at (t, (int) (l - base));
      int from = l == al ? ac : 0, to = l == el ? ec : t->cols;
      if (!line)
	break;
      if (to > t->cols)
	to = t->cols;
      int last = to;
      while (last > from && (line[last - 1].ch == ' '))
	last--;
      for (int c = from; c < last; c++)
	{
	  unsigned ch = line[c].ch;
	  if (ch < 32)
	    ch = ch == 3 ? '-' : ch == 12 ? '|' : ch <= 15 ? '+' : ch == 18 ? '*' : '#';
	  else if (ch == 127)
	    ch = ' ';
	  out[n++] = (char) ch;
	}
      /* a line taken to its end ends there */
      if (l != el || (to == t->cols && last < to))
	out[n++] = '\n';
    }
  out[n] = 0;
  *len = n;
  return out;
}

/* Copy: the selection becomes the clipboard, which is then RiSH's to give */
static void
copy_selection (Term *t)
{
  int n = 0;
  char *text = selection_text (t, &n);
  if (!text || !n)
    {
      free (text);
      return;
    }
  free (clip);
  clip = text;
  clipn = n;
  if (!clip_ours)
    {
      int m[6] = { 24, 0, 0, 0, 15, 4 };	/* Message_ClaimEntity: the clipboard */
      _swix (Wimp_SendMessage, _INR (0, 2), 17, m, 0);
      clip_ours = 1;
    }
}

/* pasted text typed into the terminal: line ends as Return, Latin-1 as
   UTF-8, control codes left out */
static void
paste_text (Term *t, const char *text, int n)
{
  char out[512];
  int k = 0;
  if (t->state != S_RUN)
    return;
  if (t->m_bpaste)
    sends (t, "\033[200~");
  for (int i = 0; i < n; i++)
    {
      unsigned c = (unsigned char) text[i];
      if (c == 13 && i + 1 < n && text[i + 1] == 10)
	continue;
      if (c == 10 || c == 13)
	out[k++] = '\r';
      else if (c == 9)
	out[k++] = '\t';
      else if (c < 32 || c == 127)
	;
      else if (c < 0x80)
	out[k++] = (char) c;
      else if (c < 0xA0)
	{
	  /* RISC OS's own characters there, as near as ASCII has them */
	  const char *r = c == 0x8C ? "..." : c == 0x90 || c == 0x91 ? "'" : c == 0x94 || c == 0x95 ? "\"" : c == 0x97 || c == 0x98 || c == 0x99 ? "-" : c == 0x8F ? "*" : "?";
	  while (*r)
	    out[k++] = *r++;
	}
      else
	{
	  out[k++] = (char) (0xC0 | (c >> 6));
	  out[k++] = (char) (0x80 | (c & 0x3F));
	}
      if (k > (int) sizeof out - 8)
	{
	  send (t, out, k);
	  k = 0;
	}
    }
  send (t, out, k);
  if (t->m_bpaste)
    sends (t, "\033[201~");
}

/* Paste: from RiSH's own clipboard, or by asking whoever has it */
static void
paste (Term *t)
{
  if (t->state != S_RUN)
    return;
  if (clip_ours && clip)
    {
      paste_text (t, clip, clipn);
      return;
    }
  int m[12] = { 48, 0, 0, 0, 16, t->win, 0, 0, 0, 4, 0xFFF, -1 };	/* Message_DataRequest */
  paste_to = t;
  _swix (Wimp_SendMessage, _INR (0, 2), 17, m, 0);
}

/* the clipboard's messages (b: the message) */
static void
clipboard_message (void)
{
  int action = b[4];
  if (b[1] == task)
    return;			/* (our own broadcasts) */
  if (action == 15)
    {
      /* someone else has claimed the clipboard */
      if (b[5] & 4)
	{
	  clip_ours = 0;
	  free (clip);
	  clip = 0;
	  clipn = 0;
	}
    }
  else if (action == 16)
    {
      /* asked for the clipboard: offered as a text file */
      if ((b[9] & 4) && clip_ours && clip)
	{
	  b[3] = b[2];
	  b[4] = 1;
	  b[9] = clipn;
	  b[10] = 0xFFF;
	  strcpy ((char *) &b[11], "Clipboard");
	  b[0] = 56;
	  _swix (Wimp_SendMessage, _INR (0, 2), 17, b, b[1]);
	}
    }
  else if (action == 2)
    {
      /* where to save it: saved, and the asker told */
      if (clip_ours && clip)
	{
	  if (!_swix (OS_File, _INR (0, 2) | _INR (4, 5), 10, (char *) &b[11], 0xFFF, clip, clip + clipn))
	    {
	      b[3] = b[2];
	      b[4] = 3;
	      b[9] = clipn;
	      _swix (Wimp_SendMessage, _INR (0, 2), 17, b, b[1]);
	    }
	}
    }
  else if (action == 1)
    {
      /* the clipboard's owner offers it for our paste: through a file */
      if (paste_to)
	{
	  b[3] = b[2];
	  b[4] = 2;
	  b[9] = -1;		/* (not a safe place: the scrap file) */
	  strcpy ((char *) &b[11], "<Wimp$Scrap>");
	  b[0] = 60;
	  _swix (Wimp_SendMessage, _INR (0, 2), 17, b, b[1]);
	}
    }
  else if (action == 3)
    {
      /* the file is there: its text goes to the terminal */
      Term *t = paste_to;
      int found = 0;
      for (int i = 0; i < MAXTERMS; i++)
	if (terms[i] && terms[i] == t)
	  found = 1;
      paste_to = 0;
      if (!found)
	return;
      char name[220];
      strncpy (name, (char *) &b[11], sizeof name - 1);
      name[sizeof name - 1] = 0;
      int type = 0, size = 0;
      if (!_swix (OS_File, _INR (0, 1) | _OUT (0) | _OUT (4), 17, name, &type, &size) && type == 1 && size > 0 && size <= 1024 * 1024)
	{
	  char *buf = malloc ((size_t) size);
	  if (buf && !_swix (OS_File, _INR (0, 3), 16, name, buf, 0))
	    {
	      int reply[64];
	      memcpy (reply, b, sizeof reply);
	      paste_text (t, buf, size);
	      memcpy (b, reply, sizeof reply);
	    }
	  free (buf);
	}
      b[3] = b[2];
      b[4] = 4;
      _swix (Wimp_SendMessage, _INR (0, 2), 17, b, b[1]);
      _swix (OS_File, _INR (0, 1), 6, name);
    }
}

static int
shift_held (void)
{
  int x = 0;
  _swix (OS_Byte, _INR (0, 2) | _OUT (1), 121, 0x80, 0, &x);
  return x == 0xFF;
}

/* --- keys ------------------------------------------------------------------ */

static void
ask_key (Term *t, int k)
{
  if (k == 13)
    {
      t->line[t->linen] = 0;
      term_puts (t, "\r\n");
      if (t->ask == 2)
	{
	  strcpy (t->first, t->line);
	  ask (t, 3);
	}
      else if (t->ask == 3 && (strcmp (t->first, t->line) || !t->line[0]))
	{
	  term_puts (t, t->line[0] ? "They aren't the same.\r\n" : "A password is needed.\r\n");
	  ask (t, 2);
	}
      else
	{
	  char h[140];
	  sprintf (h, "%s %s", t->ask == 3 ? "@newpassword" : "@password", t->line);
	  t->tries++;
	  start_job (t, h);
	  memset (h, 0, sizeof h);
	}
      memset (t->line, 0, sizeof t->line);
      t->linen = 0;
    }
  else if (k == 8 || k == 127)
    {
      if (t->linen)
	t->linen--;
    }
  else if (k == 27 || k == 3)
    close_term (t);
  else if (k >= 32 && k < 256 && k != 127 && t->linen < (int) sizeof t->line - 1)
    t->line[t->linen++] = (char) k;
}

static void
key (Term *t, int k)
{
  char s[8];
  const char *q = 0;
  int app = t->m_appkeys;
  if (t->state == S_ASK)
    {
      if (k < 256)
	{
	  int w = t->win;
	  ask_key (t, k);
	  if (find (w) == t)
	    refresh (t);
	}
      else
	_swix (Wimp_ProcessKey, _IN (0), k);
      return;
    }
  if (t->state == S_RUN && ((k == 3 || k == 22) && shift_held ()))
    {
      /* Ctrl-Shift-C and Ctrl-Shift-V: copy and paste (Ctrl-C alone is
	 Linux's) */
      if (k == 3)
	copy_selection (t);
      else
	paste (t);
      return;
    }
  if (t->state == S_RUN && k == 0x1DD)
    {
      paste (t);		/* Shift-Insert */
      return;
    }
  if (t->state == S_RUN && k < 0x1CC)
    {
      /* typing brings the view back down to the screen */
      int save[8];
      memcpy (save, b, sizeof save);
      b[0] = t->win;
      if (!_swix (Wimp_GetWindowState, _IN (1), b) && b[6] != -t->hcount * CH)
	open_at_bottom (t);
      memcpy (b, save, sizeof save);
    }
  if (t->state != S_RUN)
    {
      if (k == 13 || k == 27)
	close_term (t);
      else if (k >= 256)
	_swix (Wimp_ProcessKey, _IN (0), k);
      return;
    }
  if (k < 256)
    {
      if (k == 8)
	s[0] = 127, send (t, s, 1);
      else if (k == 127)
	sends (t, "\033[3~");
      else if (k == 30)
	sends (t, app ? "\033OH" : "\033[H");
      else if (k >= 0x80)
	{
	  s[0] = (char) (0xC0 | (k >> 6));
	  s[1] = (char) (0x80 | (k & 0x3F));
	  send (t, s, 2);
	}
      else
	{
	  s[0] = (char) k;
	  send (t, s, 1);
	}
      return;
    }
  switch (k)
    {
    case 0x18C: q = app ? "\033OD" : "\033[D"; break;
    case 0x18D: q = app ? "\033OC" : "\033[C"; break;
    case 0x18E: q = app ? "\033OB" : "\033[B"; break;
    case 0x18F: q = app ? "\033OA" : "\033[A"; break;
    case 0x1AC: q = "\033[1;5D"; break;	/* Ctrl with the cursor keys */
    case 0x1AD: q = "\033[1;5C"; break;
    case 0x1AE: q = "\033[1;5B"; break;
    case 0x1AF: q = "\033[1;5A"; break;
    case 0x18B: q = app ? "\033OF" : "\033[F"; break;	/* Copy: End */
    case 0x19E: q = "\033[6~"; break;	/* Page Down */
    case 0x19F: q = "\033[5~"; break;	/* Page Up */
    case 0x1CD: q = "\033[2~"; break;	/* Insert */
    case 0x18A: q = "\t"; break;
    case 0x19A: q = "\033[Z"; break;	/* Shift-Tab */
    case 0x181: q = "\033OP"; break;
    case 0x182: q = "\033OQ"; break;
    case 0x183: q = "\033OR"; break;
    case 0x184: q = "\033OS"; break;
    case 0x185: q = "\033[15~"; break;
    case 0x186: q = "\033[17~"; break;
    case 0x187: q = "\033[18~"; break;
    case 0x188: q = "\033[19~"; break;
    case 0x189: q = "\033[20~"; break;
    case 0x1CA: q = "\033[21~"; break;
    case 0x1CB: q = "\033[23~"; break;
    }
  if (q)
    sends (t, q);
  else
    _swix (Wimp_ProcessKey, _IN (0), k);
}

/* --- menus ----------------------------------------------------------------- */

static void
menu_item (int *m, int i, int flags, const char *text)
{
  int *e = m + 7 + i * 6;
  e[0] = flags;
  e[1] = -1;
  e[2] = 0x07000021;
  memset (&e[3], 0, 12);
  strncpy ((char *) &e[3], text, 12);
}

static void
show_menu (Term *t, int x, int y)
{
  memset (menu, 0, sizeof menu);
  strcpy ((char *) menu, "RiSH");
  unsigned char *c = (unsigned char *) &menu[3];
  c[0] = 7; c[1] = 2; c[2] = 7; c[3] = 0;
  menu[4] = 16 * 13 + 12;
  menu[5] = 44;
  menu[6] = 0;
  menu_term = t;
  if (t)
    {
      menu_item (menu, 0, 0, "New terminal");
      menu_item (menu, 1, 0, "Copy");
      menu_item (menu, 2, 0, "Paste");
      menu_item (menu, 3, scale == 2 ? 1 : 0, "Large text");
      menu_item (menu, 4, 0x80, "Close");
      if (!t->sel)
	menu[7 + 1 * 6 + 2] |= 1 << 22;	/* nothing selected: Copy is grey */
      if (t->state != S_RUN)
	menu[7 + 2 * 6 + 2] |= 1 << 22;
    }
  else
    {
      menu_item (menu, 0, 0, "Info");
      menu_item (menu, 1, 0, "New terminal");
      menu_item (menu, 2, 0, "Root shell");
      menu_item (menu, 3, scale == 2 ? 1 : 0, "Large text");
      menu_item (menu, 4, 0x80, "Quit");
      y = 96 + 44 * 5;
      x -= 64;
    }
  _swix (Wimp_CreateMenu, _INR (1, 3), menu, x, y);
}

static void
set_scale (int s)
{
  scale = s;
  read_mode ();
  for (int i = 0; i < MAXTERMS; i++)
    if (terms[i])
      {
	Term *t = terms[i];
	b[0] = t->win;
	if (_swix (Wimp_GetWindowState, _IN (1), b))
	  continue;
	int cols = t->cols > maxcols ? maxcols : t->cols, rows = t->rows > maxrows ? maxrows : t->rows;
	resize (t, cols, rows);
	set_extent (t);
	t->grew = 0;
	/* the same columns and rows, moved if need be to stay on the
	   screen (above the icon bar) */
	int sw = scrw << xeig, sh = scrh << yeig, w = cols * CW, h = rows * CH;
	if (b[1] + w > sw)
	  b[1] = sw - w < 0 ? 0 : sw - w;
	if (b[4] - h < 140)
	  b[4] = 140 + h;
	if (b[4] > sh - 44)
	  b[4] = sh - 44;
	b[1] -= b[1] % dx;
	b[4] -= b[4] % dy;
	b[3] = b[1] + w;
	b[2] = b[4] - h;
	b[5] = 0;
	b[6] = -t->hcount * CH;
	_swix (Wimp_OpenWindow, _IN (1), b);
	_swix (Wimp_ForceRedraw, _INR (0, 4), t->win, 0, -(t->hcount + maxrows) * CH, maxcols * CW, 0);
      }
}

static void
menu_choice (int item)
{
  if (menu_term)
    {
      if (!find (menu_term->win))
	return;
      if (item == 0)
	new_term (0);
      else if (item == 1)
	copy_selection (menu_term);
      else if (item == 2)
	paste (menu_term);
      else if (item == 3)
	set_scale (scale == 2 ? 1 : 2);
      else if (item == 4)
	close_term (menu_term);
    }
  else
    switch (item)
      {
      case 0:
	report ("RiSH, the RISC Shell, " VERSION ": a Linux terminal for RISC OS on Linux. (c) 2026 Andrew Youll, MIT licence.");
	break;
      case 1:
	new_term (0);
	break;
      case 2:
	new_term (1);
	break;
      case 3:
	set_scale (scale == 2 ? 1 : 2);
	break;
      case 4:
	quit = 1;
	break;
      }
}

/* --- main ------------------------------------------------------------------ */

static void
make_palette (void)
{
  static const unsigned char base[16][3] = {
    { 0, 0, 0 }, { 205, 49, 49 }, { 13, 188, 121 }, { 229, 229, 16 }, { 60, 120, 240 }, { 188, 63, 188 },
    { 17, 168, 205 }, { 200, 200, 200 }, { 110, 110, 110 }, { 241, 76, 76 }, { 35, 209, 139 },
    { 245, 245, 67 }, { 90, 150, 255 }, { 214, 112, 214 }, { 41, 184, 219 }, { 255, 255, 255 } };
  for (int i = 0; i < 16; i++)
    pal[i] = ((unsigned) base[i][2] << 24) | ((unsigned) base[i][1] << 16) | ((unsigned) base[i][0] << 8);
  for (int i = 0; i < 216; i++)
    {
      unsigned r = (unsigned) (i / 36), g = (unsigned) (i / 6 % 6), bl = (unsigned) (i % 6);
      r = r ? 55 + 40 * r : 0;
      g = g ? 55 + 40 * g : 0;
      bl = bl ? 55 + 40 * bl : 0;
      pal[16 + i] = (bl << 24) | (g << 16) | (r << 8);
    }
  for (unsigned i = 0; i < 24; i++)
    {
      unsigned v = 8 + 10 * i;
      pal[232 + i] = (v << 24) | (v << 16) | (v << 8);
    }
}

int
main (int argc, char **argv)
{
  static int messages[] = { 1, 2, 3, 15, 16, 0x400C1, 0 };
  _kernel_oserror *e = _swix (Wimp_Initialise, _INR (0, 3) | _OUT (1), 380, 0x4B534154, "RiSH", messages, &task);
  if (e)
    {
      fprintf (stderr, "RiSH: %s\n", e->errmess);
      return 1;
    }
  make_palette ();
  read_mode ();
  if (scrw >= 2560)
    {
      scale = 2;
      read_mode ();
    }
  /* the icon bar */
  b[0] = -1;
  b[1] = 0; b[2] = 0; b[3] = 68; b[4] = 68;
  b[5] = 0x3002;		/* a sprite; clicks */
  memset (&b[6], 0, 12);
  strcpy ((char *) &b[6], "!rish");
  _swix (Wimp_CreateIcon, _IN (1) | _OUT (0), b, &iconbar);
  if (!(argc > 1 && !strcmp (argv[1], "-iconbar")))
    new_term (0);

  while (!quit)
    {
      int reason, busy = 0;
      for (int i = 0; i < MAXTERMS; i++)
	if (terms[i] && (terms[i]->state == S_RUN || terms[i]->dragging))
	  busy = 1;
      if (busy)
	e = _swix (Wimp_PollIdle, _INR (0, 2) | _OUT (0), 0, b, now_cs () + 2, &reason);
      else
	e = _swix (Wimp_Poll, _INR (0, 1) | _OUT (0), 1, b, &reason);
      if (e)
	break;
      Term *t;
      switch (reason)
	{
	case 0:
	  for (int i = 0; i < MAXTERMS; i++)
	    if (terms[i] && terms[i]->dragging)
	      drag_poll (terms[i]);
	  for (int i = 0; i < MAXTERMS; i++)
	    if (terms[i])
	      poll_job (terms[i]);
	  break;
	case 1:
	  if ((t = find (b[0])))
	    redraw (t, 0, 0);
	  else
	    {
	      int more;
	      _swix (Wimp_RedrawWindow, _IN (1) | _OUT (0), b, &more);
	      while (more)
		_swix (Wimp_GetRectangle, _IN (1) | _OUT (0), b, &more);
	    }
	  break;
	case 2:
	  if ((t = find (b[0])))
	    open_request (t);
	  else
	    _swix (Wimp_OpenWindow, _IN (1), b);
	  break;
	case 3:
	  if ((t = find (b[0])))
	    close_term (t);
	  break;
	case 6:
	  if (b[3] == -2)
	    {
	      if (b[2] & 2)
		show_menu (0, b[0], b[1]);
	      else if (b[2] & 5)
		new_term (b[2] & 1 ? 1 : 0);	/* Adjust: a root terminal */
	    }
	  else if ((t = find (b[3])))
	    {
	      if (b[2] & 2)
		show_menu (t, b[0] - 64, b[1]);
	      else
		{
		  int adjust = b[2] & 1;
		  give_focus (t);
		  select_click (t, adjust);
		}
	    }
	  break;
	case 8:
	  if ((t = find (b[0])))
	    key (t, b[6]);
	  else
	    _swix (Wimp_ProcessKey, _IN (0), b[6]);
	  break;
	case 9:
	  menu_choice (b[0]);
	  break;
	case 11:
	case 12:
	  if ((t = find (b[0])))
	    {
	      t->focus = reason == 12;
	      dirty (t, t->cy);
	      refresh (t);
	    }
	  break;
	case 17:
	case 18:
	  if (b[4] == 0)
	    quit = 1;
	  else if (b[4] == 0x400C1)
	    set_scale (scale);
	  else if (b[4] == 1 || b[4] == 2 || b[4] == 3 || b[4] == 15 || b[4] == 16)
	    clipboard_message ();
	  break;
	}
      /* each poll also carries any terminal's traffic (keys are answered
	 at once, not a tick later) */
      if (reason != 0)
	for (int i = 0; i < MAXTERMS; i++)
	  if (terms[i])
	    poll_job (terms[i]);
    }
  for (int i = 0; i < MAXTERMS; i++)
    if (terms[i])
      close_term (terms[i]);
  _swix (Wimp_CloseDown, _INR (0, 1), task, 0x4B534154);
  return 0;
}
