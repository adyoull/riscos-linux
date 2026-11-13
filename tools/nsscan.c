/* tools/nsscan: NativeSupport's scanner on the build machine.

   NativeSupport rewrites privileged instructions in code loaded from disc
   (MRS/MSR CPSR, CPS, LDM/STM ^) and has to tell them from data that
   reads the same. This runs that same code (modules/NativeSupport's
   sites.c, scan.c and privform.c, compiled in unchanged) over files, and lists every
   word it would rewrite, so a change to the rules can be tried on real
   programs and modules in seconds, and a word that is really data shows
   up here and not in someone's desktop.

   It is a 32-bit ARM Linux program (the scanner keeps addresses in 32-bit
   words), run through qemu-arm:

     make -C tools nsscan            (arm-linux-gnueabihf-gcc -static)
     qemu-arm out/bin/nsscan [-q] [-w] FILE...
     qemu-arm out/bin/nsscan -t tests/nsscan-cases.txt

   FILE: a program's image (!RunImage: AIF or not), a module, or any
   piece of code; all of it is scanned as one range, which is at least
   what NativeSupport scans of it (a squeezed program is not scanned
   until it has unpacked itself: nsscan says so and scans it anyway).
   For each word that would be rewritten: its offset, the word, what it is
   as an instruction, the two words either side, and, for an AIF or ELF
   file, a note if it isn't in the file's code (NativeSupport scans what
   programs synchronise, which is normally only code: a word listed
   outside it is not evidence of a fault). -q: only the count
   per file. -w: also the words that look privileged and were left.
   -m: the files are modules (in a 32-bit one, a privileged NE instruction
   after TEQ pc, pc becomes a NOP, as in the RMA).
   -t FILE: the cases in FILE (see tests/nsscan-cases.txt): each a row of
   words and which of them must be rewritten; exit status 1 if any differ.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* what sites.c and scan.c use of the rest of NativeSupport */
#define ROM_LO 0x20000000u
#define ROM_HI 0x20C00000u
#define SYS_mprotect 125
#define OS_SynchroniseCodeAreas 0x6e
typedef struct { unsigned r[10]; } regs;
static struct
{
  unsigned full, stubbed, cond_nopped, lur_stubbed;
  int lur_ok;
} ns;
static int show_left;
static void
dbg (const char *what, unsigned a, unsigned b, unsigned c)
{
  if (show_left)
    printf ("    %s &%08X &%08X %u\n", what, a, b, c);
}
static int lsys3 (unsigned n, unsigned a, unsigned b, unsigned c) { (void) n; (void) a; (void) b; (void) c; return 0; }
static void swix_regs (unsigned swi, regs *r) { (void) swi; (void) r; }
static unsigned mono_cs (void) { return 0; }
static unsigned LOADUSRREGISTERS;

#include "../modules/NativeSupport/sites.c"
#include "../modules/NativeSupport/scan.c"
#include "../modules/NativeSupport/privform.c"

static const char *
what (unsigned w)
{
  static char b[48];
  static const char *cc[] = { "EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", "HI", "LS", "GE", "LT", "GT", "LE", "", "" };
  const char *c = cc[w >> 28];
  if ((w & 0xfff1fe20u) == 0xf1000000u)
    snprintf (b, sizeof b, "CPS%s", (w >> 18) & 3 ? ((w >> 18) & 1 ? "ID" : "IE") : "");
  else if ((w & 0x0fbf0fffu) == 0x010f0000u)
    snprintf (b, sizeof b, "MRS%s r%u, %cPSR", c, (w >> 12) & 15, w & 0x400000u ? 'S' : 'C');
  else if ((w & 0x0fb0fff0u) == 0x0120f000u)
    snprintf (b, sizeof b, "MSR%s %cPSR_%s%s%s%s, r%u", c, w & 0x400000u ? 'S' : 'C', w & 0x80000u ? "f" : "",
	      w & 0x40000u ? "s" : "", w & 0x20000u ? "x" : "", w & 0x10000u ? "c" : "", w & 15);
  else if ((w & 0x0fb0f000u) == 0x0320f000u)
    snprintf (b, sizeof b, "MSR%s %cPSR_%s%s, #&%X", c, w & 0x400000u ? 'S' : 'C', w & 0x80000u ? "f" : "",
	      w & 0x10000u ? "c" : "", w & 0xff);
  else if ((w & 0x0e400000u) == 0x08400000u)
    snprintf (b, sizeof b, "%s%s r%u, {&%04X}^", w & 0x100000u ? "LDM" : "STM", c, (w >> 16) & 15, w & 0xffff);
  else
    snprintf (b, sizeof b, "?");
  return b;
}

static void
reset (void)
{
  if (!sites)
    {
      sites = calloc (SITES_MAX, sizeof *sites);
      site_hash = calloc (SITE_HASH, sizeof *site_hash);
    }
  memset (site_hash, 0, SITE_HASH * sizeof *site_hash);
  nsites = 0;
  nnops = 0;
  nostubs = 1;
  literals_left = mrs_flags_left = veneer_psr = userbank_sites = 0;
  ns.full = 0;
}

/* scans n words (a copy of them is changed, as NativeSupport would change
   the code); changed[i] set where a word was rewritten; the count */
