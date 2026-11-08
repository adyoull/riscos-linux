LinuxCodec 0.03 (31 October 2026)
=================================
Linux's video decoders for RISC OS programs, on RISC OS for Linux running
natively. On a Raspberry Pi 3 or 4 that's VideoCore's H.264 decoder
(Raspberry Pi's bcm2835-codec driver, built into this image's Pi kernel).
The Pi 5 has no H.264 decoder in hardware.

RISC OS runs as a Linux process on these images, so the module drives
Linux's V4L2 decoder itself (through the port's IXSupport_LinuxSyscall
SWI): it hands Linux the compressed video and gives back the decoded
pictures in Linux's own buffers, mapped into RISC OS's memory - no copy.
It doesn't work under QEMU (UTM, or /riscos/native set to "off").

Trying it
---------
  *RMLoad <IXFS$HardDisc4>.TestBed.Codec.LinuxCodec
  *LinuxCodec
lists Linux's video devices; on a Pi 3 or 4 /dev/video10 should say
"bcm2835-codec-decode ... - H.264 decoder".
  *BASIC -quit <IXFS$HardDisc4>.TestBed.Codec.CodecTest
decodes Test720/h264 (10 seconds of 1280x720 at 25 pictures a second)
and reports how many pictures came out, how fast, and a checksum of the
first picture: it should be 250 pictures and checksum 116111199 (what
ffmpeg's own decoder gives). Give a file name after CodecTest to decode
another H.264 file (raw Annex B, as ffmpeg -f h264 writes).

SWIs (chunk &5AF00, allocated by RISC OS Open)
--------------------------------------------------------
LinuxCodec_Open (&5AF00)
  r0 = flags (0), r1 = codec (0 = H.264)  ->  r0 = handle
LinuxCodec_Close (&5AF01)
  r0 = handle
LinuxCodec_Decode (&5AF02)
  r0 = handle, r1 -> data, r2 = length, r3 = timestamp (any 32-bit value:
  it comes back with the picture), r4 = flags (bit 0: end of stream -
  the decoder then gives out the pictures it still holds)
  -> r2 = bytes taken. 0 means no buffer was free: take pictures with
  GetFrame, then call again with the rest.
  The data can be cut anywhere: LinuxCodec holds it back and gives
  Linux's decoder one whole picture at a time (the Pi's decoder takes
  each buffer as a complete picture; a picture cut in two came out with
  its lower part green).
LinuxCodec_GetFrame (&5AF03)
  r0 = handle  ->  r0 -> picture block, or 0 if none is ready yet
  The block (valid until the picture is released):
    +0  index (for ReleaseFrame)
    +4  width, +8 height (the visible picture)
    +12 Y line length in bytes
    +16 -> Y (luma), +20 -> U (Cb), +24 -> V (Cr): 4:2:0 planar (I420)
    +28 U/V line length
    +32 timestamp (from Decode)
    +36 flags: bit 0 = the last picture after the end of stream
    +40 coded width, +44 coded height
LinuxCodec_ReleaseFrame (&5AF04)
  r0 = handle, r1 = index: the decoder may reuse the picture's buffer.
  Hold on to at most two pictures at once.
