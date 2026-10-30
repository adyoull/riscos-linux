@ LinuxAudio: a HAL audio controller device for RISC OS for Linux, so that
@ RISC OS Open's own SoundDMA (in the port's ROM) has hardware to drive.
@ See linuxaudio.c.
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
	.word	0				@ no SWIs
	.word	0
	.word	0
	.word	0
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"LinuxAudio"
help:	.asciz	"LinuxAudio\t0.10 (18 Oct 2026) sound to Linux, for SoundDMA"
	.align	2
commands:
	.asciz	"LinuxAudio"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0				@ no syntax message
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*LinuxAudio shows where RISC OS's sound is going: the Linux program it is sent to, the sample rate and buffer size, and the buffers sent and dropped.\rSyntax: *LinuxAudio"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.include "cmodule.inc"
	CM_COMMAND 0

@ The ResourceFS files LinuxAudio registers: SharedSound's Messages
@ (Resources.SSound.Messages, RISC OS Open's, rool-sharedsound/), which the
@ Pi ROM's SharedSound module, loaded from disc, can't find on the port:
@ in ROM, it's in the ROM's ResourceFS.
	.global	la_resources
la_resources:
	adr	r0, resources
	mov	pc, lr
	.align	2
resources:
	.include "la-resources.inc"

@ OS_CallEvery routine: IRQ mode, IRQs off, everything preserved. It only
@ asks for a transient callback (la_callback), in SVC mode with lr_svc
@ saved, as an interrupt routine must to call a SWI; the buffers are filled
@ there, where SoundDMA (and SoundScheduler, which it calls) can do what
@ they like.
	.global	la_ticker
la_ticker:
	stmfd	sp!, {r0-r3, r12, lr}
	mrs	r3, cpsr
	bic	r1, r3, #0x1f
	orr	r1, r1, #0x93			@ SVC mode, IRQs off
	msr	cpsr_c, r1
	mov	r2, sp
	bic	sp, sp, #7			@ 8-byte aligned stack for C
	stmfd	sp!, {r2, r3, r4, lr}
	bl	la_tick_irq			@ C: decides whether to ask
	ldmfd	sp!, {r2, r3, r4, lr}
	mov	sp, r2
	msr	cpsr_c, r3
	ldmfd	sp!, {r0-r3, r12, pc}

@ The transient callback: SVC mode, IRQs on; r0-r12 must be preserved.
	.global	la_callback
la_callback:
	stmfd	sp!, {r0-r12, lr}
	mov	r4, sp
	bic	sp, sp, #7
	bl	la_tick
	mov	sp, r4
	ldmfd	sp!, {r0-r12, pc}

