REM The GPIO HAL devices (RISC OS 5.31-17): on a Raspberry Pi 3 or 4 (Linux's
REM /dev/gpiomem) two BCM2835 GPIO devices, API 1.0, whose Address is the GPIO
REM registers (the word at &1C reads "gpio" on the BCM283x/2711); elsewhere
REM none. Prints "gpio ok" (devices found and right), "gpio none" (no
REM devices) or "gpio wrong". Reads only: changes no pin.
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "gpio error: ";REPORT$ : END
n%=0 : bad%=0 : i%=0
REPEAT
  SYS "XOS_Hardware",&7FFF0403,i%,,,,,,,4 TO ,i%,d%;f%
  IF (f% AND 1)=0 AND i%<>-1 THEN
    n%+=1
    ver%=d%!8 : addr%=d%!16 : num%=d%!68
    PRINT "device ";n%;": ";FNs(d%!12);", ID ";(!d% >>> 16);", API &";~ver%;", number ";num%;", ports ";d%!64;", address &";~addr%
    IF (!d% AND &FFFF)<>&403 OR (!d% >>> 16)<>2 OR ver%<&10000 THEN bad%+=1
    m%=addr%!&1C : m$=CHR$(m%>>>24)+CHR$(m%>>>16 AND &FF)+CHR$(m%>>>8 AND &FF)+CHR$(m% AND &FF)
    PRINT "  word at &1C: &";~m%;" (";m$;")"
    IF m%<>&6770696F THEN bad%+=1
    IF num%=0 THEN
      REM the modes of GPIO 0-27 (ReadMode, the 13th entry) and Enumerate's pins
      s$=""
      FOR p%=0 TO 27 : A%=d% : B%=p% : s$+=STR$(USR(d%!96)) : NEXT
      PRINT "  GPIO 0-27 modes: ";s$
      c%=0 : k%=0 : DIM cc% 4 : !cc%=0
      REPEAT : A%=d% : B%=cc% : r%=USR(d%!72) : IF r% THEN k%+=1
      UNTIL !cc%=-1 OR k%>64
      PRINT "  Enumerate: ";k%;" pins"
      IF k%<>28 AND k%<>32 AND k%<>26 THEN bad%+=1
    ENDIF
  ENDIF
UNTIL (f% AND 1)<>0 OR i%=-1 OR n%>4
IF n%=0 THEN PRINT "gpio none" : END
IF n%=2 AND bad%=0 THEN PRINT "gpio ok" ELSE PRINT "gpio wrong"
END
DEF FNs(a%)
LOCAL s$
IF a%=0 THEN =""
WHILE ?a%<>0 AND LEN(s$)<80:s$+=CHR$?a%:a%+=1:ENDWHILE
=s$
