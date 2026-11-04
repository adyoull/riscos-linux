/* t_sockopt: socket options and Socket_Sendmsg, with the Internet module's
 * own SWIs (as real RISC OS stacks, Internet 5 and later, answer them).
 *
 * 1. Socket_Getsockopt SOL_SOCKET/SO_TYPE gives SOCK_STREAM (1) for a TCP
 *    socket and SOCK_DGRAM (2) for a UDP one. GLib's g_socket_new_from_fd()
 *    needs this; LinuxInternet before 5.31-6 refused it (ENOPROTOOPT).
 * 2. A TCP-level option the module doesn't know (8) is refused, not
 *    treated as the socket option with the same number (SO_KEEPALIVE).
 * 3. Socket_Sendmsg takes its flags from R2: with R2 = 0 and R3 = 1
 *    (MSG_OOB, which a UDP socket refuses) the send must work.
 * 4. Socket_Sendmsg_1 (UnixLib's and TCPIPLibs' sendmsg()) exists and
 *    sends.
 * 3 and 4 send a datagram to the socket itself, so need the loopback
 * interface (skipped in the native test VM, which has none up).
 *
 * Prints what it checked, then PASS or FAIL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <kernel.h>
#include <swis.h>

#define S_Creat       0x41200
#define S_Bind        0x41201
#define S_Recv        0x41205
#define S_Sendmsg     0x4120A
#define S_Getsockopt  0x4120D
#define S_Getsockname 0x4120F
#define S_Close       0x41210
#define S_Sendmsg_1   0x4121E

struct sin { uint8_t len, family; uint16_t port; uint32_t addr; char zero[8]; };
struct iov { void *base; int len; };
struct msg { void *name; unsigned namelen; struct iov *iov; unsigned iovlen;
             void *control; unsigned controllen; int flags; };

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static int
sock (int type)
{
  int s = -1;
  _kernel_oserror *e = _swix (S_Creat, _INR (0, 2) | _OUT (0), 2, type, 0, &s);
  if (e)
    printf ("Socket_Creat %d: error &%X %s\n", type, e->errnum, e->errmess);
  return e ? -1 : s;
}

/* getsockopt LEVEL, OPT on S: the value, or -1 with the error printed */
static int
getopt (int s, int level, int opt)
{
  int v = -1, l = sizeof v;
  _kernel_oserror *e = _swix (S_Getsockopt, _INR (0, 4), s, level, opt, &v, &l);
  if (e)
    {
      printf ("Socket_Getsockopt &%X &%X: error &%X %s\n", level, opt, e->errnum, e->errmess);
      return -1;
    }
  return v;
}

/* send "x" to A from S with SWI, R2 FLAGS and R3 JUNK; then read it back */
static void
sendback (const char *what, int swi, int s, struct sin *a, int flags, int junk)
{
  char c = 'x', got[4] = { 0 };
  struct iov v = { &c, 1 };
  struct msg m = { a, 16, &v, 1, 0, 0, 0 };
  int n = -1;
  _kernel_oserror *e = _swix (swi, _INR (0, 3) | _OUT (0), s, &m, flags, junk, &n);
  if (e)
    printf ("%s: error &%X %s\n", what, e->errnum, e->errmess);
  int r = -1;
  if (!e)
    _swix (S_Recv, _INR (0, 3) | _OUT (0), s, got, sizeof got, 0x80 /* MSG_DONTWAIT */, &r);
  int ok = !e && n == 1 && r == 1 && got[0] == 'x';
  printf ("%s: sent %d, received %d %s\n", what, n, r, ok ? "ok" : "WRONG");
  if (!ok)
    fails++;
}

int
main (void)
{
  int t = sock (1), u = sock (2);
  CHECK (t >= 0 && u >= 0);
  if (t < 0 || u < 0)
    {
      printf ("FAIL\n");
      return 1;
    }

  /* 1: SO_TYPE */
  int vt = getopt (t, 0xFFFF, 0x1008), vu = getopt (u, 0xFFFF, 0x1008);
  printf ("SO_TYPE: TCP socket %d, UDP socket %d\n", vt, vu);
  CHECK (vt == 1);
  CHECK (vu == 2);

  /* 2: an unknown TCP option */
  int v8 = getopt (t, 6, 8);
  printf ("TCP option 8: %s\n", v8 == -1 ? "refused (right)" : "answered (WRONG: read as SO_KEEPALIVE)");
  CHECK (v8 == -1);

  /* 3 and 4 */
  struct sin a = { 16, 2, 0, 0x0100007F, { 0 } };
  _kernel_oserror *be = _swix (S_Bind, _INR (0, 2), u, &a, 16);
  if (be && be->errnum == 0x20E31)
    printf ("no loopback here: the Sendmsg checks are skipped\n");
  else
    {
      CHECK (!be);
      int alen = 16;
      CHECK (!_swix (S_Getsockname, _INR (0, 2), u, &a, &alen));
      a.len = 16, a.family = 2;
      sendback ("Socket_Sendmsg (R2 0, R3 1)", S_Sendmsg, u, &a, 0, 1);
      sendback ("Socket_Sendmsg_1", S_Sendmsg_1, u, &a, 0, 0);
    }

  _swix (S_Close, _IN (0), t);
  _swix (S_Close, _IN (0), u);
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
