REM OS_RemoveTickerEvent against a firing ticker (RISC OS 5.31-29). The
REM kernel turned interrupts off there with a bare MSR, which does nothing
REM in this port, so a tick between its reading a node's link and writing
REM it back could tie the chain in a loop: the next OS_RemoveTickerEvent
REM never came back and the desktop stopped (Ralph's report after
REM !ROBench, the kernel at &20823180). The window is a few instructions,
REM so this is a soak test, not a reproduction: 5.31-28 passes it too.
REM One OS_CallEvery ticker ahead of an OS_CallAfter that is removed at
REM once, as fast as it goes for 40 s.
ON ERROR PRINT REPORT$;" at ";ERL : PROCoff : END
DIM code% 64
P%=code%
[OPT 2
.tick
LDR R0,[R12]
ADD R0,R0,#1
STR R0,[R12]
MOV PC,R14
]
SYS "OS_SynchroniseCodeAreas",1,code%,P%
n%=1 : DIM w% 4*(n%+1)
FOR i%=0 TO n% : w%!(4*i%)=0 : NEXT
FOR i%=0 TO n%-1 : SYS "OS_CallEvery",1,tick,w%+4*i% : NEXT
x%=w%+4*n% : c%=0
T%=TIME
REPEAT
FOR j%=1 TO 200
SYS "OS_CallAfter",2,tick,x%
SYS "OS_RemoveTickerEvent",tick,x%
NEXT
c%+=200
UNTIL TIME-T%>4000
PROCoff
t%=0 : FOR i%=0 TO n%-1 : t%+=w%!(4*i%) : NEXT
IF t%>1000 THEN PRINT "ticker ok" ELSE PRINT "ticker: ";t%;" ticks"
PRINT ;c%;" removals, ";t%;" ticks"
END
DEF PROCoff
LOCAL i%
FOR i%=0 TO n%-1 : SYS "OS_RemoveTickerEvent",tick,w%+4*i% : NEXT
SYS "OS_RemoveTickerEvent",tick,x%
ENDPROC
