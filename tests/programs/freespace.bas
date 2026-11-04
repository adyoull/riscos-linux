REM IXFS's free space: OS_FSControl 49 (32-bit) and 55 (64-bit) on a
REM directory. Editors ask before saving (StrongEd couldn't save to IXFS
REM while these failed). Prints "freespace ok" if both answer sensibly.
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "freespace error: ";REPORT$ : END
d$="IXFS:$.tmp"
SYS "OS_FSControl",49,d$ TO free%,big%,size%
PRINT "49: free ";free%;" biggest ";big%;" size ";size%
SYS "OS_FSControl",55,d$ TO flo%,fhi%,big2%,slo%,shi%
PRINT "55: free ";fhi%;":";flo%;" biggest ";big2%;" size ";shi%;":";slo%
ok%=(size%<>0) AND (free%<>0) AND ((slo%<>0) OR (shi%<>0)) AND (shi%>=0)
IF ok% THEN PRINT "freespace ok" ELSE PRINT "freespace wrong"
