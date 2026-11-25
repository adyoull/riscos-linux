/* rol-print: RISC OS's printing through Linux's CUPS (riscos-print).

   RISC OS prints with its own printer drivers (!Printers: PostScript
   Level 2), and !Printers' "File" connection writes each job to a file.
   The ready-made printer, "Linux printer", writes to
   IXFS:$.riscos.print.Default; any other name there is a CUPS queue. This
   program:
     - starts what CUPS needs, as ROL's init isn't systemd: dbus (for
       avahi), avahi-daemon (finding network printers: IPP Everywhere,
       AirPrint, so most printers made since about 2012 need no driver),
       cupsd, and ipp-usb (USB printers that speak IPP over USB); each is
       restarted if it stops;
     - watches /riscos/print (inotify): each file RISC OS finishes writing
       is a job, given to CUPS (lp) for the queue it's named after
       ("Default": the plug-in's choice while CUPS has it, else CUPS's
       default printer, else the first one found),
       then removed;
     - keeps /riscos/print/Printers up to date (the printers CUPS can
       use, the ones added by hand, the default), and /riscos/print/Log
       (the last jobs), for RISC OS to read;
     - takes its settings and requests from Configure's Linux printing plug-in
       (!LinuxPrint): /riscos/print/config ("default=NAME": the printer
       "Linux printer" prints on) and /riscos/print/request (one line:
       "add NAME URI", "remove NAME", "test NAME", "find" (Found: the
       printers Linux can reach that CUPS doesn't use yet), "options NAME"
       (Options: the printer's settings and their choices, as CUPS has
       them), "set NAME KEY=VALUE ..." (its settings changed: its
       defaults for jobs from RISC OS) or "setup" (Linux printer put in
       !Printers' choices).
   In riscos-support (from 1.0.0086-1), started by ROL's init; with no
   CUPS (riscos-print not installed) it only watches, and says so in Log.
   What riscos-print installs is started within seconds of the install.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SPOOL "/riscos/print"
#define LP "/usr/bin/lp"
#define LPSTAT "/usr/bin/lpstat"
#define LPADMIN "/usr/sbin/lpadmin"
#define LPINFO "/usr/sbin/lpinfo"
#define LPOPTIONS "/usr/bin/lpoptions"
#define SETUP "/usr/lib/riscos/setup-linux-printer"
/* the printers added through the plug-in (CUPS's own temporary queues,
   for printers it found, look the same in lpstat -v) */
#define ADDED "/var/lib/riscos/print-added"
#define IPPTOOL "/usr/bin/ipptool"
#define SUPPLIES "/usr/share/riscos/print/supplies.test"

static int
was_added (const char *name)
{
  FILE *f = fopen (ADDED, "r");
  char l[300];
  int r = 0;
  while (f && fgets (l, sizeof l, f))
    {
      l[strcspn (l, "\n")] = 0;
      r |= !strcmp (l, name);
    }
  if (f)
    fclose (f);
  return r;
}

static void
note_added (const char *name, int add)
{
  char all[8192] = "", l[300];
  size_t n = 0;
  FILE *f = fopen (ADDED, "r");
  while (f && fgets (l, sizeof l, f))
    {
      l[strcspn (l, "\n")] = 0;
      if (*l && strcmp (l, name) && n + strlen (l) + 2 < sizeof all)
	n += sprintf (all + n, "%s\n", l);
    }
  if (f)
    fclose (f);
  if (add && n + strlen (name) + 2 < sizeof all)
    n += sprintf (all + n, "%s\n", name);
  mkdir ("/var/lib/riscos", 0755);
  f = fopen (ADDED, "w");
  if (f)
    {
      fputs (all, f);
      fclose (f);
    }
}

static void
kmsg (const char *fmt, ...)
{
  char m[512];
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (m, sizeof m, fmt, ap);
  va_end (ap);
  int fd = open ("/dev/kmsg", O_WRONLY | O_CLOEXEC);
  if (fd >= 0)
    {
      char l[600];
      int n = snprintf (l, sizeof l, "rol-print: %s\n", m);
      if (write (fd, l, n) < 0) { }
      close (fd);
    }
}

/* --- the services ----------------------------------------------------- */

struct service
{
  const char *name, *path;
  const char *argv[6];
  pid_t pid;
  time_t started;
};

static struct service services[] = {
  { "dbus", "/usr/bin/dbus-daemon", { "dbus-daemon", "--system", "--nofork", "--nopidfile", NULL }, 0, 0 },
  { "avahi", "/usr/sbin/avahi-daemon", { "avahi-daemon", "--no-drop-root", "--no-chroot", NULL }, 0, 0 },
  { "cups", "/usr/sbin/cupsd", { "cupsd", "-f", NULL }, 0, 0 },
  { "ipp-usb", "/usr/sbin/ipp-usb", { "ipp-usb", "standalone", NULL }, 0, 0 },
};
#define NSERVICES (int) (sizeof services / sizeof services[0])

static void
start (struct service *s)
{
  if (access (s->path, X_OK))
    return;
  time_t now = time (NULL);
  if (s->started && now - s->started < 10)
    return;			/* (restarted at most every 10 s) */
  s->started = now;
  pid_t p = fork ();
  if (p == 0)
    {
      int fd = open ("/dev/null", O_RDWR);
      dup2 (fd, 0);
      dup2 (fd, 1);
      dup2 (fd, 2);
      setsid ();
      execv (s->path, (char *const *) s->argv);
      _exit (127);
    }
  if (p > 0)
    {
      s->pid = p;
      kmsg ("%s started", s->name);
    }
}

