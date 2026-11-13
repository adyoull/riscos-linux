/* NativeSupport: the patch table: which words were rewritten to UDF, and what they were.
   Part of native.c (included there; not compiled on its own). */
/* ------------------------------------------------------------------------
   The patch table: UDF #(UDF_BASE + i) stands for sites[i].orig at
   sites[i].addr. */
#define SITES_MAX	16384
#define UDF_BASE	0x1000
#define KIND_PSR	1	/* emulated: MRS/MSR CPSR, CPS, MRS/MSR SPSR */
#define KIND_GETDENTS	2	/* BL __PVirt_LinuxSyscall for getdents64 */
#define KIND_CACHEFLUSH	3	/* BL __PVirt_LinuxSyscall for cacheflush */
#define KIND_FULL_BEGIN	4	/* the kernel's do_IMB_Full: mov r1, #0 */
#define KIND_FULL_END	5	/* and its ldmfd sp!, {r0-r3, ip, pc} */
#define KIND_LUR	6	/* LoadUsrRegisters' first instruction */
#define KIND_ABTDUMP	7	/* the abort register dump's switch back to ABT */
#define KIND_DAADDR	8	/* AllocateAreaAddress's mmap2: where a new area goes */
/* The kernel's abort handling copies the aborted mode's r8-r14 by
   switching to that mode (SYS for a program), then back to ABT the port's
   way: push {r0-r3, ip, lr}; mov r1, #&D7; bl SetCPSR_c_unstack; msr. That
   push is on the program's stack: if its sp is what went wrong (a stack
   overflow, a crash with sp junk), it faults inside the abort handling,
   which starts again, until the ABT stack runs out (NetSurf, POV-Ray). Done
   here instead, without a stack. */
static unsigned ABTDUMP_SW;	/* found by find_kernel_sites: 5.29 &20824624, 5.31 &208248B0 */
static unsigned DAADDR_BL;	/* BL __PVirt_LinuxSyscall (mmap2 NULL, max): 5.29 &20817DAC, 5.31 &20817F30 */
static unsigned IMB_FULL;	/* do_IMB_Full: 5.29 &208169E0, 5.31 &20816A8C */

struct site { unsigned addr, orig, kind; };
static struct site *sites;
/* per site: how often it was hit, and where last (*NativeSupport lists the
   busiest); and traps that weren't sites, by pc */
static unsigned *site_hits;
struct other_trap { unsigned pc, insn, n; };
#define OTHER_N 32
static struct other_trap other_traps[OTHER_N];
/* and traps of instructions whose condition failed (skipped), by pc: some
   processors trap these, and each costs a signal */
static struct other_trap skip_traps[OTHER_N];
static unsigned reset_cs;	/* when the counts started (monotonic cs) */
static unsigned mono_cs (void);

static void
trap_count (struct other_trap *t, unsigned pc, unsigned w)
{
  int k, e = -1;
  for (k = 0; k < OTHER_N; k++)
    if (t[k].pc == pc)
      break;
    else if (e < 0 && !t[k].n)
      e = k;
  if (k == OTHER_N && e >= 0)
    k = e;
  else if (k == OTHER_N)
    {
      /* full: the new place takes the least-counted one's slot and count
	 (the "space-saving" count: a busy place always gets in, its count
	 at most that much over) */
      k = 0;
      for (int j = 1; j < OTHER_N; j++)
	if (t[j].n < t[k].n)
	  k = j;
    }
  t[k].pc = pc;
  t[k].insn = w;
  t[k].n++;
}
static unsigned nsites;

static unsigned
udf (unsigned imm)
{
  return 0xe7f000f0u | ((imm >> 4) << 8) | (imm & 15);
}

static int
udf_imm (unsigned w)
{
  if ((w & 0xfff000f0u) != 0xe7f000f0u)
    return -1;
  return (int) (((w >> 8) & 0xfff) << 4 | (w & 15));
}

/* The site for UDF #n at pc. A PSR instruction doesn't depend on where it
   is, so a copy of patched code (a module copying a routine into its
   workspace) works too; the others are where they were patched. */
static struct site *
site_at (unsigned pc, unsigned w)
{
  int imm = udf_imm (w);
  if (imm < UDF_BASE || (unsigned) (imm - UDF_BASE) >= nsites)
    return NULL;
  struct site *s = &sites[imm - UDF_BASE];
  return s->addr == pc || s->kind == KIND_PSR ? s : NULL;
}

/* (Sites aren't reused: application space's belong to tasks that may be
   paged out. 16384 of them is far more than a desktop's.) */
static int debug, debug_faults;
/* who synchronised application space (sync_patch, *NativeSupport) */
struct syncwho { unsigned caller, cao, lo, hi, n, sites, s_addr, s_orig; char what; };
#define SYNCWHO_MAX 48
static struct syncwho syncwho[SYNCWHO_MAX];
static unsigned nsyncwho, syncwho_lost, syncwho_logged;
static int syncwho_cur = -1;	/* the row whose sync is being patched */
static unsigned app_sites;	/* sites made in application space (ever) */
static void dbg (const char *what, unsigned a, unsigned b, unsigned c);
#define SITE_HASH 32768u		/* (a power of 2, twice SITES_MAX) */
static unsigned short *site_hash;	/* site index + 1, by address and word */
static unsigned sites_reused;
static int
site_add (unsigned *p, unsigned kind)
{
  /* (0.43) The same instruction at the same address again (a program
     run again: the DDE's ROM build made about 930 new sites in each
     short build, 4000 in a full one, and 16384 is all there are; after
     that programs' privileged instructions were left as they were):
     its site again. Any task still holding that UDF there means the
     same instruction. */
  unsigned i, h = 0;
  if (site_hash)
    {
      h = (((unsigned) p >> 2) * 2654435761u ^ *p * 40503u) >> 17;
      for (int k = 0; k < 8; k++)
	{
	  unsigned v = site_hash[(h + k) & (SITE_HASH - 1)];
	  if (!v)
	    break;
	  struct site *t = &sites[v - 1];
	  if (t->addr == (unsigned) p && t->orig == *p && t->kind == kind)
	    {
	      if ((unsigned) p < 0x20000000u && syncwho_cur >= 0)
		syncwho[syncwho_cur].sites++;
	      *p = udf (UDF_BASE + v - 1);
	      sites_reused++;
	      return 1;
	    }
	}
    }
  i = nsites;
  if (i == SITES_MAX)
    {
      ns.full++;
      return 0;
    }
  nsites++;
  if (site_hash)
    for (int k = 0; k < 8; k++)
      if (!site_hash[(h + k) & (SITE_HASH - 1)])
	{
	  site_hash[(h + k) & (SITE_HASH - 1)] = i + 1;
	  break;
	}
  {
    static int app_logged;
    if (debug_faults && (unsigned) p < 0x20000000u && app_logged < 80)
      {
	app_logged++;
	dbg ("fault-site", (unsigned) p, *p, kind);
      }
  }
  if ((unsigned) p < 0x20000000u)
    app_sites++;
  if (syncwho_cur >= 0 && (unsigned) p < 0x20000000u)
    {
      struct syncwho *w = &syncwho[syncwho_cur];
      if (!w->sites++)
	w->s_addr = (unsigned) p, w->s_orig = *p;
    }
  sites[i].addr = (unsigned) p;
  sites[i].orig = *p;
  sites[i].kind = kind;
  *p = udf (UDF_BASE + i);
  return 1;
}
