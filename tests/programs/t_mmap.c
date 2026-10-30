/* mmap, mremap and munmap (ARMEABISupport's MMapOp), a file mapping, and
   a large malloc. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MB (1024 * 1024)
static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static void fill (unsigned char *p, size_t n, unsigned seed) { for (size_t i = 0; i < n; i++) p[i] = (unsigned char) (i * 7 + seed); }
static int same (const unsigned char *p, size_t n, unsigned seed) { for (size_t i = 0; i < n; i++) if (p[i] != (unsigned char) (i * 7 + seed)) return 0; return 1; }

int
main (void)
{
  unsigned char *p = mmap (NULL, MB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  CHECK (p != MAP_FAILED);
  fill (p, MB, 1);
  /* a second map, so the first can't simply grow in place */
  unsigned char *q = mmap (NULL, 64 * 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  CHECK (q != MAP_FAILED);
  unsigned char *r = mremap (p, MB, 8 * MB, MREMAP_MAYMOVE);
  CHECK (r != MAP_FAILED);
  CHECK (same (r, MB, 1));
  fill (r, 8 * MB, 2);
  CHECK (munmap (r + 4 * MB, 4 * MB) == 0);	/* the tail */
  CHECK (munmap (r + MB, MB) == 0);		/* a hole */
  CHECK (same (r, MB, 2));
  CHECK (munmap (r, MB) == 0);
  CHECK (munmap (r + 2 * MB, 2 * MB) == 0);
  CHECK (munmap (q, 64 * 1024) == 0);

  /* mremap: the new part of a block made bigger reads as zeros, even where
     the pages were used before */
  unsigned char *z = mmap (NULL, 3 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  CHECK (z != MAP_FAILED);
  memset (z, 0xEE, 3 * 4096);
  /* a map after it, so the pages it gives up aren't returned to RISC OS */
  unsigned char *y = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  CHECK (y != MAP_FAILED);
  unsigned char *z1 = mremap (z, 3 * 4096, 4096, 0);
  CHECK (z1 == z);
  unsigned char *z2 = mremap (z, 4096, 3 * 4096, MREMAP_MAYMOVE);
  CHECK (z2 != MAP_FAILED);
  int zeros = 1;
  for (int i = 4096; i < 3 * 4096; i++)
    zeros &= z2[i] == 0;
  CHECK (zeros);
  CHECK (z2[0] == 0xEE);
  CHECK (munmap (z2, 3 * 4096) == 0);
  CHECK (munmap (y, 4096) == 0);

  /* a file mapping (UnixLib reads the file into the block) */
  char name[64];
  snprintf (name, sizeof name, "/tmp/rolmmap%d", (int) getpid ());
  int fd = open (name, O_RDWR | O_CREAT | O_TRUNC, 0644);
  CHECK (fd >= 0);
  static unsigned char buf[10000];
  fill (buf, sizeof buf, 3);
  CHECK (write (fd, buf, sizeof buf) == (ssize_t) sizeof buf);
  unsigned char *f = mmap (NULL, sizeof buf, PROT_READ, MAP_PRIVATE, fd, 0);
  CHECK (f != MAP_FAILED);
  CHECK (f != MAP_FAILED && same (f, sizeof buf, 3));
  if (f != MAP_FAILED)
    CHECK (munmap (f, sizeof buf) == 0);
  close (fd);
  unlink (name);

  /* a big malloc */
  unsigned char *m = malloc (64 * MB);
  CHECK (m != NULL);
  if (m)
    {
      fill (m, 64 * MB, 4);
      CHECK (same (m, 64 * MB, 4));
      free (m);
    }
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
