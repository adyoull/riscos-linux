@ Trig64: from RISC OS Open's VFPSupport s/Trig64, translated to GNU as
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


@ EXPORT fp64cos
@ EXPORT fp64sin
@ EXPORT fp64tan

	.text
	.arm


@ double fp64cos(double x)
@ Use the identity cos(x) = sin(x + pi/2)

@ Exceptions per ISO9899:2018 F10.1.5
@       x = ±INF is an invalid operation
v4_fp64cos:
@ Deal with special cases
	VMOV    a1, a2, d0
	ORRS    a3, a1, a2, LSL #1
	VLDREQ  d0, v4_Trig64_done
	BXEQ    lr	@ cos(±0) = 1
	push	{v1-v3, lr}
	LDR     ip, =0x7FF
	mov	a3, a2, lsl #1	@ ExpBits64 a3, a2
	mov	a3, a3, lsr #21
	CMP     a3, ip
	BNE     10f
	ORRS    a3, a1, a2, LSL #1+11
	ORREQ   v2, a2, #1<<19	@ ±INF => QNaN
	MOVNE   v2, a2	@ NaNs => propagated
	MOV     v1, a1
	MOV     a1, #FPSCR_IOC
	BL      v4_RaiseException
	VMOV    d0, v1, v2
	pop	{v1-v3, pc}
10:
	CMP     a2, #1<<(52-32)	@ Smallest
	CMPEQ   a1, #0	@ Subnormal and too small to scale
	BCS     20f
	VLDR    d0, v4_Trig64_done
	MOV     a1, #FPSCR_UFC
	BL      v4_RaiseException
	pop	{v1-v3, pc}
20:
@ Argument reduction
	MOV     v1, a2, LSR #31	@ sign bit of x
	BIC     a2, a2, #1<<31
	LDRD    a3, a4, v4_Trig64_dpiby4
	CMP     a2, a4
	CMPEQ   a1, a3
	BHI     40f

	ldr	ip, =(1023 + (-13)) << 20	@ LoadExp64 ip, -13
	CMP     a2, ip
	CMPEQ   a1, #0
	BCC     30f
@ 2^-13 >= x <= piby4
	ADR     a3, v4_Trig64_dCcos
	VLDMIA  a3, { d2-d7 }
@ Calculate the polynomial 1 - 0.5*x^2 + C1*x^4 + C2*x^6 + C3*x^8 + C4*x^10 + C5*x^12 + C6*x^14
@                        = 1 + (x^2 * (-0.5 + C1*x^2 + C2*x^4 + C3*x^6 + C4*x^8 + C5*x^10 + C6*x^12))
@                        = 1 + (x^2 * (-0.5 + x^2 * (C1 + x^2 * (C2 + x^2 * (C3 + x^2 * (C4 + x^2 * (C5 + x^2 * C6))))))))
	VMUL.F64 d1, d0, d0	@ x^2
	vfma.f64	d6, d1, d7	@ VFused64 d6, d1, d7
	vfma.f64	d5, d1, d6	@ VFused64 d5, d1, d6
	vfma.f64	d4, d1, d5	@ VFused64 d4, d1, d5
	VLDR    d5, v4_Trig64_dhalf
	VLDR    d6, v4_Trig64_done
	VMLA.F64 d3, d1, d4
	VMLA.F64 d2, d1, d3
	VNMLS.F64 d5, d1, d2
	VMUL.F64 d1, d1, d5	@ d1 :=      x^2 * (powers 2+)
	VADD.F64 d0, d6, d1	@ d0 := 1 + (x^2 * (powers 2+))
	pop	{v1-v3, pc}
30:
	ldr	ip, =(1023 + (-27)) << 20	@ LoadExp64 ip, -27
	CMP     a2, ip
	CMPEQ   a1, #0
	VLDRLS  d0, v4_Trig64_done
	popls	{v1-v3, pc}	@ Approx cos(x) = 1
@ 2^-27 < x < 2^-13
	VLDR    d6, v4_Trig64_dhalf
	VMUL.F64 d2, d0, d0
	VLDR    d0, v4_Trig64_done
	VMLS.F64 d0, d2, d6	@ Approx cos(x) = 1 - (x^2 / 2)
	pop	{v1-v3, pc}
40:
@ x > piby4
	VABS.F64 d0, d0
	BL      v4_Trig64_CosRangeReduce
	ADD     a1, a1, #1	@ region + pi/2
	AND     v2, a1, #3
	MOV     v1, #0	@ sign + pi/2
	B       50f

v4_Trig64_dpiby2:	.word	0x54442D18, 0x3FF921FB	@ pi/2
v4_Trig64_dpiby4:	.word	0x54442D18, 0x3FE921FB	@ pi/4
v4_Trig64_dpiby4r:	.word	0x33145C06, 0x3C81A626	@ Next 17sf of pi/4
v4_Trig64_done:	.word	0x00000000, 0x3FF00000	@ (double)1

