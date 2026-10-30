REM psr: privileged PSR instructions in code loaded from disc (here,
REM BASIC's assembler in application space), as modules and programs
REM built for real machines use them: MRS, MSR CPSR_c and CPSID/CPSIE
REM must see and change the port's virtual CPU mode and interrupt mask,
REM and a SWI after switching back to USR mode must leave USR's r14 (the
REM return address) alone. Prints the low byte of the CPSR at each step.
REM Last, MRS with sp not a stack (code does that): natively it must not
REM touch memory.
DIM code% 256, res% 32
FOR pass%=0 TO 2 STEP 2
P%=code%
[OPT pass%
 STMFD R13!,{R4-R9,R14}
 MOV R9,R0
 MRS R4,CPSR
 SWI "OS_EnterOS"
 MRS R5,CPSR
 EQUD &F10C0080        ; CPSID i
 MRS R6,CPSR
 EQUD &F1080080        ; CPSIE i
 MRS R7,CPSR
 MSR CPSR_c,#&10
 MRS R8,CPSR
 SWI "OS_ReadMonotonicTime"
 STMIA R9,{R4-R8}
 MOV R1,R13
 MOV R13,#&80000000
 MRS R2,CPSR
 MOV R13,R1
 STR R2,[R9,#20]
 LDMFD R13!,{R4-R9,PC}
]
NEXT
A%=res%
CALL code%
PRINT "usr &";~res%!0 AND &FF;" svc &";~res%!4 AND &FF;" cpsid &";~res%!8 AND &FF;" cpsie &";~res%!12 AND &FF;" usr &";~res%!16 AND &FF;" nosp &";~res%!20 AND &FF
