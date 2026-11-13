/* NativeSupport: starting: finding the ROM's routines, the handlers, *NativeSupport.
   Part of native.c (included there; not compiled on its own). */
/* ------------------------------------------------------------------------
   Starting */
ERROR_BLOCK (bad_rom, 0x823800, "NativeSupport: this isn't the ROM it knows (RISC OS 5.29 for Linux)");
ERROR_BLOCK (no_memory, 0x823801, "NativeSupport: not enough memory");
ERROR_BLOCK (no_kill, 0x823802, "NativeSupport can't be removed while RISC OS runs natively");
/* (0x823803: header.s, a NativeSupport SWI that reached the module's own
   SWI handler, not the SWI vector) */

extern void kpmp_0 (void), kpmp_1 (void), kpmp_2 (void), kpmp_3 (void);
extern char module_start[];

static int
mem_eq (unsigned a, const char *s, unsigned n)
{
  for (unsigned i = 0; i < n; i++)
    if (*(volatile char *) (a + i) != s[i])
      return 0;
  return 1;
}

/* The ROM: its OS image is mapped from 0x20810000 to 0x20C00000 */

static void
find_kpmp_stubs (void)
{
  /* (no pointers in initialised data: only the GOT is relocated) */
  static const char names[4][24] = {
    "DynArea_PMP_PhysOp\r\n", "DynArea_PMP_LogOp\r\n",
    "DynArea_PMP_Resize\r\n", "DynArea_PMP_GetPages\r\n",
  };
  static const char bad[] = "\xea\x01\x00\x00" "BadParm";
  for (int n = 0; n < 4; n++)
    {
      unsigned len = strlen (names[n]);
      for (unsigned a = ROMOS_LO + 8; a + len <= ROMOS_HI; a += 4)
	if (*(volatile char *) a == 'D' && mem_eq (a, names[n], len)
	    && RD (a - 8) == 0xe52de004u)	/* push {lr} */
	  {
	    kpmp_stub[n] = a - 8;
	    break;
	  }
    }
  for (unsigned a = ROMOS_LO; a + sizeof bad <= ROMOS_HI; a += 4)
    if (*(volatile char *) a == '\xea' && mem_eq (a, bad, sizeof bad))
      {
	kpmp_err = a;
	break;
      }
}

static void
patch_kpmp_stubs (void)
{
  for (int n = 0; n < 4; n++)
    {
      unsigned a = kpmp_stub[n];
      void (*fn) (void) = n == 0 ? kpmp_0 : n == 1 ? kpmp_1 : n == 2 ? kpmp_2 : kpmp_3;
      if (!a || !kpmp_err)
	continue;
      if (!rom_writable (a, a + 8, 1))
	continue;
      RD (a) = 0xe51ff004u;			/* ldr pc, [pc, #-4] */
      RD (a + 4) = (unsigned) fn;
      rom_writable (a, a + 8, 0);
      sync_code (a, a + 8);
    }
}

/* IXFS's getdents64: "mov r7, #217" then, a few instructions on, "bl
   __PVirt_LinuxSyscall": the BL is patched */
static void
patch_getdents (void)
{
  for (unsigned a = ROMOS_LO; a < ROMOS_HI - 32; a += 4)
    {
      if (RD (a) != 0xe3a070d9u)
	continue;
      for (unsigned b = a + 4; b < a + 20; b += 4)
	{
	  unsigned w = RD (b);
	  if ((w & 0x0f000000u) != 0x0b000000u)
	    continue;
	  int off = (int) (w << 8) >> 6;
	  if (b + 8 + off != ns.sys_entry)
	    continue;
	  if (rom_writable (b, b + 4, 1))
	    {
	      site_add ((unsigned *) b, KIND_GETDENTS);
	      rom_writable (b, b + 4, 0);
	      sync_code (b, b + 4);
	      ns.patched_rom++;
	    }
	  break;
	}
    }
}

/* The kernel's (and HAL's) cacheflush calls: "ldr r7, =0xF0002" then,
   a few instructions on, "bl __PVirt_LinuxSyscall" */
