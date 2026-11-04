/* SCSILinux: FileCore discs through Linux, for RISC OS's SCSIFS.

   On a native Raspberry Pi, USB discs reach SCSIFS through SCSISwitch
   (RISC OS's SCSI SWIs) and SCSISoftUSB (USB mass storage). On RISC OS on
   Linux, Linux owns the USB discs: the image's rol-usbd mounts FAT, exFAT,
   NTFS and ext discs for IXFS, and gives a disc in RISC OS's own FileCore
   format to RISC OS whole. This module is the SCSISwitch driver for those:
   one SCSI device for each disc, and each SCSI command SCSIFS (or
   SCSISwitch) sends is done on the disc itself - reads and writes of its
   sectors, its size, and replies for the rest.

   The discs: rol-usbd lists them in /riscos/mediactl/filecore (device,
   size, disc name; tab-separated), and opens one for us when asked on its
   abstract socket "rol-usbd-discs" ("OPEN sdb": the reply carries the open
   disc). RISC OS can't open Linux's /dev itself. For testing, the system
   variable SCSILinux$Test names a disc image file (a Linux path), which
   becomes a disc too.

   Commands are done at once (foreground), with scatter lists. Writes reach
   the disc before the command ends (rol-usbd opens it O_DSYNC).

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */

#include "cmodule.h"

#define IXSupport_LinuxSyscall	0x59EC0
#define OS_ReadVarVal		0x23
#define OS_CallEvery		0x3C
#define OS_RemoveTickerEvent	0x3D
#define OS_RemoveCallBack	0x5F
#define SCSI_Deregister		0x403FE
#define SCSI_Register		0x403FF

#define NR_read		3
#define NR_open		5
#define NR_close	6
#define NR_ioctl	54
#define NR_fsync	118
#define NR_llseek	140
#define NR_pread64	180
#define NR_pwrite64	181
#define NR_socket	281
#define NR_connect	283
#define NR_send		289
#define NR_recvmsg	297
#define O_RDWR		2
#define O_CLOEXEC	02000000
#define O_DSYNC		010000
#define MSG_CMSG_CLOEXEC 0x40000000
#define BLKSSZGET	0x1268

#define MAXDEV		8

void scsi_entry (void);
void ticker_entry (void);
void callback_entry (void);

