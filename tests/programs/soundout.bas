REM soundout: sound through RISC OS Open's SoundDMA (the ROM's) and
REM LinuxAudio, saved by rol-run (ROL_SOUND_WAV) for tests/sound-out.sh:
REM a SoundChannels note, A (pitch 89, 440 Hz), on channel 1 panned hard
REM left, for half a second; 0.3 s of nothing; then a linear handler's
REM output for a second: left -4096, right +4096 (the low halfword of each
REM word is the right sample, as SoundDMA has it); then a note scheduled
REM (SoundScheduler: SOUND's fifth parameter) for 0.2 s later, centred.
REM
REM Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
ON ERROR PRINT "Error: ";REPORT$;" at ";ERL:END
SYS "Sound_Enable",2
SYS "Sound_Stereo",1,-127
SOUND 1,-15,89,10
T%=TIME:REPEAT UNTIL TIME>T%+80
SYS "Sound_Stereo",1,0
DIM code% 256
P%=code%
[OPT 0
.handler
STMFD R13!,{R4,R14}
MOV R4,#&1000
RSB R14,R4,#0
MOV R14,R14,LSL #16
ORR R14,R14,R4
.loop
STR R14,[R1],#4
CMP R1,R2
BLO loop
LDMFD R13!,{R4,PC}
]
SYS "Sound_LinearHandler",1,handler,0 TO ,oldh%,oldr%
T%=TIME:REPEAT UNTIL TIME>T%+100
SYS "Sound_LinearHandler",1,oldh%,oldr%
T%=TIME:REPEAT UNTIL TIME>T%+30
SOUND 1,-15,101,10,20
T%=TIME:REPEAT UNTIL TIME>T%+100
SYS "Sound_SampleRate",0 TO ,n%
PRINT "rates ";n%
*LinuxAudio