@ double fp64sin(double x)
@ Reduce argument x to   |x| = (N * pi) + f
@ where N is an integer    N = round(x / pi)
@                          f = |x| - (N * pi)
@ then because sine is periodic sin(x) = sin((N * pi) + f)
@ then use the identity         sin((N * pi) + f) = (sin(N * pi) * cos(f)) + (cos(N * pi) * sin(f))
@                                                 = (     0      * cos(f)) + (    ±1      * sin(f))
@ where f is in the range [-pi/2 +pi/2] approximated by a polynomial.
@ Tiny values can skip the polynomial evaluation and use a more crude cubic fit.

@ Exceptions per ISO9899:2018 F10.1.6
@       x = ±INF is an invalid operation
@ Based on a translation to AArch32 of
@       aocl-libm-ose/src/optmized/sin.c
@       Copyright (C) 2008-2020 Advanced Micro Devices, Inc. All rights reserved.
@       SPDX-License-Identifier: BSD-3-Clause
v4_fp64sin:
@ Deal with special cases
	VMOV    a1, a2, d0
	ORRS    a3, a1, a2, LSL #1
	BXEQ    lr	@ sin(±0) = ±0
	push	{v1-v3, lr}
	LDR     ip, =0x7FF
	mov	v3, a2, lsl #1	@ ExpBits64 v3, a2
	mov	v3, v3, lsr #21
	CMP     v3, ip
	BNE     10f
	ORRS    a3, a1, a2, LSL #1+11
	ORREQ   v2, a2, #1<<19	@ ±INF => QNaN
	MOVNE   v2, a2	@ NaNs => propagated
	MOV     v1, a1
	MOV     a1, #FPSCR_IOC
	BL      v4_RaiseException
	VMOV    d0, v1, v2
	pop	{v1-v3, pc}
10:
	CMP     a2, #1<<(52-32)	@ Smallest
	CMPEQ   a1, #0	@ Subnormal and too small to scale
	BCS     20f
	MOV     a1, #FPSCR_UFC
	BL      v4_RaiseException
	pop	{v1-v3, pc}
20:
@ Argument reduction
	MOV     v1, a2, LSR #31	@ sign bit of x
	BIC     a2, a2, #1<<31
	LDRD    a3, a4, v4_Trig64_dpiby4
	CMP     a2, a4
	CMPEQ   a1, a3
	BHI     40f

	ldr	ip, =(1023 + (-13)) << 20	@ LoadExp64 ip, -13
	CMP     a2, ip
	CMPEQ   a1, #0
	BCC     30f
@ 2^-13 >= x <= piby4
	ADR     a3, v4_Trig64_dCsin
	VLDMIA  a3, { d2-d7 }
@ Calculate the polynomial x + C1*x^3 + C2*x^5 + C3*x^7 + C4*x^9 + C5*x^11 + C6*x^13
@                        = x + (x^3 * (C1 + C2*x^2 + C3*x^4 + C4*x^6 + C5*x^8 + C6*x^10))
@                        = x + (x * (x^2 * (C1 + x^2 * (C2 + x^2 * (C3 + x^2 * (C4 + x^2 * (C5 + x^2 * C6))))))))
	VMUL.F64 d1, d0, d0	@ x^2
	vfma.f64	d6, d1, d7	@ VFused64 d6, d1, d7
	vfma.f64	d5, d1, d6	@ VFused64 d5, d1, d6
	vfma.f64	d4, d1, d5	@ VFused64 d4, d1, d5
	VMLA.F64 d3, d1, d4
	VMLA.F64 d2, d1, d3
	VMUL.F64 d1, d1, d2	@ d1 :=          x^2 * (powers 3+)
	VMLA.F64 d0, d0, d1	@ d0 := x + x * (x^2 * (powers 3+))
	pop	{v1-v3, pc}
30:
	ldr	ip, =(1023 + (-27)) << 20	@ LoadExp64 ip, -27
	CMP     a2, ip
	CMPEQ   a1, #0
	popls	{v1-v3, pc}	@ Approx sin(x) = x
@ 2^-27 < x < 2^-13
	VLDR    d6, v4_Trig64_dminussixth
	VMUL.F64 d2, d0, d0
	VMUL.F64 d1, d2, d6
	VMLA.F64 d0, d1, d0	@ Approx sin(x) = x - (x^3 / 6)
	pop	{v1-v3, pc}

v4_Trig64_dhalf:	.word	0x00000000, 0x3FE00000	@ 1/2
v4_Trig64_dminussixth:	.word	0x55555555, 0xBFC55555	@ -1/6
@  Remez's coefficients from Sollya
v4_Trig64_dCsin:	.word	0x55555555, 0xBFC55555	@ -0x1.5555555555555p-3
	.word	0x11110BB3, 0x3F811111	@  0x1.1111111110bb3p-7
	.word	0x19E83E5C, 0xBF2A01A0	@ -0x1.a01a019e83e5cp-13
	.word	0x796CDE01, 0x3EC71DE3	@  0x1.71de3796cde01p-19
	.word	0xB42FDFA7, 0xBE5AE600	@ -0x1.ae600b42fdfa7p-26
	.word	0xF9A43BB8, 0x3DE5E0B2	@  0x1.5e0b2f9a43bb8p-33
