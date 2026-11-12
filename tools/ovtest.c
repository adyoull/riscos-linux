/* ovtest: a Linux client for rol-fb's overlay socket ("rol-fb-overlay"),
 * to test it and to copy from (RISC3D's rol-3d shows a GL window's buffers
 * this way). The protocol is in images/fe/rol-fb.c.
 *
 *   ovtest            protocol test: three overlays at once (two RGB, one
 *                     video), a second video taking over, an old client
 *                     (version 0). With /riscos/overlay-test present and no
 *                     planes, rol-fb only logs. Prints "ovtest ok".
 *   ovtest show [N] [key]
 *                     on a machine with planes: N (1..3, default 2) RGB
 *                     overlays of moving colour bars for 10 seconds, from
 *                     dumb buffers of /dev/dri/card*, and the frame rate.
 *                     They show through the desktop whatever it has there
 *                     (SHOW's test bit); with "key", only where the
 *                     desktop has the key colour 0x080008, as a program's
 *                     overlay does.
 *
 * Build: aarch64-linux-gnu-gcc -O2 -static -Wall -Wextra -o ovtest ovtest.c
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

static int
ov_connect (void)
{
  int s = socket (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
  struct sockaddr_un a = { .sun_family = AF_UNIX };
  memcpy (a.sun_path, "\0rol-fb-overlay", 15);
  if (s < 0 || connect (s, (struct sockaddr *) &a, offsetof (struct sockaddr_un, sun_path) + 15))
    {
      perror ("ovtest: rol-fb-overlay");
      exit (1);
    }
  return s;
}

/* one message (with fd if >= 0) and its reply; the reply's [0] */
static int
ov_call (int s, const int32_t *m, int fd, int32_t *r)
{
  union { char buf[CMSG_SPACE (sizeof (int))]; struct cmsghdr align; } ctl;
  struct iovec iov = { (void *) m, 16 * sizeof (int32_t) };
  struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
  if (fd >= 0)
    {
      msg.msg_control = ctl.buf;
      msg.msg_controllen = sizeof ctl.buf;
      struct cmsghdr *c = CMSG_FIRSTHDR (&msg);
      c->cmsg_level = SOL_SOCKET;
      c->cmsg_type = SCM_RIGHTS;
      c->cmsg_len = CMSG_LEN (sizeof (int));
      memcpy (CMSG_DATA (c), &fd, sizeof fd);
    }
  int32_t rr[16];
  if (!r)
    r = rr;
  memset (r, 0, sizeof rr);
  if (sendmsg (s, &msg, MSG_NOSIGNAL) < 0)
    return -errno;
  ssize_t n = recv (s, r, sizeof rr, 0);
  if (n <= 0)
    return -EPIPE;
  return r[0];
}

static int fails;
static void
want (const char *what, int got, int wanted)
{
  if (got != wanted)
    {
      printf ("FAIL %s: %d (wanted %d)\n", what, got, wanted);
      fails++;
    }
}

static int
proto (void)
{
  int a = ov_connect (), b = ov_connect (), v = ov_connect ();
  int32_t r[16];
  int32_t open_rgb[16] = { 1, 1, 1 }, open_vid[16] = { 1, 0, 1 }, open_old[16] = { 1 };
  want ("OPEN a (RGB)", ov_call (a, open_rgb, -1, r), 0);
  want ("a: key", r[1], 0x080008);
  want ("a: version", r[3], 1);
  printf ("a: under %d, formats %d, screen %dx%d\n", r[2], r[4], r[5], r[6]);
  want ("OPEN b (RGB)", ov_call (b, open_rgb, -1, r), 0);
  want ("OPEN v (video)", ov_call (v, open_vid, -1, r), 0);
  int mf = (int) syscall (SYS_memfd_create, "ovtest", 1u);
  if (mf < 0 || ftruncate (mf, 320 * 240 * 4) < 0)
    return 1;
  int32_t add[16] = { 2, 0, 320, 240, (int32_t) DRM_FORMAT_XBGR8888, 320 * 4, 0 };
  /* (a memfd isn't a dmabuf: with real planes this is refused, which is
     right; the test mode logs it) */
  int ar = ov_call (a, add, mf, r);
  printf ("ADDBUF a: %d\n", ar);
  want ("ADDBUF b", ov_call (b, add, mf, r), ar);
  int32_t show[16] = { 3, 0, 100, 100, 320, 240, 0, 0, 320, 240, 0 };
  int sr = ov_call (a, show, -1, r);
  printf ("SHOW a: %d, before %d, at %d.%06d, took %d us\n", sr, r[1], r[3], r[4], r[5]);
  want ("SHOW a", sr, ar);
  show[2] = 500;
  want ("SHOW b", ov_call (b, show, -1, r), ar);
  int32_t bad[16] = { 3, 7, 0, 0, 10, 10, 0, 0, 10, 10 };
  if (ar)
    want ("SHOW a, no such buffer", ov_call (a, bad, -1, r), -EINVAL);
  int32_t hide[16] = { 4 };
  want ("HIDE a", ov_call (a, hide, -1, r), 0);
  /* a second video: the first one's connection is closed */
  int v2 = ov_connect ();
  want ("OPEN v2 (old client)", ov_call (v2, open_old, -1, r), 0);
  want ("v2: no version", r[3], 0);
  want ("v after v2 took over", ov_call (v, hide, -1, r), -EPIPE);
  /* the RGB overlays are still there */
  want ("HIDE b", ov_call (b, hide, -1, r), 0);
  /* a fifth connection (a, b, v2 and c3 are four): refused */
  int c3 = ov_connect (), c4 = ov_connect ();
  want ("OPEN c3 (the fourth)", ov_call (c3, open_rgb, -1, r), 0);
  int32_t rr[16] = { 0 };
  ssize_t n = recv (c4, rr, sizeof rr, 0);
  want ("the fifth is refused", n > 0 ? rr[0] : -1, -EBUSY);
  close (a); close (b); close (v); close (v2); close (c3); close (c4);
  /* and all free again */
  usleep (200000);
  a = ov_connect ();
  want ("OPEN again", ov_call (a, open_rgb, -1, r), 0);
  close (a);
  printf (fails ? "ovtest: %d failed\n" : "ovtest ok\n", fails);
  return fails != 0;
}

