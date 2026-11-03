@ CMath: from RISC OS Open's VFPSupport s/CMath, translated to GNU as
@ by objasm2gas.py (riscos-linux-testbed), v4 variant
@ (VFPv4: VFMA). The original's
@ copyright and licence follow; they apply to this file.
@ Copyright 2021 RISC OS Open Limited
@ Copyright 2026 Andrew Youll
@
@ Licensed under the Apache License, Version 2.0 (the "License");
@ you may not use this file except in compliance with the License.
@ You may obtain a copy of the License at
@
@     http://www.apache.org/licenses/LICENSE-2.0
@
@ Unless required by applicable law or agreed to in writing, software
@ distributed under the License is distributed on an "AS IS" BASIS,
@ WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@ See the License for the specific language governing permissions and
@ limitations under the License.
@

@ Maths functions from <math.h> in the SharedCLibrary - translated by hand
@ to stand alone in VFPSupport

	.equ	v4_CMath_INT_MAX, 0x7FFFFFFF
	.equ	v4_CMath_INT_MIN, 0x80000000
	.equ	v4_CMath_FP_ILOGB0, -v4_CMath_INT_MAX
	.equ	v4_CMath_FP_ILOGBNAN, v4_CMath_INT_MIN

@ EXPORT NextAfter
@ EXPORT FRExp
@ EXPORT LDExp
@ EXPORT ILogB


@ double nextafter(double a, double b)
@ Return the next machine value after a in the direction of b
v4_NextAfter:
	push	{v1, lr}
	LDR     v1, =0x7FF
	VMOV    a1, a2, d0
	mov	ip, a2, lsl #1	@ ExpBits64 ip, a2
	mov	ip, ip, lsr #21
	TEQ     ip, v1
	BNE     10f
	ORRS    ip, a1, a2, LSL #1+11
	BNE     20f
10:
	VMOV    a3, a4, d1
	mov	ip, a4, lsl #1	@ ExpBits64 ip, a4
	mov	ip, ip, lsr #21
	TEQ     ip, v1
	BNE     30f
	ORRS    ip, a3, a4, LSL #1+11
	BEQ     30f
20:
	VADD.F64 d0, d0, d1	@ a or b were NaN
	pop	{v1, pc}
30:
	VCMP.F64 d0, d1
	VMRS    APSR_nzcv, FPSCR
	VMOVEQ.F64 d0, d1	@ a == b
	popeq	{v1, pc}

	ORRS    ip, a1, a2, LSL #1
	BNE     40f
	AND     a4, a4, #1<<31	@ a == 0
	MOV     a3, #1
	VMOV    d0, a3, a4	@ ulp with sign of b
	MOV     a1, #FPSCR_UFC
	BL      v4_RaiseException
	pop	{v1, pc}
40:
	TST     a2, #1<<31
	BNE     50f
	VMRS    APSR_nzcv, FPSCR	@ a > 0
	BGT     v4_CMath_ulpm	@       a > b
	B       v4_CMath_ulpp	@       a < b
50:
	VMRS    APSR_nzcv, FPSCR	@ a < 0
	BGT     v4_CMath_ulpp	@       a > b
v4_CMath_ulpm:	@       a < b
	SUBS    a1, a1, #1
	SBC     a2, a2, #0	@ -1 ulp
	B       60f
v4_CMath_ulpp:
	ADDS    a1, a1, #1
	ADC     a2, a2, #0	@ +1 ulp
60:
	VMOV    d0, a1, a2
	mov	ip, a2, lsl #1	@ ExpBits64 ip, a2
	mov	ip, ip, lsr #21
	CMP     ip, v1	@ overflowed?
	MOVCS   a1, #FPSCR_OFC
	BCS     90f
	CMP     ip, #1<<20	@ underflowed?
	popcs	{v1, pc}	@ normal
	MOV     a1, #FPSCR_UFC
90:
	BL      v4_RaiseException
	pop	{v1, pc}

@ double frexp(double a, int *e)
@ Split a double into exponent and mantissa
v4_FRExp:
	push	{v1, lr}
	MOV     v1, a1
	VMOV    a1, a2, d0
	mov	ip, a2, lsl #1	@ ExpBits64 ip, a2
	mov	ip, ip, lsr #21
	LDR     a3, =0x7FF
@ ip = exponent, a3 = &7FF
	TEQ     ip, a3	@ NaN or ±INF
	BEQ     30f
	LDR     a3, =DBL_EXP_BIAS - 1
	TEQ     ip, #0	@ 0.0 or subnormal
	BEQ     10f
	SUB     a4, ip, a3	@ Normal
	B       40f
10:
@ exponent == 0
	ORRS    a4, a1, a2, LSL #1
	MOVEQ   a4, #0
	STREQ   a4, [v1]	@ 0.0
	popeq	{v1, pc}
	SUB     ip, ip, a3
	ADD     ip, ip, #1
	AND     lr, a2, #1<<31	@ Keep the sign
20:
	TST     a2, #1<<19	@ Top bit of fractional part
	MOV     a4, a1, LSR #31
	ORR     a2, a4, a2, LSL #1
	MOV     a1, a1, LSL #1
	SUB     ip, ip, #1
	BEQ     20b
	BIC     a2, a2, #1<<31
	ORR     a2, a2, lr	@ Put sign back
	MOV     a4, ip
	B       40f
