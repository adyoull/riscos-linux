/* LinuxCodec: Linux's video decoders for RISC OS programs, on RISC OS for
   Linux running natively (Raspberry Pi 3/4: VideoCore's H.264 decoder,
   through Raspberry Pi's bcm2835-codec V4L2 driver).

   RISC OS runs as a Linux process here, so this module drives the decoder
   itself, with the port's IXSupport_LinuxSyscall SWI: it opens the V4L2
   memory-to-memory device (/dev/videoN), queues the program's compressed
   video and hands back decoded pictures in the buffers Linux maps into
   RISC OS's address space. No copy of the pictures is made.

   It's the V4L2 "stateful decoder" interface: the bitstream goes into
   OUTPUT buffers (Annex B H.264: start codes, SPS/PPS first); when the
   decoder has parsed the stream it signals a source change, and the
   CAPTURE side (YUV 4:2:0, planar I420) is set up at the size it reports.

   SWIs (chunk &5A440, not yet registered with RISC OS Open):
     LinuxCodec_Open      r0 = flags (0), r1 = codec (0 = H.264)
                          -> r0 = handle
     LinuxCodec_Close     r0 = handle
     LinuxCodec_Decode    r0 = handle, r1 -> data, r2 = length,
                          r3 = timestamp (copied to the picture),
                          r4 = flags: bit 0 = end of stream (drain)
                          -> r2 = bytes taken (0: no buffer free yet; get
                          pictures, then try again)
     LinuxCodec_GetFrame  r0 = handle -> r0 -> picture block, or 0 if none
                          is ready. The block (valid until it's released):
                            +0 index, +4 width, +8 height (the visible
                            picture), +12 Y line length, +16 -> Y,
                            +20 -> U (Cb), +24 -> V (Cr), +28 U/V line
                            length, +32 timestamp, +36 flags (bit 0: the
                            last picture after a drain), +40 coded width,
                            +44 coded height
     LinuxCodec_ReleaseFrame r0 = handle, r1 = index: the decoder may reuse
                          the picture's buffer
     LinuxCodec_Info      r0 = handle -> r1 = width, r2 = height,
                          r3 = state (0 waiting for the stream's size,
                          1 decoding, 2 drained), r4 = pictures decoded,
                          r5 = bytes queued

   Not under QEMU (V4L2's ioctls aren't translated there): it says so.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "cmodule.h"

#define IXSupport_LinuxSyscall	0x59EC0
#define OS_ReadMonotonicTime	0x42

/* Linux (32-bit ARM EABI) */
#define NR_close	6
#define NR_open		5
#define NR_ioctl	54
#define NR_munmap	91
#define NR_mmap2	192
#define NR_socket	281
#define NR_connect	283
#define NR_recvfrom	292
#define NR_sendmsg	296
#define O_RDWR		2
#define O_NONBLOCK	04000
#define O_CLOEXEC	02000000
#define PROT_RW		3
#define MAP_SHARED	1
#define EAGAIN		11
#define EINVAL		22
#define ENOSYS		38

/* V4L2, 32-bit layouts (the _TIME32 forms of the buffer and event ioctls,
   which arm64 kernels with COMPAT_32BIT_TIME accept from 32-bit programs) */
#define IOC(dir, nr, size)	(((dir) << 30) | ((size) << 16) | ('V' << 8) | (nr))
#define IOR(nr, size)		IOC (2, nr, size)
#define IOW(nr, size)		IOC (1, nr, size)
#define IOWR(nr, size)		IOC (3, nr, size)

struct capability { unsigned char driver[16], card[32], bus[32]; unsigned version, caps, device_caps, reserved[3]; };
struct fmtdesc { unsigned index, type, flags; unsigned char description[32]; unsigned pixelformat, mbus_code, reserved[3]; };
struct pix_format { unsigned width, height, pixelformat, field, bytesperline, sizeimage, colorspace, priv, flags, ycbcr_enc, quantization, xfer_func; };
struct format { unsigned type; union { struct pix_format pix; unsigned char raw[200]; } fmt; };
struct requestbuffers { unsigned count, type, memory, capabilities; unsigned char flags, reserved[3]; };
struct buffer
{
  unsigned index, type, bytesused, flags, field;
  int ts_sec, ts_usec;
  unsigned timecode[4];
  unsigned sequence, memory, offset, length, reserved2, request_fd;
};
struct event_subscription { unsigned type, id, flags, reserved[5]; };
struct event { unsigned type, pad; unsigned char u[64]; unsigned pending, sequence; int ts[2]; unsigned id, reserved[8], pad2; };
struct control { unsigned id; int value; };
struct selection { unsigned type, target, flags; int left, top; unsigned width, height, reserved[9]; };
struct decoder_cmd { unsigned cmd, flags; unsigned raw[16]; };
struct exportbuffer { unsigned type, index, plane, flags; int fd; unsigned reserved[11]; };

