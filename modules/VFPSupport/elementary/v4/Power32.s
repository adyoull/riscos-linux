@ Power32: from RISC OS Open's VFPSupport s/Power32, translated to GNU as
@ by objasm2gas.py (riscos-linux-testbed), v4 variant
@ (VFPv4: VFMA). The original's
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


@ EXPORT fp32log10
@ EXPORT fp32log
@ EXPORT fp32pow
@ EXPORT fp32exp

	.text
	.arm


@ float fp32log10(float x)
@ Find k and f, such that x = 2^k * (1 + f)
@ where 1 + f is in the range [2^-0.5 2^+0.5]
@ Approximate log(1 + f) using a polynomial, change base to 10.

@ Exceptions per ISO9899:2018 F10.3.8
@       x = ±0 is a divide by zero
@       x < 0 is an invalid operation
@ Based on a translation to AArch32 of
@       openlibm/src/e_log10f.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunPro, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v4_fp32log10:
@ Deal with special cases
	VMOV    a2, s0
	push	{v1, lr}
	LDR     v1, =0xFF<<23
	TEQ     a2, v1	@ log(+INF) = +INF
	BEQ     10f
	BICS    a3, a2, #1<<31
	ORREQ   v1, v1, #1<<31	@ log(±0) = -INF
	MOVEQ   a1, #FPSCR_DZC
	BEQ     15f
	CMP     a3, v1
	pophi	{v1, pc}	@ Propagate NaNs
	TST     a2, #1<<31
	ORRNE   v1, v1, #1<<22	@ x < 0 => QNaN
	BEQ     20f
10:
	MOV     a1, #FPSCR_IOC
15:
	BL      v4_RaiseException
	VMOV    s0, v1
	pop	{v1, pc}
20:
@ x is not 0, NaN, nor INF, and is positive
	MOV     v1, #0	@ k
	VLDR    s1, v4_Power32_fone
	ldr	ip, =(127 + (-126)) << 23	@ LoadExp32 ip, -126
	CMP     a2, ip
	VLDRCC  s2, v4_Power32_ftwo25
	VMULCC.F32 s0, s0, s2
	VMOVCC  a2, s0
	SUBCC   v1, v1, #25	@ Scale up by 2^25 to normalise
	mov	a3, a2, lsl #1	@ ExpBits32 a3, a2
	mov	a3, a3, lsr #24
	SUB     a3, a3, #FLT_EXP_BIAS
	ADD     v1, v1, a3	@ k += unbiased exponent
	SUB     ip, ip, #1	@ ip := &7FFFFF
	AND     a2, a2, ip
	LDR     a4, =0x95F64<<3
	VMOV    a3, s1
	ADD     a4, a4, a2
	ANDS    a4, a4, #1<<23
	EOR     a4, a4, a3
	ORR     ip, a2, a4
	VMOV    s0, ip
	ADDNE   v1, v1, #1	@ Normalised to x or x/2
	VMOV    s4, v1
	VCVT.F32.S32 s4, s4	@ s4 = (float)k
	VSUB.F32 s0, s0, s1	@ f
30:
@ Remez polynomial
	ADR     a3, v4_Power32_fClogO
	VLDMIA  a3, { s12-s15 }
	VADD.F32 s2, s1, s1	@ 2
	VADD.F32 s3, s2, s0
	VDIV.F32 s3, s0, s3	@ s = f / (2 + f)
	VLDR    s2, v4_Power32_fhalf
	VMUL.F32 s5, s3, s3	@ s^2
	VMUL.F32 s6, s5, s5	@ s^4
	vfma.f32	s14, s6, s15	@ VFused32 s14, s6, s15
	VMUL.F32 s14, s6, s14
	vfma.f32	s12, s6, s13	@ VFused32 s12, s6, s13
	VMUL.F32 s12, s5, s12
	VADD.F32 s1, s12, s14	@ R = poly

	VLDR    s5, v4_Power32_flog2
	VLDR    s6, v4_Power32_flog2r
	VMUL.F32 s2, s0, s2
	VMUL.F32 s2, s0, s2	@ 1/2 * f^2
	VADD.F32 s7, s2, s1
	VMUL.F32 s7, s3, s7
	VSUB.F32 s1, s0, s2	@ s1 := hi
	VMOV    a3, s1
	BIC     a3, a3, #0xFF0
	BIC     a3, a3, #0x00F
	VMOV    s1, a3
	VSUB.F32 s0, s0, s1
	VSUB.F32 s0, s0, s2
	VLDR    s8, v4_Power32_f1uponln10
	VLDR    s9, v4_Power32_f1uponln10r
	VADD.F32 s0, s0, s7	@ s0 := lo

