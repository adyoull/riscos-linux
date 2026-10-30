/* ROBench: the portable tests (integer, floating point, memory).
   Copyright 2026 Andrew Youll. MIT licence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bench.h"

#define MB (1024.0 * 1024.0)

uint32_t
rng32 (uint32_t *s)             /* xorshift32 */
{
  uint32_t x = *s;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return *s = x;
}

/* English-like text: words from a fixed list, Zipf-ish choice, with
   punctuation and line breaks (what compressors and parsers meet). */
static const char *const words[] = {
  "the", "of", "and", "to", "a", "in", "is", "it", "that", "for", "on",
  "was", "with", "as", "by", "at", "from", "this", "be", "are", "or",
  "an", "which", "but", "not", "have", "had", "they", "you", "were",
  "their", "one", "all", "we", "can", "her", "has", "there", "been",
  "if", "more", "when", "will", "would", "who", "so", "no", "acorn",
  "computer", "desktop", "window", "module", "filer", "sprite", "draw",
  "network", "memory", "processor", "application", "interrupt", "vector",
  "keyboard", "printer", "screen", "palette", "font", "manager", "task",
  "riscos", "archimedes", "machine", "system", "program", "basic",
  "assembler", "register", "pointer", "message", "redraw", "icon"
};
#define NWORDS (sizeof words / sizeof words[0])

void
make_text (unsigned char *buf, size_t len, uint32_t seed)
{
  size_t i = 0;
  int col = 0;
  while (i < len)
    {
      uint32_t r = rng32 (&seed);
      /* Zipf-ish: small indices much more likely */
      size_t w = (size_t) ((r % 1000) * (r % 1000) / 1000000.0 * NWORDS);
      if (w >= NWORDS)
        w = NWORDS - 1;
      const char *p = words[w];
      while (*p && i < len)
        buf[i++] = (unsigned char) *p++, col++;
      if (i >= len)
        break;
      r = rng32 (&seed);
      if (r % 17 == 0)
        buf[i++] = ',';
      else if (r % 23 == 0)
        buf[i++] = '.';
      if (i >= len)
        break;
      if (col > 70)
        buf[i++] = '\n', col = 0;
      else
        buf[i++] = ' ', col++;
    }
}

static uint32_t crc_table[256];

static void
crc_init (void)
{
  for (uint32_t i = 0; i < 256; i++)
    {
      uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      crc_table[i] = c;
    }
}

