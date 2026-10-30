/* ROBench: the runner. Calibrates each test so a timed run lasts the
   target time, runs it several times, checks the results and reports
   the median, the best and the spread.
   Copyright 2026 Andrew Youll. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "bench.h"
#ifdef __riscos
#include <kernel.h>
#include <swis.h>
#endif

#define VERSION "1.01"
#define DATE "18-Oct-2026"

const char *g_scratch;
static double g_target = 2.0;   /* seconds per timed run */
static int g_runs = 5;
static FILE *g_out;             /* results file (also stdout) */

double
now_seconds (void)
{
#ifdef __riscos
  /* centiseconds; runs last >= 1 s, so +-0.5% at worst */
  int t = 0;
  _swix (OS_ReadMonotonicTime, _OUT (0), &t);
  return (unsigned) t / 100.0;
#else
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

static void
out (const char *fmt, ...)
  __attribute__ ((format (printf, 1, 2)));

#include <stdarg.h>
static void
out (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fflush (stdout);
  if (g_out)
    {
      va_start (ap, fmt);
      vfprintf (g_out, fmt, ap);
      va_end (ap);
    }
}

/* reference results (a previous run's CSV) for the ratio column */
typedef struct { char id[32]; double median; } ref_t;
static ref_t g_ref[128];
static int g_nref;
static char g_refname[80];

static void
load_ref (const char *file)
{
  FILE *f = fopen (file, "r");
  char line[256];
  if (!f)
    {
      fprintf (stderr, "robench: can't read %s\n", file);
      return;
    }
  while (fgets (line, sizeof line, f) && g_nref < 128)
    {
      char id[32], unit[16];
      double med;
      if (strncmp (line, "machine,", 8) == 0)
        {
          snprintf (g_refname, sizeof g_refname, "%s", line + 8);
          g_refname[strcspn (g_refname, "\r\n")] = 0;
        }
      if (sscanf (line, "result,%31[^,],%15[^,],%lf", id, unit, &med) == 3)
        {
          strcpy (g_ref[g_nref].id, id);
          g_ref[g_nref++].median = med;
        }
    }
  fclose (f);
}

static double
ref_of (const char *id)
{
  for (int i = 0; i < g_nref; i++)
    if (!strcmp (g_ref[i].id, id))
      return g_ref[i].median;
  return 0;
}

static int
cmp_d (const void *a, const void *b)
{
  double x = *(const double *) a, y = *(const double *) b;
  return x < y ? -1 : x > y;
}

typedef struct { const test_t *t; double median, best, spread; int ok; } res_t;
static res_t g_res[128];
static int g_nres;

/* Calibrate: grow the count until a run takes >= 1/8 of the target
   (this also warms caches and branch predictors), then scale it so a
   run lasts the target time. */
static uint32_t
calibrate (const test_t *t)
{
  uint32_t n = 1;
  for (;;)
    {
      double t0 = now_seconds ();
      t->run (n);
      double dt = now_seconds () - t0;
      if (dt >= g_target / 8 || n >= 0x40000000u)
        {
          double want = n * (g_target / (dt > 0 ? dt : 0.01));
          return want < 1 ? 1 : want > 4e9 ? 0xffffffffu : (uint32_t) want;
        }
      n = dt < g_target / 80 ? n * 10 : n * 2;
    }
}

static void
run_test (const test_t *t)
{
  char msg[160] = "";
  double times[32];
  uint32_t sum0 = 0;
  int ok = 1;
  int se = t->setup ? t->setup () : 0;
  if (se != 0)
    {
      out ("%-15s %s: SKIPPED (%s)\n", t->id, t->name,
           se == 2 ? "no NEON" : "out of memory");
      return;
    }
  uint32_t n = calibrate (t);
  for (int r = 0; r < g_runs; r++)
    {
      double t0 = now_seconds ();
      uint32_t sum = t->run (n);
      times[r] = now_seconds () - t0;
      if (r == 0)
        sum0 = sum;
      else if (sum != sum0 && !(t->flags & T_GFX))
        ok = 0;                 /* same work must give the same answer */
    }
  if (t->verify && !t->verify (msg, sizeof msg))
    ok = 0;
  qsort (times, g_runs, sizeof times[0], cmp_d);
  double tmed = times[g_runs / 2], tbest = times[0], tworst = times[g_runs - 1];
  double units = t->work * n;
  double med, best;
  if (t->flags & T_LOWER_BETTER)
    med = tmed * 1e9 / units, best = tbest * 1e9 / units;
  else
    med = units / tmed, best = units / tbest;
  double spread = (tworst - tbest) / tmed * 100;
  res_t *r = &g_res[g_nres++];
  r->t = t, r->median = med, r->best = best, r->spread = spread, r->ok = ok;
  char ratio[24] = "";
  double ref = ref_of (t->id);
  if (ref > 0)
    snprintf (ratio, sizeof ratio, "%7.2fx",
              t->flags & T_LOWER_BETTER ? ref / med : med / ref);
  /* the screen gets a short line (80 columns); the file the details */
  printf ("%-15s %11.2f %-9s %5.1f%% %8s%s\n", t->id, med, t->unit, spread,
          ratio, ok ? "" : " FAILED");
  fflush (stdout);
  if (!ok && msg[0])
    printf ("                %s\n", msg);
  if (g_out)
    fprintf (g_out, "%-15s %-44.44s %11.2f %-8s %11.2f %5.1f%% %8s %s%s%s\n",
             t->id, t->name, med, t->unit, best, spread, ratio,
             ok ? "" : "FAILED ", msg[0] ? "- " : "", msg);
}

static int
selected (const test_t *t, const char *only, int gfx, int io)
{
  if ((t->flags & T_GFX) && !gfx)
    return 0;
  if ((t->flags & T_IO) && !io)
    return 0;
  if (!only)
    return 1;
  char buf[256];
  snprintf (buf, sizeof buf, "%s", only);
  for (char *p = strtok (buf, ","); p; p = strtok (NULL, ","))
    if (strncmp (t->id, p, strlen (p)) == 0)
      return 1;
  return 0;
}

extern void machine_info (char *buf, size_t len, char *shortname, size_t sl);
extern void gfx_begin (void), gfx_end (void);

static void
usage (void)
{
  puts ("robench " VERSION " (" DATE "): CPU, memory, OS and graphics benchmark\n"
        "  -t <s>     seconds per timed run (default 2)\n"
        "  -r <n>     timed runs per test (default 5; the median is reported)\n"
        "  -q         quick: 0.5 s, 3 runs\n"
        "  -only a,b  only tests whose names start with a or b (e.g. fp,mem.lat)\n"
        "  -g         include the graphics tests (draws on the screen)\n"
        "  -noio      leave out the file tests\n"
        "  -noneon    run the neon tests as plain C\n"
        "  -d <dir>   directory for the file tests (default <Wimp$ScrapDir>)\n"
        "  -o <file>  also write the results to a file (with CSV lines)\n"
        "  -ref <f>   compare with an earlier results file (ratio column)\n"
        "  -list      list the tests");
}

int
main (int argc, char **argv)
{
  const char *only = NULL, *outfile = NULL;
  int gfx = 0, io = 1, list = 0;
  g_scratch = NULL;
  for (int i = 1; i < argc; i++)
    {
      const char *a = argv[i];
      if (!strcmp (a, "-t") && i + 1 < argc)
        g_target = atof (argv[++i]);
      else if (!strcmp (a, "-r") && i + 1 < argc)
        g_runs = atoi (argv[++i]);
      else if (!strcmp (a, "-q"))
        g_target = 0.5, g_runs = 3;
      else if (!strcmp (a, "-only") && i + 1 < argc)
        only = argv[++i];
      else if (!strcmp (a, "-g"))
        gfx = 1;
      else if (!strcmp (a, "-noneon"))
        neon_disable ();
      else if (!strcmp (a, "-noio"))
        io = 0;
      else if (!strcmp (a, "-d") && i + 1 < argc)
        g_scratch = argv[++i];
      else if (!strcmp (a, "-o") && i + 1 < argc)
        outfile = argv[++i];
      else if (!strcmp (a, "-ref") && i + 1 < argc)
        load_ref (argv[++i]);
      else if (!strcmp (a, "-list"))
        list = 1;
      else
        {
          usage ();
          return a[1] == 'h' ? 0 : 1;
        }
    }
  if (g_runs < 1)
    g_runs = 1;
  if (g_runs > 31)
    g_runs = 31;
  if (g_target < 0.2)
    g_target = 0.2;

  const test_t *all[160];
  size_t n = 0;
  for (size_t i = 0; i < n_cpu_tests; i++)
    all[n++] = &cpu_tests[i];
  for (size_t i = 0; i < n_neon_tests; i++)
    all[n++] = &neon_tests[i];
  for (size_t i = 0; i < n_os_tests; i++)
    all[n++] = &os_tests[i];
  if (list)
    {
      for (size_t i = 0; i < n; i++)
        printf ("%-15s %-44s %s%s%s\n", all[i]->id, all[i]->name, all[i]->unit,
                all[i]->flags & T_GFX ? " (-g)" : "",
                all[i]->flags & T_LOWER_BETTER ? " (lower is better)" : "");
      return 0;
    }
  static char outname[300];
  if (outfile)
    {
      /* %T in the name: the date and time, so runs don't overwrite */
      const char *p = strstr (outfile, "%T");
      if (p)
        {
          time_t t0 = time (NULL);
          char stamp[32];
          strftime (stamp, sizeof stamp, "%y%m%d%H%M", localtime (&t0));
          snprintf (outname, sizeof outname, "%.*s%s%s", (int) (p - outfile),
                    outfile, stamp, p + 2);
        }
      else
        snprintf (outname, sizeof outname, "%s", outfile);
      outfile = outname;
      g_out = fopen (outfile, "w");
      if (!g_out)
        fprintf (stderr, "robench: can't write %s\n", outfile);
#ifdef __riscos
      else      /* for !Run: which file to open, and add the BASIC results to */
        _swix (OS_SetVarVal, _INR (0, 4), "ROBench$Last", outfile,
               (int) strlen (outfile), 0, 0);
#endif
    }

  char info[1024], shortname[128];
  machine_info (info, sizeof info, shortname, sizeof shortname);
  time_t now = time (NULL);
  char when[64];
  strftime (when, sizeof when, "%Y-%m-%d %H:%M", localtime (&now));
  out ("ROBench %s (%s) - %s\n%sNEON tests: %s\n", VERSION, DATE, when, info,
       neon_mode ());
  out ("Timing: %.1f s per run, %d runs; the median is reported. Spread =\n"
       "(slowest - fastest) / median.\n", g_target, g_runs);
  if (g_nref)
    out ("Ratio column: this machine / %s (higher is better).\n", g_refname);
  printf ("\n%-15s %11s %-9s %6s %8s\n", "test", "median", "unit", "spread",
          g_nref ? "ratio" : "");
  if (g_out)
    fprintf (g_out, "\n%-15s %-44s %11s %-8s %11s %6s %8s\n", "test", "what",
             "median", "unit", "best", "spread", g_nref ? "ratio" : "");

  int gfx_on = 0;
  for (size_t i = 0; i < n; i++)
    {
      const test_t *t = all[i];
      if (!selected (t, only, gfx, io))
        continue;
      if ((t->flags & T_GFX) && !gfx_on)
        gfx_begin (), gfx_on = 1;
      run_test (t);
      /* free a group's memory once no later selected test shares it */
      if (t->teardown)
        {
          int later = 0;
          for (size_t j = i + 1; j < n; j++)
            if (all[j]->teardown == t->teardown && selected (all[j], only, gfx, io))
              later = 1;
          if (!later)
            t->teardown ();
        }
    }
  if (gfx_on)
    gfx_end ();

  /* summary: geometric mean of the ratios per group */
  if (g_nref)
    {
      static const char *const groups[] = { "int.", "fp.", "mem.", "os.", "io.",
        "gfx." };
      double all_l = 0;
      int all_n = 0;
      out ("\nGeometric mean of the ratios to %s:\n", g_refname);
      for (int g = 0; g < 6; g++)
        {
          double l = 0;
          int c = 0;
          for (int i = 0; i < g_nres; i++)
            {
              double ref = ref_of (g_res[i].t->id);
              if (ref > 0 && !strncmp (g_res[i].t->id, groups[g], strlen (groups[g])))
                {
                  double r = g_res[i].t->flags & T_LOWER_BETTER
                    ? ref / g_res[i].median : g_res[i].median / ref;
                  l += log (r), c++;
                }
            }
          if (c)
            {
              out ("  %-5s %6.2fx  (%d tests)\n", groups[g], exp (l / c), c);
              all_l += l, all_n += c;
            }
        }
      if (all_n)
        out ("  all   %6.2fx  (%d tests)\n", exp (all_l / all_n), all_n);
    }
  int failed = 0;
  for (int i = 0; i < g_nres; i++)
    failed += !g_res[i].ok;
  out ("\n%d tests, %d failed checks.\n", g_nres, failed);

  if (g_out)
    {
      fprintf (g_out, "\n# CSV (for -ref and spreadsheets)\nmachine,%s\n", shortname);
      for (int i = 0; i < g_nres; i++)
        fprintf (g_out, "result,%s,%s,%.4f,%.4f,%.2f,%s\n", g_res[i].t->id,
                 g_res[i].t->unit, g_res[i].median, g_res[i].best,
                 g_res[i].spread, g_res[i].ok ? "ok" : "FAILED");
      fclose (g_out);
#ifdef __riscos
      _swix (OS_File, _INR (0, 2), 18, outfile, 0xfff);   /* Text */
#endif
    }
  return failed ? 2 : 0;
}