#define VIDIOC_QUERYCAP		IOR (0, sizeof (struct capability))
#define VIDIOC_ENUM_FMT		IOWR (2, sizeof (struct fmtdesc))
#define VIDIOC_G_FMT		IOWR (4, sizeof (struct format))
#define VIDIOC_S_FMT		IOWR (5, sizeof (struct format))
#define VIDIOC_REQBUFS		IOWR (8, sizeof (struct requestbuffers))
#define VIDIOC_QUERYBUF		IOWR (9, sizeof (struct buffer))
#define VIDIOC_QBUF		IOWR (15, sizeof (struct buffer))
#define VIDIOC_EXPBUF		IOWR (16, sizeof (struct exportbuffer))
#define VIDIOC_DQBUF		IOWR (17, sizeof (struct buffer))
#define VIDIOC_STREAMON		IOW (18, 4)
#define VIDIOC_STREAMOFF	IOW (19, 4)
#define VIDIOC_G_CTRL		IOWR (27, sizeof (struct control))
#define VIDIOC_DQEVENT		IOR (89, sizeof (struct event))
#define VIDIOC_SUBSCRIBE_EVENT	IOW (90, sizeof (struct event_subscription))
#define VIDIOC_G_SELECTION	IOWR (94, sizeof (struct selection))
#define VIDIOC_DECODER_CMD	IOWR (96, sizeof (struct decoder_cmd))

#define BUF_CAPTURE		1
#define BUF_OUTPUT		2
#define MEMORY_MMAP		1
#define CAP_M2M			0x00008000u
#define CAP_DEVICE_CAPS		0x80000000u
#define BUF_FLAG_LAST		0x00100000u
#define EVENT_EOS		2
#define EVENT_SOURCE_CHANGE	5
#define CID_MIN_BUFFERS_FOR_CAPTURE 0x00980927u
#define SEL_TGT_COMPOSE		0x0100
#define DEC_CMD_STOP		1
#define FOURCC(a, b, c, d)	((unsigned) (a) | ((unsigned) (b) << 8) | ((unsigned) (c) << 16) | ((unsigned) (d) << 24))
#define FMT_H264		FOURCC ('H', '2', '6', '4')
#define FMT_I420		FOURCC ('Y', 'U', '1', '2')

/* sizeof checks: the ioctl numbers depend on them */
typedef char check_buffer[sizeof (struct buffer) == 68 ? 1 : -1];
typedef char check_format[sizeof (struct format) == 204 ? 1 : -1];
typedef char check_event[sizeof (struct event) == 128 ? 1 : -1];
typedef char check_capability[sizeof (struct capability) == 104 ? 1 : -1];

#define OUT_BUFS	4
#define OUT_SIZE	(512 * 1024)
#define CAP_MAX		16

struct mapping { void *addr; unsigned length; };

typedef struct decoder
{
  struct decoder *next;
  unsigned magic;
  int fd, dev;
  int state;			/* 0 waiting for the size, 1 decoding, 2 drained */
  struct mapping out[OUT_BUFS];
  unsigned char out_busy[OUT_BUFS];
  struct mapping cap[CAP_MAX];
  unsigned ncap;
  unsigned coded_w, coded_h, bpl, sizeimage, vis_w, vis_h;
  unsigned decoded, queued_bytes;
  unsigned block[12];		/* the picture block GetFrame returns */
  unsigned cap_gen;		/* counts capture set-ups */
  int ovfd;			/* the connection to rol-fb's overlay, or -1 */
  unsigned ov_gen;		/* the capture set-up rol-fb has the buffers of */
  int ov_shown;			/* the picture on the overlay, or -1 */
  unsigned ov_key;		/* key colour, 0x00RRGGBB */
} decoder;

