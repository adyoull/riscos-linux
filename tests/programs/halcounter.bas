REM The HAL counter (OS_Hardware 0: HAL entries 19 CounterRate, 20
REM CounterPeriod, 21 CounterRead) must behave like real hardware's: timer
REM 0's countdown, reloading at each centisecond tick (period = rate/100),
REM so the centisecond clock plus (period-1-count)/rate gives finer time
REM (the Internet module's microtime, UnixLib's clock_gettime). Until
REM RISC OS 5.31-15 the period was a second. Prints "halcounter ok".
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "halcounter error: ";REPORT$ : END
SYS "OS_Hardware",0,0,0,0,0,0,0,0,0,19 TO rate%
SYS "OS_Hardware",0,0,0,0,0,0,0,0,0,20 TO period%
PRINT "rate ";rate%;" period ";period%
IF rate%<10000 THEN PRINT "halcounter wrong: rate below 10 kHz" : END
want=rate%/100
IF ABS(period%-want)>want/100 THEN PRINT "halcounter wrong: period isn't rate/100" : END
bad%=0 : back%=0 : jump%=0 : n%=0 : last=-1
SYS "OS_ReadMonotonicTime" TO start%
REPEAT
  SYS "OS_ReadMonotonicTime" TO t1%
  SYS "OS_Hardware",0,0,0,0,0,0,0,0,0,21 TO c%
  SYS "OS_ReadMonotonicTime" TO t2%
  IF t1%=t2% THEN
    IF c%<0 OR c%>=period% THEN bad%+=1
    now=(t1%-start%)/100+(period%-1-c%)/rate%
    IF last>=0 THEN
      IF now<last-0.0101 THEN back%+=1
      IF now>last+0.05 THEN jump%+=1
    ENDIF
    last=now : n%+=1
  ENDIF
UNTIL t2%-start%>=200
PRINT n%;" reads, out of range ";bad%;", back ";back%;", jumps ";jump%
IF n%>100 AND bad%=0 AND back%=0 AND jump%<=n%DIV100 THEN PRINT "halcounter ok" ELSE PRINT "halcounter wrong"
