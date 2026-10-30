/* Iconv (lite) for RISC OS for Linux.

   UnixLib's iconv_open, iconv and iconv_close call the Iconv module
   (SWIs Iconv_Open, Iconv_Iconv, Iconv_Close, chunk &57540), which
   HardDisc4 doesn't have. This one converts between the character sets
   programs ask for most: UTF-8, ISO-8859-1 (Latin-1), ISO-8859-15,
   Windows-1252, US-ASCII, UTF-16 and UCS-2 (BE, LE, or with a byte order
   mark) and UTF-32/UCS-4 (BE, LE, or with a byte order mark; "WCHAR_T" is
   UTF-32LE). A "//TRANSLIT" suffix turns characters the target can't
   hold into "?"; "//IGNORE" leaves them out. Names are matched without
   case, '-' or '_'.

   Iconv_Open: r0 -> to-code, r1 -> from-code; out r0 = handle.
   Iconv_Iconv: r0 handle, r1 -> inbuf pointer (or 0: reset), r2 -> bytes
   left in, r3 -> outbuf pointer, r4 -> bytes left out; out r0 = the number
   of irreversible conversions. Errors as UnixLib expects: &81B900 no
   memory, &81B901 incomplete input or bad name, &81B902 output full,
   &81B903 invalid input.
   Iconv_Close: r0 handle.
   A handle that isn't open gets an error. A character set that isn't
   supported is noted in Iconv$Unsupported ("to <- from").

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */

#include "cmodule.h"

ERROR_BLOCK (err_nomem, 0x81B900, "Not enough memory for Iconv");
ERROR_BLOCK (err_inval, 0x81B901, "Invalid or incomplete character sequence");
ERROR_BLOCK (err_2big, 0x81B902, "Output buffer full");
ERROR_BLOCK (err_ilseq, 0x81B903, "Invalid character sequence");
ERROR_BLOCK (err_badname, 0x81B901, "Character set not supported by Iconv (lite)");
ERROR_BLOCK (err_bad_swi, 0x1E6, "Unknown Iconv SWI");
ERROR_BLOCK (err_badf, 0x81B901, "Invalid Iconv handle");

enum cs { UTF8, LATIN1, LATIN9, CP1252, ASCII, UTF16, UTF16BE, UTF16LE,
	  UTF32, UTF32BE, UTF32LE };

#define MAGIC 0x564e4f43u	/* "CONV" */

typedef struct conv
{
  struct conv *next;		/* the open handles, for checking them */
  unsigned magic;
  enum cs from, to;
  int translit, ignore;
  int in_le;			/* UTF16/UTF32 input: byte order (from BOM) */
  int bom_read, bom_written;
} conv;

/* The open handles: a handle is checked against these before it's used,
   so a bad one (a program passing on iconv_open's failure, (iconv_t) -1)
   gets an error instead of crashing in the SWI, which takes the desktop
   down with it (RiscOSM did) */
static conv *open_list;

static int
is_open (const conv *c)
{
  for (const conv *k = open_list; k; k = k->next)
    if (k == c)
      return 1;
  return 0;
}

static char
up (char c)
{
  return c >= 'a' && c <= 'z' ? c - 32 : c;
}

/* Compares a name (to its end or "//") with a pattern of capitals and
   digits, ignoring case, '-', '_' and spaces in the name. */
static int
name_is (const char *n, const char *pat)
{
  for (;;)
    {
      while (*n == '-' || *n == '_' || *n == ' ')
	n++;
      int end = !*n || (n[0] == '/' && n[1] == '/');
      if (!*pat)
	return end;
      if (end || up (*n) != *pat)
	return 0;
      n++, pat++;
    }
}