30:
@ exponent == &7FF
	ORRS    a4, a1, a2, LSL #1+11
	LDRNE   a4, =v4_CMath_FP_ILOGBNAN
	LDREQ   a4, =v4_CMath_INT_MAX
	STR     a4, [v1]
	pop	{v1, pc}
40:
	STR     a4, [v1]
	BIC     a2, a2, #0x70000000
	BIC     a2, a2, #0x0FF00000
	ORR     a2, a2, a3, LSL #20
	VMOV    d0, a1, a2
	pop	{v1, pc}

@ double ldexp(double a, int e)
@ Load the exponent to a double
v4_LDExp:
	push	{v1, lr}
	MOVS    v1, a1
	popeq	{v1, pc}	@ n = 0
	VMOV    a1, a2, d0
	mov	ip, a2, lsl #1	@ ExpBits64 ip, a2
	mov	ip, ip, lsr #21
	LDR     a3, =0x7FF
@ ip = exponent, a3 = &7FF
	TEQ     ip, a3	@ NaN or ±INF
	popeq	{v1, pc}
	TEQ     ip, #0	@ 0.0 or subnormal
	BNE     20f

@ Starting subnormal or zero
	ORRS    a4, a1, a2, LSL #1
	popeq	{v1, pc}	@ return a
	ADD     ip, ip, #1
	AND     lr, a2, #1<<31	@ Keep the sign
10:
	TST     a2, #1<<19	@ Top bit of fractional part
	MOV     a4, a1, LSR #31
	ORR     a2, a4, a2, LSL #1
	MOV     a1, a1, LSL #1
	SUB     ip, ip, #1
	BEQ     10b
	BIC     a2, a2, #1<<31
	ORR     a2, a2, lr	@ Put sign back
20:
	ADD     a4, ip, v1
	CMP     v1, #0x1000
	BGT     30f
	CMP     a4, a3
	BLT     40f
30:
@ Overflowed
	MOV     v1, a2
	MOV     a1, #FPSCR_OFC | FPSCR_IXC
	BL      v4_RaiseException
	TST     v1, #1<<31
	VLDR    d0, v4_CMath_dblinf	@ Return ±INF on overflow
	VNEGNE.F64 d0, d0
	pop	{v1, pc}

	.ltorg

v4_CMath_dblinf:	.word	0x00000000, 0x7FF00000
v4_CMath_hugeval:	.word	0xFFFFFFFF, 0x7FEFFFFF
v4_CMath_twom53:	.word	0x00000000, 0x3CA00000

40:
	CMP     v1, #-0x1000
	BLT     50f
	CMP     a4, #-DBL_MANT_DIG
	BGT     60f
50:
@ Total underflow
	MOV     v1, a2
	MOV     a1, #FPSCR_UFC | FPSCR_IXC
	BL      v4_RaiseException
	AND     a2, v1, #1<<31
	MOV     a1, #0
	VMOV    d0, a1, a2	@ Return ±0 on underflow
	pop	{v1, pc}
60:
	CMP     a4, #0
	ADDLE   a4, a4, #DBL_MANT_DIG
	BIC     a2, a2, #0x70000000
	BIC     a2, a2, #0x0FF00000
	ORR     a2, a2, a4, LSL #20
	VMOV    d0, a1, a2
	popgt	{v1, pc}
70:
@ Subnormal result
	VLDR    d1, v4_CMath_twom53
	VMUL.F64 d0, d0, d1	@ Scaled up and rounded to nearest
	VCMP.F64 d0,#0.0
	VMRS    APSR_nzcv, FPSCR
	MOVEQ   a1, #FPSCR_UFC | FPSCR_IXC
	BLEQ    v4_RaiseException
	pop	{v1, pc}

@ int ilogb(double a)
@ Return the unbiased exponent of a double
v4_ILogB:
	VMOV    a1, a2, d0
	mov	ip, a2, lsl #1	@ ExpBits64 ip, a2
	mov	ip, ip, lsr #21
	LDR     a3, =0x7FF
@ ip = exponent, a3 = &7FF
	TEQ     ip, a3	@ NaN or ±INF
	BEQ     30f
	TEQ     ip, #0	@ 0.0 or subnormal
	BEQ     10f
	SUB     a1, ip, a3, LSR #1	@ Normal
	BX      lr
10:
@ exponent == 0
	ORRS    a4, a1, a2, LSL #1+11
	LDREQ   a1, =v4_CMath_FP_ILOGB0	@ 0.0
	BEQ     40f
	MOV     ip, #1
20:
	TST     a2, #1<<19	@ Top bit of fractional part
	MOV     a4, a1, LSR #31
	ORR     a2, a4, a2, LSL #1
	MOV     a1, a1, LSL #1
	SUB     ip, ip, #1
	BEQ     20b
	SUB     a1, ip, a3, LSR #1
	BX      lr
30:
@ exponent == &7FF
	MOV     a2, a2, LSL #1+11
	ORRS    a4, a2, a1
	LDRNE   a1, =v4_CMath_FP_ILOGBNAN
	LDREQ   a1, =v4_CMath_INT_MAX
40:
	push	{a1, lr}
	MOV     a1, #FPSCR_IOC
	BL      v4_RaiseException
	pop	{a1, pc}


	.ltorg				@ objasm's literal pool at the end of the AREA