@ Change of base for (hi,lo) from e to 10
	VADD.F32 s7, s0, s1
	VMUL.F32 s2, s4, s5
	VMLA.F32 s2, s4, s6
	VMLA.F32 s2, s7, s9
	VMLA.F32 s2, s0, s8
	VMLA.F32 s2, s1, s8
	VMOV.F32 s0, s2
	pop	{v1, pc}

v4_Power32_fone:	.word	0x3F800000	@ (float)1
v4_Power32_fhalf:	.word	0x3F000000	@ 1/2
v4_Power32_ftwo25:	.word	0x4C000000	@ (float)1<<25
v4_Power32_fln2:	.word	0x3F317180	@ 6.9313812256e-01
v4_Power32_fln2r:	.word	0x3717F7D1	@ Next 9sf of ln(2)
v4_Power32_flog2:	.word	0x3E9A2080	@ 3.0102920532e-01
v4_Power32_flog2r:	.word	0x355427DB	@ Next 9sf of log(2)
v4_Power32_f1uponln10:	.word	0x3EDE6000	@ 4.3432617188e-01
v4_Power32_f1uponln10r:	.word	0xB804EAD9	@ Next 9sf of 1/ln(10)

@ float fp32log(float x)
@ Find k and f, such that x = 2^k * (1 + f)
@ where 1 + f is in the range [2^-0.5 2^+0.5]
@ Approximate log(1 + f) using a polynomial.

@ Exceptions per ISO9899:2018 F10.3.7
@       x = ±0 is a divide by zero
@       x < 0 is an invalid operation
@ Based on a translation to AArch32 of
@       openlibm/src/e_logf.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunPro, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v4_fp32log:
@ Deal with special cases
	VMOV    a2, s0
	push	{v1, lr}
	LDR     v1, =0xFF<<23
	TEQ     a2, v1	@ log(+INF) = +INF
	BEQ     10f
	BICS    a3, a2, #1<<31
	ORREQ   v1, v1, #1<<31	@ log(±0) = -INF
	MOVEQ   a1, #FPSCR_DZC
	BEQ     15f
	CMP     a3, v1
	pophi	{v1, pc}	@ Propagate NaNs
	TST     a2, #1<<31
	ORRNE   v1, v1, #1<<22	@ x < 0 => QNaN
	BEQ     20f
10:
	MOV     a1, #FPSCR_IOC
15:
	BL      v4_RaiseException
	VMOV    s0, v1
	pop	{v1, pc}
20:
@ x is not 0, NaN, nor INF, and is positive
	MOV     v1, #0	@ k
	VLDR    s1, v4_Power32_fone
	ldr	ip, =(127 + (-126)) << 23	@ LoadExp32 ip, -126
	CMP     a2, ip
	VLDRCC  s2, v4_Power32_ftwo25
	VMULCC.F32 s0, s0, s2
	VMOVCC  a2, s0
	SUBCC   v1, v1, #25	@ Scale up by 2^25 to normalise
	mov	a3, a2, lsl #1	@ ExpBits32 a3, a2
	mov	a3, a3, lsr #24
	SUB     a3, a3, #FLT_EXP_BIAS
	ADD     v1, v1, a3	@ k += unbiased exponent
	SUB     ip, ip, #1	@ ip := &7FFFFF
	AND     a2, a2, ip
	LDR     a4, =0x95F64<<3
	VMOV    a3, s1
	ADD     a4, a4, a2
	ANDS    a4, a4, #1<<23
	EOR     a4, a4, a3
	ORR     ip, a2, a4
	VMOV    s0, ip
	ADDNE   v1, v1, #1	@ Normalised to x or x/2
	VMOV    s4, v1
	VCVT.F32.S32 s4, s4	@ s4 = (float)k
	VSUB.F32 s0, s0, s1	@ f
30:
@ Remez polynomial
	ADR     a3, v4_Power32_fClogO
	VLDMIA  a3, { s12-s15 }
	VADD.F32 s2, s1, s1	@ 2
	VADD.F32 s3, s2, s0
	VDIV.F32 s3, s0, s3	@ s = f / (2 + f)
	VLDR    s2, v4_Power32_fhalf
	VMUL.F32 s5, s3, s3	@ s^2
	VMUL.F32 s6, s5, s5	@ s^4
	vfma.f32	s14, s6, s15	@ VFused32 s14, s6, s15
	VMUL.F32 s14, s6, s14
	vfma.f32	s12, s6, s13	@ VFused32 s12, s6, s13
	VMUL.F32 s12, s5, s12
	VADD.F32 s1, s12, s14	@ R = poly

	LDR     a3, =0x6147A<<3
	LDR     a4, =0x6B851<<3
	SUB     a3, a2, a3
	SUB     a4, a4, a2
	ORRS    a2, a3, a4
	VLDR    s5, v4_Power32_fln2
	VLDR    s6, v4_Power32_fln2r
	BLE     40f

	VMUL.F32 s2, s0, s2
	VMUL.F32 s2, s0, s2	@ 1/2 * f^2
	VADD.F32 s7, s2, s1
	VMUL.F32 s7, s3, s7
	VMLA.F32 s7, s4, s6
	VSUB.F32 s1, s2, s7
	B       50f
