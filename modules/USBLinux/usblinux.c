/* USBDriver (RISC OS for Linux): RISC OS's USB device interface, carried
   out with Linux's own USB access for programs (usbfs, as libusb uses).

   On RISC OS for Linux, Linux owns the USB hardware, so RISC OS's USB
   stack (USBDriver and the host controller drivers) can't run. Programs
   that talk to USB devices themselves through RISC OS's interface - PDFS
   (phones, cameras and tablets over MTP), and others written the same way
   - use this instead, unchanged:

   - DeviceFS devices "USB<n>", one per device, opened as
       devices#interface<i>;alternate<a>;endpoint<e>;bulk;size<n>:USB<n>
     with the same special fields as USBDriver's (endpoint, interface,
     alternate, report, control/isochronous/bulk/interrupt, usbtimeout,
     size, nopad, short) and the same behaviour: a bulk read is asked for
     with DeviceCall WakeUpRX (R3 = bytes) and ends at a short packet;
     what's written goes as one transfer per WakeUpTX, from the buffer,
     and is taken out of the buffer when the transfer has finished.
   - DeviceFS_CallDevice's USB calls: USBRequest (&80000000), BufferSpace
     (&80000002), GetHandles (&80000003), GetLocation (&80000004),
     ClearStall (&80000005), TransferInfo (&80000006), GetHandles2
     (&80000007), GetSetOptions (&80000008).
   - Service_USB: 0 (Attach) when a device arrives, 1 (Connected: the list
     of devices, USBServiceCall records), 3 when this starts, 4 when it
     goes. A device that goes is deregistered from DeviceFS, which issues
     Service_DeviceDead.
   - USBDriver_Version (&54A45) -> 131.

   Which devices: the ones Linux gives RISC OS. The image's rol-usbd gives
   RISC OS's user the devices Linux itself has no driver for (phones in
   MTP or PTP mode, cameras, ...); keyboards, mice, discs and sound stay
   Linux's. They're found in /dev/bus/usb, as the nodes this can open.

   Not done: isochronous endpoints, HID reports (the report field), USB
   keyboards and mice (Linux has those: rol-fb passes them on), the host
   controller SWIs.

   Transfers are Linux URBs, submitted without waiting and collected on a
   timer (every 2 cs) and whenever a program calls in.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
   Written from RISC OS Open's USBDriver (build/c/usbmodule: Apache 2.0
   and BSD) as the description of the interface, and Linux's usbfs
   (include/uapi/linux/usbdevice_fs.h). */
#include "cmodule.h"
#include "../LinuxCodec/rolsys.h"

#define NR_getdents64	217
#define NR_lseek	19
#define NR_read		3
#define O_DIRECTORY	040000
#define ENODEV		19
#define ESHUTDOWN	108
#define EPIPE		32
#define ENOENT		2
#define ETIMEDOUT	110
#define EOVERFLOW	75
#define EPROTO		71
#define EILSEQ		84
#define ECONNRESET	104

#define OS_ServiceCall		0x30
#define OS_CallEvery		0x3C
#define OS_RemoveTickerEvent	0x3D
#define OS_RemoveCallBack	0x5F
#define OS_ReadMonotonicTime	0x42
#define OS_FSControl		0x29
#define DeviceFS_Register	0x42740
#define DeviceFS_Deregister	0x42741
#define Buffer_InternalInfo	0x42949

/* usbfs (32-bit forms) */
#define UIOC(dir, nr, size)	IOC (dir, 'U', nr, size)
struct urb32 { unsigned char type, endpoint; int status; unsigned flags, buffer; int length, actual, start_frame, packets, errors; unsigned signr, context; };
struct ctrl32 { unsigned char rtype, req; unsigned short value, index, length; unsigned timeout, data; };
struct setif { unsigned iface, alt; };
typedef char check_urb[sizeof (struct urb32) == 44 ? 1 : -1];
typedef char check_ctrl[sizeof (struct ctrl32) == 16 ? 1 : -1];
#define USBDEVFS_CONTROL32	UIOC (3, 0, sizeof (struct ctrl32))
#define USBDEVFS_SETINTERFACE	UIOC (2, 4, sizeof (struct setif))
#define USBDEVFS_SUBMITURB32	UIOC (2, 10, sizeof (struct urb32))
#define USBDEVFS_DISCARDURB	UIOC (0, 11, 0)
#define USBDEVFS_REAPURBNDELAY32 UIOC (1, 13, 4)
#define USBDEVFS_CLAIMINTERFACE	UIOC (2, 15, 4)
#define USBDEVFS_RELEASEINTERFACE UIOC (2, 16, 4)
#define USBDEVFS_CLEAR_HALT	UIOC (2, 21, 4)
#define USBDEVFS_GET_SPEED	UIOC (0, 31, 0)
#define URB_TYPE_INTERRUPT	1
#define URB_TYPE_BULK		3
#define URB_ZERO_PACKET		0x40

/* DeviceFS */
enum { DC_Initialise, DC_Finalise, DC_WakeUpTX, DC_WakeUpRX, DC_SleepRX, DC_EnumDir, DC_CreateBufferTX,
       DC_CreateBufferRX, DC_Halt, DC_Resume, DC_EndOfData, DC_StreamCreated, DC_MonitorTX, DC_MonitorRX };
#define DC_USBRequest	0x80000000u
#define DC_BufferSpace	0x80000002u
#define DC_GetHandles	0x80000003u
#define DC_GetLocation	0x80000004u
#define DC_ClearStall	0x80000005u
#define DC_TransferInfo	0x80000006u
#define DC_GetHandles2	0x80000007u
#define DC_GetSetOptions 0x80000008u

/* the buffer manager's service routine */
enum { BM_InsertByte, BM_InsertBlock, BM_RemoveByte, BM_RemoveBlock, BM_ExamineByte, BM_ExamineBlock,
       BM_UsedSpace, BM_FreeSpace, BM_PurgeBuffer, BM_NextFilledBlock };
