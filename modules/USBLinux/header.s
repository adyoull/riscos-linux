@ USBDriver (RISC OS for Linux): RISC OS's USB device interface (DeviceFS
@ "USB<n>" devices, Service_USB) on Linux's own USB access for programs
@ (usbfs). See usblinux.c.
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
	.word	0x54A40				@ USBDriver's SWI chunk
	.word	cm_swi - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"USBDriver"
help:	.asciz	"USBDriver\t1.31 (02 Nov 2026) RISC OS for Linux (usbfs)"
swi_names:
	.asciz	"USBDriver\0RegisterBus\0DeregisterBus\0InsertTransfer\0TransferComplete\0ScheduleSoftInterrupt\0Version\0"
	.align	2
commands:
	.asciz	"USBDevices"
	.align	2
	.word	cm_command_0 - module_start
	.word	0x00010000			@ 0 or 1 parameters
	.word	0
	.word	cmd_help - module_start
	.word	0
cmd_help:
	.asciz	"*USBDevices lists the USB devices Linux has given RISC OS (those Linux has no driver for: phones, cameras, ...), then the ones Linux keeps (keyboards, discs, sound, network...) with their Linux drivers and what is on each disc.\rSyntax: *USBDevices"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ service calls: only Service_USB (&D2) and Service_Reset (&27) matter
	.word	0				@ (no fast table)
service:
	teq	r1, #0xD2
	teqne	r1, #0x27
	movne	pc, lr
	b	cm_service

@ DeviceFS's calls to the driver: r0 = reason, r1-r7 as it says, r8 = the
@ device's record (DeviceFS_Register R3), r12 = our private word.
@ -> c_driver(regs *): 0, or an error (V set). r0-r7 come back changed.
	.global	driver_entry
driver_entry:
	stmfd	sp!, {r0-r9, lr}
	mov	r0, sp
	bl	c_driver
	cmp	r0, #0
	bne	1f
	ldmfd	sp!, {r0-r9, lr}
	msr	APSR_nzcvq, #0
	mov	pc, lr
1:	add	sp, sp, #4
	ldmfd	sp!, {r1-r9, lr}
	msr	APSR_nzcvq, #0x10000000
	mov	pc, lr

@ unsigned call_bufman(regs *r, void *routine, void *ws): the buffer
@ manager's service routine with r0-r3 from r, r2-r3 back into it
	.global	call_bufman
call_bufman:
	stmfd	sp!, {r4, r5, r12, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r12, r2
	ldmia	r4, {r0-r3}
	mov	lr, pc
	mov	pc, r5
	str	r2, [r4, #8]
	str	r3, [r4, #12]
	mrs	r0, CPSR
	ldmfd	sp!, {r4, r5, r12, pc}

@ the ticker (OS_CallEvery): asks for a callback, once
	.global	ticker_entry
ticker_entry:
	stmfd	sp!, {r0-r2, lr}
	mrs	r2, spsr			@ SPSR_svc: the SWI below writes it, and
					@ the code interrupted may be about to
					@ return with it (the kernel doesn't
					@ keep it for ticker routines)
	ldr	r0, [r12, #0]			@ r12 -> our state: +0 callback pending
	teq	r0, #0
	bne	2f
	mov	r0, #1
	str	r0, [r12, #0]
	adr	r0, callback_entry
	mov	r1, r12
	swi	0x20054				@ XOS_AddCallBack
2:	msr	spsr_cxsf, r2
	ldmfd	sp!, {r0-r2, pc}

@ the callback: c_poll() with everything kept
	.global	callback_entry
callback_entry:
	stmfd	sp!, {r0-r12, lr}
	mov	r0, #0
	str	r0, [r12, #0]
	ldr	r0, [r12, #4]			@ +4: the module's GOT base is relocated already
	bl	c_poll
	ldmfd	sp!, {r0-r12, pc}

	.include "cmodule.inc"
	CM_COMMAND 0
