REM MemMapEntries: OS_FindMemMapEntries, OS_SetMemMapEntries and
REM OS_ReadMemMapEntries on a dynamic area's pages (the port's kernel has
REM none of them; the MemMapEntries module answers them with Linux's
REM mremap). 16 pages, each filled with its own number: found, put in
REM reverse order in one call, read back, one put out of the way and
REM brought back onto another's address; then the area made smaller, and
REM another removed: their pages are forgotten.
ON ERROR PRINT REPORT$;" at ";ERL : PROCend : END
n%=16 : ps%=4096 : da%=-1
DIM t% 12*(n%+1)+4, nm% 16
$nm%="MemMapTest"+CHR$0
SYS "OS_DynamicArea",0,-1,n%*ps%,-1,&80,n%*ps%,0,0,nm% TO ,da%,,b%
FOR i%=0 TO n%-1 : FOR j%=0 TO ps%-4 STEP 1024 : b%!(i%*ps%+j%)=&A5000000+i%*256+(j% DIV 1024) : NEXT : NEXT
REM the pages
FOR i%=0 TO n%-1 : t%!(12*i%)=0 : t%!(12*i%+4)=b%+i%*ps% : t%!(12*i%+8)=99 : NEXT
t%!(12*n%)=-1
SYS "OS_FindMemMapEntries",t%
DIM pn%(n%-1)
FOR i%=0 TO n%-1
pn%(i%)=t%!(12*i%)
IF pn%(i%)=-1 OR t%!(12*i%+8)<>0 THEN PRINT "find: page ";i%;" ";pn%(i%);" ";t%!(12*i%+8) : PROCend : END
IF i%>0 THEN FOR k%=0 TO i%-1 : IF pn%(k%)=pn%(i%) THEN PRINT "find: same number twice" : PROCend : END
IF i%>0 THEN NEXT
NEXT
REM asked again: the same numbers
SYS "OS_FindMemMapEntries",t%
FOR i%=0 TO n%-1 : IF t%!(12*i%)<>pn%(i%) THEN PRINT "find again: page ";i% : PROCend : END
NEXT
REM nothing mapped at address 4096*3: -1, level 3
t%!0=0 : t%!4=&3000 : t%!8=0 : t%!12=-1
SYS "OS_FindMemMapEntries",t%
IF t%!0<>-1 OR t%!8<>3 THEN PRINT "find: unmapped gave ";t%!0;" ";t%!8 : PROCend : END
REM reverse them in one call
FOR i%=0 TO n%-1 : t%!(12*i%)=pn%(i%) : t%!(12*i%+4)=b%+(n%-1-i%)*ps% : t%!(12*i%+8)=0 : NEXT
t%!(12*n%)=-1
SYS "OS_SetMemMapEntries",t%
FOR i%=0 TO n%-1 : FOR j%=0 TO ps%-4 STEP 1024
IF b%!((n%-1-i%)*ps%+j%)<>&A5000000+i%*256+(j% DIV 1024) THEN PRINT "set: page ";i%;" isn't at its new address: &";~b%!((n%-1-i%)*ps%+j%) : PROCend : END
NEXT : NEXT
REM where they are now
FOR i%=0 TO n%-1 : t%!(12*i%)=pn%(i%) : t%!(12*i%+4)=0 : NEXT
SYS "OS_ReadMemMapEntries",t%
FOR i%=0 TO n%-1 : IF t%!(12*i%+4)<>b%+(n%-1-i%)*ps% THEN PRINT "read: page ";i%;" at &";~t%!(12*i%+4) : PROCend : END
NEXT
REM and by address
FOR i%=0 TO n%-1 : t%!(12*i%)=0 : t%!(12*i%+4)=b%+i%*ps% : NEXT
SYS "OS_FindMemMapEntries",t%
FOR i%=0 TO n%-1 : IF t%!(12*i%)<>pn%(n%-1-i%) THEN PRINT "find after set: address ";i% : PROCend : END
NEXT
REM page 0 (now at the last address) out of the way: fresh memory there
t%!0=pn%(0) : t%!4=-1 : t%!8=0 : t%!12=-1
SYS "OS_SetMemMapEntries",t%
IF b%!((n%-1)*ps%)<>0 THEN PRINT "out of the way: &";~b%!((n%-1)*ps%);" left behind" : PROCend : END
t%!0=pn%(0) : t%!4=0 : t%!12=-1
SYS "OS_ReadMemMapEntries",t%
IF t%!4<>-1 THEN PRINT "out of the way: read gives &";~t%!4 : PROCend : END
REM back, onto page 5's address (page 5 is at address n-1-5): page 5 goes
REM out of the way with its contents, and comes back at the last address
t%!0=pn%(0) : t%!4=b%+(n%-6)*ps% : t%!12=-1
SYS "OS_SetMemMapEntries",t%
IF b%!((n%-6)*ps%)<>&A5000000 THEN PRINT "back: &";~b%!((n%-6)*ps%) : PROCend : END
t%!0=pn%(5) : t%!4=0 : t%!12=-1
SYS "OS_ReadMemMapEntries",t%
IF t%!4<>-1 THEN PRINT "page 5 should be out of the way: &";~t%!4 : PROCend : END
t%!0=pn%(5) : t%!4=b%+(n%-1)*ps% : t%!12=-1
SYS "OS_SetMemMapEntries",t%
IF b%!((n%-1)*ps%+1024)<>&A5000501 THEN PRINT "page 5 back: &";~b%!((n%-1)*ps%+1024) : PROCend : END
REM (0.03) a protection level set is the level read back, by number and
REM by address, and the page is still readable and writable; a page
REM moved without mention of it afterwards keeps it... (each call gives
REM every page it lists a level: page 1 gets 1, then moves with level 1)
t%!0=pn%(1) : t%!4=b%+(n%-2)*ps% : t%!8=1 : t%!12=-1
SYS "OS_SetMemMapEntries",t%
IF t%!8<>1 THEN PRINT "level: the list's own word was changed to ";t%!8 : PROCend : END
t%!0=pn%(1) : t%!4=0 : t%!8=77 : t%!12=-1
SYS "OS_ReadMemMapEntries",t%
IF t%!8<>1 THEN PRINT "level: read gives ";t%!8 : PROCend : END
t%!0=0 : t%!4=b%+(n%-2)*ps% : t%!8=77 : t%!12=-1
SYS "OS_FindMemMapEntries",t%
IF t%!0<>pn%(1) OR t%!8<>1 THEN PRINT "level: find gives ";t%!8 : PROCend : END
b%!((n%-2)*ps%+8)=b%!((n%-2)*ps%+8)
t%!0=pn%(2) : t%!4=0 : t%!8=77 : t%!12=-1
SYS "OS_ReadMemMapEntries",t%
IF t%!8<>0 THEN PRINT "level: another page's is ";t%!8 : PROCend : END
t%!0=pn%(1) : t%!4=b%+(n%-2)*ps% : t%!8=0 : t%!12=-1
SYS "OS_SetMemMapEntries",t%
REM a bad page number is an error, and nothing moves
t%!0=&7FFFFFF0 : t%!4=b% : t%!12=-1
SYS "XOS_SetMemMapEntries",t% TO ;f%
IF (f% AND 1)=0 THEN PRINT "bad page number: no error" : PROCend : END
REM application space is refused
t%!0=0 : t%!4=&8000 : t%!12=-1
SYS "OS_FindMemMapEntries",t%
t%!4=b% : t%!12=-1
SYS "XOS_SetMemMapEntries",t% TO ;f%
IF (f% AND 1)=0 THEN PRINT "application space: no error" : PROCend : END
REM (0.02) memory that goes takes its pages with it. The area made 4
REM pages smaller: the pages at its last 4 addresses are bad page numbers
REM now, and the one before them is still good
FOR i%=0 TO 4 : t%!(12*i%)=0 : t%!(12*i%+4)=b%+(n%-5+i%)*ps% : NEXT : t%!60=-1
SYS "OS_FindMemMapEntries",t%
DIM q%(4) : FOR i%=0 TO 4 : q%(i%)=t%!(12*i%) : NEXT
SYS "OS_ChangeDynamicArea",da%,-4*ps%
FOR i%=1 TO 4
t%!0=q%(i%) : t%!4=0 : t%!12=-1
SYS "XOS_ReadMemMapEntries",t% TO ;f%
IF (f% AND 1)=0 THEN PRINT "smaller: page at a removed address still good (";i%;")" : PROCend : END
NEXT
t%!0=q%(0) : t%!4=0 : t%!12=-1
SYS "OS_ReadMemMapEntries",t%
IF t%!4<>b%+(n%-5)*ps% THEN PRINT "smaller: a page that stayed is at &";~t%!4 : PROCend : END
REM a second area: two pages found, one put out of the way, the area
REM removed: both are bad page numbers
$nm%="MemMapTest2"+CHR$0
SYS "OS_DynamicArea",0,-1,2*ps%,-1,&80,2*ps%,0,0,nm% TO ,d2%,,c%
t%!0=0 : t%!4=c% : t%!12=0 : t%!16=c%+ps% : t%!24=-1
SYS "OS_FindMemMapEntries",t%
x%=t%!0 : y%=t%!12
t%!0=x% : t%!4=-1 : t%!12=-1
SYS "OS_SetMemMapEntries",t%
SYS "OS_DynamicArea",1,d2%
t%!0=x% : t%!4=0 : t%!12=-1
SYS "XOS_ReadMemMapEntries",t% TO ;f%
IF (f% AND 1)=0 THEN PRINT "removed: the page out of the way is still good" : PROCend : END
t%!0=y% : t%!4=0 : t%!12=-1
SYS "XOS_ReadMemMapEntries",t% TO ;f%
IF (f% AND 1)=0 THEN PRINT "removed: its page is still good" : PROCend : END
REM and the first area's pages are untouched by that
t%!0=q%(0) : t%!4=0 : t%!12=-1
SYS "OS_ReadMemMapEntries",t%
IF t%!4<>b%+(n%-5)*ps% THEN PRINT "removed: another area's page is at &";~t%!4 : PROCend : END
PROCend
t%!0=q%(0) : t%!4=0 : t%!12=-1
SYS "XOS_ReadMemMapEntries",t% TO ;f%
IF (f% AND 1)=0 THEN PRINT "removed: the first area's page is still good" : END
REM a new area afterwards starts afresh
$nm%="MemMapTest3"+CHR$0
SYS "OS_DynamicArea",0,-1,2*ps%,-1,&80,2*ps%,0,0,nm% TO ,da%,,b%
!b%=&111 : b%!ps%=&222
t%!0=0 : t%!4=b% : t%!12=0 : t%!16=b%+ps% : t%!24=-1
SYS "OS_FindMemMapEntries",t%
x%=t%!0 : y%=t%!12
t%!0=x% : t%!4=b%+ps% : t%!12=y% : t%!16=b% : t%!24=-1
SYS "OS_SetMemMapEntries",t%
IF !b%<>&222 OR b%!ps%<>&111 THEN PRINT "afresh: &";~!b%;" &";~b%!ps% : PROCend : END
PROCend
PRINT "memmap ok"
END
DEF PROCend
IF da%<>-1 THEN SYS "XOS_DynamicArea",1,da% : da%=-1
ENDPROC
