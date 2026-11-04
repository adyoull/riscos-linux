/* t_mimetype: IXFS gives a file with no RISC OS type the type MimeMap gives
 * its extension, as DOSFS does ("photo.jpg" is JPEG), from 5.31-8. That's
 * for USB discs (FAT, exFAT, NTFS: no extended attributes, so files saved
 * elsewhere have no type) and files Linux programs wrote.
 *
 * The test writes its own MimeMap table and sets Inet$MimeMappings to it,
 * saves files with RISC OS (which stores their type), then reads them
 * through IXFS#X:, which ignores stored types, so only the name decides:
 *   photo/jpg -> JPEG (&C85), PHOTO/JPEG -> JPEG, notes/txt -> Text,
 *   noext -> Text, odd/qqq (not in the table) -> Text,
 *   and with M in the special field (IXFS#XM:) photo/jpg -> Text.
 * IXFS asks MimeMap first from a callback, so the first answers may be
 * Text: the test waits up to 3 s for photo/jpg to become JPEG.
 * A file whose type is stored (photo/jpg read through IXFS:) keeps it.
 *
 * Prints what it checked, then PASS or FAIL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

#define DIR "IXFS:$.tmp.rolmime"

static int fails;
#define CHECK(c) do { if (!(c)) { printf ("FAILED: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)

static unsigned
now (void)
{
  unsigned t = 0;
  _swix (OS_ReadMonotonicTime, _OUT (0), &t);
  return t;
}

static int
save (const char *name, int type, const char *data)
{
  _kernel_oserror *e = _swix (OS_File, _INR (0, 2) | _INR (4, 5), 10, name, type,
			      data, data + strlen (data));
  if (e)
    printf ("save %s: &%X %s\n", name, e->errnum, e->errmess);
  return !e;
}

/* the type OS_File 5 gives NAME (-1: not found, -2: untyped) */
static int
type_of (const char *name)
{
  int obj = 0;
  unsigned load = 0;
  if (_swix (OS_File, _INR (0, 1) | _OUT (0) | _OUT (2), 5, name, &obj, &load) || obj == 0)
    return -1;
  return (load & 0xFFF00000u) == 0xFFF00000u ? (int) ((load >> 8) & 0xFFF) : -2;
}

int
main (void)
{
  _swix (OS_File, _INR (0, 1) | _IN (4), 8, DIR, 0);
  static const char map[] =
    "# t_mimetype's own table\n"
    "image/jpeg  &C85  .jpg .jpeg\n"
    "text/plain  &FFF  .txt\n"
    "application/zip  &A91  .zip\n";
  CHECK (save (DIR ".map", 0xFFF, map));
  CHECK (!_swix (OS_SetVarVal, _INR (0, 4), "Inet$MimeMappings", DIR ".map",
		 strlen (DIR ".map"), 0, 4));

  CHECK (save (DIR ".photo/jpg", 0xFFF, "not really a JPEG"));
  CHECK (save (DIR ".PHOTO/JPEG", 0xFFF, "x"));
  CHECK (save (DIR ".notes/txt", 0xFFD, "x"));
  CHECK (save (DIR ".noext", 0xFFD, "x"));
  CHECK (save (DIR ".odd/qqq", 0xFFD, "x"));

  /* MimeMap is asked from a callback first: wait for it */
  unsigned t0 = now ();
  int t;
  while ((t = type_of ("IXFS#X:$.tmp.rolmime.photo/jpg")) != 0xC85 && now () - t0 < 300)
    ;
  printf ("photo/jpg by its name: &%03X after %u cs\n", t & 0xFFF, now () - t0);
  CHECK (t == 0xC85);
  /* MimeMap itself (asked after IXFS, so that IXFS's own first ask is tested) */
  int mt = -1;
  _kernel_oserror *me = _swix (0x50B00, _INR (0, 2) | _OUT (3), 3, "jpg", 0, &mt);
  printf ("MimeMap: jpg is &%03X%s%s\n", mt & 0xFFF, me ? ", error: " : "", me ? me->errmess : "");
  CHECK (!me && mt == 0xC85);

  static const struct { const char *name; int want; } c[] = {
    { "IXFS#X:$.tmp.rolmime.PHOTO/JPEG", 0xC85 },
    { "IXFS#X:$.tmp.rolmime.notes/txt", 0xFFF },
    { "IXFS#X:$.tmp.rolmime.noext", 0xFFF },
    { "IXFS#X:$.tmp.rolmime.odd/qqq", 0xFFF },
    { "IXFS#XM:$.tmp.rolmime.photo/jpg", 0xFFF },
    { DIR ".photo/jpg", 0xFFF },		/* its stored type wins */
    { DIR ".notes/txt", 0xFFD },
  };
  for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++)
    {
      t = type_of (c[i].name);
      printf ("%-36s &%03X %s\n", c[i].name, t & 0xFFF, t == c[i].want ? "ok" : "WRONG");
      if (t != c[i].want)
	fails++;
    }

  static const char *const files[] = { "photo/jpg", "PHOTO/JPEG", "notes/txt", "noext", "odd/qqq", "map" };
  for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++)
    {
      char n[80];
      snprintf (n, sizeof n, DIR ".%s", files[i]);
      _swix (OS_File, _INR (0, 1), 6, n);
    }
  _swix (OS_File, _INR (0, 1), 6, DIR);
  printf ("%s\n", fails ? "FAIL" : "PASS");
  return fails;
}