unsigned call_bufman (regs *r, void *routine, void *ws);
static void *bm_routine, *bm_ws;

void driver_entry (void);
void ticker_entry (void);
void callback_entry (void);

#define DEAD		0xDEADDEADu
#define UE_CONTROL	0
#define UE_ISOCHRONOUS	1
#define UE_BULK		2
#define UE_INTERRUPT	3
#define FLAG_NOPAD	1
#define FLAG_SHORT	2
#define FLAG_VALIDMASK	3
/* the transfer's state, as USBDriver's xfer->status */
#define X_DONE		0	/* USBD_NORMAL_COMPLETION */
#define X_BUSY		1	/* USBD_IN_PROGRESS */
#define X_FAILED	2	/* anything else */
enum { RS_Idle, RS_Busy, RS_Complete };

#define MAXDEV		16
#define DESC_MAX	4096
#define XFER_MAX	65536

typedef struct ustream ustream;
typedef struct udev udev;

struct ustream
{
  udev *dev;
  int ep;			/* address, with 0x80 for IN */
  int type;			/* UE_BULK, UE_INTERRUPT */
  int mps;
  unsigned fs_stream;		/* DeviceFS's stream handle (0: closed) */
  unsigned buffer, buffer_id;
  unsigned flags, timeout, size;
  int xstatus, busy;		/* X_*; a URB is in flight */
  int read_status, delayed_read;
  unsigned count, totalcount, nextcount, padded;
  int repeat;			/* an interrupt IN: always reading */
  int dying;
  unsigned started;		/* (cs) the transfer in flight began */
  struct urb32 urb;
  unsigned char *xbuf;
  unsigned nsub, nreap, lastlen;	/* (for *USBDevices) */
  int kick;			/* discarded to be sent again (QEMU) */
  int suberr, lastst, lastact;
};

struct udev
{
  int used, unit, fd, gone;
  int bus, addr, speed;
  char path[40];
  /* DeviceFS_Register's device block (USBDriver's dev_struct): */
  struct { int name_offset, flags, tx_flags, tx_size, rx_flags, rx_size, reserved, end; char name[24]; } reg;
  unsigned devfs;		/* DeviceFS's device handle */
  unsigned claimed;		/* interfaces claimed (bit per interface) */
  int alt[32];
  unsigned char desc[DESC_MAX];	/* device descriptor, then the configuration's */
  int desclen;
  ustream *str[32];		/* by endpoint: address, +16 for IN */
};

static udev devs[MAXDEV];
/* a trace of what happened (*USBDevices trace) */
#define TRACE 256
static unsigned tr_w[TRACE][4], tr_n;
static void
trace (unsigned what, unsigned a, unsigned b, unsigned c)
{
  if (tr_n)
    {
      unsigned *l = tr_w[(tr_n - 1) & (TRACE - 1)];
      if (l[0] == what && l[1] == a && l[2] == b && l[3] == c)
	return;			/* (the same again) */
    }
  unsigned *t = tr_w[tr_n++ & (TRACE - 1)];
  t[0] = what, t[1] = a, t[2] = b, t[3] = c;
}
static int next_unit = 1, started, polling, qemu_kick;
static unsigned state[2];	/* the ticker's: [0] callback pending */
static unsigned last_scan;

ERROR_BLOCK (e_nomem, 0x819C01, "USB: not enough memory");
ERROR_BLOCK (e_noendpoint, 0x819C02, "USB: no such endpoint");
ERROR_BLOCK (e_used, 0x819C03, "USB: endpoint in use");
ERROR_BLOCK (e_iface, 0x819C04, "USB: can't claim the interface (is Linux using it?)");
ERROR_BLOCK (e_alt, 0x819C05, "USB: can't set the alternate setting");
ERROR_BLOCK (e_nostream, 0x819C06, "USB: no such stream");
ERROR_BLOCK (e_request, 0x819C07, "USB: the request failed");
ERROR_BLOCK (e_failed, 0x819C00, "USB transfer failed: IO Error");
ERROR_BLOCK (e_notsupp, 0x819C08, "USB: isochronous endpoints and HID reports aren't provided under RISC OS for Linux");
ERROR_BLOCK (e_swi, 0x819C09, "This USBDriver SWI isn't provided under RISC OS for Linux (Linux drives the USB hardware)");
ERROR_BLOCK (e_badswi, 0x1E6, "Unknown USBDriver SWI");

/* (libgcc's divide by zero handler wants it) */
int raise (int sig);
int
raise (int sig)
{
  (void) sig;
  return 0;
}

static unsigned
now_cs (void)
{
  regs r = {{ 0 }};
  swix_regs (OS_ReadMonotonicTime, &r);
  return r.r[0];
}

static unsigned
le16 (const unsigned char *p)
{
  return (unsigned) p[0] | (unsigned) p[1] << 8;
}

static void
bm (regs *r)
{
  if (bm_routine)
    call_bufman (r, bm_routine, bm_ws);
}

/* --- descriptors ----------------------------------------------------------- */

/* the first configuration descriptor (and what follows it) */
static const unsigned char *
config (udev *d, int *len)
{
  if (d->desclen < 18 + 9)
    return 0;
  const unsigned char *c = d->desc + 18;
  int total = (int) le16 (c + 2);
  if (c[1] != 2 || total < 9 || 18 + total > d->desclen)
    total = d->desclen - 18;
  *len = total;
  return c;
}

typedef struct { unsigned endpoint, interface, alternate, report, ep_type, timeout, size, nopad, shortflag; } device_valid;

/* USBDriver's find_interface_and_endpoint: the first endpoint that matches
   what was given, in the right direction (flags bit 0: TX, an OUT one) */