static void
patch_cacheflush (unsigned lo, unsigned hi)
{
  for (unsigned a = lo; a < hi - 16; a += 4)
    {
      unsigned w = RD (a);
      if ((w & 0x0f7ff000u) != 0x051f7000u)
	continue;
      unsigned off = w & 0xfff;
      unsigned lit = a + 8 + ((w & (1u << 23)) ? off : -off);
      if (lit < lo || lit >= hi || RD (lit) != 0xf0002u)
	continue;
      for (unsigned b = a + 4; b < a + 16; b += 4)
	{
	  unsigned x = RD (b);
	  if ((x & 0x0f000000u) != 0x0b000000u)
	    continue;
	  int o = (int) (x << 8) >> 6;
	  if (b + 8 + o != ns.sys_entry)
	    break;
	  if (rom_writable (b, b + 4, 1))
	    {
	      site_add ((unsigned *) b, KIND_CACHEFLUSH);
	      rom_writable (b, b + 4, 0);
	      sync_code (b, b + 4);
	      ns.patched_rom++;
	      ns.flush_sites++;
	    }
	  break;
	}
    }
}

/* The kernel's do_IMB_Full (a whole-cache synchronisation: Linux's
   cacheflush over everything, data and screen memory too, which mustn't be
   scanned for code):
     stmfd sp!, {r0-r3, ip, lr}; mov r1, #0; mvn r2, #3; mov lr, pc;
     ldr pc, [pc, #&418]; ldmfd sp!, {r0-r3, ip, pc}
   Its mov and ldmfd mark the cacheflush calls between as whole ones. */
static void
patch_imb_full (void)
{
  static const unsigned sig[6] = { 0xe92d500fu, 0xe3a01000u, 0xe3e02003u,
				   0xe1a0e00fu, 0xe59ff418u, 0xe8bd900fu };
  for (int i = 0; i < 6; i++)
    if (!IMB_FULL || RD (IMB_FULL + 4 * i) != sig[i])
      return;
  if (!rom_writable (IMB_FULL, IMB_FULL + 24, 1))
    return;
  site_add ((unsigned *) (IMB_FULL + 4), KIND_FULL_BEGIN);
  site_add ((unsigned *) (IMB_FULL + 20), KIND_FULL_END);
  rom_writable (IMB_FULL, IMB_FULL + 24, 0);
  sync_code (IMB_FULL, IMB_FULL + 24);
  ns.patched_rom += 2;
  ns.imb_full = 1;
}

/* The ROM modules emulated as disc code (the sound modules), and the
   modules already loaded from disc */
static int
title_is (unsigned base, const char *t)
{
  unsigned off = RD (base + 16);
  return off && mem_eq (base + off, t, strlen (t) + 1);
}

static void
patch_modules (unsigned self)
{
  static const char sound[5][16] = {
    "SoundDMA", "SoundChannels", "SoundScheduler", "StringLib", "Percussion"
  };
  regs r = {{ 12, 0, 0 }};
  for (;;)
    {
      r.r[0] = 12;		/* r1, r2: the next module and incarnation */
      if (swix_regs (OS_Module, &r))
	break;
      unsigned base = r.r[3];
      unsigned len = RD (base - 4) - 4;
      if (base >= ROMOS_LO && base < ROMOS_HI)
	{
	  for (unsigned i = 0; i < sizeof sound / sizeof sound[0]; i++)
	    if (len < 0x100000 && title_is (base, sound[i]) && ns.emul_n < 8)
	      {
		ns.emul_lo[ns.emul_n] = base;
		ns.emul_hi[ns.emul_n] = base + len;
		ns.emul_n++;
		if (rom_writable (base, base + len, 1))
		  {
		    ns.patched_rom += patch_range (base, base + len);
		    rom_writable (base, base + len, 0);
		    sync_code (base, base + len);
		  }
	      }
	}
      else if (base != self && len < 0x1000000)
	{
	  if (module_32bit (base, len))
	    cond_nop_range (base, base + len);
	  ns.patched += patch_range (base, base + len);
	  sync_code (base, base + len);
	}
    }
  /* the ROM's never-run LDMNE ^ (and the like) after TEQ pc, pc */
  if (!nocondnop && rom_writable (ROMOS_LO, ROMOS_HI, 1))
    {
      if (cond_nop_range (ROMOS_LO, ROMOS_HI))
	sync_code (ROMOS_LO, ROMOS_HI);
      rom_writable (ROMOS_LO, ROMOS_HI, 0);
    }
  if (traptest && rom_writable (ROMOS_LO, ROMOS_HI, 1))
    {
      /* test: make the ROM's "LDMNE sp!, {...}^" after TEQ pc, pc trap,
	 as an A78C does, to try the way back from it */
      for (unsigned *p = (unsigned *) ROMOS_LO + 1;
	   p < (unsigned *) ROMOS_HI; p++)
	if (p[-1] == 0xe13f000fu && (p[0] & 0xffff0000u) == 0x18fd0000u
	    && (p[0] & (1u << 22)))
	  ns.patched_rom += site_add (p, KIND_PSR);
      rom_writable (ROMOS_LO, ROMOS_HI, 0);
      sync_code (ROMOS_LO, ROMOS_HI);
    }
}

