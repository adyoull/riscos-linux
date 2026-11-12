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
	.word	0x5AF40				@ SWI chunk (allocated by ROOL, 7 Nov 2026)
	.word	swi_handler - module_start
	.word	swi_names - module_start
	.word	0
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"NativeSupport"
help:	.asciz	"NativeSupport\t0.48 (12 Nov 2026) RISC OS for Linux, native"
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
	.asciz	"*NativeSupport shows what NativeSupport does for RISC OS running natively on Linux: the privileged instructions patched and emulated, and the other calls it handles. *NativeSupport FastSWI On|Off turns the fast SWI entry (riscos-linux kernels) on or off, *NativeSupport FastFPA On|Off the fast FPA entry (FPEmulator's instructions), *NativeSupport SWIStubs On|Off the ROM's busiest SWIs made without a trap. *NativeSupport Reset starts the counts again.\rSyntax: *NativeSupport [FastSWI On|Off|FastFPA On|Off|SWIStubs On|Off|Reset]"
	.align	2
flags:	.word	1				@ 32-bit compatible
swi_names:
	.asciz	"NativeSupport"
	.asciz	"PMPOp"
	.asciz	"AbortReturn"
	.byte	0
	.align	2
@ NativeSupport_PMPOp (&5AF40) and NativeSupport_AbortReturn (&5AF41) are
@ answered on the SWI vector (ns_swi_vector, below), before the kernel looks
@ for a module: this handler sees one only from Thumb code or with
@ NativeSupport not active (RISC OS under QEMU answers them in QEMU itself).
swi_handler:
	adr	r0, swi_err
	mov	r12, #0x80000000
	cmp	r12, #1				@ V set
	mov	pc, lr
swi_err:
	.word	0x823803
	.asciz	"NativeSupport isn't active, or the SWI came from Thumb code"
	.align	2

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

