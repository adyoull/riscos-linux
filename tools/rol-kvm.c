/* rol-kvm: a headless screen for RISC OS for Linux.
 *
 *   rol-kvm [-o DIR] [-g WIDTHxHEIGHT] [-c FIFO] -- COMMAND [ARGS...]
 *   (-c may also name a regular file, which is followed as it grows: lines
 *   appended to it act as the FIFO's; tools/native-vm can't share a FIFO)
 *
 * Runs COMMAND (the port's ROM image, or qemu-arm-riscos with it) with a
 * SocketKVM connection, as the port's SDL front end does, but keeps the
 * screen in memory instead of showing it. It writes the screen as a PNG:
 *
 *   - when RISC OS sends this process SIGUSR1 (rol-run's *TBShot does that from
 *     RISC OS: IXSupport_LinuxSyscall kill(getppid(), SIGUSR1)), to
 *     DIR/shot-NNN.png;
 *   - on "shot FILE" from the control FIFO (-c), to FILE;
 *   - when COMMAND exits, to DIR/final.png (if a mode was ever set).
 *
 * The control FIFO also takes "mouse X Y BUTTONS" (BUTTONS: 4 select,
 * 2 menu, 1 adjust; X, Y from the top left) and "key CODE 1|0" (a RISC OS
 * low-level key number, down or up), one command a line.
 *
 * -g asks RISC OS for that screen size at start-up (as resizing the SDL
 * window does); the default is 1024x768.
 *
 * Exits with COMMAND's exit status.
 *
 * The protocol (SocketKVM_Protocol.h) is Timothy Baldwin's, from the
 * Linux port's SocketKVMFrontends (Apache-2.0); this file is written
 * from it for the test bed.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* --- the protocol (little-endian words) ----------------------------- */
enum { EV_MOUSE = 0, EV_KEYDOWN = 1, EV_KEYUP = 2, EV_MODE_SYNC = 3,
       EV_RESIZE = 4, EV_CLOSE = 5, EV_VERSION = 6 };
enum { C_MODE_CHANGE = 0, C_SET_PALETTE = 1, C_POINTER = 2, C_SUSPEND = 3,
       C_CLOSE_CTL = 4, C_VERSION = 6, C_ACTIVESCREEN = 7, C_STARTSCREEN = 8 };

struct report { int32_t reason; uint32_t a, b, c; };	/* mouse x y buttons, key code, version */

#define SCREEN_MAX (1024 * 1024 * 100)

static int sock = -1, screen_fd = -1;
static unsigned char *pixels;
static int width, height, log2bpp = -1;
static uint32_t palette[256];		/* 0x00BBGGRR */
static uint32_t start_addr, active_addr;
static int have_start, have_active;
static const char *outdir = ".";
static int shots;

static void
send_report (int reason, uint32_t a, uint32_t b, uint32_t c)
{
  struct report r = { reason, a, b, c };
  if (write (sock, &r, sizeof r) < 0)
    perror ("rol-kvm: write");
}

/* --- PNG (stored deflate blocks: no zlib needed) ----------------------- */
static uint32_t crc_table[256];

static void
crc_init (void)
{
  for (uint32_t n = 0; n < 256; n++)
    {
      uint32_t c = n;
      for (int k = 0; k < 8; k++)
	c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      crc_table[n] = c;
    }
}