uint32_t
crc32_buf (uint32_t crc, const void *p, size_t n)
{
  const unsigned char *b = p;
  if (!crc_table[1])
    crc_init ();
  crc = ~crc;
  while (n--)
    crc = crc_table[(crc ^ *b++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

/* ------------------------------------------------------------------ */
/* CoreMark-style integer workload: linked list, matrix, state machine,
   CRC (the kinds of code CoreMark uses; this is not CoreMark and its
   numbers aren't CoreMark scores). */

#define CM_LIST 300
#define CM_N 24
typedef struct cm_node { struct cm_node *next; int16_t data, idx; } cm_node;
static cm_node cm_nodes[CM_LIST];
static int16_t cm_A[CM_N * CM_N], cm_B[CM_N * CM_N];
static int32_t cm_C[CM_N * CM_N];
static char cm_text[640];

static uint16_t
crc16u (uint16_t crc, uint32_t v)
{
  for (int i = 0; i < 32; i += 8)
    {
      uint8_t d = (uint8_t) (v >> i);
      for (int k = 0; k < 8; k++)
        {
          uint8_t x = (uint8_t) ((d ^ crc) & 1);
          d >>= 1;
          crc = x ? (uint16_t) ((crc >> 1) ^ 0xA001) : (uint16_t) (crc >> 1);
        }
    }
  return crc;
}

static cm_node *
cm_merge_sort (cm_node *list, int by_idx)
{
  /* bottom-up merge sort of a singly linked list */
  int insize = 1;
  if (!list)
    return NULL;
  for (;;)
    {
      cm_node *p = list, *tail = NULL;
      int merges = 0;
      list = NULL;
      while (p)
        {
          cm_node *q = p;
          int psize = 0, qsize = insize;
          merges++;
          for (int i = 0; i < insize && q; i++)
            psize++, q = q->next;
          while (psize > 0 || (qsize > 0 && q))
            {
              cm_node *e;
              if (psize == 0)
                e = q, q = q->next, qsize--;
              else if (qsize == 0 || !q)
                e = p, p = p->next, psize--;
              else if ((by_idx ? p->idx <= q->idx : p->data <= q->data))
                e = p, p = p->next, psize--;
              else
                e = q, q = q->next, qsize--;
              if (tail)
                tail->next = e;
              else
                list = e;
              tail = e;
            }
          p = q;
        }
      tail->next = NULL;
      if (merges <= 1)
        return list;
      insize *= 2;
    }
}

static int
cm_setup (void)
{
  uint32_t s = 0x1234567;
  for (int i = 0; i < CM_N * CM_N; i++)
    {
      cm_A[i] = (int16_t) (rng32 (&s) & 0x3ff) - 0x200;
      cm_B[i] = (int16_t) (rng32 (&s) & 0x3ff) - 0x200;
    }
  static const char *const toks[] = { "5012", "1.23e4", "-0.5", "+76", "abc",
    "0x1f", "3.", "-12E-3", "77", "-.5", "8888", "1e" };
  size_t n = 0;
  while (n + 10 < sizeof cm_text)
    {
      const char *t = toks[rng32 (&s) % 12];
      size_t l = strlen (t);
      memcpy (cm_text + n, t, l);
      n += l;
      cm_text[n++] = ',';
    }
  cm_text[n] = 0;
  return 0;
}

enum { S_START, S_INT, S_FLOAT, S_EXP, S_SCI, S_INVALID, S_NSTATES };

static uint32_t
cm_state (const char *p, uint32_t counts[S_NSTATES])
{
  int st = S_START;
  uint32_t trans = 0;
  for (; *p; p++)
    {
      char c = *p;
      int ns = st;
      if (c == ',')
        {
          counts[st]++;
          ns = S_START;
        }
      else
        switch (st)
          {
          case S_START:
            ns = (c >= '0' && c <= '9') ? S_INT
              : (c == '+' || c == '-') ? S_INT : (c == '.') ? S_FLOAT : S_INVALID;
            break;
          case S_INT:
            ns = (c >= '0' && c <= '9') ? S_INT : (c == '.') ? S_FLOAT
              : (c == 'e' || c == 'E') ? S_EXP : S_INVALID;
            break;
          case S_FLOAT:
            ns = (c >= '0' && c <= '9') ? S_FLOAT
              : (c == 'e' || c == 'E') ? S_EXP : S_INVALID;
            break;
          case S_EXP:
            ns = (c == '+' || c == '-' || (c >= '0' && c <= '9')) ? S_SCI : S_INVALID;
            break;
          case S_SCI:
            ns = (c >= '0' && c <= '9') ? S_SCI : S_INVALID;
            break;
          default:
            break;
          }
      trans += ns != st;
      st = ns;
    }
  return trans;
}

static uint32_t
cm_run (uint32_t iters)
{
  uint16_t crc = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      /* list: build, find, reverse, sort by data, sort back by idx */
      int16_t seed = (int16_t) it;
      for (int i = 0; i < CM_LIST; i++)
        {
          cm_nodes[i].next = i + 1 < CM_LIST ? &cm_nodes[i + 1] : NULL;
          cm_nodes[i].idx = (int16_t) i;
          cm_nodes[i].data = (int16_t) ((i * 7919 + seed * 31) & 0x7fff);
        }
      cm_node *list = &cm_nodes[0];
      for (int f = 0; f < 32; f++)
        {
          int16_t want = (int16_t) (((f * 37 + seed) * 7919) & 0x7fff);
          int pos = 0;
          for (cm_node *n = list; n; n = n->next, pos++)
            if (n->data == want)
              break;
          crc = crc16u (crc, (uint32_t) pos);
        }
      cm_node *prev = NULL;
      while (list)
        {
          cm_node *nx = list->next;
          list->next = prev;
          prev = list;
          list = nx;
        }
      list = cm_merge_sort (prev, 0);
      crc = crc16u (crc, (uint32_t) list->data);
      list = cm_merge_sort (list, 1);
      crc = crc16u (crc, (uint32_t) list->next->idx);

      /* matrix */
      int16_t k = (int16_t) (seed | 1);
      for (int i = 0; i < CM_N * CM_N; i++)
        cm_A[i] = (int16_t) (cm_A[i] + k);
      for (int i = 0; i < CM_N; i++)
        for (int j = 0; j < CM_N; j++)
          {
            int32_t acc = 0;
            for (int x = 0; x < CM_N; x++)
              acc += (int32_t) cm_A[i * CM_N + x] * cm_B[x * CM_N + j];
            cm_C[i * CM_N + j] = acc;
          }
      uint32_t msum = 0;
      for (int i = 0; i < CM_N * CM_N; i++)
        msum += (uint32_t) ((cm_C[i] >> 2) & 0xf) * (uint32_t) ((cm_C[i] >> 5) & 0x7f);
      crc = crc16u (crc, msum);
      for (int i = 0; i < CM_N * CM_N; i++)
        cm_A[i] = (int16_t) (cm_A[i] - k);

      /* state machine */
      uint32_t counts[S_NSTATES] = { 0 };
      crc = crc16u (crc, cm_state (cm_text, counts));
      for (int i = 0; i < S_NSTATES; i++)
        crc = crc16u (crc, counts[i]);
    }
  return crc;
}

/* ------------------------------------------------------------------ */
/* LZ77 compression (LZ4-style greedy, hash table) and decompression,
   on 1 MB of English-like text. */

#define LZ_IN (1024 * 1024)
static unsigned char *lz_in, *lz_out, *lz_back;
static size_t lz_clen;

static size_t
lz_compress (const unsigned char *in, size_t n, unsigned char *out)
{
  static uint32_t table[1 << 14];
  memset (table, 0, sizeof table);
  size_t ip = 0, op = 0, anchor = 0;
  while (ip + 12 < n)
    {
      uint32_t v;
      memcpy (&v, in + ip, 4);
      uint32_t h = (v * 2654435761u) >> 18;
      size_t ref = table[h];
      table[h] = (uint32_t) ip;
      uint32_t w;
      if (ref && ip - ref < 65536 && (memcpy (&w, in + ref, 4), w == v))
        {
          size_t len = 4;
          while (ip + len + 5 < n && in[ref + len] == in[ip + len])
            len++;
          size_t lit = ip - anchor;
          unsigned char *tok = out + op++;
          *tok = (unsigned char) ((lit < 15 ? lit : 15) << 4);
          if (lit >= 15)
            {
              size_t l = lit - 15;
              for (; l >= 255; l -= 255)
                out[op++] = 255;
              out[op++] = (unsigned char) l;
            }
          memcpy (out + op, in + anchor, lit);
          op += lit;
          size_t off = ip - ref;
          out[op++] = (unsigned char) off;
          out[op++] = (unsigned char) (off >> 8);
          size_t ml = len - 4;
          *tok |= (unsigned char) (ml < 15 ? ml : 15);
          if (ml >= 15)
            {
              size_t l = ml - 15;
              for (; l >= 255; l -= 255)
                out[op++] = 255;
              out[op++] = (unsigned char) l;
            }
          ip += len;
          anchor = ip;
        }
      else
        ip++;
    }
  /* last literals */
  size_t lit = n - anchor;
  unsigned char *tok = out + op++;
  *tok = (unsigned char) ((lit < 15 ? lit : 15) << 4);
  if (lit >= 15)
    {
      size_t l = lit - 15;
      for (; l >= 255; l -= 255)
        out[op++] = 255;
      out[op++] = (unsigned char) l;
    }
  memcpy (out + op, in + anchor, lit);
  op += lit;
  return op;
}

static size_t
lz_decompress (const unsigned char *in, size_t n, unsigned char *out)
{
  size_t ip = 0, op = 0;
  while (ip < n)
    {
      unsigned tok = in[ip++];
      size_t lit = tok >> 4;
      if (lit == 15)
        {
          unsigned b;
          do
            lit += b = in[ip++];
          while (b == 255);
        }
      memcpy (out + op, in + ip, lit);
      ip += lit;
      op += lit;
      if (ip >= n)
        break;
      size_t off = in[ip] | (size_t) in[ip + 1] << 8;
      ip += 2;
      size_t ml = tok & 15;
      if (ml == 15)
        {
          unsigned b;
          do
            ml += b = in[ip++];
          while (b == 255);
        }
      ml += 4;
      unsigned char *d = out + op;
      const unsigned char *s = d - off;
      while (ml--)
        *d++ = *s++;            /* may overlap: byte by byte */
      op = (size_t) (d - out);
    }
  return op;
}

static int
lz_setup (void)
{
  if (lz_in)
    return 0;
  lz_in = malloc (LZ_IN);
  lz_out = malloc (LZ_IN + LZ_IN / 64 + 64);
  lz_back = malloc (LZ_IN + 64);
  if (!lz_in || !lz_out || !lz_back)
    return -1;
  make_text (lz_in, LZ_IN, 42);
  lz_clen = lz_compress (lz_in, LZ_IN, lz_out);
  return 0;
}

static void
lz_teardown (void)
{
  free (lz_in);
  free (lz_out);
  free (lz_back);
  lz_in = lz_out = lz_back = NULL;
}

static uint32_t
lzc_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t i = 0; i < iters; i++)
    {
      lz_clen = lz_compress (lz_in, LZ_IN, lz_out);
      sum += (uint32_t) lz_clen + lz_out[lz_clen / 2];
    }
  return sum;
}

static uint32_t
lzd_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t i = 0; i < iters; i++)
    {
      size_t n = lz_decompress (lz_out, lz_clen, lz_back);
      sum += (uint32_t) n + lz_back[i % LZ_IN];
    }
  return sum;
}

