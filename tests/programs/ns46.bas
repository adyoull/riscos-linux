REM NativeSupport 0.46 (natively: QEMU system or a real machine), by hand:
REM 1. a copy of patched code written to a file in pieces has no UDF
REM    markers left (0.45 left those at a piece's first or last word);
REM 2. MRS SPSR in user mode: the error shows the instruction, the site
REM    comes back, and SWIs aren't slower while it is put back;
REM 3. LDM ^ on a bad address in SVC mode is a data abort (0.45:
REM    "undefined instruction").
REM Needs the LinuxAudio module (any RAM module with sites would do).
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
SYS "OS_Module",18,"LinuxAudio" TO ,,,b%
l%=b%!-4-4
m%=0:FOR a%=b% TO b%+l%-4 STEP 4:IF (!a% AND &FFF000F0)=&E7F000F0 THEN m%+=1
NEXT:PRINT "LinuxAudio &";~b%;" ";l%;" bytes, markers in memory: ";m%
DIM copy% l%+16, back% l%+16
FOR a%=0 TO l%-4 STEP 4:copy%!a%=b%!a%:NEXT
f$="IXFS:$.riscos.HardDisc4.NS46Out"
SYS "OS_File",10,f$,&FFD,,copy%,copy%+l%
PRINT "copy saved whole: markers in the file ";FNcount
FOR c%=4 TO 20 STEP 4
  h%=OPENOUT(f$)
  FOR a%=0 TO l%-1 STEP c%
    n%=c%:IF a%+n%>l% THEN n%=l%-a%
    SYS "OS_GBPB",2,h%,copy%+a%,n%
  NEXT
  CLOSE#h%
  PRINT "copy saved in pieces of ";c%;": markers in the file ";FNcount
NEXT
OSCLI "Delete "+f$
DIM c% 64
P%=c%:[OPT 2:MRS R0,SPSR:MOV PC,R14:]
SYS "OS_SynchroniseCodeAreas",1,c%,c%+8
PRINT "MRS SPSR site &";~!c%
ON ERROR LOCAL PRINT "  error: ";REPORT$:GOTO 1000
CALL c%
1000 ON ERROR OFF
PRINT "  word after the error &";~!c%;" (the instruction: put back)"
T%=TIME:n%=0
REPEAT:SYS "OS_ReadMonotonicTime":n%+=1:UNTIL TIME-T%>200
PRINT "  ";n%;" SWIs in 2 s while it is put back"
T%=TIME:REPEAT:UNTIL TIME-T%>950
FOR i%=1 TO 600:SYS "OS_ReadMonotonicTime":NEXT
PRINT "  word 11.5 s later &";~!c%;" (the site again)"
T%=TIME:n%=0
REPEAT:SYS "OS_ReadMonotonicTime":n%+=1:UNTIL TIME-T%>200
PRINT "  ";n%;" SWIs in 2 s afterwards"
DIM d% 64
P%=d%:[OPT 2:SWI "OS_EnterOS":MOV R0,#&1F000000:LDMIA R0,{R1,R2}^:MOV R0,R0:TEQP PC,#0:MOV R0,R0:MOV PC,R14:]
SYS "OS_SynchroniseCodeAreas",1,d%,d%+28
PRINT "LDM ^ site &";~d%!8
ON ERROR LOCAL PRINT "  error: ";REPORT$:GOTO 2000
CALL d%
2000 ON ERROR OFF
PRINT "  word after the error &";~d%!8;" (0.46: a data abort, above)"
PRINT "done"
END
DEF FNcount
LOCAL t%,n%,j%,k%
SYS "OS_File",17,f$ TO t%,,,,n%
SYS "OS_File",16,f$,back%,,0
k%=0
FOR j%=0 TO n%-4 STEP 4:IF (back%!j% AND &FFF000F0)=&E7F000F0 THEN k%+=1
NEXT
=k%
