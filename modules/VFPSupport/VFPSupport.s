@ VFPSupport for RISC OS for Linux (riscos-linux-testbed)
@
@ The Linux port of RISC OS runs as an ordinary Linux user-mode process, so
@ it can't use ROOL's VFPSupport: FPEXC, FPSID and MVFR0/1 are privileged
@ registers. This module offers the same SWIs (chunk &58EC0) and context
@ layout, with these differences:
@
@  * Contexts are switched eagerly. ChangeContext saves the outgoing
@    registers and loads the incoming ones at once; the "lazy" flag is
@    accepted and ignored. FPEXC is never touched.
@  * Features reports values made up from Linux's AT_HWCAP (read from
@    /proc/self/auxv), not the real ID registers.
@  * No support code (the undefined-instruction handler that emulates
@    bounced VFP instructions): on the port the Linux kernel or QEMU does
@    that job, so there are no trapped floating-point exceptions and no
@    short vectors. VFP can't be switched off, so VFP code that runs with
@    no context active works instead of faulting.
@
@ The rest is as ROOL's VFPSupport 0.18: the SWIs, FastAPI, ExceptionDump,
@ the ElementaryFunctions (ROOL's own code, translated to GNU as: see
@ elementary/), interrupts off around each switch (through the port's
@ PSR HAL device), Service_ModulePostFinal on finalisation, one instance.
@
@ Context layout (as ROOL's VFPSupport, so ExamineContext dumps look the same):
@   +0 flags, +4 number of D registers, +8 FPSCR, +12 FPEXC (0 = never
@   saved, &40000000 = registers saved), +16 FPINST, +20 FPINST2, +24 D0...
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.syntax	unified
	.arm
	.fpu	neon-vfpv4			@ (VFMA only in the v4 functions)
	.text

	.equ	X,			0x20000
	.equ	OS_Find,		0x0D
	.equ	OS_GBPB,		0x0C
	.equ	OS_Module,		0x1E
	.equ	OS_Write0,		0x02
	.equ	OS_NewLine,		0x03
	.equ	OS_ConvertHex8,		0xD4
	.equ	OS_IntOn,		0x13
	.equ	OS_IntOff,		0x14
	.equ	OS_ServiceCall,		0x30
	.equ	OS_GenerateError,	0x2B
	.equ	OS_Hardware,		0x7A
	.equ	Service_ModulePostFinal, 0xDB
	.equ	PV_DEVICE_TYPE,		0x30A
	.equ	PV_DEVICE_ID,		1
	.equ	PV_GETCPSR,		80
	.equ	PV_SETCPSR_C,		104
	.equ	I32_bit,		0x80
	.equ	IXSupport_LinuxSyscall,	0x59EC0

	.equ	CTX_FLAGS,		0
	.equ	CTX_NREGS,		4
	.equ	CTX_FPSCR,		8
	.equ	CTX_FPEXC,		12
	.equ	CTX_REGS,		24

	.equ	FLAG_USER,		1
	.equ	FLAG_APPSPACE,		2
	.equ	FLAG_LAZY_ACTIVATE,	0x40000000
	.equ	FLAG_ACTIVATE,		0x80000000
	.equ	FLAG_STATUS_ACTIVE,	0x40000000
	.equ	FLAG_VFPMEMORY,		0x80000000
	.equ	FPEXC_EN,		0x40000000

	.equ	HWCAP_VFP,		1 << 6
	.equ	HWCAP_NEON,		1 << 12
	.equ	HWCAP_VFPv3,		1 << 13
	.equ	HWCAP_VFPv4,		1 << 16
	.equ	HWCAP_VFPD32,		1 << 19
	.equ	AT_HWCAP,		16
	.equ	AT_HWCAP2,		26

	.equ	VERSION,		92	@ 0.92
	.equ	VERSION_BCD,		0x91

	.macro	CLRV
	msr	APSR_nzcvq, #0
	.endm
	.macro	SETV
	msr	APSR_nzcvq, #0x10000000
	.endm

@ --- module header ----------------------------------------------------------
module_start:
	.word	0				@ start
	.word	init - module_start
	.word	final - module_start
	.word	0				@ service call handler
	.word	title - module_start
	.word	help - module_start
	.word	commands - module_start
	.word	0x58EC0				@ SWI chunk
	.word	swi_entry - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"VFPSupport"
help:	.asciz	"VFPSupport\t0.92 (18 Oct 2026) RISC OS for Linux test bed"
swi_names:
	.ascii	"VFPSupport\0CheckContext\0CreateContext\0DestroyContext\0"
	.ascii	"ChangeContext\0ExamineContext\0FastAPI\0ActiveContext\0Version\0"
	.asciz	"Features\0ExceptionDump\0ElementaryFunctions\0"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ *VFPSupport_Info: what the module found, and the active context
commands:
	.asciz	"VFPSupport_Info"
	.align	2
	.word	cmd_info - module_start
	.word	0				@ no parameters, no flags
	.word	0				@ syntax message
	.word	cmd_info_help - module_start
	.word	0
cmd_info_help:
	.asciz	"*VFPSupport_Info shows the Linux HWCAP, the made-up FPSID and MVFRs, which elementary functions are used, and the active VFP context."
	.align	2

@ --- state (the module lives in the RMA, which is writable) ------------------
active:		.word	0			@ the context in the registers now
hwcap:		.word	0
hwcap_source:	.word	0			@ 1 = read from auxv, 0 = assumed
num_regs:	.word	16
soft_fpsid:	.word	0
soft_mvfr0:	.word	0
soft_mvfr1:	.word	0
soft_mvfr2:	.word	0
pv_desc:	.word	0			@ the port's PSR HAL device, or 0
exc_context:	.word	0			@ ExceptionDump: the context, or 0
exc_dump:	.space	CTX_REGS + 32 * 8	@ and the copy of it

auxv_name:	.asciz	"/proc/self/auxv"
	.align	2

@ --- errors -----------------------------------------------------------------
err_nohw:	.word	0x81F100
		.asciz	"No VFP/NEON hardware present"
		.align	2
err_feature:	.word	0x81F102
		.asciz	"A requested VFP/NEON feature is unavailable"
		.align	2
err_context:	.word	0x81F103
		.asciz	"Bad VFP context"
		.align	2
err_badfeature:	.word	0x81F104
		.asciz	"Bad VFPSupport_Features reason code"
		.align	2
err_flags:	.word	0x81F107
		.asciz	"Invalid flags passed to VFPSupport SWI"
		.align	2
err_badhw:	.word	0x81F101		@ (never given on the port)
		.asciz	"Unsupported VFP/NEON version"
		.align	2
err_instanced:	.word	0x81F106
		.asciz	"Only one instance of VFPSupport can be active at a time"
		.align	2
@ The elementary functions' floating-point exceptions (RaiseException)
err_io:		.word	0x80000600
		.asciz	"Vector floating point exception: invalid operation"
		.align	2
err_dz:		.word	0x80000601
		.asciz	"Vector floating point exception: division by zero"
		.align	2
err_of:		.word	0x80000602
		.asciz	"Vector floating point exception: overflow"
		.align	2
err_uf:		.word	0x80000603
		.asciz	"Vector floating point exception: underflow"
		.align	2
err_ix:		.word	0x80000604
		.asciz	"Vector floating point exception: inexact operation"
		.align	2
err_id:		.word	0x80000605
		.asciz	"Vector floating point exception: input subnormal"
		.align	2
err_badswi:	.word	0x1E6
		.asciz	"Unknown VFPSupport SWI"
		.align	2

@ --- initialisation -----------------------------------------------------------
init:
	stmfd	sp!, {r0-r6, lr}
	cmp	r11, #0				@ one instance only, as ROOL's
	adrlne	r0, err_instanced
	bne	init_error
	bl	find_pv
	bl	read_hwcap			@ r0 = AT_HWCAP, or 0; r1 = v8?
	mov	r6, r1				@ ARMv8?
	mov	r1, #1
	cmp	r0, #0
	@ Not readable: assume a Cortex-A8 class CPU (VFPv3: so the elementary
	@ functions that work everywhere are chosen).
	ldreq	r0, =HWCAP_VFP | HWCAP_NEON | HWCAP_VFPv3 | HWCAP_VFPD32
	moveq	r1, #0
	str	r0, hwcap
	str	r1, hwcap_source
	tst	r0, #HWCAP_VFP
	adrleq	r0, err_nohw
	beq	init_error

	@ 32 D registers: VFPD32, or NEON (which always has them: a 64-bit
	@ kernel's HWCAP for 32-bit programs leaves VFPD32 out)
	tst	r0, #HWCAP_VFPD32 | HWCAP_NEON
	moveq	r1, #16
	movne	r1, #32
	str	r1, num_regs

	@ Made-up ID registers in the ARMv7 formats.
	tst	r0, #HWCAP_VFPv3
	beq	init_vfpv2
	@ MVFR0: rounding modes, sqrt, divide, double, single, 16/32 registers
	@ (no short vectors, no trapped exceptions)
	ldr	r2, =0x10110221
	cmp	r1, #32
	addeq	r2, r2, #1
	@ MVFR1: flush-to-zero, default NaN, + NEON, + VFPv4 (FMA, half precision)
	mov	r3, #0x11
	tst	r0, #HWCAP_NEON
	orrne	r3, r3, #0x11000
	orrne	r3, r3, #0x100
	tst	r0, #HWCAP_VFPv4
	beq	1f
	orr	r3, r3, #0x11000000
	tst	r0, #HWCAP_NEON
	orrne	r3, r3, #0x00100000
	ldr	r4, =0x41023075			@ like a Cortex-A7
	b	2f
1:	ldr	r4, =0x410330C1			@ like a Cortex-A8
	b	2f
init_vfpv2:
	ldr	r2, =0x10110111			@ VFPv2, 16 registers
	mov	r3, #0
	ldr	r4, =0x410120B5			@ like an ARM1176's VFP11
2:	str	r4, soft_fpsid
	str	r2, soft_mvfr0
	str	r3, soft_mvfr1
	@ MVFR2 (ARMv8 only): FPMisc 4 (VSEL, VMAXNM, VRINT, VCVTA...),
	@ SIMDMisc 3, as a Cortex-A53/A72 in AArch32
	cmp	r6, #0
	movne	r0, #0x43
	moveq	r0, #0
	str	r0, soft_mvfr2
	mov	r0, #0
	str	r0, active
	str	r0, exc_context
	ldmfd	sp!, {r0-r6, lr}
	CLRV
	mov	pc, lr

init_error:
	add	sp, sp, #4			@ drop the saved r0
	ldmfd	sp!, {r1-r6, lr}
	SETV
	mov	pc, lr

@ r0 = AT_HWCAP from /proc/self/auxv, or 0; r1 = 1 if the CPU is ARMv8
@ (AT_HWCAP2 isn't 0: only ARMv8 has those features, CRC32 at least on
@ every Pi), else 0. Corrupts r2-r6.
@ Read with Linux system calls through IXSupport_LinuxSyscall (IXFS can't:
@ /proc files have no length).
read_hwcap:
	stmfd	sp!, {r7, lr}
	sub	sp, sp, #512
	adrl	r0, auxv_name
	mov	r1, #0				@ O_RDONLY
	mov	r2, #0
	mov	r7, #5				@ open
	swi	X | IXSupport_LinuxSyscall
	movvs	r0, #-1
	cmp	r0, #0
	movlt	r0, #0
	movlt	r1, #0
	blt	9f
	mov	r5, r0				@ fd
	mov	r1, sp
	mov	r2, #512
	mov	r7, #3				@ read
	swi	X | IXSupport_LinuxSyscall
	movvs	r0, #0
	cmp	r0, #0
	movlt	r0, #0
	mov	r4, r0				@ bytes read
	mov	r0, r5
	mov	r7, #6				@ close
	swi	X | IXSupport_LinuxSyscall
	mov	r2, sp
	add	r4, r2, r4			@ end
	mov	r5, #0				@ HWCAP
	mov	r6, #0				@ v8?
3:	add	r3, r2, #8
	cmp	r3, r4
	bhi	8f
	ldmia	r2!, {r0, r1}			@ type, value
	cmp	r0, #0				@ AT_NULL
	beq	8f
	cmp	r0, #AT_HWCAP
	moveq	r5, r1
	cmp	r0, #AT_HWCAP2
	bne	3b
	cmp	r1, #0
	movne	r6, #1
	b	3b
8:	mov	r0, r5
	mov	r1, r6
9:	add	sp, sp, #512
	ldmfd	sp!, {r7, pc}

final:
	stmfd	sp!, {r0-r4, lr}
	@ Warn our clients that we're about to die, as ROOL's does, while
	@ the SWIs still work (they may want to destroy their contexts).
	adrl	r0, module_start
	mov	r1, #Service_ModulePostFinal
	adrl	r2, title
	mov	r3, #0				@ only one instance
	ldr	r4, =VERSION_BCD
	swi	X | OS_ServiceCall
	mov	r0, #0
	str	r0, active
	str	r0, exc_context
	ldmfd	sp!, {r0-r4, lr}
	CLRV
	mov	pc, lr

@ Find the port's PSR HAL device (type &30A, ID 1, API 0.x): its routine
@ at +80 reads the (virtual) CPSR. Corrupts r0-r3, r8.
find_pv:
	stmfd	sp!, {r8, lr}
	mov	r3, #0
	str	r3, pv_desc
1:	ldr	r0, =PV_DEVICE_TYPE
	mov	r1, r3
	mov	r8, #5				@ OS_Hardware 5: enumerate devices
	swi	X | OS_Hardware
	popvs	{r8, pc}
	cmn	r1, #1
	popeq	{r8, pc}			@ none (not the Linux port)
	mov	r3, r1				@ where to go on from
	ldrh	r0, [r2, #2]			@ ID
	cmp	r0, #PV_DEVICE_ID
	bne	1b
	ldr	r0, [r2, #8]			@ API version: major 0
	cmp	r0, #0x10000
	bhs	1b
	str	r2, pv_desc
	ldmfd	sp!, {r8, pc}

@ Interrupts off, for a switch to be atomic. ROOL's VFPSupport does it with
@ MSR CPSR_c; on the port the (virtual) CPSR is set by the PSR device's
@ SetCPSR_c routine (in: r1 = new CPSR_c; corrupts r0, r2, r3, ip, lr; it
@ takes any pending interrupt when they're turned on). Out: r3 = the CPSR_c
@ to give back to irq_restore, or -1 if there's nothing to undo (they were
@ already off, or not on the port). Preserves the other registers and the
@ flags.
irq_off:
	stmfd	sp!, {r0-r2, r4, r12, lr}
	mrs	r4, cpsr
	mvn	r3, #0
	ldr	r2, pv_desc
	cmp	r2, #0
	beq	1f
	mov	lr, pc
	ldr	pc, [r2, #PV_GETCPSR]		@ r0 = CPSR
	tst	r0, #I32_bit
	bne	1f				@ already off (e.g. in IRQ code)
	and	r1, r0, #0xFF
	orr	r1, r1, #I32_bit
	ldr	r2, pv_desc
	mov	lr, pc
	ldr	pc, [r2, #PV_SETCPSR_C]
	bic	r3, r1, #I32_bit
1:	msr	cpsr_f, r4
	ldmfd	sp!, {r0-r2, r4, r12, pc}

@ In: r3 from irq_off. Preserves all registers and the flags.
irq_restore:
	cmn	r3, #1
	moveq	pc, lr
	stmfd	sp!, {r0-r4, r12, lr}
	mrs	r4, cpsr
	mov	r1, r3
	ldr	r2, pv_desc
	mov	lr, pc
	ldr	pc, [r2, #PV_SETCPSR_C]
	msr	cpsr_f, r4
	ldmfd	sp!, {r0-r4, r12, pc}

@ --- SWIs -------------------------------------------------------------------
swi_entry:
	cmp	r11, #(swi_table_end - swi_table) / 4
	addlo	pc, pc, r11, lsl #2
	b	swi_unknown
swi_table:
	b	swi_check
	b	swi_create
	b	swi_destroy
	b	swi_change
	b	swi_examine
	b	swi_fastapi
	b	swi_active
	b	swi_version
	b	swi_features
	b	swi_exceptiondump
	b	swi_elementary
swi_table_end:

swi_unknown:
	adrl	r0, err_badswi
	SETV
	mov	pc, lr

@ CheckContext
@  in: r0 = flags (b0 user mode, b1 app space, b30/b31 ignored), r1 = D registers
@ out: r0 = context size
swi_check:
	stmfd	sp!, {r2, lr}
	bics	r2, r0, #FLAG_USER | FLAG_APPSPACE | FLAG_LAZY_ACTIVATE | FLAG_ACTIVATE
	bne	1f
	cmp	r1, #1
	blt	1f
	ldr	r2, num_regs
	cmp	r1, r2
	bgt	1f
	mov	r0, #CTX_REGS
	add	r0, r0, r1, lsl #3
	CLRV
	ldmfd	sp!, {r2, pc}
1:	adrl	r0, err_feature
	SETV
	ldmfd	sp!, {r2, pc}

@ CreateContext
@  in: r0 = flags, r1 = D registers, r2 = save area or 0, r3 = initial FPSCR
@ out: r0 = context; r1 = previously active context (if activated) or preserved
swi_create:
	stmfd	sp!, {r1-r6, lr}
	mov	r4, r0				@ flags
	mov	r5, r2
	bl	swi_check
	bvs	9f
	mov	r6, r0				@ size
	and	r0, r4, #FLAG_USER | FLAG_APPSPACE
	cmp	r5, #0
	bne	1f
	mov	r0, #6				@ claim
	mov	r3, r6
	swi	X | OS_Module
	bvs	9f
	mov	r5, r2
	and	r0, r4, #FLAG_USER | FLAG_APPSPACE
	orr	r0, r0, #FLAG_VFPMEMORY
1:	ldr	r1, [sp, #0]			@ D registers
	ldr	r2, [sp, #8]			@ FPSCR
	mov	r3, #0
	stmia	r5, {r0-r3}			@ flags, D registers, FPSCR, FPEXC = 0
	mov	r2, #0
	str	r2, [r5, #16]
	str	r2, [r5, #20]
	add	r0, r5, #CTX_REGS		@ clear the register dump
	add	r1, r5, r6
2:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	2b
	mov	r0, r5
	tst	r4, #FLAG_LAZY_ACTIVATE | FLAG_ACTIVATE
	beq	8f
	mov	r1, #0
	bl	swi_change			@ r0 = previously active
	str	r0, [sp, #0]			@ returned in r1
	mov	r0, r5
8:	CLRV
	ldmfd	sp!, {r1-r6, pc}
9:	SETV
	ldmfd	sp!, {r1-r6, pc}

@ DestroyContext
@  in: r0 = context, r1 = context to activate if r0 was active
@ out: r0 = active context
swi_destroy:
	stmfd	sp!, {r1-r5, lr}
	movs	r2, r0
	adrleq	r0, err_context
	beq	9f
	ldr	r3, [r2]			@ (faults now if r0 is junk)
	bl	irq_off				@ atomic, as ROOL's
	mov	r5, r3
	ldr	r3, active
	cmp	r3, r2
	bne	1f
	mov	r3, #0				@ drop it without saving it
	str	r3, active
	cmp	r1, r2
	cmpne	r1, #0
	movne	r0, r1
	movne	r1, #0
	blne	swi_change
	bvs	8f
1:	mov	r3, r5				@ (on again for the free, as ROOL's)
	bl	irq_restore
	ldr	r0, [r2, #CTX_FLAGS]
	tst	r0, #FLAG_VFPMEMORY
	movne	r0, #7				@ free
	swine	X | OS_Module
	ldr	r0, active
	CLRV
	ldmfd	sp!, {r1-r5, pc}
8:	mov	r3, r5
	bl	irq_restore
9:	SETV
	ldmfd	sp!, {r1-r5, pc}

@ ChangeContext
@  in: r0 = context to activate (0 = none), r1 = flags (b0 lazy, b1 app space)
@ out: r0 = previously active context
swi_change:
	stmfd	sp!, {r1-r4, lr}
	cmp	r0, #0
	ldrne	r2, [r0]			@ (faults now if r0 is junk)
	bl	irq_off				@ atomic, as ROOL's
	mov	r4, r3
	ldr	r1, active
	cmp	r0, r1
	beq	1f
	cmp	r1, #0
	blne	save_r1
	cmp	r0, #0
	blne	load_r0
	str	r0, active
1:	mov	r0, r1
	mov	r3, r4
	bl	irq_restore
	CLRV
	ldmfd	sp!, {r1-r4, pc}

@ Save the registers into the context at r1. Corrupts r2, r3.
save_r1:
	vmrs	r2, fpscr
	str	r2, [r1, #CTX_FPSCR]
	mov	r2, #FPEXC_EN
	str	r2, [r1, #CTX_FPEXC]
	ldr	r2, [r1, #CTX_NREGS]
	sub	r2, r2, #1
	cmp	r2, #32
	movhs	pc, lr				@ not 1..32: nothing to save
	add	r3, r1, #CTX_REGS
	add	r2, r2, r2, lsl #1		@ 3 instructions an entry
	add	pc, pc, r2, lsl #2
	nop
	.include "tables-save.inc"
save_done:
	mov	pc, lr

@ Load the context at r0 into the registers (zeros if it was never saved).
@ Corrupts r2, r3.
load_r0:
	ldr	r2, [r0, #CTX_FPEXC]
	tst	r2, #FPEXC_EN
	addne	r3, r0, #CTX_REGS
	adrleq	r3, zeros
	ldr	r2, [r0, #CTX_NREGS]
	sub	r2, r2, #1
	cmp	r2, #32
	bhs	load_done
	add	r2, r2, r2, lsl #1
	add	pc, pc, r2, lsl #2
	nop
	.include "tables-load.inc"
load_done:
	ldr	r2, [r0, #CTX_FPSCR]
	vmsr	fpscr, r2
	mov	pc, lr

@ ExamineContext
@  in: r0 = context, r1 = flags (b0 serialise)
@ out: r0 = flags, r1 = D registers, r2 = active register mask,
@      r3 = dump format descriptor, r4 = context size
swi_examine:
	stmfd	sp!, {lr}
	movs	r4, r0
	adrleq	r0, err_context
	beq	9f
	tst	r1, #1				@ serialise: write the live registers out
	beq	1f
	bl	irq_off				@ (atomic: not switched away meanwhile)
	stmfd	sp!, {r3}
	ldr	r2, active
	cmp	r2, r4
	moveq	r1, r4
	bleq	save_r1
	ldmfd	sp!, {r3}
	bl	irq_restore
1:	ldr	r0, [r4, #CTX_FLAGS]
	ldr	r1, [r4, #CTX_NREGS]
	ldr	r2, active
	cmp	r2, r4
	movne	r2, #0
	bne	2f
	orr	r0, r0, #FLAG_STATUS_ACTIVE
	mvn	r2, #0				@ registers 0..n-1 active
	cmp	r1, #32
	rsblo	r3, r1, #32
	movlo	r2, r2, lsr r3
2:	adr	r3, format_descriptor
	mov	r4, #CTX_REGS
	add	r4, r4, r1, lsl #3
	CLRV
	ldmfd	sp!, {pc}
9:	SETV
	ldmfd	sp!, {pc}

format_descriptor:
	.word	0 + (CTX_FPSCR << 16)		@ FPSCR
	.word	1 + (CTX_FPEXC << 16)		@ FPEXC
	.word	5 + (CTX_REGS << 16)		@ register dump
	.word	-1

@ FastAPI: r0 = workspace, r1-r4 = CheckContext, CreateContext,
@ DestroyContext, ChangeContext (call with BL, r12 = r0)
swi_fastapi:
	mov	r0, r12
	adrl	r1, swi_check
	adrl	r2, swi_create
	adrl	r3, swi_destroy
	adrl	r4, swi_change
	CLRV
	mov	pc, lr

swi_active:
	ldr	r0, active
	CLRV
	mov	pc, lr

swi_version:
	mov	r0, #VERSION
	CLRV
	mov	pc, lr

@ Features: 0 = FPSID/MVFR0/MVFR1, 1 = trappable exceptions (none),
@ 2 = misc (b2: elementary functions; no short vectors), 3 = MVFR2
swi_features:
	cmp	r0, #0
	ldreq	r0, soft_fpsid
	ldreq	r1, soft_mvfr0
	ldreq	r2, soft_mvfr1
	beq	8f
	cmp	r0, #1
	moveq	r0, #0
	beq	8f
	cmp	r0, #2
	moveq	r0, #4
	beq	8f
	cmp	r0, #3
	adrlne	r0, err_badfeature
	bne	9f
	ldr	r0, soft_mvfr2
	mov	r1, #0
	mov	r2, #0
	mov	r3, #0
8:	CLRV
	mov	pc, lr
9:	SETV
	mov	pc, lr

@ ExceptionDump, as ROOL's: a copy of one context, kept for reporting an
@ exception (made by a client, or support code, with Create).
@  in: r0 = flags: b0 GetDump, b1 GetContext, b2 Clear, b3 Create;
@      r1 = context to copy (Create; 0 = clear)
@ out: r0 = RMA copy of the dump, or 0 (GetDump: the caller frees it);
@      r1 = the context the dump came from, or 0 (GetContext)
swi_exceptiondump:
	cmp	r0, #0xF
	adrlhi	r0, err_flags
	bhi	99f
	stmfd	sp!, {r0-r8, lr}
	mov	r7, r0
	bl	irq_off
	mov	r8, r3
	tst	r7, #8				@ Create first
	beq	50f
	movs	r0, r1
	streq	r0, exc_context			@ a null context: clear
	beq	50f
	mov	r1, #1				@ ExamineContext, serialised
	bl	swi_examine			@ r4 = size
	strvs	r0, [sp]
	bvs	90f
	ldr	r0, [sp, #4]
	str	r0, exc_context
	adrl	r1, exc_dump
10:	ldr	r2, [r0], #4
	subs	r4, r4, #4
	str	r2, [r1], #4
	bne	10b
50:	ldr	r6, exc_context
	tst	r7, #4				@ Clear
	movne	r0, #0
	strne	r0, exc_context
	tst	r7, #1				@ GetDump
	beq	80f
	cmp	r6, #0
	streq	r6, [sp]
	beq	80f
	@ Copy the dump onto the stack first, so the caller gets the dump of
	@ that context even if another exception comes during OS_Module.
	adrl	r0, exc_dump
	ldr	r2, [r0, #CTX_NREGS]
	mov	r3, #CTX_REGS
	add	r3, r3, r2, lsl #3
	sub	sp, sp, r3
	mov	r5, r3
	mov	r1, sp
60:	ldr	r2, [r0], #4
	subs	r5, r5, #4
	str	r2, [r1], #4
	bne	60b
	mov	r0, #6				@ claim
	swi	X | OS_Module
	addvs	sp, sp, r3
	strvs	r0, [sp]
	bvs	90f
	mov	r0, sp
	mov	r5, r3
	mov	r1, r2
70:	ldr	r4, [r0], #4
	subs	r5, r5, #4
	str	r4, [r1], #4
	bne	70b
	add	sp, sp, r3
	str	r2, [sp]
80:	tst	r7, #2				@ GetContext
	strne	r6, [sp, #4]
	mov	r3, r8
	bl	irq_restore
	CLRV
	ldmfd	sp!, {r0-r8, pc}
90:	mov	r3, r8
	bl	irq_restore
	SETV
	ldmfd	sp!, {r0-r8, pc}
99:	SETV
	mov	pc, lr

@ ElementaryFunctions
@  in: r0 = 0
@ out: r0 = number of functions, r1 = single-precision table, r2 = double-
@      precision table: one "B function" word each, called with BLX
@      (hard-float: arguments in s0-s2 / d0-d2, result in s0 / d0)
swi_elementary:
	cmp	r0, #0
	adrlne	r0, err_flags
	bne	9f
	mov	r0, #(v3_elementary32_end - v3_elementary32) / 4
	ldr	r1, hwcap
	tst	r1, #HWCAP_VFPv4
	adrleq	r1, v3_elementary32
	adrleq	r2, v3_elementary64
	adrlne	r1, v4_elementary32
	adrlne	r2, v4_elementary64
	CLRV
	mov	pc, lr
9:	SETV
	mov	pc, lr

@ The tables, in hdr/VFPSupport's order (VFPSupport_Fn_*). As ROOL builds
@ a ROM for each kind of machine, there are two builds of the functions:
@ v3 (FMA in software) and v4 (VFMA, for VFPv4 cores), chosen by HWCAP.
	.macro	elementary_tables v
\v\()_elementary32:
	b	\v\()_fp32sin
	b	\v\()_fp32cos
	b	\v\()_fp32tan
	b	\v\()_fp32asin
	b	\v\()_fp32acos
	b	\v\()_fp32atan
	b	\v\()_fp32atan2
	b	\v\()_fp32log
	b	\v\()_fp32log10
	b	\v\()_fp32exp
	b	\v\()_fp32pow
	b	\v\()_fused32_muladd
\v\()_elementary32_end:
\v\()_elementary64:
	b	\v\()_fp64sin
	b	\v\()_fp64cos
	b	\v\()_fp64tan
	b	\v\()_fp64asin
	b	\v\()_fp64acos
	b	\v\()_fp64atan
	b	\v\()_fp64atan2
	b	\v\()_fp64log
	b	\v\()_fp64log10
	b	\v\()_fp64exp
	b	\v\()_fp64pow
	b	\v\()_fused64_muladd
	.endm
	elementary_tables v3
	elementary_tables v4

@ RaiseException (called by the elementary functions): r0 = FPSCR
@ cumulative exception bits to raise. They're ORed into the FPSCR; if any
@ of them is also enabled as a trap, the matching "Vector floating point
@ exception" error is raised (non-X OS_GenerateError), as ROOL's does.
@ (On the port no FPSCR trap-enable bit can be set: QEMU and every
@ Cortex-A core make them read as zero.) Corrupts r0-r3, ip.
RaiseException:
v3_RaiseException:
v4_RaiseException:
	vmrs	r3, fpscr
	and	r0, r0, #0xFF			@ only the cumulative bits
	orr	r3, r3, r0
	vmsr	fpscr, r3
	tst	r0, r3, lsr #8			@ any of them enabled as a trap?
	bxeq	lr
	stmfd	sp!, {r4-r6, lr}
	mov	r6, r0
	@ (ROOL's order: the last match wins)
	tst	r6, #FPSCR_IOC
	adrlne	r0, err_io
	tst	r6, #FPSCR_DZC
	adrlne	r0, err_dz
	tst	r6, #FPSCR_OFC
	adrlne	r0, err_of
	tst	r6, #FPSCR_UFC
	adrlne	r0, err_uf
	tst	r6, #FPSCR_IXC
	adrlne	r0, err_ix
	tst	r6, #FPSCR_IDC
	adrlne	r0, err_id
	ldmfd	sp!, {r4-r6, lr}
	swi	OS_GenerateError



@ --- *VFPSupport_Info -------------------------------------------------------
cmd_info:
	stmfd	sp!, {r0-r3, lr}
	adrl	r0, info_hwcap
	adrl	r1, hwcap
	ldr	r1, [r1]
	bl	print_hex
	adrl	r0, hwcap_source
	ldr	r0, [r0]
	cmp	r0, #0
	adrleq	r0, info_assumed
	swieq	X | OS_Write0
	swi	X | OS_NewLine
	adrl	r0, info_fpsid
	adrl	r1, soft_fpsid
	ldr	r1, [r1]
	bl	print_hex_nl
	adrl	r0, info_mvfr0
	adrl	r1, soft_mvfr0
	ldr	r1, [r1]
	bl	print_hex_nl
	adrl	r0, info_mvfr1
	adrl	r1, soft_mvfr1
	ldr	r1, [r1]
	bl	print_hex_nl
	adrl	r0, info_mvfr2
	adrl	r1, soft_mvfr2
	ldr	r1, [r1]
	bl	print_hex_nl
	adrl	r0, info_regs
	adrl	r1, num_regs
	ldr	r1, [r1]
	bl	print_hex_nl
	adrl	r0, info_elem
	swi	X | OS_Write0
	adrl	r0, hwcap
	ldr	r0, [r0]
	tst	r0, #HWCAP_VFPv4
	adrleq	r0, info_elem_v3
	adrlne	r0, info_elem_v4
	swi	X | OS_Write0
	swi	X | OS_NewLine
	adrl	r0, info_irq
	swi	X | OS_Write0
	adrl	r0, pv_desc
	ldr	r0, [r0]
	cmp	r0, #0
	adrleq	r0, info_irq_no
	adrlne	r0, info_irq_yes
	swi	X | OS_Write0
	swi	X | OS_NewLine
	adrl	r0, info_active
	adrl	r1, active
	ldr	r1, [r1]
	bl	print_hex_nl
	CLRV
	ldmfd	sp!, {r0-r3, pc}

print_hex_nl:
	stmfd	sp!, {lr}
	bl	print_hex
	swi	X | OS_NewLine
	ldmfd	sp!, {pc}

@ r0 = label, r1 = value
print_hex:
	stmfd	sp!, {r0-r2, lr}
	swi	X | OS_Write0
	sub	sp, sp, #12
	mov	r0, r1
	mov	r1, sp
	mov	r2, #12
	swi	X | OS_ConvertHex8
	swi	X | OS_Write0
	add	sp, sp, #12
	ldmfd	sp!, {r0-r2, pc}

info_hwcap:	.asciz	"Linux AT_HWCAP: &"
info_assumed:	.asciz	" (assumed: /proc/self/auxv not readable)"
info_fpsid:	.asciz	"FPSID (made up): &"
info_mvfr0:	.asciz	"MVFR0 (made up): &"
info_mvfr1:	.asciz	"MVFR1 (made up): &"
info_mvfr2:	.asciz	"MVFR2 (made up): &"
info_regs:	.asciz	"D registers: &"
info_elem:	.asciz	"Elementary functions: "
info_elem_v3:	.asciz	"ROOL's, FMA in software (no VFPv4)"
info_elem_v4:	.asciz	"ROOL's, with VFMA (VFPv4)"
info_irq:	.asciz	"Interrupts off around switches: "
info_irq_no:	.asciz	"no (no PSR device: not the Linux port?)"
info_irq_yes:	.asciz	"yes (through the port's PSR device)"
info_active:	.asciz	"Active context: &"
	.align	3
zeros:	.space	32 * 8
	.ltorg

@ The elementary functions themselves: RISC OS Open's code (BSD licence;
@ see each file), translated by elementary/objasm2gas.py, in two builds.
	.include "defs.inc"
	.irp	v, v3, v4
	.include "\v/CMath.s"
	.include "\v/Trig32.s"
	.include "\v/Trig64.s"
	.include "\v/ArcTrig32.s"
	.include "\v/ArcTrig64.s"
	.include "\v/Power32.s"
	.include "\v/Power64.s"
	.include "\v/Fused32.s"
	.include "\v/Fused64.s"
	.endr
	.syntax	unified
	.arm
	.balign	4