v4_Trig64_dCcos:	.word	0x55555555, 0x3FA55555	@  0x1.5555555555555p-5
	.word	0x16C16967, 0xBF56C16C	@ -0x1.6c16c16c16967p-10
	.word	0x19F4EC91, 0x3EFA01A0	@  0x1.A01A019F4EC91p-16
	.word	0xA17F667B, 0xBE927E4F	@ -0x1.27E4FA17F667Bp-22
	.word	0x90382EEC, 0x3E21EEB6	@  0x1.1EEB690382EECp-29
	.word	0x47258AA7, 0xBDA907DB	@ -0x1.907DB47258AA7p-37

40:
@ x > piby4
	VABS.F64 d0, d0
	BL      v4_Trig64_SinRangeReduce
	MOV     v2, a1	@ region
50:
@ Now compute the polynomial on doubledouble (r,rr)
	VPUSH	{d8}
	VMUL.F64 d2, d0, d0	@ r^2
	TST     v2, #1
	BEQ     60f

@ Odd (cosine) quadrant
	ADR     a3, v4_Trig64_dCcos
	VLDMIA  a3, { d3-d8 }

	VMUL.F64 d1, d1, d0	@ rr*r
	VMUL.F64 d0, d2, d2	@ r^4
	vfma.f64	d7, d2, d8	@ VFused64 d7, d2, d8
	vfma.f64	d6, d2, d7	@ VFused64 d6, d2, d7
	vfma.f64	d5, d2, d6	@ VFused64 d5, d2, d6
	VMLA.F64 d4, d2, d5
	VMLA.F64 d3, d2, d4
	VPOP	{d8}
	VLDR    d7, v4_Trig64_dhalf
	VLDR    d6, v4_Trig64_done
	VMUL.F64 d3, d0, d3	@ poly
	VMUL.F64 d2, d7, d2	@ s = 1/2 * r^2
	VSUB.F64 d4, d2, d6	@ t = s - 1.0
	VADD.F64 d5, d6, d4
	VSUB.F64 d5, d5, d2
	VSUB.F64 d5, d5, d1
	VADD.F64 d5, d5, d3
	VSUB.F64 d0, d5, d4	@ result = ((((1.0 + t) - s) - rr*r) + poly) - t
	B       90f
60:
@ Even (sine) quadrant
	ADR     a3, v4_Trig64_dCsin
	VLDMIA  a3, { d3-d8 }

	vfma.f64	d7, d2, d8	@ VFused64 d7, d2, d8
	vfma.f64	d6, d2, d7	@ VFused64 d6, d2, d7
	vfma.f64	d5, d2, d6	@ VFused64 d5, d2, d6
	VMLA.F64 d4, d2, d5	@ poly
	VNEG.F64 d4, d4	@ -poly
	VMUL.F64 d6, d2, d0	@ r^3
	VPOP	{d8}
	VLDR    d7, v4_Trig64_dhalf
	VMUL.F64 d7, d7, d1	@ s = 1/2 * rr
	VMLA.F64 d7, d6, d4	@                       s - r^3 * poly
	VMUL.F64 d5, d2, d7	@                r^2 * (s - r^3 * poly)
	VSUB.F64 d5, d5, d1	@               (r^2 * (s - r^3 * poly)) - rr
	VMUL.F64 d3, d3, d6	@                                                 C1 * r^3
	VSUB.F64 d3, d5, d3	@              ((r^2 * (s - r^3 * poly)) - rr) - (C1 * r^3)
	VSUB.F64 d0, d0, d3	@ result = r - ((r^2 * (s - r^3 * poly)) - rr) - (C1 * r^3)
90:
@ Fixup the signs
	AND     a3, v1, v2, LSR #1
	MVN     v2, v2, LSR #1
	BIC     a4, v2, v1
	ORR     a4, a3, a4
	TST     a4, #1	@ Positive
	VNEGEQ.F64 d0, d0
	pop	{v1-v3, pc}

@ double fp64tan(double x)
@ Reduce argument x to   |x| = (N * pi) + f
@ where N is an integer    N = round(x / pi)
@                          f = |x| - (N * pi)
@ then because tangent is periodic tan(x) = tan((N * pi/2) + f)
@ Using the identity tan(pi/2 - x) = cot(x)
@                and       tan(-x) = -tan(x)
@               tan(f - (N *pi/2)) = -cot(f) = -1/tan(f)    for N odd
@                           tan(f) = tan(x)                 for N even
@ where f is in the range [-pi/4 +pi/4] approximated by a polynomial.
@ Tiny values can skip the polynomial evaluation and use a more crude cubic fit.

@ Exceptions per ISO9899:2018 F10.1.7
@       x = ±INF is an invalid operation
@ Based on a translation to AArch32 of
@       aocl-libm-ose/src/optmized/tan.c
@       Copyright (C) 2008-2020 Advanced Micro Devices, Inc. All rights reserved.
@       SPDX-License-Identifier: BSD-3-Clause
v4_fp64tan:
@ Deal with special cases
	VMOV    a1, a2, d0
	ORRS    a3, a1, a2, LSL #1
	BXEQ    lr	@ tan(±0) = ±0
	push	{v1-v3, lr}
	LDR     ip, =0x7FF
	mov	v3, a2, lsl #1	@ ExpBits64 v3, a2
	mov	v3, v3, lsr #21
	CMP     v3, ip
	BNE     10f
	ORRS    a3, a1, a2, LSL #1+11
	ORREQ   v2, a2, #1<<19	@ ±INF => QNaN
	MOVNE   v2, a2	@ NaNs => propagated
	MOV     v1, a1
	MOV     a1, #FPSCR_IOC
	BL      v4_RaiseException
	VMOV    d0, v1, v2
	pop	{v1-v3, pc}
