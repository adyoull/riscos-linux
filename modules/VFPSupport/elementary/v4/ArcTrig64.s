@ ArcTrig64: from RISC OS Open's VFPSupport s/ArcTrig64, translated to GNU as
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


@ EXPORT fp64acos
@ EXPORT fp64asin
@ EXPORT fp64atan
@ EXPORT fp64atan2

	.text
	.arm


@ double fp64acos(double x)
@ Use the identity acos(x) = pi/2 - asin(x)

@ Exceptions per ISO9899:2018 F10.1.1
@       |x| > 1 is an invalid operation
v4_fp64acos:
@ Deal with special cases
	VMOV    a1, a2, d0
	push	{v1, lr}
	MOV     v1, a2, LSR #31	@ Sign of x
	BIC     a2, a2, #1<<31	@ |x|
	LDRD    a3, a4, v4_ArcTrig64_done
	CMP     a2, a4
	CMPEQ   a1, a3
	BCC     20f
	BHI     10f
	TST     v1, #1
	VMOVEQ  d0, a3, a3	@ acos(+1) = 0
	ADRLNE  a3, v4_ArcTrig64_dpi
	VLDRNE  d0, [a3]	@ acos(-1) = pi
	pop	{v1, pc}
10:
	MOV     a1, #FPSCR_IOC
	BL      v4_RaiseException
	VLDR    d0, v4_ArcTrig64_dqnan	@ |x| > 1 => QNaN
	pop	{v1, pc}
20:
@ |x| < 1
	BL      v4_fp64asin
	ADRL    a1, v4_ArcTrig64_dpiby2
	VLDR    d1, [a1]
	VSUB.F64 d0, d1, d0
	pop	{v1, pc}

v4_ArcTrig64_done:	.word	0x00000000, 0x3FF00000	@ (double)1
v4_ArcTrig64_dhalf:	.word	0x00000000, 0x3FE00000	@ 1/2

@ double fp64asin(double x)
@ Split into subranges
@ [0 0.5] asin = x + x * x^2 * polynomial(x^2)
@ [0.5 1] asin = pi/2 - 2 * asin(sqrt((1 - |x|) / 2))

@ Exceptions per ISO9899:2018 F10.1.2
@       |x| > 1 is an invalid operation
@ Based on a translation to AArch32 of
@       openlibm/src/e_asin.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunSoft, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v4_fp64asin:
@ Deal with special cases
	VMOV    a1, a2, d0
	ORRS    a4, a1, a2, LSL #1
	BXEQ    lr	@ asin(±0) = ±0
	push	{v1, lr}
	MOV     v1, a2, LSR #31	@ Sign of x
	BIC     a2, a2, #1<<31	@ |x|
	LDRD    a3, a4, v4_ArcTrig64_done
	CMP     a2, a4
	CMPEQ   a1, a3
	BCC     10f
	ADRLEQ  a1, v4_ArcTrig64_dpiby2
	VLDREQ  d0, [a1]	@ asin(±1) = ±pi/2
	BEQ     50f
	MOV     a1, #FPSCR_IOC
	BL      v4_RaiseException
	VLDR    d0, v4_ArcTrig64_dqnan	@ |x| > 1 => QNaN
	pop	{v1, pc}
10:
	ldr	ip, =(1023 + (-1)) << 20	@ LoadExp64 ip, -1
	CMP     a2, ip
	CMPEQ   a1, #0
	BCS     30f
	ldr	ip, =(1023 + (-26)) << 20	@ LoadExp64 ip, -26
	CMP     a2, ip
	CMPEQ   a1, #0
	BCS     20f
	MOV     a1, #FPSCR_IXC
	pop	{v1, lr}
	B       v4_RaiseException	@ Approx asin(x) = x
20:
@ 2^-26 <= |x| < 0.5
	VPUSH   { d0 }
	VMUL.F64 d0, d0, d0	@ x^2
	BL      60f
	VMOV.F64 d7, d0
	VPOP    { d0 }
	VMLA.F64 d0, d0, d7	@ = x + (x * p/q)
	pop	{v1, pc}
