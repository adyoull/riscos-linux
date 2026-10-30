REM >CodecTest: decodes an H.264 file (Annex B) with LinuxCodec and
REM reports the pictures, their size, the time taken and a checksum of the
REM first picture's Y plane (to compare with a known decoder's).
REM   *BASIC -quit CodecTest <file>      (default: Test720/h264 beside it)
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT REPORT$;" (";ERL;")":PROCclose:END
SYS "OS_GetEnv" TO env$
file$=FNlastword(env$)
IF file$="" OR INSTR(file$,"CodecTest")>0 THEN file$=FNdir+".Test720/h264"
RMEnsure_ok%=0
SYS "XOS_Module",18,"LinuxCodec" TO ;f%
IF f% AND 1 THEN OSCLI("RMLoad "+FNdir+".LinuxCodec")
chunk%=65536
DIM buf% chunk%
h%=0:fh%=0
SYS "LinuxCodec_Open",0,0 TO h%
fh%=OPENIN(file$)
IF fh%=0 THEN ERROR 1,"Can't open "+file$
total%=EXT#fh%
PRINT "Decoding ";file$;" (";total%;" bytes) with LinuxCodec"
SYS "OS_ReadMonotonicTime" TO t0%
pics%=0:sent%=0:first%=TRUE:w%=0:ht%=0:sum%=0:done%=FALSE
REPEAT
  n%=total%-sent%:IF n%>chunk% THEN n%=chunk%
  IF n%>0 THEN
    PTR#fh%=sent%
    SYS "OS_GBPB",4,fh%,buf%,n%
    off%=0
    REPEAT
      SYS "LinuxCodec_Decode",h%,buf%+off%,n%-off%,sent%+off%,0 TO ,,took%
      off%+=took%
      PROCpictures
      IF took%=0 THEN PROCwait
    UNTIL off%>=n%
    sent%+=n%
  ELSE
    SYS "LinuxCodec_Decode",h%,0,0,0,1
    t1%=0:SYS "OS_ReadMonotonicTime" TO tw%
    REPEAT
      PROCpictures:PROCwait
      SYS "LinuxCodec_Info",h% TO ,,,state%
      SYS "OS_ReadMonotonicTime" TO t1%
    UNTIL state%=2 OR t1%-tw%>500
    done%=TRUE
  ENDIF
UNTIL done%
SYS "OS_ReadMonotonicTime" TO t2%
cs%=t2%-t0%:IF cs%<1 THEN cs%=1
PRINT pics%;" pictures, ";w%;"x";ht%;", in ";cs%/100;" s: ";pics%*100/cs%;" pictures a second"
PRINT "First picture's Y checksum: ";sum%;" (test720/h264 from ffmpeg: 116111199; 250 pictures)"
PROCclose
END

DEF PROCpictures
LOCAL p%,i%,y%,ys%,x%,r%
REPEAT
  SYS "LinuxCodec_GetFrame",h% TO p%
  IF p%<>0 THEN
    i%=!p%:w%=p%!4:ht%=p%!8:ys%=p%!12:y%=p%!16
    IF first% THEN
      sum%=0
      FOR r%=0 TO ht%-1
        FOR x%=0 TO w%-1 STEP 4
          v%=y%!(r%*ys%+x%)
          sum%+=(v% AND &FF)+((v%>>>8) AND &FF)+((v%>>>16) AND &FF)+(v%>>>24)
        NEXT
      NEXT
      first%=FALSE
    ENDIF
    pics%+=1
    SYS "LinuxCodec_ReleaseFrame",h%,i%
  ENDIF
UNTIL p%=0
ENDPROC

DEF PROCwait
LOCAL t%,u%
SYS "OS_ReadMonotonicTime" TO t%
REPEAT SYS "OS_ReadMonotonicTime" TO u%:UNTIL u%<>t%
ENDPROC

DEF PROCclose
IF fh%<>0 THEN CLOSE#fh%:fh%=0
IF h%<>0 THEN SYS "XLinuxCodec_Close",h%:h%=0
ENDPROC

DEF FNlastword(a$)
LOCAL i%
WHILE RIGHT$(a$,1)=" ":a$=LEFT$(a$,LEN(a$)-1):ENDWHILE
i%=LEN(a$)
WHILE i%>0 AND MID$(a$,i%,1)<>" ":i%-=1:ENDWHILE
=MID$(a$,i%+1)

DEF FNdir
LOCAL d$
SYS "XOS_ReadVarVal","LinuxCodec$Dir",STRING$(255," "),255,0,3 TO ,d$,l%
IF l%>0 THEN =LEFT$(d$,l%)
="<IXFS$HardDisc4>.TestBed.Codec"