#define MAGIC	0x43584C4Cu	/* "LLXC" */

static decoder *decoders;
static int native = -1;

ERROR_BLOCK (no_ixsupport, 0x81A30, "LinuxCodec needs RISC OS for Linux");
ERROR_BLOCK (not_native, 0x81A31, "LinuxCodec needs RISC OS running natively (not under QEMU)");
ERROR_BLOCK (no_device, 0x81A32, "Linux has no video decoder for this codec (no V4L2 device takes it)");
ERROR_BLOCK (bad_handle, 0x81A33, "Bad LinuxCodec handle");
ERROR_BLOCK (no_memory, 0x81A34, "LinuxCodec: not enough memory");
ERROR_BLOCK (bad_codec, 0x81A35, "LinuxCodec: unknown codec");
ERROR_BLOCK (linux_failed, 0x81A36, "LinuxCodec: the Linux decoder failed");
ERROR_BLOCK (bad_swi, 0x1E6, "Unknown LinuxCodec SWI");

static int
lsys (int nr, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
  regs r = {{ a, b, c, d, e, f, 0, (unsigned) nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -ENOSYS;
  return (int) r.r[0];
}

static int
xioctl (int fd, unsigned req, void *arg)
{
  return lsys (NR_ioctl, (unsigned) fd, req, (unsigned) arg, 0, 0, 0);
}

/* Under QEMU, V4L2's ioctls aren't translated: only natively, which is
   when NativeSupport is loaded (it refuses to start under QEMU). */
static int
is_native (void)
{
  if (native < 0)
    {
      regs r = {{ 18, (unsigned) "NativeSupport" }};
      native = swix_regs (OS_Module, &r) == 0;
    }
  return native;
}

static void
dev_name (char *p, int n)
{
  const char *s = "/dev/video";
  while (*s)
    *p++ = *s++;
  if (n >= 10)
    *p++ = (char) ('0' + n / 10);
  *p++ = (char) ('0' + n % 10);
  *p = 0;
}

static int
dev_open (int n)
{
  char name[16];
  dev_name (name, n);
  return lsys (NR_open, (unsigned) name, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0, 0, 0, 0);
}

/* Does the device take fmt on its OUTPUT (compressed) side, and give I420
   on its CAPTURE side? */
static int
has_fmt (int fd, unsigned type, unsigned fourcc)
{
  for (unsigned i = 0; i < 64; i++)
    {
      struct fmtdesc d;
      memset (&d, 0, sizeof d);
      d.index = i;
      d.type = type;
      if (xioctl (fd, VIDIOC_ENUM_FMT, &d) < 0)
	return 0;
      if (d.pixelformat == fourcc)
	return 1;
    }
  return 0;
}

static int
is_decoder (int fd, unsigned fourcc)
{
  struct capability c;
  memset (&c, 0, sizeof c);
  if (xioctl (fd, VIDIOC_QUERYCAP, &c) < 0)
    return 0;
  unsigned caps = (c.caps & CAP_DEVICE_CAPS) ? c.device_caps : c.caps;
  return (caps & CAP_M2M) && has_fmt (fd, BUF_OUTPUT, fourcc)
	 && has_fmt (fd, BUF_CAPTURE, FMT_I420);
}

static void
unmap_all (struct mapping *m, unsigned n)
{
  for (unsigned i = 0; i < n; i++)
    if (m[i].addr)
      {
	lsys (NR_munmap, (unsigned) m[i].addr, m[i].length, 0, 0, 0, 0);
	m[i].addr = 0;
      }
}

/* REQBUFS and map them (the mapping offsets V4L2 gives are page aligned:
   mmap2 takes pages) */
static int
map_buffers (decoder *d, unsigned type, struct mapping *m, unsigned want, unsigned *got)
{
  struct requestbuffers rb;
  memset (&rb, 0, sizeof rb);
  rb.count = want;
  rb.type = type;
  rb.memory = MEMORY_MMAP;
  if (xioctl (d->fd, VIDIOC_REQBUFS, &rb) < 0 || rb.count == 0)
    return -1;
  if (rb.count > want)
    rb.count = want;
  for (unsigned i = 0; i < rb.count; i++)
    {
      struct buffer b;
      memset (&b, 0, sizeof b);
      b.index = i;
      b.type = type;
      b.memory = MEMORY_MMAP;
      if (xioctl (d->fd, VIDIOC_QUERYBUF, &b) < 0)
	return -1;
      int a = lsys (NR_mmap2, 0, b.length, PROT_RW, MAP_SHARED, (unsigned) d->fd, b.offset >> 12);
      if (a < 0 && a > -4096)
	return -1;
      m[i].addr = (void *) a;
      m[i].length = b.length;
    }
  *got = rb.count;
  return 0;
}

static void
release_decoder (decoder *d)
{
  if (d->ovfd >= 0)
    lsys (NR_close, (unsigned) d->ovfd, 0, 0, 0, 0, 0);	/* rol-fb hides it */
  d->ovfd = -1;
  int t = BUF_CAPTURE;
  xioctl (d->fd, VIDIOC_STREAMOFF, &t);
  t = BUF_OUTPUT;
  xioctl (d->fd, VIDIOC_STREAMOFF, &t);
  unmap_all (d->cap, d->ncap);
  unmap_all (d->out, OUT_BUFS);
  lsys (NR_close, (unsigned) d->fd, 0, 0, 0, 0, 0);
  d->magic = 0;
}

static decoder *
get (unsigned h)
{
  for (decoder *d = decoders; d; d = d->next)
    if ((unsigned) d == h && d->magic == MAGIC)
      return d;
  return 0;
}

static oserror *
codec_open (regs *r)
{
  if (!is_native ())
    return ERR (not_native);
  if (r->r[1] != 0)
    return ERR (bad_codec);
  unsigned fourcc = FMT_H264;
  int fd = -1, dev = -1;
  for (int n = 0; n < 32 && fd < 0; n++)
    {
      int f = dev_open (n);
      if (f < 0)
	{
	  if (f == -ENOSYS)
	    return ERR (no_ixsupport);
	  continue;
	}
      if (is_decoder (f, fourcc))
	fd = f, dev = n;
      else
	lsys (NR_close, (unsigned) f, 0, 0, 0, 0, 0);
    }
  if (fd < 0)
    return ERR (no_device);
  decoder *d = rma_claim (sizeof *d);
  if (!d)
    {
      lsys (NR_close, (unsigned) fd, 0, 0, 0, 0, 0);
      return ERR (no_memory);
    }
  memset (d, 0, sizeof *d);
  d->fd = fd;
  d->dev = dev;
  d->magic = MAGIC;
  d->ovfd = -1;
  d->ov_shown = -1;
  /* the compressed side */
  struct format f;
  memset (&f, 0, sizeof f);
  f.type = BUF_OUTPUT;
  f.fmt.pix.pixelformat = fourcc;
  f.fmt.pix.width = 1920;
  f.fmt.pix.height = 1080;
  f.fmt.pix.sizeimage = OUT_SIZE;
  unsigned got = 0;
  struct event_subscription s;
  memset (&s, 0, sizeof s);
  s.type = EVENT_SOURCE_CHANGE;
  if (xioctl (fd, VIDIOC_S_FMT, &f) < 0
      || map_buffers (d, BUF_OUTPUT, d->out, OUT_BUFS, &got) < 0
      || xioctl (fd, VIDIOC_SUBSCRIBE_EVENT, &s) < 0)
    {
      release_decoder (d);
      rma_free (d);
      return ERR (linux_failed);
    }
  for (unsigned i = got; i < OUT_BUFS; i++)
    d->out_busy[i] = 1;		/* fewer than asked for: never used */
  s.type = EVENT_EOS;
  xioctl (fd, VIDIOC_SUBSCRIBE_EVENT, &s);
  int t = BUF_OUTPUT;
  if (xioctl (fd, VIDIOC_STREAMON, &t) < 0)
    {
      release_decoder (d);
      rma_free (d);
      return ERR (linux_failed);
    }
  d->next = decoders;
  decoders = d;
  r->r[0] = (unsigned) d;
  return 0;
}

static oserror *
codec_close (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  for (decoder **p = &decoders; *p; p = &(*p)->next)
    if (*p == d)
      {
	*p = d->next;
	break;
      }
  release_decoder (d);
  rma_free (d);
  return 0;
}

/* The decoder knows the stream's size: set up the pictures */
static int
setup_capture (decoder *d)
{
  struct format f;
  memset (&f, 0, sizeof f);
  f.type = BUF_CAPTURE;
  if (xioctl (d->fd, VIDIOC_G_FMT, &f) < 0)
    return -1;
  if (f.fmt.pix.pixelformat != FMT_I420)
    {
      f.fmt.pix.pixelformat = FMT_I420;
      if (xioctl (d->fd, VIDIOC_S_FMT, &f) < 0)
	return -1;
    }
  d->coded_w = f.fmt.pix.width;
  d->coded_h = f.fmt.pix.height;
  d->bpl = f.fmt.pix.bytesperline ? f.fmt.pix.bytesperline : f.fmt.pix.width;
  d->sizeimage = f.fmt.pix.sizeimage;
  d->vis_w = d->coded_w;
  d->vis_h = d->coded_h;
  struct selection sel;
  memset (&sel, 0, sizeof sel);
  sel.type = BUF_CAPTURE;
  sel.target = SEL_TGT_COMPOSE;
  if (xioctl (d->fd, VIDIOC_G_SELECTION, &sel) == 0 && sel.width && sel.height)
    {
      d->vis_w = sel.width;
      d->vis_h = sel.height;
    }
  struct control c = { CID_MIN_BUFFERS_FOR_CAPTURE, 0 };
  unsigned want = 4;
  if (xioctl (d->fd, VIDIOC_G_CTRL, &c) == 0 && c.value > 0)
    want = (unsigned) c.value;
  want += 2;			/* the program may hold on to two */
  if (want > CAP_MAX)
    want = CAP_MAX;
  unmap_all (d->cap, d->ncap);
  d->ncap = 0;
  if (map_buffers (d, BUF_CAPTURE, d->cap, want, &d->ncap) < 0)
    return -1;
  for (unsigned i = 0; i < d->ncap; i++)
    {
      struct buffer b;
      memset (&b, 0, sizeof b);
      b.index = i;
      b.type = BUF_CAPTURE;
      b.memory = MEMORY_MMAP;
      if (xioctl (d->fd, VIDIOC_QBUF, &b) < 0)
	return -1;
    }
  int t = BUF_CAPTURE;
  if (xioctl (d->fd, VIDIOC_STREAMON, &t) < 0)
    return -1;
  d->state = 1;
  d->cap_gen++;
  return 0;
}

/* Events: a source change sets up (or redoes) the pictures */
static int
poll_events (decoder *d)
{
  struct event e;
  for (int i = 0; i < 8; i++)
    {
      memset (&e, 0, sizeof e);
      if (xioctl (d->fd, VIDIOC_DQEVENT, &e) < 0)
	break;
      if (e.type == EVENT_SOURCE_CHANGE)
	{
	  if (d->state == 1)
	    {
	      int t = BUF_CAPTURE;
	      xioctl (d->fd, VIDIOC_STREAMOFF, &t);
	    }
	  if (setup_capture (d) < 0)
	    return -1;
	}
    }
  return 0;
}

static void
reap_output (decoder *d)
{
  for (;;)
    {
      struct buffer b;
      memset (&b, 0, sizeof b);
      b.type = BUF_OUTPUT;
      b.memory = MEMORY_MMAP;
      if (xioctl (d->fd, VIDIOC_DQBUF, &b) < 0)
	return;
      if (b.index < OUT_BUFS)
	d->out_busy[b.index] = 0;
    }
}

static oserror *
codec_decode (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  reap_output (d);
  if (poll_events (d) < 0)
    return ERR (linux_failed);
  if (r->r[4] & 1)
    {
      struct decoder_cmd c;
      memset (&c, 0, sizeof c);
      c.cmd = DEC_CMD_STOP;
      xioctl (d->fd, VIDIOC_DECODER_CMD, &c);
      r->r[2] = 0;
      return 0;
    }
  unsigned taken = 0;
  for (unsigned i = 0; i < OUT_BUFS; i++)
    if (!d->out_busy[i] && d->out[i].addr)
      {
	unsigned n = r->r[2];
	if (n > d->out[i].length)
	  n = d->out[i].length;
	memcpy (d->out[i].addr, (const void *) r->r[1], n);
	struct buffer b;
	memset (&b, 0, sizeof b);
	b.index = i;
	b.type = BUF_OUTPUT;
	b.memory = MEMORY_MMAP;
	b.bytesused = n;
	b.ts_sec = (int) r->r[3];
	if (xioctl (d->fd, VIDIOC_QBUF, &b) < 0)
	  return ERR (linux_failed);
	d->out_busy[i] = 1;
	d->queued_bytes += n;
	taken = n;
	break;
      }
  r->r[2] = taken;
  return 0;
}

static oserror *
codec_getframe (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  reap_output (d);
  if (poll_events (d) < 0)
    return ERR (linux_failed);
  r->r[0] = 0;
  if (d->state != 1)
    return 0;
  struct buffer b;
  memset (&b, 0, sizeof b);
  b.type = BUF_CAPTURE;
  b.memory = MEMORY_MMAP;
  if (xioctl (d->fd, VIDIOC_DQBUF, &b) < 0)
    return 0;
  if (b.index >= d->ncap)
    return 0;
  if (b.flags & BUF_FLAG_LAST)
    d->state = 2;
  if (b.bytesused == 0)
    {
      /* an empty LAST buffer: nothing to show, give it straight back */
      xioctl (d->fd, VIDIOC_QBUF, &b);
      return 0;
    }
  unsigned char *y = d->cap[b.index].addr;
  unsigned ys = d->bpl, cs = d->bpl / 2;
  unsigned char *u = y + ys * d->coded_h;
  unsigned char *v = u + cs * (d->coded_h / 2);
  unsigned *k = d->block;
  k[0] = b.index;
  k[1] = d->vis_w;
  k[2] = d->vis_h;
  k[3] = ys;
  k[4] = (unsigned) y;
  k[5] = (unsigned) u;
  k[6] = (unsigned) v;
  k[7] = cs;
  k[8] = (unsigned) b.ts_sec;
  k[9] = (b.flags & BUF_FLAG_LAST) ? 1 : 0;
  k[10] = d->coded_w;
  k[11] = d->coded_h;
  d->decoded++;
  r->r[0] = (unsigned) k;
  return 0;
}

static oserror *
codec_release (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  if (r->r[1] >= d->ncap)
    return ERR (bad_handle);
  struct buffer b;
  memset (&b, 0, sizeof b);
  b.index = r->r[1];
  b.type = BUF_CAPTURE;
  b.memory = MEMORY_MMAP;
  if (xioctl (d->fd, VIDIOC_QBUF, &b) < 0)
    return ERR (linux_failed);
  return 0;
}

static oserror *
codec_info (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  poll_events (d);
  r->r[1] = d->vis_w;
  r->r[2] = d->vis_h;
  r->r[3] = (unsigned) d->state;
  r->r[4] = d->decoded;
  r->r[5] = d->queued_bytes;
  return 0;
}


/* --- the video overlay (rol-fb): pictures shown by the display hardware ---
   The decoder's picture buffers are given to rol-fb (the port's screen
   front end) as dmabufs; it shows them on a YUV overlay plane under the
   desktop, scaled by the display hardware. See rol-fb.c. */
ERROR_BLOCK (no_overlay, 0x81A37, "LinuxCodec: no video overlay (rol-fb hasn't one on this display)");
ERROR_BLOCK (not_decoding, 0x81A38, "LinuxCodec: no pictures yet (the decoder hasn't found the stream's size)");

static int
ov_call (decoder *d, int *msg, int fd, int *reply)
{
  struct { unsigned len; int level, type; int fd; } cm = { 16, 1, 1, fd };	/* SOL_SOCKET, SCM_RIGHTS */
  struct { void *base; unsigned len; } iov = { msg, 64 };
  struct { void *name; unsigned namelen; void *iov; unsigned iovlen; void *control; unsigned controllen; int flags; }
    mh = { 0, 0, &iov, 1, fd >= 0 ? &cm : 0, fd >= 0 ? 16 : 0, 0 };
  if (lsys (NR_sendmsg, (unsigned) d->ovfd, (unsigned) &mh, 0x4000 /* MSG_NOSIGNAL */, 0, 0, 0) != 64)
    return -1;
  memset (reply, 0, 64);
  if (lsys (NR_recvfrom, (unsigned) d->ovfd, (unsigned) reply, 64, 0, 0, 0) < 4)
    return -1;
  return reply[0];
}

/* Give rol-fb this capture set-up's buffers */
static int
ov_register (decoder *d)
{
  int m[16], r[16];
  memset (m, 0, sizeof m);
  m[0] = 5;					/* FORGET */
  if (ov_call (d, m, -1, r) < 0)
    return -1;
  d->ov_shown = -1;
  unsigned cw = d->bpl, ch = d->coded_h, cs = d->bpl / 2;
  for (unsigned i = 0; i < d->ncap; i++)
    {
      struct exportbuffer e;
      memset (&e, 0, sizeof e);
      e.type = BUF_CAPTURE;
      e.index = i;
      e.flags = O_CLOEXEC;
      if (xioctl (d->fd, VIDIOC_EXPBUF, &e) < 0)
	return -1;
      memset (m, 0, sizeof m);
      m[0] = 2;					/* ADDBUF */
      m[1] = (int) i;
      m[2] = (int) d->coded_w;
      m[3] = (int) ch;
      m[4] = (int) FMT_I420;			/* DRM's YUV420 is the same fourcc */
      m[5] = (int) cw; m[6] = 0;
      m[7] = (int) cs; m[8] = (int) (cw * ch);
      m[9] = (int) cs; m[10] = (int) (cw * ch + cs * (ch / 2));
      int rc = ov_call (d, m, e.fd, r);
      lsys (NR_close, (unsigned) e.fd, 0, 0, 0, 0, 0);
      if (rc < 0)
	return -1;
    }
  d->ov_gen = d->cap_gen;
  return 0;
}

static oserror *
codec_overlay_open (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  if (d->ovfd < 0)
    {
      int s = lsys (NR_socket, 1 /* AF_UNIX */, 5 /* SOCK_SEQPACKET */ | O_CLOEXEC, 0, 0, 0, 0);
      if (s < 0)
	return ERR (no_overlay);
      static const char addr[] = "\1\0\0rol-fb-overlay";	/* sun_family AF_UNIX, abstract name */
      if (lsys (NR_connect, (unsigned) s, (unsigned) addr, 2 + 15, 0, 0, 0) < 0)
	{
	  lsys (NR_close, (unsigned) s, 0, 0, 0, 0, 0);
	  return ERR (no_overlay);
	}
      d->ovfd = s;
      d->ov_gen = 0;
      d->ov_shown = -1;
    }
  int m[16], rep[16];
  memset (m, 0, sizeof m);
  m[0] = 1;					/* OPEN */
  if (ov_call (d, m, -1, rep) < 0)
    {
      lsys (NR_close, (unsigned) d->ovfd, 0, 0, 0, 0, 0);
      d->ovfd = -1;
      return ERR (no_overlay);
    }
  d->ov_key = (unsigned) rep[1];
  /* as a RISC OS colour, &BBGGRR00, for ColourTrans_SetGCOL */
  unsigned k = d->ov_key;
  r->r[0] = ((k & 0xFF) << 24) | (((k >> 8) & 0xFF) << 16) | (((k >> 16) & 0xFF) << 8);
  r->r[1] = rep[2] ? 1 : 0;
  return 0;
}

static unsigned
mode_var (int v)
{
  regs r = {{ (unsigned) -1, (unsigned) v }};
  swix_regs (0x35 /* OS_ReadModeVariable */, &r);
  return r.r[2];
}

static oserror *
codec_overlay_show (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  if (d->ovfd < 0)
    return ERR (no_overlay);
  if (d->state < 1 || d->ncap == 0)
    return ERR (not_decoding);
  if (r->r[1] >= d->ncap)
    return ERR (bad_handle);
  if (d->ov_gen != d->cap_gen && ov_register (d) < 0)
    return ERR (linux_failed);
  /* OS units from the bottom left to pixels from the top left */
  unsigned xe = mode_var (4), ye = mode_var (5), yl = mode_var (12) + 1;
  int x0 = (int) r->r[2] >> xe, x1 = (int) r->r[4] >> xe;
  int top = (int) yl - ((int) r->r[5] >> ye), bot = (int) yl - ((int) r->r[3] >> ye);
  int m[16], rep[16];
  memset (m, 0, sizeof m);
  m[0] = 3;					/* SHOW */
  m[1] = (int) r->r[1];
  m[2] = x0; m[3] = top; m[4] = x1 - x0; m[5] = bot - top;
  m[6] = 0; m[7] = 0; m[8] = (int) d->vis_w; m[9] = (int) d->vis_h;
  if (ov_call (d, m, -1, rep) < 0)
    return ERR (linux_failed);
  /* the picture shown before is the decoder's again */
  if (rep[1] >= 0 && (unsigned) rep[1] < d->ncap && rep[1] != (int) r->r[1])
    {
      struct buffer b;
      memset (&b, 0, sizeof b);
      b.index = (unsigned) rep[1];
      b.type = BUF_CAPTURE;
      b.memory = MEMORY_MMAP;
      xioctl (d->fd, VIDIOC_QBUF, &b);
    }
  d->ov_shown = (int) r->r[1];
  return 0;
}

static oserror *
codec_overlay_hide (regs *r)
{
  decoder *d = get (r->r[0]);
  if (!d)
    return ERR (bad_handle);
  if (d->ovfd < 0)
    return 0;
  int m[16], rep[16];
  memset (m, 0, sizeof m);
  m[0] = 4;					/* HIDE */
  if (ov_call (d, m, -1, rep) >= 0 && rep[1] >= 0 && (unsigned) rep[1] < d->ncap)
    {
      struct buffer b;
      memset (&b, 0, sizeof b);
      b.index = (unsigned) rep[1];
      b.type = BUF_CAPTURE;
      b.memory = MEMORY_MMAP;
      xioctl (d->fd, VIDIOC_QBUF, &b);
    }
  d->ov_shown = -1;
  return 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  switch (number)
    {
    case 0: return codec_open (r);
    case 1: return codec_close (r);
    case 2: return codec_decode (r);
    case 3: return codec_getframe (r);
    case 4: return codec_release (r);
    case 5: return codec_info (r);
    case 6: return codec_overlay_open (r);
    case 7: return codec_overlay_show (r);
    case 8: return codec_overlay_hide (r);
    }
  return ERR (bad_swi);
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  decoders = 0;
  native = -1;
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  while (decoders)
    {
      decoder *d = decoders;
      decoders = d->next;
      release_decoder (d);
      rma_free (d);
    }
  return 0;
}

static void
write_n (const unsigned char *s, int n)
{
  char buf[40];
  int i = 0;
  for (; i < n && i < 39 && s[i]; i++)
    buf[i] = (char) s[i];
  buf[i] = 0;
  write0 (buf);
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  if (!is_native ())
    {
      write0 ("RISC OS is running under QEMU: LinuxCodec only works natively.");
      newline ();
      return 0;
    }
  int any = 0;
  for (int n = 0; n < 32; n++)
    {
      int fd = dev_open (n);
      if (fd < 0)
	continue;
      struct capability c;
      memset (&c, 0, sizeof c);
      if (xioctl (fd, VIDIOC_QUERYCAP, &c) == 0)
	{
	  unsigned caps = (c.caps & CAP_DEVICE_CAPS) ? c.device_caps : c.caps;
	  char name[16];
	  dev_name (name, n);
	  write0 (name);
	  write0 (": ");
	  write_n (c.driver, 16);
	  write0 (", ");
	  write_n (c.card, 32);
	  if (caps & CAP_M2M)
	    write0 (is_decoder (fd, FMT_H264) ? " - H.264 decoder" : " - memory to memory");
	  newline ();
	  any = 1;
	}
      lsys (NR_close, (unsigned) fd, 0, 0, 0, 0, 0);
    }
  if (!any)
    {
      write0 ("No V4L2 devices (/dev/video*): Linux has no video codecs here.");
      newline ();
    }
  for (decoder *d = decoders; d; d = d->next)
    {
      write0 ("Decoder ");
      write_hex8 ((unsigned) d);
      write0 (": ");
      write_dec (d->vis_w);
      write0 ("x");
      write_dec (d->vis_h);
      write0 (d->state == 0 ? ", waiting for the stream's size" : d->state == 1 ? ", decoding" : ", drained");
      write0 (", pictures ");
      write_dec (d->decoded);
      write0 (", bytes ");
      write_dec (d->queued_bytes);
      newline ();
    }
  return 0;
}

void
mod_service (regs *r)
{
  (void) r;
}