static void
reap (void)
{
  int st;
  pid_t p;
  while ((p = waitpid (-1, &st, WNOHANG)) > 0)
    for (int i = 0; i < NSERVICES; i++)
      if (services[i].pid == p)
	{
	  services[i].pid = 0;
	  kmsg ("%s stopped (status %d): starting it again", services[i].name, st);
	}
}

static void
mkdirs (void)
{
  mkdir ("/run/dbus", 0755);
  mkdir ("/run/avahi-daemon", 0755);
  mkdir ("/run/cups", 0755);
  /* dbus needs a machine id */
  if (access ("/etc/machine-id", R_OK) && access ("/var/lib/dbus/machine-id", R_OK)
      && !access ("/usr/bin/dbus-uuidgen", X_OK))
    if (system ("/usr/bin/dbus-uuidgen --ensure >/dev/null 2>&1")) { }
}

/* --- running a command, its output kept --------------------------------- */

/* (set: a program's errors aren't part of what it says) */
static int run_quiet;

static int
run (char *out, size_t outsz, const char *path, char *const argv[])
{
  int p[2];
  if (pipe2 (p, O_CLOEXEC))
    return -1;
  pid_t c = fork ();
  if (c == 0)
    {
      dup2 (p[1], 1);
      if (run_quiet)
	{
	  int nul = open ("/dev/null", O_WRONLY);
	  if (nul >= 0)
	    dup2 (nul, 2);
	}
      else
	dup2 (p[1], 2);
      execv (path, argv);
      _exit (127);
    }
  close (p[1]);
  size_t n = 0;
  ssize_t r;
  while ((r = read (p[0], out + n, outsz - 1 - n)) > 0 && n + r < outsz - 1)
    n += r;
  out[n] = 0;
  close (p[0]);
  int st = 0;
  while (waitpid (c, &st, 0) < 0 && errno == EINTR)
    ;
  return WIFEXITED (st) ? WEXITSTATUS (st) : -1;
}

/* --- the printers -------------------------------------------------------- */

static char printers[4096], deflt[256], added[4096], chosen[256];

/* the plug-in's choice of printer (config: "default=NAME") */
static void
read_config (void)
{
  chosen[0] = 0;
  FILE *f = fopen (SPOOL "/config", "r");
  if (!f)
    return;
  char l[300];
  while (fgets (l, sizeof l, f))
    if (!strncmp (l, "default=", 8))
      {
	snprintf (chosen, sizeof chosen, "%s", l + 8);
	chosen[strcspn (chosen, "\r\n")] = 0;
      }
  fclose (f);
}

/* What CUPS can print to, for the plug-in's list: each printer's queue
   name, what it is (its model, or the address of one added by hand), its
   state and how it's reached (Network, USB, Added).
     - lpstat -e: every printer CUPS can use, the ones it has found by
       itself (DNS-SD through avahi: network printers, and USB ones
       through ipp-usb) and the ones added;
     - ippfind: what each found one is (its service name and "ty", the
       model), its host (localhost: ipp-usb, so USB) and its address
       (for its ink levels, and to keep the one chosen);
     - lpstat -v and the plug-in's own list: the ones added, and where;
     - lpstat -p: the state of each queue CUPS has made. */
#define MAXPR 16
static struct printer
{
  char name[128], info[160], state[16], kind[16], uri[256];
} prs[MAXPR];
static int nprs;

/* a DNS-SD service name as CUPS names its queue: letters, digits, - kept,
   anything else one _ */
static void
queue_name (const char *s, char *q, size_t qn)
{
  size_t n = 0;
  for (; *s && n + 1 < qn; s++)
    {
      int ok = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-';
      if (ok)
	q[n++] = *s;
      else if (n && q[n - 1] != '_')
	q[n++] = '_';
    }
  while (n && q[n - 1] == '_')
    n--;
  q[n] = 0;
}

