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
 *   NAME.in    (terminal jobs, below) what the user types, appended to
 *
 * A terminal job. A request whose first line is "@tty COLUMNS ROWS" gets a
 * terminal (a pty) of that size instead of no input and a plain file:
 * programs that ask questions, read passwords or edit a line work, and
 * Ctrl-C reaches them. What RISC OS appends to NAME.in is typed into the
 * terminal; what the terminal shows is appended to NAME.out. With no
 * command, the job is an interactive shell (bash, else sh). Who runs it
 * is as for any other request: root after the password, or RISC OS's own
 * user with "@user" (the "@tty" line comes before those). Requests
 * without "@tty" are run exactly as before (!Updates reads apt's output
 * as a file's). NAME.in is only read if it is a plain file of RISC OS's
 * own user (never through a link).
 *
 * For a terminal program (RiSH), three more things. "@tty COLUMNS ROWS
 * NAME" sets TERM to NAME (letters, digits, - and + only; without it TERM
 * is "dumb", as LinuxShell wants). NAME.size, holding "COLUMNS ROWS",
 * changes the terminal's size (the program on it is told, SIGWINCH).
 * NAME.trim, holding a decimal count, says RISC OS has read that much of
 * NAME.out: the disc space under it is given back (the file keeps its
 * length, so the places RISC OS reads from don't move), so that a session
 * left open for days doesn't fill the disc. Both are only obeyed as plain
 * files of RISC OS's own user, for terminal jobs, and are removed.
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
 * A request starting "@user" runs its command as RISC OS's own Linux user
 * (riscos, uid 1000; *LinuxUser, and protected RISC OS's *IXRun): that
 * can't change the system, so it needs no password. It can't gain
 * privileges (no_new_privs: setuid programs don't), starts in HardDisc4
 * and has HOME /riscos. Only RISC OS (and root) can write /riscos/cmd.
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <grp.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <termios.h>

#define DIR_PATH "/riscos/cmd"
#define TOKEN_IDLE 300		/* seconds a token lasts unused */
#define NTOK 4
#define RC_AUTH 77
#define RC_NOPASS 78
#define RC_WAIT 79
#define RO_UID 1000		/* RISC OS's own Linux user (init: protected RISC OS) */
#define RO_GID 1000
#define MAXJOBS 16

/* master: a terminal job's pty (-1: none), out: its output file, inpos:
   how much of NAME.in has been typed into it */
static struct job { pid_t pid; char name[64]; int master, out; off_t inpos; } jobs[MAXJOBS];

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

/* A line in /var/log/rol-cmd.log (root's alone) about a password set or
   refused: never the password. From the reports of a password set at the
   first use of Updates and refused later: this says what arrived. */
static void
pw_log (const char *fmt, ...)
{
  struct stat st;
  int fd = open ("/var/log/rol-cmd.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (fd < 0)
    return;
  if (fstat (fd, &st) == 0 && st.st_size > 65536 && ftruncate (fd, 0)) { }
  char line[300];
  time_t now = time (NULL);
  struct tm tm;
  gmtime_r (&now, &tm);
  size_t n = strftime (line, 40, "%Y-%m-%d %H:%M:%S UTC ", &tm);
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (line + n, sizeof line - n - 1, fmt, ap);
  va_end (ap);
  strcat (line, "\n");
  if (write (fd, line, strlen (line)) < 0) { }
  close (fd);
}

static int
pw_top (const char *pw)
{
  int n = 0;
  for (; *pw; pw++)
    n += (unsigned char) *pw >= 128;
  return n;
}

/* a refused password: its length, and whether it would have been right
   with the letters' case the other way (typed with Caps Lock on, or set
   with it on) */
static void
pw_refused (const char *pw)
{
  char alt[200];
  size_t n = strlen (pw);
  int letters = 0;
  const char *swapped = "no letters";
  if (n < sizeof alt)
    {
      for (size_t i = 0; i <= n; i++)
	{
	  char c = pw[i];
	  if (c >= 'a' && c <= 'z')
	    c -= 32, letters = 1;
	  else if (c >= 'A' && c <= 'Z')
	    c += 32, letters = 1;
	  alt[i] = c;
	}
      if (letters)
	swapped = check_password (alt) ? "YES" : "no";
      memset (alt, 0, sizeof alt);
    }
  pw_log ("password refused: %d characters, %d of them not ASCII; right with the letters' case swapped: %s",
	  (int) n, pw_top (pw), swapped);
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
	  if (have_password ())
	    pw_refused (pw);
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
      pw_log ("password set: %d characters, %d of them not ASCII", (int) strlen (npw), pw_top (npw));
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

static char term_name[32];	/* "@tty"'s TERM, if it gave one */

/* in the child, on its terminal: the command, or with none a shell */
static void
run_on_tty (const char *run)
{
  if (term_name[0])
    {
      /* a real terminal: programs may ask their questions on it */
      setenv ("TERM", term_name, 1);
      unsetenv ("DEBIAN_FRONTEND");
      unsetenv ("APT_LISTCHANGES_FRONTEND");
    }
  if (*run)
    execl ("/bin/sh", "sh", "-c", run, (char *) NULL);
  else
    {
      if (access ("/bin/bash", X_OK) == 0)
	execl ("/bin/bash", "bash", "-i", (char *) NULL);
      execl ("/bin/sh", "sh", "-i", (char *) NULL);
    }
  _exit (127);
}

/* a terminal job's traffic: what its terminal shows goes on the end of
   NAME.out, and what RISC OS has added to NAME.in is typed into it */
static void
pump (struct job *j)
{
  char buf[4096];
  ssize_t n;
  if (j->master < 0)
    return;
  while ((n = read (j->master, buf, sizeof buf)) > 0)
    if (j->out >= 0)
      {
	ssize_t w = write (j->out, buf, (size_t) n);
	(void) w;
      }
  char ip[160];
  path (ip, sizeof ip, j->name, "in");
  int f = open (ip, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (f < 0)
    return;
  struct stat st;
  if (fstat (f, &st) == 0 && S_ISREG (st.st_mode) && st.st_uid == RO_UID && st.st_size > j->inpos)
    {
      n = pread (f, buf, sizeof buf, j->inpos);
      if (n > 0)
	{
	  ssize_t w = write (j->master, buf, (size_t) n);
	  if (w > 0)
	    j->inpos += w;	/* (the rest next time, if the terminal was full) */
	}
    }
  close (f);
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
  /* "@tty COLUMNS ROWS" first: a terminal job */
  char *req = cmd;
  int tty = 0;
  unsigned cols = 80, rows = 25;
  if (!strncmp (req, "@tty", 4) && (req[4] == ' ' || req[4] == '\n' || req[4] == '\r'))
    {
      tty = 1;
      term_name[0] = 0;
      sscanf (req + 4, "%u %u %31[A-Za-z0-9+-]", &cols, &rows, term_name);
      if (cols < 20 || cols > 500)
	cols = 80;
      if (rows < 5 || rows > 300)
	rows = 25;
      char *nl = strchr (req, '\n');
      req = nl ? nl + 1 : req + strlen (req);
    }
  char *run;
  int arc, asuser = 0;
  /* "@info" alone: rol-info (installed RISC OS on Linux packages, updates
     apt last found), which tells nothing secret: no password needed */
  static char info[] = "/usr/lib/riscos/rol-info";
  if (!strncmp (req, "@info", 5) && (req[5] == 0 || req[5] == '\n' || req[5] == '\r'))
    run = info, arc = 0;
  else if (!strncmp (req, "@user", 5) && (req[5] == '\n' || req[5] == '\r'))
    {
      /* as RISC OS's own user: no password */
      run = req + 6;
      while (*run == '\n' || *run == '\r')
	run++;
      arc = 0, asuser = 1;
    }
  else
    arc = authorise (req, &run, name);
  if (run >= cmd && run < cmd + sizeof cmd)
    memset (cmd, 0, (size_t) (run - cmd));	/* (the password: gone) */
  n = (ssize_t) strlen (run);
  while (n > 0 && (run[n - 1] == '\n' || run[n - 1] == '\r'))
    run[--n] = 0;
  if (arc || (!*run && !tty))
    {
      char rcp[160], t[16];
      path (rcp, sizeof rcp, name, "rc");
      snprintf (t, sizeof t, "%d\n", arc);
      write_file (rcp, t);
      return;
    }
  char op[160];
  path (op, sizeof op, name, "out");
  /* a terminal job: the pty, and the output file this daemon fills from it */
  int master = -1, tout = -1;
  char slave[64] = "";
  if (tty)
    {
      master = posix_openpt (O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
      if (master < 0 || grantpt (master) || unlockpt (master) || ptsname_r (master, slave, sizeof slave))
	{
	  if (master >= 0)
	    close (master);
	  master = -1, tty = 0;	/* (no ptys: as a plain job) */
	}
      else
	{
	  struct winsize ws = { (unsigned short) rows, (unsigned short) cols, 0, 0 };
	  ioctl (master, TIOCSWINSZ, &ws);
	  unlink (op);
	  tout = open (op, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_APPEND | O_CLOEXEC, 0644);
	}
      if (!tty && !*run)
	{
	  char rcp[160];
	  path (rcp, sizeof rcp, name, "rc");
	  write_file (rcp, "127\n");
	  return;
	}
    }
  pid_t p = fork ();
  if (p == 0)
    {
      setsid ();
      if (tty)
	{
	  /* signals as a terminal's programs expect them (this daemon ignores
	     SIGPIPE, and may itself have been started with others ignored) */
	  signal (SIGINT, SIG_DFL);
	  signal (SIGQUIT, SIG_DFL);
	  signal (SIGPIPE, SIG_DFL);
	  signal (SIGHUP, SIG_DFL);
	  signal (SIGTSTP, SIG_DFL);
	  /* the pty's other end: this session's terminal */
	  int t = open (slave, O_RDWR);
	  if (t < 0)
	    _exit (126);
	  ioctl (t, TIOCSCTTY, 0);
	  if (asuser && fchown (t, RO_UID, (gid_t) -1) != 0)
	    { /* (programs that reopen their terminal by name can't: not fatal) */ }
	  dup2 (t, 0);
	  dup2 (t, 1);
	  dup2 (t, 2);
	  if (t > 2)
	    close (t);
	}
      else
	{
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
	}
      clearenv ();
      if (asuser)
	{
	  gid_t g = RO_GID;
	  if (setgroups (1, &g) || setgid (RO_GID) || setuid (RO_UID) || setuid (0) == 0)
	    _exit (126);
	  prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	  setenv ("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
	  setenv ("HOME", "/riscos", 1);
	  setenv ("TERM", "dumb", 1);
	  setenv ("LANG", "C.UTF-8", 1);
	  if (chdir ("/riscos/HardDisc4") != 0 && chdir ("/riscos") != 0 && chdir ("/") != 0)
	    _exit (126);
	  if (tty)
	    run_on_tty (run);
	  if (access ("/usr/bin/stdbuf", X_OK) == 0)
	    execl ("/usr/bin/stdbuf", "stdbuf", "-oL", "-eL", "/bin/sh", "-c", run, (char *) NULL);
	  execl ("/bin/sh", "sh", "-c", run, (char *) NULL);
	  _exit (127);
	}
      setenv ("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
      setenv ("HOME", "/root", 1);
      setenv ("TERM", "dumb", 1);
      setenv ("LANG", "C.UTF-8", 1);
      setenv ("DEBIAN_FRONTEND", "noninteractive", 1);
      setenv ("APT_LISTCHANGES_FRONTEND", "none", 1);
      if (chdir ("/root") != 0 && chdir ("/") != 0)
	_exit (126);
      if (tty)
	run_on_tty (run);
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
      if (master >= 0)
	close (master);
      if (tout >= 0)
	close (tout);
      return;
    }
  jobs[slot].pid = p;
  jobs[slot].master = master;
  jobs[slot].out = tout;
  jobs[slot].inpos = 0;
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
	  if (jobs[i].master >= 0)
	    {
	      /* the last of what it showed, before RISC OS is told it ended */
	      pump (&jobs[i]);
	      close (jobs[i].master);
	      if (jobs[i].out >= 0)
		close (jobs[i].out);
	      jobs[i].master = jobs[i].out = -1;
	      char ip[160];
	      path (ip, sizeof ip, jobs[i].name, "in");
	      unlink (ip);
	    }
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
      {
	if (jobs[i].master >= 0)
	  {
	    /* a terminal job: an interactive shell ignores SIGTERM, and
	       what it is running is in a process group of its own. Hang
	       up on the terminal's foreground program, then end both. */
	    pid_t fg = tcgetpgrp (jobs[i].master);
	    if (fg > 0 && fg != jobs[i].pid)
	      {
		kill (-fg, SIGHUP);
		kill (-fg, SIGKILL);
	      }
	    kill (-jobs[i].pid, SIGHUP);
	    kill (-jobs[i].pid, SIGKILL);
	  }
	else
	  kill (-jobs[i].pid, SIGTERM);
      }
}

/* NAME.size ("COLUMNS ROWS") and NAME.trim ("BYTES"): see the top */
static void
tty_request (const char *file, const char *name, int trim)
{
  char p[300], t[64];
  snprintf (p, sizeof p, DIR_PATH "/%s", file);
  int f = open (p, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  ssize_t n = -1;
  struct stat st;
  if (f >= 0)
    {
      if (fstat (f, &st) == 0 && S_ISREG (st.st_mode) && st.st_uid == RO_UID)
	n = read (f, t, sizeof t - 1);
      close (f);
    }
  unlink (p);
  if (n <= 0)
    return;
  t[n] = 0;
  for (int i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid && jobs[i].master >= 0 && strcmp (jobs[i].name, name) == 0)
      {
	if (trim)
	  {
	    long long to = atoll (t);
	    off_t len = jobs[i].out >= 0 ? lseek (jobs[i].out, 0, SEEK_END) : 0;
	    if (to > len)
	      to = len;
	    to &= ~4095LL;
	    if (to > 0)
	      fallocate (jobs[i].out, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, to);
	  }
	else
	  {
	    unsigned cols = 0, rows = 0;
	    if (sscanf (t, "%u %u", &cols, &rows) == 2 && cols >= 20 && cols <= 500 && rows >= 5 && rows <= 300)
	      {
		struct winsize ws = { (unsigned short) rows, (unsigned short) cols, 0, 0 };
		ioctl (jobs[i].master, TIOCSWINSZ, &ws);
	      }
	  }
      }
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
      if (job_name (e->d_name, "size", name, sizeof name))
	{
	  tty_request (e->d_name, name, 0);
	  continue;
	}
      if (job_name (e->d_name, "trim", name, sizeof name))
	{
	  tty_request (e->d_name, name, 1);
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
  for (int i = 0; i < MAXJOBS; i++)
    jobs[i].master = jobs[i].out = -1;
  /* requests are looked for every 100 ms; a terminal job's typing and
     output are carried every 10 ms while there is one */
  for (unsigned tick = 0;; tick++)
    {
      int ttys = 0;
      for (int i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid && jobs[i].master >= 0)
	  {
	    pump (&jobs[i]);
	    ttys++;
	  }
      reap ();
      if (!ttys || tick % 10 == 0)
	sweep ();
      struct timespec t = { 0, (ttys ? 10 : 100) * 1000 * 1000 };
      nanosleep (&t, NULL);
    }
}
