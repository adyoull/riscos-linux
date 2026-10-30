@ Trig32: from RISC OS Open's VFPSupport s/Trig32, translated to GNU as
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


@ EXPORT fp32cos
@ EXPORT fp32sin
@ EXPORT fp32tan

	.text
	.arm


v3_Trig32_fone:	.word	0x3F800000	@ (float)1
v3_Trig32_d18p52:	.word	0x00000000, 0x43380000	@ 0x1.8000000000000p52
v3_Trig32_dminus1:	.word	0x00000000, 0xBFF00000	@ -1
v3_Trig32_d1uponpi:	.word	0x6DC9C883, 0x3FD45F30	@ 1/pi
v3_Trig32_d1upon2pi:	.word	0x6DC9C883, 0x3FE45F30	@ 1/2pi
v3_Trig32_dpiby2:	.word	0x54442D18, 0x3FF921FB	@ pi/2

@ float fp32cos(float x)
@ Use the identity cos(x) = sin(x + pi/2)

@ Exceptions per ISO9899:2018 F10.1.5
@       x = ±INF is an invalid operation
v3_fp32cos:
@ Deal with special cases
	VMOV    a1, s0
	BICS    a2, a1, #1<<31
	VLDREQ  s0, v3_Trig32_fone
	BXEQ    lr	@ cos(±0) = 1
	push	{v1, lr}
	mov	a3, a2, lsl #1	@ ExpBits32 a3, a2
	mov	a3, a3, lsr #24
	CMP     a3, #0xFF
	BNE     10f
	MOVS    a3, a1, LSL #9
	ORREQ   v1, a1, #1<<22	@ ±INF => QNaN
	MOVNE   v1, a1	@ NaNs => propagated
	MOV     a1, #FPSCR_IOC
	BL      v3_RaiseException
	VMOV    s0, v1
	pop	{v1, pc}
10:
	CMP     a2, #1<<23	@ Subnormal and too small to scale
	BCS     20f
	VLDR    s0, v3_Trig32_fone
	MOV     a1, #FPSCR_UFC
	BL      v3_RaiseException
	pop	{v1, pc}
20:
	VCVT.F64.F32 d0, s0
	ldr	ip, =(127 + (26)) << 23	@ LoadExp32 ip, 26
	CMP     a2, ip
	BCC     30f	@ Small enough to use fast route
	BL      v3_fp64cos	@ Use doubledouble argument reduction route
	B       40f
30:
	VLDR    d1, v3_Trig32_dpiby2
	VADD.F64 d0, d0, d1
	VMOV    a1, s1	@ Needed to get the sign right
	B       30f

@ float fp32sin(float x)
@ Reduce argument x to   |x| = (N * pi) + f
@ where N is an integer    N = round(x / pi)
@                          f = |x| - (N * pi)
@ then because sine is periodic sin(x) = sin((N * pi) + f)
@ then use the identity         sin((N * pi) + f) = (sin(N * pi) * cos(f)) + (cos(N * pi) * sin(f))
@                                                 = (     0      * cos(f)) + (    ±1      * sin(f))
@ where f is in the range [-pi/2 +pi/2] approximated by a polynomial.

@ Exceptions per ISO9899:2018 F10.1.6
@       x = ±INF is an invalid operation
@ Based on a translation to AArch32 of
@       aocl-libm-ose/src/optmized/sinf.c
@       Copyright (C) 2008-2020 Advanced Micro Devices, Inc. All rights reserved.
@       SPDX-License-Identifier: BSD-3-Clause
v3_fp32sin:
@ Deal with special cases
	VMOV    a1, s0
	BICS    a2, a1, #1<<31
	BXEQ    lr	@ sin(±0) = ±0
	push	{v1, lr}
	mov	a3, a2, lsl #1	@ ExpBits32 a3, a2
	mov	a3, a3, lsr #24
	CMP     a3, #0xFF
	BNE     10f
	MOVS    a3, a1, LSL #9
	ORREQ   v1, a1, #1<<22	@ ±INF => QNaN
	MOVNE   v1, a1	@ NaNs => propagated
	MOV     a1, #FPSCR_IOC
	BL      v3_RaiseException
	VMOV    s0, v1
	pop	{v1, pc}
10:
	CMP     a2, #1<<23	@ Subnormal and too small to scale
	BCS     20f
	MOV     a1, #FPSCR_UFC
	BL      v3_RaiseException
	pop	{v1, pc}

v3_Trig32_dpi1:	.word	0x50000000, 0x400921FB	@ First 17sf of pi in machine representation
v3_Trig32_dpi2:	.word	0x611A6263, 0x3E6110B4	@ Next 17sf of pi in machine representation
@ Remez's coefficients from Sollya
v3_Trig32_dCsin:	.word	0x00000000, 0x3FF00000	@  0x1.p0
	.word	0xD018DF8B, 0xBFC55554	@ -0x1.55554d018df8bp-3
	.word	0x293A5DCB, 0x3F8110F0	@  0x1.110f0293a5dcbp-7
	.word	0xA0AEBDB9, 0xBF29F781	@ -0x1.9f781a0aebdb9p-13
	.word	0xE7550C85, 0x3EC5E2A3	@  0x1.5e2a3e7550c85p-19