static void
read_printers (void)
{
  static char o[16384];
  printers[0] = deflt[0] = added[0] = 0;
  nprs = 0;
  if (access (LPSTAT, X_OK))
    return;
  char *a1[] = { "lpstat", "-e", NULL };
  if (run (o, sizeof o, LPSTAT, a1) == 0)
    {
      size_t n = 0;
      for (char *l = strtok (o, "\n"); l; l = strtok (NULL, "\n"))
	if (*l && n + strlen (l) + 2 < sizeof printers)
	  {
	    n += sprintf (printers + n, "%s\n", l);
	    if (nprs < MAXPR)
	      {
		struct printer *p = &prs[nprs++];
		snprintf (p->name, sizeof p->name, "%s", l);
		snprintf (p->info, sizeof p->info, "%s", l);
		p->uri[0] = 0;
		strcpy (p->state, "Ready");
		strcpy (p->kind, "Network");
	      }
	  }
    }
  /* what the found ones are */
  if (!access ("/usr/bin/ippfind", X_OK))
    {
      char *a[] = { "ippfind", "-T", "2", "_ipp._tcp", "_ipps._tcp", "-x", "/bin/echo",
		    "{service_name}", "@@", "{txt_ty}", "@@", "{service_hostname}", "@@", "{service_uri}", ";", NULL };
      if (run (o, sizeof o, "/usr/bin/ippfind", a) == 0 || o[0])
	for (char *l = strtok (o, "\n"); l; l = strtok (NULL, "\n"))
	  {
	    char *f1 = strstr (l, " @@ "), *f2 = f1 ? strstr (f1 + 4, " @@ ") : NULL;
	    char *f3 = f2 ? strstr (f2 + 4, " @@ ") : NULL;
	    if (!f3)
	      continue;
	    *f1 = *f2 = *f3 = 0;
	    char q[128];
	    queue_name (l, q, sizeof q);
	    for (int i = 0; i < nprs; i++)
	      if (!strcmp (prs[i].name, q))
		{
		  const char *ty = f1 + 4;
		  snprintf (prs[i].info, sizeof prs[i].info, "%s", *ty ? ty : l);
		  if (!strncmp (f2 + 4, "localhost", 9))
		    strcpy (prs[i].kind, "USB");
		  /* (ipp:// rather than ipps://, when it has both: ipptool
		     would refuse a printer's own certificate) */
		  const char *u = f3 + 4;
		  if (!prs[i].uri[0] || (!strncmp (prs[i].uri, "ipps:", 5) && !strncmp (u, "ipp:", 4)))
		    snprintf (prs[i].uri, sizeof prs[i].uri, "%s", u);
		}
	  }
    }
  /* the ones added through the plug-in: "device for NAME: URI" */
  char *a0[] = { "lpstat", "-v", NULL };
  if (run (o, sizeof o, LPSTAT, a0) == 0)
    {
      size_t n = 0;
      for (char *l = strtok (o, "\n"); l; l = strtok (NULL, "\n"))
	if (!strncmp (l, "device for ", 11))
	  {
	    char *c = strstr (l, ": ");
	    char nm[256];
	    snprintf (nm, sizeof nm, "%.*s", c ? (int) (c - l - 11) : 0, l + 11);
	    for (int i = 0; c && i < nprs; i++)
	      if (!strcmp (prs[i].name, nm) && !prs[i].uri[0])
		snprintf (prs[i].uri, sizeof prs[i].uri, "%s", c + 2);
	    if (c && was_added (nm))
	      {
		if (n + strlen (l) < sizeof added - 2)
		  n += sprintf (added + n, "%s %s\n", nm, c + 2);
		for (int i = 0; i < nprs; i++)
		  if (!strcmp (prs[i].name, nm))
		    {
		      /* the host in its address */
		      const char *h = strstr (c + 2, "://");
		      h = h ? h + 3 : c + 2;
		      snprintf (prs[i].info, sizeof prs[i].info, "%s (%.*s)", nm, (int) strcspn (h, "/:"), h);
		      strcpy (prs[i].kind, "Added");
		    }
	      }
	  }
    }
  /* the state of each queue made: "printer NAME is idle." etc. */
  char *a3[] = { "lpstat", "-p", NULL };
  if (run (o, sizeof o, LPSTAT, a3) == 0)
    for (char *l = strtok (o, "\n"); l; l = strtok (NULL, "\n"))
      if (!strncmp (l, "printer ", 8))
	for (int i = 0; i < nprs; i++)
	  {
	    size_t k = strlen (prs[i].name);
	    if (!strncmp (l + 8, prs[i].name, k) && l[8 + k] == ' ')
	      strcpy (prs[i].state, strstr (l, "disabled") ? "Stopped"
		      : strstr (l, "now printing") ? "Printing" : "Ready");
	  }
  char *a2[] = { "lpstat", "-d", NULL };
  if (run (o, sizeof o, LPSTAT, a2) == 0)
    {
      char *c = strstr (o, ": ");
      if (c && strncmp (o, "no system default", 17))
	{
	  snprintf (deflt, sizeof deflt, "%s", c + 2);
	  deflt[strcspn (deflt, "\n")] = 0;
	}
    }
}

static uid_t ro_uid = 1000;
static gid_t ro_gid = 1000;

static void
write_file (const char *leaf, const char *text)
{
  char p[300], t[300];
  snprintf (p, sizeof p, SPOOL "/%s", leaf);
  snprintf (t, sizeof t, SPOOL "/.%s.new", leaf);
  int fd = open (t, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0)
    return;
  if (write (fd, text, strlen (text)) < 0) { }
  if (fchown (fd, ro_uid, ro_gid)) { }
  /* (on the disc before it's in place: a Pi can be switched off at once) */
  fsync (fd);
  close (fd);
  rename (t, p);
}

