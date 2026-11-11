REM fpa: FPA instructions (FPEmulator) checked and timed.
REM Each pass runs loads, arithmetic, conversions, compares, conditional
REM instructions that pass and fail, transfers and multiple loads/stores
REM on a table of values, and folds every result into a checksum. The
REM checksum must be the same with NativeSupport's FastFPA on or off (and
REM natively, and on real FPEmulator hardware). Then a timed loop.
REM Prints "fpa sum &XXXXXXXX" and "fpa time N cs for M ops".
REM Copyright 2026 Andrew Youll. MIT licence.
ON ERROR PRINT "fpa error ";REPORT$;" at ";ERL:END
DIM code% 2048, vals% 8*16, out% 64, save% 64
REM 1.5,-2.25,3E10,-7E-12,0.1,100,-0.5,12345.678,2,-3,1E-300,1E300,
REM 0.3333333,-1E5,7,0.0001 as FPA doubles (high word first)
FOR i%=0 TO 31
  READ w%:vals%!(4*i%)=w%
NEXT
DATA &3FF80000,&00000000,&C0020000,&00000000,&421BF08E,&B0000000,&BD9EC94C,&A210599E
DATA &3FB99999,&9999999A,&40590000,&00000000,&BFE00000,&00000000,&40C81CD6,&C8B43958
DATA &40000000,&00000000,&C0080000,&00000000,&01A56E1F,&C2F8F359,&7E37E43C,&8800759C
DATA &3FD55555,&318ABC87,&C0F86A00,&00000000,&401C0000,&00000000,&3F1A36E2,&EB1C432D
FOR pass%=0 TO 2 STEP 2
P%=code%
[OPT pass%
.check
 STMFD R13!,{R4-R11,R14}
 ADR R11,save_ptr
 LDR R11,[R11]
 SFM F4,4,[R11]
 MOV R10,#0              ; checksum
 RFS R9
 STMFD R13!,{R9}
 MOV R9,#0
 WFS R9                  ; round to nearest, no traps
 LDR R4,vals_ptr
 MOV R5,#0
.outer
 ADD R6,R4,R5,LSL #3
 LDFD F0,[R6]
 AND R7,R5,#15
 EOR R7,R7,#5
 ADD R7,R4,R7,LSL #3
 LDFD F1,[R7]
 ADFD F2,F0,F1
 SUFD F3,F0,F1
 MUFD F4,F0,F1
 DVFD F5,F0,F1
 RDFD F6,F0,F1
 BL fold2
 MVFD F2,F3
 BL fold2
 MVFD F2,F4
 BL fold2
 MVFD F2,F5
 BL fold2
 MVFD F2,F6
 BL fold2
 ABSD F2,F0
 SQTD F2,F2
 BL fold2
 MNFD F2,F1
 BL fold2
 FLTD F3,R5
 MUFD F2,F3,#0.5
 ADFD F2,F2,#10
 BL fold2
 FIXZ R8,F4              ; truncate (big values saturate)
 ADD R10,R10,R8
 FIX R8,F2               ; round to nearest
 EOR R10,R8,R10,ROR #3
 CMF F0,F1
 MVFGTD F2,F0            ; condition pass/fail both ways
 MVFLED F2,F1
 ADFVSD F2,F2,#1         ; V clear unless unordered, so skipped
 BL fold2
 CMFE F1,F0
 MOVMI R8,#1
 MOVPL R8,#2
 ADD R10,R10,R8
 MUFS F2,F0,F1           ; single precision rounding
 BL fold2
 MUFE F2,F0,F1           ; extended
 STFE F2,[R11,#48]
 LDR R8,[R11,#48]
 ADD R10,R10,R8
 LDR R8,[R11,#52]
 EOR R10,R10,R8
 ADD R5,R5,#1
 CMP R5,#16
 BLT outer
 RFS R8                  ; exception flags (inexact etc.)
 EOR R10,R10,R8,LSL #16
 LDMFD R13!,{R9}
 WFS R9
 LFM F4,4,[R11]
 MOV R0,R10
 LDMFD R13!,{R4-R11,PC}
.fold2                   ; R10 = R10 rotate + the two words of F2 (double)
 STFD F2,[R11,#56]
 LDR R8,[R11,#56]
 ADD R10,R8,R10,ROR #7
 LDR R8,[R11,#60]
 EOR R10,R10,R8
 MOV PC,R14
.time
 ; R0 = iterations, 10 FPA instructions each
 STMFD R13!,{R14}
 RFS R2
 STMFD R13!,{R2}
 MOV R2,#0
 WFS R2
 MVFD F0,#1
 MVFD F1,#0.5
.tloop
 ADFD F2,F0,F1
 MUFD F2,F2,F1
 SUFD F2,F2,#1
 ADFD F0,F0,F2
 CMF F0,#10
 MVFGTD F0,#1
 CNF F0,#10
 MVFLTD F0,#1
 DVFD F3,F0,#2
 FIX R1,F3
 SUBS R0,R0,#1
 BNE tloop
 RFS R1
 LDMFD R13!,{R2}
 WFS R2
 MOV R0,R1
 LDMFD R13!,{PC}
.vals_ptr EQUD vals%
.save_ptr EQUD save%
]
NEXT
s%=USR(check)
s2%=USR(check)
IF s%<>s2% THEN PRINT "fpa error: two runs differ ";~s%;" ";~s2%:END
PRINT "fpa sum &";RIGHT$("0000000"+STR$~s%,8)
n%=50000
t%=TIME:A%=n%:f%=USR(time):t%=TIME-t%
PRINT "fpa time ";t%;" cs for ";n%*10;" ops, fpsr &";~f%
END