10:
	CMP     a2, #1<<(52-32)	@ Smallest
	CMPEQ   a1, #0	@ Subnormal and too small to scale
	BCS     20f
	MOV     a1, #FPSCR_UFC
	BL      v4_RaiseException
	pop	{v1-v3, pc}
20:
@ Argument reduction
	MOV     v1, a2, LSR #31	@ sign bit of x
	BIC     a2, a2, #1<<31
	ADRL    lr, v4_Trig64_dpiby4
	LDRD    a3, a4, [lr]
	CMP     a2, a4
	CMPEQ   a1, a3
	BHI     40f

	ldr	ip, =(1023 + (-13)) << 20	@ LoadExp64 ip, -13
	CMP     a2, ip
	CMPEQ   a1, #0
	BCC     30f
@ 2^-13 >= x <= piby4
	MOV     a1, #0
	VMOV    d1, a1, a1
	pop	{v1-v3, lr}
	B       v4_Trig64_TanPiBy4
30:
	ldr	ip, =(1023 + (-27)) << 20	@ LoadExp64 ip, -27
	CMP     a2, ip
	CMPEQ   a1, #0
	popls	{v1-v3, pc}	@ Approx tan(x) = x
@ 2^-27 < x < 2^-13
	VLDR    d6, v4_Trig64_dthird
	VMUL.F64 d2, d0, d0
	VMUL.F64 d3, d2, d0
	VMLA.F64 d0, d3, d6	@ Approx tan(x) = x + (x^3 / 3)
	pop	{v1-v3, pc}

v4_Trig64_dthird:	.word	0x55555555, 0x3FD55555	@ 1/3
v4_Trig64_d5e5:	.word	0x00000000, 0x411E8480	@ 5e5

40:
@ x > piby4
	VABS.F64 d0, d0
	LDRD    a3, a4, v4_Trig64_d5e5
	CMP     a2, a4
	CMPEQ   a1, a3
	BCS     60f

@ Fast reduce the argument to be in a range [-pi/4 +pi/4]
@ by subtracting multiples of pi/2
	VLDR    d2, v4_Trig64_d2uponpi
	VLDR    d7, v4_Trig64_d18p52
	VMUL.F64 d2, d0, d2	@ |x| * 2/pi
	VADD.F64 d5, d2, d7
	VMOV    a3, a4, d5
	AND     v2, a3, #3	@ region
	VSUB.F64 d5, d5, d7
	VLDR    d6, v4_Trig64_dpi1
	VLDR    d7, v4_Trig64_dpi2
	VMUL.F64 d3, d5, d6
	VSUB.F64 d3, d0, d3	@ rhead
	VMUL.F64 d4, d5, d7	@ rtail
	VSUB.F64 d0, d3, d4	@ r
	VMOV    a3, a4, d0
	mov	ip, a4, lsl #1	@ ExpBits64 ip, a4
	mov	ip, ip, lsr #21
	SUB     ip, v3, ip	@ Exponent difference
	CMP     ip, #15
	BLS     51f

	VLDR    d6, v4_Trig64_dpi3	@ Big difference, go next double down
	VLDR    d7, v4_Trig64_dpi4
	VMUL.F64 d2, d5, d6
	VMOV.F64 d6, d3
	VSUB.F64 d3, d6, d2	@ rhead
	VSUB.F64 d4, d6, d3
	VSUB.F64 d4, d4, d2
	VMUL.F64 d6, d5, d7
	VSUB.F64 d4, d6, d4	@ rtail
	CMP     ip, #48
	BLS     50f

	VLDR    d6, v4_Trig64_dpi5	@ Really big difference, go next double down
	VLDR    d7, v4_Trig64_dpi6
	VMUL.F64 d2, d5, d6
	VMOV.F64 d6, d3
	VSUB.F64 d3, d6, d2	@ rhead
	VSUB.F64 d4, d6, d3
	VSUB.F64 d4, d4, d2
	VMUL.F64 d6, d5, d7
	VSUB.F64 d4, d6, d4	@ rtail
50:
	VSUB.F64 d0, d3, d4	@ r
51:
	VSUB.F64 d3, d3, d0
	VSUB.F64 d1, d3, d4	@ rr = (rhead - r) - rtail
	B       70f
60:
@ x > 5e6 (massive), go for slow modulus reduction
	SUB     sp, sp, #8
	MOV     a1, sp
	BL      v4_Trig64_RemainderPiBy2
	LDR     v2, [sp], #8	@ region
70:
	AND     a1, v2, #1
	BL      v4_Trig64_TanPiBy4
@ Fixup the sign
	TST     v1, #1
	VNEGNE.F64 d0, d0
	pop	{v1-v3, pc}