/* Printers: for RISC OS (and people) to read */
static void
write_status (void)
{
  static char last[8192];
  char s[8192];
  int n = snprintf (s, sizeof s,
		    "# The printers Linux's CUPS can print to, for RISC OS's \"Linux printer\"\n"
		    "# (!Printers' File connection, IXFS:$.riscos.print.Default). Printing to\n"
		    "# IXFS:$.riscos.print.NAME prints to the one called NAME.\n"
		    "cups=%s\ndefault=%s\nchosen=%s\n",
		    access (LP, X_OK) ? "missing" : !access ("/run/cups/cups.sock", F_OK) ? "running" : "starting",
		    deflt, chosen);
  /* printer=NAME<tab>WHAT<tab>STATE<tab>HOW */
  for (int i = 0; i < nprs && n < (int) sizeof s - 400; i++)
    n += snprintf (s + n, sizeof s - n, "printer=%s\t%s\t%s\t%s\n",
		   prs[i].name, prs[i].info, prs[i].state, prs[i].kind);
  for (char *l = added, *e; *l && n < (int) sizeof s - 300; l = e + 1)
    {
      e = strchr (l, '\n');
      if (!e)
	break;
      n += snprintf (s + n, sizeof s - n, "added=%.*s\n", (int) (e - l), l);
    }
  {
    struct stat cs;
    n += snprintf (s + n, sizeof s - n, "riscos=%s\n",
		   stat ("/riscos/HardDisc4/!Boot/Choices/Printers/Settings", &cs)
		   && stat ("/riscos/HardDisc4/!Boot/Choices/Printers/Settings,fc6", &cs) ? "none"
		   : system ("grep -qs '^sn: Linux$' '/riscos/HardDisc4/!Boot/Choices/Printers/Settings'"
			     " '/riscos/HardDisc4/!Boot/Choices/Printers/Settings,fc6'") == 0 ? "linux" : "other");
  }
  if (strcmp (s, last))
    {
      write_file ("Printers", s);
      strcpy (last, s);
    }
}

/* --- jobs ---------------------------------------------------------------- */

static char logtext[4096];

static void
log_job (const char *fmt, ...)
{
  char m[600], stamp[32];
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (m, sizeof m, fmt, ap);
  va_end (ap);
  time_t t = time (NULL);
  strftime (stamp, sizeof stamp, "%d %b %Y %H:%M:%S", localtime (&t));
  kmsg ("%s", m);
  /* the newest first; the last 20 or so kept */
  char n[sizeof logtext];
  snprintf (n, sizeof n, "%s %s\n%s", stamp, m, logtext);
  int lines = 0;
  for (char *c = n; *c; c++)
    if (*c == '\n' && ++lines == 20)
      {
	c[1] = 0;
	break;
      }
  strcpy (logtext, n);
  write_file ("Log", logtext);
}


/* --- a printer's settings, from the PPD CUPS made for it ----------------- */

/* "*en.Translation KEY/Label" and "*en.KEY CHOICE/Label" lines (English),
   else the PPD's own "/Label", else the keyword itself */
static void
ppd_label (const char *ppd, const char *key, const char *choice, char *out, size_t on)
{
  char l[512], want1[200], want2[200];
  snprintf (out, on, "%s", choice ? choice : key);
  FILE *f = fopen (ppd, "r");
  if (!f)
    return;
  if (choice)
    {
      snprintf (want1, sizeof want1, ".%s %s/", key, choice);
      snprintf (want2, sizeof want2, "*%s %s/", key, choice);
    }
  else
    {
      snprintf (want1, sizeof want1, ".Translation %s/", key);
      snprintf (want2, sizeof want2, "*OpenUI *%s/", key);
    }
  while (fgets (l, sizeof l, f))
    {
      char *w = NULL;
      int en = l[0] == '*' && l[1] == 'e' && l[2] == 'n' && (l[3] == '.' || l[3] == '_');
      if ((en && (w = strstr (l, want1)))
	  || (!strncmp (l, want2, strlen (want2)) && (w = l + strlen (want2) - 1)))
	{
	  char *lab = strchr (w, '/');
	  if (lab)
	    {
	      lab++;
	      size_t k = strcspn (lab, ":\"\n");
	      while (k && lab[k - 1] == ' ')
		k--;
	      if (k)
		snprintf (out, on, "%.*s", (int) k, lab);
	      if (!strncmp (l + 1, "en", 2))
		break;		/* (the English one is best) */
	    }
	}
    }
  fclose (f);
}

/* Options: "printer=NAME", then for each setting
   "option=KEY<tab>Label<tab>CURRENT<tab>CHOICE<tab>Label<tab>..." */
/* the values of attribute KEY in ipptool's report O ("KEY (type) = a,b"),
   into V (up to 16) */
static int
ipp_values (const char *o, const char *key, char v[][64])
{
  char k[80];
  snprintf (k, sizeof k, "%s (", key);
  const char *p = strstr (o, k);
  if (!p || !(p = strstr (p, " = ")))
    return 0;
  p += 3;
  int n = 0;
  while (n < 16 && *p && *p != '\n')
    {
      size_t l = strcspn (p, ",\n");
      snprintf (v[n++], 64, "%.*s", (int) l, p);
      p += l;
      if (*p == ',')
	p++;
    }
  return n;
}

/* the value of FIELD ("level=", ...) in one of printer-supply's entries */
static const char *
supply_field (const char *e, const char *field, char *v, size_t vn)
{
  const char *p = strstr (e, field);
  v[0] = 0;
  if (p)
    snprintf (v, vn, "%.*s", (int) strcspn (p + strlen (field), ";"), p + strlen (field));
  return v;
}

/* PWG's printer-supply, which printers give when they don't give marker-*:
   "index=1;class=...;type=toner;unit=percent;maxcapacity=100;level=75;
   colorantname=black;,index=2;..." with printer-supply-description's names */