static int
find_endpoint (udev *d, device_valid *v, unsigned dir, int *iface, int *alt, int *ep, int *mps)
{
  if (v->ep_type == DEAD && v->endpoint == DEAD)
    v->ep_type = UE_BULK;
  int len, i = -1, j = 0;
  const unsigned char *c = config (d, &len), *p = c;
  if (!c)
    return -1;
  while (p + 2 <= c + len && p[0] >= 2)
    {
      if (p[1] == 4 && p[0] >= 9)
	i = p[2], j = p[3];
      else if (p[1] == 5 && p[0] >= 7 && i >= 0)
	{
	  unsigned a = p[2], t = p[3] & 3;
	  if ((v->interface == (unsigned) i || v->interface == DEAD)
	      && (v->alternate == (unsigned) j || v->alternate == DEAD)
	      && (v->endpoint == (a & 15) || v->endpoint == DEAD)
	      && (v->ep_type == t || v->ep_type == DEAD)
	      && (((dir & 1) << 7) != (a & 0x80)))
	    {
	      *iface = i;
	      *alt = j;
	      *ep = (int) a;
	      *mps = (int) (le16 (p + 4) & 0x7FF);
	      v->ep_type = t;
	      return 0;
	    }
	}
      p += p[0];
    }
  return -1;
}

/* --- transfers --------------------------------------------------------------- */

static int
submit (ustream *s, unsigned len, int zlp)
{
  struct urb32 *u = &s->urb;
  memset (u, 0, sizeof *u);
  u->type = s->type == UE_INTERRUPT ? URB_TYPE_INTERRUPT : URB_TYPE_BULK;
  u->endpoint = (unsigned char) s->ep;
  u->flags = zlp ? URB_ZERO_PACKET : 0;
  u->buffer = (unsigned) s->xbuf;
  u->length = (int) len;
  u->signr = 0;
  u->context = (unsigned) s;
  int e = lioctl (s->dev->fd, USBDEVFS_SUBMITURB32, u);
  s->nsub++;
  s->lastlen = len;
  trace ('S', (unsigned) s->ep, len, (unsigned) e);
  if (e < 0)
    {
      s->suberr = e;
      s->busy = 0;
      s->xstatus = X_FAILED;
      if (e == -ENODEV || e == -ESHUTDOWN)
	s->dev->gone = 1;
      return -1;
    }
  s->busy = 1;
  s->xstatus = X_BUSY;
  s->started = now_cs ();
  return 0;
}

static void start_read (ustream *s);
static void start_write (ustream *s);

/* USBDriver's start_write: what's in the buffer goes as one transfer */
static void
start_write (ustream *s)
{
  if (s->busy || s->xstatus == X_FAILED || s->dev->gone || !s->buffer_id)
    return;
  regs r = {{ BM_UsedSpace, s->buffer_id }};
  bm (&r);
  unsigned n = r.r[2];
  if (!n)
    return;
  if (n > XFER_MAX)
    n = XFER_MAX;
  regs x = {{ BM_ExamineBlock, s->buffer_id, (unsigned) s->xbuf, n }};
  bm (&x);
  n -= x.r[3];			/* (what couldn't be copied) */
  int zlp = (s->flags & FLAG_SHORT) && s->mps && n % (unsigned) s->mps == 0;
  submit (s, n, zlp);
}

/* USBDriver's start_read: a bulk read only when one has been asked for
   (WakeUpRX), as much as there's room for in whole packets, up to what's
   asked; an interrupt IN reads a packet at a time, always */
static void
start_read (ustream *s)
{
  if (s->busy || s->xstatus == X_FAILED || s->dev->gone || !s->buffer_id)
    return;
  if (s->read_status == RS_Busy && s->count >= s->totalcount && !s->repeat)
    {
      s->read_status = RS_Complete;
      if (!s->delayed_read)
	return;
    }
  if (!s->repeat)
    {
      if (s->read_status != RS_Busy)
	{
	  if (!s->delayed_read)
	    return;
	  s->delayed_read = 0;
	  s->totalcount = s->nextcount;
	  s->count = 0;
	  s->padded = 0;
	  s->read_status = RS_Busy;
	}
    }
  regs r = {{ BM_FreeSpace, s->buffer_id }};
  bm (&r);
  int n = (int) r.r[2];
  int mps = s->mps ? s->mps : 64;
  if (n < mps)
    return;
  if (s->type == UE_INTERRUPT)
    n = mps;
  else
    {
      n -= n % mps;
      if (s->totalcount && n > (int) (s->totalcount - s->count))
	{
	  int want = (int) (s->totalcount - s->count);
	  if (want <= 0)
	    return;
	  /* whole packets still: a device sends whole packets, and a
	     transfer shorter than its packet overflows (USBDriver's host
	     controllers fill a whole-packet buffer and give it all) */
	  want += mps - 1;
	  want -= want % mps;
	  if (want < n)
	    n = want;
	}
    }
  if (n > XFER_MAX)
    n = XFER_MAX - XFER_MAX % mps;
  submit (s, (unsigned) n, 0);
}

static void
insert (ustream *s, const unsigned char *p, int n)
{
  regs r = {{ BM_InsertBlock, s->buffer_id, (unsigned) p, (unsigned) n }};
  bm (&r);
}