30:
@ 0.5 <= |x| < 1
	VLDR    d2, v4_ArcTrig64_dhalf
	VMOV    d1, a3, a4	@ 1.0
	VABS.F64 d3, d0
	VSUB.F64 d3, d1, d3
	VMUL.F64 d0, d3, d2	@ 1/2 * (1 - |x|)
	VPUSH   { d0 }
	push	{a1-a2}
	BL      60f
	pop	{a1-a2}
	LDR     ip, =0x3FEF3333	@ Approx 0.975
	VPOP    { d1 }
	ADRL    a3, v4_ArcTrig64_dpiby2
	VLDMIA  a3, { d3-d4 }
	VSQRT.F64 d2, d1
	CMP     a2, ip
	BCC     40f
	VMLA.F64 d2, d2, d0
	VADD.F64 d2, d2, d2
	VSUB.F64 d5, d2, d4
	VSUB.F64 d0, d3, d5	@ asin(x) = pi/2 - 2 * (s + (s * poly))
	B       50f
40:
@ |x| < 0.975
	VMOV    a3, a4, d2
	MOV     a3, #0
	VMOV    d5, a3, a4
	VADD.F64 d6, d2, d5
	VMLS.F64 d1, d5, d5
	VDIV.F64 d1, d1, d6
	VLDR    d3, v4_ArcTrig64_dpiby4
	VADD.F64 d1, d1, d1
	VSUB.F64 d4, d4, d1
	VMUL.F64 d6, d2, d0
	VADD.F64 d6, d6, d6
	VSUB.F64 d6, d6, d4	@ p = 2 * (s * poly)
	VADD.F64 d5, d5, d5
	VSUB.F64 d4, d3, d5	@ q = pi/4 - 2 * s
	VSUB.F64 d2, d6, d4
	VSUB.F64 d0, d3, d2	@ asin(x) = pi/4 - (p - q)
50:
	TST     v1, #1
	VNEGNE.F64 d0, d0	@ Fixup the sign
	pop	{v1, pc}

v4_ArcTrig64_dqnan:	.word	0x00000000, 0x7FF80000	@ QNaN
v4_ArcTrig64_dPSin:	.word	0x55555555, 0x3FC55555	@  1.66666666666666657415e-01
	.word	0x03EB6F7D, 0xBFD4D612	@ -3.25565818622400915405e-01
	.word	0x0E884455, 0x3FC9C155	@  2.01212532134862925881e-01
	.word	0xB5688F3B, 0xBFA48228	@ -4.00555345006794114027e-02
	.word	0x7501B288, 0x3F49EFE0	@  7.91534994289814532176e-04
	.word	0x0DFDF709, 0x3F023DE1	@  3.47933107596021167570e-05
v4_ArcTrig64_dQSin:	.word	0x1C8A2D4B, 0xC0033A27	@ -2.40339491173441421878e+00
	.word	0x9C598AC8, 0x40002AE5	@  2.02094576023350569471e+00
	.word	0x1B8D0159, 0xBFE6066C	@ -6.88283971605453293030e-01
	.word	0xB12E9282, 0x3FB3B8C5	@  7.70381505559019352791e-02

60:
@ Polynomial
	ADR     a3, v4_ArcTrig64_dPSin
	VLDMIA  a3, { d2-d7 }
	VLDR    d1, v4_ArcTrig64_done
	vfma.f64	d6, d0, d7	@ VFused64 d6, d0, d7
	vfma.f64	d5, d0, d6	@ VFused64 d5, d0, d6
	vfma.f64	d4, d0, d5	@ VFused64 d4, d0, d5
	VMLA.F64 d3, d0, d4
	VMLA.F64 d2, d0, d3
	VMUL.F64 d2, d0, d2	@ p
	ADR     a3, v4_ArcTrig64_dQSin
	VLDMIA  a3, { d4-d7 }
	vfma.f64	d6, d0, d7	@ VFused64 d6, d0, d7
	vfma.f64	d5, d0, d6	@ VFused64 d5, d0, d6
	vfma.f64	d4, d0, d5	@ VFused64 d4, d0, d5
	VMLA.F64 d1, d0, d4	@ q
	VDIV.F64 d0, d2, d1
	BX      lr