static int
parse (const char *n, enum cs *cs, int *translit, int *ignore)
{
  /* names inline: no pointers in data (the module has no relocations) */
  static const struct { char name[16]; enum cs cs; } names[] = {
    { "UTF8", UTF8 }, { "", UTF8 }, { "CHAR", UTF8 },
    { "ISO88591", LATIN1 }, { "LATIN1", LATIN1 }, { "L1", LATIN1 },
    { "ISO885911987", LATIN1 }, { "CP819", LATIN1 }, { "IBM819", LATIN1 },
    { "ISO885915", LATIN9 }, { "LATIN9", LATIN9 }, { "LATIN0", LATIN9 },
    { "CP1252", CP1252 }, { "WINDOWS1252", CP1252 }, { "MS1252", CP1252 },
    { "ASCII", ASCII }, { "USASCII", ASCII }, { "ANSIX3.41968", ASCII },
    { "646", ASCII }, { "US", ASCII },
    { "UTF16", UTF16 }, { "UCS2", UTF16 },
    { "UTF16BE", UTF16BE }, { "UCS2BE", UTF16BE },
    { "UTF16LE", UTF16LE }, { "UCS2LE", UTF16LE },
    { "UTF32", UTF32 }, { "UCS4", UTF32 },
    { "UTF32BE", UTF32BE }, { "UCS4BE", UTF32BE },
    { "UTF32LE", UTF32LE }, { "UCS4LE", UTF32LE },
    { "WCHART", UTF32LE }, { "UCS4INTERNAL", UTF32LE },
  };
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (name_is (n, names[i].name))
      {
	*cs = names[i].cs;
	for (const char *s = n; *s; s++)
	  if (s[0] == '/' && s[1] == '/')
	    {
	      if (name_is (s + 2, "TRANSLIT"))
		*translit = 1;
	      else if (name_is (s + 2, "IGNORE"))
		*ignore = 1;
	    }
	return 1;
      }
  return 0;
}

/* Windows-1252 &80-&9F */
static const unsigned short cp1252[32] = {
  0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
  0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178
};
/* ISO-8859-15 differs from Latin-1 at these 8 places */
static const unsigned char l9_at[8] = { 0xA4, 0xA6, 0xA8, 0xB4, 0xB8, 0xBC, 0xBD, 0xBE };
static const unsigned short l9_is[8] = { 0x20AC, 0x0160, 0x0161, 0x017D, 0x017E, 0x0152, 0x0153, 0x0178 };

/* Decodes one character. Returns its length (> 0), 0 if the input is
   incomplete, -1 if it's invalid. */
static int
decode (conv *c, const unsigned char *p, size_t n, unsigned *u)
{
  if (!n)
    return 0;
  switch (c->from)
    {
    case UTF8:
      {
	unsigned b = p[0], len, v;
	if (b < 0x80)
	  {
	    *u = b;
	    return 1;
	  }
	if (b < 0xC2)
	  return -1;
	len = b < 0xE0 ? 2 : b < 0xF0 ? 3 : b < 0xF5 ? 4 : 0;
	if (!len)
	  return -1;
	v = b & (0x7F >> len);
	for (unsigned i = 1; i < len; i++)
	  {
	    if (i >= n)
	      return 0;
	    if ((p[i] & 0xC0) != 0x80)
	      return -1;
	    v = v << 6 | (p[i] & 0x3F);
	  }
	if ((len == 3 && v < 0x800) || (len == 4 && (v < 0x10000 || v > 0x10FFFF))
	    || (v >= 0xD800 && v < 0xE000))
	  return -1;
	*u = v;
	return (int) len;
      }
    case LATIN1:
      *u = p[0];
      return 1;
    case LATIN9:
      *u = p[0];
      for (int i = 0; i < 8; i++)
	if (p[0] == l9_at[i])
	  *u = l9_is[i];
      return 1;
    case CP1252:
      *u = p[0] >= 0x80 && p[0] < 0xA0 ? cp1252[p[0] - 0x80] : p[0];
      return *u == 0xFFFD ? -1 : 1;
    case ASCII:
      *u = p[0];
      return p[0] < 0x80 ? 1 : -1;
    case UTF16:
    case UTF16BE:
    case UTF16LE:
      {
	int le = c->from == UTF16LE || (c->from == UTF16 && c->in_le);
	if (n < 2)
	  return 0;
	unsigned w = le ? (unsigned) (p[0] | p[1] << 8) : (unsigned) (p[0] << 8 | p[1]);
	if (w >= 0xDC00 && w < 0xE000)
	  return -1;
	if (w >= 0xD800 && w < 0xDC00)
	  {
	    if (n < 4)
	      return 0;
	    unsigned w2 = le ? (unsigned) (p[2] | p[3] << 8) : (unsigned) (p[2] << 8 | p[3]);
	    if (w2 < 0xDC00 || w2 >= 0xE000)
	      return -1;
	    *u = 0x10000 + ((w - 0xD800) << 10) + (w2 - 0xDC00);
	    return 4;
	  }
	*u = w;
	return 2;
      }
    default:			/* UTF32 */
      {
	int le = c->from == UTF32LE || (c->from == UTF32 && c->in_le);
	if (n < 4)
	  return 0;
	unsigned v = le ? (unsigned) (p[0] | p[1] << 8 | p[2] << 16 | (unsigned) p[3] << 24)
			: ((unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]);
	if (v > 0x10FFFF || (v >= 0xD800 && v < 0xE000))
	  return -1;
	*u = v;
	return 4;
      }
    }
}

