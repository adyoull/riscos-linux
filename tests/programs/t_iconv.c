/* iconv (UnixLib -> the Iconv module): UTF-8 <-> Latin-1, UTF-16 with a
   byte order mark, an invalid sequence, and a character Latin-1 can't
   hold (an error, then "?" with //TRANSLIT).
   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <errno.h>
#include <iconv.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static size_t
conv (const char *to, const char *from, const char *in, size_t inlen, char *out, size_t outlen, int *err)
{
  iconv_t cd = iconv_open (to, from);
  *err = 0;
  if (cd == (iconv_t) -1)
    {
      *err = errno;
      return (size_t) -1;
    }
  char *ip = (char *) in, *op = out;
  size_t il = inlen, ol = outlen;
  size_t r = iconv (cd, &ip, &il, &op, &ol);
  if (r == (size_t) -1)
    *err = errno;
  iconv_close (cd);
  return r == (size_t) -1 ? r : outlen - ol;
}

int
main (void)
{
  char out[64];
  int err;
  size_t n = conv ("ISO-8859-1", "UTF-8", "caf\xc3\xa9", 5, out, sizeof out, &err);
  CHECK (n == 4 && !memcmp (out, "caf\xe9", 4));
  n = conv ("UTF-8", "ISO-8859-1", "caf\xe9", 4, out, sizeof out, &err);
  CHECK (n == 5 && !memcmp (out, "caf\xc3\xa9", 5));
  n = conv ("UTF-16", "UTF-8", "A\xe2\x82\xac", 4, out, sizeof out, &err);
  CHECK (n == 6 && !memcmp (out, "\xfe\xff\x00\x41\x20\xac", 6));
  n = conv ("UTF-8", "UTF-16", "\xff\xfe\x41\x00\xac\x20", 6, out, sizeof out, &err);
  CHECK (n == 4 && !memcmp (out, "A\xe2\x82\xac", 4));
  n = conv ("ISO-8859-1", "UTF-8", "\xc3\x28", 2, out, sizeof out, &err);
  CHECK (n == (size_t) -1 && err == EILSEQ);
  n = conv ("ISO-8859-1", "UTF-8", "\xe2\x82\xac", 3, out, sizeof out, &err);
  CHECK (n == (size_t) -1 && err == EILSEQ);
  n = conv ("ISO-8859-1//TRANSLIT", "UTF-8", "\xe2\x82\xac!", 4, out, sizeof out, &err);
  CHECK (n == 2 && !memcmp (out, "?!", 2));
  CHECK (iconv_open ("KLINGON", "UTF-8") == (iconv_t) -1);
  puts (fails ? "FAIL" : "PASS");
  return fails != 0;
}
