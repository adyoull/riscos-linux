@ TBShot: a RISC OS Utility (filetype &FFC) for rol-run's desktop mode.
@ It asks rol-kvm (the parent of the RISC OS process) for a screenshot
@ by sending it SIGUSR1 through IXSupport_LinuxSyscall, then waits 0.2 s
@ so that the screen doesn't change before rol-kvm has copied it.
@
@ A Utility runs in the RMA without replacing the current application,
@ so a program can use it with OS_CLI (UnixLib refuses to start a new
@ application, such as BASIC, from inside a program).
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.equ	X, 0x20000
	.equ	OS_ReadMonotonicTime, 0x42
	.equ	IXSupport_LinuxSyscall, 0x59EC0
	.equ	SYS_getppid, 64
	.equ	SYS_kill, 37
	.equ	SIGUSR1, 10

	.text
	.arm
	stmfd	sp!, {r4-r8, lr}
	mov	r7, #SYS_getppid
	swi	X | IXSupport_LinuxSyscall
	bvs	done
	mov	r1, #SIGUSR1
	mov	r7, #SYS_kill
	swi	X | IXSupport_LinuxSyscall
	bvs	done
	swi	X | OS_ReadMonotonicTime
	add	r4, r0, #20
wait:
	swi	X | OS_ReadMonotonicTime
	subs	r0, r0, r4		@ signed difference: safe across wrap
	bmi	wait
	mov	r0, #0
	msr	APSR_nzcvq, r0		@ V clear: no error
done:
	ldmfd	sp!, {r4-r8, pc}
