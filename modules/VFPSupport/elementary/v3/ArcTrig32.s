@ ArcTrig32: from RISC OS Open's VFPSupport s/ArcTrig32, translated to GNU as
@ by objasm2gas.py (riscos-linux-testbed), v3 variant
@ (no VFPv4: FMA in software). The original's
@ copyright and licence follow; they apply to this file.
@
@ Copyright (c) 2021 RISC OS Open Limited
@ Copyright (c) 2026 Andrew Youll
@ All rights reserved.
@
@ Redistribution and use in source and binary forms, with or without
@ modification, are permitted provided that the following conditions
@ are met:
@ 1. Redistributions of source code must retain the above copyright
@    notice, this list of conditions and the following disclaimer.
@ 2. Redistributions in binary form must reproduce the above copyright
@    notice, this list of conditions and the following disclaimer in the
@    documentation and/or other materials provided with the distribution.
@
@ THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
@ ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
@ IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
@ ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
@ FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
@ DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
@ OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
@ HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
@ LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
@ OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
@ SUCH DAMAGE.
@



@ Constraints in this translation of the original:
@ * PRESERVE8 stack alignment
@ * Only use D0-D15 to allow for D16 VFP units
@ * Flush to zero is off
@ One day we might be able to write this in C...


@ EXPORT fp32acos
@ EXPORT fp32asin
@ EXPORT fp32atan
@ EXPORT fp32atan2

	.text
	.arm


v3_ArcTrig32_fone:	.word	0x3F800000	@ (float)1
v3_ArcTrig32_done:	.word	0x00000000, 0x3FF00000	@ (double)1
v3_ArcTrig32_dhalf:	.word	0x00000000, 0x3FE00000	@ 1/2

@ float fp32acos(float x)
@ Use the identity acos(x) = pi/2 - asin(x)

@ Exceptions per ISO9899:2018 F10.1.1
@       |x| > 1 is an invalid operation
v3_fp32acos:
	VMOV    a1, s0
	push	{v1, lr}
	MOV     v1, a1, LSR #31	@ Sign of x
	BIC     a2, a1, #1<<31	@ |x|
	LDR     a4, v3_ArcTrig32_fone
	CMP     a2, a4
	BCC     20f
	BHI     10f
	TST     v1, #1
	MOVEQ   a1, #0
	VMOVEQ  s0, a1	@ acos(+1) = 0
	VLDRNE  d1, v3_ArcTrig32_dpiby2
	VADDNE.F64 d0, d1, d1	@ acos(-1) = pi
	VCVTNE.F32.F64 s0, d0
	pop	{v1, pc}
10:
	MOV     a1, #FPSCR_IOC
	BL      v3_RaiseException
	VLDR    s0, v3_ArcTrig32_fqnan	@ |x| > 1 => QNaN
	pop	{v1, pc}
20:
@ |x| < 1
	BL      v3_fp32asin
	VLDR    d1, v3_ArcTrig32_dpiby2
	VCVT.F64.F32 d0, s0
	VSUB.F64 d0, d1, d0
	VCVT.F32.F64 s0, d0
	pop	{v1, pc}

@ float fp32asin(float x)
@ Split into subranges
@ [0 0.5] asin = x + x * x^2 * polynomial(x^2)
@ [0.5 1] asin = pi/2 - 2 * asin(sqrt((1 - |x|) / 2))

@ Exceptions per ISO9899:2018 F10.1.2
@       |x| > 1 is an invalid operation
@ Based on a translation to AArch32 of
@       openlibm/src/e_asinf.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunPro, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v3_fp32asin:
@ Deal with special cases
	VMOV    a1, s0
	BICS    a2, a1, #1<<31
	BXEQ    lr	@ asin(±0) = ±0
	push	{v1, lr}
	MOV     v1, a1, LSR #31	@ Sign of x
	LDR     a4, v3_ArcTrig32_fone
	CMP     a2, a4
	BCC     10f
	VLDREQ  d0, v3_ArcTrig32_dpiby2	@ asin(±1) = ±pi/2
	BEQ     40f
	MOV     a1, #FPSCR_IOC
	BL      v3_RaiseException
	VLDR    s0, v3_ArcTrig32_fqnan	@ |x| > 1 => QNaN
	pop	{v1, pc}
10:
	ADR     a3, v3_ArcTrig32_dPSin
	VLDMIA  a3, { d4-d7 }
	VLDR    d1, v3_ArcTrig32_done

	ldr	ip, =(127 + (-1)) << 23	@ LoadExp32 ip, -1
	CMP     a2, ip
	BCS     30f
	ldr	ip, =(127 + (-12)) << 23	@ LoadExp32 ip, -12
	CMP     a2, ip
	BCS     20f
	MOV     a1, #FPSCR_IXC
	pop	{v1, lr}
	B       v3_RaiseException	@ Approx asin(x) = x
