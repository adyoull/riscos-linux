REM >VCTest: checks the VideoCore stand-ins (VCHIQ, BCMSupport, PCI) on a
REM Raspberry Pi 3 or 4 running RISC OS for Linux natively: asks the
REM firmware for the VideoCore's memory, opens VCHIQ's "mmal" service (the
REM one ReelHWAccel's vcdec and vcenc use), asks MMAL its version, and
REM sends a block of PCI memory through a bulk transfer.
REM   *BASIC -quit VCTest
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "Failed: ";REPORT$;" (";ERL;")":PROCclose:END
inst%=0:h%=0:mem%=0
OSCLI("Obey "+FNdir+".LoadVC")
DIM b% 64
!b%=32:b%!4=0:b%!8=&10006:b%!12=8:b%!16=0:b%!20=0:b%!24=0:b%!28=0
SYS "BCMSupport_SendTempPropertyBuffer",b%,b%
PRINT "1. Firmware (BCMSupport, /dev/vcio): reply &";~b%!4;", the VideoCore has ";b%!24>>20;" MB (gpu_mem)"
IF b%!4<>&80000000 THEN ERROR 1,"the firmware didn't answer"
SYS "PCI_RAMAlloc",65536,4096 TO mem%
PRINT "2. PCI_RAMAlloc: 64K at &";~mem%
SYS "VCHIQ_Initialise" TO inst%
SYS "VCHIQ_Connect",0,0,inst%
PRINT "3. VCHIQ (/dev/vchiq): connected, instance &";~inst%
DIM s% 44
FOR i%=0 TO 40 STEP 4:s%!i%=0:NEXT
s%!0=15:s%!4=10:s%!8=&6D6D616C
SYS "VCHIQ_ServiceOpen",inst%,s% TO h%
PRINT "4. MMAL service open: handle &";~h%
DIM m% 512
FOR i%=0 TO 20 STEP 4:m%!i%=0:NEXT
!m%=&6C616D6D:m%!4=3:m%!12=1
SYS "VCHIQ_MsgQueue",h%,m%,24
SYS "OS_ReadMonotonicTime" TO t0%
REPEAT
  SYS "VCHIQ_MsgDequeue",h%,m%,512 TO ,,n%
  SYS "OS_ReadMonotonicTime" TO t%
UNTIL n%>0 OR t%-t0%>200
IF n%=0 THEN ERROR 1,"no reply from MMAL in 2 seconds"
PRINT "5. MMAL's reply: ";n%;" bytes, type ";m%!4;", status ";m%!16;
IF n%>=40 THEN PRINT ", version ";m%!28;".";m%!32 ELSE PRINT
PROCclose
PRINT "All OK: ReelHWAccel's VideoCore decoding should work (try Reel)."
END

DEF PROCclose
IF h%<>0 THEN SYS "XVCHIQ_ServiceClose",h%:h%=0
IF inst%<>0 THEN SYS "XVCHIQ_Disconnect",inst%:inst%=0
IF mem%<>0 THEN SYS "XPCI_RAMFree",mem%:mem%=0
ENDPROC

DEF FNdir
LOCAL d$,l%
SYS "XOS_ReadVarVal","LinuxCodec$Dir",STRING$(255," "),255,0,3 TO ,d$,l%
IF l%>0 THEN =LEFT$(d$,l%)
="<IXFS$HardDisc4>.TestBed.Codec"