/* a URB has come back: USBDriver's read_cb / write_cb */
static void
done (ustream *s, struct urb32 *u)
{
  s->busy = 0;
  int st = u->status, act = u->actual > 0 ? u->actual : 0;
  s->nreap++;
  s->lastst = st;
  s->lastact = act;
  if (s->kick && st == -ENOENT && !act)
    {
      s->kick = 0;
      submit (s, s->lastlen, 0);	/* (the same again) */
      return;
    }
  if (qemu_kick && st == -EPIPE && !act && (s->ep & 0x80))
    {
      /* (QEMU's MTP device stalls an IN transfer while it's still taking
         a command's data, where a real one NAKs it: clear it, and ask
         again after the next write) */
      unsigned ep = (unsigned) s->ep;
      lioctl (s->dev->fd, USBDEVFS_CLEAR_HALT, &ep);
      s->kick = 2;
      s->xstatus = X_BUSY;
      return;
    }
  s->kick = 0;
  if (st == -ENODEV || st == -ESHUTDOWN)
    s->dev->gone = 1;
  if (st != 0 && !(st == -EOVERFLOW && act))
    {
      s->xstatus = X_FAILED;
      s->delayed_read = 0;
      s->read_status = RS_Complete;
      return;
    }
  s->xstatus = X_DONE;
  if (!s->fs_stream || !s->buffer_id)
    {
      s->delayed_read = 0;
      s->read_status = RS_Complete;
      return;
    }
  if (!(s->ep & 0x80))
    {
      /* written: out of the buffer now, then the next */
      unsigned char junk[256];
      int left = act;
      while (left > 0)
	{
	  int k = left > (int) sizeof junk ? (int) sizeof junk : left;
	  regs r = {{ BM_RemoveBlock, s->buffer_id, (unsigned) junk, (unsigned) k }};
	  bm (&r);
	  left -= k;
	}
      /* (USBDriver$QEMU set: the device's IN endpoints are asked again
         after each write. QEMU's USB devices don't answer an IN transfer
         that was waiting before they had data; real ones do.) */
      if (qemu_kick)
	for (int i = 16; i < 32; i++)
	  {
	    ustream *t = s->dev->str[i];
	    if (t && t->kick == 2 && !t->busy)
	      {
		t->kick = 0;
		submit (t, t->lastlen, 0);
	      }
	    else if (t && t->busy && !t->kick)
	      {
		t->kick = 1;
		lioctl (s->dev->fd, USBDEVFS_DISCARDURB, &t->urb);
	      }
	  }
      start_write (s);
      return;
    }
  /* read */
  if (act)
    insert (s, s->xbuf, act);
  s->count += (unsigned) act;
  if (s->repeat)
    {
      start_read (s);
      return;
    }
  int delayed = s->delayed_read;
  if ((s->flags & FLAG_NOPAD) || s->count >= s->totalcount)
    {
      s->read_status = RS_Complete;
      if (delayed)
	start_read (s);
      return;
    }
  if (act % (s->mps ? s->mps : 64))
    {
      /* a short packet: the transfer has ended; the rest of what was asked
         for is padded with zeros (USBDriver's "DeviceFS concept") */
      s->read_status = RS_Complete;
      unsigned char zero[64];
      memset (zero, 0, sizeof zero);
      int remain = (int) (s->padded = s->totalcount - s->count);
      while (remain > 0)
	{
	  int k = remain > 64 ? 64 : remain;
	  regs r = {{ BM_InsertBlock, s->buffer_id, (unsigned) zero, (unsigned) k }};
	  bm (&r);
	  remain -= k;
	  if (r.r[3])
	    break;
	}
      if (delayed)
	start_read (s);
    }
  else
    start_read (s);
}

static void
reap (udev *d)
{
  if (d->fd < 0)
    return;
  for (int k = 0; k < 64; k++)
    {
      unsigned p = 0;
      int e = lioctl (d->fd, USBDEVFS_REAPURBNDELAY32, &p);
      if (e < 0 && e != -EAGAIN)
	trace ('E', (unsigned) -e, 0, 0);
      if (e < 0)
	{
	  if (e == -ENODEV || e == -ESHUTDOWN)
	    d->gone = 1;
	  return;
	}
      struct urb32 *u = (struct urb32 *) p;
      ustream *s = u ? (ustream *) u->context : 0;
      trace ('R', u ? u->endpoint : 0, u ? (unsigned) u->actual : 0, u ? (unsigned) -u->status : 0);
      if (s)
	done (s, u);
    }
}

/* transfers that have run out of time (usbtimeout, in ms) */
static void
timeouts (udev *d)
{
  unsigned t = now_cs ();
  for (int i = 0; i < 32; i++)
    {
      ustream *s = d->str[i];
      if (s && s->busy && s->timeout && t - s->started > (s->timeout + 9) / 10)
	lioctl (d->fd, USBDEVFS_DISCARDURB, &s->urb);	/* (reaped with -ENOENT) */
    }
}

/* --- devices ----------------------------------------------------------------- */

/* USBServiceCall: sclen(2) descoff(2) devname(20) bus devaddr hostaddr
   hostport speed spare*3, then the device descriptor and the
   configuration's descriptors (with a link word in front, for lists) */
static unsigned char *
record (udev *d, int link)
{
  int clen;
  const unsigned char *c = config (d, &clen);
  if (!c)
    clen = 0;
  unsigned body = 32 + 18 + (unsigned) clen + 1;
  body = (body + 3) & ~3u;
  unsigned char *b = rma_claim (body + (link ? 4 : 0));
  if (!b)
    return 0;
  memset (b, 0, body + (link ? 4 : 0));
  unsigned char *s = b + (link ? 4 : 0);
  s[0] = (unsigned char) body;
  s[1] = (unsigned char) (body >> 8);
  s[2] = 32;
  for (int i = 0; d->reg.name[i] && i < 19; i++)
    s[4 + i] = (unsigned char) d->reg.name[i];
  s[24] = (unsigned char) d->bus;
  s[25] = (unsigned char) d->addr;
  s[26] = 0;
  s[27] = 0;
  s[28] = (unsigned char) d->speed;
  memcpy (s + 32, d->desc, 18);
  if (clen)
    memcpy (s + 50, c, (size_t) clen);
  return b;
}

static void
put_dec (char *o, unsigned v)
{
  char t[12];
  int n = 0;
  do
    t[n++] = (char) ('0' + v % 10);
  while ((v /= 10) && n < 11);
  while (n)
    *o++ = t[--n];
  *o = 0;
}

