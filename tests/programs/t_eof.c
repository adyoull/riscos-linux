/* t_eof: select() and read() see the other end close a TCP connection.
 *
 * A socket listens on 127.0.0.1 and a second connects to it. The accepted
 * end sends "hello" and closes. A moment later (so the close has arrived)
 * the client select()s, reads "hello", then select()s again: that must say
 * readable at once, and read() must return 0 (end of file).
 *
 * LinuxInternet before 5.31-12 lost it: after a read it marked the socket
 * readable again only if more data was waiting, so a close that had
 * already arrived was never reported, and select() waited out its timeout
 * (ClawsMail's feed and calendar fetches each took 60 s).
 *
 * Prints what it checked, then PASS or FAIL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int
readable (int s, int secs)
{
  fd_set f;
  FD_ZERO (&f);
  FD_SET (s, &f);
  struct timeval tv = { secs, 0 };
  return select (s + 1, &f, NULL, NULL, &tv);
}

int
main (void)
{
  int fail = 0;
  int l = socket (AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a;
  memset (&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = inet_addr ("127.0.0.1");
  socklen_t al = sizeof a;
  if (bind (l, (struct sockaddr *) &a, sizeof a) && errno == EADDRNOTAVAIL)
    {
      /* (tools/native-vm's VM has no network, not even loopback) */
      printf ("no loopback interface here: skipped\nPASS\n");
      return 0;
    }
  if (listen (l, 1) || getsockname (l, (struct sockaddr *) &a, &al))
    {
      printf ("listen: %s\nFAIL\n", strerror (errno));
      return 1;
    }
  int c = socket (AF_INET, SOCK_STREAM, 0);
  if (connect (c, (struct sockaddr *) &a, sizeof a))
    {
      printf ("connect: %s\nFAIL\n", strerror (errno));
      return 1;
    }
  int s = accept (l, NULL, NULL);
  if (s < 0)
    {
      printf ("accept: %s\nFAIL\n", strerror (errno));
      return 1;
    }
  write (s, "hello", 5);
  close (s);
  usleep (300000);		/* the close arrives before the first read */

  int r = readable (c, 2);
  char buf[64];
  int n = r > 0 ? read (c, buf, sizeof buf) : -1;
  printf ("first select %d, read %d\n", r, n);
  if (r != 1 || n != 5 || memcmp (buf, "hello", 5))
    fail = 1;

  time_t t0 = time (NULL);
  r = readable (c, 5);
  printf ("second select %d after %ld s\n", r, (long) (time (NULL) - t0));
  if (r != 1)
    fail = 1;
  else
    {
      n = read (c, buf, sizeof buf);
      printf ("read %d (end of file)\n", n);
      if (n != 0)
	fail = 1;
    }
  close (c);
  close (l);
  puts (fail ? "FAIL" : "PASS");
  return fail;
}
