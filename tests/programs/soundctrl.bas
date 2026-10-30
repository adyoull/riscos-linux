REM soundctrl: SoundControl's SWIs by name (Configure's Sound plug-in uses
REM them): the mixer's description, and a gain set and read back.
ON ERROR PRINT "Error: ";REPORT$:END
SYS "SoundCtrl_ExamineMixer",0,0,0 TO ,,need%,size%
DIM b% -need%
SYS "SoundCtrl_ExamineMixer",0,b%,-need% TO ,,,size%,n%
SYS "SoundCtrl_SetMix",0,b%!0>>>16,0,0,-96
SYS "SoundCtrl_GetMix",0,b%!0>>>16,0 TO ,,,m%,g%
PRINT "channels ";n%;" block ";size%;" mute ";m%;" gain ";g%
