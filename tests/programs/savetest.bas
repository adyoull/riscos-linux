REM savetest: a program's patched instructions written to a file come out as they were
DIM c% 256, d% 64, e% 64
P%=c%
[ OPT 0
SWI "OS_EnterOS"
MSR CPSR_c,#&10
MRS R2,CPSR
MOV R1,R14
MOV R14,#&55
SWI "OS_ReadMonotonicTime"
MOV R0,R14
MOV R14,R1
MOV PC,R14
]
SYS "OS_SynchroniseCodeAreas",1,c%,c%+60
IF USR(c%)<>&55 THEN PRINT "savetest: code doesn't run (wrong)":END
m%=c%!4: PRINT "savetest: in memory &";~m%;" &";~c%!8
FOR i%=0 TO 28 STEP 4: d%!i%=c%!i%: NEXT
f$="<Wimp$ScrapDir>.svt"
SYS "OS_File",10,f$,&FFD,,d%,d%+32
SYS "OS_File",255,f$,e%,0
IF e%!4=&E321F010 AND e%!8=&E10F2000 THEN PRINT "savetest: copy, OS_File: right" ELSE PRINT "savetest: copy, OS_File: &";~e%!4;" &";~e%!8;" (wrong)"
SYS "OS_File",10,f$,&FFD,,c%,c%+32
SYS "OS_File",255,f$,e%,0
IF e%!4=&E321F010 AND e%!8=&E10F2000 THEN PRINT "savetest: code itself, OS_File: right" ELSE PRINT "savetest: code itself, OS_File: &";~e%!4;" &";~e%!8;" (wrong)"
IF c%!4=m% THEN PRINT "savetest: still patched in memory: right" ELSE PRINT "savetest: memory now &";~c%!4;" (wrong)"
FOR i%=0 TO 28 STEP 4: d%!i%=c%!i%: NEXT
h%=OPENOUT f$: SYS "OS_GBPB",2,h%,d%,32: CLOSE#h%
SYS "OS_File",255,f$,e%,0
IF e%!4=&E321F010 AND e%!8=&E10F2000 THEN PRINT "savetest: copy, OS_GBPB: right" ELSE PRINT "savetest: copy, OS_GBPB: &";~e%!4;" &";~e%!8;" (wrong)"
IF USR(c%)=&55 THEN PRINT "savetest: code still runs: right" ELSE PRINT "savetest: code no longer runs (wrong)"
