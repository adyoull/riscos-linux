/* NativeSupport: which words are privileged instructions at all.
   Part of native.c (included from psr.c, where it always was; scan.c uses
   it too, and so does tools/nsscan). */
/* The privileged instructions handled when they trap (failing their
   condition, some processors trap anyway: skipped, as they should be) */
static int
priv_form (unsigned w)
{
  return (w & 0x0f000f10u) == 0x0e000f10u			/* MRC/MCR p15 */
	 || (w & 0x0fbf0fffu) == 0x010f0000u			/* MRS */
	 || (w & 0x0fb0f000u) == 0x0120f000u			/* MSR reg */
	 || (w & 0x0fb0f000u) == 0x0320f000u			/* MSR imm */
	 || (w & 0x0e400000u) == 0x08400000u			/* LDM/STM ^ */
	 || (w & 0x0c10f000u) == 0x0010f000u			/* <op>S pc */
	 || (w & 0x0fb00ff0u) == 0x01000090u;			/* SWP{B} */
}
