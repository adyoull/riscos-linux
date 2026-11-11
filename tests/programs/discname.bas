REM IXFS's disc name (RISC OS 5.31-16): canonical names are IXFS::ROL.$.path,
REM as FileCore's discs' are (SDFS::Disc.$.path), and a disc name given is
REM ignored. Programs that take ".$." as the sign of a full path (StrongEd
REM 4.70's save) refused IXFS:$.path. Prints "discname ok".
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "discname error: ";REPORT$ : END
DIM b% 256
bad%=0
RESTORE
FOR i%=1 TO 3
  READ p$,w$
  SYS "OS_FSControl",37,p$,b%,0,0,256
  c$=FNz(b%)
  PRINT p$;" -> ";c$
  IF c$<>w$ THEN bad%+=1
NEXT
DATA "IXFS:$","IXFS::ROL.$"
DATA "IXFS:$.tmp","IXFS::ROL.$.tmp"
DATA "IXFS::Other.$.tmp","IXFS::ROL.$.tmp"
IF bad%=0 THEN PRINT "discname ok" ELSE PRINT "discname wrong"
END
DEF FNz(a%)
LOCAL s$
WHILE ?a%<>0 AND LEN(s$)<250:s$+=CHR$?a%:a%+=1:ENDWHILE
=s$