static int
lz_verify (char *msg, size_t len)
{
  size_t n = lz_decompress (lz_out, lz_clen, lz_back);
  int ok = n == LZ_IN && memcmp (lz_in, lz_back, LZ_IN) == 0;
  snprintf (msg, len, "ratio %.2f, round trip %s",
            (double) LZ_IN / (double) lz_clen, ok ? "exact" : "WRONG");
  return ok;
}

/* ------------------------------------------------------------------ */
/* SHA-256 (FIPS 180-4) */

typedef struct { uint32_t h[8]; } sha_ctx;
static const uint32_t sha_k[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
  0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
  0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
  0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
  0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void
sha_block (sha_ctx *c, const unsigned char *p)
{
  uint32_t w[64], a, b, d, e, f, g, h, cc;
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t) p[4 * i] << 24 | (uint32_t) p[4 * i + 1] << 16
      | (uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 64; i++)
    {
      uint32_t s0 = ROR (w[i - 15], 7) ^ ROR (w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = ROR (w[i - 2], 17) ^ ROR (w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
  a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
  e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
  for (int i = 0; i < 64; i++)
    {
      uint32_t t1 = h + (ROR (e, 6) ^ ROR (e, 11) ^ ROR (e, 25))
        + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
      uint32_t t2 = (ROR (a, 2) ^ ROR (a, 13) ^ ROR (a, 22))
        + ((a & b) ^ (a & cc) ^ (b & cc));
      h = g, g = f, f = e, e = d + t1, d = cc, cc = b, b = a, a = t1 + t2;
    }
  c->h[0] += a, c->h[1] += b, c->h[2] += cc, c->h[3] += d;
  c->h[4] += e, c->h[5] += f, c->h[6] += g, c->h[7] += h;
}

static void
sha256 (const void *data, size_t n, uint32_t out[8])
{
  static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372,
    0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
  sha_ctx c;
  const unsigned char *p = data;
  unsigned char last[128];
  size_t full = n & ~(size_t) 63, rest = n - full;
  memcpy (c.h, iv, sizeof iv);
  for (size_t i = 0; i < full; i += 64)
    sha_block (&c, p + i);
  memset (last, 0, sizeof last);
  memcpy (last, p + full, rest);
  last[rest] = 0x80;
  size_t lb = rest < 56 ? 64 : 128;
  uint64_t bits = (uint64_t) n * 8;
  for (int i = 0; i < 8; i++)
    last[lb - 1 - i] = (unsigned char) (bits >> (8 * i));
  sha_block (&c, last);
  if (lb == 128)
    sha_block (&c, last + 64);
  memcpy (out, c.h, sizeof c.h);
}

#define SHA_BUF (1024 * 1024)
static unsigned char *sha_buf;

static int
sha_setup (void)
{
  if (sha_buf)
    return 0;
  sha_buf = malloc (SHA_BUF);
  if (!sha_buf)
    return -1;
  uint32_t s = 7;
  for (size_t i = 0; i < SHA_BUF; i++)
    sha_buf[i] = (unsigned char) rng32 (&s);
  return 0;
}

static void
sha_teardown (void)
{
  free (sha_buf);
  sha_buf = NULL;
}

static uint32_t
sha_run (uint32_t iters)
{
  uint32_t out[8], sum = 0;
  for (uint32_t i = 0; i < iters; i++)
    {
      sha256 (sha_buf, SHA_BUF, out);
      sum += out[0] ^ out[7];
    }
  return sum;
}

static int
sha_verify (char *msg, size_t len)
{
  uint32_t out[8];
  sha256 ("abc", 3, out);
  int ok = out[0] == 0xba7816bf && out[7] == 0xf20015ad;
  sha256 ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, out);
  ok = ok && out[0] == 0x248d6a61 && out[7] == 0x19db06c1;
  snprintf (msg, len, "FIPS 180-4 test vectors %s", ok ? "match" : "WRONG");
  return ok;
}

/* ------------------------------------------------------------------ */
/* CRC-32 (zlib's polynomial, byte at a time from a table) */

static uint32_t
crc_run (uint32_t iters)
{
  uint32_t c = 0;
  for (uint32_t i = 0; i < iters; i++)
    c = crc32_buf (c, sha_buf, SHA_BUF);
  return c;
}

static int
crc_verify (char *msg, size_t len)
{
  int ok = crc32_buf (0, "123456789", 9) == 0xCBF43926u;
  snprintf (msg, len, "check value %s", ok ? "matches" : "WRONG");
  return ok;
}

/* ------------------------------------------------------------------ */
/* Sorting: introsort-style quicksort (median of three, insertion sort
   for small parts) of 256K random 32-bit numbers. */

#define SORT_N (256 * 1024)
static uint32_t *sort_src, *sort_a;

static void
isort (uint32_t *a, long n)
{
  for (long i = 1; i < n; i++)
    {
      uint32_t v = a[i];
      long j = i - 1;
      while (j >= 0 && a[j] > v)
        a[j + 1] = a[j], j--;
      a[j + 1] = v;
    }
}

static void
qsort32 (uint32_t *a, long n)
{
  while (n > 16)
    {
      uint32_t x = a[0], y = a[n / 2], z = a[n - 1], p;
      p = x < y ? (y < z ? y : (x < z ? z : x)) : (x < z ? x : (y < z ? z : y));
      long i = 0, j = n - 1;
      for (;;)
        {
          while (a[i] < p)
            i++;
          while (a[j] > p)
            j--;
          if (i >= j)
            break;
          uint32_t t = a[i];
          a[i] = a[j], a[j] = t;
          i++, j--;
        }
      /* recurse into the smaller part */
      if (j + 1 < n - j - 1)
        {
          qsort32 (a, j + 1);
          a += j + 1, n -= j + 1;
        }
      else
        {
          qsort32 (a + j + 1, n - j - 1);
          n = j + 1;
        }
    }
  isort (a, n);
}

static int
sort_setup (void)
{
  sort_src = malloc (SORT_N * 4);
  sort_a = malloc (SORT_N * 4);
  if (!sort_src || !sort_a)
    return -1;
  uint32_t s = 99;
  for (long i = 0; i < SORT_N; i++)
    sort_src[i] = rng32 (&s);
  return 0;
}

static void
sort_teardown (void)
{
  free (sort_src);
  free (sort_a);
}

static uint32_t
sort_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t i = 0; i < iters; i++)
    {
      memcpy (sort_a, sort_src, SORT_N * 4);
      sort_a[i % SORT_N] ^= i;  /* a slightly different array each time */
      qsort32 (sort_a, SORT_N);
      sum += sort_a[SORT_N / 2] + sort_a[0];
    }
  return sum;
}

static int
sort_verify (char *msg, size_t len)
{
  for (long i = 1; i < SORT_N; i++)
    if (sort_a[i - 1] > sort_a[i])
      {
        snprintf (msg, len, "NOT SORTED at %ld", i);
        return 0;
      }
  snprintf (msg, len, "sorted");
  return 1;
}

/* ------------------------------------------------------------------ */
/* Word frequency: tokenise 1 MB of text, count words in a hash table
   (FNV-1a, open addressing) - string handling as in parsers. */

#define WF_SLOTS 4096
static struct { uint32_t off, len, count, hash; } wf_tab[WF_SLOTS];
static unsigned char *wf_text;

