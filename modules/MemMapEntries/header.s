@ MemMapEntries: OS_ReadMemMapEntries, OS_FindMemMapEntries and
@ OS_SetMemMapEntries for RISC OS for Linux, whose kernel has none of them
@ ("SWI &60 not known"). See memmap.c.
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
	.word	0				@ no SWIs of its own
	.word	0
	.word	0
	.word	0
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"MemMapEntries"
help:	.asciz	"MemMapEntries\t0.05 (13 Nov 2026) Page mapping SWIs, RISC OS for Linux"
	.align	2
commands:
	.asciz	"MemMapEntries"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*MemMapEntries shows what the module has done: the pages it knows, and the calls of OS_FindMemMapEntries, OS_ReadMemMapEntries and OS_SetMemMapEntries it has answered.\rSyntax: *MemMapEntries"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ Service calls (RISC OS 4 style table): Service_MemoryMoved (&4E) and
@ Service_DynamicAreaRemove (&91), to forget the pages of memory that has
@ gone (mm_service in memmap.c)
	.word	service_table - module_start
service:
	mov	r0, r0				@ the "service table" marker
	teq	r1, #0x4E
	teqne	r1, #0x91
	movne	pc, lr
service_handler:
	b	cm_service
service_table:
	.word	0				@ flags
	.word	service_handler - module_start
	.word	0x4E
	.word	0x91
	.word	0

@ The SWI vector (OS_ClaimProcessorVector 2), as NativeSupport's: SVC mode,
@ lr = the address after the SWI, the caller's registers, and its CPSR in
@ the port's vcpu (the thread register; SPSR_svc at +24). The three SWIs,
@ also through OS_CallASWI (r10) and OS_CallASWIR12, go to mm_swi_c and
@ back to the caller through the port's MOVS pc, lr routine; every other
@ SWI goes on to the claimant before, as it came.
	.global	mm_swi_vector, mm_old_swi_vector, mm_movs_pc_lr
mm_swi_vector:
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
	teq	r2, #0x52			@ OS_ReadMemMapEntries
	teqne	r2, #0x53			@ OS_SetMemMapEntries
	teqne	r2, #0x60			@ OS_FindMemMapEntries
	beq	1f
8:	msr	APSR_nzcvq, r1			@ the caller's flags, as they came
	ldmfd	sp!, {r0-r3}
	ldr	pc, mm_old_swi_vector
1:	str	r2, mm_swi_number
	ldmfd	sp!, {r0-r3}
	stmfd	sp!, {r0-r12, lr}
	mov	r0, sp				@ r0-r12, lr
	ldr	r1, mm_swi_number
	mov	r4, sp
	bic	sp, sp, #7
	bl	mm_swi_c			@ 0, or an error block
	mov	sp, r4
	mrc	p15, 0, r1, c13, c0, 3
	ldr	r2, [r1, #24]
	cmp	r0, #0
	biceq	r2, r2, #0x10000000		@ V clear,
	orrne	r2, r2, #0x10000000		@ or set with r0 = the error
	str	r2, [r1, #24]
	strne	r0, [sp]
	ldmfd	sp!, {r0-r12, lr}
	ldr	pc, mm_movs_pc_lr		@ CPSR = SPSR, pc = lr
mm_old_swi_vector:
	.word	0
mm_movs_pc_lr:
	.word	0
mm_swi_number:
	.word	0

	.include "cmodule.inc"
	CM_COMMAND 0
