@ NVMeDriver for RISC OS on Linux: the NVMe driver module RISC OS Open's
@ NVMeFS talks to, on Linux's own NVMe driver instead of the hardware. See
@ nvmelinux.c.
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
	.word	0				@ no service calls
	.word	title - module_start
	.word	help - module_start
	.word	commands - module_start
	.word	0x5A5C0				@ NVMeDriver's SWI chunk (RISC OS Open's)
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"NVMeDriver"
help:	.asciz	"NVMeDriver\t0.50 (16 Nov 2026) NVMe drives through Linux, for NVMeFS"
swi_names:
	.asciz	"NVMeDriver\0ControllerEnumerate\0ControllerAccessInfo\0Transfer\0GetResult\0SetSpecialPRPinfo\0QueueReset\0ControllerInfo\0"
	.align	2
commands:
	.asciz	"NVMeDevices"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*NVMeDevices lists the NVMe drives NVMeDriver is giving NVMeFS (from Linux: the machine's NVMe drives in RISC OS's own format).\rSyntax: *NVMeDevices"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ void nv_call (void (*proc) (void), void *ws, unsigned handle, unsigned id):
@ a caller's completion routine (a CMHG generic veneer): r0 = its handle,
@ r1 = the completion identifier, r12 = its workspace
	.global	nv_call
nv_call:
	stmfd	sp!, {r4-r11, lr}
	mov	r4, r0
	mov	r12, r1
	mov	r0, r2
	mov	r1, r3
	blx	r4
	ldmfd	sp!, {r4-r11, pc}

@ a transient callback: completion routines that were to be called "later"
@ (and the driver's own start-up service call)
	.global	callback_entry
callback_entry:
	stmfd	sp!, {r0-r12, lr}
	bl	c_callback
	ldmfd	sp!, {r0-r12, pc}

@ OS_MMUControl 2 (flush the caches and TLBs), which NVMeFS calls before it
@ copies data and treats an error from as a failed transfer. The port's
@ kernel has no OS_MMUControl ("SWI not known"); there is nothing to flush
@ here, so reason 2 is answered with no error. It is a kernel SWI, so it
@ is taken on the SWI vector (OS_ClaimProcessorVector 2), as MemMapEntries
@ takes its three: SVC mode, lr = the address after the SWI, the caller's
@ registers, and its CPSR in the port's vcpu (the thread register;
@ SPSR_svc at +24). Also through OS_CallASWI (r10) and OS_CallASWIR12.
@ Every other SWI, and OS_MMUControl's other reasons, go on to the
@ claimant before, as they came.
	.global	nv_swi_vector, nv_old_swi_vector, nv_movs_pc_lr
nv_swi_vector:
	stmfd	sp!, {r0-r3}
	mrc	p15, 0, r0, c13, c0, 3		@ the vcpu
	ldr	r1, [r0, #24]			@ the caller's CPSR
	tst	r1, #0x20			@ Thumb: not ours
	bne	8f
	ldr	r2, [lr, #-4]			@ the SWI
	and	r3, r2, #0x0f000000		@ (not a SWI instruction: a site
	teq	r3, #0x0f000000			@ NativeSupport is giving a stub)
	bne	8f
	bic	r2, r2, #0xff000000
	bic	r2, r2, #0x20000		@ (X bit)
	teq	r2, #0x71			@ OS_CallASWIR12
	moveq	r2, r12
	teq	r2, #0x6f			@ OS_CallASWI
	moveq	r2, r10
	bic	r2, r2, #0xff000000
	bic	r2, r2, #0x20000
	teq	r2, #0x6b			@ OS_MMUControl
	bne	8f
	ldr	r3, [sp]			@ the caller's r0: the reason
	and	r3, r3, #0xff
	teq	r3, #2
	bne	8f
	bic	r1, r1, #0x10000000		@ no error
	str	r1, [r0, #24]
	ldmfd	sp!, {r0-r3}
	ldr	pc, nv_movs_pc_lr		@ CPSR = SPSR, pc = lr
8:	msr	APSR_nzcvq, r1			@ the caller's flags, as they came
	ldmfd	sp!, {r0-r3}
	ldr	pc, nv_old_swi_vector
nv_old_swi_vector:
	.word	0
nv_movs_pc_lr:
	.word	0

	.include "cmodule.inc"
	CM_COMMAND 0