static void
remove_device (udev *d)
{
  for (int i = 0; i < 32; i++)
    if (d->str[i])
      {
	d->str[i]->dying = 1;
	d->str[i]->xstatus = X_FAILED;
      }
  if (d->devfs)
    {
      regs r = {{ d->devfs }};
      d->devfs = 0;
      swix_regs (DeviceFS_Deregister, &r);	/* (DeviceFS: Service_DeviceDead) */
    }
  for (int i = 0; i < 32; i++)
    if (d->str[i])
      {
	if (d->str[i]->busy && d->fd >= 0)
	  lioctl (d->fd, USBDEVFS_DISCARDURB, &d->str[i]->urb);
      }
  if (d->fd >= 0)
    {
      reap (d);
      lclose (d->fd);
      d->fd = -1;
    }
  for (int i = 0; i < 32; i++)
    if (d->str[i])
      {
	rma_free (d->str[i]->xbuf);
	rma_free (d->str[i]);
	d->str[i] = 0;
      }
  d->used = 0;
}

static void
add_device (const char *path, int fd, int bus, int addr)
{
  udev *d = 0;
  for (int i = 0; i < MAXDEV && !d; i++)
    if (!devs[i].used)
      d = &devs[i];
  if (!d)
    {
      lclose (fd);
      return;
    }
  memset (d, 0, sizeof *d);
  d->fd = fd;
  d->bus = bus;
  d->addr = addr;
  int i = 0;
  for (; path[i] && i < (int) sizeof d->path - 1; i++)
    d->path[i] = path[i];
  d->path[i] = 0;
  lsys (NR_lseek, (unsigned) fd, 0, 0, 0, 0, 0);
  int n = lsys (NR_read, (unsigned) fd, (unsigned) d->desc, DESC_MAX, 0, 0, 0);
  d->desclen = n > 0 ? n : 0;
  if (d->desclen < 18 || d->desc[1] != 1 || d->desc[4] == 9)	/* (hubs: Linux's) */
    {
      lclose (fd);
      return;
    }
  int sp = lioctl (fd, USBDEVFS_GET_SPEED, 0);
  d->speed = sp == 1 ? 1 : sp == 2 ? 2 : sp >= 3 ? 3 : 2;
  d->unit = next_unit++;
  d->reg.name[0] = 'U', d->reg.name[1] = 'S', d->reg.name[2] = 'B';
  put_dec (d->reg.name + 3, (unsigned) d->unit);
  d->reg.name_offset = (int) ((char *) d->reg.name - (char *) &d->reg);
  d->reg.flags = 3;
  d->reg.tx_flags = 8;
  d->reg.tx_size = 8192;
  d->reg.rx_flags = 8;
  d->reg.rx_size = 1024;
  d->reg.end = 0;
  regs r = {{ 6, (unsigned) &d->reg, (unsigned) driver_entry, (unsigned) d, (unsigned) state,
	      (unsigned) "endpoint/Ninterface/Nalternate/Nreport/Ncontrol,isochronous,bulk,interrupt/Susbtimeout/Nsize/Nnopad/Sshort/S",
	      (unsigned) -1, (unsigned) -1 }};
  if (swix_regs (DeviceFS_Register, &r))
    {
      lclose (fd);
      return;
    }
  d->devfs = r.r[0];
  d->used = 1;
  /* Service_USB 0: Attach */
  unsigned char *b = record (d, 0);
  if (b)
    {
      regs s = {{ 0, 0xD2, (unsigned) b }};
      swix_regs (OS_ServiceCall, &s);
      rma_free (b);
    }
}

/* /dev/bus/usb/BBB/DDD: the ones RISC OS's user may open */
static void
scan (void)
{
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used && devs[i].gone)
      remove_device (&devs[i]);
  static unsigned char dbuf[1024], ebuf[1024];
  int top = lsys (NR_open, (unsigned) "/dev/bus/usb", O_DIRECTORY | O_CLOEXEC, 0, 0, 0, 0);
  if (top < 0)
    return;
  int n;
  while ((n = lsys (NR_getdents64, (unsigned) top, (unsigned) dbuf, sizeof dbuf, 0, 0, 0)) > 0)
    for (int o = 0; o < n;)
      {
	unsigned short reclen = (unsigned short) (dbuf[o + 16] | dbuf[o + 17] << 8);
	const char *bn = (const char *) dbuf + o + 19;
	o += reclen;
	if (bn[0] < '0' || bn[0] > '9')
	  continue;
	char bpath[40] = "/dev/bus/usb/";
	int l = 13;
	for (int k = 0; bn[k] && l < 30; k++)
	  bpath[l++] = bn[k];
	bpath[l] = 0;
	int sub = lsys (NR_open, (unsigned) bpath, O_DIRECTORY | O_CLOEXEC, 0, 0, 0, 0);
	if (sub < 0)
	  continue;
	int m;
	while ((m = lsys (NR_getdents64, (unsigned) sub, (unsigned) ebuf, sizeof ebuf, 0, 0, 0)) > 0)
	  for (int q = 0; q < m;)
	    {
	      unsigned short rl = (unsigned short) (ebuf[q + 16] | ebuf[q + 17] << 8);
	      const char *dn = (const char *) ebuf + q + 19;
	      q += rl;
	      if (dn[0] < '0' || dn[0] > '9')
		continue;
	      char dpath[48];
	      int j = 0;
	      for (; bpath[j]; j++)
		dpath[j] = bpath[j];
	      dpath[j++] = '/';
	      for (int k = 0; dn[k] && j < 46; k++)
		dpath[j++] = dn[k];
	      dpath[j] = 0;
	      int known = 0;
	      for (int k = 0; k < MAXDEV; k++)
		if (devs[k].used)
		  {
		    int x = 0;
		    while (devs[k].path[x] && devs[k].path[x] == dpath[x])
		      x++;
		    if (!devs[k].path[x] && !dpath[x])
		      known = 1;
		  }
	      if (known)
		continue;
	      int fd = lsys (NR_open, (unsigned) dpath, O_RDWR | O_CLOEXEC, 0, 0, 0, 0);
	      if (fd < 0)
		continue;	/* (not given to RISC OS) */
	      int bus = 0, addr = 0;
	      for (int k = 0; bn[k]; k++)
		bus = bus * 10 + bn[k] - '0';
	      for (int k = 0; dn[k]; k++)
		addr = addr * 10 + dn[k] - '0';
	      add_device (dpath, fd, bus, addr);
	    }
	lclose (sub);
      }
  lclose (top);
}