LinuxCodec_Info (&5AF05)
  r0 = handle  ->  r1 = width, r2 = height, r3 = state (0 waiting for
  the stream's size, 1 decoding, 2 drained), r4 = pictures decoded,
  r5 = bytes taken

The video overlay (rol-fb)
--------------------------
The decoded pictures can be shown by the display hardware itself: on a
Raspberry Pi, the display engine (vc4) has overlay planes that take YUV
pictures and scale them, so neither the conversion to RGB nor the scaling
costs any processor time. rol-fb (the image's screen front end) puts the
overlay under the desktop and makes the key colour see-through there:
paint the video's area in the key colour and the video shows through it,
with windows and menus on top of it still showing.
  *BASIC -quit <IXFS$HardDisc4>.TestBed.Codec.OverlayTest
plays Test720/h264 in a rectangle in the middle of the screen at 25
pictures a second (drag a window over it to see the key colour at work).
The desktop should be in a 16 million or 32 thousand colour mode, so that
the key colour is exact.

LinuxCodec_OverlayOpen (&5AF06)
  r0 = handle  ->  r0 = key colour (&BBGGRR00, for ColourTrans_SetGCOL),
  r1 = 1 if the overlay is under the desktop (else it's on top of it:
  nothing covers the video)
LinuxCodec_OverlayShow (&5AF07)
  r0 = handle, r1 = picture index (from GetFrame),
  r2-r5 = the screen rectangle x0, y0, x1, y1 in OS units (as a window's
  visible area): the picture is scaled to fill it.
  The picture stays on the screen until the next one is shown: don't
  ReleaseFrame a picture you've shown - LinuxCodec gives it back to the
  decoder when the next one replaces it.
LinuxCodec_OverlayHide (&5AF08)
  r0 = handle: takes the video off the screen (Close does too).

Errors (block &823600, allocated by RISC OS Open): LinuxCodec
&823600-&823608, the VCHIQ stand-in &823610-&82361C, BCMSupport
&823620-&823626, PCI &823630-&823632.

The VideoCore stand-ins (VCHIQ, BCMSupport, PCI)
------------------------------------------------
Programs written for RISC OS on the Raspberry Pi's own VideoCore modules
- ReelHWAccel's vcdec and vcenc, so Reel, Matinee and FFmpeg's h264_vchiq
decoder and encoder - talk to the VideoCore through RISC OS's VCHIQ,
BCMSupport and PCI modules. Under Linux, Linux's drivers own the VideoCore,
so those modules can't run; these three answer the same SWIs (same SWI
chunks and names) with Linux's /dev/vchiq and /dev/vcio instead:
  VCHIQ       Initialise, Connect, Disconnect, ServiceOpen, ServiceClose,
              ServiceUse, ServiceRelease, MsgQueue, MsgDequeue,
              BulkQueueTransmit, BulkQueueReceive (*VCHIQInfo for counts)
  BCMSupport  AllocPropertyBuffer, FreePropertyBuffer, SendPropertyBuffer
              (blocking), SendTempPropertyBuffer
  PCI         RAMAlloc, RAMFree (from the RMA: Linux's VCHIQ driver takes
              ordinary memory, so it needn't be physically contiguous)
  *Obey <IXFS$HardDisc4>.TestBed.Codec.LoadVC
loads them (on a Pi 3 or 4; VCHIQ refuses to load where Linux has no
VideoCore, so programs decode in software there), then
  *BASIC -quit <IXFS$HardDisc4>.TestBed.Codec.VCTest
checks the path: the firmware's answer through /dev/vcio, the "mmal"
service opened through /dev/vchiq, MMAL's version, and PCI memory. If that
says "All OK", start Reel and play an H.264 video: it should say it's
decoding on the VideoCore.

What's different from a Pi running RISC OS directly:
- Bulk transfers finish before the SWI returns (the callback, for a
  receive, is called before it returns too).
- No direct output: the VideoCore's shared memory service ("SMEM") needs
  physical addresses, so it's refused and vcdec has every picture sent by
  bulk transfer (vcdec does that on its own when SMEM isn't there).
- No buffer pools: OS_MMUControl isn't there under RISC OS for Linux, so
  vcdec and vcenc use PCI_RAMAlloc memory (they fall back to it
  themselves).
- HEVC (ReelHWAccel's hevcdec) drives the Pi 4's HEVC block's registers
  directly: that can't be done from a Linux process, so HEVC stays in
  software.
- The SWIs at VCHIQ offsets 6, 7, 8, 11 and 12 and BCMSupport offsets 0
  and 1 (not used by ReelHWAccel) give an error; their names in the
  tables are placeholders.
- Untested on a real Pi yet (the emulated machines here have no
  VideoCore): VCTest is the first thing to try.

Notes
-----
- Only one program's video is on the overlay at a time: a second
  OverlayOpen takes it over.
- No overlay (an error from OverlayOpen) on displays without YUV overlay
  planes: UTM and other virtual machines, and so far the Dev Kit.
- Data goes in as it comes: Decode copies up to one buffer's worth (512K)
  at a time; the decoder finds the pictures in the stream itself.
- Pictures come out in display order with the timestamps they went in
  with.
- Untested on real hardware yet: the Pi kernel's codec drivers and this
  module were built and checked here, but the emulated Pi used for
  testing has no VideoCore.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