/* Encodes one character into b. Returns its length, or -1 if the target
   can't hold it. */
static int
encode (conv *c, unsigned u, unsigned char *b)
{
  switch (c->to)
    {
    case UTF8:
      if (u < 0x80)
	{
	  b[0] = u;
	  return 1;
	}
      if (u < 0x800)
	{
	  b[0] = 0xC0 | u >> 6;
	  b[1] = 0x80 | (u & 0x3F);
	  return 2;
	}
      if (u < 0x10000)
	{
	  b[0] = 0xE0 | u >> 12;
	  b[1] = 0x80 | ((u >> 6) & 0x3F);
	  b[2] = 0x80 | (u & 0x3F);
	  return 3;
	}
      b[0] = 0xF0 | u >> 18;
      b[1] = 0x80 | ((u >> 12) & 0x3F);
      b[2] = 0x80 | ((u >> 6) & 0x3F);
      b[3] = 0x80 | (u & 0x3F);
      return 4;
    case LATIN1:
      if (u > 0xFF)
	return -1;
      b[0] = u;
      return 1;
    case LATIN9:
      for (int i = 0; i < 8; i++)
	{
	  if (u == l9_is[i])
	    {
	      b[0] = l9_at[i];
	      return 1;
	    }
	  if (u == l9_at[i])
	    return -1;
	}
      if (u > 0xFF)
	return -1;
      b[0] = u;
      return 1;
    case CP1252:
      if (u < 0x80 || (u >= 0xA0 && u <= 0xFF))
	{
	  b[0] = u;
	  return 1;
	}
      for (int i = 0; i < 32; i++)
	if (cp1252[i] == u && u != 0xFFFD)
	  {
	    b[0] = 0x80 + i;
	    return 1;
	  }
      return -1;
    case ASCII:
      if (u > 0x7F)
	return -1;
      b[0] = u;
      return 1;
    case UTF16:
    case UTF16BE:
    case UTF16LE:
      {
	int le = c->to == UTF16LE, n;
	unsigned w[2];
	if (u >= 0x10000)
	  {
	    w[0] = 0xD800 + ((u - 0x10000) >> 10);
	    w[1] = 0xDC00 + ((u - 0x10000) & 0x3FF);
	    n = 2;
	  }
	else
	  {
	    w[0] = u;
	    n = 1;
	  }
	for (int i = 0; i < n; i++)
	  {
	    b[2 * i + (le ? 0 : 1)] = w[i] & 0xFF;
	    b[2 * i + (le ? 1 : 0)] = w[i] >> 8;
	  }
	return 2 * n;
      }
    default:			/* UTF32 */
      {
	int le = c->to == UTF32LE;
	for (int i = 0; i < 4; i++)
	  b[le ? i : 3 - i] = (u >> (8 * i)) & 0xFF;
	return 4;
      }
    }
}

static oserror *
iconv_open (const char *to, const char *from, unsigned *handle)
{
  conv k;
  memset (&k, 0, sizeof k);
  if (!parse (to, &k.to, &k.translit, &k.ignore)
      || !parse (from, &k.from, &k.translit, &k.ignore))
    {
      /* note what was asked for in Iconv$Unsupported ("to <- from"), so
	 a program's missing character set can be seen with *Show */
      char v[100];
      size_t n = 0;
      for (const char *q = to; q && *q && n < 45; q++)
	v[n++] = *q;
      static const char arrow[] = " <- ";
      for (const char *q = arrow; *q; q++)
	v[n++] = *q;
      for (const char *q = from; q && *q && n < sizeof v - 1; q++)
	v[n++] = *q;
      regs sv = {{ (unsigned) "Iconv$Unsupported", (unsigned) v, n, 0, 0 }};
      swix_regs (0x24, &sv);		/* OS_SetVarVal, a string */
      return ERR (err_badname);
    }
  conv *c = rma_claim (sizeof *c);
  if (!c)
    return ERR (err_nomem);
  *c = k;
  c->magic = MAGIC;
  c->next = open_list;
  open_list = c;
  *handle = (unsigned) c;
  return NULL;
}

