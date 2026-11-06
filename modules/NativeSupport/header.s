@ NativeSupport: what the test bed's QEMU does for RISC OS for Linux, for
@ the port running natively (straight on a processor that runs 32-bit ARM
@ programs: a Raspberry Pi 4 or 5 with a 64-bit Linux, say). See native.c.
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.syntax	unified
	.arm
	.section .text.header, "ax"
	.global	module_start
module_start:
	.word	0				@ start
	.word	cm_init - module_start
	.word	cm_final - module_start
	.word	service - module_start
	.word	title - module_start
	.word	help - module_start
	.word	commands - module_start
	.word	0				@ no SWIs
	.word	0
	.word	0
	.word	0
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"NativeSupport"
help:	.asciz	"NativeSupport\t0.21 (06 Nov 2026) RISC OS for Linux, native"
	.align	2
commands:
	.asciz	"NativeSupport"
	.align	2
	.word	cm_command_0 - module_start
	.word	0x00020000			@ 0 to 2 parameters
	.word	0				@ no syntax message
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*NativeSupport shows what NativeSupport does for RISC OS running natively on Linux: the privileged instructions patched and emulated, and the other calls it handles. *NativeSupport FastSWI On|Off turns the fast SWI entry (riscos-linux kernels) on or off, *NativeSupport FastFPA On|Off the fast FPA entry (FPEmulator's instructions). *NativeSupport Reset starts the counts again.\rSyntax: *NativeSupport [FastSWI On|Off|FastFPA On|Off|Reset]"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ Service calls (RISC OS 4 style table): Service_DynamicAreaCreate (&90)
@ and _Remove (&91), for fault reports, and Service_ModulePreInit (&B9).
	.word	service_table - module_start
service:
	mov	r0, r0				@ the "service table" marker
	teq	r1, #0xB9
	teqne	r1, #0x90
	teqne	r1, #0x91
	movne	pc, lr
service_handler:
	b	cm_service
service_table:
	.word	0				@ flags
	.word	service_handler - module_start
	.word	0x90
	.word	0x91
	.word	0xB9
	.word	0

	.include "cmodule.inc"
	CM_COMMAND 0

@ The ROM's DynArea_PMP_PhysOp, _LogOp, _Resize and _GetPages stubs are
@ replaced by "LDR pc, [pc, #-4]; DCD kpmp_N": called by the kernel in SVC
@ mode with r1 = area, r2, r3 as the operation's; return V clear, or V set
@ with r0 = error (the other flags as they came). kpmp_call (C) gets the
@ operation and the saved r0-r12.
	.macro	KPMP n
	.global	kpmp_\n
kpmp_\n:
	stmfd	sp!, {r0-r12, lr}
	mrs	r5, APSR			@ the caller's flags
	mov	r0, #\n
	mov	r1, sp
	mov	r4, sp
	bic	sp, sp, #7
	bl	kpmp_call
	mov	sp, r4
	bic	r5, r5, #0x10000000		@ V: the result
	cmp	r0, #0
	orrne	r5, r5, #0x10000000
	msr	APSR_nzcvq, r5
	ldmfd	sp!, {r0-r12, lr}
	mov	pc, lr
	.endm
	KPMP	0
	KPMP	1
	KPMP	2
	KPMP	3

@ The SWI vector (OS_ClaimProcessorVector 2): SVC mode, lr = the address
@ after the SWI, the caller's registers and (in the port's vcpu) SPSR. Most
@ SWIs go straight on to the previous claimant, as they came; the few
@ NativeSupport answers - OS_Memory (&68), OS_DynamicArea (&66),
@ OS_ReadSysInfo (&58), &C0002,
@ &C0003, also through OS_CallASWI(R12) - go to ns_swi_c (C), and back to
@ the caller through the port's MOVS pc, lr routine (V in SPSR as C sets).
	.global	ns_swi_vector, ns_old_swi_vector, ns_movs_pc_lr, ns_svc_base
ns_swi_vector:
	stmfd	sp!, {r0-r3}
@ The mode check: here (SVC mode) the stack must be in the SVC stack's
@ megabyte, as the C library's SWI veneers assume (sl = sp >> 20 << 20).
@ If not, RISC OS's mode and its stack disagree: ns_mode_bad (C) counts
@ and reports it, and the SWI goes on as it came.
	ldr	r0, ns_svc_base
	cmp	r0, #0
	beq	9f
	eor	r1, r0, sp
	movs	r1, r1, lsr #20
	bne	7f
