@ ARMEABISupport (lite) for RISC OS for Linux: module header.
@ See armeabi.c for what it does and doesn't do.
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
	.word	0				@ service call handler
	.word	title - module_start
	.word	help - module_start
	.word	commands - module_start
	.word	0x59D00				@ SWI chunk
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"ARMEABISupport"
help:	.asciz	"ARMEABISupport\t1.90 (15 Oct 2026) lite, RISC OS for Linux test bed"
swi_names:
	.ascii	"ARMEABISupport\0MemoryOp\0AbortOp\0StackOp\0Cleanup\0"
	.asciz	"MMapOp\0ShmOp\0"
	.align	2
flags:	.word	1				@ 32-bit compatible

commands:
	.asciz	"ARMEABISupport_Info"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0
	.word	info_help - module_start
	.word	0
info_help:
	.asciz	"*ARMEABISupport_Info lists the programs, stacks and memory maps this ARMEABISupport (lite) knows about."
	.align	2

	.include "cmodule.inc"

@ pv_store_usr(routine, out): call the port's StoreUsr HAL routine
@ (r1 -> 2 words for USR r13, r14; preserves the other registers).
	.pushsection .text
	.global	pv_store_usr
	.hidden	pv_store_usr
pv_store_usr:
	push	{r4, lr}
	mov	r4, r0
	mov	lr, pc
	mov	pc, r4
	pop	{r4, pc}
	.popsection
	CM_COMMAND 0
