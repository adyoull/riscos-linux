@ SCSILinux: a SCSISwitch driver for RISC OS on Linux. FileCore discs
@ (USB sticks and SD cards in RISC OS's own format) appear as SCSI devices,
@ so RISC OS Open's SCSIFS and SCSIFiler use them as on a native machine;
@ each SCSI command becomes a read or write of the disc through Linux.
@ See scsilinux.c.
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

title:	.asciz	"SCSILinux"
help:	.asciz	"SCSILinux\t0.01 (02 Nov 2026) FileCore discs through Linux, for SCSIFS"
	.align	2
commands:
	.asciz	"SCSILinux"
	.align	2
	.word	cm_command_0 - module_start
	.word	0				@ no parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*SCSILinux lists the FileCore discs SCSILinux is giving SCSIFS (from Linux: USB sticks and SD cards in RISC OS's own format).\rSyntax: *SCSILinux"
	.align	2
flags:	.word	1				@ 32-bit compatible

	.word	0				@ (no fast table)
@ Service_SCSIStarting (&20100), Service_SCSIDying (&20101)
service:
	stmfd	sp!, {r0}
	ldr	r0, =0x20100
	teq	r1, r0
	addne	r0, r0, #1
	teqne	r1, r0
	ldmfd	sp!, {r0}
	movne	pc, lr
	b	cm_service

@ SCSISwitch's driver entry: r8 = our handle (device slot + 1), r11 =
@ reason, r0-r7 as the reason says. r10-r12 may be corrupted; r0 (and r1,
@ r4) are the results, V set for an error.
	.global	scsi_entry
scsi_entry:
	stmfd	sp!, {r0-r9, lr}
	mov	r0, sp
	mov	r1, r8
	mov	r2, r11
	bl	scsi_call
	cmp	r0, #0
	ldmfd	sp!, {r0-r9, lr}
	bne	1f
	msr	APSR_nzcvq, #0
	mov	pc, lr
1:	msr	APSR_nzcvq, #0x10000000
	mov	pc, lr

@ every second: a callback, which looks for discs coming and going
	.global	ticker_entry
ticker_entry:
	stmfd	sp!, {r0-r1, lr}
	ldr	r0, [r12, #0]			@ r12 -> state: +0 callback pending
	teq	r0, #0
	bne	2f
	mov	r0, #1
	str	r0, [r12, #0]
	adr	r0, callback_entry
	mov	r1, r12
	swi	0x20054				@ XOS_AddCallBack
2:	ldmfd	sp!, {r0-r1, pc}

	.global	callback_entry
callback_entry:
	stmfd	sp!, {r0-r12, lr}
	mov	r0, #0
	str	r0, [r12, #0]
	bl	c_poll
	ldmfd	sp!, {r0-r12, pc}

	.ltorg

	.include "cmodule.inc"
	CM_COMMAND 0
