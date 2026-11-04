/* LinuxShell: *Linux <command> runs a Linux command and prints its output,
   on RISC OS for Linux images.

   RISC OS is one Linux process and can't start Linux programs itself, so
   the image's rol-cmd daemon runs them: this module hands it the command
   as a file in /riscos/cmd (NAME.req), then prints NAME.out as it grows
   until NAME.rc says the command has ended. Escape writes NAME.kill. In a
   task window it gives the desktop time between looks (OS_UpCall 6).
   The files are opened through IXSupport_LinuxSyscall (Linux's own
   paths), which works natively and under QEMU alike.

   rol-cmd runs commands as root, so it wants Linux's root password
   (rc 77), or a token it gave for the last one (NAME.tok, good for five
   idle minutes, kept here in memory). With no password set yet (rc 78)
   this asks for a new one first. *LinuxPassword changes it (from RISC OS: the root password itself, in /etc/shadow).

   *LinuxUser runs the command as RISC OS's own Linux user (riscos, uid
   1000: rol-cmd's "@user" request) instead: it can't change the system,
   so no password. Protected RISC OS's *IXRun is an alias for it (TestBed
   start-up): RISC OS's own view of the files has no Linux programs.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "cmodule.h"
#include "../LinuxCodec/rolsys.h"

#define NR_read		3
#define NR_write	4
#define NR_unlink	10
#define NR_access	33
#define NR_rename	38
#define NR_nanosleep	162
#define O_WRONLY	1
#define O_CREAT		0100
#define O_TRUNC		01000
#define OS_ReadMonotonicTime	0x42
#define OS_UpCall	0x33
#define OS_Byte		0x06
#define OS_WriteC_	0x00
#define OS_ReadC	0x04

#define DIR	"/riscos/cmd/"

int escape_state (void);

ERROR_BLOCK (no_linux, 0x81A70, "*Linux needs RISC OS for Linux");
ERROR_BLOCK (no_daemon, 0x81A71, "*Linux needs the image's rol-cmd (Linux didn't take the command)");
ERROR_BLOCK (too_long, 0x81A72, "*Linux: the command is too long");
ERROR_BLOCK (failed, 0x81A73, "The Linux command failed");
ERROR_BLOCK (stopped, 0x11, "Escape");
ERROR_BLOCK (bad_pw, 0x81A74, "Wrong password");
ERROR_BLOCK (wait_pw, 0x81A75, "Too many wrong passwords: try again in a minute");
ERROR_BLOCK (mismatch, 0x81A76, "The two new passwords are different");
ERROR_BLOCK (empty_pw, 0x81A77, "The password can't be empty");

#define RC_AUTH 77
#define RC_NOPASS 78
#define RC_WAIT 79

static char token[40];		/* rol-cmd's last token for us ("": none) */

static unsigned counter;

static int
strcmp_ (const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return *a != *b;
}

static void
cat (char *d, const char *a, const char *b, const char *c)
{
  while (*a) *d++ = *a++;
  while (*b) *d++ = *b++;
  while (*c) *d++ = *c++;
  *d = 0;
}

static void
hex (char *d, unsigned v)
{
  for (int i = 7; i >= 0; i--)
    {
      unsigned n = (v >> (i * 4)) & 15;
      *d++ = (char) (n < 10 ? '0' + n : 'a' + n - 10);
    }
  *d = 0;
}

static unsigned
now_cs (void)
{
  regs r = {{ 0 }};
  swix_regs (OS_ReadMonotonicTime, &r);
  return r.r[0];
}

static void
nap (void)
{
  static unsigned word = 1;	/* non-zero: TaskWindow resumes us at its next poll */
  regs r = {{ 6, (unsigned) &word }};
  swix_regs (OS_UpCall, &r);
  unsigned ts[2] = { 0, 20 * 1000 * 1000 };
  lsys (NR_nanosleep, (unsigned) ts, 0, 0, 0, 0, 0);
}

static void
put (const unsigned char *s, int n)
{
  for (int i = 0; i < n; i++)
    {
      unsigned c = s[i];
      regs r = {{ c }};
      if (c == '\n')
	{
	  regs a = {{ 10 }}, b = {{ 13 }};
	  swix_regs (OS_WriteC_, &a);
	  swix_regs (OS_WriteC_, &b);
	}
      else if (c == 27)
	continue;		/* (no escape sequences: TERM=dumb asks for none) */
      else
	swix_regs (OS_WriteC_, &r);
    }
}

