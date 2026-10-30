/* Arguments in, output on stdout and stderr, exit status out. */
#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
    printf ("arg%d=%s\n", i, argv[i]);
  fprintf (stderr, "a line on stderr\n");
  return argc > 1 ? atoi (argv[1]) : 0;
}
