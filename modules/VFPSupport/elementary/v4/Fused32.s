@ Fused32: from RISC OS Open's VFPSupport s/Fused32, translated to GNU as
@ by objasm2gas.py (riscos-linux-testbed), v4 variant
@ (VFPv4: VFMA). The original's
@ copyright and licence follow; they apply to this file.
@
@ Copyright (c) 2005-2011 David Schultz <das@FreeBSD.ORG>
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


@ EXPORT fused32_muladd

	.text
	.arm


@ float fused32_muladd(float x, float y, float z)
@ Compute z + (x * y) with a single rounding error.
v4_fused32_muladd:
	push	{v1, lr}

	VMOV    a1, s0
	VMOV    a2, s1
	VMOV    a4, s2
	LDR     ip, =0xFF<<23	@ INF
	MOV     v1, #0	@ Nothing exceptional
	BIC     a1, a1, #1<<31
	BIC     a2, a2, #1<<31
	BIC     a3, a4, #1<<31

@ Handle special cases
	CMP     a1, ip
	CMPEQ   a2, #0
	BEQ     10f
	CMP     a2, ip
	CMPEQ   a1, #0
	BNE     20f
10:
@ x = ±INF and y = ±0, or
@ x = ±0   and y = ±INF
	CMP     a3, ip
	MOVLS   v1, #FPSCR_IOC	@ z is not a NaN
	B       30f
20:
	CMP     a3, ip
	BNE     30f	@ z is not ±INF
	VMUL.F32 s7, s0, s1
	VMOV    a1, s7
	EOR     a2, a1, a4
	TEQ     a2, #1<<31
	MOVEQ   v1, #FPSCR_IOC	@ x * y is INF and z is INF, but of opposite sign
30:
@ A double has more than twice as much precision than a float, so
@ direct double-precision arithmetic suffices, except where double
@ rounding occurs.
	VCVT.F64.F32 d2, s2	@ z
	VCVT.F64.F32 d1, s1	@ y
	VCVT.F64.F32 d0, s0	@ x
@ xy = (double)x * y;
@ result = xy + z;
@ EXTRACT_WORDS(hi, lo, result);
	VMUL.F64 d0, d0, d1
	VADD.F64 d3, d0, d2
	VMOV    r2, r3, d3

@ Common case: The double precision result is fine.
@ if ((lo & 0x1fffffff) != 0x10000000 ||
@     (hi & 0x7ff00000) == 0x7ff00000 ||
@     result - xy == z ||
@     fegetround() != FE_TONEAREST)
@     return (result);
	BIC     a1, r2, #0xE0000000
	TEQ     a1, #0x10000000	@ Not a halfway case
	BNE     40f

	MOV     a1, r3, LSL #1
	MOV     a1, a1, ASR #21
	CMP     a1, #-1	@ NaN or INF
	BEQ     40f

	VSUB.F64 d4, d3, d0
	VCMP.F64 d4, d2	@ Exact?
	VMRS    APSR_nzcv, FPSCR
	BEQ     40f

	VMRS    a1, FPSCR
	TST     a1, #FPSCR_RMODE_MASK	@ Non standard rounding
	BNE     40f

@ If result is inexact, and exactly halfway between two float values,
@ we need to adjust the low-order bit in the direction of the error.
	ORR     a2, a1, #FPSCR_RMODE_ZERO
@ fesetround(FE_TOWARDZERO);
@ double adjresult = xy + z;
@ fesetround(FE_TONEAREST);
	VMSR    FPSCR, a2
	VADD.F64 d4, d0, d2
	VMSR    FPSCR, a1
@ if (result == adjusted_result)
@     SET_LOW_WORD(adjusted_result, lo + 1);
	VCMP.F64 d3, d4
	VMRS    APSR_nzcv, FPSCR
	ADDEQ   r2, r2, #1
	VMOVEQ  d4, r2, r3
@ return (adjusted_result);
	VMOV.F64 d3, d4
40:
	VMRS    a1, FPSCR
	BIC     a1, a1, #FPSCR_CUMULATIVE_FLAGS
	VMSR    FPSCR, a1
	VCVT.F32.F64 s0, d3
	VMRS    a1, FPSCR	@ Check on over/underflow situation
	AND     a1, a1, #FPSCR_OFC|FPSCR_UFC
	ORRS    a1, a1, v1	@ Combine with invalid exceptions
	BLNE    v4_RaiseException
	pop	{v1, pc}


	.ltorg				@ objasm's literal pool at the end of the AREA
	.if !((( 0) - (FPSCR_RMODE_NEAREST )) == 0)
	.error "ASSERT failed: FPSCR_RMODE_NEAREST = 0"
	.endif
	.if !((( FPSCR_RMODE_MASK) - (FPSCR_RMODE_ZERO )) == 0)
	.error "ASSERT failed: FPSCR_RMODE_ZERO = FPSCR_RMODE_MASK"
	.endif