static unsigned
read_midr (void)
{
  char b[64];
  int fd = lsys3 (SYS_open,
		  (unsigned) "/sys/devices/system/cpu/cpu0/regs/identification/midr_el1",
		  0x80000, 0);
  unsigned v = 0x410fd083u;			/* Cortex-A72 */
  if (fd >= 0)
    {
      int n = lsys3 (SYS_read, fd, (unsigned) b, sizeof b - 1);
      lsys3 (SYS_close, fd, 0, 0);
      if (n > 2)
	{
	  b[n] = 0;
	  const char *s = b + (b[0] == '0' && b[1] == 'x' ? 2 : 0);
	  unsigned long long x = 0;
	  for (; *s; s++)
	    {
	      char c = *s;
	      if (c >= '0' && c <= '9') x = x * 16 + (c - '0');
	      else if (c >= 'a' && c <= 'f') x = x * 16 + (c - 'a' + 10);
	      else break;
	    }
	  if (x)
	    v = (unsigned) x;
	}
    }
  return v;
}

static int
set_handler (int sig, sighandler *h, struct ksigaction *old)
{
  if (lsys (SYS_rt_sigaction, sig, 0, (unsigned) old, 8, 0, 0) < 0
      || old->restorer + 4 != ns.sys_entry)
    return 0;
  struct ksigaction a = *old;
  a.handler = (unsigned) h;
  return lsys (SYS_rt_sigaction, sig, (unsigned) &a, 0, 8, 0, 0) == 0;
}

extern void ns_swi_vector (void);
extern unsigned ns_old_swi_vector, ns_movs_pc_lr;

/* The port's PVOps HAL device (type &30A, as its InitPVOps finds it): its
   routines from +64 (PatchCode, FastSWI, SWI, LinuxSyscall, GetCPSR,
   GetSPSR, LoadSaveBlock, LoadUsr, LoadUsrRegisters, MOVS_PC_LR...) */
static int
claim_swi_vector (void)
{
  regs h = {{ 0x30A, 0, 0, 0, 0, 0, 0, 0, 5 }};
  if (swix_regs (OS_Hardware, &h) || h.r[1] == 0xffffffffu || !h.r[2])
    return 0;
  unsigned dev = h.r[2];
  if (RD (dev + 76) != ns.sys_entry)		/* its LinuxSyscall: ours? */
    return 0;
  ns_movs_pc_lr = RD (dev + 100);
  /* the handler now (the vcpu's vectors: +8), set before the claim: an
     interrupt can call a SWI as soon as it's made */
  unsigned *vectors = (unsigned *) RD (get_tls () + 8);
  ns_old_swi_vector = vectors[1];
  regs v = {{ 0x102, (unsigned) ns_swi_vector }};	/* claim the SWI vector */
  if (swix_regs (0x69 /* OS_ClaimProcessorVector */, &v))
    return 0;
  return v.r[1] == ns_old_swi_vector;
}

