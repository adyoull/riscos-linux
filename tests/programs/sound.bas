REM sound: SoundDMA (the ROM's on LinuxAudio, or the silent stand-in): its SWIs answer as
REM SoundDMA's do, and the sound system above it (SoundChannels, the
REM voices) works on it. Prints what it saw.
SYS "Sound_Enable",0 TO e%
SYS "Sound_Configure",0,0,0,0,0 TO c%,l%,p%,h%
SYS "Sound_Stereo",1,64
SYS "Sound_Stereo",1,-128 TO ,s%
SYS "Sound_SampleRate",0 TO ,n%
VOICES 2
SOUND 1,-15,100,10
SYS "Sound_AttachVoice",1,0 TO ,v%
PRINT "enabled ";e%;" channels ";c%;" handler ";-(h%<>0);" stereo ";s%;" rates ";n%;" voice ";v%