static int
wf_setup (void)
{
  wf_text = malloc (LZ_IN);
  if (!wf_text)
    return -1;
  make_text (wf_text, LZ_IN, 4242);
  return 0;
}

static void
wf_teardown (void)
{
  free (wf_text);
}

static uint32_t
wf_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      memset (wf_tab, 0, sizeof wf_tab);
      size_t i = 0;
      uint32_t distinct = 0;
      while (i < LZ_IN)
        {
          while (i < LZ_IN && !(wf_text[i] >= 'a' && wf_text[i] <= 'z'))
            i++;
          size_t s = i;
          uint32_t h = 2166136261u;
          while (i < LZ_IN && wf_text[i] >= 'a' && wf_text[i] <= 'z')
            h = (h ^ wf_text[i++]) * 16777619u;
          size_t l = i - s;
          if (!l)
            break;
          uint32_t slot = h & (WF_SLOTS - 1);
          for (;;)
            {
              if (!wf_tab[slot].count)
                {
                  wf_tab[slot].off = (uint32_t) s, wf_tab[slot].len = (uint32_t) l;
                  wf_tab[slot].hash = h, wf_tab[slot].count = 1;
                  distinct++;
                  break;
                }
              if (wf_tab[slot].hash == h && wf_tab[slot].len == l
                  && memcmp (wf_text + wf_tab[slot].off, wf_text + s, l) == 0)
                {
                  wf_tab[slot].count++;
                  break;
                }
              slot = (slot + 1) & (WF_SLOTS - 1);
            }
        }
      uint32_t best = 0;
      for (int k = 0; k < WF_SLOTS; k++)
        if (wf_tab[k].count > best)
          best = wf_tab[k].count;
      sum += distinct * 65536 + best;
    }
  return sum;
}

/* ------------------------------------------------------------------ */
/* N-queens (10x10, bitmask recursion: 724 solutions) - branchy code */

static uint32_t
nq (uint32_t cols, uint32_t d1, uint32_t d2, uint32_t all)
{
  if (cols == all)
    return 1;
  uint32_t n = 0, avail = all & ~(cols | d1 | d2);
  while (avail)
    {
      uint32_t bit = avail & -avail;
      avail -= bit;
      n += nq (cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1, all);
    }
  return n;
}

static uint32_t nq_last;

static uint32_t
nq_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t i = 0; i < iters; i++)
    sum += nq_last = nq (0, 0, 0, (1u << 10) - 1);
  return sum;
}

static int
nq_verify (char *msg, size_t len)
{
  snprintf (msg, len, "%u solutions%s", nq_last, nq_last == 724 ? "" : " (WRONG)");
  return nq_last == 724;
}

/* ------------------------------------------------------------------ */
/* Whetstone (Curnow & Wichmann 1976; the double precision C version's
   modules): one iteration = 1 million Whetstone instructions. */

static double wt_t = 0.499975, wt_t1 = 0.50025, wt_t2 = 2.0;
static double wt_e1[4];
static int wt_j, wt_k, wt_l;
static double wt_x, wt_y, wt_z, wt_check;

static void
wt_pa (double *e)
{
  for (int j = 0; j < 6; j++)
    {
      e[0] = (e[0] + e[1] + e[2] - e[3]) * wt_t;
      e[1] = (e[0] + e[1] - e[2] + e[3]) * wt_t;
      e[2] = (e[0] - e[1] + e[2] + e[3]) * wt_t;
      e[3] = (-e[0] + e[1] + e[2] + e[3]) / wt_t2;
    }
}

static void
wt_p0 (void)
{
  wt_e1[wt_j] = wt_e1[wt_k];
  wt_e1[wt_k] = wt_e1[wt_l];
  wt_e1[wt_l] = wt_e1[wt_j];
}

static void
wt_p3 (double x, double y, double *z)
{
  x = wt_t * (x + y);
  y = wt_t * (x + y);
  *z = (x + y) / wt_t2;
}

static uint32_t
wt_run (uint32_t iters)
{
  const long L = 10;            /* x10 = one million instructions */
  long n2 = 12 * L, n3 = 14 * L, n4 = 345 * L, n6 = 210 * L, n7 = 32 * L,
    n8 = 899 * L, n9 = 616 * L, n11 = 93 * L;
  double sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      double x1 = 1.0, x2 = -1.0, x3 = -1.0, x4 = -1.0;
      /* module 2: array elements */
      wt_e1[0] = 1.0, wt_e1[1] = -1.0, wt_e1[2] = -1.0, wt_e1[3] = -1.0;
      for (long i = 0; i < n2; i++)
        {
          wt_e1[0] = (wt_e1[0] + wt_e1[1] + wt_e1[2] - wt_e1[3]) * wt_t;
          wt_e1[1] = (wt_e1[0] + wt_e1[1] - wt_e1[2] + wt_e1[3]) * wt_t;
          wt_e1[2] = (wt_e1[0] - wt_e1[1] + wt_e1[2] + wt_e1[3]) * wt_t;
          wt_e1[3] = (-wt_e1[0] + wt_e1[1] + wt_e1[2] + wt_e1[3]) * wt_t;
        }
      sum += wt_e1[0] + wt_e1[1] + wt_e1[2] + wt_e1[3];
      /* module 3: array as parameter */
      for (long i = 0; i < n3; i++)
        wt_pa (wt_e1);
      sum += wt_e1[0] + wt_e1[1] + wt_e1[2] + wt_e1[3];
      /* module 4: conditional jumps */
      wt_j = 1;
      for (long i = 0; i < n4; i++)
        {
          wt_j = wt_j == 1 ? 2 : 3;
          wt_j = wt_j > 2 ? 0 : 1;
          wt_j = wt_j < 1 ? 1 : 0;
        }
      sum += wt_j;
      /* module 6: integer arithmetic */
      wt_j = 1, wt_k = 2, wt_l = 3;
      for (long i = 0; i < n6; i++)
        {
          wt_j = wt_j * (wt_k - wt_j) * (wt_l - wt_k);
          wt_k = wt_l * wt_k - (wt_l - wt_j) * wt_k;
          wt_l = (wt_l - wt_k) * (wt_k + wt_j);
          wt_e1[wt_l - 2] = wt_j + wt_k + wt_l;
          wt_e1[wt_k - 2] = wt_j * wt_k * wt_l;
        }
      sum += wt_j + wt_k + wt_l + wt_e1[0] + wt_e1[1];
      /* module 7: trigonometric functions */
      wt_x = 0.5, wt_y = 0.5;
      for (long i = 0; i < n7; i++)
        {
          wt_x = wt_t * atan (wt_t2 * sin (wt_x) * cos (wt_x)
                              / (cos (wt_x + wt_y) + cos (wt_x - wt_y) - 1.0));
          wt_y = wt_t * atan (wt_t2 * sin (wt_y) * cos (wt_y)
                              / (cos (wt_x + wt_y) + cos (wt_x - wt_y) - 1.0));
        }
      sum += wt_x + wt_y;
      /* module 8: procedure calls */
      wt_x = 1.0, wt_y = 1.0, wt_z = 1.0;
      for (long i = 0; i < n8; i++)
        wt_p3 (wt_x, wt_y, &wt_z);
      sum += wt_z;
      /* module 9: array references */
      wt_j = 0, wt_k = 1, wt_l = 2;
      wt_e1[0] = 1.0, wt_e1[1] = 2.0, wt_e1[2] = 3.0;
      for (long i = 0; i < n9; i++)
        wt_p0 ();
      sum += wt_e1[0] + wt_e1[1] + wt_e1[2];
      /* module 11: standard functions */
      wt_x = 0.75;
      for (long i = 0; i < n11; i++)
        wt_x = sqrt (exp (log (wt_x) / wt_t1));
      (void) x1, (void) x2, (void) x3, (void) x4;
      sum += wt_x;
    }
  wt_check = sum / iters;
  double scaled = wt_check * 1e6;
  return (uint32_t) (int64_t) scaled;
}