static uint32_t
crc (uint32_t c, const unsigned char *p, size_t n)
{
  c ^= 0xFFFFFFFFu;
  while (n--)
    c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

static void
put32 (unsigned char *p, uint32_t v)
{
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void
chunk (FILE *f, const char *type, const unsigned char *data, uint32_t len)
{
  unsigned char hdr[8];
  put32 (hdr, len);
  memcpy (hdr + 4, type, 4);
  fwrite (hdr, 1, 8, f);
  if (len)
    fwrite (data, 1, len, f);
  uint32_t c = crc (crc (0, (const unsigned char *) type, 4), data, len);
  put32 (hdr, c);
  fwrite (hdr, 1, 4, f);
}

/* The pixel at (x, y) as 0xRRGGBB. */
static uint32_t
pixel (const unsigned char *base, int x, int y)
{
  switch (log2bpp)
    {
    case 3:
      {
	uint32_t c = palette[base[y * width + x]];
	return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
      }
    case 4:
      {
	unsigned v = base[(y * width + x) * 2] | (base[(y * width + x) * 2 + 1] << 8);
	unsigned r = v & 31, g = (v >> 5) & 31, b = (v >> 10) & 31;
	return (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
      }
    case 5:
      {
	const unsigned char *p = base + (y * width + x) * 4;
	return (p[0] << 16) | (p[1] << 8) | p[2];
      }
    }
  return 0;
}

static int
write_png (const char *path)
{
  if (log2bpp < 3 || !pixels || width <= 0 || height <= 0)
    {
      fprintf (stderr, "rol-kvm: no screen to save (mode not set yet)\n");
      return -1;
    }
  const unsigned char *base = pixels;
  if (have_start && have_active && active_addr >= start_addr)
    base += active_addr - start_addr;
  size_t row = 1 + (size_t) width * 3;
  size_t raw_len = row * height;
  unsigned char *raw = malloc (raw_len);
  if (!raw)
    return -1;
  for (int y = 0; y < height; y++)
    {
      unsigned char *r = raw + y * row;
      *r++ = 0;			/* filter: none */
      for (int x = 0; x < width; x++)
	{
	  uint32_t c = pixel (base, x, y);
	  *r++ = c >> 16; *r++ = c >> 8; *r++ = c;
	}
    }
  /* zlib stream of stored blocks */
  size_t nblocks = (raw_len + 65534) / 65535;
  size_t z_len = 2 + raw_len + nblocks * 5 + 4;
  unsigned char *z = malloc (z_len), *zp = z;
  if (!z)
    {
      free (raw);
      return -1;
    }
  *zp++ = 0x78; *zp++ = 0x01;
  uint32_t a = 1, b = 0;
  for (size_t off = 0; off < raw_len; off += 65535)
    {
      size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
      *zp++ = off + n == raw_len;
      *zp++ = n; *zp++ = n >> 8; *zp++ = ~n; *zp++ = (~n) >> 8;
      memcpy (zp, raw + off, n);
      for (size_t i = 0; i < n; i++)
	{
	  a = (a + zp[i]) % 65521;
	  b = (b + a) % 65521;
	}
      zp += n;
    }
  put32 (zp, (b << 16) | a);
  zp += 4;
  FILE *f = fopen (path, "wb");
  if (!f)
    {
      perror (path);
      free (raw); free (z);
      return -1;
    }
  static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
  fwrite (sig, 1, 8, f);
  unsigned char ihdr[13];
  put32 (ihdr, width);
  put32 (ihdr + 4, height);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  chunk (f, "IHDR", ihdr, 13);
  chunk (f, "IDAT", z, zp - z);
  chunk (f, "IEND", NULL, 0);
  int bad = fclose (f) != 0;
  free (raw); free (z);
  if (!bad)
    fprintf (stderr, "rol-kvm: %s (%dx%d)\n", path, width, height);
  return bad ? -1 : 0;
}

static void
numbered_shot (void)
{
  char path[4096];
  snprintf (path, sizeof path, "%s/shot-%03d.png", outdir, ++shots);
  write_png (path);
}

/* --- messages from RISC OS -------------------------------------------- */
static void
read_messages (void)
{
  union { int32_t reason; uint32_t w[64]; unsigned char b[33 * 8 + 16]; } c;
  for (;;)
    {
      union { char buf[CMSG_SPACE (sizeof (int)) * 2]; struct cmsghdr align; } ctl;
      struct iovec iov = { &c, sizeof c };
      struct msghdr msg = { 0 };
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      msg.msg_control = ctl.buf;
      msg.msg_controllen = sizeof ctl.buf;
      ssize_t s = recvmsg (sock, &msg, MSG_DONTWAIT);
      if (s < 4)
	return;
      for (struct cmsghdr *m = CMSG_FIRSTHDR (&msg); m; m = CMSG_NXTHDR (&msg, m))
	if (m->cmsg_level == SOL_SOCKET && m->cmsg_type == SCM_RIGHTS)
	  {
	    if (screen_fd >= 0)
	      close (screen_fd);
	    memcpy (&screen_fd, CMSG_DATA (m), sizeof (int));
	    void *p = mmap (pixels, SCREEN_MAX, PROT_READ, MAP_FIXED | MAP_SHARED, screen_fd, 0);
	    if (p == MAP_FAILED)
	      perror ("rol-kvm: mmap screen");
	  }
      switch (c.reason)
	{
	case C_MODE_CHANGE:		/* vidc[16] after the reason */
	  log2bpp = c.w[1 + 1];
	  width = c.w[1 + 5];
	  height = c.w[1 + 11];
	  send_report (EV_MODE_SYNC, 0, 0, 0);
	  break;
	case C_SET_PALETTE:		/* colour, index, type */
	  if (c.w[3] == 0 && c.w[2] < 256)
	    palette[c.w[2]] = c.w[1] >> 8;
	  break;
	case C_VERSION:
	  send_report (EV_VERSION, 1, 0, 0);
	  break;
	case C_ACTIVESCREEN:
	  active_addr = c.w[1];
	  have_active = 1;
	  break;
	case C_STARTSCREEN:
	  start_addr = c.w[1];
	  have_start = 1;
	  break;
	default:
	  break;
	}
    }
}

/* --- the control FIFO --------------------------------------------------- */
static void
control_line (char *line)
{
  char file[4096];
  unsigned x, y, buttons, code, down;
  if (sscanf (line, "shot %4095s", file) == 1)
    write_png (file);
  else if (sscanf (line, "mouse %u %u %u", &x, &y, &buttons) == 3)
    send_report (EV_MOUSE, x, y, buttons);
  else if (sscanf (line, "key %u %u", &code, &down) == 2)
    send_report (down ? EV_KEYDOWN : EV_KEYUP, code, 0, 0);
  else if (*line)
    fprintf (stderr, "rol-kvm: unknown control command: %s\n", line);
}

int
main (int argc, char **argv)
{
  const char *fifo = NULL;
  unsigned gw = 1024, gh = 768;
  int opt;
  while ((opt = getopt (argc, argv, "+o:g:c:")) != -1)
    switch (opt)
      {
      case 'o': outdir = optarg; break;
      case 'g':
	if (sscanf (optarg, "%ux%u", &gw, &gh) != 2)
	  {
	    fprintf (stderr, "rol-kvm: -g WIDTHxHEIGHT\n");
	    return 2;
	  }
	break;
      case 'c': fifo = optarg; break;
      default:
	fprintf (stderr, "usage: rol-kvm [-o DIR] [-g WxH] [-c FIFO] -- COMMAND [ARGS...]\n");
	return 2;
      }
  if (optind >= argc)
    {
      fprintf (stderr, "usage: rol-kvm [-o DIR] [-g WxH] [-c FIFO] -- COMMAND [ARGS...]\n");
      return 2;
    }
  crc_init ();
  pixels = mmap (NULL, SCREEN_MAX, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pixels == MAP_FAILED)
    {
      perror ("rol-kvm: mmap");
      return 1;
    }

  sigset_t set;
  sigemptyset (&set);
  sigaddset (&set, SIGCHLD);
  sigaddset (&set, SIGUSR1);
  sigaddset (&set, SIGTERM);
  sigaddset (&set, SIGINT);
  sigprocmask (SIG_BLOCK, &set, NULL);
  /* Non-blocking: the loop below reads until it's empty. */
  int sfd = signalfd (-1, &set, SFD_CLOEXEC | SFD_NONBLOCK);
  if (sfd < 0)
    {
      perror ("rol-kvm: signalfd");
      return 1;
    }

  int sv[2];
  if (socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv))
    {
      perror ("rol-kvm: socketpair");
      return 1;
    }
  pid_t self = getpid ();
  pid_t pid = fork ();
  if (pid == 0)
    {
      prctl (PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
      int s = fcntl (sv[1], F_DUPFD, 31);	/* not inherited by accident: no CLOEXEC */
      char env[64];
      snprintf (env, sizeof env, "%d", s);
      setenv ("RISC_OS_SocketKVM_Socket", env, 1);
      sigprocmask (SIG_UNBLOCK, &set, NULL);
      if (getppid () == self)
	execvp (argv[optind], argv + optind);
      perror ("rol-kvm: exec");
      _exit (126);
    }
  close (sv[1]);
  sock = sv[0];
  send_report (EV_RESIZE, gw, gh, 0);

  int cfd = -1, cfile = 0;
  if (fifo)
    {
      /* a FIFO; or a regular file, followed as it grows (a FIFO can't be
	 shared with tools/native-vm's VM over 9p) */
      struct stat st;
      cfile = stat (fifo, &st) == 0 && S_ISREG (st.st_mode);
      cfd = open (fifo, (cfile ? O_RDONLY : O_RDWR | O_NONBLOCK) | O_CLOEXEC);	/* RDWR: never sees EOF */
    }
  char cbuf[8192];
  size_t clen = 0;
  int status = 0;
  for (;;)
    {
      struct pollfd p[3] = { { sfd, POLLIN, 0 }, { sock, POLLIN, 0 }, { cfd, POLLIN, 0 } };
      if (poll (p, cfd >= 0 && !cfile ? 3 : 2, cfile ? 200 : -1) < 0 && errno != EINTR)
	break;
      if (cfile)
	p[2].revents = POLLIN;
      if (p[1].revents & POLLIN)
	read_messages ();
      if (cfd >= 0 && (p[2].revents & POLLIN))
	{
	  ssize_t n = read (cfd, cbuf + clen, sizeof cbuf - 1 - clen);
	  if (n == 0 && cfile)
	    p[2].revents = 0;
	  if (n > 0)
	    {
	      clen += n;
	      cbuf[clen] = 0;
	      char *nl;
	      while ((nl = strchr (cbuf, '\n')))
		{
		  *nl = 0;
		  control_line (cbuf);
		  memmove (cbuf, nl + 1, clen - (nl + 1 - cbuf) + 1);
		  clen -= nl + 1 - cbuf;
		}
	      if (clen == sizeof cbuf - 1)
		{
		  fprintf (stderr, "rol-kvm: control line too long, ignored\n");
		  clen = 0;
		}
	    }
	}
      if (p[0].revents & POLLIN)
	{
	  struct signalfd_siginfo si;
	  while (read (sfd, &si, sizeof si) == sizeof si)
	    {
	      if (si.ssi_signo == SIGUSR1)
		{
		  read_messages ();	/* any mode change first */
		  numbered_shot ();
		}
	      else if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT)
		kill (pid, SIGTERM);
	      else if (si.ssi_signo == SIGCHLD && waitpid (pid, &status, WNOHANG) == pid)
		{
		  char path[4096];
		  read_messages ();	/* the last palette or screen bank */
			  snprintf (path, sizeof path, "%s/final.png", outdir);
		  if (log2bpp >= 3)
		    write_png (path);
		  return WIFEXITED (status) ? WEXITSTATUS (status) : 128 + WTERMSIG (status);
		}
	    }
	}
    }
  return 1;
}