20:
@ 2^-12 <= |x| < 0.5
	VCVT.F64.F32 d0, s0
	VMUL.F64 d2, d0, d0	@ x^2
	VMLA.F64 d5, d2, d6
	VMLA.F64 d4, d2, d5
	VMUL.F64 d4, d2, d4	@ p
	VMLA.F64 d1, d2, d7	@ q
	VDIV.F64 d1, d4, d1
	VMLA.F64 d0, d0, d1
	VCVT.F32.F64 s0, d0
	pop	{v1, pc}
30:
@ 0.5 <= |x| < 1
	VCVT.F64.F32 d0, s0
	VLDR    d3, v3_ArcTrig32_dhalf
	VABS.F64 d2, d0
	VSUB.F64 d2, d1, d2	@ 1 - |x|
	VMUL.F64 d2, d2, d3	@ (1 - |x|) / 2
	VMLA.F64 d5, d2, d6
	VMLA.F64 d4, d2, d5
	VMUL.F64 d4, d2, d4	@ p
	VMLA.F64 d1, d2, d7	@ q
	VLDR    d5, v3_ArcTrig32_dpiby2
	VSQRT.F64 d2, d2
	VDIV.F64 d1, d4, d1
	VMLA.F64 d2, d2, d1
	VDIV.F64 d2, d2, d3	@ 2x
	VSUB.F64 d0, d5, d2
40:
	TST     v1, #1
	VNEGNE.F64 d0, d0	@ Fixup the sign
	VCVT.F32.F64 s0, d0
	pop	{v1, pc}

v3_ArcTrig32_fqnan:	.word	(0xFF<<23)|(1<<22)
v3_ArcTrig32_dPSin:	.word	0x9FFF7105, 0x3FC5554E	@  1.6666586697e-01
	.word	0x3FFFFBEE, 0xBFA5E277	@ -4.2743422091e-02
	.word	0x60006346, 0xBF81BA6D	@ -8.6563630030e-03
v3_ArcTrig32_dQSin:	.word	0xBFFF84BC, 0xBFE69CB5	@ -7.0662963390e-01

@ float fp32atan(float x)
@ Use the identity atan(x) = -atan(-x) to limit to positive x
@ Split into subranges
@ [0 7/16]      atan = polynomial
@ [7/16 11/16]  atan = atan(1/2) + atan((t - 0.5) / (1 + t/2))
@ [11/16 19/16] atan = atan( 1 ) + atan((t - 1) / (1 + t))
@ [19/16 39/16] atan = atan(3/2) + atan((t - 1.5) / (1 + 1.5*t))
@ [39/16 INF]   atan = atan(INF) + atan(-1/t)

@ Exceptions per ISO9899:2018 F10.1.3
@       None
@ Based on a translation to AArch32 of
@       openlibm/src/s_atanf.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunPro, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v3_fp32atan:
@ Deal with special cases
	VMOV    a1, s0
	push	{v1, lr}
	MOV     v1, a1, LSR #31	@ Sign of x
	BIC     a2, a1, #1<<31	@ |x|
	LDR     ip, =0xFF<<23
	CMP     a2, ip
	pophi	{v1, pc}	@ NaNs => propagated
	ldr	ip, =(127 + (26)) << 23	@ LoadExp32 ip, 26
	CMP     a2, ip
	BCC     10f
	VLDR    d0, v3_ArcTrig32_dtanid + (8 * 3)
	VCVT.F32.F64 s0, d0
	TST     v1, #1
	VNEGNE.F32 s0, s0	@ Fixup the sign
	pop	{v1, pc}
10:
	ldr	ip, =(127 + (-12)) << 23	@ LoadExp32 ip, -12
	CMP     a2, ip
	MOVCC   a1, #FPSCR_IXC
	popcc	{v1, lr}
	BCC     v3_RaiseException	@ Approx atan(x) = x

	LDR     ip, =0x3EE00000	@ 7/16
	CMP     a2, ip
	MOVCC   a4, #-1	@ id := -1
	BCC     80f

	VLDR    s7, v3_ArcTrig32_fone
	VABS.F32 s0, s0
@ |x| >= 7/16
	LDR     ip, =0x3F980000	@ 19/16
	CMP     a2, ip
	BCS     30f
@ 7/16 <= |x| < 19/16
	LDR     ip, =0x3F300000	@ 11/16
	CMP     a2, ip
	BCS     20f
@ 7/16 <= |x| < 11/16
	MOV     a4, #0	@ id := 0
	VADD.F32 s5, s7, s7
	VADD.F32 s4, s5, s0
	VMUL.F32 s5, s5, s0
	VSUB.F32 s5, s5, s7
	B       70f	@ (t - 0.5) / (1 + t/2)