static int
pwg_supplies (const char *o, char *s, size_t sn)
{
  const char *p = strstr (o, "printer-supply (");
  if (!p || !(p = strstr (p, " = ")))
    return snprintf (s, sn, "supplies=none\n");
  p += 3;
  char ds[16][64];
  int kd = ipp_values (o, "printer-supply-description", ds), n = 0, i = 0;
  while (*p && *p != '\n' && i < 16 && n < (int) sn - 200)
    {
      /* (an entry ends ";," or at the end of the line) */
      const char *e = strstr (p, ";,"), *nl = strchr (p, '\n');
      if (!e || (nl && e > nl))
	e = nl ? nl : p + strlen (p);
      char ent[512], lv[16], mx[16], col[32], ty[32], name[96];
      snprintf (ent, sizeof ent, "%.*s;", (int) (e - p), p);
      supply_field (ent, "level=", lv, sizeof lv);
      supply_field (ent, "maxcapacity=", mx, sizeof mx);
      supply_field (ent, "colorantname=", col, sizeof col);
      supply_field (ent, "type=", ty, sizeof ty);
      int l = lv[0] ? atoi (lv) : -1, m = atoi (mx);
      if (l >= 0 && m > 0)
	l = l * 100 / m;
      if (i < kd)
	snprintf (name, sizeof name, "%s", ds[i]);
      else
	snprintf (name, sizeof name, "%s %s", strcmp (col, "unknown") ? col : "", ty);
      const char *rgb = !strcmp (col, "black") ? "#000000" : !strcmp (col, "cyan") ? "#00FFFF"
	: !strcmp (col, "magenta") ? "#FF00FF" : !strcmp (col, "yellow") ? "#FFFF00" : "";
      n += snprintf (s + n, sn - n, "supply=%s\t%d\t%s\t\n", name, l, rgb);
      i++;
      p = *e == ';' ? e + 2 : e;
    }
  return n ? n : snprintf (s, sn, "supplies=none\n");
}

/* Printer NAME's ink, toner and other supplies, asked of the printer
   itself (IPP's marker-names, -levels, -colors and -low-levels), as
   "supply=NAME<tab>LEVEL<tab>COLOUR<tab>LOW" lines into S: LEVEL is 0-100,
   or -3 (some left), -2 or -1 (unknown); COLOUR #RRGGBB. Printers that
   aren't IPP, or don't say, give "supplies=none". The printer's address
   is ippfind's for a printer found on the network, else its queue's
   (lpstat -v); one CUPS reaches some other way is asked through CUPS. */
static int
supplies (const char *name, char *s, size_t sn)
{
  static char o[16384];
  char uri[300] = "";
  for (int i = 0; i < nprs; i++)
    if (!strcmp (prs[i].name, name))
      snprintf (uri, sizeof uri, "%s", prs[i].uri);
  if (strncmp (uri, "ipp://", 6) && strncmp (uri, "ipps://", 7))
    snprintf (uri, sizeof uri, "ipp://localhost:631/printers/%s", name);
  if (access (IPPTOOL, X_OK) || access (SUPPLIES, R_OK))
    return snprintf (s, sn, "supplies=none\n");
  char *a[] = { "ipptool", "-tv", "-T", "8", uri, SUPPLIES, NULL };
  run_quiet = 1;
  run (o, sizeof o, IPPTOOL, a);
  run_quiet = 0;
  char nm[16][64], lv[16][64], co[16][64], lo[16][64];
  int k = ipp_values (o, "marker-names", nm), kl = ipp_values (o, "marker-levels", lv);
  int kc = ipp_values (o, "marker-colors", co), kw = ipp_values (o, "marker-low-levels", lo);
  if (!k || kl < k)
    return pwg_supplies (o, s, sn);
  int n = 0;
  for (int i = 0; i < k && n < (int) sn - 200; i++)
    n += snprintf (s + n, sn - n, "supply=%s\t%s\t%s\t%s\n", nm[i], lv[i],
		   i < kc ? co[i] : "", i < kw ? lo[i] : "");
  return n;
}

/* The printer chosen in the plug-in (config "default=") kept as a queue of
   its own: CUPS makes a queue for a printer it found only while it's in
   use, and takes it away after a minute, so CUPS's own web pages lost it.
   Made with IPP Everywhere, at the address ippfind gave. */
static void
keep_chosen (void)
{
  read_config ();
  if (!chosen[0] || access (LPADMIN, X_OK))
    return;
  read_printers ();
  for (int i = 0; i < nprs; i++)
    if (!strcmp (prs[i].name, chosen) && strcmp (prs[i].kind, "Added")
	&& (!strncmp (prs[i].uri, "ipp://", 6) || !strncmp (prs[i].uri, "ipps://", 7)))
      {
	/* (a queue of its own is in printers.conf; one CUPS made for a while
	   isn't, though lpstat shows it while it's there) */
	static char o[4096];
	char want[300], line[400];
	int kept = 0;
	FILE *f = fopen ("/etc/cups/printers.conf", "r");
	snprintf (want, sizeof want, "Printer %s>", chosen);
	while (f && fgets (line, sizeof line, f))
	  if (line[0] == '<' && strstr (line, want))
	    kept = 1;
	if (f)
	  fclose (f);
	if (kept)
	  return;
	char *argv[] = { "lpadmin", "-p", chosen, "-E", "-v", prs[i].uri, "-m", "everywhere", NULL };
	int r = run (o, sizeof o, LPADMIN, argv);
	o[strcspn (o, "\n")] = 0;
	if (r == 0)
	  log_job ("kept %s as a printer of its own (%s)", prs[i].info, prs[i].uri);
	else
	  log_job ("couldn't keep %s: %s", prs[i].info, o[0] ? o : "lpadmin failed");
	sync ();
	return;
      }
}

