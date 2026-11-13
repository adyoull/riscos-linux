/* NativeSupport: Linux system calls, through the port's own SWI 0.
   Part of native.c (included there; not compiled on its own). */
/* ------------------------------------------------------------------------
   Linux system calls, through the port's own SWI 0 */
static int
lsys (unsigned nr, unsigned a, unsigned b, unsigned c, unsigned d,
      unsigned e, unsigned f)
{
  register unsigned r0 asm ("r0") = a;
  register unsigned r1 asm ("r1") = b;
  register unsigned r2 asm ("r2") = c;
  register unsigned r3 asm ("r3") = d;
  register unsigned r4 asm ("r4") = e;
  register unsigned r5 asm ("r5") = f;
  register unsigned r7 asm ("r7") = nr;
  register unsigned ip asm ("ip") = ns.sys_entry;
  asm volatile ("blx	ip"
		: "+r" (r0), "+r" (ip)
		: "r" (r1), "r" (r2), "r" (r3), "r" (r4), "r" (r5), "r" (r7)
		: "lr", "memory", "cc");
  return (int) r0;
}
#define lsys3(n, a, b, c) lsys ((n), (a), (b), (c), 0, 0, 0)

/* The same, before the entry is known: IXSupport_LinuxSyscall */
static int
lsys_swi (unsigned nr, unsigned a, unsigned b, unsigned c, unsigned d)
{
  regs r = {{ a, b, c, d, 0, 0, 0, nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -38;
  return (int) r.r[0];
}

static unsigned
get_tls (void)
{
  unsigned v;
  asm volatile ("mrc	p15, 0, %0, c13, c0, 3" : "=r" (v));
  return v;
}

static inline unsigned
ror32 (unsigned v, unsigned s)
{
  s &= 31;
  return s ? (v >> s) | (v << (32 - s)) : v;
}

static int
popcount16 (unsigned v)
{
  int n = 0;
  for (v &= 0xffff; v; v &= v - 1)
    n++;
  return n;
}

static int
cond_pass (unsigned insn, unsigned cpsr)
{
  int n = cpsr >> 31 & 1, z = cpsr >> 30 & 1, c = cpsr >> 29 & 1,
    v = cpsr >> 28 & 1;
  switch (insn >> 28)
    {
    case 0: return z;
    case 1: return !z;
    case 2: return c;
    case 3: return !c;
    case 4: return n;
    case 5: return !n;
    case 6: return v;
    case 7: return !v;
    case 8: return c && !z;
    case 9: return !c || z;
    case 10: return n == v;
    case 11: return n != v;
    case 12: return !z && n == v;
    case 13: return z || n != v;
    default: return 1;
    }
}

/* Code that is the port's own (its ROM), unless one of the ROM modules
   emulated as disc code is */
static int
pc_emulated (unsigned pc)
{
  if (pc < ROM_LO || pc >= ROM_HI)
    return 1;
  for (int i = 0; i < ns.emul_n; i++)
    if (pc >= ns.emul_lo[i] && pc < ns.emul_hi[i])
      return 1;
  return 0;
}

