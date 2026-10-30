REM >OverlayTest: plays an H.264 file (Annex B) on the video overlay:
REM decoded by Linux (VideoCore on a Pi 3/4) and shown by the display
REM hardware, scaled to a rectangle on the screen, at 25 pictures a second.
REM   *BASIC -quit OverlayTest [<file>]    (default: Test720/h264 beside it)
REM The rectangle is filled with the overlay's key colour: the video shows
REM through it, under anything drawn on top (try dragging a window over).
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT REPORT$;" (";ERL;")":PROCclose:END
SYS "OS_GetEnv" TO env$
file$=FNlastword(env$)
IF file$="" OR INSTR(file$,"OverlayTest")>0 THEN file$=FNdir+".Test720/h264"
SYS "XOS_Module",18,"LinuxCodec" TO ;f%
IF f% AND 1 THEN OSCLI("RMLoad "+FNdir+".LinuxCodec")
chunk%=65536
DIM buf% chunk%
h%=0:fh%=0:shown%=-1
SYS "LinuxCodec_Open",0,0 TO h%
SYS "LinuxCodec_OverlayOpen",h% TO key%,under%
IF under% THEN u$=" (under the desktop)" ELSE u$=" (on top of the desktop)"
PRINT "Overlay open: key colour &";~key%;u$
REM a 16:9 rectangle in the middle of the screen, in OS units
SYS "OS_ReadModeVariable",-1,11 TO ,,xl%:SYS "OS_ReadModeVariable",-1,4 TO ,,xe%
SYS "OS_ReadModeVariable",-1,12 TO ,,yl%:SYS "OS_ReadModeVariable",-1,5 TO ,,ye%
sw%=(xl%+1)<<xe%:sh%=(yl%+1)<<ye%
w%=sw%*2 DIV 3:h2%=w%*9 DIV 16
x0%=(sw%-w%) DIV 2 AND NOT 7:y0%=(sh%-h2%) DIV 2 AND NOT 7:x1%=x0%+w%:y1%=y0%+h2%
SYS "ColourTrans_SetGCOL",key%,,,0,0
RECTANGLE FILL x0%,y0%,w%-1,h2%-1
fh%=OPENIN(file$)
IF fh%=0 THEN ERROR 1,"Can't open "+file$
total%=EXT#fh%
SYS "OS_ReadMonotonicTime" TO t0%
next%=t0%:pics%=0:sent%=0:done%=FALSE:late%=0
REPEAT
  n%=total%-sent%:IF n%>chunk% THEN n%=chunk%
  IF n%>0 THEN
    PTR#fh%=sent%
    SYS "OS_GBPB",4,fh%,buf%,n%
    off%=0
    REPEAT
      SYS "LinuxCodec_Decode",h%,buf%+off%,n%-off%,0,0 TO ,,took%
      off%+=took%
      PROCshow
    UNTIL off%>=n%
    sent%+=n%
  ELSE
    SYS "LinuxCodec_Decode",h%,0,0,0,1
    SYS "OS_ReadMonotonicTime" TO tw%
    REPEAT
      PROCshow
      SYS "LinuxCodec_Info",h% TO ,,,state%
      SYS "OS_ReadMonotonicTime" TO t1%
    UNTIL state%=2 OR t1%-tw%>500
    done%=TRUE
  ENDIF
UNTIL done%
SYS "OS_ReadMonotonicTime" TO t2%
PRINT pics%;" pictures shown in ";(t2%-t0%)/100;" s (";late%;" late)"
PROCclose
END

DEF PROCshow
LOCAL p%,t%
REPEAT
  SYS "LinuxCodec_GetFrame",h% TO p%
  IF p%<>0 THEN
    REPEAT SYS "OS_ReadMonotonicTime" TO t%:UNTIL t%>=next%
    IF t%>next%+4 THEN late%+=1:next%=t%
    SYS "LinuxCodec_OverlayShow",h%,!p%,x0%,y0%,x1%,y1%
    next%+=4:pics%+=1
  ENDIF
UNTIL p%=0
ENDPROC

DEF PROCclose
IF fh%<>0 THEN CLOSE#fh%:fh%=0
IF h%<>0 THEN SYS "XLinuxCodec_OverlayHide",h%:SYS "XLinuxCodec_Close",h%:h%=0
ENDPROC

DEF FNlastword(a$)
LOCAL i%
WHILE RIGHT$(a$,1)=" ":a$=LEFT$(a$,LEN(a$)-1):ENDWHILE
i%=LEN(a$)
WHILE i%>0 AND MID$(a$,i%,1)<>" ":i%-=1:ENDWHILE
=MID$(a$,i%+1)

DEF FNdir
LOCAL d$,l%
SYS "XOS_ReadVarVal","LinuxCodec$Dir",STRING$(255," "),255,0,3 TO ,d$,l%
IF l%>0 THEN =LEFT$(d$,l%)
="<IXFS$HardDisc4>.TestBed.Codec"