/* prints what's new in the output file from *pos on; 0 if none */
static int
drain (const char *out, unsigned *pos)
{
  int f = lsys (NR_open, (unsigned) out, 0, 0, 0, 0, 0);
  if (f < 0)
    return 0;
  int any = 0;
  if (lsys (19 /* lseek */, (unsigned) f, *pos, 0, 0, 0, 0) >= 0)
    {
      unsigned char buf[512];
      int n;
      while ((n = lsys (NR_read, (unsigned) f, (unsigned) buf, sizeof buf, 0, 0, 0)) > 0)
	{
	  put (buf, n);
	  *pos += (unsigned) n;
	  any = 1;
	}
    }
  lclose (f);
  return any;
}

static int
read_rc (const char *rc)
{
  char b[16];
  int f = lsys (NR_open, (unsigned) rc, 0, 0, 0, 0, 0);
  if (f < 0)
    return -1;
  int n = lsys (NR_read, (unsigned) f, (unsigned) b, sizeof b - 1, 0, 0, 0);
  lclose (f);
  if (n <= 0)
    return -1;
  int v = 0;
  for (int i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++)
    v = v * 10 + (b[i] - '0');
  return v;
}

/* reads a line with no echo into b (n bytes): 0, or Escape */
static oserror *
read_password (const char *prompt, char *b, int n)
{
  write0 (prompt);
  int l = 0;
  for (;;)
    {
      regs r = {{ 0 }};
      swix_regs (OS_ReadC, &r);
      unsigned c = r.r[0];
      if (c == 27 || escape_state ())
	{
	  regs a = {{ 126 }};
	  swix_regs (OS_Byte, &a);
	  newline ();
	  memset (b, 0, (size_t) n);
	  return ERR (stopped);
	}
      if (c == 13 || c == 10)
	break;
      if (c == 8 || c == 127)
	{
	  if (l)
	    l--;
	  continue;
	}
      if (c >= 32 && l < n - 1)
	b[l++] = (char) c;
    }
  b[l] = 0;
  newline ();
  return 0;
}

static void
add (char *d, int *n, int max, const char *s)
{
  while (*s && *n < max - 1)
    d[(*n)++] = *s++;
  d[*n] = 0;
}

/* hands rol-cmd header + cmd and prints the output; *code = its rc */
static oserror *
run_job (const char *header, const char *cmd, int *code)
{
  char body[1400];
  int n = 0;
  add (body, &n, sizeof body, header);
  add (body, &n, sizeof body, cmd);

  char name[24] = "j", h[12], tmp[64], req[64], out[64], rc[64], kill[64], tok[64];
  hex (h, now_cs ());
  cat (name + 1, h, "", "");
  hex (h, ++counter);
  cat (name + 9, h + 4, "", "");
  cat (tmp, DIR, name, ".new");
  cat (req, DIR, name, ".req");
  cat (out, DIR, name, ".out");
  cat (rc, DIR, name, ".rc");
  cat (kill, DIR, name, ".kill");
  cat (tok, DIR, name, ".tok");

  int f = lsys (NR_open, (unsigned) tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600, 0, 0, 0);
  if (f < 0)
    return ERR (no_daemon);
  lsys (NR_write, (unsigned) f, (unsigned) body, (unsigned) n, 0, 0, 0);
  lclose (f);
  memset (body, 0, sizeof body);
  lsys (NR_rename, (unsigned) tmp, (unsigned) req, 0, 0, 0, 0);

  unsigned pos = 0, t0 = now_cs ();
  int escaped = 0;
  for (;;)
    {
      drain (out, &pos);
      if ((*code = read_rc (rc)) >= 0)
	break;
      if (!escaped && escape_state ())
	{
	  regs a = {{ 126 }};
	  swix_regs (OS_Byte, &a);	/* acknowledge it */
	  int k = lsys (NR_open, (unsigned) kill, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0, 0);
	  if (k >= 0)
	    lclose (k);
	  escaped = 1;
	}
      /* nobody took it in 5 seconds: no rol-cmd */
      if (pos == 0 && now_cs () - t0 > 500 && lsys (NR_access, (unsigned) req, 0, 0, 0, 0, 0) == 0)
	{
	  lsys (NR_unlink, (unsigned) req, 0, 0, 0, 0, 0);
	  return ERR (no_daemon);
	}
      nap ();
    }
  drain (out, &pos);
  /* a token for next time? */
  f = lsys (NR_open, (unsigned) tok, 0, 0, 0, 0, 0);
  if (f >= 0)
    {
      char t[40];
      int m = lsys (NR_read, (unsigned) f, (unsigned) t, 32, 0, 0, 0);
      lclose (f);
      lsys (NR_unlink, (unsigned) tok, 0, 0, 0, 0, 0);
      if (m == 32)
	{
	  memcpy (token, t, 32);
	  token[32] = 0;
	}
    }
  lsys (NR_unlink, (unsigned) out, 0, 0, 0, 0, 0);
  lsys (NR_unlink, (unsigned) rc, 0, 0, 0, 0, 0);
  if (escaped)
    return ERR (stopped);
  return 0;
}