@ double fp64atan(double x)
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
@       openlibm/src/s_atan.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunPro, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v4_fp64atan:
@ Deal with special cases
	VMOV    a1, a2, d0
	push	{v1, lr}
	LDR     ip, =0x7FF
	MOV     v1, a2, LSR #31	@ Sign of x
	BIC     a2, a2, #1<<31	@ |x|
	CMP     a2, ip, LSL #20
	CMPEQ   a1, #0
	pophi	{v1, pc}	@ NaNs => propagated
	ldr	ip, =(1023 + (66)) << 20	@ LoadExp64 ip, 66
	CMP     a2, ip
	BCC     10f
	ADR     a3, v4_ArcTrig64_dtanid + (16 * 3)
	VLDMIA  a3, { d6-d7 }
	VADD.F64 d0, d6, d7
	TST     v1, #1
	VNEGNE.F64 d0, d0	@ Fixup the sign
	pop	{v1, pc}
10:
	ldr	ip, =(1023 + (-27)) << 20	@ LoadExp64 ip, -27
	CMP     a2, ip
	CMPEQ   a1, #0
	MOVCC   a1, #FPSCR_IXC
	popcc	{v1, lr}
	BCC     v4_RaiseException	@ Approx atan(x) = x

	LDR     ip, =0x3FDC0000	@ 7/16
	CMP     a2, ip
	CMPEQ   a1, #0
	MOVCC   a4, #-1	@ id := -1
	BCC     80f

	VLDR    d7, v4_ArcTrig64_done
	VABS.F64 d0, d0
@ |x| >= 7/16
	LDR     ip, =0x3FF30000	@ 19/16
	CMP     a2, ip
	CMPEQ   a1, #0
	BCS     30f
@ 7/16 <= |x| < 19/16
	LDR     ip, =0x3FE60000	@ 11/16
	CMP     a2, ip
	CMPEQ   a1, #0
	BCS     20f
@ 7/16 <= |x| < 11/16
	MOV     a4, #0	@ id := 0
	VADD.F64 d5, d7, d7
	VADD.F64 d4, d5, d0
	VMUL.F64 d5, d5, d0
	VSUB.F64 d5, d5, d7
	B       70f	@ (t - 0.5) / (1 + t/2)
20:
@ 11/16 <= |x| < 19/16
	MOV     a4, #1	@ id := 1
	VADD.F64 d4, d7, d0
	VSUB.F64 d5, d0, d7
	B       70f	@ (t - 1) / (1 + t)
30:
@ |x| >= 19/16
	LDR     ip, =0x40038000	@ 39/16
	CMP     a2, ip
	CMPEQ   a1, #0
@ 39/16 <= |x| < 2^66
	MOVCS   a4, #3
	VDIVCS.F64 d0, d7, d0
	VNEGCS.F64 d0, d0	@ -1/t
	BCS     80f
@ 19/16 <= |x| < 39/16
	MOV     a4, #2	@ id := 2
	VLDR    d6, v4_ArcTrig64_d1point5
	VMUL.F64 d4, d6, d0
	VADD.F64 d4, d7, d4
	VSUB.F64 d5, d0, d6	@ (t - 1.5) / (1 + 1.5*t)
70:
	VDIV.F64 d0, d5, d4
80:
@ Polynomial
	VPUSH   { d8 }
	VMUL.F64 d1, d0, d0	@ t^2
	VMUL.F64 d2, d1, d1	@ t^4
	ADR     a3, v4_ArcTrig64_dCatanE
	VLDMIA  a3!, { d3-d8 }	@ Even polynomial
	vfma.f64	d7, d2, d8	@ VFused64 d7, d2, d8
	vfma.f64	d6, d2, d7	@ VFused64 d6, d2, d7
	vfma.f64	d5, d2, d6	@ VFused64 d5, d2, d6
	VMLA.F64 d4, d2, d5
	VMLA.F64 d3, d2, d4
	VMUL.F64 d3, d1, d3	@ d3 := s1
	VLDMIA  a3, { d4-d8 }	@ Odd polynomial
	vfma.f64	d7, d2, d8	@ VFused64 d7, d2, d8
	vfma.f64	d6, d2, d7	@ VFused64 d6, d2, d7
	VMLA.F64 d5, d2, d6
	VMLA.F64 d4, d2, d5
	VMUL.F64 d4, d2, d4	@ d4 := s2
	VPOP    { d8 }
	VADD.F64 d2, d3, d4
	VMUL.F64 d2, d0, d2	@ t * (s1 + s2)
	CMP     a4, #0
	VSUBLT.F64 d0, d0, d2
	poplt	{v1, pc}

	ADR     a3, v4_ArcTrig64_dtanid
	ADD     a3, a3, a4, LSL #4
	VLDMIA  a3, { d6-d7 }