9:	mrc	p15, 0, r0, c13, c0, 3		@ the vcpu
	ldr	r1, [r0, #24]			@ SPSR_svc: the caller's CPSR
	tst	r1, #0x20			@ Thumb: not ours
	bne	8f
	ldr	r2, [lr, #-4]			@ the SWI
	bic	r2, r2, #0xff000000
	bic	r2, r2, #0x20000		@ (X bit)
	teq	r2, #0x71			@ OS_CallASWIR12
	moveq	r2, r12
	teq	r2, #0x6f			@ OS_CallASWI
	moveq	r2, r10
	bic	r2, r2, #0xff000000
	bic	r2, r2, #0x20000
	teq	r2, #0x68
	teqne	r2, #0x66
	teqne	r2, #0x58			@ OS_ReadSysInfo (9, 7: the platform name)
	beq	1f
	sub	r3, r2, #0xC0000
	teq	r3, #2
	teqne	r3, #3
	beq	1f
	sub	r3, r2, #0x59000		@ ARMEABISupport_MMapOp (&59D04),
	sub	r3, r3, #0xD00			@ for the fault reports
	teq	r3, #4
	beq	1f
	sub	r3, r2, #0x54000		@ Reporter's SWIs (&54C80-&54C8F)
	sub	r3, r3, #0xC80
	cmp	r3, #16
	blo	1f
8:	msr	APSR_nzcvq, r1			@ the caller's flags, as they came
	ldmfd	sp!, {r0-r3}
	ldr	pc, ns_old_swi_vector
1:	ldmfd	sp!, {r0-r3}
	stmfd	sp!, {r0-r12, lr}
	mov	r0, sp				@ r0-r12, lr (the return address)
	mov	r4, sp
	bic	sp, sp, #7
	bl	ns_swi_c			@ 0: not after all
	mov	sp, r4
	cmp	r0, #0
	ldmfd	sp!, {r0-r12, lr}
	bne	2f
	stmfd	sp!, {r0-r1}
	mrc	p15, 0, r0, c13, c0, 3
	ldr	r1, [r0, #24]
	msr	APSR_nzcvq, r1
	ldmfd	sp!, {r0-r1}
	ldr	pc, ns_old_swi_vector
2:	ldr	pc, ns_movs_pc_lr		@ CPSR = SPSR, pc = lr
7:	stmfd	sp!, {r4, r12, lr}
	add	r0, sp, #12			@ the caller's r0-r3
	mov	r1, lr				@ the address after the SWI
	mov	r4, sp
	bic	sp, sp, #7
	bl	ns_mode_bad
	mov	sp, r4
	ldmfd	sp!, {r4, r12, lr}
	b	9b
@ GCCSDK ARMEABISupport 1.05's armeabi_mmap makes a new mmap allocator
@ (and area) of a fixed 100MB, so one mmap of more fails. Patched there
@ (aeabi105_fix in native.c) to call these:
@ aeabi_size, for its "mov r1, #&6400000" (r5 = the pages wanted; lr and
@ the flags are free there): r1 = max (100MB, r5 pages). Bigger than
@ 100MB, RISC OS's clamp on a new area's maximum (DALimit, 128MB by
@ default) is lifted to the size for the area that's made next;
@ aeabi_after, for the "cmp r0, #0" after that call: puts the clamp back.
	.global	aeabi_size, aeabi_after
aeabi_size:
	mov	r1, r5, lsl #12
	cmp	r1, #0x6400000
	movls	r1, #0x6400000
	movls	pc, lr
	stmfd	sp!, {r0, r2, r3, lr}
	str	r1, aeabi_len
	mov	r2, r1
	mov	r0, #8
	mov	r1, #0
	mov	r3, #0
	swi	0x20066				@ XOS_DynamicArea 8: clamps
	movvs	r2, #0
	str	r2, aeabi_clamp			@ the old one (0: none set)
	ldr	r1, aeabi_len
	ldmfd	sp!, {r0, r2, r3, pc}
aeabi_after:
	stmfd	sp!, {r0-r3, lr}
	ldr	r2, aeabi_clamp
	cmp	r2, #0
	beq	1f
	mov	r0, #8
	mov	r1, #0
	mov	r3, #0
	swi	0x20066
	mov	r2, #0
	str	r2, aeabi_clamp
1:	ldmfd	sp!, {r0-r3, lr}
	cmp	r0, #0
	mov	pc, lr
aeabi_len:
	.word	0
aeabi_clamp:
	.word	0
ns_svc_base:
	.word	0
ns_old_swi_vector:
	.word	0
ns_movs_pc_lr:
	.word	0

@ int probe_call (int (*fn) (void *), void *arg): fn (arg), or 0 if it
@ faulted. The fault handler sends a fault in this module's code, while
@ probe_sp is set, to probe_recover (a signal handler's own memory accesses
@ to addresses the interrupted code gave it: a fault there must fail the
@ emulation, not kill RISC OS). Nests: the port's signal handlers run with
@ SA_NODEFER, so an interrupt can start another emulation inside one; the
@ previous probe_sp is kept on the stack.
	.global	probe_call, probe_recover, probe_sp
probe_call:
	stmfd	sp!, {r4-r11, lr}
	ldr	r3, probe_sp
	str	r3, [sp, #-4]!			@ (10 words: 8-byte aligned)
	str	sp, probe_sp
	mov	r2, r0
	mov	r0, r1
	blx	r2
	ldr	r3, [sp], #4
	str	r3, probe_sp
	ldmfd	sp!, {r4-r11, pc}
probe_recover:
	ldr	sp, probe_sp
	ldr	r3, [sp], #4
	str	r3, probe_sp
	mov	r0, #0
	ldmfd	sp!, {r4-r11, pc}
probe_sp:
	.word	0

@ The fast FPA entry (riscos-linux's kernels, prctl PR_RISCOS_UND): an FPA
@ instruction (coprocessor 1 or 2) comes here straight from Linux's
@ undefined instruction trap, with r1 a siginfo and r2 a ucontext (r0-r15,
@ the CPSR) on the signal stack, as for a SIGILL but without the signal.
@ This does what the port's undefined_signal_handler does (its
@ ENTER_EXCEPTION(UND, 0, 4)), then goes to RISC OS's undefined
@ instruction vector (FPEmulator) itself, as the port's SWI_handler does
@ for the SVC vector, instead of returning through sigreturn:
@   the current mode's r13, r14 into its bank; UND's SPSR = the flags and
@   the vcpu's mode; the vcpu in UND mode, IRQs off; r13 = UND's, r14 =
@   the instruction + 4, r0-r12 as they were; pc = vectors[0].
@ The kernel has already set the vcpu's I bit (and passes the old CPSR
@ byte as si_errno), so an interrupt signal arriving here is deferred by
@ the port's handler, as one arriving in its SWI_handler is: taken here, the
@ RISC OS IRQ code's SWIs could overwrite this frame before it is read.
@ ns_vmode_offsets: the ROM's __PVirt_VModeOffsets (from its SWI_handler).
@ The vcpu's layout: +8 vectors, +13 cpsr, +16 SVC, ... +64 UND
@ {r13, r14, spsr}.
	.global	ns_und_fast, ns_vmode_offsets
ns_und_fast:
	mrc	p15, 0, r0, c13, c0, 3		@ TPIDRURO: the vcpu
	ldr	r6, [r1, #4]			@ vcpu->cpsr before the trap (its mode;
						@ the kernel has set its I bit)
	and	lr, r6, #31
	ldr	r3, ns_vmode_offsets
	ldrb	lr, [r3, lr]
	add	lr, lr, r0			@ the current mode's bank
	add	r3, r2, #32 + 12 * 4		@ &uc r12
	ldmia	r3, {r2, r4, r5, r7, r8}	@ r12, r13, r14, pc, CPSR
	stmia	lr, {r4, r5}			@ bank r13, r14 (before reading UND's)
	bic	r8, r8, #0xDF
	orr	r8, r8, r6
	str	r8, [r0, #64 + 8]		@ UND's SPSR
	ldr	r12, [r0, #64]			@ UND's r13
	mov	r1, #0x9B			@ UND32_mode + I32_bit
	strb	r1, [r0, #13]
	ldr	r1, [r0, #8]			@ vcpu->vectors
	ldr	r4, [r1]			@ the undefined instruction vector
						@ (vectors[0], as the port's handler)
	stmfd	r12!, {r2, r4}
	add	lr, r7, #4			@ r14_und = the instruction + 4
	sub	r3, r3, #12 * 4
	ldmia	r3, {r0-r11}
	mov	sp, r12
	ldmfd	sp!, {r12, pc}
ns_vmode_offsets:
	.word	0
