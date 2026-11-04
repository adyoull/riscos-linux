REM IXFS and files over 2GB: a byte written just past 2GB (the file is
REM extended to there, so it's sparse on Linux: no 2GB written), its
REM extent, the pointer, and the byte read back. RISC OS files go up to
REM 4GB - 1. Before RISC OS 5.31-13 IXFS stopped at 2GB natively ("File
REM too large": Linux's limit for a 32-bit program's files without
REM O_LARGEFILE; LanMan98 copies failed there). Prints "bigfile ok".
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "bigfile error: ";REPORT$;" (line ";ERL;")" : SYS "XOS_File",6,f$ : END
f$="IXFS:$.tmp.rol-bigfile-test"
SYS "XOS_File",6,f$
h%=OPENOUT(f$)
PTR#h%=&80000010
BPUT#h%,65
e%=EXT#h% : p%=PTR#h%
CLOSE#h%
PRINT "extent &";~e%;", pointer &";~p%
h%=OPENIN(f$)
PTR#h%=&80000010
c%=BGET#h%
CLOSE#h%
SYS "OS_File",17,f$ TO ,,,,l%
PRINT "read back ";c%;", length &";~l%
SYS "XOS_File",6,f$
IF e%=&80000011 AND p%=&80000011 AND c%=65 AND l%=&80000011 THEN PRINT "bigfile ok" ELSE PRINT "bigfile wrong"
