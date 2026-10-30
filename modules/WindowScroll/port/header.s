@ WindowScroll (RISC OS Open's, 0.03) built for the test bed: the module
@ header that CMHG would make from rool/cmhg/modhead, and its veneers (the
@ task's start code, the EventV handler). cmodule.inc has the rest.
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.syntax	unified
	.arm
	.section .text.header, "ax"
	.global	module_start
module_start:
	.word	ws_start_entry - module_start	@ start: the Wimp task
	.word	cm_init - module_start
	.word	cm_final - module_start
	.word	cm_service - module_start
	.word	title - module_start
	.word	help - module_start
	.word	commands - module_start
	.word	0				@ no SWIs
	.word	0
	.word	0
	.word	0
	.word	messages - module_start		@ international help file
	.word	flags - module_start

title:	.asciz	"WindowScroll"
help:	.asciz	"WindowScroll\t0.03 (03 Jun 2026) test bed build"
messages:
	.asciz	"Resources:$.Resources.WindowScroll.Messages"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ commands (international: the help and syntax are Messages tokens)
commands:
	.asciz	"Desktop_WindowScroll"
	.align	2
	.word	cm_command_0 - module_start
	.word	0x10000000			@ 0 to 0 parameters, international
	.word	tok_swsdws - module_start
	.word	tok_hwsdws - module_start
	.asciz	"WimpScroll"
	.align	2
	.word	cm_command_1 - module_start
	.word	0x10050000			@ 0 to 5 parameters, international
	.word	tok_sws - module_start
	.word	tok_hws - module_start
	.word	0
tok_swsdws:	.asciz	"SWSDWS"
tok_hwsdws:	.asciz	"HWSDWS"
tok_sws:	.asciz	"SWS"
tok_hws:	.asciz	"HWS"
	.align	2

@ The task: entered in user mode by OS_Module Enter. The stack goes at the
@ top of the application space (*Desktop_WindowScroll asks for 8K).
ws_start_entry:
	swi	0x10				@ OS_GetEnv: r1 = RAM limit
	mov	sp, r1
	bl	ws_start			@ doesn't return

@ EventV: Event_Mouse, Event_Keyboard and Event_Expansion go to the C
@ handler (which only sets the task's pollword); everything is passed on.
	.global	eventv_handler_entry
eventv_handler_entry:
	teq	r0, #10
	teqne	r0, #11
	teqne	r0, #21
	movne	pc, lr
	stmfd	sp!, {r0-r12, lr}
	mov	r0, sp				@ _kernel_swi_regs: r0-r9 as they came
	mov	r1, r12
	bl	eventv_handler
	ldmfd	sp!, {r0-r12, pc}

@ void *Resources(void): the ResourceFS files (the Messages file)
	.global	Resources
Resources:
	adr	r0, resources
	mov	pc, lr

	.include "cmodule.inc"
	CM_COMMAND 0
	CM_COMMAND 1

	.align	2
resources:
	.include "resources.inc"