/* the timer's callback, and every call in: transfers, then (every second)
   devices arriving and going */
void
c_poll (void)
{
  if (polling)
    return;
  polling = 1;
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used)
      {
	reap (&devs[i]);
	timeouts (&devs[i]);
	if (devs[i].gone)
	  remove_device (&devs[i]);
      }
  if (now_cs () - last_scan > 100)
    {
      last_scan = now_cs ();
      scan ();
    }
  polling = 0;
}

static int
getvar_set (const char *name)
{
  regs r = {{ (unsigned) name, 0, (unsigned) -1, 0, 0 }};
  swix_regs (0x23, &r);		/* OS_ReadVarVal: R2 < 0 if it exists */
  return (int) r.r[2] < 0;
}

/* --- DeviceFS's calls ------------------------------------------------------- */

static oserror *
stream_create (udev *d, unsigned fs, unsigned flags, device_valid *v, unsigned *handle)
{
  if (v->report != DEAD)
    return ERR (e_notsupp);
  if (v->endpoint != DEAD)
    v->endpoint &= 15;
  int iface, alt, ep, mps;
  if (find_endpoint (d, v, flags, &iface, &alt, &ep, &mps))
    return ERR (e_noendpoint);
  if (v->ep_type == UE_ISOCHRONOUS || v->ep_type == UE_CONTROL)
    return ERR (e_notsupp);
  int index = (ep & 15) + ((ep & 0x80) ? 16 : 0);
  ustream *s = d->str[index];
  if (s && s->fs_stream)
    return ERR (e_used);
  if (!(d->claimed & (1u << iface)))
    {
      unsigned i = (unsigned) iface;
      if (lioctl (d->fd, USBDEVFS_CLAIMINTERFACE, &i) < 0)
	return ERR (e_iface);
      d->claimed |= 1u << iface;
      d->alt[iface] = 0;
    }
  if (alt != d->alt[iface])
    {
      struct setif si = { (unsigned) iface, (unsigned) alt };
      if (lioctl (d->fd, USBDEVFS_SETINTERFACE, &si) < 0)
	return ERR (e_alt);
      d->alt[iface] = alt;
    }
  if (!s)
    {
      /* (an endpoint used before keeps its record, so its toggle carries on) */
      s = rma_claim (sizeof *s);
      if (!s)
	return ERR (e_nomem);
      memset (s, 0, sizeof *s);
      s->xbuf = rma_claim (XFER_MAX);
      if (!s->xbuf)
	{
	  rma_free (s);
	  return ERR (e_nomem);
	}
      d->str[index] = s;
    }
  s->dev = d;
  s->ep = ep;
  s->type = (int) v->ep_type;
  s->mps = mps;
  s->size = v->size;
  s->timeout = v->timeout == DEAD ? 0 : v->timeout;
  s->flags = (v->nopad ? 0 : FLAG_NOPAD) | (v->shortflag ? 0 : FLAG_SHORT);
  s->read_status = RS_Idle;
  s->delayed_read = 0;
  s->count = s->totalcount = s->nextcount = s->padded = 0;
  if (!s->busy)
    s->xstatus = X_DONE;
  s->repeat = s->type == UE_INTERRUPT && (ep & 0x80);
  s->buffer = s->buffer_id = 0;
  s->fs_stream = fs;
  *handle = (unsigned) s;
  return 0;
}

static ustream *
by_fs (udev *d, unsigned fileswitch_handle)
{
  regs r = {{ 21, fileswitch_handle }};
  if (swix_regs (OS_FSControl, &r))
    return 0;
  for (int i = 0; i < 32; i++)
    if (d->str[i] && d->str[i]->fs_stream && d->str[i]->fs_stream == r.r[1])
      return d->str[i];
  return 0;
}

