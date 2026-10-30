/* Files through UnixLib and IXFS: write, read back, stat, rename, remove,
   directories, and the environment. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

int
main (void)
{
  char dir[64], a[96], b[96];
  snprintf (dir, sizeof dir, "/tmp/rolfiles%d", (int) getpid ());
  snprintf (a, sizeof a, "%s/one.txt", dir);
  snprintf (b, sizeof b, "%s/two.txt", dir);
  CHECK (mkdir (dir, 0755) == 0);
  FILE *f = fopen (a, "w");
  CHECK (f != NULL);
  for (int i = 0; i < 1000; i++)
    fprintf (f, "line %d\n", i);
  CHECK (fclose (f) == 0);
  struct stat st;
  CHECK (stat (a, &st) == 0 && st.st_size == 8890);
  CHECK (rename (a, b) == 0);
  f = fopen (b, "r");
  CHECK (f != NULL);
  char line[32];
  int n = 0;
  while (f && fgets (line, sizeof line, f))
    n++;
  if (f)
    fclose (f);
  CHECK (n == 1000);
  DIR *d = opendir (dir);
  CHECK (d != NULL);
  int found = 0;
  struct dirent *e;
  while (d && (e = readdir (d)))
    found |= strcmp (e->d_name, "two.txt") == 0;
  if (d)
    closedir (d);
  CHECK (found);
  CHECK (remove (b) == 0);
  CHECK (rmdir (dir) == 0);
  setenv ("ROLTEST", "value", 1);
  CHECK (getenv ("ROLTEST") && strcmp (getenv ("ROLTEST"), "value") == 0);
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
