REM VFPSupport allows one instance only, as ROOL's: a second must fail
REM with error &81F106.
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
SYS "XOS_Module",14,"VFPSupport%Two" TO r0%;f%
IF f% AND 1 THEN PRINT "error &";~!r0% ELSE PRINT "a second instance was made"