oserror *
c_driver (regs *r)
{
  udev *d = (udev *) r->r[8];
  ustream *s = (ustream *) r->r[2];
  if (!d || !d->used)
    return ERR (e_nostream);
  c_poll ();
  if (r->r[0] != DC_BufferSpace && r->r[0] != DC_TransferInfo && !(r->r[0] == DC_WakeUpRX && s && s->busy))
    trace ('D', r->r[0], s ? (unsigned) s->ep : 0xFF, r->r[3]);
  switch (r->r[0])
    {
    case DC_Initialise:
      return stream_create (d, r->r[2], r->r[3], (device_valid *) r->r[6], &r->r[2]);
    case DC_Finalise:
      if (!s)
	{
	  for (int i = 0; i < 32; i++)
	    if (d->str[i])
	      d->str[i]->fs_stream = 0, d->str[i]->buffer_id = 0;
	}
      else
	{
	  /* the endpoint's record stays, with any transfer in flight (as
	     USBDriver: it isn't stopped) */
	  s->fs_stream = 0;
	  s->buffer = s->buffer_id = 0;
	}
      break;
    case DC_WakeUpTX:
      if (s)
	{
	  s->count = 0;
	  start_write (s);
	}
      break;
    case DC_WakeUpRX:
      if (s && !s->repeat && r->r[3])
	{
	  s->delayed_read = 1;
	  s->nextcount = r->r[3];
	  start_read (s);
	}
      break;
    case DC_CreateBufferTX:
    case DC_CreateBufferRX:
      if (s)
	{
	  if (s->size != DEAD)
	    r->r[4] = s->size;
	  r->r[6] = r->r[4] - (unsigned) s->mps;
	}
      break;
    case DC_Resume:
      if (s)
	{
	  if (s->ep & 0x80)
	    {
	      if (!s->repeat)
		start_read (s);
	    }
	  else
	    start_write (s);
	}
      break;
    case DC_EndOfData:
      if (s && s->xstatus == X_BUSY)
	r->r[3] = 0;
      break;
    case DC_StreamCreated:
      if (s)
	{
	  s->buffer = r->r[3];
	  regs b = {{ r->r[3] }};
	  if (!swix_regs (Buffer_InternalInfo, &b))
	    {
	      s->buffer_id = b.r[0];
	      bm_routine = (void *) b.r[1];
	      bm_ws = (void *) b.r[2];
	    }
	  /* (USBDriver$QEMU set: an interrupt IN endpoint is only read when
	     asked, not always, for QEMU's MTP device: see done) */
	  qemu_kick = getvar_set ("USBDriver$QEMU");
	  if (s->repeat && qemu_kick)
	    s->repeat = 0;
	  if (s->repeat)
	    start_read (s);
	}
      break;
    case DC_MonitorTX:
    case DC_MonitorRX:
      if (s && s->xstatus == X_FAILED)
	{
	  regs p = {{ BM_PurgeBuffer, s->buffer_id }};
	  bm (&p);
	  return ERR (e_failed);
	}
      break;
    case DC_USBRequest:
      {
	struct ctrl32 c;
	c.rtype = (unsigned char) r->r[3];
	c.req = (unsigned char) (r->r[3] >> 8);
	c.value = (unsigned short) (r->r[3] >> 16);
	c.index = (unsigned short) r->r[4];
	c.length = (unsigned short) (r->r[4] >> 16);
	c.timeout = 1000;
	c.data = r->r[5];
	if (lioctl (d->fd, USBDEVFS_CONTROL32, &c) < 0)
	  return ERR (e_request);
      }
      break;
    case DC_BufferSpace:
      {
	ustream *t = by_fs (d, r->r[2]);
	if (!t || !t->buffer_id)
	  return ERR (e_nostream);
	regs a = {{ BM_FreeSpace, t->buffer_id }};
	bm (&a);
	unsigned fr = a.r[2];
	regs u = {{ BM_UsedSpace, t->buffer_id }};
	bm (&u);
	r->r[4] = fr;
	r->r[3] = fr + u.r[2];
      }
      break;
    case DC_GetHandles:
    case DC_GetHandles2:
      {
	ustream *t = by_fs (d, r->r[2]);
	if (!t)
	  return ERR (e_nostream);
	r->r[3] = t->buffer;
	r->r[4] = t->fs_stream;
	if (r->r[0] == DC_GetHandles2)
	  {
	    r->r[5] = (unsigned) t;
	    r->r[6] = d->devfs;
	  }
      }
      break;
    case DC_GetLocation:
      if (r->r[3])
	{
	  unsigned char *l = (unsigned char *) r->r[3];
	  memset (l, 0, 6);
	  l[0] = (unsigned char) d->bus;
	  l[1] = (unsigned char) d->addr;
	}
      break;
    case DC_ClearStall:
      {
	ustream *t = by_fs (d, r->r[2]);
	if (!t)
	  return ERR (e_nostream);
	unsigned ep = (unsigned) t->ep;
	if (lioctl (d->fd, USBDEVFS_CLEAR_HALT, &ep) < 0)
	  return ERR (e_request);
	t->xstatus = X_DONE;
      }
      break;
    case DC_TransferInfo:
      if (!s)
	return ERR (e_nostream);
      r->r[0] = s->count;
      r->r[1] = s->totalcount;
      r->r[3] = s->xstatus == X_DONE ? (s->delayed_read ? 0u : 1u) : s->xstatus == X_BUSY ? 0u : (unsigned) -1;
      r->r[4] = s->padded;
      break;
    case DC_GetSetOptions:
      if (!s)
	return ERR (e_nostream);
      {
	unsigned nw = ((s->flags & r->r[4]) ^ r->r[3]) & FLAG_VALIDMASK;
	r->r[3] = s->flags;
	s->flags = r->r[4] = nw;
      }
      break;
    }
  return 0;
}

/* --- the module ------------------------------------------------------------------ */

oserror *
mod_swi (unsigned number, regs *r)
{
  if (number == 5)
    {
      r->r[0] = 131;
      return 0;
    }
  return number < 6 ? ERR (e_swi) : ERR (e_badswi);
}

void
mod_service (regs *r)
{
  if (r->r[1] == 0xD2 && r->r[0] == 1 && started)
    {
      /* Connected: add ours to the list (each with a link word) */
      c_poll ();
      unsigned char *list = (unsigned char *) r->r[2];
      for (int i = MAXDEV - 1; i >= 0; i--)
	if (devs[i].used && !devs[i].gone)
	  {
	    unsigned char *b = record (&devs[i], 1);
	    if (!b)
	      continue;
	    memcpy (b, &list, 4);
	    list = b;
	  }
      r->r[2] = (unsigned) list;
    }
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  memset (devs, 0, sizeof devs);
  for (int i = 0; i < MAXDEV; i++)
    devs[i].fd = -1;
  next_unit = 1;
  polling = 0;
  state[0] = 0;
  bm_routine = bm_ws = 0;
  if (lsys (20 /* getpid */, 0, 0, 0, 0, 0, 0) == -ENOSYS)
    {
      static const struct { int n; char m[48]; } e = { 0x819C0A, "USBDriver (RISC OS for Linux) needs Linux" };
      return (oserror *) &e;
    }
  regs t = {{ 1, (unsigned) ticker_entry, (unsigned) state }};	/* every 2 cs */
  oserror *e = swix_regs (OS_CallEvery, &t);
  if (e)
    return e;
  started = 1;
  regs s = {{ 3, 0xD2 }};			/* Service_USB 3: USBDriver starting */
  swix_regs (OS_ServiceCall, &s);
  last_scan = now_cs () - 1000;
  scan ();
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  regs s = {{ 4, 0xD2 }};			/* Service_USB 4: dying */
  swix_regs (OS_ServiceCall, &s);
  regs t = {{ (unsigned) ticker_entry, (unsigned) state }};
  swix_regs (OS_RemoveTickerEvent, &t);
  regs c = {{ (unsigned) callback_entry, (unsigned) state }};
  swix_regs (OS_RemoveCallBack, &c);
  started = 0;
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used)
      remove_device (&devs[i]);
  return 0;
}

