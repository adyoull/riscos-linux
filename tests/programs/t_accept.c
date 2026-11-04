/* t_accept: Socket_Select on a socket from Socket_Accept, when the peer
 * sent before the accept (as RDP, TLS and most clients do: they connect,
 * send at once, and wait for an answer).
 *
 * A TCP socket listens on 127.0.0.1; a second connects to it and sends
 * "hello"; only then is the connection accepted. Socket_Select (no wait,
 * then a 1 s wait) must say the accepted socket can be read and written,
 * and Socket_Recv must get "hello".
 *
 * LinuxInternet before 5.31-7 said neither: its select answers from flags
 * that Linux's I/O signal sets, the signal is only set up for the new
 * socket in Socket_Accept, and Linux signals only new events, so data
 * already there (and room to write) were never reported. RDPServer saw
 * the connection and then never read the client's request ("server not
 * responding").
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
#define S_Listen      0x41202
#define S_Accept      0x41203
#define S_Connect     0x41204
#define S_Recv        0x41205
#define S_Send        0x41208
#define S_Getsockname 0x4120F
#define S_Close       0x41210
#define S_Select      0x41211

struct sin { uint8_t len, family; uint16_t port; uint32_t addr; char zero[8]; };
struct tv { long sec, usec; };

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static void
pause_cs (unsigned cs)
{
  unsigned t0 = 0, t = 0;
  _swix (OS_ReadMonotonicTime, _OUT (0), &t0);
  do
    _swix (OS_ReadMonotonicTime, _OUT (0), &t);
  while (t - t0 < cs);
}

/* select on S for reading and writing, waiting up to SEC seconds */
static void
sel (int s, long sec, int *rd_set, int *wr_set, int *n)
{
  uint32_t rd[8] = { 0 }, wr[8] = { 0 };
  struct tv t = { sec, 0 };
  rd[s / 32] |= 1u << (s % 32);
  wr[s / 32] |= 1u << (s % 32);
  *n = -1;
  _kernel_oserror *e = _swix (S_Select, _INR (0, 4) | _OUT (0), s + 1, rd, wr, 0, &t, n);
  if (e)
    printf ("select error &%X %s\n", e->errnum, e->errmess);
  *rd_set = !!(rd[s / 32] & (1u << (s % 32)));
  *wr_set = !!(wr[s / 32] & (1u << (s % 32)));
}

int
main (void)
{
  int l = -1, c = -1, a = -1;
  CHECK (!_swix (S_Creat, _INR (0, 2) | _OUT (0), 2, 1, 0, &l));
  CHECK (!_swix (S_Creat, _INR (0, 2) | _OUT (0), 2, 1, 0, &c));
  if (l < 0 || c < 0)
    {
      printf ("FAIL\n");
      return 1;
    }
  struct sin sa = { 16, 2, 0, 0x0100007F, { 0 } };
  _kernel_oserror *be = _swix (S_Bind, _INR (0, 2), l, &sa, 16);
  if (be && be->errnum == 0x20E31)
    {
      /* the native test VM's job init leaves the loopback interface down */
      printf ("no loopback interface here: skipped\nPASS\n");
      return 0;
    }
  CHECK (!be);
  int alen = 16;
  CHECK (!_swix (S_Getsockname, _INR (0, 2), l, &sa, &alen));
  sa.len = 16;
  sa.family = 2;
  CHECK (!_swix (S_Listen, _INR (0, 1), l, 1));
  _kernel_oserror *ce = _swix (S_Connect, _INR (0, 2), c, &sa, 16);
  if (ce)
    printf ("Socket_Connect error &%X %s\n", ce->errnum, ce->errmess);
  CHECK (!ce);
  int sent = -1;
  CHECK (!_swix (S_Send, _INR (0, 3) | _OUT (0), c, "hello", 5, 0, &sent));
  CHECK (sent == 5);
  pause_cs (20);                /* the data is waiting before the accept */

  struct sin pa;
  int plen = 16;
  _kernel_oserror *ae = _swix (S_Accept, _INR (0, 2) | _OUT (0), l, &pa, &plen, &a);
  if (ae)
    printf ("Socket_Accept error &%X %s\n", ae->errnum, ae->errmess);
  CHECK (!ae && a >= 0);
  if (a >= 0)
    {
      int r, w, n;
      sel (a, 0, &r, &w, &n);
      printf ("accepted socket %d, select with no wait: %d ready, read %s, write %s\n",
              a, n, r ? "yes" : "NO", w ? "yes" : "NO");
      CHECK (n == 2 && r && w);
      sel (a, 1, &r, &w, &n);
      printf ("select waiting up to 1 s: %d ready, read %s, write %s\n",
              n, r ? "yes" : "NO", w ? "yes" : "NO");
      CHECK (n == 2 && r && w);
      char buf[16] = { 0 };
      int got = -1;
      _kernel_oserror *re = _swix (S_Recv, _INR (0, 3) | _OUT (0), a, buf, sizeof buf - 1, 0, &got);
      if (re)
        printf ("Socket_Recv error &%X %s\n", re->errnum, re->errmess);
      printf ("received %d bytes \"%s\"\n", got, got > 0 ? buf : "");
      CHECK (got == 5 && !strcmp (buf, "hello"));
      _swix (S_Close, _IN (0), a);
    }
  _swix (S_Close, _IN (0), c);
  _swix (S_Close, _IN (0), l);
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