static int
streq (const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

static int
lsys (int nr, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
  regs r = {{ a, b, c, d, e, f, 0, (unsigned) nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -38;
  return (int) r.r[0];
}

struct dev
{
  int used;
  char src[64];			/* rol-usbd's device (sdb), or the test file */
  char name[16];		/* its FileCore disc name, for INQUIRY */
  int fd;
  int ro;
  int removable;		/* a stick or card (SCSIFS drives 0-3), not a
				   hard disc (4-7): as Linux says */
  unsigned log2ss;		/* sector size */
  unsigned long long bytes, sectors;
  int registered;		/* with SCSISwitch */
  unsigned id;			/* SCSISwitch's device ID for it */
  int seen;			/* still listed (this poll) */
  unsigned char sense[18];	/* for REQUEST SENSE */
  unsigned reads, writes, errors;
};

static struct dev devs[MAXDEV];
static unsigned state[2];	/* the ticker's: [0] callback pending */
static int started;

/* --- the discs ---------------------------------------------------------- */

static unsigned long long
file_size (int fd)
{
  unsigned long long r = 0;
  if (lsys (NR_llseek, (unsigned) fd, 0, 0, (unsigned) &r, 2 /* SEEK_END */, 0) < 0)
    return 0;
  return r;
}

/* ask rol-usbd for device DEV: the open disc, or -1 */
static int
ask_rolusbd (const char *dev, int *ro)
{
  int s = lsys (NR_socket, 1 /* AF_UNIX */, 5 /* SOCK_SEQPACKET */ | O_CLOEXEC, 0, 0, 0, 0);
  if (s < 0)
    return -1;
  static const char addr[] = "\1\0\0rol-usbd-discs";	/* abstract name */
  int fd = -1;
  if (lsys (NR_connect, (unsigned) s, (unsigned) addr, 2 + 15, 0, 0, 0) == 0)
    {
      char req[48] = "OPEN ";
      size_t l = strlen (dev);
      if (l > 40)
	l = 40;
      memcpy (req + 5, dev, l);
      req[5 + l] = 0;
      if (lsys (NR_send, (unsigned) s, (unsigned) req, 6 + l, 0, 0, 0) > 0)
	{
	  char rep[64];
	  struct { unsigned len; int level, type; int fd; } cm = { 16, 0, 0, -1 };
	  struct { void *base; unsigned len; } iov = { rep, sizeof rep - 1 };
	  struct { void *name; unsigned namelen; void *iov; unsigned iovlen; void *control; unsigned controllen; int flags; }
	    mh = { 0, 0, &iov, 1, &cm, 16, 0 };
	  memset (rep, 0, sizeof rep);
	  int n = lsys (NR_recvmsg, (unsigned) s, (unsigned) &mh, MSG_CMSG_CLOEXEC, 0, 0, 0);
	  int got = mh.controllen >= 16 && cm.level == 1 && cm.type == 1 ? cm.fd : -1;
	  if (n > 3 && rep[0] == 'O' && rep[1] == 'K' && got >= 0)
	    {
	      /* "OK bytes ro" */
	      const char *p = rep + 3;
	      while (*p && *p != ' ')
		p++;
	      *ro = *p == ' ' && p[1] == '1';
	      fd = got;
	    }
	  else if (got >= 0)
	    lsys (NR_close, (unsigned) got, 0, 0, 0, 0, 0);
	}
    }
  lsys (NR_close, (unsigned) s, 0, 0, 0, 0, 0);
  return fd;
}

static void
dev_register (struct dev *d)
{
  if (d->registered || d->fd < 0)
    return;
  regs r = {{ 0 /* a device */, (unsigned) scsi_entry, 0, (unsigned) (d - devs) + 1 }};
  if (!swix_regs (SCSI_Register, &r))
    {
      d->registered = 1;
      d->id = r.r[0];
    }
}

static void
dev_deregister (struct dev *d)
{
  if (!d->registered)
    return;
  regs r = {{ d->id, (unsigned) scsi_entry, 0, (unsigned) (d - devs) + 1 }};
  swix_regs (SCSI_Deregister, &r);
  d->registered = 0;
}

static void
dev_remove (struct dev *d)
{
  dev_deregister (d);
  if (d->fd >= 0)
    lsys (NR_close, (unsigned) d->fd, 0, 0, 0, 0, 0);
  memset (d, 0, sizeof *d);
  d->fd = -1;
}

/* a disc listed (or the test file): add it if it's new */
static void
dev_seen (const char *src, const char *name, int test, int removable)
{
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used && streq (devs[i].src, src))
      {
	devs[i].seen = 1;
	dev_register (&devs[i]);	/* (SCSISwitch may have come since) */
	return;
      }
  struct dev *d = 0;
  for (int i = 0; i < MAXDEV && !d; i++)
    if (!devs[i].used)
      d = &devs[i];
  if (!d)
    return;
  int ro = 0, fd;
  if (test)
    {
      fd = lsys (NR_open, (unsigned) src, O_RDWR | O_DSYNC | O_CLOEXEC, 0, 0, 0, 0);
      if (fd < 0)
	{
	  fd = lsys (NR_open, (unsigned) src, O_CLOEXEC /* O_RDONLY */, 0, 0, 0, 0);
	  ro = 1;
	}
    }
  else
    fd = ask_rolusbd (src, &ro);
  if (fd < 0)
    return;
  memset (d, 0, sizeof *d);
  d->used = d->seen = 1;
  d->fd = fd;
  d->ro = ro;
  d->removable = removable;
  size_t l = strlen (src);
  if (l >= sizeof d->src)
    l = sizeof d->src - 1;
  memcpy (d->src, src, l);
  l = strlen (name);
  if (l >= sizeof d->name)
    l = sizeof d->name - 1;
  memcpy (d->name, name, l);
  int ss = 0;
  if (lsys (NR_ioctl, (unsigned) fd, BLKSSZGET, (unsigned) &ss, 0, 0, 0) < 0 || ss < 512)
    ss = 512;				/* (a file: 512) */
  d->log2ss = 9;
  while ((1 << d->log2ss) < ss && d->log2ss < 12)
    d->log2ss++;
  d->bytes = file_size (fd);
  d->sectors = d->bytes >> d->log2ss;
  dev_register (d);
}

/* every second (a callback): the discs rol-usbd lists, and the test file */
void
c_poll (void)
{
  if (!started)
    return;
  for (int i = 0; i < MAXDEV; i++)
    devs[i].seen = 0;
  static char buf[1024];
  int fd = lsys (NR_open, (unsigned) "/riscos/mediactl/filecore", O_CLOEXEC, 0, 0, 0, 0);
  if (fd >= 0)
    {
      int n = lsys (NR_read, (unsigned) fd, (unsigned) buf, sizeof buf - 1, 0, 0, 0);
      lsys (NR_close, (unsigned) fd, 0, 0, 0, 0, 0);
      buf[n > 0 ? n : 0] = 0;
      /* lines: device <tab> bytes <tab> name */
      char *p = buf;
      while (*p)
	{
	  char *e = p;
	  while (*e && *e != '\n')
	    e++;
	  char *t1 = p;
	  while (t1 < e && *t1 != '\t')
	    t1++;
	  char *t2 = t1 < e ? t1 + 1 : e;
	  while (t2 < e && *t2 != '\t')
	    t2++;
	  /* (a fourth field: removable, 1 or 0; a stick if it isn't there) */
	  char *t3 = t2 < e ? t2 + 1 : e;
	  while (t3 < e && *t3 != '\t')
	    t3++;
	  int removable = !(t3 < e && t3[1] == '0');
	  char dev[40], name[16];
	  size_t dl = (size_t) (t1 - p), nl = t2 < e ? (size_t) (t3 - t2 - 1) : 0;
	  if (dl > 0 && dl < sizeof dev)
	    {
	      memcpy (dev, p, dl);
	      dev[dl] = 0;
	      if (nl >= sizeof name)
		nl = sizeof name - 1;
	      memcpy (name, t2 + 1, nl);
	      name[nl] = 0;
	      dev_seen (dev, name, 0, removable);
	    }
	  p = *e ? e + 1 : e;
	}
    }
  /* the test file */
  char path[64];
  regs v = {{ (unsigned) "SCSILinux$Test", (unsigned) path, sizeof path - 1, 0, 0 }};
  if (!swix_regs (OS_ReadVarVal, &v) && v.r[2] > 0)
    {
      path[v.r[2]] = 0;
      dev_seen (path, "Test", 1, 1);
    }
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used && !devs[i].seen)
      dev_remove (&devs[i]);
}

/* --- SCSI commands -------------------------------------------------------- */

#define GOOD		0x00
#define CHECK		0x02

static unsigned
check (struct dev *d, unsigned key, unsigned asc)
{
  memset (d->sense, 0, sizeof d->sense);
  d->sense[0] = 0x70;
  d->sense[2] = (unsigned char) key;
  d->sense[7] = 10;
  d->sense[12] = (unsigned char) asc;
  return CHECK;
}

static unsigned
be (const unsigned char *p, int n)
{
  unsigned v = 0;
  while (n--)
    v = (v << 8) | *p++;
  return v;
}

static void
put_be (unsigned char *p, unsigned long long v, int n)
{
  while (n--)
    {
      p[n] = (unsigned char) v;
      v >>= 8;
    }
}

/* The transfer's memory: one block, or a scatter list of address, length
   pairs (FileCore's wrap: an address of &FFFF0000 up with length 0 moves
   back along the list by that much). Walk it: next piece of at most WANT
   bytes. */
struct xfer
{
  int scatter;
  unsigned *list;
  unsigned addr, left;		/* the current piece */
};

static void
xfer_start (struct xfer *x, unsigned flags, unsigned r3, unsigned len)
{
  x->scatter = (flags >> 26) & 1;
  if (x->scatter)
    {
      x->list = (unsigned *) r3;
      x->addr = x->left = 0;
    }
  else
    {
      x->addr = r3;
      x->left = len;
    }
}

static unsigned
xfer_next (struct xfer *x, unsigned want, unsigned *addr)
{
  if (x->scatter)
    for (int guard = 0; x->left == 0 && guard < 64; guard++)
      {
	unsigned a = x->list[0], l = x->list[1];
	if (a >= 0xFFFF0000u && l == 0)
	  {
	    x->list = (unsigned *) ((char *) x->list + (int) a);
	    continue;
	  }
	x->addr = a;
	x->left = l;
	x->list += 2;
      }
  unsigned n = x->left < want ? x->left : want;
  *addr = x->addr;
  x->addr += n;
  x->left -= n;
  return n;
}

/* copy N bytes of reply data into the transfer's memory: bytes copied */
static unsigned
reply (struct xfer *x, const unsigned char *src, unsigned n, unsigned len)
{
  unsigned done = 0;
  if (n > len)
    n = len;
  while (done < n)
    {
      unsigned a, k = xfer_next (x, n - done, &a);
      if (!k)
	break;
      memcpy ((void *) a, src + done, k);
      done += k;
    }
  return done;
}

/* read or write BYTES at OFF: bytes done, or -1 for an I/O error */
static long
disc_io (struct dev *d, struct xfer *x, int write, unsigned long long off, unsigned bytes)
{
  unsigned done = 0;
  while (done < bytes)
    {
      unsigned a, k = xfer_next (x, bytes - done, &a);
      if (!k)
	break;
      unsigned long long o = off + done;
      int r = lsys (write ? NR_pwrite64 : NR_pread64, (unsigned) d->fd, a, k, 0,
		    (unsigned) o, (unsigned) (o >> 32));
      if (r < 0)
	return -1;
      done += (unsigned) r;
      if ((unsigned) r < k)
	{
	  /* a short read at the end of a file (a test image): zeros */
	  if (!write)
	    memset ((void *) (a + (unsigned) r), 0, k - (unsigned) r);
	  else
	    return -1;
	  done += k - (unsigned) r;
	}
    }
  return (long) done;
}

/* Foreground operation: r[0] flags (bits 24-25 direction, 26 scatter),
   r[1] CDB length, r[2] CDB, r[3] memory or scatter list, r[4] length.
   Out: r[0] status byte, r[4] bytes not transferred. */
static void
scsi_op (struct dev *d, regs *r)
{
  const unsigned char *cdb = (const unsigned char *) r->r[2];
  unsigned len = r->r[4], flags = r->r[0], moved = 0, status = GOOD;
  struct xfer x;
  xfer_start (&x, flags, r->r[3], len);
  unsigned char buf[36];
  unsigned op = cdb[0];
  unsigned long long lba = 0;
  unsigned count = 0;
  int rw = -1;				/* 0 read, 1 write */

  /* one unit (LUN 0) per disc: the others aren't there (INQUIRY says so,
     as a real device does; anything else: "logical unit not supported") */
  if ((flags >> 5) & 7)
    {
      if (op == 0x12 && !(cdb[1] & 1))
	{
	  memset (buf, 0, 36);
	  buf[0] = 0x7F;			/* no device at this unit */
	  moved = reply (&x, buf, 36, len < cdb[4] ? len : cdb[4]);
	  r->r[0] = GOOD;
	}
      else
	r->r[0] = op == 0x03 ? GOOD : check (d, 0x05, 0x25);
      r->r[4] = len - moved;
      return;
    }
  if (d->fd < 0)
    {
      r->r[0] = op == 0x03 ? GOOD : check (d, 0x02, 0x3A);	/* not ready: no medium */
      r->r[4] = len;
      return;
    }
  switch (op)
    {
    case 0x00:				/* TEST UNIT READY */
    case 0x04:				/* FORMAT UNIT (HForm writes the structure itself) */
    case 0x15: case 0x55:		/* MODE SELECT */
    case 0x16: case 0x17:		/* RESERVE, RELEASE */
    case 0x1B:				/* START STOP UNIT (load, eject) */
    case 0x1E:				/* PREVENT ALLOW MEDIUM REMOVAL */
    case 0x2B:				/* SEEK */
    case 0x2F:				/* VERIFY */
      break;
    case 0x35:				/* SYNCHRONIZE CACHE */
      lsys (NR_fsync, (unsigned) d->fd, 0, 0, 0, 0, 0);
      break;
    case 0x03:				/* REQUEST SENSE */
      if (!d->sense[0])
	{
	  d->sense[0] = 0x70;
	  d->sense[7] = 10;
	}
      moved = reply (&x, d->sense, sizeof d->sense, len < cdb[4] ? len : cdb[4]);
      memset (d->sense, 0, sizeof d->sense);
      break;
    case 0x12:				/* INQUIRY */
      if (cdb[1] & 1)			/* (no vital product data pages) */
	{
	  status = check (d, 0x05, 0x24);
	  break;
	}
      memset (buf, ' ', sizeof buf);
      buf[0] = 0x00;			/* direct access */
      buf[1] = d->removable ? 0x80 : 0;	/* removable medium */
      buf[2] = 0x05;
      buf[3] = 0x02;
      buf[4] = 31;
      buf[5] = buf[6] = buf[7] = 0;
      memcpy (buf + 8, "Linux", 5);
      memcpy (buf + 16, d->name, strlen (d->name));
      memcpy (buf + 32, "0.01", 4);
      moved = reply (&x, buf, 36, len < cdb[4] ? len : cdb[4]);
      break;
    case 0x1A:				/* MODE SENSE (6): the header */
      memset (buf, 0, 4);
      buf[0] = 3;
      buf[2] = d->ro ? 0x80 : 0;	/* write-protected */
      moved = reply (&x, buf, 4, len < cdb[4] ? len : cdb[4]);
      break;
    case 0x5A:				/* MODE SENSE (10) */
      memset (buf, 0, 8);
      buf[1] = 6;
      buf[3] = d->ro ? 0x80 : 0;
      moved = reply (&x, buf, 8, len);
      break;
    case 0x25:				/* READ CAPACITY (10) */
      put_be (buf, d->sectors - 1 > 0xFFFFFFFFull ? 0xFFFFFFFFull : d->sectors - 1, 4);
      put_be (buf + 4, 1u << d->log2ss, 4);
      moved = reply (&x, buf, 8, len);
      break;
    case 0x9E:				/* SERVICE ACTION IN: READ CAPACITY (16) */
      if ((cdb[1] & 0x1F) != 0x10)
	{
	  status = check (d, 0x05, 0x20);
	  break;
	}
      memset (buf, 0, 32);
      put_be (buf, d->sectors - 1, 8);
      put_be (buf + 8, 1u << d->log2ss, 4);
      moved = reply (&x, buf, 32, len);
      break;
    case 0x08: case 0x0A:		/* READ, WRITE (6) */
      lba = ((cdb[1] & 0x1F) << 16) | (cdb[2] << 8) | cdb[3];
      count = cdb[4] ? cdb[4] : 256;
      rw = op == 0x0A;
      break;
    case 0x28: case 0x2A:		/* (10) */
      lba = be (cdb + 2, 4);
      count = be (cdb + 7, 2);
      rw = op == 0x2A;
      break;
    case 0xA8: case 0xAA:		/* (12) */
      lba = be (cdb + 2, 4);
      count = be (cdb + 6, 4);
      rw = op == 0xAA;
      break;
    case 0x88: case 0x8A:		/* (16) */
      lba = ((unsigned long long) be (cdb + 2, 4) << 32) | be (cdb + 6, 4);
      count = be (cdb + 10, 4);
      rw = op == 0x8A;
      break;
    default:
      status = check (d, 0x05, 0x20);	/* illegal request: invalid command */
      break;
    }
  if (rw >= 0)
    {
      if (lba + count > d->sectors)
	status = check (d, 0x05, 0x21);	/* LBA out of range */
      else if (rw && d->ro)
	status = check (d, 0x07, 0x27);	/* write protected */
      else
	{
	  unsigned long long want = (unsigned long long) count << d->log2ss;
	  unsigned bytes = want < len ? (unsigned) want : len;
	  long n = disc_io (d, &x, rw, lba << d->log2ss, bytes);
	  if (n < 0)
	    {
	      d->errors++;
	      status = check (d, 0x03, rw ? 0x0C : 0x11);	/* write / read error */
	    }
	  else
	    {
	      moved = (unsigned) n;
	      if (rw)
		d->writes++;
	      else
		d->reads++;
	    }
	}
    }
  r->r[0] = status;
  r->r[4] = len - moved;
}

/* SCSISwitch calls: handle = device slot + 1, reason as SoftDoc. 0, or 1
   with r->r[0] the error (a SCSI error number under 256, or a block). */
int
scsi_call (regs *r, unsigned handle, unsigned reason)
{
  if (handle < 1 || handle > MAXDEV || !devs[handle - 1].used)
    {
      r->r[0] = 0x0A;			/* "Device not present"-ish: a SCSI error */
      return 1;
    }
  struct dev *d = &devs[handle - 1];
  switch (reason)
    {
    case 0:				/* features: scatter lists, foreground */
      r->r[1] = 1u << 5;
      return 0;
    case 4:				/* foreground operation */
      scsi_op (d, r);
      return 0;
    default:
      r->r[0] = 1;			/* "Unknown SCSI SWI number" */
      return 1;
    }
}

/* --- the module ----------------------------------------------------------- */

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  memset (devs, 0, sizeof devs);
  for (int i = 0; i < MAXDEV; i++)
    devs[i].fd = -1;
  state[0] = 0;
  if (lsys (20 /* getpid */, 0, 0, 0, 0, 0, 0) == -38)
    {
      ERROR_BLOCK (e_nolinux, 0x819C0B, "SCSILinux (RISC OS for Linux) needs Linux");
      return ERR (e_nolinux);
    }
  regs t = {{ 99, (unsigned) ticker_entry, (unsigned) state }};	/* every second */
  oserror *e = swix_regs (OS_CallEvery, &t);
  if (e)
    return e;
  started = 1;
  c_poll ();
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  regs t = {{ (unsigned) ticker_entry, (unsigned) state }};
  swix_regs (OS_RemoveTickerEvent, &t);
  regs c = {{ (unsigned) callback_entry, (unsigned) state }};
  swix_regs (OS_RemoveCallBack, &c);
  started = 0;
  for (int i = 0; i < MAXDEV; i++)
    if (devs[i].used)
      dev_remove (&devs[i]);
  return 0;
}