struct buf { int fd; uint32_t pitch, size; uint32_t *map; };

static int
dumb (int card, int w, int h, struct buf *b)
{
  struct drm_mode_create_dumb cd = { .width = (uint32_t) w, .height = (uint32_t) h, .bpp = 32 };
  if (ioctl (card, DRM_IOCTL_MODE_CREATE_DUMB, &cd))
    return -1;
  struct drm_mode_map_dumb md = { .handle = cd.handle };
  struct drm_prime_handle ph = { .handle = cd.handle, .flags = DRM_CLOEXEC | DRM_RDWR, .fd = -1 };
  if (ioctl (card, DRM_IOCTL_MODE_MAP_DUMB, &md) || ioctl (card, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph))
    return -1;
  b->map = mmap (NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, card, md.offset);
  if (b->map == MAP_FAILED)
    return -1;
  b->fd = ph.fd;
  b->pitch = cd.pitch;
  b->size = (uint32_t) cd.size;
  return 0;
}

static int
show (int n, int keyed)
{
  enum { W = 480, H = 360 };
  int card = -1;
  for (int i = 0; i < 4 && card < 0; i++)
    {
      char name[32];
      snprintf (name, sizeof name, "/dev/dri/card%d", i);
      int fd = open (name, O_RDWR | O_CLOEXEC);
      struct buf t;
      if (fd >= 0 && dumb (fd, 16, 16, &t) == 0)
	card = fd;
      else if (fd >= 0)
	close (fd);
    }
  if (card < 0)
    {
      fprintf (stderr, "ovtest: no card makes dumb buffers\n");
      return 1;
    }
  int s[3];
  struct buf b[3][2];
  for (int k = 0; k < n; k++)
    {
      int32_t r[16], op[16] = { 1, 1, 1 };
      s[k] = ov_connect ();
      int e = ov_call (s[k], op, -1, r);
      if (e)
	{
	  fprintf (stderr, "ovtest: OPEN %d: %s\n", k, strerror (-e));
	  return 1;
	}
      printf ("overlay %d: under the desktop %d, formats %d, screen %dx%d\n", k, r[2], r[4], r[5], r[6]);
      for (int i = 0; i < 2; i++)
	{
	  if (dumb (card, W, H, &b[k][i]))
	    {
	      perror ("ovtest: dumb buffer");
	      return 1;
	    }
	  int32_t add[16] = { 2, i, W, H, (int32_t) DRM_FORMAT_XRGB8888, (int32_t) b[k][i].pitch, 0 };
	  e = ov_call (s[k], add, b[k][i].fd, r);
	  if (e)
	    {
	      fprintf (stderr, "ovtest: ADDBUF: %s\n", strerror (-e));
	      return 1;
	    }
	}
    }
  /* a process for each overlay, as programs are: each draws its own
     picture, asks for it, and waits for the answer (which comes when it
     is on screen) */
  fflush (stdout);
  for (int k = 0; k < n; k++)
    {
      pid_t pid = fork ();
      if (pid < 0)
	return 1;
      if (pid)
	continue;
      struct timespec t0, t;
      clock_gettime (CLOCK_MONOTONIC, &t0);
      long frames = 0, took = 0;
      for (;;)
	{
	  clock_gettime (CLOCK_MONOTONIC, &t);
	  double el = (double) (t.tv_sec - t0.tv_sec) + (double) (t.tv_nsec - t0.tv_nsec) / 1e9;
	  if (el >= 10)
	    {
	      printf ("overlay %d of %d: %ld frames in %.1f s: %.1f a second; %ld us from SHOW to the screen\n",
		      k, n, frames, el, (double) frames / el, took / (frames > 0 ? frames : 1));
	      fflush (stdout);
	      _exit (0);
	    }
	  struct buf *d = &b[k][frames & 1];
	  for (int y = 0; y < H; y++)
	    {
	      uint32_t *row = (uint32_t *) ((char *) d->map + (size_t) y * d->pitch);
	      for (int x = 0; x < W; x++)
		{
		  int bar = ((x + (int) frames * 4) / 60 + k) % 6 + 1;
		  row[x] = (bar & 1 ? 0xFF0000u : 0) | (bar & 2 ? 0x00FF00u : 0) | (bar & 4 ? 0x0000FFu : 0);
		}
	    }
	  int32_t r[16], sh[16] = { 3, (int32_t) (frames & 1), 40 + k * 200, 60 + k * 120, W, H, 0, 0, W, H,
				    keyed ? 0 : 256 };
	  int e = ov_call (s[k], sh, -1, r);
	  if (e)
	    {
	      fprintf (stderr, "ovtest: SHOW: %s\n", strerror (-e));
	      _exit (1);
	    }
	  took += r[5];
	  frames++;
	}
    }
  int bad = 0;
  for (int k = 0; k < n; k++)
    {
      int st = 0;
      wait (&st);
      bad |= st != 0;
    }
  return bad;
}

int
main (int argc, char **argv)
{
  if (argc > 1 && !strcmp (argv[1], "show"))
    {
      int n = argc > 2 ? atoi (argv[2]) : 2;
      return show (n < 1 ? 1 : n > 3 ? 3 : n, argc > 3 && !strcmp (argv[3], "key"));
    }
  return proto ();
}
