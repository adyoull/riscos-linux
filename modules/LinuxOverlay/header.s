@ LinuxOverlay: the overlay calls of GraphicsV (20-28) for RISC OS on
@ Linux's display driver, so that RISC OS Open's VideoOverlay module works
@ (Reel, KinoAmp...). See linuxoverlay.c.
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.syntax	unified
	.arm
	.section .text.header, "ax"
	.global	module_start
module_start:
	.word	0				@ start
	.word	lo_init - module_start
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

title:	.asciz	"LinuxOverlay"
help:	.asciz	"LinuxOverlay\t0.01 (02 Nov 2026) Video overlays for RISC OS on Linux"
	.align	2
commands:
	.asciz	"LinuxOverlay"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*LinuxOverlay shows the video overlay LinuxOverlay is giving VideoOverlay (through rol-fb's overlay plane).\rSyntax: *LinuxOverlay"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.include "cmodule.inc"
	CM_COMMAND 0

@ init: the C init, then claim GraphicsV (r12 = our private word pointer)
lo_init:
	stmfd	sp!, {r4-r11, lr}
	bl	cm_relocate_got
	mov	r0, r10
	mov	r1, r11
	mov	r2, r12
	bl	mod_init
	cmp	r0, #0
	bne	cm_return
	mov	r0, #0x2A			@ GraphicsV
	adr	r1, gv_entry
	mov	r2, #0
	swi	0x2001F				@ XOS_Claim
	movvc	r0, #0
	b	cm_return

@ void gv_release(void): from mod_final
	.global	gv_release
gv_release:
	stmfd	sp!, {r4, lr}
	mov	r0, #0x2A
	adr	r1, gv_entry
	mov	r2, #0
	swi	0x20020				@ XOS_Release
	ldmfd	sp!, {r4, pc}

@ GraphicsV: r4 = reason | (overlay or head << 16) | (driver << 24). The
@ C side answers the overlay reasons for the current driver (claiming the
@ call: r4 = 0 and back to the vector's caller) and passes on the rest.
gv_entry:
	stmfd	sp!, {r0-r12, lr}
	mov	r0, sp
	bl	gv_call
	cmp	r0, #0
	ldmfd	sp!, {r0-r12, lr}
	moveq	pc, lr
	ldmfd	sp!, {pc}