static void
write_options (const char *name)
{
  static char o[16384], s[32768];
  char ppd[300];
  int n = snprintf (s, sizeof s, "printer=%s\n", name);
  /* (asking makes CUPS make the queue of a printer it found, and its PPD) */
  char *a[] = { "lpoptions", "-p", (char *) name, "-l", NULL };
  run_quiet = 1;
  int r = run (o, sizeof o, LPOPTIONS, a), found = 0;
  run_quiet = 0;
  if (r != 0)
    {
      /* (what went wrong is on its errors, not its output) */
      run (o, sizeof o, LPOPTIONS, a);
    }
  snprintf (ppd, sizeof ppd, "/etc/cups/ppd/%s.ppd", name);
  if (r != 0)
    {
      o[strcspn (o, "\n")] = 0;
      n += snprintf (s + n, sizeof s - n, "error=%s\n", o[0] ? o : "CUPS has no settings for it");
    }
  else
    for (char *l = strtok (o, "\n"); l && n < (int) sizeof s - 2000; l = strtok (NULL, "\n"))
      {
	/* "KEY/Label: c1 *c2 c3" */
	/* (only "KEY/Label: choices" lines) */
	char *colon = strstr (l, ": ");
	char *slash = strchr (l, '/');
	if (!colon || !slash || slash > colon)
	  continue;
	*colon = 0;
	found++;
	char key[100], lab[160];
	snprintf (key, sizeof key, "%.*s", slash ? (int) (slash - l) : (int) strlen (l), l);
	if (!strcmp (key, "PageRegion"))
	  continue;		/* (the same as PageSize) */
	ppd_label (ppd, key, NULL, lab, sizeof lab);
	if (!strcmp (lab, key) && slash)
	  snprintf (lab, sizeof lab, "%s", slash + 1);
	char cur[100] = "", list[8192] = "";
	size_t ln = 0;
	for (char *c = strtok_r (colon + 2, " ", &colon); c; c = strtok_r (NULL, " ", &colon))
	  {
	    if (*c == '*')
	      snprintf (cur, sizeof cur, "%s", ++c);
	    char cl[160];
	    ppd_label (ppd, key, c, cl, sizeof cl);
	    if (ln + strlen (c) + strlen (cl) + 3 < sizeof list)
	      ln += sprintf (list + ln, "\t%s\t%s", c, cl);
	  }
	n += snprintf (s + n, sizeof s - n, "option=%s\t%s\t%s%s\n", key, lab, cur, list);
      }
  if (r == 0 && !found)
    n += snprintf (s + n, sizeof s - n, "error=CUPS has no settings for it\n");
  n += supplies (name, s + n, sizeof s - n);
  write_file ("Options", s);
}

/* Found: the printers Linux can reach that CUPS doesn't use by itself
   (USB printers that aren't IPP, network ones that take PostScript or
   PCL on port 9100, LPD): "found=URI<tab>what it is" */
static void
write_found (void)
{
  static char o[16384], s[8192];
  int n = snprintf (s, sizeof s, "# found\n");
  char *a[] = { "lpinfo", "-l", "-v", "--timeout", "8", NULL };
  if (!access (LPINFO, X_OK) && run (o, sizeof o, LPINFO, a) == 0)
    {
      char uri[400] = "", info[200] = "";
      for (char *l = strtok (o, "\n"); ; l = strtok (NULL, "\n"))
	{
	  if (!l || !strncmp (l, "Device: uri = ", 14))
	    {
	      /* the one before: with an address, and not an IPP printer CUPS
		 already uses by itself */
	      if (uri[0] && strstr (uri, "://") && !strstr (uri, "._ipp") && !strstr (uri, "._ipps")
		  && strncmp (uri, "ipp", 3) && n < (int) sizeof s - 700)
		n += snprintf (s + n, sizeof s - n, "found=%s\t%s\n", uri, info[0] ? info : uri);
	      if (!l)
		break;
	      snprintf (uri, sizeof uri, "%s", l + 14);
	      info[0] = 0;
	    }
	  else if (strstr (l, "info = "))
	    snprintf (info, sizeof info, "%s", strstr (l, "info = ") + 7);
	}
    }
  write_file ("Found", s);
}

/* a name CUPS will take for a queue: letters, digits, - and _ */
static int
good_name (const char *n)
{
  if (!*n || strlen (n) > 127)
    return 0;
  for (const char *c = n; *c; c++)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')
	  || *c == '-' || *c == '_'))
      return 0;
  return 1;
}