v4_Trig64_d0p68:	.word	0x5C28F5C3, 0x3FE5C28F	@ 0.68
@  Remez's coefficients
v4_Trig64_dCtan:	.word	0x6638564A, 0x3FD7D50F	@  0.372379159759792203640806338901e0
	.word	0xC7569ABB, 0xBF977C24	@ -0.229345080057565662883358588111e-1
	.word	0x289C385A, 0x3F2D5DAF	@  0.224044448537022097264602535574e-3
	.word	0x8CAA40B8, 0x3FF1DFCB	@  0.111713747927937668539901657944e1
	.word	0x499EB90F, 0xBFE08046	@ -0.515658515729031149329237816945e0
	.word	0xF80A0ACF, 0x3F9AB0F4	@  0.260656620398645407524064091208e-1
	.word	0xEF6D98F8, 0xBF2E7517	@ -0.232371494088563558304549252913e-3

@ double TanPiBy4(doubledouble x, bool reciprocal)
@ Evaluate Remez polynomial for tan(x), only valid on the interval [-pi/4 +pi/4].
@ or if reciprocal requested return -1/tan(x) for the doubledouble x.
@ Since tan(-x) = -tan(x) the polynomial only needs to consider +ve values then do sign correction.
v4_Trig64_TanPiBy4:
	VLDR    d2, v4_Trig64_d0p68
	VCMP.F64 d0, d2
	VMRS    APSR_nzcv, FPSCR

@ Transform x > 0.68
	MOVGT   a2, #1	@ Transform +1
	VNEGGT.F64 d0, d0
	VNEGGT.F64 d1, d1
	BGT     10f

	VNEG.F64 d2, d2
	VCMP.F64 d0, d2
	VMRS    APSR_nzcv, FPSCR
	MOVGE   a2, #0	@ No transform
	BGE     20f

@ Transform x < -0.68
	MOV     a2, #-1	@ Transform -1
10:
	ADRL    a3, v4_Trig64_dpiby4
	VLDMIA  a3, { d6-d7 }
	VADD.F64 d0, d6, d0
	VADD.F64 d3, d7, d1
	VADD.F64 d0, d0, d3	@ x := (piby4 ± x) + (piby4r ± xx)
	MOV     a3, #0
	VMOV    d1, a3, a3	@ xx := 0.0
20:
@ Core Remez approximation to tan(x+xx) on the interval [0.00 0.68]
	VPUSH   { d8-d9 }
	ADR     a3, v4_Trig64_dCtan
	VLDMIA  a3, { d3-d9 }
	VMUL.F64 d2, d0, d1
	VADD.F64 d2, d2, d2
	VMLA.F64 d2, d0, d0	@ r := x^2 + 2 * x * xx

	vfma.f64	d8, d9, d2	@ VFused64 d8, d9, d2
	vfma.f64	d7, d8, d2	@ VFused64 d7, d8, d2
	vfma.f64	d6, d7, d2	@ VFused64 d6, d7, d2
	VPOP    { d8-d9 }

	vfma.f64	d4, d5, d2	@ VFused64 d4, d5, d2
	vfma.f64	d3, d4, d2	@ VFused64 d3, d4, d2

	VDIV.F64 d4, d3, d6
	VMUL.F64 d4, d2, d4
	VMLA.F64 d1, d0, d4	@ result (t1,t2)

	ADRL    a3, v4_Trig64_done
	VLDR    d7, [a3]	@ d7 := 1.0
	VADD.F64 d3, d0, d1
	CMP     a2, #0
	BEQ     30f

@ Apply the earlier selected transform
@ Uses the identity       tan(-x) = -tan(x)
@               and tan(x + pi/4) = (tan(x) + 1) / (1 - tan(x))
@                => tan(pi/4 - x) = (1 - tan(x)) / (1 + tan(x))   as x -> -pi/4
@                   tan(x - pi/4) = (tan(x) - 1) / (1 + tan(x))   as x -> pi/4
	CMP     a1, #0	@ Want the reciprocal or not?
	VADDEQ.F64 d5, d7, d3
	VSUBNE.F64 d5, d3, d7
	VDIV.F64 d3, d3, d5
	VADD.F64 d3, d3, d3
	VSUBEQ.F64 d0, d7, d3
	VSUBNE.F64 d0, d3, d7
	CMP     a2, #-1
	VNEGEQ.F64 d0, d0	@ Fixup the sign
	BX      lr
30:
	CMP     a1, #0
	VMOVEQ.F64 d0, d3	@ No reciprocal
	BXEQ    lr

@ Compute -1.0/(t1 + t2) accurately
	VMOV    a3, a4, d3
	MOV     a1, #0
	VMOV    d4, a1, a4	@ z1 := high 32 bits
	VSUB.F64 d5, d4, d0
	VSUB.F64 d5, d1, d5	@ z2 := diff

	VDIV.F64 d3, d7, d3
	VNEG.F64 d2, d3	@ -1/(t1+t2)
	VMOV    a3, a4, d2
	VMOV    d3, a1, a4	@ -1/(t1+t2) high 32 bits

	vfma.f64	d7, d3, d4	@ VFused64 d7, d3, d4
	vfma.f64	d7, d3, d5	@ VFused64 d7, d3, d5
	vfma.f64	d3, d2, d7	@ VFused64 d3, d2, d7
	VMOV.F64 d0, d3
	BX      lr

