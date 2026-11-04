/* LinuxOverlay: video overlays for RISC OS on Linux.

   RISC OS Open's VideoOverlay module (in !System) gives programs hardware
   video overlays: a picture in YUV, scaled by the display hardware, under
   or over the desktop. It does it through the display driver's GraphicsV
   overlay calls (20-28), which RISC OS on Linux's driver (SocketKVM, in
   the ROM) hasn't got, so VideoOverlay's Create fails and programs such
   as Reel draw their video into the screen themselves (and it tears: the
   screen reaches the monitor through rol-fb, out of step with it).

   This module answers those calls for the current driver, with rol-fb's
   overlay plane: the one LinuxCodec uses (rol-fb.c, "the video overlay").
   rol-fb makes each buffer (a DRM dumb buffer: memory the display can
   scan out) and hands it over as a dmabuf, which is mapped here for the
   program to write; showing one puts it on the plane, which changes at
   the vertical blank, so a picture is never shown half written.

   The calls, as VideoOverlay 0.02 makes them (r4 = reason | overlay << 16
   | driver << 24; a claimed call returns r4 = 0):
     20 Create   r0 mode selector (YUV: Log2BPP 7, NColour = FOURCC;
                 ModeFlags; MinScreenBanks = the number of buffers),
                 r1 aspect, r2 flags (bit 0 scalable)
                 -> r0 overlay (1), r1 type (0 Z-order, 1 Basic),
                    r2, r3 smallest and r5, r6 largest scaled size
     21 Destroy
     22 SetPosition r0 -> list of {head, x, y} (screen pixels, top left),
                 r1 entries (0: hidden), r2 buffer to show
     23 MapBuffer r0 buffer -> r0 -> {address, stride} for each plane
     24 UnmapBuffer, 25 DiscardBuffer r0 buffer (nothing to do here)
     26 Vet      as Create, without making anything
     27 SetTransform r0-r3 the visible part (x0, y0, x1, y1, pixels from
                 the overlay's top left), r5, r6 scaled width and height
                 (returned as done), r7 aspect
     28 SetZOrder r0 -> overlays, r1 how many
     12 ReadPaletteEntry r0 = 3 (colour key) -> r1 the key's pixel value
   One overlay at a time (rol-fb has one plane). Formats: YV12 (RISC OS's:
   Y, then Cb, then Cr: DRM's YUV420) and NV12.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "cmodule.h"

#define IXSupport_LinuxSyscall	0x59EC0
#define OS_ReadVduVariables	0x31

#define NR_close	6
#define NR_munmap	91
#define NR_mmap2	192
#define NR_socket	281
#define NR_connect	283
#define NR_sendmsg	296
#define NR_recvmsg	297
#define O_CLOEXEC	02000000
#define MSG_NOSIGNAL	0x4000
#define MSG_CMSG_CLOEXEC 0x40000000

#define DRM_YUV420	0x32315559	/* "YU12" */
#define DRM_NV12	0x3231564e	/* "NV12" */
#define RO_YV12		0x32315659	/* RISC OS's "YV12" (Y, Cb, Cr) */
#define RO_NV12		0x3231564e

#define MAXBUF		8