static void
request (const char *leaf)
{
  char path[300], l[600] = "", out[1024] = "";
  snprintf (path, sizeof path, SPOOL "/%s", leaf);
  FILE *f = fopen (path, "r");
  if (f)
    {
      if (!fgets (l, sizeof l, f))
	l[0] = 0;
      fclose (f);
    }
  unlink (path);
  l[strcspn (l, "\r\n")] = 0;
  char *verb = strtok (l, " "), *name = strtok (NULL, " "), *uri = strtok (NULL, " ");
  int r = -1;
  if (!verb)
    return;
  if (!strcmp (verb, "setup"))
    {
      char *argv[] = { "setup-linux-printer", NULL };
      r = access (SETUP, X_OK) ? -1 : run (out, sizeof out, SETUP, argv);
      log_job ("%s", out[0] ? out : r == 0 ? "Linux printer set up in !Printers" : "Linux printer: riscos-print isn't installed");
    }
  else if (!strcmp (verb, "find"))
    {
      write_found ();
      return;
    }
  else if (!name || !good_name (name))
    log_job ("%s: give a printer name of letters, digits, - and _ only", verb);
  else if (access (LPADMIN, X_OK))
    log_job ("%s %s: CUPS isn't installed (riscos-print)", verb, name);
  else if (!strcmp (verb, "add") && uri)
    {
      /* a bare address (or host name) is taken as an IPP printer */
      char u[400];
      if (strstr (uri, "://"))
	snprintf (u, sizeof u, "%s", uri);
      else
	snprintf (u, sizeof u, "ipp://%s/ipp/print", uri);
      char *argv[] = { "lpadmin", "-p", name, "-E", "-v", u, "-m", "everywhere", NULL };
      r = run (out, sizeof out, LPADMIN, argv);
      if (r != 0 && strncmp (u, "ipp", 3))
	{
	  /* not an IPP printer (usb://, socket://, lpd://): CUPS's Generic
	     PostScript Printer, so its settings (paper size...) are there */
	  char *argv2[] = { "lpadmin", "-p", name, "-E", "-v", u, "-m", "drv:///sample.drv/generic.ppd", NULL };
	  r = run (out, sizeof out, LPADMIN, argv2);
	}
      out[strcspn (out, "\n")] = 0;
      if (r == 0)
	note_added (name, 1);
      log_job (r == 0 ? "added %s (%s)" : "couldn't add %s (%s): %s", name, u, out[0] ? out : "lpadmin failed");
    }
  else if (!strcmp (verb, "remove"))
    {
      char *argv[] = { "lpadmin", "-x", name, NULL };
      r = run (out, sizeof out, LPADMIN, argv);
      out[strcspn (out, "\n")] = 0;
      if (r == 0)
	note_added (name, 0);
      log_job (r == 0 ? "removed %s" : "couldn't remove %s: %s", name, out);
    }
  else if (!strcmp (verb, "options"))
    {
      write_options (name);
      return;
    }
  else if (!strcmp (verb, "set") && uri)
    {
      /* uri is the first KEY=VALUE; the rest follow */
      char *argv[64];
      int k = 0;
      argv[k++] = "lpoptions";
      argv[k++] = "-p";
      argv[k++] = name;
      for (char *kv = uri; kv && k < 60; kv = strtok (NULL, " "))
	if (strchr (kv, '='))
	  {
	    argv[k++] = "-o";
	    argv[k++] = kv;
	  }
      argv[k] = NULL;
      r = run (out, sizeof out, LPOPTIONS, argv);
      out[strcspn (out, "\n")] = 0;
      log_job (r == 0 ? "settings of %s changed" : "settings of %s not changed: %s", name, out);
      write_options (name);
    }
  else if (!strcmp (verb, "test"))
    {
      char *argv[] = { "lp", "-d", name, "-t", "Test page", "--", "/usr/share/cups/data/testprint", NULL };
      r = run (out, sizeof out, LP, argv);
      out[strcspn (out, "\n")] = 0;
      log_job (r == 0 ? "test page sent to %s: %s" : "test page not sent to %s: %s", name, out);
    }
  else
    log_job ("unknown request: %s", verb);
  /* (CUPS's files, as changed, on the disc) */
  sync ();
  read_printers ();
  read_config ();
  write_status ();
}

/* whether CUPS has a queue called NAME */
static int waiting;

static int
have_printer (const char *name)
{
  for (int i = 0; i < nprs; i++)
    if (!strcmp (prs[i].name, name))
      return 1;
  return 0;
}

/* the queue a job for QUEUE goes to ("" if none): "Default" is the
   plug-in's choice while CUPS has it, else CUPS's default, else the first */
static void
pick_dest (const char *queue, char *dest, size_t dn)
{
  dest[0] = 0;
  if (strcmp (queue, "Default"))
    snprintf (dest, dn, "%s", queue);
  else if (chosen[0] && have_printer (chosen))
    snprintf (dest, dn, "%s", chosen);
  else if (deflt[0])
    snprintf (dest, dn, "%s", deflt);
  else if (printers[0])
    snprintf (dest, dn, "%.*s", (int) strcspn (printers, "\n"), printers);
}

/* the job in WORK to CUPS's queue DEST, then removed */
static void
send (const char *work, const char *queue, const char *dest, long size)
{
  (void) queue;
  if (!dest[0])
    {
      log_job ("not printed (%ld bytes): Linux has found no printer", size);
      unlink (work);
      return;
    }
  if (access (LP, X_OK))
    {
      log_job ("not printed: CUPS isn't installed");
      unlink (work);
      return;
    }
  char out[1024];
  char *argv[] = { "lp", "-d", (char *) dest, "-t", "RISC OS", "--", (char *) work, NULL };
  int r = run (out, sizeof out, LP, argv);
  out[strcspn (out, "\n")] = 0;
  if (r == 0)
    log_job ("printed on %s (%ld bytes)", dest, size);
  else
    log_job ("not printed on %s (%ld bytes): %s", dest, size, out[0] ? out : "lp failed");
  unlink (work);
}