v4_Trig64_d2uponpi:	.word	0x6DC9C883, 0x3FE45F30	@ 2/pi
v4_Trig64_dpi1:	.word	0x54400000, 0x3FF921FB	@ First 17sf of pi/2 in machine representation
v4_Trig64_dpi2:	.word	0x1A626331, 0x3DD0B461	@ Next 17sf of pi/2 in machine representation
v4_Trig64_dpi3:	.word	0x1A600000, 0x3DD0B461	@ First 17sf of dpi2 in machine representation
v4_Trig64_dpi4:	.word	0x2E037073, 0x3BA3198A	@ Next 17sf of dpi2 in machine representation
v4_Trig64_dpi5:	.word	0x2E000000, 0x3DD0B461	@ First 17sf of dpi3 in machine representation
v4_Trig64_dpi6:	.word	0x252049C1, 0x397B839A	@ Next 17sf of dpi3 in machine representation
v4_Trig64_d5e6:	.word	0x00000000, 0x415312D0	@ 5e6
v4_Trig64_d18p52:	.word	0x00000000, 0x43380000	@ 0x1.8000000000000p52

@ Range reduce x to a doubledouble
@ x > piby4
v4_Trig64_SinRangeReduce:
v4_Trig64_CosRangeReduce:
	push	{v2, lr}
	LDRD    a3, a4, v4_Trig64_d5e6
	CMP     a2, a4
	CMPEQ   a1, a3
	BCS     30f

@ Fast reduce the argument to be in a range from -pi/4 to +pi/4
@ by subtracting multiples of pi/2
	VLDR    d2, v4_Trig64_d2uponpi
	VLDR    d7, v4_Trig64_d18p52
	VMUL.F64 d2, d0, d2	@ |x| * 2/pi
	VADD.F64 d5, d2, d7
	VMOV    a3, a4, d5
	AND     v2, a3, #3	@ region
	VSUB.F64 d5, d5, d7
	VLDR    d6, v4_Trig64_dpi1
	VLDR    d7, v4_Trig64_dpi2
	VMUL.F64 d3, d5, d6
	VSUB.F64 d3, d0, d3	@ rhead
	VMUL.F64 d4, d5, d7	@ rtail
	VSUB.F64 d0, d3, d4	@ r
	VMOV    a3, a4, d0
	mov	ip, a4, lsl #1	@ ExpBits64 ip, a4
	mov	ip, ip, lsr #21
	SUB     ip, v3, ip	@ Exponent difference
	CMP     ip, #15
	BLS     20f

	VLDR    d6, v4_Trig64_dpi3	@ Big difference, go next double down
	VLDR    d7, v4_Trig64_dpi4
	VMUL.F64 d2, d5, d6
	VMOV.F64 d6, d3
	VSUB.F64 d3, d6, d2	@ rhead
	VSUB.F64 d4, d6, d3
	VSUB.F64 d4, d4, d2
	VMUL.F64 d6, d5, d7
	VSUB.F64 d4, d6, d4	@ rtail
10:
	VSUB.F64 d0, d3, d4	@ r
20:
	VSUB.F64 d3, d3, d0
	VSUB.F64 d1, d3, d4	@ rr = (rhead - r) - rtail
	B       40f
30:
@ x > 5e6 (massive), go for slow modulus reduction
	SUB     sp, sp, #8
	MOV     a1, sp
	BL      v4_Trig64_RemainderPiBy2
	LDR     v2, [sp], #8	@ region
40:
	MOV     a1, v2
	pop	{v2, pc}

@ Fast unsigned divide by 10: dividend in a1
@ Returns quotient in a1, remainder in a2
v4_Trig64_DivRem10:
	LDR     a2, =0xCCCCCCCD	@ (8^32) / 10
	UMULL   ip, a3, a2, a1
	MOV     a3, a3, LSR #3	@ Accurate division by 10
	SUB     a2, a1, a3, LSL #1
	MOV     a1, a3
	SUB     a2, a2, a3, LSL #3
	BX      lr

v4_Trig64_pibits:
	.equ	v4_Trig64_BITSPER, 10
	.hword	0,    0,    0,    0,    0,    0
	.hword	162,  998,   54,  915,  580,   84,  671,  777,  855,  839
	.hword	851,  311,  448,  877,  553,  358,  316,  270,  260,  127
	.hword	593,  398,  701,  942,  965,  390,  882,  283,  570,  265
	.hword	221,  184,    6,  292,  750,  642,  465,  584,  463,  903
	.hword	491,  114,  786,  617,  830,  930,   35,  381,  302,  749
	.hword	72,  314,  412,  448,  619,  279,  894,  260,  921,  117
	.hword	569,  525,  307,  637,  156,  529,  504,  751,  505,  160
	.hword	945, 1022,  151, 1023,  480,  358,   15,  956,  753,   98
	.hword	858,   41,  721,  987,  310,  507,  242,  498,  777,  733
	.hword	244,  399,  870,  633,  510,  651,  373,  158,  940,  506
	.hword	997,  965,  947,  833,  825,  990,  165,  164,  746,  431
	.hword	949, 1004,  287,  565,  464,  533,  515,  193,  111,  798

	.ltorg