40:
	VSUB.F32 s2, s0, s1
	VMUL.F32 s1, s2, s3	@ s * (f - R)
	VMLS.F32 s1, s4, s6
50:
	VMUL.F32 s5, s5, s4
	VSUB.F32 s1, s1, s0
	VSUB.F32 s0, s5, s1	@ log(x) = k * log(2) - (equation - f)
	pop	{v1, pc}

v4_Power32_fexpmax:	.word	0x42b17180	@  8.8721679688e+01
v4_Power32_fexpmin:	.word	0xc2cff1b5	@ -1.0397208405e+02
v4_Power32_fCexp:	.word	0x3E2AAA8F	@  1.6666625440e-1
	.word	0xBB355215	@ -2.7667332906e-3
v4_Power32_f2pow127:	.word	0x7F000000	@ (float)2^127
v4_Power32_f1uponln2:	.word	0x3FB8AA3B	@  1.4426950216e+00

v4_Power32_fClogO:	.word	0x3F2AAAAA	@ 0.66666662693
	.word	0x3E91E9EE	@ 0.28498786688
v4_Power32_fClogE:	.word	0x3ECCCE13	@ 0.40000972152
	.word	0x3E789E26	@ 0.24279078841

@ float fp32exp(float y)
@ Find r and k such that    x = k*ln(2) + r
@                        |r| <= ln(2)/2
@ Approximate exp(r) over the range [0 ln(2)/2] then scale back
@ to          exp(y) = 2^k * exp(r)

@ Exceptions per ISO9899:2018 F10.3.1
@       None
@ Based on a translation to AArch32 of
@       openlibm/src/e_expf.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunPro, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v4_fp32exp:
	LDR     ip, =0xFF<<23
	VMOV    a2, s0
	BIC     a3, a2, #1<<31
	CMP     a3, ip
	BXHI    lr	@ Propagate NaNs
	BNE     10f

	TST     a2, #1<<31
	MOVNE   a3, #0
	VMOVNE  s0, a3	@ e^-INF = 0
	BX      lr	@ e^+INF = +INF
10:
	VLDR    s1, v4_Power32_fexpmax
	VLDR    s2, v4_Power32_fexpmin
	VCMP.F32 s0, s1
	VMRS    APSR_nzcv, FPSCR
	VMOVGT  s0, ip
	MOVGT   a1, #FPSCR_OFC
	BGT     v4_RaiseException	@ Will overflow
	VCMP.F32 s0, s2
	VMRS    APSR_nzcv, FPSCR
	VSUBLT.F32 s0, s0, s0
	MOVLT   a1, #FPSCR_UFC
	BLT     v4_RaiseException	@ Will underflow

	push	{v1, lr}
	MOV     v1, #0	@ k = 0
	VMOV    s2, v1	@ hi = 0.0
	VMOV.F32 s1, s2	@ lo = 0.0
	LDR     ip, =0x3EB17218	@ Approx 1/2 ln(2)
	CMP     a3, ip
	BLS     30f
	VLDR    s5, v4_Power32_fln2
	VLDR    s6, v4_Power32_fln2r
	LDR     ip, =0x3F851592	@ Approx 3/2 ln(2)
	CMP     a3, ip
	BCS     20f
@ 1/2 ln(2) < |y| < 3/2 ln(2)
	TST     a2, #1<<31
	VNEGNE.F32 s5, s5
	VNEGNE.F32 s6, s6
	VSUB.F32 s2, s0, s5	@ hi
	VMOV.F32 s1, s6	@ lo
	ADDEQ   v1, v1, #1
	SUBNE   v1, v1, #1
	B       40f
20:
@ |y| >= 3/2 ln(2)
	TST     a2, #1<<31
	VLDR    s3, v4_Power32_fhalf
	VLDR    s4, v4_Power32_f1uponln2
	VNEGNE.F32 s3, s3
	VMLA.F32 s3, s4, s0
	VCVT.S32.F32 s3, s3	@ k = integer round towards zero
	VMOV    v1, s3
	VCVT.F32.S32 s3, s3
	VMUL.F32 s5, s3, s5
	VSUB.F32 s2, s0, s5	@ hi
	VMUL.F32 s1, s3, s6	@ lo
	B       40f
	B       40f
30:
@ |y| <= 1/2 ln(2)
	ldr	ip, =(127 + (-14)) << 23	@ LoadExp32 ip, -14
	CMP     a3, ip
	BCS     50f
	MOV     a1, #FPSCR_IXC
	BL      v4_RaiseException
	VLDR    s1, v4_Power32_fone
	VADD.F32 s0, s1, s0
	pop	{v1, pc}	@ Tiny, so approximate exp(y) = 1 + y