static void
job (const char *leaf)
{
  char queue[256], path[300], work[300];
  /* (its own files, and the plug-in's settings) */
  if (leaf[0] == '.' || !strcmp (leaf, "Printers") || !strcmp (leaf, "Log")
      || !strcmp (leaf, "Options") || !strcmp (leaf, "Found")
      || !strncmp (leaf, "config", 6))
    {
      if (!strcmp (leaf, "config"))
	keep_chosen ();
      return;
    }
  if (!strncmp (leaf, "request", 7))
    {
      request (leaf);
      return;
    }
  snprintf (queue, sizeof queue, "%s", leaf);
  /* a RISC OS filetype, ",xxx", isn't part of the name */
  size_t l = strlen (queue);
  if (l > 4 && queue[l - 4] == ',')
    queue[l - 4] = 0;
  snprintf (path, sizeof path, SPOOL "/%s", leaf);
  struct stat st;
  if (stat (path, &st) || !S_ISREG (st.st_mode))
    return;
  if (st.st_size == 0)
    {
      unlink (path);
      return;
    }
  /* out of RISC OS's way at once, so its next job can't write over it */
  static unsigned n;
  snprintf (work, sizeof work, SPOOL "/.job-%u-%ld", ++n, (long) time (NULL));
  if (rename (path, work))
    return;
  read_printers ();
  read_config ();
  write_status ();
  char dest[256];
  pick_dest (queue, dest, sizeof dest);
  if (!dest[0] && !access (LP, X_OK))
    {
      /* (CUPS and avahi just started, as at switching on: a network
	 printer takes some seconds to be found) */
      char wait[600];
      snprintf (wait, sizeof wait, SPOOL "/.wait-%ld-%u-%s", (long) time (NULL), n, queue);
      if (rename (work, wait) == 0)
	{
	  waiting = 1;
	  return;
	}
    }
  send (work, queue, dest, st.st_size);
}

/* jobs waiting for a printer (.wait-TIME-N-QUEUE): printed when there is
   one, given up after two minutes */
static void
retry_waiting (void)
{
  DIR *d = opendir (SPOOL);
  struct dirent *e;
  int read = 0, left = 0;
  while (d && (e = readdir (d)))
    {
      long t;
      unsigned k;
      int at = 0;
      if (sscanf (e->d_name, ".wait-%ld-%u-%n", &t, &k, &at) < 2 || !at)
	continue;
      if (!read)
	{
	  read_printers ();
	  read_config ();
	  read = 1;
	}
      char path[600], dest[256];
      struct stat st;
      snprintf (path, sizeof path, SPOOL "/%s", e->d_name);
      if (stat (path, &st))
	continue;
      pick_dest (e->d_name + at, dest, sizeof dest);
      if (dest[0] || time (NULL) - t > 120)
	send (path, e->d_name + at, dest, st.st_size);
      else
	left = 1;
    }
  if (d)
    closedir (d);
  if (read)
    write_status ();
  waiting = left;
}

/* --- main ---------------------------------------------------------------- */

int
main (void)
{
  signal (SIGPIPE, SIG_IGN);
  /* RISC OS's own user owns the folder, so it can write its jobs there */
  struct stat hd;
  if (stat ("/riscos/HardDisc4", &hd) == 0)
    {
      ro_uid = hd.st_uid;
      ro_gid = hd.st_gid;
    }
  mkdir (SPOOL, 0775);
  if (chown (SPOOL, ro_uid, ro_gid)) { }
  chmod (SPOOL, 0775);
  /* jobs left from before (a restart while printing) */
  DIR *d = opendir (SPOOL);
  struct dirent *e;
  while (d && (e = readdir (d)))
    if (!strncmp (e->d_name, ".job-", 5) || !strncmp (e->d_name, ".wait-", 6))
      {
	char p[300];
	snprintf (p, sizeof p, SPOOL "/%s", e->d_name);
	unlink (p);
      }
  if (d)
    closedir (d);

  mkdirs ();
  for (int i = 0; i < NSERVICES; i++)
    {
      start (&services[i]);
      if (i == 0)
	sleep (1);		/* (dbus first, for avahi) */
    }

  int in = inotify_init1 (IN_CLOEXEC | IN_NONBLOCK);
  if (in < 0 || inotify_add_watch (in, SPOOL, IN_CLOSE_WRITE | IN_MOVED_TO) < 0)
    {
      kmsg ("can't watch " SPOOL ": %s", strerror (errno));
      return 1;
    }
  /* any job already there */
  d = opendir (SPOOL);
  while (d && (e = readdir (d)))
    if (e->d_type == DT_REG)
      job (e->d_name);
  if (d)
    closedir (d);

  time_t last_status = 0;
  for (;;)
    {
      struct pollfd p = { in, POLLIN, 0 };
      poll (&p, 1, 5000);
      char buf[4096] __attribute__ ((aligned (8)));
      ssize_t n;
      while ((n = read (in, buf, sizeof buf)) > 0)
	for (char *c = buf; c < buf + n;)
	  {
	    struct inotify_event *ev = (struct inotify_event *) c;
	    if (ev->len)
	      job (ev->name);
	    c += sizeof *ev + ev->len;
	  }
      reap ();
      if (waiting)
	retry_waiting ();
      /* (riscos-print installed while this runs: its services start now) */
      int want = 0;
      for (int i = 0; i < NSERVICES; i++)
	want |= !services[i].pid && !access (services[i].path, X_OK);
      if (want)
	mkdirs ();
      for (int i = 0; i < NSERVICES; i++)
	if (!services[i].pid)
	  start (&services[i]);
      time_t now = time (NULL);
      if (now - last_status >= 30)
	{
	  last_status = now;
	  read_printers ();
	  read_config ();
	  write_status ();
	}
    }
}