@ doubledouble remainder_piby2(double x, int *region)
@ Given positive argument x, reduce it to the range [-pi/4,pi/4] using
@ extra precision, and return the doubledouble result.
@ Return value "region" tells how many lots of pi/2 were subtracted
@ from x modulo 4 to get it in range, hence the quadrant.

@ Based on a translation to AArch32 of
@       aocl-libm-ose/src/ref/remainder_piby2.c
@       Copyright (c) 2002-2019 Advanced Micro Devices, Inc.
@       SPDX-License-Identifier: MIT
v4_Trig64_RemainderPiBy2:
	push	{a1, v1-v6, lr}

	VMOV    v1, v2, d0
	mov	a3, v2, lsl #1	@ ExpBits64 a3, v2
	mov	a3, a3, lsr #21
	LDR     a2, =1023
	SUB     a1, a3, a2	@ Unbiased exp
	BIC     v2, v2, #0xF0000000
	BIC     v2, v2, #0x0FF00000
	ORR     v2, v2, #0x00100000	@ Normalised fabs(x)

	BL      v4_Trig64_DivRem10
	push	{a2}	@ Remainder resexp

@ 180 is the theoretical maximum number of bits (actually 175 for
@ IEEE double precision) that we need to extract from the middle
@ of 2/pi to compute the reduced argument accurately enough for our purposes
	ADD     a2, a1, #180 / v4_Trig64_BITSPER

	MOV     a4, #1<<v4_Trig64_BITSPER
	SUB     a4, a4, #1	@ Mask

	ADR     a3, v4_Trig64_pibits
	SUB     sp, sp, #20*2	@ For long long multiply intermediate results