static unsigned
scan (const unsigned *orig, unsigned n, int module, unsigned char *changed, unsigned **copy_out)
{
  unsigned *c = malloc ((n + 1) * 4), k = 0;
  memcpy (c, orig, n * 4);
  reset ();
  patch_range ((unsigned) c, (unsigned) (c + n));
  if (module && module_32bit ((unsigned) c, n * 4))
    cond_nop_range ((unsigned) c, (unsigned) (c + n));
  for (unsigned i = 0; i < n; i++)
    k += changed[i] = c[i] != orig[i];
  if (copy_out)
    *copy_out = c;
  else
    free (c);
  return k;
}

/* where in the file a word is, when the file says: an AIF image's
   read-only area (its code), its data, or past them (debugging tables,
   not loaded); an ELF file's executable segments or not */
static const char *
where (const unsigned *w, unsigned n, unsigned off)
{
  if (n > 32 && w[4] == 0xef000011u && (w[0] >> 24) != 0xeb)
    return off < w[5] ? "" : off < w[5] + w[6] ? "  [in the image's data]" : "  [past the image: not loaded]";
  if (n > 13 && w[0] == 0x464c457fu)
    {
      unsigned phoff = w[7], phnum = w[11] >> 16, phentsize = w[10] >> 16;
      for (unsigned i = 0; i < phnum && phentsize >= 32; i++)
	{
	  const unsigned *ph = (const unsigned *) ((const char *) w + phoff + i * phentsize);
	  if ((unsigned) ((const char *) (ph + 8) - (const char *) w) > n * 4)
	    break;
	  if (ph[0] == 1 && off >= ph[1] && off < ph[1] + ph[4])
	    return ph[6] & 1 ? "" : "  [in a data segment]";
	}
      return "  [not in a loaded segment]";
    }
  return "";
}

static int
do_file (const char *name, int quiet, int module)
{
  FILE *f = fopen (name, "rb");
  if (!f)
    {
      perror (name);
      return 1;
    }
  fseek (f, 0, SEEK_END);
  long len = ftell (f);
  rewind (f);
  unsigned n = (unsigned) len / 4;
  unsigned *w = calloc (n + 1, 4);
  unsigned char *ch = calloc (n + 1, 1);
  if (fread (w, 1, (size_t) len, f) != (size_t) len)
    perror (name);
  fclose (f);
  unsigned *c;
  unsigned k = scan (w, n, module, ch, &c);
  /* an AIF image whose first instruction is a BL has a decompressor to
     run first (squeeze): NativeSupport waits until that has run */
  int aif = n > 32 && w[4] == 0xef000011u;
  printf ("%s: %u of %u words would be rewritten%s%s", name, k, n,
	  aif && (w[0] >> 24) == 0xeb ? " (squeezed AIF: scanned once unpacked; this is the packed image)" : "",
	  ns.full ? " (the site table filled)" : "");
  printf (" [left alone: %u literals, %u flags-only MRS]\n", literals_left, mrs_flags_left);
  if (!quiet)
    for (unsigned i = 0; i < n; i++)
      if (ch[i])
	{
	  printf ("  +%06X %08X %-22s", i * 4, w[i], c[i] == 0xe1a00000u ? "(never run: NOP)" : what (w[i]));
	  for (int d = -2; d <= 2; d++)
	    if (d && (int) i + d >= 0 && i + d < n)
	      printf (" %s%08X", d == 1 ? "| " : "", w[i + d]);
	  printf ("%s\n", where (w, n, i * 4));
	}
  free (c);
  free (w);
  free (ch);
  return 0;
}

/* a case: "name: word word *word word" (hex; * marks the words that must
   be rewritten, and only those); # starts a comment */
static int
do_cases (const char *name)
{
  FILE *f = fopen (name, "r");
  if (!f)
    {
      perror (name);
      return 1;
    }
  char line[4096];
  int bad = 0, cases = 0;
  while (fgets (line, sizeof line, f))
    {
      char *h = strchr (line, '#'), *colon;
      if (h)
	*h = 0;
      if (!(colon = strchr (line, ':')))
	continue;
      *colon = 0;
      unsigned w[256], n = 0;
      unsigned char want[256], got[256];
      for (char *t = strtok (colon + 1, " \t\r\n"); t && n < 256; t = strtok (NULL, " \t\r\n"))
	{
	  want[n] = *t == '*';
	  w[n] = (unsigned) strtoul (t + want[n], NULL, 16);
	  n++;
	}
      if (!n)
	continue;
      cases++;
      scan (w, n, 0, got, NULL);
      if (memcmp (want, got, n))
	{
	  bad++;
	  printf ("FAIL %s:", line);
	  for (unsigned i = 0; i < n; i++)
	    printf (" %s%08X%s", got[i] ? "*" : "", w[i], got[i] != want[i] ? "<" : "");
	  printf ("\n");
	}
    }
  fclose (f);
  printf ("nsscan: %d cases, %d failed\n", cases, bad);
  return bad != 0;
}

int
main (int argc, char **argv)
{
  int quiet = 0, module = 0, rc = 0, any = 0;
  for (int a = 1; a < argc; a++)
    if (!strcmp (argv[a], "-q"))
      quiet = 1;
    else if (!strcmp (argv[a], "-w"))
      show_left = debug_faults = 1;
    else if (!strcmp (argv[a], "-m"))
      module = 1;
    else if (!strcmp (argv[a], "-t") && a + 1 < argc)
      rc |= do_cases (argv[++a]), any = 1;
    else
      rc |= do_file (argv[a], quiet, module), any = 1;
  if (!any)
    {
      fprintf (stderr, "nsscan [-q] [-w] [-m] FILE... | -t CASES\n");
      return 2;
    }
  return rc;
}
