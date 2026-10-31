/* rol-cmd: runs Linux commands for RISC OS (the *Linux command, the
 * Linux updates application), on the RISC OS for Linux images.
 *
 * RISC OS can't start Linux programs itself (it is one Linux process), so
 * it asks this daemon, through files in /riscos/cmd (IXFS:$.riscos.cmd in
 * RISC OS):
 *   NAME.req   the command line (written to another name first, then
 *              renamed, so it's complete when seen). RISC OS may add a
 *              filetype suffix (",fff"): it's ignored.
 *   NAME.out   the command's output (stdout and stderr), as it comes
 *   NAME.rc    written when the command has ended: its exit status
 *              (128 + the signal if it was killed)
 *   NAME.kill  RISC OS asks for the command to be stopped (Escape)
 * (RISC OS may add a filetype suffix such as ",ffd" to any of its files.)
 *   NAME.tok   (after a password) a session token for the next commands
 *
 * The root password. Commands run as root, so each needs root's
 * password, or a token from one given in the last 5 minutes. The request
 * starts with header lines:
 *   @password <password>
 *   @token <token>
 *   @newpassword <password>   sets the password: the first time with no
 *                             other header, after that with @password
 * and the rest is the command (none: just the password change). Answers
 * in NAME.rc besides the command's own exit status:
 *   77  the password is wrong, or the token has run out: ask for it
 *   78  root has no password yet: choose one (@newpassword)
 *   79  too many wrong passwords: wait a minute
 * A request of just "@info" runs /usr/lib/riscos/rol-info (what the
 * Updates plug-in shows: versions installed and available) without one.
 * The password is Linux's own root password (in /etc/shadow, which RISC OS
 * can't read), so RISC OS can set and change the root password this way.
 * Forgotten: put a file called RESETPW on the card's FAT partition; init
 * then clears the root password at the next start (see init.c).
 *
 * The command runs as root, through /bin/sh -c, in its own session, with
 * DEBIAN_FRONTEND=noninteractive (apt asks nothing). RISC OS removes the
 * files when it has read them; leftovers older than a day are removed.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DIR_PATH "/riscos/cmd"
#define TOKEN_IDLE 300		/* seconds a token lasts unused */
#define NTOK 4
#define RC_AUTH 77
#define RC_NOPASS 78
#define RC_WAIT 79
#define MAXJOBS 8

static struct job { pid_t pid; char name[64]; } jobs[MAXJOBS];

static void
path (char *p, size_t n, const char *name, const char *ext)
{
  snprintf (p, n, DIR_PATH "/%s.%s", name, ext);
}

