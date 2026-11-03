REM priv: the other privileged instructions code from disc uses (as
REM HardDisc4's SharedCLibrary 6.22 does to resume a program): MSR/MRS
REM SPSR, STM {r13}^ (USR's r13 from SVC mode), LDM {pc}^ and MOVS pc, lr
REM (exception return to USR mode). Prints the low bytes of what it saw.
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
DIM code% 512, res% 64, blk% 16
FOR pass%=0 TO 2 STEP 2
P%=code%
[OPT pass%
 STMFD R13!,{R4-R12,R14}
 MOV R9,R0
 MOV R10,R13
 SWI "OS_EnterOS"
 MOV R1,#&10
 MSR SPSR_cxsf,R1
 MRS R4,SPSR
 STR R4,[R9,#0]
 ADD R2,R9,#40
 STMIA R2,{R13}^
 ADR R1,back1
 LDR R2,blkadr
 STR R1,[R2]
 LDMIA R2,{PC}^
.back1
 MRS R5,CPSR
 STR R5,[R9,#4]
 STR R13,[R9,#8]
 SWI "OS_EnterOS"
 MOV R1,#&10
 MSR SPSR_c,R1
 ADR R14,back2
 MOVS PC,R14
.back2
 MRS R6,CPSR
 STR R6,[R9,#12]
 STR R13,[R9,#16]
 SWI "OS_ReadMonotonicTime"
 LDMFD R13!,{R4-R12,PC}
.blkadr EQUD blk%
]
NEXT
A%=res%
CALL code%
R%=res%!40
PRINT "spsr &";~res%!0 AND &FF;" ldm^ &";~res%!4 AND &FF;" movs &";~res%!12 AND &FF;" usr-sp ";-(res%!8=res%!40 AND res%!16=res%!40)
