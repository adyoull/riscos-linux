REM A number in a literal pool that reads as a privileged instruction is
REM left alone (NativeSupport 0.47): 256000.0's first word, &410F4000, is
REM MRSMI r4, CPSR, and sits between AL instructions as a compiler puts it.
REM ChartDraw and MultiPlot do exactly this sum (a width in draw units).
REM Also an integer literal that reads as MSR CPSR_c, r0 (&E121F000), and
REM (0.48) two table words that read as CPS with mode bits but no mode
REM change (&F10C0098, &F10C0099: OvationPro has them), beside an AL
REM instruction.
ON ERROR PRINT REPORT$;" at ";ERL : END
DIM code% 512
FOR pass%=0 TO 2 STEP 2
P%=code%
[OPT pass%
.t1
STMFD R13!,{R1,R2,R14}
LDFD F2,[R13],#8
LDFD F3,lit
MUFD F0,F2,F3
FIXZ R0,F0
LDMFD R13!,{PC}
.lit
EQUD &410F4000
EQUD 0
.t2
LDR R0,lit2
MOV PC,R14
.lit2
EQUD &E121F000
MOV R0,R0
.tab
EQUD &F10C0098
EQUD &F10C0099
MOV R0,R0
]
NEXT
SYS "OS_SynchroniseCodeAreas",1,code%,P%
B%=&3FEB3333 : C%=&33333333
a%=USR(t1) : b%=USR(t2)
IF a%=217600 AND b%=&E121F000 AND !lit=&410F4000 AND !tab=&F10C0098 AND tab!4=&F10C0099 THEN PRINT "literal ok" ELSE PRINT "literal: ";a%;" &";~b%;" &";~!lit