static int
wt_verify (char *msg, size_t len)
{
  /* the same on every IEEE double machine with a sound maths library */
  int ok = fabs (wt_check - 25.113881060) < 1e-6;
  snprintf (msg, len, "result %.9f%s", wt_check, ok ? "" : " (expected 25.113881060)");
  return ok;
}

/* ------------------------------------------------------------------ */
/* LINPACK (Dongarra's dgefa/dgesl, double, 200 x 200, column major):
   LU factorisation with partial pivoting, then solve. */

#define LP_N 200
#define LP_LDA 201
static double *lp_a, *lp_b, *lp_x, *lp_a0, *lp_b0;
static int lp_ipvt[LP_N];
static double lp_resid;

static void
lp_matgen (double *a, double *b)
{
  int init = 1325;
  for (int j = 0; j < LP_N; j++)
    for (int i = 0; i < LP_N; i++)
      {
        init = 3125 * init % 65536;
        a[LP_LDA * j + i] = (init - 32768.0) / 16384.0;
      }
  for (int i = 0; i < LP_N; i++)
    b[i] = 0;
  for (int j = 0; j < LP_N; j++)
    for (int i = 0; i < LP_N; i++)
      b[i] += a[LP_LDA * j + i];
}

static void
lp_daxpy (int n, double da, const double *dx, double *dy)
{
  for (int i = 0; i < n; i++)
    dy[i] += da * dx[i];
}

static int
lp_dgefa (double *a, int *ipvt)
{
  int info = 0;
  for (int k = 0; k < LP_N - 1; k++)
    {
      double *col = a + LP_LDA * k;
      int l = k;
      double dmax = fabs (col[k]);
      for (int i = k + 1; i < LP_N; i++)
        if (fabs (col[i]) > dmax)
          dmax = fabs (col[i]), l = i;
      ipvt[k] = l;
      if (col[l] == 0.0)
        {
          info = k;
          continue;
        }
      if (l != k)
        {
          double t = col[l];
          col[l] = col[k], col[k] = t;
        }
      double t = -1.0 / col[k];
      for (int i = k + 1; i < LP_N; i++)
        col[i] *= t;
      for (int j = k + 1; j < LP_N; j++)
        {
          double *cj = a + LP_LDA * j;
          t = cj[l];
          if (l != k)
            cj[l] = cj[k], cj[k] = t;
          lp_daxpy (LP_N - (k + 1), t, col + k + 1, cj + k + 1);
        }
    }
  ipvt[LP_N - 1] = LP_N - 1;
  return info;
}

static void
lp_dgesl (const double *a, const int *ipvt, double *b)
{
  for (int k = 0; k < LP_N - 1; k++)
    {
      int l = ipvt[k];
      double t = b[l];
      if (l != k)
        b[l] = b[k], b[k] = t;
      lp_daxpy (LP_N - (k + 1), t, a + LP_LDA * k + k + 1, b + k + 1);
    }
  for (int kb = 0; kb < LP_N; kb++)
    {
      int k = LP_N - (kb + 1);
      b[k] /= a[LP_LDA * k + k];
      double t = -b[k];
      lp_daxpy (k, t, a + LP_LDA * k, b);
    }
}

static int
lp_setup (void)
{
  size_t sz = sizeof (double) * LP_LDA * LP_N;
  lp_a = malloc (sz), lp_a0 = malloc (sz);
  lp_b = malloc (sizeof (double) * LP_N), lp_b0 = malloc (sizeof (double) * LP_N);
  lp_x = malloc (sizeof (double) * LP_N);
  if (!lp_a || !lp_a0 || !lp_b || !lp_b0 || !lp_x)
    return -1;
  lp_matgen (lp_a0, lp_b0);
  return 0;
}

static void
lp_teardown (void)
{
  free (lp_a), free (lp_a0), free (lp_b), free (lp_b0), free (lp_x);
}

static uint32_t
lp_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      memcpy (lp_a, lp_a0, sizeof (double) * LP_LDA * LP_N);
      memcpy (lp_b, lp_b0, sizeof (double) * LP_N);
      lp_dgefa (lp_a, lp_ipvt);
      lp_dgesl (lp_a, lp_ipvt, lp_b);
      sum += (uint32_t) (int64_t) (lp_b[it % LP_N] * 1e6);
    }
  memcpy (lp_x, lp_b, sizeof (double) * LP_N);
  return sum;
}

static int
lp_verify (char *msg, size_t len)
{
  /* residual |Ax - b| normalised, as LINPACK prints it */
  double rmax = 0, xmax = 0, amax = 0;
  for (int i = 0; i < LP_N; i++)
    {
      double r = -lp_b0[i];
      for (int j = 0; j < LP_N; j++)
        {
          r += lp_a0[LP_LDA * j + i] * lp_x[j];
          if (fabs (lp_a0[LP_LDA * j + i]) > amax)
            amax = fabs (lp_a0[LP_LDA * j + i]);
        }
      if (fabs (r) > rmax)
        rmax = fabs (r);
      if (fabs (lp_x[i]) > xmax)
        xmax = fabs (lp_x[i]);
    }
  lp_resid = rmax / (LP_N * amax * xmax * 2.220446049250313e-16);
  int ok = lp_resid < 100 && fabs (lp_x[0] - 1.0) < 1e-9;
  snprintf (msg, len, "normalised residual %.2f, x[0]-1 = %.1e", lp_resid,
            lp_x[0] - 1.0);
  return ok;
}

/* ------------------------------------------------------------------ */
/* FFT: 16384-point complex double, iterative radix 2, forward then
   inverse (counted as 2 x 5 N log2 N flops). */

#define FFT_N 16384
#define FFT_LOG 14
static double *fft_re, *fft_im, *fft_re0, *fft_im0, *fft_cos, *fft_sin;
static double fft_err;