20:
	VCVT.F64.F32 d0, s0
	ldr	ip, =(127 + (26)) << 23	@ LoadExp32 ip, 26
	CMP     a2, ip
	BCC     30f	@ Small enough to use fast route
	BL      v3_fp64sin	@ Use doubledouble argument reduction route
	B       40f
30:
@ Argument reduction
	VABS.F64 d0, d0	@ xd = fabs((double)x)
	VLDR    d1, v3_Trig32_d18p52
	VLDR    d2, v3_Trig32_d1uponpi
	VMOV.F64 d3, d1
	vpush	{d0-d7}	@ VFused64 d1, d0, d2
	vldr	d1, [sp, #16]
	vldr	d2, [sp, #8]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #8]
	vpop	{d0-d7}
	VMOV    v1, s2
	MOV     v1, v1, LSL #31	@ oddness of dn
	AND     a1, a1, #1<<31	@ sign bit of x
	VSUB.F64 d1, d1, d3	@ dn -= scaler
	VLDR    d3, v3_Trig32_dpi1
	VLDR    d4, v3_Trig32_dpi2
	VMLS.F64 d0, d1, d3	@ F = xd - (dn * pi1)
	VMLS.F64 d0, d1, d4	@ F = F  - (dn * pi2)

@ Calculate the polynomial C1*F^1 + C3*F^3 + C5*F^5 + C7*F^7 + C9*F^9
@                        = F * (C1 + C3*F^2 + C5*F^4 + C7*F^6 + C9*F^8)
@                        = F * (C1 + (F^2 * (C3 + C5*F^2)) + (F^4 * (C7*F^2 + C9*F^4)))
	ADR     a2, v3_Trig32_dCsin
	VLDMIA  a2, { d3-d7 }
	VMUL.F64 d1, d0, d0
	VMUL.F64 d2, d1, d1
	VMLA.F64 d4, d5, d1	@ d4 :=               C3 + C5*F^2
	VMLA.F64 d3, d1, d4	@ d3 := (C1 + (F^2 * (C3 + C5*F^2))
	VMUL.F64 d1, d6, d1	@ d1 :=                                       C7*F^2
	VMLA.F64 d1, d7, d2	@ d1 :=                                       C7*F^2 + C9*F^4
	VMUL.F64 d7, d2, d1	@ d7 :=                               (F^4 * (C7*F^2 + C9*F^4))
	VADD.F64 d2, d3, d7	@ d2 := (C1 + (F^2 * (C3 + C5*F^2)) + (F^4 * (C7*F^2 + C9*F^4)))
	VMUL.F64 d0, d0, d2
@ Fixup the signs
	VMOV    a3, a4, d0
	EOR     a1, a1, v1
	EOR     a4, a1, a4	@ Sign = polynomial ^ sign of x ^ oddness of dn
	VMOV    d0, a3, a4
40:
	VCVT.F32.F64 s0, d0
	pop	{v1, pc}

@ float fp32tan(float x)
@ Reduce argument x to   |x| = (N * pi) + f
@ where N is an integer    N = round(x / pi/2)
@                          f = |x| - (N * pi/2)
@ then because tangent is periodic tan(x) = tan((N * pi/2) + f)
@ Using the identity tan(pi/2 - x) = cot(x)
@                and       tan(-x) = -tan(x)
@               tan(f - (N *pi/2)) = -cot(f) = -1/tan(f)    for N odd
@                           tan(f) = tan(x)                 for N even
@ where f is in the range [-pi/4 +pi/4] approximated by a polynomial.

@ Exceptions per ISO9899:2018 F10.1.7
@       x = ±INF is an invalid operation
@ Based on a translation to AArch32 of
@       aocl-libm-ose/src/optmized/tanf.c
@       Copyright (C) 2008-2020 Advanced Micro Devices, Inc. All rights reserved.
@       SPDX-License-Identifier: BSD-3-Clause
v3_fp32tan:
@ Deal with special cases
	VMOV    a1, s0
	BICS    a2, a1, #1<<31
	BXEQ    lr	@ tan(±0) = ±0
	push	{v1, lr}
	mov	a3, a2, lsl #1	@ ExpBits32 a3, a2
	mov	a3, a3, lsr #24
	CMP     a3, #0xFF
	BNE     10f
	MOVS    a3, a1, LSL #9
	ORREQ   v1, a1, #1<<22	@ ±INF => QNaN
	MOVNE   v1, a1	@ NaNs => propagated
	MOV     a1, #FPSCR_IOC
	BL      v3_RaiseException
	VMOV    s0, v1
	pop	{v1, pc}
10:
	CMP     a2, #1<<23	@ Subnormal and too small to scale
	BCS     20f
	MOV     a1, #FPSCR_UFC
	BL      v3_RaiseException
	pop	{v1, pc}

v3_Trig32_dhalfpi1:	.word	0x54400000, 0x3FF921FB	@ First 17sf of 1/2pi in machine representation
v3_Trig32_dhalfpi2:	.word	0x1A626331, 0x3DD0B461	@ Next 17sf of 1/2pi in machine representation
@ Remez's coefficients from Sollya
v3_Trig32_dCtan:	.word	0xF99AC046, 0x3FEFFFFF	@  0x1.ffffff99ac0468p-1,
	.word	0x193ECF2B, 0x3FD55559	@  0x1.55559193ecf2bp-2,
	.word	0xF4BA8F40, 0x3FC1106B	@  0x1.1106bf4ba8f408p-3,
	.word	0xB6650308, 0x3FABBAFB	@  0x1.bbafbb6650308p-5,
	.word	0x8922DF5F, 0x3F956163	@  0x1.561638922df5fp-6,
	.word	0x33A88788, 0x3F87F3A0	@  0x1.7f3a033a88788p-7,
	.word	0x1D269620, 0xBF4BA0D4	@ -0x1.ba0d41d26961f8p-11,
	.word	0x4EFF28AC, 0x3F73952B	@  0x1.3952b4eff28ac8p-8,

20:
	VCVT.F64.F32 d0, s0
	ldr	ip, =(127 + (26)) << 23	@ LoadExp32 ip, 26
	CMP     a2, ip
	BCC     30f	@ Small enough to use fast route
	BL      v3_fp64tan	@ Use doubledouble argument reduction route
	B       40f
30:
@ Argument reduction
	VABS.F64 d0, d0	@ xd = fabs((double)x)
	VLDR    d1, v3_Trig32_d18p52
	VLDR    d2, v3_Trig32_d1upon2pi
	VMOV.F64 d3, d1
	vpush	{d0-d7}	@ VFused64 d1, d0, d2
	vldr	d1, [sp, #16]
	vldr	d2, [sp, #8]
	push	{a1-a4, ip, lr}
	bl	v3_fused64_muladd
	pop	{a1-a4, ip, lr}
	vstr	d0, [sp, #8]
	vpop	{d0-d7}
	VMOV    v1, s2
	MOV     v1, v1, LSL #31	@ oddness of dn
	AND     a1, a1, #1<<31	@ sign bit of x
	VSUB.F64 d1, d1, d3	@ dn -= scaler
	VLDR    d3, v3_Trig32_dhalfpi1
	VLDR    d4, v3_Trig32_dhalfpi2
	VMLS.F64 d0, d1, d3	@ F = xd - (dn * halfpi1)
	VMLS.F64 d0, d1, d4	@ F = F  - (dn * halfpi2)

@ Calculate the polynomial C1*F^1 + C3*F^3 + C5*F^5 + C7*F^7 + C9*F^9 + C11*F^11 + C13*F^13 + C15*F^15
@                        = F * (C1 + C3*F^2 + C5*F^4 + C7*F^6 + C9*F^8 + C11*F^10 + C13*F^12 + C15*F^14)
@                        = F * ((C1 + C3*F^2) + (F^4 * (C5 + C7*F^2)) + (F^8 * (C9 + C11*F^2)) + (F^12 * (C13 + C15*F^2)))
	VPUSH   { d8-d12 }
	ADR     a2, v3_Trig32_dCtan
	VLDMIA  a2, { d5-d12 }
	VMUL.F64 d1, d0, d0
	VMUL.F64 d2, d1, d1
	VMUL.F64 d3, d2, d2
	VMUL.F64 d4, d3, d2
	VMLA.F64 d5, d6, d1	@ d5 :=  C1 + C3*F^2
	VMLA.F64 d7, d8, d1	@ d7 :=                         C5 + C7*F^2
	VMLA.F64 d5, d2, d7	@ d5 := (C1 + C3*F^2) + (F^4 * (C5 + C7*F^2))
	VMLA.F64 d9, d10, d1	@ d9 :=         C9 + C11*F^2
	VMLA.F64 d11, d12, d1	@ d11:=                                   C13 + C15*F^2
	VMUL.F64 d6, d3, d9	@ d6 :=  F^8 * (C9 + C11*F^2)
	VMUL.F64 d7, d4, d11	@ d7 :=                           F^12 * (C13 + C15*F^2)
	VPOP    { d8-d12 }
	VADD.F64 d4, d6, d7	@ d4 := (F^8 * (C9 + C11*F^2)) + (F^12 * (C13 + C15*F^2))
	VADD.F64 d2, d4, d5
	VMUL.F64 d0, d0, d2
@ Fixup the signs
	VMOV    a3, a4, d0
	EOR     a4, a1, a4	@ Sign = polynomial ^ sign of x
	VMOV    d0, a3, a4
	TST     v1, #1<<31
	BEQ     40f
	ORRS    a1, a3, a4, LSL #1
	MOVEQ   a1, #FPSCR_DZC
	BLEQ    v3_RaiseException
	VLDR    d1, v3_Trig32_dminus1
	VDIV.F64 d0, d1, d0	@ When odd, swap for -1.0 / tanx
40:
	VCVT.F32.F64 s0, d0
	pop	{v1, pc}


	.ltorg				@ objasm's literal pool at the end of the AREA