static void
write_file (const char *p, const char *text)
{
  char tmp[160];
  snprintf (tmp, sizeof tmp, "%s.tmp", p);
  unlink (tmp);
  int f = open (tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (f < 0)
    return;
  ssize_t w = write (f, text, strlen (text));
  (void) w;
  close (f);
  rename (tmp, p);
}

/* "NAME.EXT" or "NAME.EXT,xxx" (RISC OS's filetype suffix) -> NAME, else 0 */
static int
job_name (const char *f, const char *ext, char *name, size_t n)
{
  const char *e = strrchr (f, '.');
  size_t el = strlen (ext);
  if (!e || strncmp (e + 1, ext, el) != 0 || (e[1 + el] && e[1 + el] != ','))
    return 0;
  size_t l = (size_t) (e - f);
  if (l == 0 || l >= n || strchr (f, '/'))
    return 0;
  memcpy (name, f, l);
  name[l] = 0;
  return 1;
}

/* --- the root password and the tokens ------------------------------------ */

static void
to_hex (const unsigned char *b, int n, char *o)
{
  for (int i = 0; i < n; i++)
    sprintf (o + 2 * i, "%02x", b[i]);
}
static void
random_bytes (unsigned char *b, int n)
{
  int f = open ("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (f < 0 || read (f, b, (size_t) n) != n)
    for (int i = 0; i < n; i++)
      b[i] = (unsigned char) (rand () ^ time (NULL));
  if (f >= 0)
    close (f);
}

/* The password is Linux's own root password (/etc/shadow): checked with
   perl's crypt() (perl-base is always there; it uses libcrypt, so any
   hash Linux makes), set with chpasswd -e (the hash made with perl's
   crypt). Both get it on a pipe, never on a command line. */

/* root's hash from /etc/shadow into h; 0 if it has none (locked, "*") */
static int
root_hash (char *h, size_t n)
{
  FILE *f = fopen ("/etc/shadow", "re");
  char line[1024];
  int ok = 0;
  if (!f)
    return 0;
  while (fgets (line, sizeof line, f))
    if (!strncmp (line, "root:", 5))
      {
	char *p = line + 5, *e = strchr (p, ':');
	if (e)
	  *e = 0;
	if (*p && *p != '*' && *p != '!' && strlen (p) < n)
	  {
	    strcpy (h, p);
	    ok = 1;
	  }
	break;
      }
  fclose (f);
  memset (line, 0, sizeof line);
  return ok;
}
static int
have_password (void)
{
  char h[512];
  return root_hash (h, sizeof h);
}

/* runs argv with text on its stdin; its exit status (-1: couldn't) */
static int
feed (char *const argv[], const char *text)
{
  int pp[2];
  if (pipe2 (pp, O_CLOEXEC))
    return -1;
  pid_t p = fork ();
  if (p == 0)
    {
      dup2 (pp[0], 0);
      int null = open ("/dev/null", O_WRONLY);
      dup2 (null, 1);
      dup2 (null, 2);
      clearenv ();
      setenv ("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
      execv (argv[0], argv);
      _exit (127);
    }
  close (pp[0]);
  if (p < 0)
    {
      close (pp[1]);
      return -1;
    }
  ssize_t w = write (pp[1], text, strlen (text));
  (void) w;
  close (pp[1]);
  int st;
  while (waitpid (p, &st, 0) < 0 && errno == EINTR)
    ;
  return WIFEXITED (st) ? WEXITSTATUS (st) : -1;
}
static int
check_password (const char *pw)
{
  char h[512], t[1200];
  if (!root_hash (h, sizeof h) || strchr (pw, '\n'))
    return 0;
  snprintf (t, sizeof t, "%s\n%s\n", pw, h);
  char *argv[] = { "/usr/bin/perl", "-e",
    "$p=<STDIN>;chomp $p;$h=<STDIN>;chomp $h;exit(crypt($p,$h) eq $h ? 0 : 1)", NULL };
  int r = feed (argv, t);
  memset (t, 0, sizeof t);
  return r == 0;
}
static int
set_password (const char *pw)
{
  /* the hash made here (SHA-512, perl's crypt) and given to chpasswd -e,
     which then doesn't go through PAM: PAM's "password changed" note goes
     to syslog, and with no syslog daemon onto the screen (Linux's console,
     behind RISC OS, seen when RISC OS ends) */
  static const char sc[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  unsigned char b[16];
  char salt[17], t[600];
  if (strchr (pw, '\n'))
    return -1;
  random_bytes (b, 16);
  for (int i = 0; i < 16; i++)
    salt[i] = sc[b[i] & 63];
  salt[16] = 0;
  snprintf (t, sizeof t, "%s\n%s\n", pw, salt);
  char *argv[] = { "/usr/bin/perl", "-e",
    "$p=<STDIN>;chomp $p;$s=<STDIN>;chomp $s;$h=crypt($p,'$6$'.$s.'$');"
    "exit 1 unless defined $h && substr($h,0,3) eq '$6$';"
    "open(C,'|-','/usr/sbin/chpasswd','-e') or exit 1;print C \"root:$h\\n\";"
    "close C;exit($? >> 8)", NULL };
  int r = feed (argv, t);
  memset (t, 0, sizeof t);
  return r == 0 ? 0 : -1;
}

static struct { char t[40]; time_t used; } tokens[NTOK];
static time_t fail_until;
static int fails;

static int
token_ok (const char *t)
{
  time_t now = time (NULL);
  for (int i = 0; i < NTOK; i++)
    if (tokens[i].t[0] && now - tokens[i].used < TOKEN_IDLE && strlen (t) == 32)
      {
	unsigned d = 0;
	for (int j = 0; j < 32; j++)
	  d |= (unsigned) (tokens[i].t[j] ^ t[j]);
	if (!d)
	  {
	    tokens[i].used = now;
	    return 1;
	  }
      }
  return 0;
}
static const char *
new_token (void)
{
  int k = 0;
  for (int i = 1; i < NTOK; i++)
    if (tokens[i].used < tokens[k].used)
      k = i;
  unsigned char b[16];
  random_bytes (b, 16);
  to_hex (b, 16, tokens[k].t);
  tokens[k].used = time (NULL);
  return tokens[k].t;
}
static void
forget_tokens (void)
{
  memset (tokens, 0, sizeof tokens);
}

/* the request's header lines: 0 to run the command (rest), else an answer
   code; a new token goes to NAME.tok */
static int
authorise (char *req, char **rest, const char *name)
{
  char *pw = 0, *tok = 0, *npw = 0, *p = req;
  while (*p == '@')
    {
      char *e = strchr (p, '\n');
      if (e)
	*e = 0;
      if (!strncmp (p, "@password ", 10))
	pw = p + 10;
      else if (!strncmp (p, "@token ", 7))
	tok = p + 7;
      else if (!strncmp (p, "@newpassword ", 13))
	npw = p + 13;
      p = e ? e + 1 : p + strlen (p);
    }
  *rest = p;
  time_t now = time (NULL);
  int ok = 0;
  if (tok && token_ok (tok))
    ok = 1;
  else if (pw)
    {
      if (now < fail_until)
	return RC_WAIT;
      if (have_password () && check_password (pw))
	ok = 2, fails = 0;
      else
	{
	  sleep (1);		/* (slows guessing) */
	  if (++fails >= 5)
	    fail_until = now + 60, fails = 0;
	  return have_password () ? RC_AUTH : RC_NOPASS;
	}
    }
  if (npw)
    {
      /* the first password needs nothing; a new one, the old one */
      if ((have_password () && ok != 2) || strlen (npw) < 1 || set_password (npw))
	return have_password () ? RC_AUTH : RC_NOPASS;
      forget_tokens ();
      ok = 2;
    }
  if (!ok)
    return have_password () ? RC_AUTH : RC_NOPASS;
  if (ok == 2)
    {
      char tp[160];
      path (tp, sizeof tp, name, "tok");
      write_file (tp, new_token ());
    }
  return 0;
}

static void
start (const char *file, const char *name)
{
  int slot = -1;
  for (int i = 0; i < MAXJOBS; i++)
    if (!jobs[i].pid)
      {
	slot = i;
	break;
      }
  if (slot < 0)
    return;			/* (taken later, when one ends) */
  char rp[160], cmd[4096];
  snprintf (rp, sizeof rp, DIR_PATH "/%s", file);
  int f = open (rp, O_RDONLY | O_CLOEXEC);
  if (f < 0)
    return;
  ssize_t n = read (f, cmd, sizeof cmd - 1);
  close (f);
  unlink (rp);
  if (n < 0)
    n = 0;
  cmd[n] = 0;
  char *run;
  int arc;
  /* "@info" alone: rol-info (installed RISC OS on Linux packages, updates
     apt last found), which tells nothing secret: no password needed */
  static char info[] = "/usr/lib/riscos/rol-info";
  if (!strncmp (cmd, "@info", 5) && (cmd[5] == 0 || cmd[5] == '\n' || cmd[5] == '\r'))
    run = info, arc = 0;
  else
    arc = authorise (cmd, &run, name);
  if (run >= cmd && run < cmd + sizeof cmd)
    memset (cmd, 0, (size_t) (run - cmd));	/* (the password: gone) */
  if (arc || !*run)
    {
      char rcp[160], t[16];
      path (rcp, sizeof rcp, name, "rc");
      snprintf (t, sizeof t, "%d\n", arc);
      write_file (rcp, t);
      return;
    }
  n = (ssize_t) strlen (run);
  while (n > 0 && (run[n - 1] == '\n' || run[n - 1] == '\r'))
    run[--n] = 0;
  char op[160];
  path (op, sizeof op, name, "out");
  pid_t p = fork ();
  if (p == 0)
    {
      setsid ();
      int null = open ("/dev/null", O_RDONLY);
      unlink (op);
      int out = open (op, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_APPEND, 0644);
      dup2 (null, 0);
      dup2 (out, 1);
      dup2 (out, 2);
      if (null > 2)
	close (null);
      if (out > 2)
	close (out);
      clearenv ();
      setenv ("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
      setenv ("HOME", "/root", 1);
      setenv ("TERM", "dumb", 1);
      setenv ("LANG", "C.UTF-8", 1);
      setenv ("DEBIAN_FRONTEND", "noninteractive", 1);
      setenv ("APT_LISTCHANGES_FRONTEND", "none", 1);
      if (chdir ("/root") != 0 && chdir ("/") != 0)
	_exit (126);
      /* Line-buffered output (stdbuf, inherited by everything the command
	 runs: apt, dpkg, the maintainer scripts), so that the output file,
	 which Updates shows as it grows, fills a line at a time rather than
	 in 4KB blocks long after the work was done */
      if (access ("/usr/bin/stdbuf", X_OK) == 0)
	execl ("/usr/bin/stdbuf", "stdbuf", "-oL", "-eL", "/bin/sh", "-c", run, (char *) NULL);
      execl ("/bin/sh", "sh", "-c", run, (char *) NULL);
      _exit (127);
    }
  if (p < 0)
    {
      char rcp[160];
      path (rcp, sizeof rcp, name, "rc");
      write_file (rcp, "127\n");
      return;
    }
  jobs[slot].pid = p;
  snprintf (jobs[slot].name, sizeof jobs[slot].name, "%s", name);
}

static void
reap (void)
{
  int st;
  pid_t p;
  while ((p = waitpid (-1, &st, WNOHANG)) > 0)
    for (int i = 0; i < MAXJOBS; i++)
      if (jobs[i].pid == p)
	{
	  int rc = WIFEXITED (st) ? WEXITSTATUS (st) : 128 + WTERMSIG (st);
	  char rcp[160], t[16];
	  path (rcp, sizeof rcp, jobs[i].name, "rc");
	  snprintf (t, sizeof t, "%d\n", rc);
	  write_file (rcp, t);
	  jobs[i].pid = 0;
	}
}

static void
kill_job (const char *file, const char *name)
{
  char p[300];
  snprintf (p, sizeof p, DIR_PATH "/%s", file);
  unlink (p);
  for (int i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid && strcmp (jobs[i].name, name) == 0)
      kill (-jobs[i].pid, SIGTERM);
}

static void
sweep (void)
{
  DIR *d = opendir (DIR_PATH);
  if (!d)
    return;
  struct dirent *e;
  time_t now = time (NULL);
  while ((e = readdir (d)))
    {
      char name[64];
      if (e->d_name[0] == '.')
	continue;
      if (job_name (e->d_name, "req", name, sizeof name))
	{
	  start (e->d_name, name);
	  continue;
	}
      if (job_name (e->d_name, "kill", name, sizeof name))
	{
	  kill_job (e->d_name, name);
	  continue;
	}
      char p[300];
      struct stat s;
      snprintf (p, sizeof p, DIR_PATH "/%s", e->d_name);
      if (stat (p, &s) == 0 && S_ISREG (s.st_mode) && now - s.st_mtime > 86400)
	unlink (p);
    }
  closedir (d);
}

int
main (void)
{
  mkdir (DIR_PATH, 0755);
  signal (SIGPIPE, SIG_IGN);
  for (;;)
    {
      reap ();
      sweep ();
      struct timespec t = { 0, 100 * 1000 * 1000 };
      nanosleep (&t, NULL);
    }
}