static void
fft (double *re, double *im, int inverse)
{
  for (unsigned i = 1, j = 0; i < FFT_N; i++)
    {
      unsigned bit = FFT_N >> 1;
      for (; j & bit; bit >>= 1)
        j ^= bit;
      j ^= bit;
      if (i < j)
        {
          double t = re[i];
          re[i] = re[j], re[j] = t;
          t = im[i], im[i] = im[j], im[j] = t;
        }
    }
  for (unsigned len = 2; len <= FFT_N; len <<= 1)
    {
      unsigned step = FFT_N / len, half = len / 2;
      for (unsigned i = 0; i < FFT_N; i += len)
        for (unsigned k = 0; k < half; k++)
          {
            double wr = fft_cos[k * step], wi = inverse ? fft_sin[k * step]
              : -fft_sin[k * step];
            double *ar = re + i + k, *ai = im + i + k;
            double tr = ar[half] * wr - ai[half] * wi;
            double ti = ar[half] * wi + ai[half] * wr;
            ar[half] = *ar - tr, ai[half] = *ai - ti;
            *ar += tr, *ai += ti;
          }
    }
  if (inverse)
    for (unsigned i = 0; i < FFT_N; i++)
      re[i] /= FFT_N, im[i] /= FFT_N;
}

static int
fft_setup (void)
{
  fft_re = malloc (FFT_N * 8), fft_im = malloc (FFT_N * 8);
  fft_re0 = malloc (FFT_N * 8), fft_im0 = malloc (FFT_N * 8);
  fft_cos = malloc (FFT_N / 2 * 8), fft_sin = malloc (FFT_N / 2 * 8);
  if (!fft_re || !fft_im || !fft_re0 || !fft_im0 || !fft_cos || !fft_sin)
    return -1;
  for (int i = 0; i < FFT_N / 2; i++)
    {
      fft_cos[i] = cos (2 * M_PI * i / FFT_N);
      fft_sin[i] = sin (2 * M_PI * i / FFT_N);
    }
  uint32_t s = 5;
  for (int i = 0; i < FFT_N; i++)
    {
      fft_re0[i] = (rng32 (&s) % 2000) / 1000.0 - 1.0;
      fft_im0[i] = (rng32 (&s) % 2000) / 1000.0 - 1.0;
    }
  return 0;
}

static void
fft_teardown (void)
{
  free (fft_re), free (fft_im), free (fft_re0), free (fft_im0);
  free (fft_cos), free (fft_sin);
}

static uint32_t
fft_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      memcpy (fft_re, fft_re0, FFT_N * 8);
      memcpy (fft_im, fft_im0, FFT_N * 8);
      fft (fft_re, fft_im, 0);
      sum += (uint32_t) (int64_t) (fft_re[it % FFT_N] * 1000);
      fft (fft_re, fft_im, 1);
    }
  return sum;
}

static int
fft_verify (char *msg, size_t len)
{
  fft_err = 0;
  for (int i = 0; i < FFT_N; i++)
    {
      double e = fabs (fft_re[i] - fft_re0[i]) + fabs (fft_im[i] - fft_im0[i]);
      if (e > fft_err)
        fft_err = e;
    }
  snprintf (msg, len, "round trip error %.1e", fft_err);
  return fft_err < 1e-10;
}

/* ------------------------------------------------------------------ */
/* N-body (the Computer Language Benchmarks Game's 5 planets) */

typedef struct { double x, y, z, vx, vy, vz, m; } body_t;
#define NB 5
static body_t nb_b[NB];
static double nb_e0, nb_e1;
#define SOLAR_MASS (4 * M_PI * M_PI)
#define DPY 365.24

static double
nb_energy (void)
{
  double e = 0;
  for (int i = 0; i < NB; i++)
    {
      body_t *b = &nb_b[i];
      e += 0.5 * b->m * (b->vx * b->vx + b->vy * b->vy + b->vz * b->vz);
      for (int j = i + 1; j < NB; j++)
        {
          body_t *c = &nb_b[j];
          double dx = b->x - c->x, dy = b->y - c->y, dz = b->z - c->z;
          e -= b->m * c->m / sqrt (dx * dx + dy * dy + dz * dz);
        }
    }
  return e;
}

static int
nb_setup (void)
{
  static const body_t init[NB] = {
    {0, 0, 0, 0, 0, 0, SOLAR_MASS},
    {4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
     1.66007664274403694e-03 * DPY, 7.69901118419740425e-03 * DPY,
     -6.90460016972063023e-05 * DPY, 9.54791938424326609e-04 * SOLAR_MASS},
    {8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
     -2.76742510726862411e-03 * DPY, 4.99852801234917238e-03 * DPY,
     2.30417297573763929e-05 * DPY, 2.85885980666130812e-04 * SOLAR_MASS},
    {1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
     2.96460137564761618e-03 * DPY, 2.37847173959480950e-03 * DPY,
     -2.96589568540237556e-05 * DPY, 4.36624404335156298e-05 * SOLAR_MASS},
    {1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
     2.68067772490389322e-03 * DPY, 1.62824170038242295e-03 * DPY,
     -9.51592254519715870e-05 * DPY, 5.15138902046611451e-05 * SOLAR_MASS}
  };
  memcpy (nb_b, init, sizeof init);
  double px = 0, py = 0, pz = 0;
  for (int i = 0; i < NB; i++)
    px += nb_b[i].vx * nb_b[i].m, py += nb_b[i].vy * nb_b[i].m,
      pz += nb_b[i].vz * nb_b[i].m;
  nb_b[0].vx = -px / SOLAR_MASS, nb_b[0].vy = -py / SOLAR_MASS;
  nb_b[0].vz = -pz / SOLAR_MASS;
  nb_e0 = nb_energy ();
  return 0;
}

#define NB_STEPS 1000

static uint32_t
nb_run (uint32_t iters)
{
  nb_setup ();                  /* same start every run */
  for (uint32_t it = 0; it < iters; it++)
    for (int s = 0; s < NB_STEPS; s++)
      {
        const double dt = 0.01;
        for (int i = 0; i < NB; i++)
          {
            body_t *b = &nb_b[i];
            for (int j = i + 1; j < NB; j++)
              {
                body_t *c = &nb_b[j];
                double dx = b->x - c->x, dy = b->y - c->y, dz = b->z - c->z;
                double d2 = dx * dx + dy * dy + dz * dz;
                double mag = dt / (d2 * sqrt (d2));
                b->vx -= dx * c->m * mag, b->vy -= dy * c->m * mag;
                b->vz -= dz * c->m * mag;
                c->vx += dx * b->m * mag, c->vy += dy * b->m * mag;
                c->vz += dz * b->m * mag;
              }
          }
        for (int i = 0; i < NB; i++)
          nb_b[i].x += dt * nb_b[i].vx, nb_b[i].y += dt * nb_b[i].vy,
            nb_b[i].z += dt * nb_b[i].vz;
      }
  nb_e1 = nb_energy ();
  return (uint32_t) (int64_t) (nb_b[1].x * 1e6);
}

static int
nb_verify (char *msg, size_t len)
{
  double drift = fabs ((nb_e1 - nb_e0) / nb_e0);
  snprintf (msg, len, "energy %.9f -> %.9f", nb_e0, nb_e1);
  return isfinite (nb_e1) && drift < 1e-3;
}

/* ------------------------------------------------------------------ */
/* Mandelbrot: 320 x 240, up to 256 iterations, single precision */

static uint32_t mb_total;