40:
	VSUB.F32 s0, s2, s1	@ y = hi - lo
50:
@ 0 <= |y| <= 1/2 ln(2)
	VMUL.F32 s3, s0, s0	@ y^2
	ADR     a3, v4_Power32_fCexp
	VLDMIA  a3, { s4-s5 }
	vfma.f32	s4, s3, s5	@ VFused32 s4, s3, s5
	VLDR    s5, v4_Power32_fone
	VADD.F32 s6, s5, s5	@ 2
	VMUL.F32 s4, s4, s3
	VSUB.F32 s3, s0, s4	@ s3 := poly
	CMP     v1, #0
	BNE     60f

	VSUB.F32 s4, s3, s6	@                          (poly - 2)
	VMUL.F32 s7, s0, s3	@               (y * poly)
	VDIV.F32 s7, s7, s4
	VSUB.F32 s7, s7, s0
	VSUB.F32 s0, s5, s7	@ exp(y) = 1 - ((y * poly)/(poly - 2) - x)
	pop	{v1, pc}
60:
	VSUB.F32 s4, s6, s3	@                                (2 - poly)
	VMUL.F32 s7, s0, s3	@                     (y * poly)
	VDIV.F32 s7, s7, s4
	VSUB.F32 s1, s1, s7	@                lo - (y * poly)/(2 - poly)
	VSUB.F32 s1, s1, s2	@              ((lo - (y * poly)/(2 - poly)) - hi)
	VSUB.F32 s1, s5, s1	@ exp(r) = 1 - ((lo - (y * poly)/(2 - poly)) - hi)

@ k non zero, need 2^k scaler
	CMP     v1, #128	@ Careful making an exponent look like a NaN
	VLDREQ  s4, v4_Power32_f2pow127
	VMULEQ.F32 s1, s1, s6
	VMULEQ.F32 s0, s1, s4
	popeq	{v1, pc}

	ldr	a4, =(127 + (0)) << 23	@ LoadExp32 a4, 0
	CMP     v1, #-125
	ADD     a3, a4, v1, LSL #23	@ Exponent of 2^k
	ADDLT   a3, a3, #100<<23	@             2^(k+100)
	VMOV    s2, a3
	VMUL.F32 s0, s1, s2
	ldr	a3, =(127 + (-100)) << 23	@ LoadExp32 a3, -100
	VMOVLT  s2, a3
	VMULLT.F32 s0, s0, s2
	pop	{v1, pc}

@ float fp32pow(float x, float y)
@ This is such a large function that there's little to gain from having an optimised
@ version for single precision, it'll be (approximately) as slow. Just check for unusual
@ values then do a widening cast to the double precision implementation.

@ Exceptions per ISO9899:2018 F10.4.4
@       x = ±0, y < 0 and an odd integer, is a divide by zero
@       x = ±0, finite y < 0 and not an odd integer, is a divide by zero
@       x = ±0, y = -INF may raise a divide by zero
@       finite x < 0, finite y which is not an integer, is an invalid operation
v4_fp32pow:
	VMOV    a2, s0
	VMOV    a4, s1
	BICS    ip, a4, #1<<31
	VLDREQ  s0, v4_Power32_fone	@ anything^0 = 1
	BXEQ    lr
	ldr	ip, =(127 + (0)) << 23	@ LoadExp32 ip, 0
	TEQ     a2, ip
	BXEQ    lr	@ 1^anything = 1
	TEQ     a4, ip
	BXEQ    lr	@ anything^1 = itself


	BIC     a1, a2, #1<<31
	BIC     a3, a4, #1<<31
	LDR     ip, =0xFF<<23
	CMP     a1, ip
	CMPLS   a3, ip
10:
	VADDHI.F32 s0, s0, s1	@ Propagate NaNs
	BXHI    lr

@ x is not 1, y is not 0 or 1, neither are NaN
	push	{v1, lr}
	VCVT.F64.F32 d1, s1
	VCVT.F64.F32 d0, s0
	BL      v4_fp64pow
	VCVT.F32.F64 s0, d0
	pop	{v1, pc}


	.ltorg				@ objasm's literal pool at the end of the AREA
	.if !((( v4_Power32_fClogE) - (v4_Power32_fClogO + (2*4) )) == 0)
	.error "ASSERT failed: v4_Power32_fClogO + (2*4) = v4_Power32_fClogE"
	.endif
	.if !((( v4_Power32_fClogE) - (v4_Power32_fClogO + (2*4) )) == 0)
	.error "ASSERT failed: v4_Power32_fClogO + (2*4) = v4_Power32_fClogE"
	.endif