/* The USB devices Linux keeps, from the list the image's rol-usbd writes
   (/riscos/mediactl/usb: tab-separated lines, R or L, vendor:product,
   speed in Mb/s, name, drivers, discs; "H", hubs) */
static void
linux_devices (void)
{
  static char buf[8192];
  int fd = lsys (NR_open, (unsigned) "/riscos/mediactl/usb", O_CLOEXEC /* O_RDONLY */, 0, 0, 0, 0);
  if (fd < 0)
    return;
  int n = lsys (NR_read, (unsigned) fd, (unsigned) buf, sizeof buf - 1, 0, 0, 0);
  lclose (fd);
  if (n <= 0)
    return;
  buf[n] = 0;
  int title = 0;
  for (char *l = buf; *l; )
    {
      char *end = l;
      while (*end && *end != '\n')
	end++;
      char *next = *end ? end + 1 : end;
      *end = 0;
      char *f[6] = { 0 };
      int nf = 0;
      for (char *c = l; nf < 6; )
	{
	  f[nf++] = c;
	  while (*c && *c != '\t')
	    c++;
	  if (!*c)
	    break;
	  *c++ = 0;
	}
      if (f[0][0] == 'L' && nf >= 5)
	{
	  if (!title++)
	    {
	      newline ();
	      write0 ("Linux's USB devices (its drivers have them):");
	      newline ();
	    }
	  write0 ("  ");
	  write0 (f[3]);
	  write0 ("  ");
	  write0 (f[1]);
	  write0 ("  ");
	  write0 (f[2]);
	  write0 (" Mb/s  ");
	  write0 (f[4][0] ? f[4] : "(no driver yet)");
	  if (nf >= 6 && f[5][0])
	    {
	      newline ();
	      write0 ("    disc: ");
	      write0 (f[5]);
	    }
	  newline ();
	}
      else if (f[0][0] == 'H' && nf >= 2 && f[1][0] != '0')
	{
	  write0 ("  (hubs: ");
	  write0 (f[1]);
	  write0 (")");
	  newline ();
	}
      l = next;
    }
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  c_poll ();
  int any = 0;
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used)
      {
	udev *d = &devs[i];
	write0 (d->reg.name);
	write0 ("  bus ");
	write_dec ((unsigned) d->bus);
	write0 (" address ");
	write_dec ((unsigned) d->addr);
	write0 ("  vendor &");
	write_hex8 (le16 (d->desc + 8));
	write0 (" product &");
	write_hex8 (le16 (d->desc + 10));
	int len;
	const unsigned char *c = config (d, &len), *p = c;
	while (c && p + 2 <= c + len && p[0] >= 2)
	  {
	    if (p[1] == 4 && p[0] >= 9)
	      {
		newline ();
		write0 ("    interface ");
		write_dec (p[2]);
		write0 (" class ");
		write_dec (p[5]);
		write0 ("/");
		write_dec (p[6]);
		write0 ("/");
		write_dec (p[7]);
		if (p[5] == 6 && p[6] == 1 && p[7] == 1)
		  write0 (" (MTP/PTP)");
	      }
	    p += p[0];
	  }
	newline ();
	for (int k = 0; k < 32; k++)
	  if (d->str[k])
	    {
	      ustream *t = d->str[k];
	      write0 ("    endpoint &");
	      write_hex8 ((unsigned) t->ep);
	      write0 (t->fs_stream ? " open" : " closed");
	      write0 (" buffer ");
	      write_dec (t->buffer_id ? 1 : 0);
	      write0 (" sent ");
	      write_dec (t->nsub);
	      write0 (" back ");
	      write_dec (t->nreap);
	      write0 (" busy ");
	      write_dec ((unsigned) t->busy);
	      write0 (" state ");
	      write_dec ((unsigned) t->xstatus);
	      write0 (" len ");
	      write_dec (t->lastlen);
	      write0 (" got ");
	      write_dec ((unsigned) t->lastact);
	      write0 (" status ");
	      write_dec ((unsigned) -t->lastst);
	      write0 (" err ");
	      write_dec ((unsigned) -t->suberr);
	      write0 (" count ");
	      write_dec (t->count);
	      write0 ("/");
	      write_dec (t->totalcount);
	      newline ();
	    }
	any = 1;
      }
  if (argc)
    {
      unsigned from = tr_n > TRACE ? tr_n - TRACE : 0;
      for (unsigned i = from; i < tr_n; i++)
	{
	  unsigned *t = tr_w[i & (TRACE - 1)];
	  char w[2] = { (char) t[0], 0 };
	  write0 (w);
	  write0 (" ");
	  write_hex8 (t[1]);
	  write0 (" ");
	  write_hex8 (t[2]);
	  write0 (" ");
	  write_hex8 (t[3]);
	  newline ();
	}
    }
  if (!any)
    {
      write0 ("No USB devices given to RISC OS (Linux keeps those it has drivers for: keyboards, discs, ...)");
      newline ();
    }
  linux_devices ();
  return 0;
}