/* The hardware platform name (OS_ReadSysInfo 9, item 7): RISC OS on a
   Raspberry Pi gives the board's name; the port gives none. Running
   natively on a Raspberry Pi, the name Linux's device tree has for the
   board (e.g. "Raspberry Pi 4 Model B Rev 1.4"); on anything else, nothing
   changes. */
static void
read_pi_model (void)
{
  int fd = lsys3 (SYS_open, (unsigned) "/proc/device-tree/model", 0x80000, 0);
  if (fd < 0)
    return;
  int n = lsys3 (SYS_read, fd, (unsigned) pi_model, sizeof pi_model - 1);
  lsys3 (SYS_close, fd, 0, 0);
  if (n < 0)
    n = 0;
  pi_model[n] = 0;
  static const char pfx[] = "Raspberry Pi";
  for (unsigned i = 0; i < sizeof pfx - 1; i++)
    if (pi_model[i] != pfx[i])
      {
	pi_model[0] = 0;
	return;
      }
}

/* The fast SWI entry of riscos-linux's kernels (prctl PR_RISCOS_SWI): the
   ROM's SWIs go to its SIGSYS handler straight from Linux's system call
   entry, with the registers it reads written on its signal stack, instead
   of through seccomp and a signal (about 2 microseconds a SWI on a Pi 4).
   Other kernels refuse it, and SWIs go the usual way. */
#define PR_RISCOS_SWI	0x524f5301u

/* The fast FPA entry (prctl PR_RISCOS_UND): FPA instructions go to
   ns_und_fast (header.s) straight from Linux's undefined instruction
   trap, instead of through a SIGILL, this module's SIGILL handler and the
   port's, and a sigreturn. ns_und_fast needs the ROM's table of the vcpu's
   mode banks, found in its SWI_handler ("ADD lr, lr, #table - . - 12;
   LDRB lr, [pc, lr]") and checked. -1: not in this kernel or this ROM. */
#define PR_RISCOS_UND	0x524f5302u
extern char ns_und_fast[];
extern unsigned ns_vmode_offsets;

static unsigned
arm_imm (unsigned w)
{
  unsigned r = ((w >> 8) & 15) * 2, v = w & 255;
  return r ? (v >> r) | (v << (32 - r)) : v;
}

static int
find_vmode_offsets (void)
{
  unsigned h = ns.swi_handler;
  if (!h)
    return 0;
  for (unsigned i = 0; i < 12; i++)
    {
      unsigned a = h + i * 4;
      if ((RD (a) & 0xfffff000u) == 0xe28ee000u && RD (a + 4) == 0xe7dfe00eu)
	{
	  const volatile uint8_t *t = (const volatile uint8_t *) (a + 4 + 8 + arm_imm (RD (a)));
	  /* usr, svc, und, abt: the vcpu's banks at 28, 16, 64, 76 */
	  if (t[0x10] == 28 && t[0x13] == 16 && t[0x1b] == 64 && t[0x17] == 76)
	    {
	      ns_vmode_offsets = (unsigned) t;
	      return 1;
	    }
	  return 0;
	}
    }
  return 0;
}

static void
fastfpa_set (int on)
{
  if (ns.fastfpa < 0)
    return;
  if (on && !ns_vmode_offsets && !find_vmode_offsets ())
    {
      ns.fastfpa = -1;
      return;
    }
  if (lsys (SYS_prctl, PR_RISCOS_UND, on ? (unsigned) ns_und_fast : 0, 0, 0, 0, 0) == 0)
    ns.fastfpa = on;
  else if (on)
    ns.fastfpa = -1;
}

static void
fastswi_set (int on)
{
  if (ns.fastswi < 0)
    return;
  /* (with SWI stubs on, through their counting hook) */
  unsigned h = sw.state > 0 ? sw.area + SWIS_FIRST : ns.swi_handler;
  if (lsys (SYS_prctl, PR_RISCOS_SWI, on ? h : 0, ns.sys_entry + 4, 0, 0, 0) == 0)
    ns.fastswi = on;
  else if (on)
    ns.fastswi = -1;
}