@ Do a doubledouble add of earlier selected constant 'id'
	VSUB.F64 d3, d2, d7
	VSUB.F64 d3, d3, d0
	VSUB.F64 d0, d6, d3
	TST     v1, #1
	VNEGNE.F64 d0, d0	@ Fixup the sign
	pop	{v1, pc}

v4_ArcTrig64_dpi:	.word	0x54442D18, 0x400921FB	@ pi
v4_ArcTrig64_dpir:	.word	0x33145C07, 0x3CA1A626	@ Next 17sf of pi
v4_ArcTrig64_dpiby4:	.word	0x54442D18, 0x3FE921FB	@ pi/4
v4_ArcTrig64_d1point5:	.word	0x00000000, 0x3FF80000	@ 1.5

v4_ArcTrig64_dtanid:	.word	0x0561BB4F, 0x3FDDAC67	@ atan(0.5)
	.word	0x222F65E2, 0x3C7A2B7F	@ Next 17sf of atan(0.5)
	.word	0x54442D18, 0x3FE921FB	@ atan(1.0)
	.word	0x33145C07, 0x3C81A626	@ Next 17sf of atan(1.0)
	.word	0xD281F69B, 0x3FEF730B	@ atan(1.5)
	.word	0x7AF0CBBD, 0x3C700788	@ Next 17sf of atan(1.5)
v4_ArcTrig64_dpiby2:	.word	0x54442D18, 0x3FF921FB	@ atan(INF)
v4_ArcTrig64_dpiby2r:	.word	0x33145C07, 0x3C91A626	@ Next 17sf of atan(INF)

v4_ArcTrig64_dCatanE:	.word	0x5555550D, 0x3FD55555	@  3.33333333333329318027e-01 
	.word	0x920083FF, 0x3FC24924	@  1.42857142725034663711e-01 
	.word	0xC54C206E, 0x3FB745CD	@  9.09088713343650656196e-02 
	.word	0xA0D03D51, 0x3FB10D66	@  6.66107313738753120669e-02 
	.word	0x24760DEB, 0x3FA97B4B	@  4.97687799461593236017e-02 
	.word	0xE322DA11, 0x3F90AD3A	@  1.62858201153657823623e-02 
v4_ArcTrig64_dCatanO:	.word	0x9998EBC4, 0xBFC99999	@ -1.99999999998764832476e-01 
	.word	0xFE231671, 0xBFBC71C6	@ -1.11111104054623557880e-01 
	.word	0xAF749A6D, 0xBFB3B0F2	@ -7.69187620504482999495e-02 
	.word	0x52DEFD9A, 0xBFADDE2D	@ -5.83357013379057348645e-02 
	.word	0x2C6A6C2F, 0xBFA2B444	@ -3.65315727442169155270e-02 

	.ltorg

@ double fp64atan2(double y, double x)
@ Pick off special cases
@ Otherwise, call atan(y/x) then post correct the sign

@ Exceptions per ISO9899:2018 F10.1.4
@       None
@ Based on a translation to AArch32 of
@       openlibm/src/e_atan2.c
@       Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
@       Developed at SunSoft, a Sun Microsystems, Inc. business.
@       Permission to use, copy, modify, and distribute this software is
@       freely granted, provided that this notice is preserved.
v4_fp64atan2:
@ Deal with special cases
	push	{v1, lr}
	VMOV    a1, a2, d0
	VMOV    a3, a4, d1
	LDR     v1, =0x7FF
	BIC     ip, a4, #1<<31
	CMP     ip, v1, LSL #20
	CMPEQ   a3, #0
	pophi	{v1, pc}	@ atan2(NaN,anything) = NaN
	BIC     ip, a4, #1<<31
	CMP     ip, v1, LSL #20
	CMPEQ   a3, #0
	VMOVHI.F64 d0, d1
	pophi	{v1, pc}	@ atan2(anything,NaN) = NaN