/* asks for a new password twice into hdr as "@newpassword ..." */
static oserror *
ask_new (char *hdr, int max, int *n)
{
  char a[128], b[128];
  oserror *e = read_password ("New root password: ", a, sizeof a);
  if (!e)
    e = read_password ("Again: ", b, sizeof b);
  if (!e && strcmp_ (a, b))
    e = ERR (mismatch);
  if (!e && !a[0])
    e = ERR (empty_pw);
  if (!e)
    {
      add (hdr, n, max, "@newpassword ");
      add (hdr, n, max, a);
      add (hdr, n, max, "\n");
    }
  memset (a, 0, sizeof a);
  memset (b, 0, sizeof b);
  return e;
}

/* runs cmd ("" for none), asking for the password as rol-cmd wants it */
static oserror *
authorised (const char *cmd, const char *extra, int *code)
{
  char hdr[400];
  int n = 0, tries = 0;
  oserror *e;
  hdr[0] = 0;
  if (token[0])
    {
      add (hdr, &n, sizeof hdr, "@token ");
      add (hdr, &n, sizeof hdr, token);
      add (hdr, &n, sizeof hdr, "\n");
    }
  add (hdr, &n, sizeof hdr, extra);
  for (;;)
    {
      e = run_job (hdr, cmd, code);
      memset (hdr, 0, sizeof hdr);
      n = 0;
      if (e)
	return e;
      if (*code == RC_WAIT)
	return ERR (wait_pw);
      if (*code != RC_AUTH && *code != RC_NOPASS)
	return 0;
      token[0] = 0;
      if (*code == RC_AUTH && tries++ == 3)
	return ERR (bad_pw);
      if (*code == RC_NOPASS)
	{
	  write0 ("Linux commands run as root, so they need Linux's root password. It has none yet: choose one.");
	  newline ();
	  if ((e = ask_new (hdr, sizeof hdr, &n)))
	    return e;
	}
      else
	{
	  char pw[128];
	  if (tries > 1)
	    {
	      write0 ("Wrong password.");
	      newline ();
	    }
	  e = read_password ("Root password: ", pw, sizeof pw);
	  if (!e)
	    {
	      add (hdr, &n, sizeof hdr, "@password ");
	      add (hdr, &n, sizeof hdr, pw);
	      add (hdr, &n, sizeof hdr, "\n");
	    }
	  memset (pw, 0, sizeof pw);
	  if (e)
	    return e;
	  add (hdr, &n, sizeof hdr, extra);
	}
    }
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) argc;
  if (lsys (20 /* getpid */, 0, 0, 0, 0, 0, 0) == -ENOSYS)
    return ERR (no_linux);
  int code;
  oserror *e;
  if (index == 1)
    {
      /* *LinuxPassword: the current password (rol-cmd asks), then the new */
      char hdr[400];
      int n = 0;
      if ((e = ask_new (hdr, sizeof hdr, &n)))
	return e;
      token[0] = 0;		/* (a change needs the password itself) */
      e = authorised ("", hdr, &code);
      memset (hdr, 0, sizeof hdr);
      if (e)
	return e;
      if (code)
	return ERR (failed);
      write0 ("Linux's root password has been set.");
      newline ();
      return 0;
    }
  char cmd[1024];
  int n = 0;
  while (tail[n] >= 32 || tail[n] == 9)
    {
      if (n >= (int) sizeof cmd - 2)
	return ERR (too_long);
      cmd[n] = tail[n];
      n++;
    }
  cmd[n++] = '\n';
  cmd[n] = 0;
  if (index == 2)
    e = run_job ("@user\n", cmd, &code);	/* *LinuxUser: no password */
  else
    e = authorised (cmd, "", &code);
  if (e)
    return e;
  if (code != 0)
    {
      write0 ("(exit status ");
      write_dec ((unsigned) code);
      write0 (")");
      newline ();
      return ERR (failed);
    }
  return 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  (void) number; (void) r;
  return 0;
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  counter = 0;
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  return 0;
}

void
mod_service (regs *r)
{
  (void) r;
}