static int
lsys (int nr, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
  regs r = {{ a, b, c, d, e, f, 0, (unsigned) nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -38;
  return (int) r.r[0];
}

static struct
{
  int fd;			/* the connection to rol-fb, or -1 */
  int busy;			/* SHOWs not answered yet */
  unsigned key;			/* the colour key, as a pixel */
  int under;			/* the plane is under the desktop */
  int made;			/* an overlay exists */
  unsigned fourcc, w, h, nbuf, cflags;
  struct { unsigned addr, size; unsigned planes[6]; } buf[MAXBUF];
  int shown, cur;
  int x, y;			/* where its top left is (screen pixels) */
  int clip[4], have_clip;	/* the visible part, from its top left */
  int sw, sh;			/* scaled size */
  unsigned creates, shows, errors;
  unsigned calls[29];		/* GraphicsV calls seen, by reason */
  unsigned sel[12];		/* the last Create/Vet selector */
  int last_err;
} ov = { .fd = -1 };

void gv_release (void);

/* --- talking to rol-fb ------------------------------------------------- */

static int
ov_send (const int *m)
{
  struct { void *base; unsigned len; } iov = { (void *) m, 64 };
  struct { void *name; unsigned namelen; void *iov; unsigned iovlen; void *control; unsigned controllen; int flags; }
    mh = { 0, 0, &iov, 1, 0, 0, 0 };
  return lsys (NR_sendmsg, (unsigned) ov.fd, (unsigned) &mh, MSG_NOSIGNAL, 0, 0, 0) == 64 ? 0 : -1;
}

/* a reply (and the fd it carries, or -1) */
static int
ov_recv (int *rep, int *fd)
{
  struct { unsigned len; int level, type; int fd; } cm = { 16, 0, 0, -1 };
  struct { void *base; unsigned len; } iov = { rep, 64 };
  struct { void *name; unsigned namelen; void *iov; unsigned iovlen; void *control; unsigned controllen; int flags; }
    mh = { 0, 0, &iov, 1, &cm, 16, 0 };
  memset (rep, 0, 64);
  int n = lsys (NR_recvmsg, (unsigned) ov.fd, (unsigned) &mh, MSG_CMSG_CLOEXEC, 0, 0, 0);
  if (fd)
    *fd = mh.controllen >= 16 && cm.level == 1 && cm.type == 1 ? cm.fd : -1;
  else if (mh.controllen >= 16 && cm.level == 1 && cm.type == 1)
    lsys (NR_close, (unsigned) cm.fd, 0, 0, 0, 0, 0);
  return n < 4 ? -1 : 0;
}

/* the answers to SHOWs sent without waiting */
static void
ov_settle (int keep)
{
  int rep[16];
  while (ov.busy > keep)
    {
      ov.busy--;
      if (ov_recv (rep, 0) < 0)
	ov.busy = 0;
      else if (rep[0] < 0)
	{
	  ov.errors++;
	  ov.last_err = rep[0];
	}
    }
}

static int
ov_call (const int *m, int *rep, int *fd)
{
  if (fd)
    *fd = -1;
  ov_settle (0);
  if (ov_send (m) < 0 || ov_recv (rep, fd) < 0)
    return -1;
  if (rep[0] < 0)
    {
      ov.errors++;
      ov.last_err = rep[0];
    }
  return rep[0];
}

static void
ov_disconnect (void)
{
  if (ov.fd >= 0)
    lsys (NR_close, (unsigned) ov.fd, 0, 0, 0, 0, 0);
  ov.fd = -1;
  ov.busy = 0;
}

static int
ov_connect (void)
{
  if (ov.fd >= 0)
    return 0;
  int s = lsys (NR_socket, 1 /* AF_UNIX */, 5 /* SOCK_SEQPACKET */ | O_CLOEXEC, 0, 0, 0, 0);
  if (s < 0)
    return -1;
  static const char addr[] = "\1\0\0rol-fb-overlay";	/* abstract name */
  if (lsys (NR_connect, (unsigned) s, (unsigned) addr, 2 + 15, 0, 0, 0) < 0)
    {
      lsys (NR_close, (unsigned) s, 0, 0, 0, 0, 0);
      return -1;
    }
  ov.fd = s;
  return 0;
}

/* --- the overlay ------------------------------------------------------- */

static void
unmap_all (void)
{
  for (unsigned i = 0; i < MAXBUF; i++)
    {
      if (ov.buf[i].addr)
	lsys (NR_munmap, ov.buf[i].addr, ov.buf[i].size, 0, 0, 0, 0);
      memset (&ov.buf[i], 0, sizeof ov.buf[i]);
    }
}

static void
destroy (void)
{
  int m[16], rep[16];
  if (ov.fd >= 0)
    {
      memset (m, 0, sizeof m);
      m[0] = 4;					/* HIDE */
      ov_call (m, rep, 0);
      m[0] = 5;					/* FORGET */
      ov_call (m, rep, 0);
    }
  unmap_all ();
  ov_disconnect ();
  ov.made = ov.shown = 0;
  ov.have_clip = 0;
}

/* The mode selector: format, size, buffers, colours. 0 if unsupported. */
static int
parse (const unsigned *sel, unsigned *fourcc, unsigned *w, unsigned *h, unsigned *nbuf, unsigned *cflags)
{
  if (!sel || (sel[0] & 1) == 0)
    return 0;
  unsigned ncolour = 0, flags = 0, banks = 1;
  for (const unsigned *v = sel + 5; v[0] != 0xFFFFFFFFu && v < sel + 5 + 64; v += 2)
    switch (v[0])
      {
      case 0: flags = v[1]; break;
      case 3: ncolour = v[1]; break;
      case 13: banks = v[1]; break;
      }
  if (sel[3] != 7)
    return 0;
  if (ncolour == RO_YV12)
    *fourcc = DRM_YUV420;
  else if (ncolour == RO_NV12)
    *fourcc = DRM_NV12;
  else
    return 0;
  *w = sel[1];
  *h = sel[2];
  if (*w < 2 || *h < 2 || *w > 4096 || *h > 4096)
    return 0;
  *nbuf = banks < 1 ? 1 : banks > MAXBUF ? MAXBUF : banks;
  /* ModeFlags bit 15: BT.709 (else 601); bit 14: video range (else full) */
  *cflags = (flags & (1u << 15) ? 1 : 0) | (flags & (1u << 14) ? 0 : 2);
  return 1;
}

static int
create (const unsigned *sel)
{
  unsigned fourcc, w, h, nbuf, cflags;
  if (ov.made || !parse (sel, &fourcc, &w, &h, &nbuf, &cflags) || ov_connect () < 0)
    return -1;
  int m[16], rep[16], fd;
  memset (m, 0, sizeof m);
  m[0] = 1;					/* OPEN */
  if (ov_call (m, rep, 0) < 0)
    {
      ov_disconnect ();
      return -1;
    }
  ov.key = (unsigned) rep[1];
  ov.under = rep[2] != 0;
  ov.fourcc = fourcc;
  ov.w = w;
  ov.h = h;
  ov.nbuf = nbuf;
  ov.cflags = cflags;
  for (unsigned i = 0; i < nbuf; i++)
    {
      memset (m, 0, sizeof m);
      m[0] = 6;					/* ALLOC */
      m[1] = (int) i;
      m[2] = (int) w;
      m[3] = (int) h;
      m[4] = (int) fourcc;
      if (ov_call (m, rep, &fd) < 0 || fd < 0)
	goto fail;
      unsigned size = (unsigned) rep[1];
      int a = lsys (NR_mmap2, 0, size, 3 /* PROT_READ | PROT_WRITE */, 1 /* MAP_SHARED */, (unsigned) fd, 0);
      lsys (NR_close, (unsigned) fd, 0, 0, 0, 0, 0);
      if (a < 0 && a > -4096)
	goto fail;
      ov.buf[i].addr = (unsigned) a;
      ov.buf[i].size = size;
      unsigned np = fourcc == DRM_NV12 ? 2 : 3;
      for (unsigned p = 0; p < np; p++)
	{
	  ov.buf[i].planes[p * 2] = (unsigned) a + (unsigned) rep[3 + p * 2];
	  ov.buf[i].planes[p * 2 + 1] = (unsigned) rep[2 + p * 2];
	}
      /* black: Y 16, Cb and Cr 128 */
      unsigned yb = (unsigned) rep[2] * h;
      memset ((void *) a, 16, yb);
      memset ((void *) (a + yb), 128, size - yb);
    }
  ov.made = 1;
  ov.shown = 0;
  ov.cur = 0;
  ov.have_clip = 0;
  ov.sw = ov.sh = 0;
  ov.creates++;
  return 0;
fail:
  destroy ();
  return -1;
}

/* Put it on the screen as it stands now (the answer isn't waited for:
   rol-fb takes the time of a vertical blank to show it) */
static void
show (void)
{
  int m[16], rep[16];
  if (!ov.made)
    return;
  int sw = ov.sw > 0 ? ov.sw : (int) ov.w, sh = ov.sh > 0 ? ov.sh : (int) ov.h;
  int x0 = 0, y0 = 0, x1 = sw, y1 = sh;
  if (ov.have_clip)
    {
      x0 = ov.clip[0] > 0 ? ov.clip[0] : 0;
      y0 = ov.clip[1] > 0 ? ov.clip[1] : 0;
      x1 = ov.clip[2] < sw ? ov.clip[2] : sw;
      y1 = ov.clip[3] < sh ? ov.clip[3] : sh;
    }
  memset (m, 0, sizeof m);
  if (!ov.shown || x1 <= x0 || y1 <= y0 || ov.cur < 0 || (unsigned) ov.cur >= ov.nbuf)
    {
      m[0] = 4;					/* HIDE */
      ov_call (m, rep, 0);
      return;
    }
  m[0] = 3;					/* SHOW */
  m[1] = ov.cur;
  m[2] = ov.x + x0;
  m[3] = ov.y + y0;
  m[4] = x1 - x0;
  m[5] = y1 - y0;
  /* the part of the picture that is visible */
  int sx = x0 * (int) ov.w / sw, sy = y0 * (int) ov.h / sh;
  int ex = x1 * (int) ov.w / sw, ey = y1 * (int) ov.h / sh;
  m[6] = sx;
  m[7] = sy;
  m[8] = ex > sx ? ex - sx : 1;
  m[9] = ey > sy ? ey - sy : 1;
  m[10] = (int) ov.cflags;
  ov_settle (1);				/* at most one waiting */
  if (ov_send (m) == 0)
    {
      ov.busy++;
      ov.shows++;
    }
}

static unsigned
current_driver (void)
{
  unsigned v[2] = { 192, 0xFFFFFFFFu }, out[1] = { 0xFFFFFFFFu };
  regs r = {{ (unsigned) v, (unsigned) out }};
  if (swix_regs (OS_ReadVduVariables, &r))
    return 0xFFFFFFFFu;
  return out[0];
}

/* GraphicsV: 1 if the call is answered (claimed) */
int
gv_call (regs *r)
{
  unsigned reason = r->r[4] & 0xFFFF;
  if (!((reason >= 20 && reason <= 28) || (reason == 12 && r->r[0] == 3)))
    return 0;
  if ((r->r[4] >> 24) != current_driver ())
    return 0;
  ov.calls[reason]++;
  if ((reason == 20 || reason == 26) && r->r[0])
    memcpy (ov.sel, (const void *) r->r[0], sizeof ov.sel);
  switch (reason)
    {
    case 12:					/* ReadPaletteEntry: the key */
      if (!ov.made)
	return 0;
      r->r[1] = ov.key;
      break;
    case 20:					/* CreateOverlay */
      if (create ((const unsigned *) r->r[0]) < 0)
	return 0;
      r->r[0] = 1;
      r->r[1] = ov.under ? 0 : 1;
      r->r[2] = r->r[3] = 2;
      r->r[5] = r->r[6] = 4096;
      break;
    case 26:					/* VetOverlay */
      {
	unsigned fourcc, w, h, nbuf, cflags;
	if (!parse ((const unsigned *) r->r[0], &fourcc, &w, &h, &nbuf, &cflags))
	  return 0;
	r->r[1] = 0;
	r->r[2] = r->r[3] = 2;
	r->r[5] = r->r[6] = 4096;
	break;
      }
    case 21:					/* DestroyOverlay */
      if (ov.made)
	destroy ();
      break;
    case 22:					/* SetOverlayPosition */
      if (!ov.made)
	return 0;
      if (r->r[1] == 0)
	ov.shown = 0;
      else
	{
	  const int *e = (const int *) r->r[0];
	  ov.x = e[1];
	  ov.y = e[2];
	  ov.cur = (int) r->r[2];
	  ov.shown = 1;
	}
      show ();
      break;
    case 23:					/* MapOverlayBuffer */
      if (!ov.made || r->r[0] >= ov.nbuf)
	return 0;
      r->r[0] = (unsigned) ov.buf[r->r[0]].planes;
      break;
    case 24:					/* UnmapOverlayBuffer */
    case 25:					/* DiscardOverlayBuffer */
    case 28:					/* SetOverlayZOrder */
      if (!ov.made)
	return 0;
      break;
    case 27:					/* SetOverlayTransform */
      if (!ov.made)
	return 0;
      for (int i = 0; i < 4; i++)
	ov.clip[i] = (int) r->r[i];
      ov.have_clip = 1;
      ov.sw = (int) r->r[5];
      ov.sh = (int) r->r[6];
      if (ov.shown)
	show ();
      break;
    }
  r->r[4] = 0;
  return 1;
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  if (ov.made)
    destroy ();
  gv_release ();
  return 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  (void) number; (void) r;
  return 0;
}

static void
line (const char *label, unsigned v)
{
  write0 (label);
  write_dec (v);
  newline ();
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  write0 ("Display driver: ");
  write_dec (current_driver ());
  newline ();
  if (!ov.made)
    write0 ("No overlay now");
  else
    {
      write0 (ov.fourcc == DRM_NV12 ? "Overlay: NV12 " : "Overlay: YV12 ");
      write_dec (ov.w);
      write0 (" x ");
      write_dec (ov.h);
      write0 (", ");
      write_dec (ov.nbuf);
      write0 (ov.under ? " buffers, under the desktop (Z-order)" : " buffers, over the desktop (Basic)");
      newline ();
      write0 (ov.shown ? "Shown at " : "Hidden; last at ");
      write_dec ((unsigned) ov.x);
      write0 (", ");
      write_dec ((unsigned) ov.y);
      write0 (", scaled to ");
      write_dec ((unsigned) (ov.sw > 0 ? ov.sw : (int) ov.w));
      write0 (" x ");
      write_dec ((unsigned) (ov.sh > 0 ? ov.sh : (int) ov.h));
      write0 (", colour key &");
      write_hex8 (ov.key);
    }
  newline ();
  line ("Overlays made: ", ov.creates);
  line ("Pictures shown: ", ov.shows);
  line ("rol-fb errors: ", ov.errors);
  write0 ("GraphicsV calls (12, 20-28):");
  for (int i = 12; i < 29; i++)
    if (i == 12 || i >= 20)
      {
	write0 (" ");
	write_dec (ov.calls[i]);
      }
  newline ();
  write0 ("Last selector:");
  for (int i = 0; i < 12; i++)
    {
      write0 (" ");
      write_hex8 (ov.sel[i]);
    }
  newline ();
  if (ov.last_err)
    {
      write0 ("Last rol-fb error: -");
      write_dec ((unsigned) -ov.last_err);
      newline ();
    }
  return 0;
}

void
mod_service (regs *r)
{
  (void) r;
}

/* libgcc's division calls this on a division by zero (never: sizes are
   checked first) */
int raise (int sig);
int
raise (int sig)
{
  (void) sig;
  return 0;
}