@ swis_fixlr (0.42-0.45): r0-r3 free (saved), lr the SWI's return address.
@ If the word before lr is no longer the SWI but a branch to its stub (the
@ site given its stub while this SWI was on its way; since 0.43 stubs are
@ only made when none can be, so this is a second line), the kernel would
@ read a SWI number out of the branch. Then lr = the stub's return: the
@ kernel reads the real SWI before it, and the SWI returns through the
@ branch there to the site + 4. Exact: the branch goes to a stub (in the
@ held pages), whose third word points into the pages after them at a SWI
@ word's end, followed by a branch back to this very lr; and the caller
@ isn't Thumb code.
	.macro	swis_fixlr
	tst	lr, #3
	bne	98f
	ldr	r0, [lr, #-4]
	and	r1, r0, #0x0e000000
	teq	r1, #0x0a000000			@ B or BL (any condition)
	bne	98f
	mov	r1, r0, lsl #8
	add	r1, lr, r1, asr #6
	add	r1, r1, #4			@ the branch's target
	ldr	r2, ns_swis_area		@ (0: no stubs)
	cmp	r2, #0
	beq	98f
	sub	r3, r1, r2
	cmp	r3, #0xf000			@ the held pages (SWIS_HELD)
	bhs	98f
	tst	r1, #15
	bne	98f
	ldr	r3, [r1, #8]			@ its return address
	sub	r0, r3, r2
	cmp	r0, #0x14000			@ the whole area (SWIS_PAGES)
	bhs	98f
	cmp	r0, #0xf000
	blo	98f
	and	r0, r3, #7
	teq	r0, #4
	bne	98f
	ldr	r0, [r3, #-4]			@ the SWI word
	and	r0, r0, #0x0f000000
	teq	r0, #0x0f000000
	bne	98f
	ldr	r0, [r3]			@ the branch back: to this lr?
	mov	r0, r0, lsl #8
	add	r0, r3, r0, asr #6
	add	r0, r0, #8
	teq	r0, lr
	bne	98f
	mrc	p15, 0, r0, c13, c0, 3
	ldr	r0, [r0, #24]			@ SPSR_svc: the caller's CPSR
	tst	r0, #0x20
	bne	98f
	mov	lr, r3
	ldr	r3, ns_swis_fixed
	add	r3, r3, #1
	str	r3, ns_swis_fixed
98:
	.endm

@ The SWI vector (OS_ClaimProcessorVector 2): SVC mode, lr = the address
@ after the SWI, the caller's registers and (in the port's vcpu) SPSR. Most
@ SWIs go straight on to the previous claimant, as they came; the few
@ NativeSupport answers - OS_Memory (&68), OS_DynamicArea (&66),
@ OS_ReadSysInfo (&58), NativeSupport_PMPOp (&5AF40) and _AbortReturn
@ (&5AF41), also through OS_CallASWI(R12) - go to ns_swi_c (C), and back to
@ the caller through the port's MOVS pc, lr routine (V in SPSR as C sets).
	.global	ns_swi_vector, ns_old_swi_vector, ns_movs_pc_lr, ns_svc_base
ns_swi_vector:
	stmfd	sp!, {r0-r3}
@ (0.41) A SWI that trapped at a site given a stub after it trapped and
@ before the kernel read it: an interrupt in the ROM's SWI_handler (the
@ vcpu still in the caller's mode, interrupts on) whose handler's SWIs
@ drained the queue and patched that site. The word before lr is then the
@ site's branch to its stub, not the SWI, and the kernel would read a SWI
@ number out of the branch (a lost BPUT# line: BASIC's SWINE OS_GBPB,
@ whose branch reads as an X SWI that fails without a word). The SWI goes
@ on through the stub: lr = its last word (the SWI before it, a branch to
@ the site + 4 after).
	swis_fixlr
3:
@ SWI sites the fast entry's hook found busy (ns_swih_tmpl, below), to be
@ given stubs: done here, in SVC mode with interrupts off (the vcpu's I
@ bit: the SWI's entry set it), never the site of this SWI (the kernel
@ reads the SWI from the word before its return address)
@ (0.45) sites put back as they were for a while (an instruction that
@ couldn't be done: site_suspend in native.c), to be made sites again
	ldr	r0, ns_fixups
	cmp	r0, #0
	bne	97f
96:
	ldr	r0, ns_swiq
	cmp	r0, #0
	ldrne	r1, [r0]
	ldrne	r2, [r0, #4]
	cmpne	r1, r2
	beq	5f
@ (0.43) only on a SWI from USR mode: then no other SWI is on its way to
@ the kernel with its site still to be read (one made from ns_swi_c, an
@ interrupt or another module on this vector is in a privileged mode),
@ so a site is never given its stub under a SWI in flight
	mrc	p15, 0, r0, c13, c0, 3
	ldr	r0, [r0, #24]			@ SPSR_svc: the caller's CPSR
	tst	r0, #0x0f
	beq	6f
5:@ The mode check: here (SVC mode) the stack must be in the SVC stack's
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
	teq	r2, #0x6e			@ OS_SynchroniseCodeAreas: who asks
	streq	lr, ns_sync_caller		@ (sync_patch: FileSwitch's are left)
	teq	r2, #0x68
	teqne	r2, #0x66
	teqne	r2, #0x58			@ OS_ReadSysInfo (9, 7: the platform name)
	teqne	r2, #0x0C			@ OS_GBPB, OS_File: writes of a program's
	teqne	r2, #0x08			@ patched words copied as data
	teqne	r2, #0x3A			@ OS_ValidateAddress (PMP areas, as OS_Memory 24)
	beq	1f
	sub	r3, r2, #0x5A000		@ NativeSupport_PMPOp (&5AF40),
	sub	r3, r3, #0xF40			@ _AbortReturn (&5AF41)
	teq	r3, #0
	teqne	r3, #1
	beq	1f
	sub	r3, r2, #0x59000		@ ARMEABISupport_MMapOp (&59D04),
	sub	r3, r3, #0xD00			@ for the fault reports
	teq	r3, #4
	beq	1f
	sub	r3, r2, #0x54000		@ Reporter's SWIs (&54C80-&54C8F)
	sub	r3, r3, #0xC80
	cmp	r3, #16
	blo	1f
8:	swis_fixlr				@ (0.42: again, just before the kernel)
	mrc	p15, 0, r0, c13, c0, 3
	ldr	r1, [r0, #24]
	msr	APSR_nzcvq, r1			@ the caller's flags, as they came
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
	stmfd	sp!, {r0-r3}
	swis_fixlr				@ (0.42: ns_swi_c's SWIs may have
	mrc	p15, 0, r0, c13, c0, 3		@ given this SWI's site its stub)
	ldr	r1, [r0, #24]
	msr	APSR_nzcvq, r1
	ldmfd	sp!, {r0-r3}
	ldr	pc, ns_old_swi_vector
2:	ldr	pc, ns_movs_pc_lr		@ CPSR = SPSR, pc = lr
97:	stmfd	sp!, {r4-r12, lr}
	mov	r4, sp
	bic	sp, sp, #7
	bl	ns_fixups_run
	mov	sp, r4
	ldmfd	sp!, {r4-r12, lr}
	b	96b
6:	stmfd	sp!, {r4-r12, lr}
	mov	r0, lr				@ this SWI's return address
	mov	r4, sp
	bic	sp, sp, #7
	bl	ns_swiq_drain
	mov	sp, r4
	ldmfd	sp!, {r4-r12, lr}
	b	5b
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
	.global	ns_swiq
ns_swiq:
	.word	0				@ the queue (0: SWI stubs off)
	.global	ns_swis_area, ns_swis_fixed
ns_swis_area:
	.word	0				@ the stub area (0: the 0.41 fix off)
ns_swis_fixed:
	.word	0				@ SWIs the fix sent through their stub
	.global	ns_fixups, ns_generr
ns_fixups:
	.word	0				@ sites waiting to be patched again
@ A plain (not X) OS_GBPB or OS_File whose write, made by NativeSupport
@ from a fixed copy, gave an error: the SWI goes on to the kernel with
@ this as its return address, so as OS_GenerateError (r0 = the error),
@ which doesn't return.
ns_generr:
	swi	0x2b				@ OS_GenerateError
	b	ns_generr
	.global	ns_sync_caller
ns_sync_caller:
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

@ SWIs without a trap (0.34): a busy SWI site in the ROM (SWI n) becomes a
@ branch (with its condition) to a 4-word stub in NativeSupport's SWI stub
@ area:
@   str  lr, scr_lr        @ the caller's lr (any mode: no stack is used)
@   bl   common            @ lr = the next word + 4
@   swi  n                 @ the SWI, where the kernel reads it ([lr - 4])
@   b    site + 4          @ the SWI's return
@ Each 4KB page of the area starts with this template (copied there, so
@ its pc-relative words are the page's own): common does what the port's
@ __PVirt_SWI does (the current mode's r13, r14 into its bank in the vcpu,
@ SPSR_svc = the flags and the vcpu's mode byte, the vcpu in SVC mode with
@ IRQs off, r13 = SVC's, r14 = the return address, pc = the SWI vector),
@ with its working values here instead of on the caller's stack (-6's
@ stubs pushed on it, which a SWI from user mode can't have). The ROM's
@ HAL holds interrupts back while the pc is in the area (its PVirt_SWIStubs
@ block, which NativeSupport sets: 5.31-20 and later), so these words
@ aren't shared with an interrupt's SWIs; from the store of the vcpu's I
@ bit on, that holds them back anyway.
@ The vcpu: +8 vectors, +13 the CPSR byte, +16 SVC's r13, r14, SPSR;
@ vmode: the ROM's __PVirt_VModeOffsets (each mode's bank).
	.global	ns_swis_tmpl, ns_swis_common, ns_swis_vmode, ns_swis_coll, ns_swis_tmpl_end
	.balign	4
ns_swis_tmpl:
scr_lr:	.word	0
scr_r0:	.word	0
scr_r1:	.word	0
scr_r2:	.word	0
scr_r3:	.word	0
scr_ret: .word	0
scr_vec: .word	0
scr_busy: .word	0			@ (0.39) calls between the common
ns_swis_coll:				@ code's start and its jump; and the
	.word	0			@ calls that found one there already
ns_swis_vmode:
	.word	0
ns_swis_common:
	str	r0, scr_r0
	str	r1, scr_r1
	str	r2, scr_r2
	str	r3, scr_r3
	ldr	r1, scr_busy			@ (0.39) another call here now?
	ldr	r2, ns_swis_coll		@ (no flags set: they're the
	add	r2, r2, r1			@ caller's, read below)
	str	r2, ns_swis_coll
	add	r1, r1, #1
	str	r1, scr_busy
	ldr	r1, [lr]			@ the return (0.45: the stub's third
						@ word has it: after its SWI word,
						@ outside the held pages)
	str	r1, scr_ret
	mrc	p15, 0, r0, c13, c0, 3		@ the vcpu
	ldrb	r1, [r0, #13]			@ its CPSR byte (mode, I, F)
	and	r2, r1, #31
	ldr	r3, ns_swis_vmode
	ldrb	r2, [r3, r2]
	add	r2, r0, r2			@ the current mode's bank
	str	sp, [r2]
	ldr	r3, scr_lr
	str	r3, [r2, #4]
	mrs	r2, APSR			@ the caller's flags (nothing here
	bic	r2, r2, #0xff			@ sets them)
	orr	r2, r2, r1
	str	r2, [r0, #24]			@ SPSR_svc
	mov	r2, #0x93			@ SVC32_mode + I32_bit
	strb	r2, [r0, #13]
	ldr	r2, [r0, #8]
	ldr	r2, [r2, #4]			@ the SWI vector
	str	r2, scr_vec
	ldr	sp, [r0, #16]			@ SVC's r13
	ldr	lr, scr_ret
	ldr	r1, scr_busy
	sub	r1, r1, #1
	str	r1, scr_busy
	ldr	r0, scr_r0
	ldr	r1, scr_r1
	ldr	r2, scr_r2
	ldr	r3, scr_r3
	ldr	pc, scr_vec
ns_swis_tmpl_end:

@ The fast SWI entry's handler while SWI stubs are on (copied into the
@ area's first page, so interrupts are held back here too): counts the
@ SWIs that trap, per site, in a table of 4096 {site, count}; a site in the
@ ROM's OS image (h_lo to h_hi, ARM code) seen h_thresh times (and every
@ 4096 after, in case it was put back) goes in the queue (64 entries:
@ +0 put, +4 taken, +8 the sites), and ns_swi_vector gives it a stub.
@ Then the ROM's own handler (h_rom), with r0-r2 (SIGSYS, siginfo,
@ ucontext) and sp as the kernel's fast entry left them: r3-r12 and lr are
@ free here, as there (it loads the caller's from the ucontext).
	.global	ns_swih_tmpl, ns_swih_lo, ns_swih_hi, ns_swih_table, ns_swih_thresh, ns_swih_queue, ns_swih_rom, ns_swih_tmpl_end
	.balign	4
ns_swih_tmpl:
	ldr	r3, [r1, #12]			@ si_call_addr: after the SWI
	ldr	r4, [r2, #32 + 16 * 4]		@ the caller's CPSR
	tst	r4, #0x20			@ Thumb: not ours
	bne	9f
	sub	r3, r3, #4
	ldr	r5, ns_swih_lo
	cmp	r3, r5
	blo	9f
	ldr	r5, ns_swih_hi
	cmp	r3, r5
	bhs	9f
	ldr	r5, ns_swih_table
	mov	r6, r3, lsr #2
	mov	r6, r6, lsl #20
	mov	r6, r6, lsr #20
	mov	r7, #4
1:	add	r8, r5, r6, lsl #3
	ldr	r9, [r8]
	teq	r9, r3
	beq	3f
	teq	r9, #0
	beq	2f
	add	r6, r6, #1
	bic	r6, r6, #0xf000
	subs	r7, r7, #1
	bne	1b
	b	9f
2:	str	r3, [r8]
	mov	r9, #1
	str	r9, [r8, #4]
	b	9f
3:	ldr	r9, [r8, #4]
	add	r9, r9, #1
	str	r9, [r8, #4]
	ldr	r10, ns_swih_thresh
	teq	r9, r10
	movne	r10, r9, lsl #20
	teqne	r10, #0
	bne	9f
	ldr	r10, ns_swih_queue
	ldr	r11, [r10]
	ldr	r12, [r10, #4]
	sub	lr, r11, r12
	cmp	lr, #64
	bhs	9f
	and	lr, r11, #63
	add	lr, r10, lr, lsl #2
	str	r3, [lr, #8]
	add	r11, r11, #1
	str	r11, [r10]
9:	ldr	pc, ns_swih_rom
ns_swih_lo:	.word	0
ns_swih_hi:	.word	0
ns_swih_table:	.word	0
ns_swih_thresh:	.word	0
ns_swih_queue:	.word	0
ns_swih_rom:	.word	0
ns_swih_tmpl_end:
