@ Power64: from RISC OS Open's VFPSupport s/Power64, translated to GNU as
@ by objasm2gas.py (riscos-linux-testbed), v3 variant
@ (no VFPv4: FMA in software). The original's
@ copyright and licence follow; they apply to this file.
@
@ Copyright (c) 2021 RISC OS Open Limited
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


@ EXPORT fp64log10
@ EXPORT fp64log
@ EXPORT fp64pow
@ EXPORT fp64exp

	.text
	.arm


@ double fp64log10(double x)
@ Find k and f, such that x = 2^k * (1 + f)
@ where 1 + f is in the range [2^-0.5 2^+0.5]
@ Approximate log(1 + f) using a polynomial, change base to 10.

@ Exceptions per ISO9899:2018 F10.3.8
@       x = ±0 is a divide by zero
@       x < 0 is an invalid operation
@ Based on a translation to AArch32 of
@       openlibm/src/e_log10.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunSoft, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v3_fp64log10:
	VMOV    a1, a2, d0
	push	{v1, lr}
	LDR     v1, =0x7FF<<20
	TEQ     a2, v1
	TEQEQ   a1, #0	@ log(+INF) = +INF
	BEQ     10f
	ORRS    a3, a1, a2, LSL #1
	ORREQ   v1, v1, #1<<31	@ log(±0) = -INF
	MOVEQ   a1, #FPSCR_DZC
	BEQ     15f
	BIC     a3, a2, #1<<31
	CMP     a3, v1
	CMPEQ   a1, #0
	pophi	{v1, pc}	@ Propagate NaNs
	TST     a2, #1<<31
	ORRNE   v1, v1, #1<<19	@ x < 0 => QNaN
	BEQ     20f
10:
	MOV     a1, #FPSCR_IOC
15:
	BL      v3_RaiseException
	MOV     a1, #0
	VMOV    d0, a1, v1
	pop	{v1, pc}
20:
@ x is not 0, NaN, nor INF, and is positive
	MOV     v1, #0	@ k
	VLDR    d1, v3_Power64_done
	ldr	ip, =(1023 + (-1022)) << 20	@ LoadExp64 ip, -1022
	CMP     a2, ip
	CMPEQ   a1, #0
	VLDRCC  d2, v3_Power64_dtwo54
	VMULCC.F64 d0, d0, d2
	VMOVCC  a1, a2, d0
	SUBCC   v1, v1, #54	@ Scale up by 2^54 to normalise
	mov	a3, a2, lsl #1	@ ExpBits64 a3, a2
	mov	a3, a3, lsr #21
	LDR     a4, =DBL_EXP_BIAS
	SUB     a3, a3, a4
	ADD     v1, v1, a3	@ k += unbiased exponent
	SUB     ip, ip, #1	@ ip := &FFFFF
	AND     a2, a2, ip
	LDR     a4, =0x95F64
	VMOV    a3, s3	@ High word of done
	ADD     a4, a4, a2
	ANDS    a4, a4, #1<<20
	EOR     a4, a4, a3
	ORR     ip, a2, a4
	VMOV    s1, ip	@ High word of x
	ADDNE   v1, v1, #1	@ Normalised to x or x/2
	VMOV    s4, v1
	VCVT.F64.S32 d4, s4	@ d4 = (double)k
	VSUB.F64 d0, d0, d1	@ f
30:
@ Remez polynomial
	VPUSH   { d8-d10 }
	ADR     a3, v3_Power64_dClogO
	VLDMIA  a3!, { d7-d10 }
	VADD.F64 d2, d1, d1	@ 2
	VADD.F64 d3, d2, d0
	VDIV.F64 d3, d0, d3	@ s = f / (2 + f)
	VLDR    d2, v3_Power64_dhalf
	VMUL.F64 d5, d3, d3	@ s^2
	VMUL.F64 d6, d5, d5	@ s^4
	vpush	{d0-d10}	@ VFused64 d9, d6, d10
	vldr	d0, [sp, #48]
	vldr	d1, [sp, #80]
	vldr	d2, [sp, #72]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #72]
	vpop	{d0-d10}
	VMLA.F64 d8, d6, d9
	VMLA.F64 d7, d6, d8
	VMUL.F64 d7, d5, d7
	VLDMIA  a3, { d8-d10 }
	vpush	{d0-d10}	@ VFused64 d9, d6, d10
	vldr	d0, [sp, #48]
	vldr	d1, [sp, #80]
	vldr	d2, [sp, #72]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #72]
	vpop	{d0-d10}
	VMLA.F64 d8, d6, d9
	VMUL.F64 d8, d6, d8
	VADD.F64 d1, d7, d8	@ R = poly
	VPOP    { d8-d10 }

	VLDR    d5, v3_Power64_dlog2
	VLDR    d6, v3_Power64_dlog2r
	VMUL.F64 d2, d0, d2
	VMUL.F64 d2, d0, d2	@ 1/2 * f^2
	VADD.F64 d7, d2, d1
	VMUL.F64 d7, d3, d7
	VSUB.F64 d1, d0, d2	@ d1 := hi
	MOV     a3, #0
	VMOV    s2, a3	@ Low word of hi
	VSUB.F64 d0, d0, d1
	VSUB.F64 d0, d0, d2
	VLDR    d2, v3_Power64_d1uponln10
	VLDR    d3, v3_Power64_d1uponln10r
	VADD.F64 d0, d0, d7	@ d0 := lo

@ Change of base for (hi,lo) from e to 10
	VMUL.F64 d5, d4, d5
	VADD.F64 d7, d0, d1
	VMUL.F64 d7, d7, d3
	VMLA.F64 d7, d0, d2
	VMLA.F64 d7, d4, d6
	VMLA.F64 d5, d1, d2
	VADD.F64 d0, d5, d7
	pop	{v1, pc}

v3_Power64_dhalf:	.word	0x00000000, 0x3FE00000	@ 1/2
v3_Power64_dtwo54:	.word	0x00000000, 0x43500000	@ (double)1<<54
v3_Power64_dln2_20:	.word	0xFEE00000, 0x3FE62E42	@ 6.93147180369123816490e-01
v3_Power64_dln2_20r:	.word	0x35793C76, 0x3DEA39EF	@ Next 17sf of ln(2)
v3_Power64_dlog2:	.word	0x509F6000, 0x3FD34413	@ 3.01029995663611771306e-01
v3_Power64_dlog2r:	.word	0x11F12B36, 0x3D59FEF3	@ Next 17sf of log(2)
v3_Power64_d1uponln10:	.word	0x15200000, 0x3FDBCB7B	@ 4.34294481878168880939e-01
v3_Power64_d1uponln10r:	.word	0xCA9AADD5, 0x3DBB9438	@ Next 17sf of 1/ln(10)

@ double fp64log(double x)
@ Find k and f, such that x = 2^k * (1 + f)
@ where 1 + f is in the range [2^-0.5 2^+0.5]
@ Approximate log(1 + f) using a polynomial.

@ Exceptions per ISO9899:2018 F10.3.7
@       x = ±0 is a divide by zero
@       x < 0 is an invalid operation
@ Based on a translation to AArch32 of
@       openlibm/src/e_log.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunSoft, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v3_fp64log:
	VMOV    a1, a2, d0
	push	{v1, lr}
	LDR     v1, =0x7FF<<20
	TEQ     a2, v1
	TEQEQ   a1, #0	@ log(+INF) = +INF
	BEQ     10f
	ORRS    a3, a1, a2, LSL #1
	ORREQ   v1, v1, #1<<31	@ log(±0) = -INF
	MOVEQ   a1, #FPSCR_DZC
	BEQ     15f
	BIC     a3, a2, #1<<31
	CMP     a3, v1
	CMPEQ   a1, #0
	pophi	{v1, pc}	@ Propagate NaNs
	TST     a2, #1<<31
	ORRNE   v1, v1, #1<<19	@ x < 0 => QNaN
	BEQ     20f
10:
	MOV     a1, #FPSCR_IOC
15:
	BL      v3_RaiseException
	MOV     a1, #0
	VMOV    d0, a1, v1
	pop	{v1, pc}
20:
@ x is not 0, NaN, nor INF, and is positive
	MOV     v1, #0	@ k
	VLDR    d1, v3_Power64_done
	ldr	ip, =(1023 + (-1022)) << 20	@ LoadExp64 ip, -1022
	CMP     a2, ip
	CMPEQ   a1, #0
	VLDRCC  d2, v3_Power64_dtwo54
	VMULCC.F64 d0, d0, d2
	VMOVCC  a1, a2, d0
	SUBCC   v1, v1, #54	@ Scale up by 2^54 to normalise
	mov	a3, a2, lsl #1	@ ExpBits64 a3, a2
	mov	a3, a3, lsr #21
	LDR     a4, =DBL_EXP_BIAS
	SUB     a3, a3, a4
	ADD     v1, v1, a3	@ k += unbiased exponent
	SUB     ip, ip, #1	@ ip := &FFFFF
	AND     a2, a2, ip
	LDR     a4, =0x95F64
	VMOV    a3, s3	@ High word of done
	ADD     a4, a4, a2
	ANDS    a4, a4, #1<<20
	EOR     a4, a4, a3
	ORR     ip, a2, a4
	VMOV    s1, ip	@ High word of x
	ADDNE   v1, v1, #1	@ Normalised to x or x/2
	VMOV    s4, v1
	VCVT.F64.S32 d4, s4	@ d4 = (double)k
	VSUB.F64 d0, d0, d1	@ f
30:
@ Remez polynomial
	VPUSH   { d8-d10 }
	ADR     a3, v3_Power64_dClogO
	VLDMIA  a3!, { d7-d10 }
	VADD.F64 d2, d1, d1	@ 2
	VADD.F64 d3, d2, d0
	VDIV.F64 d3, d0, d3	@ s = f / (2 + f)
	VLDR    d2, v3_Power64_dhalf
	VMUL.F64 d5, d3, d3	@ s^2
	VMUL.F64 d6, d5, d5	@ s^4
	vpush	{d0-d10}	@ VFused64 d9, d6, d10
	vldr	d0, [sp, #48]
	vldr	d1, [sp, #80]
	vldr	d2, [sp, #72]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #72]
	vpop	{d0-d10}
	VMLA.F64 d8, d6, d9
	VMLA.F64 d7, d6, d8
	VMUL.F64 d7, d5, d7
	VLDMIA  a3, { d8-d10 }
	vpush	{d0-d10}	@ VFused64 d9, d6, d10
	vldr	d0, [sp, #48]
	vldr	d1, [sp, #80]
	vldr	d2, [sp, #72]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #72]
	vpop	{d0-d10}
	VMLA.F64 d8, d6, d9
	VMUL.F64 d8, d6, d8
	VADD.F64 d1, d7, d8	@ R = poly
	VPOP    { d8-d10 }

	LDR     a3, =0x6147A
	LDR     a4, =0x6B851
	SUB     a3, a2, a3
	SUB     a4, a4, a2
	ORRS    a2, a3, a4
	VLDR    d5, v3_Power64_dln2_20
	VLDR    d6, v3_Power64_dln2_20r
	BLE     40f

	VMUL.F64 d2, d0, d2
	VMUL.F64 d2, d0, d2	@ 1/2 * f^2
	VADD.F64 d7, d2, d1
	VMUL.F64 d7, d3, d7
	VMLA.F64 d7, d4, d6
	VSUB.F64 d1, d2, d7
	B       50f
40:
	VSUB.F64 d2, d0, d1
	VMUL.F64 d1, d2, d3	@ s * (f - R)
	VMLS.F64 d1, d4, d6
50:
	VMUL.F64 d5, d5, d4
	VSUB.F64 d1, d1, d0
	VSUB.F64 d0, d5, d1	@ log(x) = k * log(2) - (equation - f)
	pop	{v1, pc}

v3_Power64_done:	.word	0x00000000, 0x3FF00000	@ (double)1
v3_Power64_dexpmax:	.word	0xFEFA39EF, 0x40862E42	@  7.09782712893383973096e+02
v3_Power64_dexpmin:	.word	0xD52D3051, 0xc0874910	@ -7.45133219101941108420e+02
v3_Power64_de1:	.word	0x8B145769, 0x4005BF0A	@ e^1
v3_Power64_dCexp:	.word	0x5555553E, 0x3FC55555	@  1.66666666666666019037e-01
	.word	0x16BEBD93, 0xBF66C16C	@ -2.77777777770155933842e-03
	.word	0xAF25DE2C, 0x3F11566A	@  6.61375632143793436117e-05
	.word	0xC5D26BF1, 0xBEBBBD41	@ -1.65339022054652515390e-06
	.word	0x72BEA4D0, 0x3E663769	@  4.13813679705723846039e-08
v3_Power64_d2pow1023:	.word	0x00000000, 0x7FE00000	@ (double)2^1023
v3_Power64_d1uponln2:	.word	0x652B82FE, 0x3FF71547	@  1.44269504088896338700e+00

v3_Power64_dClogO:	.word	0x55555593, 0x3FE55555	@ 6.666666666666735130e-01
	.word	0x94229359, 0x3FD24924	@ 2.857142874366239149e-01
	.word	0x96CB03DE, 0x3FC74664	@ 1.818357216161805012e-01
	.word	0xDF3E5244, 0x3FC2F112	@ 1.479819860511658591e-01
v3_Power64_dClogE:	.word	0x9997FA04, 0x3FD99999	@ 3.999999999940941908e-01
	.word	0x1D8E78AF, 0x3FCC71C5	@ 2.222219843214978396e-01
	.word	0xD078C69F, 0x3FC39A09	@ 1.531383769920937332e-01

@ double fp64exp(double y)
@ Special case e^1 for speed and accuracy.
@ Find r and k such that    x = k*ln(2) + r
@                        |r| <= ln(2)/2
@ Approximate exp(r) over the range [0 ln(2)/2] then scale back
@ to          exp(y) = 2^k * exp(r)

@ Exceptions per ISO9899:2018 F10.3.1
@       None
@ Based on a translation to AArch32 of
@       openlibm/src/e_exp.c
@       Copyright (C) 2004 by Sun Microsystems, Inc. All rights reserved.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v3_fp64exp:
	LDR     ip, =0x7FF<<20
	VMOV    a1, a2, d0
	BIC     a3, a2, #1<<31
	CMP     a3, ip
	CMPEQ   a1, #0
	BXHI    lr	@ Propagate NaNs
	BNE     10f

	TST     a2, #1<<31
	MOVNE   a3, #0
	VMOVNE  d0, a3, a3	@ e^-INF = 0
	BX      lr	@ e^+INF = +INF
10:
	VLDR    d1, v3_Power64_dexpmax
	VLDR    d2, v3_Power64_dexpmin
	VCMP.F64 d0, d1
	VMRS    APSR_nzcv, FPSCR
	MOVGT   a3, #0
	VMOVGT  d0, a3, ip
	MOVGT   a1, #FPSCR_OFC
	BGT     v3_RaiseException	@ Will overflow

	VCMP.F64 d0, d2
	VMRS    APSR_nzcv, FPSCR
	VSUBLT.F64 d0, d0, d0
	MOVLT   a1, #FPSCR_UFC
	BLT     v3_RaiseException	@ Will underflow

	ldr	ip, =(1023 + (0)) << 20	@ LoadExp64 ip, 0
	TEQ     a2, ip
	TEQEQ   a1, #0
	VLDREQ  d0, v3_Power64_de1
	BXEQ    lr

	push	{v1, lr}
	MOV     v1, #0	@ k = 0
	VMOV    d2, v1, v1	@ hi = 0.0
	VMOV.F64 d1, d2	@ lo = 0.0
	LDR     ip, =0x3FD62E42	@ Approx 1/2 ln(2)
	CMP     a3, ip
	BLS     30f
	VLDR    d5, v3_Power64_dln2_20
	VLDR    d6, v3_Power64_dln2_20r
	LDR     ip, =0x3FF0A2B2	@ Approx 3/2 ln(2)
	CMP     a3, ip
	BCS     20f
@ 1/2 ln(2) < |y| < 3/2 ln(2)
	TST     a2, #1<<31
	VNEGNE.F64 d5, d5
	VNEGNE.F64 d6, d6
	VSUB.F64 d2, d0, d5	@ hi
	VMOV.F64 d1, d6	@ lo
	ADDEQ   v1, v1, #1
	SUBNE   v1, v1, #1
	B       40f
20:
@ |y| >= 3/2 ln(2)
	TST     a2, #1<<31
	VLDR    d3, v3_Power64_dhalf
	VLDR    d4, v3_Power64_d1uponln2
	VNEGNE.F64 d3, d3
	VMLA.F64 d3, d4, d0
	VCVT.S32.F64 s6, d3	@ k = integer round towards zero
	VMOV    v1, s6
	VCVT.F64.S32 d3, s6
	VMUL.F64 d5, d3, d5
	VSUB.F64 d2, d0, d5	@ hi
	VMUL.F64 d1, d3, d6	@ lo
	B       40f
30:
@ |y| <= 1/2 ln(2)
	ldr	ip, =(1023 + (-28)) << 20	@ LoadExp64 ip, -28
	CMP     a3, ip
	BCS     50f
	ORRS    a3, a1, a2, LSL #1	@ e^0 is exact
	MOVNE   a1, #FPSCR_IXC
	BLNE    v3_RaiseException
	VLDR    d1, v3_Power64_done
	VADD.F64 d0, d1, d0
	pop	{v1, pc}	@ Tiny, so approximate exp(y) = 1 + y
40:
	VSUB.F64 d0, d2, d1	@ y = hi - lo
50:
@ 0 <= |y| <= 1/2 ln(2)
	VPUSH   { d8 }
	VMUL.F64 d3, d0, d0	@ y^2
	ADR     a3, v3_Power64_dCexp
	VLDMIA  a3, { d4-d8 }
	vpush	{d0-d8}	@ VFused64 d7, d3, d8
	vldr	d0, [sp, #24]
	vldr	d1, [sp, #64]
	vldr	d2, [sp, #56]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #56]
	vpop	{d0-d8}
	vpush	{d0-d7}	@ VFused64 d6, d3, d7
	vldr	d0, [sp, #24]
	vldr	d1, [sp, #56]
	vldr	d2, [sp, #48]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #48]
	vpop	{d0-d7}
	VMLA.F64 d5, d3, d6
	VMLA.F64 d4, d3, d5
	VPOP    { d8 }
	VLDR    d5, v3_Power64_done
	VADD.F64 d6, d5, d5	@ 2
	VMUL.F64 d4, d4, d3
	VSUB.F64 d3, d0, d4	@ d3 := poly
	CMP     v1, #0
	BNE     60f

	VSUB.F64 d4, d3, d6	@                          (poly - 2)
	VMUL.F64 d7, d0, d3	@               (y * poly)
	VDIV.F64 d7, d7, d4
	VSUB.F64 d7, d7, d0
	VSUB.F64 d0, d5, d7	@ exp(y) = 1 - ((y * poly)/(poly - 2) - x)
	pop	{v1, pc}
60:
	VSUB.F64 d4, d6, d3	@                                (2 - poly)
	VMUL.F64 d7, d0, d3	@                     (y * poly)
	VDIV.F64 d7, d7, d4
	VSUB.F64 d1, d1, d7	@                lo - (y * poly)/(2 - poly)
	VSUB.F64 d1, d1, d2	@              ((lo - (y * poly)/(2 - poly)) - hi)
	VSUB.F64 d1, d5, d1	@ exp(r) = 1 - ((lo - (y * poly)/(2 - poly)) - hi)

@ k non zero, need 2^k scaler
	CMP     v1, #1024	@ Careful making an exponent look like a NaN
	VLDREQ  d4, v3_Power64_d2pow1023
	VMULEQ.F64 d1, d1, d6
	VMULEQ.F64 d0, d1, d4
	popeq	{v1, pc}

	LDR     ip, =-1021
	ldr	a4, =(1023 + (0)) << 20	@ LoadExp64 a4, 0
	CMP     v1, ip
	ADD     a3, a4, v1, LSL #20	@ Exponent of 2^k
	ADDLT   a3, a3, #1000<<20	@             2^(k+1000)
	MOV     a2, #0
	VMOV    d2, a2, a3
	VMUL.F64 d0, d1, d2
	ldr	a3, =(1023 + (-1000)) << 20	@ LoadExp64 a3, -1000
	VMOVLT  d2, a2, a3
	VMULLT.F64 d0, d0, d2
	pop	{v1, pc}

@ double fp64pow(double x, double y)
@ Take logarithms pow(x, y) = x^y
@                 log2(x^y) = y * log2(x)
@                    => x^y = 2^(y * log2(x))
@ Only if y is an integer can x be negative.
@ Pay extra attention to inaccuracies in log2(x) because they grow quickly.

@ Exceptions per ISO9899:2018 F10.4.4
@       x = ±0, y < 0 and an odd integer, is a divide by zero
@       x = ±0, finite y < 0 and not an odd integer, is a divide by zero
@       x = ±0, y = -INF may raise a divide by zero
@       finite x < 0, finite y which is not an integer, is an invalid operation
@ Based on a translation to AArch32 of
@       openlibm/src/e_pow.c
@       Copyright (C) 2004 by Sun Microsystems, Inc. All rights reserved.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v3_fp64pow:
	VMOV    a1, a2, d0
	VMOV    a3, a4, d1
	ORRS    ip, a3, a4, LSL #1
	VLDREQ  d0, v3_Power64_done	@ anything^0 = 1
	BXEQ    lr
	ldr	ip, =(1023 + (0)) << 20	@ LoadExp64 ip, 0
	TEQ     a2, ip
	TEQEQ   a1, #0
	BXEQ    lr	@ 1^anything = 1
	TEQ     a4, ip
	TEQEQ   a3, #0
	BXEQ    lr	@ anything^1 = itself


	push	{v1-v5, lr}
	BIC     v2, a2, #1<<31
	BIC     v3, a4, #1<<31
	LDR     ip, =0x7FF<<20
	CMP     v2, ip
	CMPEQ   a1, #0
	BHI     10f
	CMP     v3, ip
	CMPEQ   a3, #0
10:
	VADDHI.F64 d0, d0, d1	@ Propagate NaNs
	pophi	{v1-v5, pc}

@ x is not 1, y is not 0 or 1, neither are NaN
	MOV     v1, #0
	TST     a2, #1<<31
	BEQ     25f
	ldr	ip, =(1023 + (DBL_MANT_DIG)) << 20	@ LoadExp64 ip, DBL_MANT_DIG
	CMP     v3, ip
	MOVCS   v1, #2	@ |y| >= bits in the mantissa
	BCS     25f
	ldr	ip, =(1023 + (0)) << 20	@ LoadExp64 ip, 0
	CMP     v3, ip
	BCC     25f	@ |y| < 1
	MOV     lr, v3, LSR #20
	SUB     lr, lr, ip, LSR #20	@ Unbiased exponent
	ADD     lr, lr, #1+11	@ Sign and exponent bits
	RSBS    ip, lr, #32
	BLS     15f
@ Shift < 32
	MOVS    v5, a4, LSL lr	@ C = lost bit
	ORR     v5, v5, a3, LSR ip
	MOV     v4, a3, LSL lr
	B       20f
15:
@ Shift >= 32
	SUB     ip, lr, #32
	MOVS    v5, a3, LSL ip	@ C = lost bit
	MOV     v4, #0
20:
	ORRS    v4, v4, v5	@ Z = 0 if there are fractional bits
	BNE     25f
	MOVCC   v1, #2
	MOVCS   v1, #1
25:
@ yisint = 0 => y is not an integer
@ yisint = 1 => y is an odd int
@ yisint = 2 => y is an even int
	LDR     ip, =0x7FF<<20

@ Pick off interesting values of y
	TEQ     a3, #0
	BNE     35f
	ldr	v4, =(1023 + (0)) << 20	@ LoadExp64 v4, 0
	TEQ     v3, ip
	BNE     30f
@ y = ±INF
	CMP     v2, v4
	CMPEQ   a1, #0
	ORREQ   ip, ip, #1<<19
	VMOVEQ  d0, a3, ip	@ ±1^±INF => QNaN
	popeq	{v1-v5, pc}
	EORCS   a4, a4, #1<<31
	TST     a4, #1<<31
	VMOVEQ  d0, a3, a3	@ (|x| > 1)^-INF = 0
@ (|x| < 1)^+INF = 0
	VMOVNE  d0, a3, ip	@ (|x| > 1)^+INF = +INF
@ (|x| < 1)^-INF = +INF
	pop	{v1-v5, pc}
30:
	ldr	v4, =(1023 + (1)) << 20	@ LoadExp64 v4, 1
	CMP     a4, v4
	VMULEQ.F64 d0, d0, d0	@ Shortcut x^2
	popeq	{v1-v5, pc}
	ldr	v4, =(1023 + (-1)) << 20	@ LoadExp64 v4, -1
	CMP     a4, v4
	TSTEQ   a2, #1<<31
	VSQRTEQ.F64 d0, d0	@ Shortcut x^(1/2) for x positive
	popeq	{v1-v5, pc}
35:
@ Pick off interesting values of x
	TEQ     a1, #0
	BNE     50f
	TEQ     v2, ip
	BNE     40f
@ x = ±INF
	ORRS    v4, a2, a4
	VMOVPL  d0, a1, ip	@ +INF^+anything = +INF
	VMOVMI  d0, a1, a1	@ +INF^-anything = +0
	TST     a2, #1<<31
	VNEGNE.F64 d0, d0	@ -INF^anything = -0
	pop	{v1-v5, pc}
40:
	ORRS    v4, a1, a2, LSL #1
	BNE     50f
@ x = ±0
	BCS     45f
@ x = +0
	TST     a4, #1<<31
	VMOVEQ  d0, a1, a1	@ +0^+anything = +0
	VMOVNE  d0, a1, ip	@ +0^-anything = +INF
	MOVNE   a1, #FPSCR_DZC
	BLNE    v3_RaiseException
	pop	{v1-v5, pc}
45:
@ x = -0
	TST     a4, #1<<31
	VABSEQ.F64 d0, d0	@ -0^+anything = +0   } anything other than
	VMOVNE  d0, a1, ip	@ -0^-anything = +INF } an odd integer
	MOVNE   a1, #FPSCR_DZC
	BLNE    v3_RaiseException
	CMP     v1, #1
	VNEGEQ.F64 d0, d0	@ -0^oddint = -0 or -INF
	pop	{v1-v5, pc}
50:
@ x and y are not 0, 1, ±INF or NaN
@ A hard value of x^y
	VABS.F64 d0, d0
	ADDS    lr, v1, a2, ASR #31
	BPL     55f
	ORR     ip, ip, #1<<19
	VMOV    d0, v1, ip	@ -ve^nonint = QNaN
	MOV     a1, #FPSCR_IOC
	pop	{v1-v5, lr}
	B       v3_RaiseException
55:
	ldr	v4, =(1023 + (0)) << 20	@ LoadExp64 v4, 0
	ldr	ip, =(1023 + (64)) << 20	@ LoadExp64 ip, 64
	CMP     v3, ip
	BHI     60f	@ |y| > 2^64 will over/underflow
	ldr	ip, =(1023 + (31)) << 20	@ LoadExp64 ip, 31
	CMP     v3, ip
	BLS     70f	@ Safe
	CMP     v2, v4
	BEQ     65f	@ Safe
60:
	CMP     v2, v4
	EORCC   a4, a4, #1<<31
	TST     a4, #1<<31
	VLDREQ  d0, v3_Power64_dhuge	@ (|x| > 1)^+large = huge
@ (|x| < 1)^-large = huge
	VLDRNE  d0, v3_Power64_dtiny	@ (|x| > 1)^-large = tiny
@ (|x| < 1)^+large = tiny
	MOVEQ   a1, #FPSCR_OFC
	MOVNE   a1, #FPSCR_UFC
	BL      v3_RaiseException
	VMUL.F64 d0, d0, d0
	B       999f

v3_Power64_dCtaylor:	.word	0x00000000, 0x3FE00000	@ 1/2
	.word	0x55555555, 0x3FD55555	@ 1/3
	.word	0x00000000, 0x3FD00000	@ 1/4
v3_Power64_d1uponln2s:	.word	0x60000000, 0x3FF71547	@ 1.44269502162933349609e+00
v3_Power64_d1uponln2sr:	.word	0xF85DDF44, 0x3E54AE0B	@ Next 17sf of 1/ln(2)
v3_Power64_d2pow53:	.word	0x00000000, 0x43400000	@ (double)2^53
v3_Power64_dhuge:	.word	0x8800759C, 0x7E37E43C	@ 10^300
v3_Power64_dtiny:	.word	0xC2F8F359, 0x01A56E1F	@ 10^-300
v3_Power64_d3over2:	.word	0x00000000, 0x3FF80000	@ 1.5
v3_Power64_dClogx:	.word	0x33333303, 0x3FE33333	@ 5.99999999999994648725e-01
	.word	0xDB6FABFF, 0x3FDB6DB6	@ 4.28571428578550184252e-01
	.word	0x518F264D, 0x3FD55555	@ 3.33333329818377432918e-01
	.word	0xA91D4101, 0x3FD17460	@ 2.72728123808534006489e-01
	.word	0x93C9DB65, 0x3FCD864A	@ 2.30660745775561754067e-01
	.word	0x4A454EEF, 0x3FCA7E28	@ 2.06975017800338417784e-01

65:
@ |y| > 2^32 but |x| is very close to one (bottom 32 bits of mantissa only)
@ As |1 - x| <= 2^-20 can approximate log(x) by a Taylor series
@ expansion t - 1/2 t^2 + 1/3 t^3 - 1/4 t^4
@           t - (t^2 * (1/2 - t * (1/3 - t * 1/4)))
	ADR     ip, v3_Power64_dCtaylor
	VLDMIA  ip, { d5-d7 }
	VLDR    d4, v3_Power64_dunity
	VSUB.F64 d4, d0, d4	@ t
	VMLS.F64 d6, d4, d7
	VMLS.F64 d5, d4, d6
	VMUL.F64 d5, d4, d5
	VMUL.F64 d5, d4, d5
	ADRL    ip, v3_Power64_d1uponln2
	VLDR    d3, [ip]
	VLDR    d6, v3_Power64_d1uponln2s
	VLDR    d7, v3_Power64_d1uponln2sr
	MOV     ip, #0
@ Change of base to 2
	VMUL.F64 d6, d6, d4
	VMUL.F64 d4, d4, d7
	VMLS.F64 d4, d5, d3
	VADD.F64 d2, d6, d4
	VMOV    s4, ip	@ Low word of d2
	VSUB.F64 d3, d2, d6
	VSUB.F64 d3, d4, d3
@ Result is doubledouble (t1+t2) in registers d2 and d3
	B       80f
70:
@ |y| <= 2^31
	MOV     v4, #0
	CMP     v2, #1<<20	@ Subnormal?
	VLDRCC  d7, v3_Power64_d2pow53
	SUBCC   v4, v4, #53
	VMULCC.F64 d0, d0, d7
	VMOVCC  v2, s1	@ High word of scaled x
	LDR     ip, =DBL_EXP_BIAS
	mov	v5, v2, lsl #1	@ ExpBits64 v5, v2
	mov	v5, v5, lsr #21
	SUB     v5, v5, ip
	ADD     v4, v4, v5
	BIC     v5, v2, #0xF0000000
	BIC     v5, v5,    #0x0FF00000
	ORR     v2, v5, ip, LSL #20	@ Normalised x

@ Determine interval
	MOV     a3, #0
	LDR     ip, =0x3988E	@ SQR(3/2)
	CMP     v5, ip
	BLS     75f
	LDR     ip, =0xBB67A	@ SQR(3)
	CMP     v5, ip
	MOVCC   a3, #1
	ADDCS   v4, v4, #1
	SUBCS   v2, v2, #1<<20
75:
	VMOV    s1, v2	@ High word of scaled x

@ Compute ss = s_h + s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5)
	TEQ     a3, #0
	VLDR    d6, v3_Power64_dunity
	VMOVEQ.F64 d7, d6
	VLDRNE  d7, v3_Power64_d3over2
	VSUB.F64 d4, d0, d7
	VADD.F64 d5, d0, d7
	VDIV.F64 d5, d6, d5
	VMUL.F64 d2, d4, d5	@ ss

	MOV     ip, #0
	VMOV.F64 d3, d2	@ s_h
	VMOV    s6, ip	@ Low word of d3
	MOV     lr, v2, LSR #1
	ORR     lr, lr, #0x20000000
	ADD     lr, lr, #1<<19
	ADD     lr, lr, a3, LSL #18
	VMOV    d6, ip, lr
	VSUB.F64 d7, d6, d7
	VSUB.F64 d7, d0, d7
	VMLS.F64 d4, d3, d6
	VMLS.F64 d4, d3, d7
	VMUL.F64 d4, d5, d4	@ s_l

@ Compute log(ax)
	VPUSH   { d8-d11 }
	ADR     ip, v3_Power64_dClogx
	VLDMIA  ip, { d6-d11 }
	VMUL.F64 d5, d2, d2	@ ss^2
	vpush	{d0-d11}	@ VFused64 d10, d5, d11
	vldr	d0, [sp, #40]
	vldr	d1, [sp, #88]
	vldr	d2, [sp, #80]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #80]
	vpop	{d0-d11}
	vpush	{d0-d10}	@ VFused64 d9, d5, d10
	vldr	d0, [sp, #40]
	vldr	d1, [sp, #80]
	vldr	d2, [sp, #72]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #72]
	vpop	{d0-d10}
	vpush	{d0-d9}	@ VFused64 d8, d5, d9
	vldr	d0, [sp, #40]
	vldr	d1, [sp, #72]
	vldr	d2, [sp, #64]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #64]
	vpop	{d0-d9}
	VMLA.F64 d7, d5, d8
	VMLA.F64 d6, d5, d7
	VMUL.F64 d6, d6, d5
	VMUL.F64 d6, d6, d5	@ d6 := poly

	VADD.F64 d7, d3, d2
	VMLA.F64 d6, d4, d7
	VMUL.F64 d5, d3, d3	@ s_h^2
	VLDR    d7, v3_Power64_d3over2
	VADD.F64 d7, d7, d7	@ 3
	VADD.F64 d8, d7, d5
	VADD.F64 d8, d8, d6
	MOV     ip, #0
	VMOV    s16, ip	@ Low word of d8
	VSUB.F64 d9, d8, d7
	VSUB.F64 d9, d9, d5
	VSUB.F64 d9, d6, d9
	VMUL.F64 d10, d3, d8
	VMUL.F64 d11, d4, d8
	VMLA.F64 d11, d9, d2

	VADD.F64 d4, d10, d11
	VMOV    s8, ip	@ Low word of d4
	VSUB.F64 d5, d4, d10
	VSUB.F64 d5, d11, d5
	VLDR    d6, v3_Power64_d2o3ln2
	VLDR    d7, v3_Power64_d2o3ln2_28
	VLDR    d8, v3_Power64_d2o3ln2_28r
	TEQ     a3, #0
	VLDRNE  d9, v3_Power64_dCdp
	VLDRNE  d10, v3_Power64_dCdpr
	VMOVEQ  d9, a3, a3
	VMOVEQ  d10, a3, a3
	VMUL.F64 d7, d7, d4

	VMUL.F64 d8, d8, d4
	VMLA.F64 d8, d5, d6
	VADD.F64 d8, d8, d10

	VMOV    s12, v4
	VCVT.F64.S32 d6, s12
	VADD.F64 d2, d7, d8
	VADD.F64 d2, d2, d9
	VADD.F64 d2, d2, d6
	VMOV    s4, ip	@ Low word of d2
	VSUB.F64 d4, d2, d6
	VSUB.F64 d4, d4, d9
	VSUB.F64 d4, d4, d7
	VSUB.F64 d3, d8, d4
@ Result is doubledouble (t1+t2) in registers d2 and d3
	VPOP    { d8-d11 }
80:
@ Split up y into y1+y2 and compute (y1+y2)*(t1+t2)
	MOV     ip, #0
	VMOV    d0, ip, a4	@ Just high word of y
	VSUB.F64 d4, d1, d0
	VMUL.F64 d4, d4, d2
	VMLA.F64 d4, d1, d3	@ product lo
	VMUL.F64 d5, d0, d2	@ product hi
	VADD.F64 d0, d4, d5	@ z = prodhi + prodlo
	VMOV    a3, a4, d0
	BIC     v3, a4, #1<<31

	MOVS    a4, a4
	BMI     90f
	LDR     ip, =0x40900000
	CMP     a4, ip
	CMPEQ   a3, #0
	BCC     100f
	BHI     85f	@ z > 1024

	VLDR    d6, v3_Power64_dovftie
	VADD.F64 d6, d6, d4
	VSUB.F64 d7, d0, d4
	VCMP.F64 d6, d7
	VMRS    APSR_nzcv, FPSCR
	BLE     100f	@ z = 1024 and tie breaker says no overflow
85:
	MOV     a1, #FPSCR_OFC
	BL      v3_RaiseException
	VLDR    d0, v3_Power64_dhuge
	VMUL.F64 d0, d0, d0
	B       999f

v3_Power64_dunity:	.word	0x00000000, 0x3FF00000	@ (double)1 for VLDR addressing range

90:
	LDR     ip, =0x4090CC00
	CMP     v3, ip
	BCC     100f
	LDR     ip, =0xC090CC00
	CMP     a4, ip
	CMPEQ   a3, #0
	BNE     95f	@ z < -1075

	VSUB.F64 d7, d0, d5
	VCMP.F64 d4, d7
	VMRS    APSR_nzcv, FPSCR
	BGT     100f	@ z = -1075 and tie breaker says no underflow
95:
	MOV     a1, #FPSCR_UFC
	BL      v3_RaiseException
	VLDR    d0, v3_Power64_dtiny
	VMUL.F64 d0, d0, d0
	B       999f
100:
@ Compute 2^(prodhi + prodlo)
	LDR     ip, =DBL_EXP_BIAS
	ldr	lr, =(1023 + (-1)) << 20	@ LoadExp64 lr, -1
	mov	a3, a4, lsl #1	@ ExpBits64 a3, a4
	mov	a3, a3, lsr #21
	SUB     a3, a3, ip
	CMP     v3, lr
	MOVLS   v4, #0
	BLS     105f

@ |z| > 0.5
	MOV     lr, #1<<20
	ADD     v5, a3, #1
	ADD     v4, a4, lr, LSR v5	@ New scale
	BIC     a3, v4, #1<<31
	RSB     a3, ip, a3, LSR #20	@ New exponent for scale
	SUB     lr, lr, #1	@ => &FFFFF
	BIC     v5, v4, lr, LSR a3
	MOV     ip, #0
	VMOV    d2, ip, v5
	AND     v4, v4, lr
	ORR     v4, v4, #1<<20
	RSB     a3, a3, #20
	MOV     v4, v4, LSR a3
	TST     a4, #1<<31
	RSBNE   v4, v4, #0
	VSUB.F64 d5, d5, d2
105:
@ |z| <= 0.5
	VLDR    d1, v3_Power64_dln2
	VLDR    d2, v3_Power64_dln2_32
	VLDR    d3, v3_Power64_dln2_32r
	MOV     ip, #0
	VADD.F64 d0, d4, d5
	VMOV    s0, ip	@ Low word of d0
	VSUB.F64 d6, d0, d5
	VSUB.F64 d6, d4, d6
	VMUL.F64 d1, d1, d6
	VMLA.F64 d1, d0, d3
	VMUL.F64 d3, d0, d2
	VADD.F64 d2, d3, d1	@ z = u + v
	VSUB.F64 d4, d2, d3
	VSUB.F64 d1, d1, d4

@ Evaluate polynomial
	ADR     ip, v3_Power64_dCpow
	VLDMIA  ip, { d3-d7 }
	VMUL.F64 d0, d2, d2	@ z^2
	vpush	{d0-d7}	@ VFused64 d6, d0, d7
	vldr	d1, [sp, #56]
	vldr	d2, [sp, #48]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #48]
	vpop	{d0-d7}
	vpush	{d0-d7}	@ VFused64 d5, d0, d6
	vldr	d1, [sp, #48]
	vldr	d2, [sp, #40]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #40]
	vpop	{d0-d7}
	vpush	{d0-d7}	@ VFused64 d4, d0, d5
	vldr	d1, [sp, #40]
	vldr	d2, [sp, #32]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #32]
	vpop	{d0-d7}
	VMLA.F64 d3, d0, d4
	VMUL.F64 d3, d0, d3
	VSUB.F64 d3, d2, d3	@ d3 := poly

	VLDR    d7, v3_Power64_dunity
	VADD.F64 d6, d7, d7	@ 2
	VMLA.F64 d1, d2, d1
	VMUL.F64 d4, d2, d3
	VSUB.F64 d5, d3, d6
	VDIV.F64 d4, d4, d5
	VSUB.F64 d4, d4, d1	@ r
	VSUB.F64 d2, d4, d2
	VSUB.F64 d2, d7, d2	@ d2 := 1 - (r - z)

@ Final exponent adjustment
	VMOV    a3, a4, d2
	ADD     a4, a4, v4, LSL #20
	MOVS    lr, a4, ASR #20
	MOV     v5, a2
	VMOVGT  d0, a3, a4	@ Normal
	MOVLE   a1, v4
	VMOVLE.F64 d0, d2
	BLLE    v3_LDExp	@ Scale subnormal
	MOV     a2, v5
999:
@ Fixup sign
	TST     a2, #1<<31
	popeq	{v1-v5, pc}	@ +ve^anything = +ve
	CMP     v1, #1
	VNEGEQ.F64 d0, d0	@ -ve^oddint  = -ve
	pop	{v1-v5, pc}

v3_Power64_d2o3ln2:	.word	0xDC3A03FD, 0x3FEEC709	@ 2/(3 * ln(2))
v3_Power64_d2o3ln2_28:	.word	0xE0000000, 0x3FEEC709	@ Truncated d2o3ln2
v3_Power64_d2o3ln2_28r:	.word	0x145B01F5, 0xBE3E2FE0	@ Next 17sf of truncated d2o3ln2
v3_Power64_dovftie:	.word	0x652B82FE, 0x3C971547	@ -(1024-log2(ovfl+.5ulp))
v3_Power64_dCdp:	.word	0x40000000, 0x3FE2B803	@ 5.84962487220764160156e-01
v3_Power64_dCdpr:	.word	0x43CFD006, 0x3E4CFDEB	@ Next 17sf of dCdp
v3_Power64_dln2:	.word	0xFEFA39EF, 0x3FE62E42	@  6.93147180559945286227e-01
v3_Power64_dln2_32:	.word	0x00000000, 0x3FE62E43	@  6.93147182464599609375e-01
v3_Power64_dln2_32r:	.word	0x0CA86C39, 0xBE205C61	@ -1.90465429995776804525e-09
v3_Power64_dCpow:	.word	0x5555553E, 0x3FC55555	@  1.66666666666666019037e-01
	.word	0x16BEBD93, 0xBF66C16C	@ -2.77777777770155933842e-03
	.word	0xAF25DE2C, 0x3F11566A	@  6.61375632143793436117e-05
	.word	0xC5D26BF1, 0xBEBBBD41	@ -1.65339022054652515390e-06
	.word	0x72BEA4D0, 0x3E663769	@  4.13813679705723846039e-08


	.ltorg				@ objasm's literal pool at the end of the AREA
	.if !((( v3_Power64_dClogE) - (v3_Power64_dClogO + (4*8) )) == 0)
	.error "ASSERT failed: v3_Power64_dClogO + (4*8) = v3_Power64_dClogE"
	.endif
	.if !((( v3_Power64_dClogE) - (v3_Power64_dClogO + (4*8) )) == 0)
	.error "ASSERT failed: v3_Power64_dClogO + (4*8) = v3_Power64_dClogE"
	.endif
