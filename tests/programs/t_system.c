/* A program that runs another (itself) with system(): when the child ends,
   ARMEABISupport must free the child's stacks and maps (and only those), so
   the parent's mapping and its own stack still work afterwards, and only
   the parent is left running (*ARMEABISupport_Info: "nested 1"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>

/* Deep enough to use a good part of the parent's stack. */
static unsigned
deep (unsigned n)
{
  volatile unsigned char pad[512];
  pad[0] = (unsigned char) n;
  return n ? deep (n - 1) + pad[0] : 0;
}

int
main (int argc, char **argv)
{
  if (argc > 1 && strcmp (argv[1], "child") == 0)
    {
      void *m = mmap (NULL, 256 * 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
      printf ("child: running, map %s\n", m == MAP_FAILED ? "failed" : "ok");
      return 7;			/* exits without munmap: Cleanup frees it */
    }
  int fails = 0;
  unsigned *m = mmap (NULL, 512 * 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (m == MAP_FAILED)
    return 99;
  for (unsigned i = 0; i < 512 * 1024 / 4; i++)
    m[i] = i ^ 0x5A5A5A5A;
  char cmd[512];
  snprintf (cmd, sizeof cmd, "%s child", argv[0]);
  int rc = system (cmd);
  printf ("parent: system returned %d (exit status %d)\n", rc, WIFEXITED (rc) ? WEXITSTATUS (rc) : -1);
  if (!WIFEXITED (rc) || WEXITSTATUS (rc) != 7)
    fails++;
  for (unsigned i = 0; i < 512 * 1024 / 4; i++)
    if (m[i] != (i ^ 0x5A5A5A5A))
      {
	printf ("parent: map changed at word %u\n", i);
	fails++;
	break;
      }
  if (munmap (m, 512 * 1024) != 0)
    {
      printf ("parent: munmap failed (map forgotten?)\n");
      fails++;
    }
  unsigned want = 0;
  for (unsigned n = 1; n <= 1000; n++)
    want += n & 255;
  int deep_ok = deep (1000) == want;	/* about 520KB of stack */
  printf ("parent: deep recursion %s\n", deep_ok ? "ok" : "failed");
  fails += !deep_ok;
  /* Only the parent left: its id, nested once (over "no program"). */
  system ("ARMEABISupport_Info { > <Wimp$ScrapDir>.aeinfo }");
  FILE *f = fopen ("/tmp/aeinfo", "r");
  char line[256];
  int nested = -1;
  while (f && fgets (line, sizeof line, f))
    {
      /* (RISC OS writes its newlines as LF CR) */
      char *n = strstr (line, ", nested ");
      if (strstr (line, "Running program ") && n)
	{
	  nested = atoi (n + 9);
	  break;
	}
    }
  if (f)
    fclose (f);
  printf ("parent: ARMEABISupport nesting %d after the child\n", nested);
  fails += nested != 1;
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
