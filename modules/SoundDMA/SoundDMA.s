@ SoundDMA: a silent stand-in for RISC OS's SoundDMA module, for the
@ Linux port, which has no sound hardware and no SoundDMA. Programs that
@ set up sound (Sound_Configure, Sound_Enable, Sound_Stereo...) stop with
@ "SWI &4014x not known" without it. This one keeps the settings it's
@ given and returns them, as SoundDMA does, but makes no sound: the
@ channel handler and linear handlers are never called.
@
@ Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

	.syntax	unified
	.arm
	.text

module_start:
	.word	0				@ start
	.word	init - module_start
	.word	0				@ finalisation
	.word	0				@ service call handler
	.word	title - module_start
	.word	help - module_start
	.word	0				@ commands
	.word	0x40140				@ SWI chunk
	.word	swi_entry - module_start
	.word	swi_names - module_start
	.word	0				@ SWI decoding code
	.word	0				@ messages file
	.word	flags - module_start

title:	.asciz	"SoundDMA"
help:	.asciz	"SoundDMA\t1.70 (17 Oct 2026) silent, for the Linux port"
	.align	2
flags:	.word	1				@ 32-bit compatible

@ settings (the module is in RMA, so these are writable)
channels:	.word	1
buflen:		.word	208
period:		.word	48
handler:	.word	0
scheduler:	.word	0
enabled:	.word	2			@ 1 off, 2 on
speaker:	.word	2
lin_code:	.word	0
lin_ws:		.word	0
rate_index:	.word	1
stereo:		.space	32			@ 8 channels' positions (bytes)

init:	mov	r0, #0
	bx	lr

swi_entry:
	cmp	r11, #(swi_table_end - swi_table) / 4
	addlo	pc, pc, r11, lsl #2
	b	bad_swi
swi_table:
	b	sound_configure
	b	sound_enable
	b	sound_stereo
	b	sound_speaker
	b	sound_mode
	b	sound_linear_handler
	b	sound_sample_rate
	b	sound_read_sys_info
swi_table_end:

bad_swi:
	adr	r0, err_bad_swi
	msr	APSR_nzcvq, #0x10000000		@ V set
	bx	lr

@ in: r0 channels, r1 buffer length, r2 period, r3 handler, r4 scheduler
@ (0 = no change); out: the previous values
sound_configure:
	stmfd	sp!, {r5-r9, lr}
	adr	r9, channels
	ldmia	r9, {r5-r8, ip}
	cmp	r0, #0
	strne	r0, [r9, #0]
	cmp	r1, #0
	strne	r1, [r9, #4]
	cmp	r2, #0
	strne	r2, [r9, #8]
	cmp	r3, #0
	strne	r3, [r9, #12]
	cmp	r4, #0
	strne	r4, [r9, #16]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r7
	mov	r3, r8
	mov	r4, ip
	ldmfd	sp!, {r5-r9, pc}

@ in: r0 0 read, 1 off, 2 on; out: r0 the previous state
sound_enable:
	adr	ip, enabled
	b	read_or_set
sound_speaker:
	adr	ip, speaker
read_or_set:
	ldr	r10, [ip]
	cmp	r0, #1
	cmpne	r0, #2
	streq	r0, [ip]
	mov	r0, r10
	bx	lr

@ in: r0 channel (1-8), r1 position (-128 to read); out: r1 previous
sound_stereo:
	sub	r10, r0, #1
	cmp	r10, #8
	bxhs	lr
	adr	ip, stereo
	ldrsb	r11, [ip, r10]
	cmn	r1, #128
	strbne	r1, [ip, r10]
	mov	r1, r11
	bx	lr

@ r0 = 0: read (r0 = 0: 8-bit logarithmic only); anything else: no change
sound_mode:
	mov	r0, #0
	mov	r1, #0
	bx	lr

@ r0 = 0 read, 1 install r1 code, r2 workspace; out: r1, r2 previous
sound_linear_handler:
	adr	ip, lin_code
	ldmia	ip, {r10, r12}
	cmp	r0, #1
	stmiaeq	ip, {r1, r2}
	mov	r1, r10
	mov	r2, r12
	bx	lr

@ r0 = 0: r1 = 1 rate; 1: r1 = current index; 2: r1 index -> r2 rate
@ (1/1024 Hz); 3: r1 new index -> r1, r2 the previous index and rate
sound_sample_rate:
	cmp	r0, #0
	moveq	r1, #1
	bxeq	lr
	cmp	r0, #1
	ldreq	r1, rate_index
	ldreq	r2, rate_hz
	bxeq	lr
	cmp	r0, #2
	ldreq	r2, rate_hz
	bxeq	lr
	cmp	r0, #3
	ldreq	r1, rate_index
	ldreq	r2, rate_hz
	bx	lr
rate_hz:	.word	44100 * 1024

sound_read_sys_info:
	mov	r0, #0
	bx	lr

err_bad_swi:
	.word	0x1E6
	.asciz	"Unknown SoundDMA operation"
	.align	2

swi_names:
	.asciz	"Sound"
	.asciz	"Configure"
	.asciz	"Enable"
	.asciz	"Stereo"
	.asciz	"Speaker"
	.asciz	"Mode"
	.asciz	"LinearHandler"
	.asciz	"SampleRate"
	.asciz	"ReadSysInfo"
	.byte	0
	.align	2