/* Service_SCSIStarting: SCSISwitch has (re)started, so register again;
   Service_SCSIDying: our registrations go with it */
void
mod_service (regs *r)
{
  if (r->r[1] == 0x20100)
    {
      for (int i = 0; i < MAXDEV; i++)
	devs[i].registered = 0;
      /* its SWIs aren't there yet: the next poll registers */
    }
  else if (r->r[1] == 0x20101)
    for (int i = 0; i < MAXDEV; i++)
      devs[i].registered = 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  (void) number; (void) r;
  return 0;
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  int any = 0;
  for (int i = 0; i < MAXDEV; i++)
    {
      struct dev *d = &devs[i];
      if (!d->used)
	continue;
      any = 1;
      write0 (d->src);
      write0 (": \"");
      write0 (d->name);
      write0 ("\", ");
      write_dec ((unsigned) (d->bytes >> 20));
      write0 (" MB");
      write0 (d->removable ? ", removable" : ", fixed");
      if (d->ro)
	write0 (", write-protected");
      if (d->registered)
	{
	  write0 (", SCSI device ");
	  write_dec (d->id & 7);
	  write0 (" (bus ");
	  write_dec ((d->id >> 3) & 3);
	  write0 (")");
	}
      else
	write0 (", not registered (no SCSISwitch?)");
      write0 (", reads ");
      write_dec (d->reads);
      write0 (", writes ");
      write_dec (d->writes);
      if (d->errors)
	{
	  write0 (", errors ");
	  write_dec (d->errors);
	}
      newline ();
    }
  if (!any)
    {
      write0 ("No FileCore discs from Linux.");
      newline ();
    }
  return 0;
}