20:
@ 11/16 <= |x| < 19/16
	MOV     a4, #1	@ id := 1
	VADD.F32 s4, s7, s0
	VSUB.F32 s5, s0, s7
	B       70f	@ (t - 1) / (1 + t)
30:
@ |x| >= 19/16
	LDR     ip, =0x401C0000	@ 39/16
	CMP     a2, ip
@ 39/16 <= |x| < 2^66
	MOVCS   a4, #3
	VDIVCS.F32 s0, s7, s0
	VNEGCS.F32 s0, s0	@ -1/t
	BCS     80f
@ 19/16 <= |x| < 39/16
	MOV     a4, #2	@ id := 2
	VLDR    s6, v3_ArcTrig32_f1point5
	VMUL.F32 s4, s6, s0
	VADD.F32 s4, s7, s4
	VSUB.F32 s5, s0, s6	@ (t - 1.5) / (1 + 1.5*t)
70:
	VDIV.F32 s0, s5, s4
80:
@ Polynomial
	VMUL.F32 s1, s0, s0	@ t^2
	VMUL.F32 s2, s1, s1	@ t^4
	ADR     a3, v3_ArcTrig32_fCatanE
	VLDMIA  a3, { s3-s7 }	@ Even and odd coefficients
	vpush	{s0-s15}	@ VFused32 s4, s2, s5
	vldr	s0, [sp, #8]
	vldr	s1, [sp, #20]
	vldr	s2, [sp, #16]
	push	{a1-a4, ip, lr}
	bl	v3_fused32_muladd
	pop	{a1-a4, ip, lr}
	vstr	s0, [sp, #16]
	vpop	{s0-s15}
	vpush	{s0-s15}	@ VFused32 s3, s2, s4
	vldr	s0, [sp, #8]
	vldr	s1, [sp, #16]
	vldr	s2, [sp, #12]
	push	{a1-a4, ip, lr}
	bl	v3_fused32_muladd
	pop	{a1-a4, ip, lr}
	vstr	s0, [sp, #12]
	vpop	{s0-s15}
	VMUL.F32 s3, s1, s3	@ s1
	vpush	{s0-s15}	@ VFused32 s6, s2, s7
	vldr	s0, [sp, #8]
	vldr	s1, [sp, #28]
	vldr	s2, [sp, #24]
	push	{a1-a4, ip, lr}
	bl	v3_fused32_muladd
	pop	{a1-a4, ip, lr}
	vstr	s0, [sp, #24]
	vpop	{s0-s15}
	VMUL.F32 s4, s2, s6	@ s2
	VADD.F32 s2, s3, s4
	VMUL.F32 s2, s0, s2	@ t * (s1 + s2)
	CMP     a4, #0
	VSUBLT.F32 s0, s0, s2
	poplt	{v1, pc}

	ADR     a3, v3_ArcTrig32_dtanid
	ADD     a3, a3, a4, LSL #3
	VLDR    d6, [a3]
@ Do an add of earlier selected constant 'id'
	VCVT.F64.F32 d2, s2
	VCVT.F64.F32 d0, s0
	VSUB.F64 d3, d2, d0
	VSUB.F64 d0, d6, d3
	VCVT.F32.F64 s0, d0
	TST     v1, #1
	VNEGNE.F32 s0, s0	@ Fixup the sign
	pop	{v1, pc}

v3_ArcTrig32_dpi:	.word	0x54442D18, 0x400921FB	@ pi
v3_ArcTrig32_dpiby4:	.word	0x54442D18, 0x3FE921FB	@ pi/4
v3_ArcTrig32_f1point5:	.word	0x3FC00000	@ (float)1.5

v3_ArcTrig32_dtanid:	.word	0x0561BB4F, 0x3FDDAC67	@ atan(0.5)
	.word	0x54442D18, 0x3FE921FB	@ atan(1.0)
	.word	0xD281F69B, 0x3FEF730B	@ atan(1.5)
v3_ArcTrig32_dpiby2:	.word	0x54442D18, 0x3FF921FB	@ atan(INF)
v3_ArcTrig32_fCatanE:	.word	0x3EAAAAA9	@  3.3333328366e-01 
	.word	0x3E11F50D	@  1.4253635705e-01 
	.word	0x3D7CAC25	@  6.1687607318e-02 
v3_ArcTrig32_fCatanO:	.word	0xBE4CCA98	@ -1.9999158382e-01 
	.word	0xBDDA1247	@ -1.0648017377e-01 

@ float fp32atan2(float y, float x)
@ Pick off special cases
@ Otherwise, call atan(y/x) then post correct the sign

@ Exceptions per ISO9899:2018 F10.1.4
@       None
@ Based on a translation to AArch32 of
@       openlibm/src/e_atan2f.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunPro, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v3_fp32atan2:
@ Deal with special cases
	push	{v1, lr}
	VMOV    a1, s0
	VMOV    a2, s1
	LDR     v1, =0xFF<<23
	BIC     ip, a1, #1<<31
	CMP     ip, v1
	pophi	{v1, pc}	@ atan2(NaN,anything) = NaN
	BIC     ip, a2, #1<<31
	CMP     ip, v1
	VMOVHI.F32 s0, s1
	pophi	{v1, pc}	@ atan2(anything,NaN) = NaN

@ Now y & x are not NaN
	MOVS    ip, a1, LSL #1
	BNE     10f
	TST     a2, #1<<31
	VMOVEQ  d0, ip, ip	@ atan2(±0, +not NaN) = ±0
	VLDRNE  d0, v3_ArcTrig32_dpi	@ atan2(±0, -not NaN) = ±pi
	B       50f
10:
@ Now y & x are not NaN, y is non zero
	MOVS    ip, a2, LSL #1
	BNE     20f
	VLDR    d0, v3_ArcTrig32_dpiby2	@ atan2(±not 0 not NaN, 0) = ±pi/2
	B       50f
20:
@ Now y & x are not NaN, y & x are non zero
	BIC     ip, a1, #1<<31
	CMP     ip, v1
	BEQ     30f
	BIC     ip, a2, #1<<31
	CMP     ip, v1
	BNE     60f
	TST     a2, #1<<31
	MOVEQ   ip, #0
	VMOVEQ  d0, ip, ip	@ atan2(±not INF not NaN, +INF) = ±0
	VLDRNE  d0, v3_ArcTrig32_dpi	@ atan2(±not INF not NaN, -INF) = ±pi
	B       50f
30:
@ y = INF
	BIC     ip, a2, #1<<31
	CMP     ip, v1
	VLDRNE  d0, v3_ArcTrig32_dpiby2
	BNE     50f	@ atan2(±INF, not 0 not NAN not INF) = ±pi/2

@ y & x = INF
	TST     a2, #1<<31
	VLDR    d0, v3_ArcTrig32_dpiby4	@ atan2(±INF, +INF) = ±pi/4
	VLDR    d1, v3_ArcTrig32_dpiby2
	VADDNE.F64 d0, d0, d1	@ atan2(±INF, -INF) = ±3*pi/4
50:
@ Copy sign of y
	TST     a1, #1<<31
	VNEGNE.F64 d0, d0
	VCVT.F32.F64 s0, d0
	pop	{v1, pc}
60:
@ Compute y/x
	mov	ip, a1, lsl #1	@ ExpBits32 ip, a1
	mov	ip, ip, lsr #24
	mov	lr, a2, lsl #1	@ ExpBits32 lr, a2
	mov	lr, lr, lsr #24
	SUB     ip, ip, lr	@ Exponent difference
	MOV     v1, a1, LSR #31
	TST     a2, #1<<31
	ORRNE   v1, v1, #2	@ Remember signs b1 = s-o-x, b0 = s-o-y
	CMP     ip, #26
	VLDRGT  d0, v3_ArcTrig32_dpiby2
	ANDGT   v1, v1, #1	@ |y/x| > 2^26
	BGT     80f
	TST     v1, #2
	BEQ     70f
	CMP     ip, #-26
	MOVLT   ip, #0
	VMOVLT  d0, ip, ip
	BLT     80f	@ |y|/x < -2^-26
70:
@ Safe to do y/x
	VDIV.F32 s0, s0, s1
	VABS.F32 s0, s0
	BL      v3_fp32atan
	VCVT.F64.F32 d0, s0
80:
	CMP     v1, #1
	VNEGEQ.F64 d0, d0	@ atan2(-,+)
	VCVTLS.F32.F64 s0, d0
	popls	{v1, pc}	@            and atan2(+,+)
	VLDR    d1, v3_ArcTrig32_dpi
	CMP     v1, #3
	VSUBCC.F64 d2, d1, d0	@ atan2(+,-)
	VSUBEQ.F64 d2, d0, d1	@            and atan2(-,-)
	VCVT.F32.F64 s0, d2
	pop	{v1, pc}


	.ltorg				@ objasm's literal pool at the end of the AREA
	.if !((( v3_ArcTrig32_dQSin) - (v3_ArcTrig32_dPSin + (3*8) )) == 0)
	.error "ASSERT failed: v3_ArcTrig32_dPSin + (3*8) = v3_ArcTrig32_dQSin"
	.endif
	.if !((( v3_ArcTrig32_fCatanO) - (v3_ArcTrig32_fCatanE + (3*4) )) == 0)
	.error "ASSERT failed: v3_ArcTrig32_fCatanE + (3*4) = v3_ArcTrig32_fCatanO"
	.endif
