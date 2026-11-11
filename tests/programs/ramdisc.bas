REM FileCore's zone compaction and pointers past the extent (RISC OS 5.31-28).
REM 1. IXFS: a pointer set past the extent extends the file with zeros at
REM    once, as RISC OS does (PRM, OS_Args 1).
REM 2. RAM disc: the same in a new file after 100 files were written and
REM    deleted (it aborted inside FileCore at &209B029C: a read just past
REM    ScratchSpace, which is at &20000000 on RISC OS on Linux).
REM 3. RAM disc: files written, extended and deleted until the map has to
REM    be compacted many times, each file checked (a ROM build on a RAM
REM    disc aborted in FileCore's CompactZone, then "FileCore in use").
REM Prints "ramdisc ok". Makes an 8 MB RAM disc if there is none.
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "ramdisc error: ";REPORT$;" at line ";ERL : END
bad%=0
max%=256*1024
DIM buf% max%+8, n$(79), sz%(79), id%(79)
f$="IXFS:$.riscos.HardDisc4.PtrTest"
PROCpast(f$,"IXFS")
SYS "XOS_ReadDynamicArea",5 TO ,rs%
IF rs%<4*1024*1024 THEN OSCLI "ChangeDynamicArea -ramfssize 8M"
OSCLI "CDir RAM::RamDisc0.$.NT"
FOR i%=1 TO 100
  PROCsave("RAM::RamDisc0.$.NT.F"+STR$i%,8192,i%)
NEXT
FOR i%=1 TO 100
  IF NOT FNcheck("RAM::RamDisc0.$.NT.F"+STR$i%,8192,i%) THEN bad%+=1:PRINT "file ";i%;" wrong"
  OSCLI "Delete RAM::RamDisc0.$.NT.F"+STR$i%
NEXT
PROCpast("RAM::RamDisc0.$.NT.Past","RAM")
count%=0:moves%=0:seed%=RND(-12345)
FOR step%=1 TO 600
  s%=1023+RND(max%-1024)
  IF (step% MOD 3)=0 THEN s%=1023+RND(4096)
  SYS "OS_FSControl",49,"RAM:" TO free%
  WHILE (free%<s%+65536 OR count%=80) AND count%>0
    PROCdrop(RND(count%)-1)
    SYS "OS_FSControl",49,"RAM:" TO free%
  ENDWHILE
  IF (step% MOD 5)=0 AND count%>0 THEN
    k%=RND(count%)-1
    IF sz%(k%)+s% <= max% AND free%>s%+65536 THEN
      h%=OPENUP(n$(k%)):PTR#h%=EXT#h%
      FOR j%=0 TO s%-1 STEP 4:buf%!j%=FNw(id%(k%)):NEXT
      SYS "OS_GBPB",2,h%,buf%,s%
      CLOSE#h%:sz%(k%)+=s%:moves%+=1
    ENDIF
  ELSE
    n$(count%)="RAM::RamDisc0.$.NT.G"+STR$step%
    sz%(count%)=s%:id%(count%)=step% AND 255
    PROCsave(n$(count%),s%,id%(count%))
    count%+=1
  ENDIF
NEXT
WHILE count%>0:PROCdrop(0):ENDWHILE
OSCLI "Wipe RAM::RamDisc0.$.NT ~C~VFR"
PRINT "600 steps, ";moves%;" files extended"
IF bad%=0 THEN PRINT "ramdisc ok" ELSE PRINT "ramdisc wrong: ";bad%
END
DEF PROCpast(f$,w$)
LOCAL h%,e%,p%,b%
h%=OPENOUT(f$)
PTR#h%=24
e%=EXT#h%:p%=PTR#h%
BPUT#h%,65
CLOSE#h%
h%=OPENIN(f$):b%=BGET#h%:PTR#h%=24:b%=b%*256+BGET#h%:CLOSE#h%
OSCLI "Delete "+f$
PRINT w$;": pointer 24 in a new file: extent ";e%;", pointer ";p%;", bytes ";~b%
IF e%<>24 OR p%<>24 OR b%<>65 THEN bad%+=1:PRINT w$;" past the extent: wrong"
ENDPROC
DEF PROCsave(f$,s%,v%)
LOCAL j%
FOR j%=0 TO s%-1 STEP 4:buf%!j%=FNw(v%):NEXT
SYS "OS_File",10,f$,&FFD,,buf%,buf%+s%
ENDPROC
DEF FNcheck(f$,s%,v%)
LOCAL t%,l%,j%,ok%
SYS "OS_File",17,f$ TO t%,,,,l%
IF t%<>1 OR l%<>s% THEN =FALSE
SYS "OS_File",16,f$,buf%,,0
ok%=TRUE
FOR j%=0 TO (s% AND NOT 3)-4 STEP 4
  IF buf%!j%<>FNw(v%) THEN ok%=FALSE:j%=s%
NEXT
=ok%
DEF PROCdrop(k%)
IF NOT FNcheck(n$(k%),sz%(k%),id%(k%)) THEN bad%+=1:PRINT n$(k%);" wrong"
OSCLI "Delete "+n$(k%)
count%-=1
n$(k%)=n$(count%):sz%(k%)=sz%(count%):id%(k%)=id%(count%)
ENDPROC
DEF FNw(v%)=v% OR v%<<8 OR v%<<16 OR v%<<24