static oserror *
iconv_iconv (conv *c, unsigned char **inbuf, size_t *inleft,
	     unsigned char **outbuf, size_t *outleft, unsigned *count)
{
  *count = 0;
  if (!inbuf || !*inbuf)
    {				/* reset: the next output starts afresh */
      c->bom_read = c->bom_written = 0;
      c->in_le = 0;
      return NULL;
    }
  if (!outbuf || !*outbuf || !outleft || !inleft)
    return ERR (err_inval);
  if (!c->bom_read && (c->from == UTF16 || c->from == UTF32))
    {
      size_t w = c->from == UTF16 ? 2 : 4;
      if (*inleft < w)
	return *inleft ? ERR (err_inval) : NULL;
      const unsigned char *p = *inbuf;
      if (w == 2 && p[0] == 0xFF && p[1] == 0xFE)
	c->in_le = 1;
      else if (w == 4 && p[0] == 0xFF && p[1] == 0xFE && !p[2] && !p[3])
	c->in_le = 1;
      else if (!((w == 2 && p[0] == 0xFE && p[1] == 0xFF)
		 || (w == 4 && !p[0] && !p[1] && p[2] == 0xFE && p[3] == 0xFF)))
	w = 0;			/* no BOM: big-endian */
      *inbuf += w;
      *inleft -= w;
      c->bom_read = 1;
    }
  while (*inleft)
    {
      unsigned u;
      unsigned char b[8];
      int n = decode (c, *inbuf, *inleft, &u);
      if (n == 0)
	return ERR (err_inval);
      if (n < 0)
	{
	  if (!c->ignore)
	    return ERR (err_ilseq);
	  *inbuf += 1;
	  *inleft -= 1;
	  continue;
	}
      int m = encode (c, u, b);
      if (m < 0)
	{
	  if (c->ignore)
	    {
	      *inbuf += n;
	      *inleft -= n;
	      (*count)++;
	      continue;
	    }
	  if (!c->translit)
	    return ERR (err_ilseq);
	  m = encode (c, '?', b);
	  (*count)++;
	}
      int bom = 0;
      if (!c->bom_written && (c->to == UTF16 || c->to == UTF32))
	{
	  bom = c->to == UTF16 ? 2 : 4;
	  memmove (b + bom, b, m);
	  if (bom == 2)
	    b[0] = 0xFE, b[1] = 0xFF;
	  else
	    b[0] = 0, b[1] = 0, b[2] = 0xFE, b[3] = 0xFF;
	  m += bom;
	}
      if ((size_t) m > *outleft)
	return ERR (err_2big);
      memcpy (*outbuf, b, m);
      *outbuf += m;
      *outleft -= m;
      *inbuf += n;
      *inleft -= n;
      if (bom)
	c->bom_written = 1;
    }
  return NULL;
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  return NULL;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  return NULL;			/* open handles are in the RMA: left there */
}

oserror *
mod_swi (unsigned number, regs *r)
{
  conv *c = (conv *) r->r[0];
  switch (number)
    {
    case 0:
      return iconv_open ((const char *) r->r[0], (const char *) r->r[1], &r->r[0]);
    case 1:
      if (!is_open (c))
	return ERR (err_badf);
      return iconv_iconv (c, (unsigned char **) r->r[1], (size_t *) r->r[2],
			  (unsigned char **) r->r[3], (size_t *) r->r[4], &r->r[0]);
    case 2:
      if (!is_open (c))
	return ERR (err_badf);
      for (conv **pp = &open_list; *pp; pp = &(*pp)->next)
	if (*pp == c)
	  {
	    *pp = c->next;
	    break;
	  }
      c->magic = 0;
      rma_free (c);
      r->r[0] = 0;
      return NULL;
    default:
      return ERR (err_bad_swi);
    }
}

void
mod_service (regs *r)
{
  (void) r;
}

oserror *
mod_command (int index, const char *tail, int argc)
{
  (void) index; (void) tail; (void) argc;
  return NULL;
}