@ a1 = first
@ a2 = last = i
@ a3 = pibits
@ a4 = mask
@ for (i = last; i >= first; i--)
@     u = pibits[i] * ux + carry
@     res[i - first] = u & mask
@     carry = u >> bitsper
@ next
@ res[last - first + 1] = 0
	MOV     v5, #0
	MOV     v6, #0
	STRH    v5, [sp, #((180 / v4_Trig64_BITSPER) + 1)*2]
10:
	MOV     ip, a2, LSL #1
	LDRH    lr, [a3, ip]
	UMULL   v3, ip, lr, v1
	MLA     v4, lr, v2, ip

	SUB     ip, a2, a1
	MOV     ip, ip, LSL #1	@ index into array of intermediate results

	ADDS    v5, v3, v5
	ADC     v6, v4, v6	@ v5,v6 := u
	AND     lr, v5, a4
	STRH    lr, [sp, ip]

	MOV     v5, v5, LSR #v4_Trig64_BITSPER
	ORR     v5, v5, v6, LSL #32-v4_Trig64_BITSPER
	MOV     v6, v6, LSR #v4_Trig64_BITSPER	@ next carry

	SUB     a2, a2, #1
	CMP     a2, a1
	BGE     10b

@ ltb = (((res[0] << bitsper) | res[1]) >> (bitsper - 1 - resexp)) & 7
	LDRH    a1, [sp, #0]
	LDRH    a2, [sp, #2]	@ a2 := res[1]
	LDR     a3, [sp, #(20*2)+0]	@ a3 := resexp
	RSB     ip, a3, #v4_Trig64_BITSPER - 1
	ORR     a1, a2, a1, LSL #v4_Trig64_BITSPER
	MOV     a1, a1, LSR ip
	AND     a1, a1, #7	@ a1 := ltb

@ Output region
	MOV     a4, a1, LSR #1
	TST     a1, #1
	ADDNE   a4, a4, #1
	ANDNE   a4, a4, #3
	LDR     ip, [sp, #(20*2)+(1*4)]
	STR     a4, [ip]

@ Reconstruct the result
	MOV     v3, #1
	MOV     v4, #0	@ v3,v4 := mant

	RSB     a4, a3, #v4_Trig64_BITSPER	@ Positive since resexp is the remainder from DIV BITSPER
	RSB     ip, a4, #32
	MOV     v4, v4, LSL a4
	ORR     v4, v4, v3, LSR ip
	MOV     v3, v3, LSL a4

	SUBS    v3, v3, #1
	SBC     v4, v4, #0

	TST     a1, #1
	MVNNE   a2, a2	@ ~res[1]
	AND     v3, v3, a2
	MOVEQ   v4, #0

@ a2 = i = 1
@ while (mant < 0x0020000000000000)
@     i++
@     mant = (mant << bitsper) | res[i]
@ endwhile
	MOV     a2, #1
	MOV     a4, #1<<v4_Trig64_BITSPER
	SUB     a4, a4, #1	@ Mask
20:
	CMP     v4, #0x00200000
	CMPEQ   v3, #0
	BCS     30f
	ADD     a2, a2, #1
	MOV     ip, a2, LSL #1	@ index into array of intermediate results
	LDRH    lr, [sp, ip]

	MOV     v4, v4, LSL #v4_Trig64_BITSPER
	ORR     v4, v4, v3, LSR #32-v4_Trig64_BITSPER
	TST     a1, #1
	MVNNE   lr, lr	@ ~res[i]
	ANDNE   lr, lr, a4
	ORR     v3, lr, v3, LSL #v4_Trig64_BITSPER
	B       20b
30:
	ADD     ip, sp, a2, LSL #1
	LDRH    lr, [ip, #2]	@ res[i + 1]
	TST     a1, #1
	MVNNE   lr, lr	@ ~res[i + 1]
	MOV     v5, #0
	MOV     v6, lr, LSL #32-v4_Trig64_BITSPER	@ v5,v6 := highbitsrr

	ADD     sp, sp, #20*2

@ rexp = 52 + resexp - i * bitsper
	ADD     a3, a3, #52
	MOV     a4, #-v4_Trig64_BITSPER
	MLA     a3, a4, a2, a3	@ a3 := rexp

@ while (mant >= 0x0020000000000000)
@     rexp++;
@     highbitsrr = (highbitsrr >> 1) | ((mant & 1) << 63)
@     mant = mant >> 1
@ endwhile
40:
	CMP     v4, #0x00200000
	CMPEQ   v3, #0
	BCC     50f
	ADD     a3, a3, #1
	MOVS    v6, v6, LSR #1
	MOV     v5, v5, RRX
	TST     v3, #1
	ORRNE   v6, v6, #1<<31
	MOVS    v4, v4, LSR #1
	MOV     v3, v3, RRX
	B       40b
50:
@ Join rexp and mant together
	LDR     a2, =1023
	ADD     a4, a3, a2
	BIC     v4, v4, #0xF0000000
	BIC     v4, v4, #0x0FF00000
	ORR     v4, v4, a4, LSL #20
	TST     a1, #1
	ORRNE   v4, v4, #1<<31	@ If we negated the mantissa we negate x too
	VMOV    d2, v3, v4	@ d2 := x

@ Join rexp and highbitsrr together
	MOV     v5, v5, LSR #12
	ORR     v5, v5, v6, LSL #32-12
	MOV     v6, v6, LSR #12	@ Note this is shifted one place too far
	SUB     a4, a4, #53
	MOV     a4, a4, LSL #20
	MOV     a3, #0
	VMOV    d4, a3, a4
	ORR     a3, a3, v5
	ORR     a4, a4, v6
	VMOV    d3, a3, a4	@ d3 := xx
	VSUB.F64 d3, d3, d4	@ Subtract the implicit bit from normalisation
	VADD.F64 d3, d3, d3	@ Double to account for the shift one place too far
	TST     a1, #1
	VNEGNE.F64 d3, d3	@ Set the correct sign

@ (x,xx) is a doubledouble version of the fractional part of
@ x * 2 / pi. Multiply (x,xx) by pi/2 in extra precision
@ to get the doubledouble reduced argument (r,rr)
	VLDR    d6, v4_Trig64_piby2_lead
	AND     v3, v3, #0xF8000000
	VMOV    d5, v3, v4	@ head of x, hx
	VSUB.F64 d4, d2, d5	@ tail of x, tx
	VMUL.F64 d0, d6, d2	@ d0 := c

	VLDR    d6, v4_Trig64_piby2_part1
	VLDR    d7, v4_Trig64_piby2_part2
	VMUL.F64 d1, d6, d5
	VSUB.F64 d1, d1, d0
	VMLA.F64 d1, d6, d4
	VLDR    d6, v4_Trig64_piby2_lead
	VMLA.F64 d1, d7, d5
	VMLA.F64 d1, d7, d4
	VLDR    d7, v4_Trig64_piby2_part3
	VMUL.F64 d3, d6, d3	@  piby2_lead * xx
	VMLA.F64 d3, d7, d2	@ (piby2_lead * xx) + (piby2_part3 * x)
	VADD.F64 d1, d1, d3

	VADD.F64 d2, d0, d1	@ r = c + cc
	VSUB.F64 d0, d0, d2
	VADD.F64 d1, d0, d1	@ rr = (c - r) + cc
	VMOV.F64 d0, d2

	ADD     sp, sp, #8
	pop	{v1-v6, pc}

v4_Trig64_piby2_lead:	.word	0x54442d18, 0x3ff921fb	@ 1.57079632679489655800e+00
v4_Trig64_piby2_part1:	.word	0x50000000, 0x3ff921fb	@ 1.57079631090164184570e+00
v4_Trig64_piby2_part2:	.word	0x60000000, 0x3e5110b4	@ 1.58932547122958567343e-08
v4_Trig64_piby2_part3:	.word	0x33145c06, 0x3c91a626	@ 6.12323399573676480327e-17


	.ltorg				@ objasm's literal pool at the end of the AREA
	.if !((( v4_Trig64_dpiby4r) - (v4_Trig64_dpiby4 + 8 )) == 0)
	.error "ASSERT failed: v4_Trig64_dpiby4 + 8 = v4_Trig64_dpiby4r"
	.endif
	.if !((( 10) - (v4_Trig64_BITSPER )) == 0)
	.error "ASSERT failed: v4_Trig64_BITSPER = 10"
	.endif
	.if !(v4_Trig64_BITSPER <= 32)
	.error "ASSERT failed: v4_Trig64_BITSPER <= 32"
	.endif
	.if !(v4_Trig64_BITSPER <= 16)
	.error "ASSERT failed: v4_Trig64_BITSPER <= 16"
	.endif
