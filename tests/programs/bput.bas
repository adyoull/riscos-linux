REM BPUT# of a header and a command, as Updates writes rol-cmd's requests
REM (two strings, the second with its newline), read back every time: a
REM lost or changed write (a SWI site being given its stub) shows here
ON ERROR PRINT REPORT$;" at ";ERL : END
bad%=0
FOR i%=1 TO 9000
  h$="@password pa"+CHR$163+"ss"+CHR$252+STR$i%+CHR$10
  c$="stdbuf -oL -eL apt-get -o APT::Status-Fd=1 update --error-on=any "+STR$i%
  f%=OPENOUT("IXFS:$.tmp.bputtest")
  IF i% MOD 3 THEN BPUT#f%,h$;
  BPUT#f%,c$
  CLOSE#f%
  f%=OPENIN("IXFS:$.tmp.bputtest")
  w$="" : IF i% MOD 3 THEN w$=GET$#f%+CHR$10
  r$=GET$#f%
  e%=EOF#f%
  CLOSE#f%
  IF (i% MOD 3)=0 THEN h$=""
  IF w$<>h$ OR r$<>c$ OR NOT e% THEN bad%+=1 : IF bad%<6 THEN PRINT "round ";i%;": header ";LEN(w$);" command ";LEN(r$)
NEXT
IF bad% THEN PRINT "bput: ";bad%;" bad" ELSE PRINT "bput ok"