static uint32_t
mb_run (uint32_t iters)
{
  uint32_t total = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      total = 0;
      for (int py = 0; py < 240; py++)
        for (int px = 0; px < 320; px++)
          {
            float cr = -2.2f + px * (3.2f / 320), ci = -1.2f + py * (2.4f / 240);
            float zr = 0, zi = 0;
            int n = 0;
            while (n < 256 && zr * zr + zi * zi < 4.0f)
              {
                float t = zr * zr - zi * zi + cr;
                zi = 2 * zr * zi + ci;
                zr = t;
                n++;
              }
            total += (uint32_t) n;
          }
    }
  mb_total = total;
  return total;
}

static int
mb_verify (char *msg, size_t len)
{
  snprintf (msg, len, "%u iterations per image", mb_total);
  return mb_total > 1000000;
}

/* ------------------------------------------------------------------ */
/* Matrix multiply: 128 x 128 single precision, i-k-j order */

#define MM_N 128
static float *mm_a, *mm_b, *mm_c;

static int
mm_setup (void)
{
  mm_a = malloc (MM_N * MM_N * 4), mm_b = malloc (MM_N * MM_N * 4);
  mm_c = malloc (MM_N * MM_N * 4);
  if (!mm_a || !mm_b || !mm_c)
    return -1;
  for (int i = 0; i < MM_N * MM_N; i++)
    mm_a[i] = (float) ((i % 13) - 6) / 8, mm_b[i] = (float) ((i % 7) - 3) / 4;
  return 0;
}

static void
mm_teardown (void)
{
  free (mm_a), free (mm_b), free (mm_c);
}

static uint32_t
mm_run (uint32_t iters)
{
  uint32_t sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    {
      memset (mm_c, 0, MM_N * MM_N * 4);
      for (int i = 0; i < MM_N; i++)
        for (int k = 0; k < MM_N; k++)
          {
            float a = mm_a[i * MM_N + k];
            float *c = mm_c + i * MM_N;
            const float *b = mm_b + k * MM_N;
            for (int j = 0; j < MM_N; j++)
              c[j] += a * b[j];
          }
      sum += (uint32_t) (int32_t) mm_c[(it * 31) % (MM_N * MM_N)];
    }
  return sum;
}

/* ------------------------------------------------------------------ */
/* Memory: STREAM (McCalpin) copy, scale, add, triad on 1M-element
   double arrays (8 MB each, well beyond the caches); bytes counted as
   STREAM counts them. */

#define ST_N (1024 * 1024)
static double *st_a, *st_b, *st_c;

static int
st_setup (void)
{
  if (st_a)
    return 0;
  st_a = malloc (ST_N * 8), st_b = malloc (ST_N * 8), st_c = malloc (ST_N * 8);
  if (!st_a || !st_b || !st_c)
    return -1;
  for (long i = 0; i < ST_N; i++)
    st_a[i] = 1.0, st_b[i] = 2.0, st_c[i] = 0.0;
  return 0;
}

static void
st_teardown (void)
{
  free (st_a), free (st_b), free (st_c);
  st_a = st_b = st_c = NULL;
}

static uint32_t
st_copy (uint32_t iters)
{
  for (uint32_t it = 0; it < iters; it++)
    for (long i = 0; i < ST_N; i++)
      st_c[i] = st_a[i];
  return (uint32_t) st_c[iters % ST_N];
}

static uint32_t
st_scale (uint32_t iters)
{
  const double s = 3.0;
  for (uint32_t it = 0; it < iters; it++)
    for (long i = 0; i < ST_N; i++)
      st_b[i] = s * st_c[i];
  return (uint32_t) st_b[iters % ST_N];
}

static uint32_t
st_add (uint32_t iters)
{
  for (uint32_t it = 0; it < iters; it++)
    for (long i = 0; i < ST_N; i++)
      st_c[i] = st_a[i] + st_b[i];
  return (uint32_t) st_c[iters % ST_N];
}

static uint32_t
st_triad (uint32_t iters)
{
  const double s = 3.0;
  for (uint32_t it = 0; it < iters; it++)
    for (long i = 0; i < ST_N; i++)
      st_a[i] = st_b[i] + s * st_c[i];
  return (uint32_t) st_a[iters % ST_N];
}

/* memcpy / memset of 8 MB (the C library's, as programs use it) */
static uint32_t
mem_cpy (uint32_t iters)
{
  for (uint32_t it = 0; it < iters; it++)
    {
      memcpy (st_b, st_a, ST_N * 8);
      st_a[it % ST_N] += 1.0;
    }
  return (uint32_t) st_b[iters % ST_N];
}

static uint32_t
mem_set (uint32_t iters)
{
  for (uint32_t it = 0; it < iters; it++)
    memset (st_c, (int) (it & 0x7f), ST_N * 8);
  return ((unsigned char *) st_c)[iters % ST_N];
}

/* Sequential read bandwidth at three working-set sizes: 16 KB (level 1
   cache), 256 KB (level 2), 8 MB (main memory). 8 MB read per run. */
static uint32_t
rd_sum (const uint32_t *p, size_t words, size_t total_words)
{
  uint32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
  for (size_t done = 0; done < total_words; done += words)
    for (size_t i = 0; i < words; i += 4)
      s0 += p[i], s1 += p[i + 1], s2 += p[i + 2], s3 += p[i + 3];
  return s0 + s1 + s2 + s3;
}

#define RD_TOTAL (2 * 1024 * 1024)  /* words: 8 MB */
static uint32_t rd_l1 (uint32_t it)
{ uint32_t s = 0; while (it--) s += rd_sum ((uint32_t *) st_a, 4096, RD_TOTAL); return s; }
static uint32_t rd_l2 (uint32_t it)
{ uint32_t s = 0; while (it--) s += rd_sum ((uint32_t *) st_a, 65536, RD_TOTAL); return s; }
static uint32_t rd_ram (uint32_t it)
{ uint32_t s = 0; while (it--) s += rd_sum ((uint32_t *) st_a, RD_TOTAL, RD_TOTAL); return s; }

/* Latency: chase a random cyclic chain of pointers (one cache line,
   64 bytes, per element), so each load waits for the one before. */
static uint32_t *lat_buf;
static uint32_t lat_built;
#define LAT_MAX (32 * 1024 * 1024)
#define LAT_LOADS (1024 * 1024)

static int
lat_setup (void)
{
  if (lat_buf)
    return 0;
  lat_built = 0;
  lat_buf = malloc (LAT_MAX);
  return lat_buf ? 0 : -1;
}

static void
lat_teardown (void)
{
  free (lat_buf);
  lat_buf = NULL;
  lat_built = 0;
}

static void
lat_build (uint32_t bytes)
{
  if (lat_built == bytes)
    return;
  uint32_t n = bytes / 64, s = 77;
  uint32_t *order = malloc (n * 4);
  for (uint32_t i = 0; i < n; i++)
    order[i] = i;
  for (uint32_t i = n - 1; i > 0; i--)   /* Fisher-Yates */
    {
      uint32_t j = rng32 (&s) % (i + 1), t = order[i];
      order[i] = order[j], order[j] = t;
    }
  for (uint32_t i = 0; i < n; i++)
    lat_buf[order[i] * 16] = order[(i + 1) % n] * 16;
  free (order);
  lat_built = bytes;
}