@ Now y & x are not NaN
	ORRS    ip, a1, a2, LSL #1
	BNE     10f
	TST     a4, #1<<31
	VABSEQ.F64 d0, d0	@ atan2(±0, +not NaN) = ±0
	VLDRNE  d0, v4_ArcTrig64_dpi	@ atan2(±0, -not NaN) = ±pi
	B       50f
10:
@ Now y & x are not NaN, y is non zero
	ORRS    ip, a3, a4, LSL #1
	BNE     20f
	VLDR    d0, v4_ArcTrig64_dpiby2	@ atan2(±not 0 not NaN, 0) = ±pi/2
	B       50f
20:
@ Now y & x are not NaN, y & x are non zero
	BIC     ip, a2, #1<<31
	CMP     ip, v1, LSL #20
	CMPEQ   a1, #0
	BEQ     30f
	BIC     ip, a4, #1<<31
	CMP     ip, v1, LSL #20
	CMPEQ   a3, #0
	BNE     60f
	TST     a4, #1<<31
	MOVEQ   ip, #0
	VMOVEQ  d0, ip, ip	@ atan2(±not INF not NaN, +INF) = ±0
	VLDRNE  d0, v4_ArcTrig64_dpi	@ atan2(±not INF not NaN, -INF) = ±pi
	B       50f
30:
@ y = INF
	BIC     ip, a4, #1<<31
	CMP     ip, v1, LSL #20
	CMPEQ   a3, #0
	VLDRNE  d0, v4_ArcTrig64_dpiby2
	BNE     50f	@ atan2(±INF, not 0 not NAN not INF) = ±pi/2

@ y & x = INF
	TST     a4, #1<<31
	VLDR    d0, v4_ArcTrig64_dpiby4	@ atan2(±INF, +INF) = ±pi/4
	VLDR    d1, v4_ArcTrig64_dpiby2
	VADDNE.F64 d0, d0, d1	@ atan2(±INF, -INF) = ±3*pi/4
50:
@ Copy sign of y
	TST     a2, #1<<31
	VNEGNE.F64 d0, d0
	pop	{v1, pc}
60:
@ Compute y/x
	mov	ip, a2, lsl #1	@ ExpBits64 ip, a2
	mov	ip, ip, lsr #21
	mov	lr, a4, lsl #1	@ ExpBits64 lr, a4
	mov	lr, lr, lsr #21
	SUB     ip, ip, lr	@ Exponent difference
	MOV     v1, a2, LSR #31
	TST     a4, #1<<31
	ORRNE   v1, v1, #2	@ Remember signs b1 = s-o-x, b0 = s-o-y
	CMP     ip, #60
	VLDRGT  d0, v4_ArcTrig64_dpiby2
	ANDGT   v1, v1, #1	@ |y/x| > 2^60
	BGT     80f
	TST     v1, #2
	BEQ     70f
	CMP     ip, #-60
	MOVLT   ip, #0
	VMOVLT  d0, ip, ip
	BLT     80f	@ |y|/x < -2^-60
70:
@ Safe to do y/x
	VDIV.F64 d0, d0, d1
	VABS.F64 d0, d0
	BL      v4_fp64atan
80:
	CMP     v1, #1
	VNEGEQ.F64 d0, d0	@ atan2(-,+)
	popls	{v1, pc}	@            and atan2(+,+)
	VLDR    d1, v4_ArcTrig64_dpi
	VLDR    d2, v4_ArcTrig64_dpir
	CMP     v1, #3
	VSUB.F64 d2, d0, d2
	VSUBCC.F64 d0, d1, d2	@ atan2(+,-)
	VSUBEQ.F64 d0, d2, d1	@            and atan2(-,-)
	pop	{v1, pc}


	.ltorg				@ objasm's literal pool at the end of the AREA
	.if !((( v4_ArcTrig64_dCatanO) - (v4_ArcTrig64_dCatanE + (3*16) )) == 0)
	.error "ASSERT failed: v4_ArcTrig64_dCatanE + (3*16) = v4_ArcTrig64_dCatanO"
	.endif