static uint32_t
lat_chase (uint32_t iters, uint32_t bytes)
{
  lat_build (bytes);
  uint32_t p = 0;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < LAT_LOADS; i += 8)
      {
        p = lat_buf[p]; p = lat_buf[p]; p = lat_buf[p]; p = lat_buf[p];
        p = lat_buf[p]; p = lat_buf[p]; p = lat_buf[p]; p = lat_buf[p];
      }
  return p;
}

static uint32_t lat_16k (uint32_t it) { return lat_chase (it, 16 * 1024); }
static uint32_t lat_256k (uint32_t it) { return lat_chase (it, 256 * 1024); }
static uint32_t lat_4m (uint32_t it) { return lat_chase (it, 4 * 1024 * 1024); }
static uint32_t lat_32m (uint32_t it) { return lat_chase (it, LAT_MAX); }

/* malloc/free: a mix of sizes kept live in a window of 256 blocks */
static uint32_t
ma_run (uint32_t iters)
{
  static void *slot[256];
  uint32_t s = 3, sum = 0;
  for (uint32_t it = 0; it < iters; it++)
    for (int i = 0; i < 1000; i++)
      {
        uint32_t r = rng32 (&s);
        int k = r & 255;
        free (slot[k]);
        size_t sz = 16 + ((r >> 8) % ((r >> 20) & 1 ? 4096 : 128));
        slot[k] = malloc (sz);
        ((unsigned char *) slot[k])[0] = (unsigned char) i;
        sum += (uint32_t) sz;
      }
  for (int k = 0; k < 256; k++)
    free (slot[k]), slot[k] = NULL;
  return sum;
}

/* ------------------------------------------------------------------ */

const test_t cpu_tests[] = {
  {"int.coremark", "CoreMark-style mix (list, matrix, state, CRC)", "iter/s",
   1, cm_setup, cm_run, NULL, NULL, 0},
  {"int.compress", "LZ77 compress 1 MB text", "MB/s", LZ_IN / MB,
   lz_setup, lzc_run, lz_verify, lz_teardown, 0},
  {"int.decompress", "LZ77 decompress 1 MB text", "MB/s", LZ_IN / MB,
   lz_setup, lzd_run, lz_verify, lz_teardown, 0},
  {"int.sha256", "SHA-256 of 1 MB", "MB/s", SHA_BUF / MB,
   sha_setup, sha_run, sha_verify, sha_teardown, 0},
  {"int.crc32", "CRC-32 of 1 MB (table)", "MB/s", SHA_BUF / MB,
   sha_setup, crc_run, crc_verify, sha_teardown, 0},
  {"int.sort", "Quicksort 256K 32-bit integers", "Melem/s", SORT_N / 1e6,
   sort_setup, sort_run, sort_verify, sort_teardown, 0},
  {"int.wordfreq", "Word frequency (hash table) of 1 MB", "MB/s", LZ_IN / MB,
   wf_setup, wf_run, NULL, wf_teardown, 0},
  {"int.queens", "N-queens 10x10 (recursion)", "solves/s", 1,
   NULL, nq_run, nq_verify, NULL, 0},
  {"fp.whetstone", "Whetstone (double)", "MWIPS", 1,
   NULL, wt_run, wt_verify, NULL, 0},
  {"fp.linpack", "LINPACK 200x200 (double)", "MFLOPS",
   (2.0 / 3.0 * LP_N * LP_N * LP_N + 2.0 * LP_N * LP_N) / 1e6,
   lp_setup, lp_run, lp_verify, lp_teardown, 0},
  {"fp.fft", "FFT 16K-point complex (double)", "MFLOPS",
   2.0 * 5.0 * FFT_N * FFT_LOG / 1e6, fft_setup, fft_run, fft_verify,
   fft_teardown, 0},
  {"fp.nbody", "N-body, 5 bodies (double)", "Ksteps/s", NB_STEPS / 1e3,
   nb_setup, nb_run, nb_verify, NULL, 0},
  {"fp.mandelbrot", "Mandelbrot 320x240 (single)", "Mpixel/s", 320 * 240 / 1e6,
   NULL, mb_run, mb_verify, NULL, 0},
  {"fp.matmul", "Matrix multiply 128x128 (single)", "MFLOPS",
   2.0 * MM_N * MM_N * MM_N / 1e6, mm_setup, mm_run, NULL, mm_teardown, 0},
  {"mem.copy", "STREAM-style copy (8 MB arrays)", "MB/s", 16.0 * ST_N / MB,
   st_setup, st_copy, NULL, st_teardown, 0},
  {"mem.scale", "STREAM-style scale", "MB/s", 16.0 * ST_N / MB, st_setup, st_scale, NULL, st_teardown, 0},
  {"mem.add", "STREAM-style add", "MB/s", 24.0 * ST_N / MB, st_setup, st_add, NULL,
   st_teardown, 0},
  {"mem.triad", "STREAM-style triad", "MB/s", 24.0 * ST_N / MB, st_setup, st_triad, NULL, st_teardown, 0},
  {"mem.memcpy", "memcpy 8 MB", "MB/s", 8.0 * ST_N / MB, st_setup, mem_cpy, NULL,
   st_teardown, 0},
  {"mem.memset", "memset 8 MB", "MB/s", 8.0 * ST_N / MB, st_setup, mem_set, NULL,
   st_teardown, 0},
  {"mem.read16k", "Read, 16 KB working set (L1)", "MB/s", 4.0 * RD_TOTAL / MB,
   st_setup, rd_l1, NULL, st_teardown, 0},
  {"mem.read256k", "Read, 256 KB working set (L2)", "MB/s",
   4.0 * RD_TOTAL / MB, st_setup, rd_l2, NULL, st_teardown, 0},
  {"mem.read8m", "Read, 8 MB working set (RAM)", "MB/s", 4.0 * RD_TOTAL / MB,
   st_setup, rd_ram, NULL, st_teardown, 0},
  {"mem.lat16k", "Latency, 16 KB random chain", "ns", LAT_LOADS,
   lat_setup, lat_16k, NULL, lat_teardown, T_LOWER_BETTER},
  {"mem.lat256k", "Latency, 256 KB random chain", "ns", LAT_LOADS,
   lat_setup, lat_256k, NULL, lat_teardown, T_LOWER_BETTER},
  {"mem.lat4m", "Latency, 4 MB random chain", "ns", LAT_LOADS,
   lat_setup, lat_4m, NULL, lat_teardown, T_LOWER_BETTER},
  {"mem.lat32m", "Latency, 32 MB random chain", "ns", LAT_LOADS,
   lat_setup, lat_32m, NULL, lat_teardown, T_LOWER_BETTER},
  {"mem.malloc", "malloc/free, mixed sizes", "Kops/s", 1000 / 1e3,
   NULL, ma_run, NULL, NULL, 0},
};
const size_t n_cpu_tests = sizeof cpu_tests / sizeof cpu_tests[0];
