# Changelog

## Since images 1.0.0020 (9 to 13 November 2026; through Updates)

RISC OS 5.31-29 (the `riscos-5.31-source` branch), NativeSupport 0.34 to
0.48, RiSH (the RISC Shell, `apps/RiSH`), interactive `*Linux` and
`*IXRun`, the Pi kernel's fan driver and the Pi's headphone socket.
riscos-rolpkg's CHANGELOG lists each package; the next images will
include them.

### Compute Module 5: its device tree files and the D0 overlay in the Pi boot files (13 Nov 2026; riscos-kernel-pi 6.6.78-10 and -11)

andym (ROOL forum): the 8 Nov image on a Compute Module 5, from SD and
from NVMe, on Raspberry Pi's CM5 IO board and a Waveshare one: the
firmware says "the installed operating system does not indicate support
for the Raspberry Pi 5"; with `os_check=0` it starts and Linux panics
after a second.

- A CM5 that starts from SD or NVMe is a CM5 Lite (no eMMC), and the
  firmware asks for `bcm2712-rpi-cm5l-cm5io.dtb` (or `-cm4io`). The boot
  files had the eMMC CM5's (`cm5-cm5io`, `cm5-cm4io`) and not the Lite's:
  the kernel build makes all four, and the list copied into the image
  left two out. The firmware's check is for exactly that file; with the
  check off Linux is started without the right device tree.
- Added to the boot files (the same build's, so the kernel is
  6.6.78-9's). **Not tried on a CM5**, and whether that is all andym's
  board needs isn't known until he tries it.
- The 1.0.0020 image hasn't got them: `cm5-lite-dtbs.zip` (the two
  files) is for copying onto the card's FAT partition by hand.
- **6.6.78-11: `overlays/bcm2712d0.dtbo`.** With the two files andym's
  CM5 was recognised, and Linux stopped at 0.51 s with "Kernel panic -
  not syncing: Asynchronous SError Interrupt". Every CM5 (and the Pi 500,
  and Pi 5 boards from the 2 GB one on) has the D0 stepping of the
  BCM2712; the firmware applies the `bcm2712d0` overlay ("the BCM2712
  C0->D0 differences") to the board's device tree by itself when the
  file is in overlays/, and the boot files had only overlay_map and the
  three vc4-kms-v3d overlays. The kernel build makes it. Added; the zip
  for existing cards is now `cm5-boot-files.zip` (the two .dtb files and
  overlays/bcm2712d0.dtbo; RISCOSonLinux docs/files). Still not tried
  here. (A Pi 5 with the D0 chip has its own `bcm2712d0-rpi-5-b.dtb`,
  which was there: why Pi 5 owners hadn't met this.)

### MemMapEntries 0.05: Photodesk "unexpected error" on a large image (13 Nov 2026; riscos-harddisc 1.0.0054-1)

George Greenfield: Photodesk 3.22 starts and shows a sprite, Display >
Resize works, Image > Rotate aborts. Reproduced in the test VM with
Photodesk 3.14 and a 1280x800 24-bit sprite (a 32x32 one rotates):
"PhotoDesk has suffered an unexpected error", the fault inside the
module's own address table lookup (at_find), at an address inside the
staging space of the OS_SetMemMapEntries call.

- Cause: pages are put down from the staging space with mremap, which
  leaves nothing at the address moved from. When the address table grew
  during the put-down, Linux gave it that freed address space; the
  staging space was then given back whole, and the table went with it.
  Only a call that moves enough pages to grow the table does it, hence
  large images only.
- Fix (memmap.c `hold`): address space that pages are moved out of stays
  reserved until the staging space is given back. Also the whole staging
  space is given back when a move fails (it was only the part before the
  failure).
- riscos-harddisc 1.0.0054-1 also puts UnixLib's PThreadTicker 0.04
  (riscos-unixlib dist, sha256 eb185268...) in !System as
  System:Modules.PThrTicker (rolpkg `place-pthreadticker`: an older copy
  in !System is replaced, a newer one left; none: !System.310). TORCS's
  !Run asks for 0.04 and looks in System first; the copy inside !TORCS
  is 0.03.
- Tested: test VM, Photodesk 3.14, the same sprite rotated 45 degrees
  (1470x1470 result), no fault; `tests/run.sh memmap` passes.

### MemMapEntries 0.04: a module for OS_FindMemMapEntries, OS_SetMemMapEntries and OS_ReadMemMapEntries (13 Nov 2026; riscos-harddisc 1.0.0053-1)

Photodesk 3.14 (the owner's copy; George reported 3.22) stopped at start
with a "Message from PhotoDesk" box of two garbage characters, natively
and under the test bed's QEMU alike. Traced with `RISCOS_SWI_TRACE=1
bin/rol-run -d ...`: its module calls XOS_FindMemMapEntries on its
dynamic area's pages, the port's kernel has that, OS_ReadMemMapEntries
and OS_SetMemMapEntries as "SWI not known" (Kernel/s/Linux:
"Unimplemented paged memory SWIs"), the module doesn't check and returns
with V still set, and the application shows an error block that isn't
one. Photodesk reorders the 32K blocks of its image store by remapping
pages. (nsscan: its unsqueezed !RunImage has nothing to rewrite; not
NativeSupport.)

The owner asked for a module, not a ROM change.

- **modules/MemMapEntries** (memmap.c, header.s; 7 KB): takes the three
  SWIs on the SWI vector (OS_ClaimProcessorVector, as NativeSupport takes
  its few; all others go straight on) and does them with Linux: a
  dynamic area's memory is an anonymous mapping, and mremap() moves its
  pages without copying. It numbers a page when OS_FindMemMapEntries
  first asks about its address; the number stays with the memory. A
  whole OS_SetMemMapEntries list is one change (all pages lifted to a
  staging space, then put down), so a list that swaps pages works in any
  order; a page that was at a destination and isn't moved itself is put
  out of the way, not lost; address -1 puts a page out of the way.
  `*MemMapEntries` shows the counts. It won't start on a RISC OS that
  has the SWIs.
- **Not done:** enforcing protection levels (see 0.03); application
  space (refused: it is mapped again at each task switch); raising the
  error of a non-X SWI.
- **0.03: protection levels are remembered** (the owner asked whether
  they are needed: not by Photodesk, which hands back the 0 it was
  given; and levels 1 to 3 can't be enforced, as the port runs RISC OS's
  kernel and modules in Linux user mode with the programs, so a page
  made "no user access" would lock out the module that asked). The
  level OS_SetMemMapEntries gives a page is kept and is what
  OS_FindMemMapEntries and OS_ReadMemMapEntries report; pages stay
  readable and writable. Tested: memmap.bas (level 1 set, read back by
  number and by address, the page still writable, another page's still
  0), both routes; Photodesk natively as before (496 pages moved, none
  refused).
- **0.02: memory that goes takes its pages with it** (the owner: "fix
  this", of 0.01 not tidying when an area is removed). On
  Service_DynamicAreaRemove, and on Service_MemoryMoved when an area with
  numbered pages has got smaller, the module looks at the dynamic areas
  there are (OS_DynamicArea 3 and 2; not the free pool, whose "size" is
  all free memory and matched every address at first) and forgets each
  page whose address is in none of them: its number is then a bad page
  number. A page out of the way is given back to Linux when the address
  it was first found at is in no area. When none are left the numbers
  start again, so the tables don't grow with each run of a program. The
  areas with pages are remembered with their sizes, so the many
  Service_MemoryMoved calls that don't concern them cost one
  OS_DynamicArea 2 each. Tested: memmap.bas (an area made 4 pages
  smaller; a second area removed with one page in it and one out of the
  way; a new area afterwards), both routes; Photodesk natively: paint,
  Quit (3,016 pages forgotten), run again and paint (3,016 numbered
  afresh, none refused).
- **Tests:** `tests/programs/memmap.bas` (check `memmap`): 16 pages
  found, reversed in one call, read back, one out of the way and back
  onto another's address, bad page number, application space: passes
  under QEMU and natively (the native VM, NativeSupport 0.48). Photodesk
  3.14 with the module loaded: starts on both; natively a new image was
  made and painted on with the airbrush (2 Find calls, 8 Set calls, 496
  pages moved, none refused). Not tried: loading and saving pictures,
  big ones, a long session, a real Pi.

- **0.04, and packaged** (the owner: "package up for push"): on a RISC
  OS that has the SWIs the module now starts and does nothing (it gave
  an error, which would stop the start-up file it is loaded from).
  riscos-harddisc 1.0.0053-1 has it in TestBed.Modules, and its postinst
  adds `RMEnsure MemMapEntries 0 RMLoad ...` to
  !Boot.Choices.Boot.PreDesk.TestBed, as it does for LinuxOverlay and
  SCSILinux. Tested in the native VM with that line: loaded at start-up,
  Photodesk run from the Filer with nothing typed, paints (496 pages
  moved, none refused); memmap.bas on both routes.

### RISC OS 5.31-29: OS_RemoveTickerEvent turns interrupts off (13 Nov 2026; riscos-rom 5.31-29)

Ralph's report (RISCOSonLinux issue 3; Pi 4, ROM 5.31-24): some minutes
after stopping !ROBench with Escape the desktop stopped, and only
Ctrl-Break worked. rol-fb's Ctrl-Break samples in his riscos-log.txt all
have RISC OS at &20823180: the loop in the kernel's
RemoveTickerEvent_Code that walks the ticker chain, with r0 = &54F12828
and r1 = &24A858F4 (the event being removed), never finding it or the
end. The chain had become circular.

- **Cause:** the routine starts `MSR CPSR_c, #SVC32_mode+I32_bit`. In the
  port a bare MSR does nothing (user mode); the kernel's other such
  places are `pvMSR`. So the chain was walked, unlinked and the node
  freed with interrupts on. A tick after it reads a node's link and
  before it writes it into the node before (that one's own ticker
  firing and going back into the chain further down) leaves a node
  pointing at itself; a node can also be freed twice.
- **Fix** (rol/src b26db80f7, Kernel/s/TickEvents): `pvMSR`. The built
  ROM has the port's call before the MSR at &20823164.
- **Tests:** not reproduced on 5.31-28 (the window is a few instructions).
  `tests/programs/ticker.bas` (check `ticker`) is a soak test: one
  OS_CallEvery ticker, and OS_CallAfter + OS_RemoveTickerEvent as fast as
  they go for 40 s. Native suite on 5.31-29: 70 passed, 1 failed
  (ramdisc, which fails the same way on 5.31-28 in this VM); the desktop
  starts under QEMU and the test passes there (537,000 removals).
- **Found on the way, not fixed:** the ROM has more bare `MSR CPSR_c`
  that the port never converted and NativeSupport doesn't rewrite (it
  does disc modules and the ROM's sound modules): turning interrupts off
  in FileCore (9 places), the keyboard driver (1) and FSLock (1), and
  register forms in about sixty modules. Each is a place where code that
  believes interrupts are off runs with them on. The next ROM job.

### NativeSupport: the source in parts, and its scanner as a program for the build machine (13 Nov 2026; no new package: the module is the same, byte for byte)

native.c had grown to 5,564 lines, and the last two fixes (0.47, 0.48)
were both to the same thing: telling a privileged instruction from data
that reads like one, found one report at a time.

- **modules/NativeSupport** is thirteen files by job (sys, sites, scan,
  privform, psr, modes, aeabi, osmemory, sync, faults, kpmp, start,
  swistubs), each saying at its top what it is for. native.c keeps the
  description, the definitions and the list, and includes them in order:
  still one translation unit, so nothing about the code changes.
  **Checked: NativeSupport,ffa built from the parts is identical to
  0.48's as released** (sha256 776247c9...), so there is no 0.49 and
  nothing to install.
- **tools/nsscan** (`make out/bin/nsscan`; a 32-bit ARM Linux program, run
  through qemu-arm): the scanner's own code (sites.c, scan.c, privform.c,
  compiled in unchanged) run over files. It lists each word NativeSupport
  would rewrite: offset, the word, what it is, its neighbours, and for an
  AIF or ELF file whether it is in the code at all. 788 programs, modules
  and libraries here take 4 seconds.
- **tests/nsscan-cases.txt** (`make nsscan-check`; in tests/run.sh as
  nsscan-cases): 22 rows of words, one for each situation the rules were
  written for (ChartDraw's literal, OvationPro's table, Iris's Thumb-2
  branch, CMHG's and the RMA's veneers, UnixLib's thread switch, _swix's
  flags...), with the words that must be rewritten marked. A change to
  the rules that breaks an old fix now fails here.
- **Found by the first run, to look at:** of 3,924 words it would rewrite
  in those 788 files, 33 are in AIF images' read-write data and one is in
  what looks like a table in Folio's !RunImage (+&120C3C8: &210F0000
  beside &E20B0000). Whether NativeSupport ever scans those places (it
  scans what programs synchronise) is the next thing to check.

### Sound: the sound chip's mixer is set (Pinebook Pro, Pinebook) (13 Nov 2026; riscos-support 1.0.0053-1)

Raik's Pinebook Pro: Configure shows the sound device, and nothing is
heard. Looking at what Armbian does for the board (armbian/build:
config/boards/pinebook-pro.conf, packages/blobs/asound.state): it
restores a saved mixer state for the ES8316 codec, whose DAC isn't
joined to its headphone output (which also feeds the speaker amplifier)
until two switches are on. rol-snd plays through the kernel's PCM
interface and never touched a mixer, and there is no alsactl here.

- **rol-snd** sets a card's mixer controls each time it opens the card:
  a table for the Pinebook Pro's ES8316 (the two Headphone Mixer DAC
  switches and Speaker Switch on, DAC Playback Volume 192, Headphone
  Mixer Volume 11, Headphone Playback Volume 2) and the Pinebook's A64
  codec (AIF1 Slot 0 Digital DAC, DAC and Line Out switches on, Line Out
  Playback Volume 28): Armbian's values, but Headphone Playback Volume,
  which Armbian has at 0 (-48 dB by the driver's table) and is 2
  (-12 dB) here. Each entry is for the card whose id, driver or name has
  "es8316" or "sun50i" in it, so another card's control of the same name
  (a USB DAC's "DAC Playback Volume") is left alone.
- `set=Control's name=value` lines in /riscos/sound/config add to the
  table or change it, for any card (a number, on/off, or an enumerated
  control's item by name). What was set is in /riscos/log.txt
  ("rol-snd: mixer: ...").
- Tested under QEMU with its usb-audio device and a test build
  (-DROL_SND_MIXER_TEST): a table entry for "usb" sets the card's volume
  (240 to 50), one for "es8316" doesn't touch it, and set lines turn its
  switch off and set its volume; a control that isn't there is passed
  over. **Not tested on a Pinebook Pro or a Pinebook** (none here): the
  control names and values are from Armbian's files and the kernel's
  es8316 driver.

### Wi-Fi: wifi_pmf=off, and Linux network keeps other settings (13 Nov 2026; riscos-support 1.0.0052-1, riscos-harddisc 1.0.0052-1)

Raik's Pinebook Pro: with power saving off the Wi-Fi is "more stable"
but "still doesn't always work". No logs from it yet, so the cause isn't
known; this gives him something to try.

- **rol-netd:** `wifi_pmf=off` in /riscos/net/config makes the network
  block `key_mgmt=WPA-PSK` and `ieee80211w=0` (the default stays
  `WPA-PSK WPA-PSK-SHA256`, `ieee80211w=1`). Broadcom's FullMAC firmware
  and some access points don't agree on protected management frames or
  the SHA256 key type, which shows as joining and being dropped.
- **Linux network 1.01** (harddisc/extras/!LinuxNet, now in this tree: it
  was only in the image's root): Set rewrote the file with its own ten
  settings, so `wifi_powersave` and any other line were lost. Lines it
  has no field for are kept and written back.
- Tested: the two wpa_supplicant blocks from a host build of
  wpa_conf_text; under QEMU a config with wifi_powersave and wifi_pmf
  lines keeps them through Set, and the wired network still comes up
  with the new rol-netd. No Wi-Fi was tested (none under QEMU).

### NativeSupport 0.48: OvationPro starts (12 Nov 2026; riscos-support 1.0.0051-1)

OvationPro (the demo, 2.xx of 06-Apr-2010, from the owner) stopped as it
started with "Internal error: undefined instruction at &00008070", with
0.47 too; with `*NativeSupport SyncPatch Off` it ran.

- **Cause.** It is squeezed by an old squeeze, so the ROM's UnSqueezeAIF
  has it unsqueezed under its control: it puts `ldr pc, [pc, #-4]` and its
  own address at &800C, and that squeeze's decompressor synchronises the
  whole of application space (&8000 to &1A4000) while the image is being
  unsqueezed. The header no longer read as squeezed (the word at &8010
  wasn't SWI OS_Exit), so the range was scanned, and two words of it,
  &F10C0098 and &F10C0099 (a table), passed for CPS beside an AL
  instruction and were made UDF markers. The decompressed image came out
  16 bytes out below them: the header's zero-initialising loop at &8070
  was the text "(06-Apr-2010)".
- **Fixes.**
  1. A synchronisation over a program at &8000 that UnSqueezeAIF has in
     hand (&800C is `ldr pc, [pc, #-4]` and &8010 an address in the ROM)
     is left, as a squeezed program's is: FileSwitch synchronises the
     unsqueezed program afterwards (seen: &8000 to &1391B4).
  2. CPS is patched only if the processor would take it: enabling or
     disabling names at least one of A, I, F; a mode change alone names
     none; with no mode change the mode bits are zero.
- With the fault log on, the trace was: sync &8000-&A9313 (squeezed,
  FileSwitch), &A9160-&A916C and &800C-&8018 (UnSqueezeAIF's pokes),
  &8000-&1A4000 (the decompressor's: sites at &52E94 and &619A4), then
  &8000-&1391B4.
- Tested in the native VM with 0.48: OvationPro starts and opens a new
  document; ChartDraw beside it. `literal` has the two table words.
- Also with 0.48 in the native VM: Draw, Paint, Edit, Alarm, Chars, Help,
  Maestro, SciCalc, RiSH, MultiPlot running together; ROBench (52 checks,
  0 failed); Iris starts and shows its home page; CloseUp, Squash.
- The suite natively: the same 8 failures as 0.47 (that VM's own), and
  two time-outs (fp-conformance-exact, desktop-window) while a second VM
  was running ROBench on the build machine: both pass run alone (28 s,
  25 s).
- Seen and not looked into: Iris started while RiSH is running gives
  "There are still clients using ARMEABISupport".
- Photodesk (George's fourth) still not tried.

### NativeSupport 0.47: a number in a literal pool is not an instruction (12 Nov 2026; riscos-support 1.0.0050-1)

George Greenfield's report: ChartDraw 3.54 and MultiPlot 3.02 (Chris
Johnson's) didn't start on RISC OS on Linux; Photodesk 3.22 and OvationPro
neither.

- **Cause** (ChartDraw and MultiPlot, reproduced natively in the test VM):
  "Floating point exception: invalid operation" as each starts, in
  ChartDraw's `linewidth_du`: `ldfd f3, [pc, #28]; mufd f0, f2, f3;
  fixz r0, f0` with 256000.0 (draw units) as the literal. Its first word,
  &410F4000, is `MRSMI r4, CPSR`; it sits after the function's return, so
  between AL instructions, which the scan of a starting program took for
  the sign of an instruction: patched to a UDF marker, the number loaded
  was &E7F104FE.. and the FIX was invalid.
- **Fix:** `literal_word()`: a candidate (MRS, MSR, CPS forms; LDM/STM ^)
  that an instruction within reach loads pc-relative is data and is left:
  `ldr rX, [pc, #n]` (the word), `ldf/ldc [pc, #n]` (a double's or
  extended's first, second or third word). Looked for 1026 words either
  way, only for candidates. Counted ("Words loaded as literals left" in
  *NativeSupport's report; `fault-literal-left` with fault reports on).
- This doesn't make the scan able to tell every piece of data from an
  instruction (NativeSupport$Leave is still there for a module with
  tables of instructions); it removes the commonest case, a compiler's
  literal pool.
- The suite natively (tools/native-vm) with 0.47: 61 pass, 8 fail; the 8
  are that VM's own (its ROM and network: bigfile, freespace, discname,
  ramdisc, hal-counter, select-timeout, sockopt, mimetype) and the four
  of them tried again with 0.46 fail the same way, where `literal` also
  fails with 0.46 and passes with 0.47.
- Test: `tests/programs/literal.bas`, `check literal`. In the native VM
  with 0.47: ChartDraw and MultiPlot start, ChartDraw opens its example
  Finance chart. Photodesk and OvationPro (commercial) not tried: 256000
  is a usual constant in such programs, so the same cause is likely.

### init starting rol-3d is packaged (12 Nov 2026; riscos-support 1.0.0049-1)

- init from this tree (2da3406: rol-3d started at start-up if
  /opt/riscos/rol-3d is there, as rol-discd is) is in riscos-support
  1.0.0049-1. Until now the package's init was the one before that
  commit. The same binary has been the QEMU test image's init since it
  was written (it starts rol-3d there). Not yet started on a Pi.

### rol-fb overlays: the Pi 4's results, and planes set without waiting (12 Nov 2026; riscos-support 1.0.0048-1)

- **The first test build on the owner's Pi 4 (kernel 6.6.78-9, 1920x1200):**
  16 overlay planes, each taking YUV420, XRGB, ARGB, XBGR and ABGR, zpos
  1 to 16 (the primary 0, the cursor 17). RGB overlays showed, stacked in
  slot order under the desktop and through the key colour. But SETPLANE
  waits for the display's next frame (13.7 ms a SHOW): one overlay ran at
  58.7 frames a second, two at 28.1 each, three at 15.7 each, and rol-fb
  did nothing else meanwhile.
- **Planes are now set by atomic commits that don't wait**
  (DRM_CLIENT_CAP_ATOMIC; DRM_MODE_ATOMIC_NONBLOCK with a page-flip
  event): every SHOW that is waiting goes into one commit, and each is
  answered when the display's event says it is on screen, with that time.
  One SHOW at a time on a connection (-EBUSY before the last is
  answered). The old way is kept for a driver without atomic commits, if
  a commit fails, or if /riscos/overlay-legacy exists. Before anything
  still set the old way (a mode, a plane off, zpos) the commit that is
  out is waited for. Video (LinuxCodec) goes the same way.
- **Build 3 on the Pi 4** (atomic commits in use): 56.1 frames a second
  with one overlay, 27.9 each with two, 26.0 each with three. A commit
  was made as soon as one SHOW came, so the others' went into the next
  frame's, and they took turns. **Build 4: a commit waits** until every
  overlay that is showing has asked, or until 5 ms before the next frame
  is due (from the last flip's time and the mode's refresh rate).
- **Build 4 on the Pi 4:** 58.5 with one, 50.8 each with two, 40.2 each
  with three (the test program drew every overlay's picture itself, in
  turn, inside the frame: `ovtest show` is now a process for each), and a
  black flash as the first overlay opened. **Build 5: no flash**: when
  the desktop's buffer is remade at the same size (with or without an
  alpha channel) the picture is copied into the new one before it is
  shown, and the old buffer is let go only after the new one is up
  (letting it go first turned its plane off).
- **Build 5 on the Pi 4** (a process per overlay): 58.6 frames a second
  with one overlay; 56.0 and 55.4 with two; 56.5, 57.1 and 56.5 with
  three. About 15.5 ms from SHOW to the screen.
- **Checked by the owner on the Pi 4:** video as before (build 4);
  with RISC3D's kit 12 on build 5: several GL windows at once, a menu or
  window over one, dragging, a mode change while running. Build 5's
  rol-fb is riscos-support 1.0.0048-1's.
- **A window part of the way off the screen** shows the part that is on:
  rol-fb cuts the destination and the source to the screen; wholly off,
  the plane goes off and the SHOW is answered as shown.
- SHOW's `[10]` bit 8 (tests): no key, the rectangle shows through the
  desktop whatever is there. `ovtest show N` uses it (add `key` for the
  key), and now asks for every overlay's frame before taking the answers.
- **Tested:** QEMU test mode only ("ovtest ok"; the desktop starts with
  the atomic capability on). **The atomic path has not run on any real
  plane:** PlaneTest-3.

### Passwords: Show, a Caps Lock warning, and a log (12 Nov 2026; riscos-support 1.0.0047-1, riscos-harddisc 1.0.0051-1)

The report (George Greenfield, Raik: the password chosen at the first use
of Updates refused after the first update) isn't reproduced and its cause
isn't found; what is left is that different characters were typed the
second time, which stars hide. So:

- **Updates 1.02** (harddisc/extras/!ROLUpdate): the password window has a
  **Show** option (the fields' "D*" validation off and on; off each time
  the window opens) and says **"Caps Lock is on"** while it is (OS_Byte
  202, read on null events while the window is open).
- **rol-cmd** (images/fe/rol-cmd.c): `/var/log/rol-cmd.log` (mode 600,
  emptied past 64K) gets a line for each password set and each refused:
  the length, how many characters aren't ASCII, and for a refused one
  whether it would have been right with the letters' case swapped. Never
  the password.
- Nothing in how a password is stored (root's entry in /etc/shadow) or
  checked has changed: existing passwords are untouched.
- riscos-support 1.0.0047-1 has this rol-cmd and **1.0.0046's rol-fb**
  (the several-overlays rol-fb below waits for its Pi test).
- Tested under QEMU: set, a case-swapped try (refused, logged YES), a
  wrong one, the right one; the window with stars, shown, Caps Lock on.

### rol-fb: several overlays at once, and RGB ones (12 Nov 2026; test build 1; in riscos-support 1.0.0048-1 as built above)

For RISC3D (its handoff of 12 Nov): a GL window's frames shown straight
from the GPU's buffers on a hardware plane, as video is, instead of read
back and copied three times.

- **Each connection to "rol-fb-overlay" is an overlay with a plane of its
  own**, up to 4, freed when the connection closes. Before, there was one
  overlay and a new connection replaced it. A second *video* still takes
  over from the first (LinuxCodec's behaviour is unchanged: it sends no
  kind or version and gets the replies it always did).
- **RGB overlays**: OPEN with kind 1 picks a plane that takes XRGB8888,
  ARGB8888, XBGR8888 or ABGR8888 and says which; ADDBUF takes those
  fourccs and, optionally, a format modifier.
- **Stacking**: the overlays from the bottom in the order of their slots,
  the desktop above them all (on the Pi: overlays at zpos 1 to 4, the
  desktop's plane at 5; where a driver won't have that, one overlay at a
  time as before). The key colour clips every overlay, so no clip
  rectangles are needed.
- **SHOW's reply to a version 1 client** has the time the plane was set
  and how long setting it took. On vc4 that call waits for the display's
  next frame, so the reply paces a client to the display. Two overlays
  animating at once may therefore get half the display's rate each: the
  Pi test measures it, and atomic commits (several planes in one) are the
  answer if so.
- **Planes logged at start-up** (type, formats, zpos), and each overlay's
  average time to set its plane every 600 frames.
- A dmabuf's handle is closed when its last framebuffer here goes (it was
  closed with each).
- `tools/ovtest.c`: a Linux client. `ovtest` tests the protocol (three
  overlays at once, a video taking over, an old client, a fifth
  connection refused); `ovtest show N` shows N RGB overlays of moving
  bars for 10 seconds and prints the frame rate.
- **Tested:** under QEMU in rol-fb's test mode (/riscos/overlay-test; the
  virtual display has no overlay planes): "ovtest ok". **Not tested: any
  real plane** (the stacking, RGB buffers, and video as before): that is
  the PlaneTest kit for a Pi 4 or 5.

### Test: BPUT# of two strings, read back (12 Nov 2026)

- tests/programs/bput.bas, `check bput`: a new file written as Updates
  writes rol-cmd's requests (a password line, then the command with its
  newline), 9000 times with every write read back. From the report that
  the root password set at the first use of Updates was refused after
  the first update (George Greenfield, Raik): a lost first line would
  read to rol-cmd as no password. Passes under QEMU and natively
  (tools/native-vm, NativeSupport 0.46 with SWI stubs), so that isn't
  the cause; rol-cmd itself sets and checks a password with Latin-1
  characters correctly. The cause isn't found yet.

### Pi kernel 6.6.78-9: the GPU's driver, v3d (12 Nov 2026; riscos-kernel-pi 6.6.78-9)

- CONFIG_DRM_V3D=y (images/kernel-6.6-pi.config): Linux's driver for the
  Pi 4 and 5's 3D hardware. It was off, so Linux had the display (vc4)
  but no 3D: nothing could draw on the GPU. Needed by the RISC3D
  prototype's Linux3D driver; nothing in the images uses it yet. The
  device trees and overlays are unchanged (dtoverlay=vc4-kms-v3d already
  enables the node).
- Built (6.6.78-v8+ #12); v3d's symbols are in System.map. **Not booted:**
  no Pi here, and this kernel doesn't boot under QEMU's virt machine.

### init starts rol-3d, if it is there (12 Nov 2026; not packaged)

- For the RISC3D prototype (its own repository, risc3d): init starts
  `/opt/riscos/rol-3d` beside rol-usbd and rol-discd when the file
  exists. No image or package has the file yet.

### Internal FileCore discs (12 Nov 2026; riscos-support 1.0.0046-1, riscos-harddisc 1.0.0050-1)

- Open since 2 Nov (the owner then: rol-usbd shouldn't watch internal
  drives). The owner's choice now: automatic at start-up, read-write.
- **rol-discd** (new, `images/fe/rol-discd.c`; init starts it beside
  rol-usbd): looks once at start-up at every disc that isn't rol-usbd's
  (not on USB, not removable, not an SD card in a slot): NVMe, SATA,
  eMMC, virtio. A disc that is FileCore as a whole is given whole;
  otherwise each partition that is. Never the partition Linux runs from,
  anything mounted or swap, or Linux's own disc as a whole (a FileCore
  partition beside Linux's is given). The same boot block test as
  rol-usbd's. Lists them in /riscos/mediactl/filecore-internal (not
  removable) and serves "OPEN dev" on the abstract socket
  "rol-discd-discs" (O_RDWR, O_EXCL, O_DSYNC); it exits if there are
  none.
- **SCSILinux 0.02:** reads both lists and asks the right daemon.
  Internal discs are fixed discs: SCSI::4 onwards, with hard disc icons.
- **Tested** under QEMU (ROM 5.31-28) with extra virtio discs: a 64 MB
  FileCore image as a whole disc (vdb) and the same inside an MBR
  partition (vdc1): both listed "fixed", catalogued, a hard disc icon
  each; a file copied on is byte for byte in the host's image at the
  right place (partition: 1 MB further on); an application directory
  copied with its types and dates; the root disc (vda1, vda2) is not
  offered. Two discs with the same name and contents confuse FileCore
  (as on any RISC OS machine): tested one at a time.
- **Not tested:** real NVMe, SATA or eMMC; a FileCore partition on the
  disc Linux runs from; discs over 2 GB; 4K-sector discs.

### NativeSupport 0.46: a review's findings (12 Nov 2026; riscos-support 1.0.0045-1)

An outside review of header.s and native.c 0.45 (the owner passed it on).
Each point checked against the source and, where it could be, run:

- **Real, fixed: patched words in a file written in pieces.** A copy of
  a patched instruction at the first or last word of a write was left as
  its UDF marker (orig_word wanted both neighbours inside the write).
  The neighbours now come from memory when they are outside it (read
  safely, only compared). Test: LinuxAudio's code copied to a buffer and
  written in pieces of 4 to 20 bytes: 0.45 left 4, 4, 4, 3 and 2
  markers in the file; 0.46 none.
- **Real, fixed: an emulated LDM/STM ^ or SWP that faults is a data
  abort.** It went to RISC OS as an undefined instruction (and, a site,
  was put back for 10 s since 0.45). The fault the emulation took now
  goes to the port's abort handler with the instruction's registers
  (abort_instead); such a site is not put back. Test: LDMIA r0,{r1,r2}^
  at &1F000000 in SVC mode: "undefined instruction" on 0.45, "abort on
  data transfer" on 0.46. *NativeSupport counts them.
- **Real, fixed: a Linux call on every SWI while a site is put back.**
  ns_fixups_run read the clock each time, for 10 s; now on one SWI in
  256. Measured under QEMU (OS_ReadMonotonicTime in a loop for 2 s):
  0.45 34,744 while put back against 54,287 after; 0.46 no slower.
- **Not a bug: aeabi_clamp's 0.** RISC OS never has a clamp of 0
  (OS_DynamicArea 8 ignores anything under 1 MB, and they start at the
  RAM limit), so 0 can only mean "none saved".
- **Not a bug: ns_und_fast and the flags.** The kernel's fast entry
  changes only the pc and r0-r2; ns_und_fast uses no flag-setting
  instruction, so the vector is entered with the interrupted flags, as
  on hardware.
- **True, left: LOWSTUBS is unused** since application space went to
  UDF sites only (deliberately: a copied branch can't be told from any
  other). One spare 4 KB mapping; to be removed with the next change
  there. SUSPEND_MAX full leaves the ninth site's UDF showing: by
  design. The three tentative `debug` declarations: tidied later.
- Checked after: the desktop, Edit, Draw, Paint and RiSH start; failure
  counts 0 apart from the test's own fault.

### RISC OS 5.31-28: FileCore's compaction, pointers past the extent (11 Nov 2026; riscos-rom 5.31-28)

- Chris Gransden: a ROM build on a 2 GB RAM disc failed after seconds,
  then "FileCore in use". The web browser project: OS_Args 1 past the
  extent doesn't extend the file on IXFS and aborts on a RAM disc.
- One cause for the aborts: FileCore's CompactZone keeps its tables in
  ScratchSpace and was written for ScratchSpace at &4000 (bit addresses
  as address * 16 in 32 bits; reads a word or two past the end, into
  application space). On RISC OS on Linux ScratchSpace is at &20000000
  with nothing after it. Fixed in the ROM source (FileCore32; the HAL
  maps the page after ScratchSpace); IXSupport extends a file when its
  pointer is set past the extent. The detail is in the ROM's CHANGELOG.
- New test `tests/programs/ramdisc.bas` (check `ramdisc`): fails on
  5.31-24 (IXFS extent 0; abort at &209B029C), passes on 5.31-28.
- ROM builds over an earlier build: Edit must be relinked (BuildROM now
  does). 5.31-25 to 5.31-27 were test builds.

### RiSH 0.04: the system font's size is put back as it was (11 Nov 2026; riscos-harddisc 1.0.0049-1)

- Found testing a paste from Edit into RiSH under QEMU: after RiSH had
  redrawn, Edit's text (system font) was drawn half height. RiSH's
  redraw set the VDU 5 character size (VDU 23,17,7) to its own and then
  "restored" it to 8 by 8 pixels; the mode's own size is 8 by 16 in a
  90 dpi mode, and the setting is global, not the task's.
- redraw() now reads GCharSizeX/Y and GCharSpaceX/Y (VDU variables 162
  to 165) before changing them and puts those values back (size and
  spacing separately, as they can differ).
- Tested under QEMU (ROM 5.31-24): Edit open beside a RiSH window that
  redraws (ls --color, typing, the cursor): Edit's text stays full
  height.

### RiSH: its copyright and licence files (11 Nov 2026; riscos-harddisc 1.0.0048-1)

- The owner: RiSH is part of RISC OS on Linux, with his copyright and the
  licences. In the application now: `LICENSE` (MIT, Copyright (c) 2026
  Andrew Youll) and `UnixLib-COPYING` (the program is statically linked
  with UnixLib), named in !Help; copyright lines in !Run and !Boot.
  LICENCES.md has RiSH's row. The package's copyright file names RiSH and
  UnixLib, and Documents.Licences has UnixLib-COPYING with a row in its
  README. The program itself is 0.03, unchanged.

### RiSH 0.03: copy and paste (11 Nov 2026; riscos-harddisc 1.0.0047-1)

- **Selecting:** Select pressed in the window starts a selection and the
  pointer is followed on null polls while the button is held (Adjust moves
  the selection's end; past the top or bottom the window scrolls a line a
  poll). It runs from a cell to just before another, by line number
  counted from the first line ever shown, so it stays on its text as that
  scrolls into the history. A click without a drag, a resize, the
  alternate screen and clearing the history remove it.
- **Copy** (the menu; Ctrl-Shift-C, told from Ctrl-C by the Shift key's
  state): the text (Latin-1, LF line ends, trailing spaces left off,
  line-drawing characters as - | +) is kept, and the clipboard claimed
  (Message_ClaimEntity). Another program's paste (Message_DataRequest) is
  answered with DataSave, and the file written where its DataSaveAck
  says, then DataLoad. RAM transfer isn't offered.
- **Paste** (the menu; Ctrl-Shift-V; Shift-Insert): RiSH's own clipboard
  directly, else Message_DataRequest, the owner's DataSave answered with
  <Wimp$Scrap>, and the file's text (up to 1 MB) typed in: line ends as
  Return, Latin-1 as UTF-8, control codes dropped, inside ESC[200~ and
  ESC[201~ when the program asked for bracketed paste (bash does).
- **Tested** under QEMU: a drag selection, Copy and Paste from the menu
  within RiSH; Ctrl-V in !Edit pasted RiSH's text; text copied in !Edit
  pasted into RiSH with Ctrl-Shift-V. Not done: several lines, the
  scrolling drag, Adjust, a paste into vi.

### RiSH 0.02: "the RISC Shell" (11 Nov 2026; riscos-harddisc 1.0.0046-1)

- The owner: RiSH stands for RISC Shell. Its Info, !Help and file headers
  say so. Nothing else changed from 0.01.

### RiSH 0.01: a Linux terminal for the desktop (11 Nov 2026; riscos-harddisc 1.0.0045-1, riscos-support 1.0.0044-1)

- **apps/RiSH** (new; C, static UnixLib, built by apps/RiSH/build.sh into
  out/apps/!RiSH; the sprites are drawn by mksprites.py). A Wimp program:
  each window is an xterm-256color terminal on a rol-cmd terminal job.
  - Emulation: cursor movement, erase, insert and delete (characters and
    lines), scroll regions, index and reverse index, the alternate screen
    (47, 1047, 1049), origin, wrap and insert modes, save and restore,
    SGR (16, 256 and true colours, the last to the nearest of the 256),
    REP, tabs, DA and DSR replies, OSC 0/2 (the window's title), the
    VT100 line-drawing set and UTF-8 (Latin-1 as the system font has it;
    box-drawing and block characters are drawn; the rest is "?").
  - The window: VDU 5 system font at 8 by 16 pixels (VDU 23,17,7), twice
    that with "Large text"; rows redrawn as they change; 1000 lines of
    history above the screen in the same work area (the scroll bar); the
    terminal's size is the window's (Open_Window_Request), sent to Linux.
  - Keys as an xterm's, with application cursor keys; unknown function
    keys go back to the Wimp (F12).
  - A root terminal probes rol-cmd with no password, then asks in the
    window (password, or a new one twice) and keeps the token rol-cmd
    gives for the next root terminal. It is marked (the owner's request):
    a red title bar that always begins "ROOT:", a red cursor, and a bold
    banner when the shell starts.
- **rol-cmd:** `@tty COLUMNS ROWS NAME` sets TERM (and then leaves
  DEBIAN_FRONTEND unset); NAME.size changes the pty's size; NAME.trim
  punches the read part out of NAME.out (fallocate; the length stays, so
  offsets don't move); MAXJOBS 16.
- **Tested** under QEMU (ROM 5.31-24, NativeSupport 0.45, 1280x800): the
  shell, `ls --color`, busybox vi, less and top, an ncurses test program
  (boxes, 256 colours, a scrolling sub-window, key names for the cursor
  keys, Home, End, Delete, Page Up, F1 and F5), `seq 1 5000`, resizing by
  the size icon (stty size followed), the scroll bar, Large text and back,
  two windows at once, a root terminal with a new password, a second one
  on the token, exit and Close. On the host: NAME.trim freed blocks with
  the length kept. Not done: a Pi, a 4K screen, copy and paste, the mouse
  in programs, nano itself (no Debian mirror from here).
- One boot of the test image stopped on the start-up screen before RiSH
  was run (hourglass, nothing drawn); the next boots were fine. Not
  investigated.

### The Pi's headphone socket: config.txt's dtparam=audio=on was the overlay's (11 Nov 2026; riscos-kernel-pi 6.6.78-8)

- Ralph Barrett: still no sound from the Pi 4's headphone socket with
  rol-snd choosing it. Every Pi 4 kernel log we have (his and ours) has
  `snd_bcm2835.enable_headphones=0` with no `=1` after it on the firmware's
  command line, and ALSA lists only vc4-hdmi-0 and vc4-hdmi-1: there is no
  Headphones card for rol-snd to choose.
- Cause: the images' config.txt had `dtparam=audio=on` after
  `dtoverlay=vc4-kms-v3d`. A dtparam after an overlay belongs to that
  overlay (vc4-kms-v3d has an `audio` parameter, HDMI's), so the base
  device tree's audio stayed off.
- Fix: the image scripts write it before the overlay; riscos-kernel-pi's
  postinst moves the line in an installed card's config.txt (only when
  the line is there and only after a dtoverlay; config.txt.old kept).
  Installed under QEMU on the generic image: line moved, second run
  changes nothing. Sound on a Pi: not yet confirmed.

### An interactive terminal for Linux commands, and the Pi 5's fan (11 Nov 2026; riscos-support 1.0.0042-1, riscos-harddisc 1.0.0043-1, riscos-kernel-pi 6.6.78-7)

- **rol-cmd: terminal jobs.** A request whose first line is `@tty COLUMNS
  ROWS` runs on a pty: its output goes to NAME.out as before, and what
  RISC OS appends to NAME.in is typed into the terminal (only a regular
  file owned by RISC OS's user, never through a link; removed when the job
  ends). An empty command is a shell (`bash -i`, or `sh -i`). The job is
  its own session with the pty as its terminal, so Ctrl-C (byte 3 in
  NAME.in) reaches what the shell is running; NAME.kill hangs up on the
  terminal's foreground program and ends the shell (an interactive shell
  ignores SIGTERM). Who it runs as is unchanged (`@user`: uid 1000;
  otherwise root, with the password or a token). Requests without `@tty`
  are untouched.
- **LinuxShell 0.04.** *Linux and *LinuxUser (*IXRun) ask for a terminal
  job sized to the text screen, send the keys waiting (OS_Byte 129) to
  NAME.in each time round their loop (Return as CR, Delete as DEL, the
  cursor keys as their escape sequences, with *FX 4,1 for the job's
  length), and take no parameters for a shell. Escape sends Ctrl-C; a
  second within 1.5 s writes NAME.kill. Output is filtered for RISC OS's
  VDU: escape sequences dropped, tabs expanded, UTF-8 to Latin-1,
  "backspace space backspace" as VDU 127 and a lone CR as rubbing the line
  out (a task window acts on neither a bare backspace nor a bare CR),
  other control codes dropped (17 changed the text colour). *LinuxPassword
  is unchanged. A shell's exit status isn't reported.
- **Tested** under QEMU (ROM 5.31-24, NativeSupport 0.45) in a task window:
  *IXRun shell (id: riscos; `touch /etc/x`: permission denied), stty size,
  `read -p`, cat with Ctrl-D, history with cursor up, Delete, Escape on
  `sleep`, Escape twice, `exit 3`, one-off commands, *Linux with a new
  root password, then passwd, `rm -i` and apt-get's [Y/n]. Not done:
  full-screen programs (TERM=dumb), the F12 command line, a Pi.
- **Pi kernel 6.6.78-7** (images/kernel-6.6-pi.config):
  CONFIG_SENSORS_PWM_FAN and CONFIG_SENSORS_RASPBERRYPI_HWMON on. The Pi
  5's fan (a user's report: the official cooler never starts) is driven by
  pwm-fan from the board's device tree (trips at 50, 60, 67.5 and 75°C);
  the PWM, thermal sensor and governor it needs were already in. Untested:
  no Pi 5 here, and QEMU has no such fan.

### Packaged 11 Nov 2026: riscos-support 1.0.0041-1, riscos-harddisc 1.0.0040-1, riscos-rom 5.31-24

- **riscos-support 1.0.0041-1:** NativeSupport 0.45 (0.34 to 0.45 below: SWIs
  without a trap, the lost-write fix, files written without patch markers,
  the review's fixes) and rol-snd's choice of the headphone socket when the
  screen has no speakers (below). Tested natively under QEMU with riscos-rom
  5.31-24: the ROM applications, seven disc applications, and Iris loading
  and scrolling a saved copy of the BBC home page, with no faults on the
  serial log and NativeSupport's failure counts at 0. rol-snd's change is
  untried on a Pi.
- **riscos-harddisc 1.0.0040-1:** PackMan's own CaCertificates 1.00-6 in
  place of the images' hand-placed `!CaCertificates` (`harddisc/extras`,
  copied by `harddisc/fetch.sh`), with its PackMan record: installing Iris
  stopped with "files conflict" (Ralph Barrett). The image build still
  copies the hand-placed one; riscos-rolpkg's install-packman-packages then
  replaces it, so the next images are right without a change here.
- **riscos-rom 5.31-24** (ROL-ROM nightly-merge 2e219ea98): new dynamic
  areas at the lowest free address, as on native RISC OS, not where Linux
  chooses. `ROL-Testbed/MemCompare2` (a BASIC test of reads in and past
  dynamic areas) gives the same result on it as on native RISC OS 5.31 on
  the owner's Pi 4.

### The Pinebook Pro: its touchpad, and its Wi-Fi dropping (9 Nov 2026; riscos-support 1.0.0038-1)

Raik's report on 1.0.0020 on a Pinebook Pro: the touchpad is "a bit
special", and the Wi-Fi connects for a second, drops, and does it again.
- **rol-fb: touchpads move the pointer like a mouse.** A touchpad reports
  where the finger is on the pad (ABS_X/Y), as a tablet does, so rol-fb
  put the pointer at that place on the screen: it jumped wherever the
  finger landed, and tapping did nothing. A touchpad (a device with
  BTN_TOOL_FINGER that isn't a touchscreen or a pen tablet) now moves the
  pointer by how far the finger moves, as relative moves RISC OS scales by
  its mouse speed (about 8 counts a millimetre, up to 3 times that when
  fast). Two fingers scroll (a wheel notch every 4 mm, both ways). A quick
  touch (under 0.2 s, under 2 mm) is a click: one finger Select, two
  Menu, three Adjust; not within 0.3 s of a key (a palm while typing).
  Pressing the pad (a clickpad's button) is Select, with two fingers on
  it Menu, three Adjust. Tablets (UTM, QEMU) and touchscreens are as
  before. The log names a touchpad ("input event3: ... (touchpad)").
  Checked with a test harness feeding rol-fb a touchpad's events (moves,
  taps with 1 and 2 fingers, two-finger scroll, clicks), and under QEMU
  that a tablet still places the pointer; not yet on a Pinebook Pro.
- **rol-netd: Wi-Fi power saving off** (wpa_supplicant's `SET ps 0`, each
  time the link comes up). The Pinebook Pro's Broadcom Wi-Fi (AP6256,
  brcmfmac) is known to drop its connection with power saving on.
  `wifi_powersave=on` in /riscos/net/config keeps it on. Not checked on a
  Pinebook Pro yet (QEMU has no Wi-Fi).
- **The network's log.** rol-netd writes what happens (links up and down,
  addresses, wpa_supplicant starting and stopping, power saving) to
  /riscos/net/log.txt, and wpa_supplicant's own log (with times now, -t,
  so disconnections and their reasons show) stays in
  /run/wpa_supplicant.log. init copies both to the FAT partition's LOGS
  (network-log.txt, wifi-log.txt) with the others. Checked under QEMU: the
  wired network's lines reach LOGS/network-log.txt.

### NativeSupport 0.45: the audit's five open points (10 Nov 2026; release candidate)

- **NativeSupport$Leave**: module titles (comma-separated, read as each
  module is loaded) left unpatched, in the kernel's synchronisation of
  the module and at Service_ModulePreInit. The patching can't tell an
  instruction from data with the same bits (a debugger's, assembler's or
  emulator's instruction tables); this is the way out for such a module.
  *NativeSupport shows "Modules left unpatched".
- **An instruction that can't be done is shown as itself.** When both
  emulations fail for a site (MRS/MSR SPSR in user mode, a fault), the
  original word is put back before RISC OS gets the undefined
  instruction, so its handler, the error and a debugger see the
  instruction, not &E7Fxxxxx (Ralph Barrett's !ArmDebug32 report). It is
  made the site again by the first SWI 10 seconds later, if the word is
  still there (ns_fixups_run, from the SWI vector; Linux's clock).
- **SWI stubs: the SWI word and the branch back are outside the pages
  where the ROM holds interrupts back.** The area is 20 pages: the
  hook's, seven data/code pairs (held), then 8 bytes a stub {SWI, B site
  + 4}. A stub is now str lr; bl common; the return address. An
  interrupt arriving at the branch back used to stay pending, with
  Linux's signals blocked, until the program's next SWI.
- **Patched words written to a file: from a corrected copy.** OS_GBPB
  1/2 and OS_File 0/10 of any memory but the ROM: the data is scanned
  (through probe_call: no OS_GetEnv, no SWI at all unless something is
  found); if it has words of ours the write is made from a copy in the
  RMA with the originals. Memory is never changed (it was, for the
  write, and copies for good). Ours: a UDF at its site's address; a copy
  of one with the same words either side as the site has (not data that
  happens to match: any matching word of a live site used to be changed
  in the program's buffer); outside application space, the branch to
  that address's MRS stub and a never-run NOP (now recorded). OS_GBPB's
  r2-r4 and C come back as the kernel's; a plain (not X) SWI's error is
  raised (the SWI goes on as OS_GenerateError) instead of coming back
  as V set.
- **Test commands gone**: SWIStubs Count, NoCond, Fresh, Fix, NoFix.
Checked under QEMU: the desktop; BASIC's GenData right with the stubs
made during it; FrontEnd saved from the RMA has no UDF (2 in memory, 4
words put back); BASIC "MRS R0, SPSR" in user mode: the error, then
&E14F0000 in memory; Tinct left by NativeSupport$Leave; ROBench os.swi,
os.readvar, io.bput the same as 0.44 within the spread.
(Found on the way: the first version read the time with a SWI from the
SWI vector and looped; Linux's clock instead.)

### NativeSupport 0.44: from a read-through of the whole module (10 Nov 2026; test version)

- **Modules patched as they start reach the instruction cache.**
  Service_ModulePreInit's patches (the never-run NOPs, any site the
  kernel's own synchronisation hadn't shown, the ARMEABISupport 1.05
  fix) were written with no cache flush after them; now flushed.
- **OS_Memory 0's "physical" numbers for application space**: the 256th
  different file mapped there got number 0 (its 8 bits overflowed) and
  later ones the first's; now 255 at most, the oldest used again.
Read and left as they are (see the project's audit note): the interrupt
held back at a stub's last word until the next SWI (one instruction;
needs the stubs' return words outside the held area), data in modules
that looks like privileged instructions being patched (debuggers' and
assemblers' tables), patched words in a module saved to a file, the
write's C flag in write_unpatched, one squeezed program pending at a
time, the test commands (Count, NoCond, Fresh, NoFix) to come out before
release.

### NativeSupport 0.43: sites used again; stubs made only on a SWI from USR mode (10 Nov 2026; test version)

From reading the rest of the module after the lost BPUT# lines:
- **The site table filled up in ROM builds.** A program run again got new
  sites for the same instructions at the same addresses: the owner's
  reports show about 930 new sites in each short build and 4,000 in a
  full one, 11,183 of the 16,384 after StubTest41's twelve rounds. Full,
  later programs' privileged instructions were left unpatched (their
  mode changes then do nothing) until a restart. site_add now finds the
  site made before for the same address, word and kind (a hash table)
  and uses it again. *NativeSupport shows "Sites in use (of 16384)" and
  "Sites used again".
- **SWI stubs are made only on a SWI from USR mode** (ns_swi_vector's
  drain): then no other SWI can be on its way to the kernel with its
  site unread (ns_swi_c's own SWIs, an interrupt's and other modules'
  on the vector are all from privileged modes), so the 0.42 case can't
  arise. The 0.42 check stays as a second line.
- **The 0.42 check is exact**: the stub's last word must branch back to
  this SWI's return address, and the caller mustn't be Thumb code
  (whose halfwords could look like a branch; an empty stub slot's &FF
  words passed the old "is a SWI" test).
Checked under QEMU: stubs made from nothing (Fresh) with the 0.42 check
off and on, BASIC's GenData right both times (the USR-mode rule alone
stops the loss); ROBench run three times, 118 sites in use throughout, 80
used again.
Not changed, noted: write_unpatched doesn't pass back the write's C
flag; patched words in a module (RMA) saved to a file by a program stay
patched (only application space is put back).

### NativeSupport 0.42: the real cause of the lost BPUT# lines (10 Nov 2026; test version)

StubTest41 (twelve builds, SWIStubs Fresh each time, 0.41's check on and
off in turn) lost line 16 in every round, check on or off, and the check
caught nothing: not an interrupt, and not chance. Reproduced under QEMU
(robench first, so application space has patched code; then Fresh and
BASIC's GenData): the 16th BPUT# lost, every time.

The cause: on the 16th call BASIC's SWINE OS_GBPB is queued for a stub.
ns_swi_vector sends every OS_GBPB and OS_File to ns_swi_c, which (once
any application-space site exists) runs unpatch_copies over the data being
written, and that calls SWIs of its own (OS_GetEnv). Their ns_swi_vector
drains the queue, and ns_swiq_drain skips only *their* site, so the site of
the OS_GBPB on its way is given its stub. ns_swi_c returns, the kernel
reads the word before lr - now the branch to the stub - as the SWI number
(an X SWI that doesn't exist), and the write fails without a word. 0.41's
check ran only on the way into ns_swi_vector, before ns_swi_c.

- swis_fixlr (a macro): the 0.41 check, now made on the way in and again
  just before each jump to the kernel (after ns_swi_c, and on the plain
  path), so lr is moved to the stub's last word whenever the site was
  patched while its SWI was on its way, whoever patched it.
Checked under QEMU: Fresh + NoFix lost line 16; Fresh + Fix wrote all of
s.VFPData, the fix counted 1. StubTest42: six builds, Fix and NoFix.

### rol-snd: HDMI only when the screen lists sound formats (9 Nov 2026; for the next riscos-support)

Ralph Barrett on the forum (Pi 4, an HDMI monitor without speakers): no
sound from the headphone socket either. rol-snd chose HDMI whenever the
port's ELD said monitor_present 1, which a monitor without speakers also
reports; it now needs sad_count above 0 too (the screen lists at least
one sound format), otherwise the headphone socket comes first. Not yet
built or tried on a Pi. The way round meanwhile: device=Headphones in
/riscos/sound/config (IXFS:$.riscos.sound.config), then restart.

### NativeSupport 0.41: a SWI is no longer lost when its site gets a stub on the way (9 Nov 2026; test version)

StubTest40 (twelve builds, On and NoCond in turn) went wrong only in
round 1, the first build after a restart: s.VFPData lost line 16, the
16th BPUT#, the call on which BASIC's SWINE OS_GBPB (BASIC+&8E00) is
found busy and queued. 0.39's lost line 606 was the call that made 4096
(3490 in the round before + 606), when a site is queued again. The call
lost is always the one in flight as its own site is patched.

The cause: a trapped SWI goes through the ROM's SWI_handler (Linux's
SIGSYS, the vcpu still in the caller's mode with interrupts on) before
the kernel reads the SWI from the word before the return address. An
interrupt there runs RISC OS's handlers, and their SWIs drain the queue
(ns_swiq_drain skips only the site of the SWI doing the draining), so
the site of the SWI on its way can be patched before the kernel reads
it. The kernel then reads the branch to the stub as the SWI: for
&1A0A2033 that's SWI &0A2033, an X SWI that doesn't exist, so it failed
silently and the line wasn't written. Under QEMU the interrupt never
lands there.

- ns_swi_vector (before the kernel) checks the word before lr: a B or BL
  into the stub area whose stub holds a SWI means this case, and lr is
  set to the stub's last word, so the kernel reads the real SWI there
  and the SWI returns through the stub's branch to the site + 4.
  Counted: "Sites patched while their SWI was on its way, sent through
  the stub".
- For testing: *NativeSupport SWIStubs Fresh (every site back, counts
  from 0, on: stubs made again as programs run, as after start-up),
  SWIStubs NoFix and Fix (the check off and on). StubTest41: twelve
  builds, Fresh with Fix and NoFix in turn.

Checked under QEMU: stubs on with the check, Fresh remakes them, ROBench's
SWI tests.

### NativeSupport 0.40: *NativeSupport SWIStubs NoCond (9 Nov 2026; a test version, not in a package)

StubTest with 0.39 (the owner's ROM build six times: stubs Count, On,
Off, On, Count, Off) went wrong in round 2 (On): s.VFPData was 37 bytes
short, one BPUT# line ("VFP_BitFieldArgs VFP_Op_Vn, &10, 3") never
written. Count and Off were right, and no stub calls overlapped. BASIC
writes each BPUT# line with a conditional SWI (SUBS r3, ...; SWINE
OS_GBPB at BASIC+&8E00), given a stub as BNE stub. NoCond gives stubs
only to SWIs with the AL condition (conditional ones trap as before;
counted as "Conditional SWI sites left to trap"), to see whether the
conditional ones are the cause. Checked under QEMU: 322 sites given
stubs, 56 conditional ones left. StubTest40: twelve builds, On and NoCond
in turn.

### NativeSupport 0.39: SWI stub diagnostics (9 Nov 2026; a test version, not in a package)

The owner's ROM builds (5.31-21, -22) with SWI stubs on lost one line of
BASIC's s.VFPData (one BPUT#: "VFP_Op_cc EQU 2"), so the ROM had no
VFPData, and the task window's text turned blue partway through; with
*NativeSupport SWIStubs Off the same build is right. GenData run by
itself (Pi and QEMU, 2 MB and 33 MB slots, stubs on and off) is always
right. 0.39 adds two things to find which part does it:
- **Overlapping stub calls counted.** Each page pair's working words
  (the caller's lr, r0-r3, the return, the vector) are shared by its
  stubs; the common code now counts calls in progress (busy) and calls
  that start while another is (overlaps), without touching the caller's
  flags. *NativeSupport shows "Stub calls overlapping another (should be
  0)". The common code is 40 bytes longer, so stubs start at +&C0 (240 a
  page, was 244), and the working words are the data page's last 40 bytes.
- ***NativeSupport SWIStubs Count**: the fast SWI entry's counting hook
  runs (sites counted and queued) but no site gets a stub, to tell the
  hook's part from the stubs'.
Checked under QEMU: 319 ROM sites given stubs, ROBench's os tests, 0
overlaps. StubTest (the owner's ROLSrc: the ROM build six times, stubs
Count, On, Off, On, Count, Off, each BuildLog kept, then AutoBench) is
the test.

### NativeSupport 0.38: no abort copying files (9 Nov 2026; not yet in a package)

The owner copied a directory from LanMan98 to HardDisc4 with the Filer:
"Internal error, no stack for trap handler: Internal error: abort on data
transfer at &24925840" (NativeSupport+&7CAC). As a file is written,
NativeSupport looks in the data for its UDFs (patched instructions copied
as data, put back as written: unpatch_copies), and for each one checks the
word at the site it stands for. A site in application space can belong to
another program, run before with a bigger slot: its address can be past the
end of the program writing now (Filer_Action), and not mapped. The copy's
data held a word that looked like one of those UDFs, so reading the site
aborted. Now a site past the current program's end (OS_GetEnv) is skipped.
An old bug (since 0.23), not from 0.34-0.37.

### NativeSupport 0.37: UnixLib's _swix no longer traps; more never-run instructions (9 Nov 2026; not yet in a package)

From the owner's AutoBench run on a Pi 4 (5.31-20, 0.36, six ROBench runs):
- **_swix's MRS.** One site in ROBench's application space trapped 7.6
  million times a run: UnixLib's _swix, after its SWI, reads the flags the
  way that works on 26- and 32-bit processors: "mov ip, #0; mrs ip, CPSR;
  teq ip, #0; ...; andne ip, ip, #&F0000000". NativeSupport patches every
  MRS CPSR in application space to a UDF (so code in a privileged mode
  reads RISC OS's mode, not Linux's), a signal at each _swix. Only the
  flags are kept there, and the processor's own flags are right in any
  mode, so that pattern (MOV Rd, #0 before, TEQ Rd, #0 after, AND Rd, Rd,
  #&F0000000 within 6) is now left to run. *NativeSupport counts them.
  Under QEMU: ROBench os.readvar (OS_ReadVarVal through _swix) 28,000 a
  second with 0.36, 155,000 with 0.37.
- **More never-run instructions.** The never-run traps fell from 5 million
  to 980,000 with 0.36; almost all those left (ARMEABISupport's, 980,028,
  and IXSupport's, 267) are "teq pc, pc; ldmne sp!, {...}; orrnes pc, lr,
  #V_bit": the privileged one is the second NE instruction after the TEQ.
  Now up to two NE instructions that leave the flags and pc alone may come
  between (in the ROM, 103 places instead of 39).
Checked natively under QEMU (5.31-20): 109 made NOPs at start-up, the
HardDisc4 apps start, Iris loads a page over HTTP with its JavaScript,
CSS and images, no faults.

### NativeSupport 0.36: only in 32-bit code (9 Nov 2026; not yet in a package)

0.35 changed the never-run instruction in any module, and in any code
synchronised outside application space. A 26-bit module (Aemulor runs them,
emulating a 26-bit processor, where TEQ pc, pc leaves Z clear and the
LDMNE ^ is the way out) or code made at run time (Aemulor's, a JIT's)
could have been changed too. Now only in the ROM and in modules whose
flags word says they're 32-bit; one that traps all the same is changed
only in the ROM. Checked natively under QEMU: still 44 (the RMA's modules
are all 32-bit), the desktop runs. 0.35 went only into the ROMTest20 kit.

### NativeSupport 0.35: no more traps from a never-run instruction (9 Nov 2026; not yet in a package)

The owner's Pi 4 report on 5.31-20 (35 minutes' use): 5 million traps
(about 2,400 a second, each a signal costing about 5 us) on instructions
whose condition failed, all at "TEQ pc, pc; LDMNE sp!, {..., pc}^" in the
SWI and service exits of IXSupport (862,000), BufferManager (726,000) and
the Toolbox modules (Window, Menu, Iconbar, DCS: 600,000 each). The test
tells 26-bit from 32-bit: on a 32-bit processor TEQ pc, pc always sets Z,
so the LDMNE never runs, but the Pi's Cortex-A72 (like the A78C and X1C)
raises "undefined instruction" for it anyway, and NativeSupport skipped it
each time. A privileged instruction with condition NE straight after TEQ
pc, pc is now made a NOP (MOV r0, r0), so it never traps: in the ROM as
NativeSupport starts, in modules as their code is patched (at start and as
they're loaded), and any that still traps the first time it does. Only
outside application space, as with the other stubs. *NativeSupport counts
them; NativeSupport$NoCondNOP set before it loads leaves them as they were.
Checked natively under QEMU (which doesn't trap them, so the count of traps
can only be seen on a Pi): 44 made NOPs (39 in the ROM, 5 in RMA modules),
IXSupport's, BufferManager's and Window's read back as &E1A00000, the
desktop and the SWI benchmark run as before.

### NativeSupport 0.34: SWIs without a trap (9 Nov 2026; needs RISC OS 5.31-20 or later, not yet in a package)

The busiest SWIs in the ROM are made without a trap to Linux, a second go
at 5.31-6's fast SWI (that one pushed registers on the caller's stack,
which broke code with no room on it). With a ROM that has PVirt_SWIStubs
(5.31-20 onwards), NativeSupport finds 64 KB of the ROM's unused fill (all
&FF) within branch reach of all the ROM's code and puts its stubs there.
The fast SWI entry's handler counts the SWIs trapped at each ROM site;
after 16, the site's SWI is replaced by a branch to a stub of its own,
which switches to SVC mode the way the HAL does and enters the SWI handler
with the return address after the site. OS_WriteS, OS_EnterOS and
OS_BreakPt are left alone (they read or change what follows the SWI or the
mode). NativeSupport's FileSwitch checks see a stub's return address as
its site's. *NativeSupport SWIStubs On|Off; NativeSupport$NoSWIStubs set
before loading leaves it off; *NativeSupport lists the counts and the
sites still trapping most.
- Each stub's working words are in a page of their own next to its code:
  QEMU throws away a page's translated code at every write to it, which at
  first made a SWI three times as slow there.
- *NativeSupport SWIStubs On puts the stubs made before back at once.
Checked natively under QEMU on the 1.0.0020 image with 5.31-19 patched to
have PVirt_SWIStubs: 320 sites given stubs, the desktop runs; BASIC, 3 runs
each, stubs on against off: SYS OS_ReadMonotonicTime 26.1 against 31.1 µs,
SYS ColourTrans_SetGCOL 33.8 against 49.3 µs (QEMU makes a trap cheap, so
a Pi should gain more).

## 1.0 (2026-10-21 to 2026-11-08; images 1.0.0020, the first public release)

### 8 Nov 2026: images 1.0.0020

The images rebuilt from 1.0.0019's with riscos-rom 5.31-19, riscos-support
1.0.0037-1 (NativeSupport 0.33, rol-fb with the relative mouse) and
riscos-harddisc 1.0.0038-1 (`images/rebuild/mk*20.sh`). 1.0.0019 was never
published; 1.0.0020 is the first public release (RELEASE-NOTES.md).
Checked under QEMU: the generic image and the Dev Kit image's root reach
the desktop with RISC OS 5.31-19 and NativeSupport 0.33, natively.

From a test bed to RISC OS on Linux's images: the Linux side, the desktop
parts, and natively on real machines. What users get from this comes in
the images (1.0.0001 onwards) and through Updates (riscos-rolpkg's
CHANGELOG lists each package).

### 8 Nov 2026: RISC OS 5.31-19 and riscos-support 1.0.0037-1 (the mouse speed and 4K in 16 million colours, released)

- **RISC OS 5.31-19** (the owner's DDE build of rol/src 44da7369c) is
  packaged: the mouse speed applies to a mouse, the screen can be up to
  64 MB (4K and 5K in 16 million colours), programs moving the pointer move
  it on screen, a Thumb program's undefined instruction is reported at the
  right address. Checked under QEMU at 3840 x 2160 with a USB mouse and
  with a tablet; Iris 1.039 draws bbc.co.uk.
- **rol-fb** (riscos-support 1.0.0037-1): the pointer is drawn where RISC
  OS has it from the start, not only after the mouse first moves, and
  stays right when its shape changes.

### 8 Nov 2026: NativeSupport 0.32 and 0.33, making or removing a dynamic area no longer stops the desktop (riscos-support 1.0.0035-1, 1.0.0036-1)

- **The bug:** from NativeSupport 0.30 (riscos-support 1.0.0031-1), once
  a program had made a dynamic area, RISC OS could abort in NativeSupport
  at every address check, and the desktop stopped. EDT and crossdev both
  reported it.
- **The cause:** the PMP area nodes NativeSupport finds in the system
  heap could be a heap block that only looked like one.
- **The fix:** each one is now checked with OS_DynamicArea 2.
- **Also, for RISC OS 5.31-19:** an undefined instruction in Thumb code
  gives lr = the instruction + 2, as on a real ARM.

### 8 Nov 2026: the mouse speed, and 4K in 16 million colours (riscos-support 1.0.0034-1, riscos-harddisc 1.0.0038-1; RISC OS 5.31-19 to be built)

From Chris's report: on a Pi 5 with a 4K monitor, Configure's mouse
speed did nothing, the pointer was slow, and 4K had only 32 thousand
colours.
- **rol-fb 3:**
  - with RISC OS 5.31-19, a mouse's movement goes to RISC OS as relative
    moves, so the mouse speed applies;
  - the pointer is drawn where RISC OS has it;
  - tablets (UTM) and older ROMs carry on as before;
  - the Pi's monitor definition lists modes up to 64MB.
- **The UTM monitor definition** is kept in `harddisc/extras` now, with
  4K and 5K modes.
- **RISC OS 5.31-19** (the `nightly-merge` source; built with the DDE):
  - SocketKVM takes relative moves and tells the front end where the
    pointer is (protocol version 3);
  - the screen memory can be 64MB (was 16MB).
  - Checked here with 5.31-18 binary-patched to 64MB: 3840 x 2160 in 16
    million colours.

### 8 Nov 2026: riscos-support 1.0.0033-1 (the pointer on a 4K monitor)

- rol-fb scales the pointer with the desktop, in whole multiples. On a Pi
  with a 4K monitor, which shows the 1920 x 1080 desktop twice the size,
  the pointer was half the size of everything else. Images 1.0.0019 have
  1.0.0032-1; Updates brings this.

### 8 Nov 2026: images 1.0.0019, the first public release

- **The images rebuilt with the newest of everything:** RISC OS 5.31-18,
  riscos-support 1.0.0032-1 (NativeSupport 0.31), riscos-harddisc
  1.0.0037-1, and kernels 7.2.0-4 (generic and Dev Kit) and 6.6.78-6 (Pi).
  - They're made from the published 1.0.0018 images, without rebuilding
    Debian (`images/rebuild/`). The packages are put in as dpkg would, and
    their postinsts' work is done against the image. `/riscos` and
    HardDisc4's own directory now belong to RISC OS on the generic image
    too, as on the Dev Kit's.
  - The generic image boots to the desktop in QEMU (UEFI, Cortex-A76,
    natively) with the versions above, and the Dev Kit image's root boots
    with its own kernel.
  - A 1.0.0018 image updated through Configure → Updates from a copy of
    the repository gets the same versions, and Roll back is then offered.
- **RELEASE-NOTES.md** is 1.0.0019's:
  - the full history since the test bed began;
  - screenshots of what RISC OS on Linux adds to RISC OS
    (`docs/screenshots/1.0.0019`).
- **The `riscos-5.31-source` branch has RISC OS 5.31-18:** the HAL's
  GetVCPU/get_tls interrupt fix and the kernel keeping SPSR_svc.
- `images/README-*.txt` for 1.0.0019.

### 8 Nov 2026: the numbers RISC OS Open allocated (not yet packaged)

| What | Now | Was |
|---|---|---|
| NativeSupport_PMPOp, NativeSupport_AbortReturn (NativeSupport 0.31 has a SWI chunk with names) | &5AF40, &5AF41 | &C0002, &C0003 |
| LinuxCodec_Open … LinuxCodec_OverlayHide | &5AF00 … &5AF08 | &5A440 … &5A448 |
| Errors of LinuxAudio, LinuxCodec (with VCHIQ, BCMSupport, PCI), LinuxShell, NativeSupport | &8235xx, &8236xx, &8237xx, &8238xx | &81A0–1, &81A30–62, &81A70–77, &81A8–A |

- **The QEMU patch** answers NativeSupport's new SWI numbers, as the lite ARMEABISupport now uses them. It also gains 0.28's OS_Memory 24 / OS_ValidateAddress item, which this copy didn't have yet.

### 7 Nov 2026: riscos-support 1.0.0031-1 (NativeSupport 0.23 to 0.30)

- **NativeSupport 0.30: Thumb-2 code isn't patched as ARM.**
  - **The Iris crash:** a word that is also a Thumb-2 32-bit branch (`B<c>.W`, `B.W`, `BL`) is no longer taken for a conditional `MSR CPSR`. JavaScriptCore's interpreter in Iris 1.039 has two: `beq.w` after a call, `F000 812D`, read as `MSRHI CPSR_fsc, r0`. Patched to a UDF, the web process went to "throw" with no exception pending and aborted on bbc.co.uk.
  - **The neighbour rule:** a same-condition neighbour must be an MSR that sets the control field. Without this, a libwebkit2gtk `.rodata` table and NetSurf data were patched.
  - **The 5.31 ROM's PMP areas:** JIT code in the ROM's own `mmap#` areas is left alone. Before, MiniBrowser's links were dead with the JIT on.
- **NativeSupport 0.28: OS_Memory 24 and OS_ValidateAddress answer for PMP areas page by page**, as the kernel's CMA_AddRange does. Shared library code and thread stacks can now be checked (EDT); unmapped pages aren't accessible.
- **NativeSupport 0.27:**
  - **Thumb-2 IT state:** a return into a Thumb-2 IT block keeps its IT state, on all four routes back to Thumb code. This was WebKit's random crashes.
  - **SWIs inside the SIGILL handler** run with SIGILL, SIGSEGV and SIGBUS let in. EDT starts.
- **NativeSupport 0.23 to 0.26: the DDE's ROM build comes out unpatched.**
  - A program's own synchronisation of its data is left.
  - Patched words a program writes to a file are put back.
  - `MRS` in application space is a UDF site, not a branch to a stub.

### 6 Nov 2026: riscos-rom 5.31-18, riscos-support 1.0.0029-1, riscos-harddisc 1.0.0035-1, riscos-kernel-wdk2023 7.2.0-4

- **NativeSupport 0.21: programs over 64 MB are patched.** A program's own
  image (its AIF read-only and read-write areas) is patched whatever its
  size, not only up to 64 MB: the web browser (105 MB) wasn't, so
  UnixLib's `MSR CPSR_c` back to user mode did nothing and the desktop
  stopped. A synchronisation of part of the program's own image is patched
  too, but only while that program is running (the current active object
  is &8000): under BASIC, &8000 can still hold the last program's header.
  0.20's rule for the rest of application space stays (the DDE's buffers).
- **NativeSupport 0.22: code BASIC assembles is patched again.** 0.20 left
  every synchronisation in application space that didn't start a program
  (for the DDE's buffers), so Iris's AlignExOff (OS_EnterOS, MRC,
  `MSR CPSR_c,#&10` in a DIM block) stayed in SVC mode and Iris 1.0.29
  stopped at its banner (Chris's Pi 5). Now only FileSwitch's are left (a
  file loaded with OS_File's code flag: the DDE's case); the SWI vector
  notes who called OS_SynchroniseCodeAreas.
- **SPSR_svc is kept across interrupts.** The kernel's IRQ entry (the
  Linux build, 5.31-18) keeps SPSR_svc as it kept r14_svc: interrupt
  code's SWIs (CallEvery routines, ScreenBlanker's ticker) wrote it, so
  SVC mode code between setting SPSR_svc and `MOVS pc` came back in SVC
  mode with IRQs off; natively each of those instructions is a trap, and
  0.2% of such pairs made with IRQs on went wrong (UnixLib's mode changes).
  LinuxAudio's, USBDriver's and SCSILinux's ticker routines also keep it
  themselves (for older ROMs). Code should still keep IRQs off between the
  two, as RISC OS's own does.
- **rol-fb: Configure → Screen shows the monitor and the mode.** With a
  real monitor (`-n`), a monitor definition file from Linux's modes and
  the EDID's name (Monitors.Linux.Screen); each start writes the start-up
  mode file the way Configure does (a plain `LoadModeFile` and `WimpMode`
  with the frame rate), only while it's rol-fb's own, never once Configure
  has saved one.
- **riscos-rom 5.31-18** also stops the occasional stall on the start-up
  screen (an IRQ landing in `__PVirt_GetVCPU`).

### Native: the fast FPA entry (5 Nov 2026; riscos-support 1.0.0028-1, riscos-kernel-pi 6.6.78-6, riscos-kernel-generic 7.2.0-4)

- **FPA instructions (coprocessors 1 and 2: FPEmulator's) go to RISC OS's
  undefined instruction vector straight from Linux's trap**, instead of
  through a SIGILL, NativeSupport's and the port's handlers and a
  sigreturn. The kernel side, `kernel/*/0201-riscos-fast-fpa.patch` (on
  top of 0200): prctl PR_RISCOS_UND (0x524f5302) names the handler; the
  kernel skips an instruction whose condition fails, otherwise writes a
  SIGILL-shaped frame (siginfo, r0-r14, pc, CPSR) on the signal stack,
  sets the I bit of RISC OS's virtual CPU (the TLS block's byte 13, the
  old byte in si_errno) and jumps to the handler. Setting the I bit, as a
  real undefined instruction exception does, means an interrupt signal
  arriving in the handler is deferred by the port, as one in its SWI
  handler is (taken there, the RISC OS IRQ code's SWIs could overwrite
  the frame). The RISC OS side is NativeSupport 0.19's `ns_und_fast`
  fast entry` (riscos-linux-testbed): the port's ENTER_EXCEPTION(UND)
  done in place, then vectors[0]. `*NativeSupport FastFPA On|Off`,
  `NativeSupport$NoFastFPA`; other kernels and ROMs: the usual path.
- Tested natively (QEMU's Cortex-A76, the generic 7.2 kernel, the
  1.0.0018 image): testbed `tests/programs/fpa.bas` (FPA arithmetic,
  conversions, compares, conditions passing and failing, SFM/LFM, on 16
  awkward values) gives the same sum (&A4907357, as QEMU's own FPA
  emulation does) with the fast entry off and on, twice each; 500,000
  FPA instructions took 150 and 153 cs off, 60 and 65 cs on (2.4 times
  as fast under QEMU's emulation). On a Raspberry Pi 4 (6.6.78-6): the
  same sum, 27 cs off and 6 cs on, 4.5 times as fast (FPEmulator emulates
  a run of FPA instructions per trap: about 5 µs a trap through the
  signal, 1 µs through the fast entry).

### QEMU: cheaper TB lookups (4 Nov 2026)

- The VFP state the translated code depends on (short-vector length and
  stride, FPEXC.EN, the inline floating point's FZ and rounding mode) is
  kept with QEMU's cached TB flags, rebuilt when FPSCR or FPEXC is
  written, instead of being worked out at every jump through a register
  or return (`qemu/` patch item 13). Checked with QEMU's own debug build,
  which compares the cached flags at every lookup, over the whole suite.
- Direct jumps are chained across pages too (QEMU only chains within a
  page, for system emulation's sake; in user mode a page's code is
  invalidated, and the jumps into it unlinked, when it's unmapped,
  loses execute permission or is written). `RISCOS_NO_XPAGE_CHAIN=1`
  turns it off.
- FFmpeg's H.264 decoding (720p, one thread) 7.19 s -> 5.65 s (21%);
  ROBench: SWI calls 1.26x, LZ77 compress 1.37x (1.5x on an AArch64
  host), the rest within noise.

### QEMU: NEON integer inline (4 Nov 2026)

- The test bed's QEMU no longer calls a helper per lane for NEON's long
  and wide arithmetic (VADDL/W, VSUBL/W, VMULL, VMLAL, VMLSL, VABDL,
  VABAL), narrowing shifts (VSHRN, VQSHRN, VQRSHRUN and the rest; QC
  too), VSHLL/VMOVL, 16-bit multiply by scalar, VZIP/VUZP, and 8- and
  16-bit VLD2/VLD4/VST2/VST4: they're host vector instructions on x86-64
  and AArch64, through new TCG vector ops (`qemu/` patch item 12).
  ROBench neon: blend 3.4x, yuv2rgb 5.6x, sad 2.1x on x86-64 (2.3x,
  4.5x, 1.6x on an AArch64 host); FFmpeg's H.264 decoding 9% faster.
  `RISCOS_NO_INLINENEON=1` turns it off.
- Tests: `t_neonperm` (236 forms, `tests/gen-neonperm.py`); `t_neonint`
  and `t_neonperm` give the same CRCs with it, without it, and with
  QEMU's exact emulation, on both hosts.

### Running natively (NativeSupport 0.11 to 0.17)

- User-bank LDM/STM ^ (also next to an MRS already stubbed, conditional
  and single-register), returns to Thumb code (Iris's JavaScript), CP15
  barriers in user mode, squeezed programs patched once unsqueezed, abort
  handling that no longer loops or ends RISC OS on a bad stack.
- JIT code left alone; Reporter's SWIs ignored without Reporter;
  `*NativeSupport` lists the busiest patched sites, dynamic areas and free
  address space; the kernel's sites found by their code.
- ARMEABISupport 1.05's mmaps over 100MB; dynamic areas created without
  the Abortable flag; the kernel does physical memory pools itself from
  RISC OS 5.31-3.
- **0.16: the fast SWI entry.** With riscos-linux kernels that have it
  (`kernel/*/0200-riscos-fast-swi.patch`), RISC OS's SWIs go straight to
  the ROM's handler instead of through seccomp and a signal: about 2.4 µs
  down to 745 ns on a Raspberry Pi 4. `*NativeSupport FastSWI On|Off`,
  `NativeSupport$NoFastSWI`. Other kernels: the usual path.
  On a Raspberry Pi 4: ROBench's OS_SWI 3.3 times faster, OS_ReadVarVal
  2.4, BPUT 4.2, BGET 2.0, Draw strokes 2.8. It needs no change to the
  ROM. (ROL-ROM's CHANGELOG has the whole story, including the ROM-side
  patching it replaced.)
- **0.17: counting traps.** `*NativeSupport Reset` starts the counts again
  and the report says how many seconds they cover; each busy place is
  named by its module (`SoundScheduler+&628`); traps of instructions whose
  condition failed (which some processors, the Pi 4's Cortex-A72 among
  them, still trap) are listed by place, as other traps are. For finding
  what costs RISC OS on Linux time in a program (RiscOSM's map drawing).
- **0.18: returns to user mode without a signal.** The kernel returns to
  user mode through the port's LoadUsrRegisters; NativeSupport had made
  its first instruction trap (to fix up returns to Thumb code), so every
  return cost a signal: 2.3 million in a RiscOSM map draw, about 10 s on
  a Pi 4 (RiscOSM took 30 s, 17 s on native RISC OS). It now branches to
  a 7-instruction stub that does the same fix-up, in the ROM image's
  unused fill near its end (the trap is kept if there is none).
  `*NativeSupport` says which.

### Tests and tools (3 Nov 2026)

- `ixfs-freespace`: IXFS's free space (OS_FSControl 49 and 55), which
  failed before RISC OS 5.31-11 (StrongEd couldn't save to IXFS).
- `select-eof` (`t_eof`): the other end's close seen by `select()` and
  `read()`, which before RISC OS 5.31-12 waited out the timeout instead
  (ClawsMail's HTTP fetches took 60 s each; from riscos-gtk's handoff).
- USB discs: Eject retries for a second if the disc is busy, and if it's
  still in use (a file on it open) rol-usbd says so (`busy-NAME` in
  /riscos/mediactl) and !DiscIcon reports it, instead of nothing
  happening. !DiscIcon lists another directory before ejecting (IXFS keeps
  the directory it listed last open, which kept the disc busy), and a
  disc's directory windows close when it goes, ejected or pulled out.
- Updates: a Changes button under the RISC OS on Linux packages opens a
  window of what each version newer than the one installed changes (and
  what the installed one changed), from rol-info's `@changes` (the
  repository's changes.txt, fetched by Check for updates).
- `*IXRun` in protected RISC OS (where it always failed with ENOENT: no
  Linux programs or terminals in RISC OS's own view of the files) is an
  alias for LinuxShell 0.03's new `*LinuxUser`, which has rol-cmd run the
  command as RISC OS's own Linux user (riscos; rol-cmd's `@user` request:
  no password, no_new_privs, starts in HardDisc4), always: through the
  shell, so `*IXRun ls` works as well as `*IXRun /bin/ls` (IXSupport's own
  doesn't search the path, and needs Linux programs and a terminal in
  RISC OS's view).
- rol-kvm `-c` may name a regular file, followed as it grows, for
  tools/native-vm's VM (a FIFO can't be shared with it).

### The images' Linux side (images/fe)

- init: Debian 13 base; protected RISC OS (its own user and view of the
  files); the root file system fills the disc; logs on the FAT partition;
  NTP all the time and the time kept across starts; RISC OS's RAM from
  what Linux has free (`ROL_RAMSIZE_MB`); a start-up screen; devpts;
  restart restarts the machine; shutdown and restart leave the disc clean.
- rol-fb: the screen at the monitor's size (a mode saved in Configure
  kept); only what changed is redrawn; a clear hardware cursor (UTM);
  Alt+Shift+Backspace as Break; **video overlays** (LinuxCodec's and
  VideoOverlay's buffers on the display's video plane, under the desktop:
  on the Pi the desktop goes on a second plane over the video's, so the
  pointer, windows and menus stay on top); the keyboard LEDs as RISC OS
  has them (with SocketKVM's `c_leds`, RISC OS 5.31-9).
- rol-cmd and `*Linux` (LinuxShell) with a root password; output
  line-buffered for Updates.
- rol-usbd: RISC OS's USB device interface (USBDriver) for devices Linux
  has no driver for (PDFS); **USB discs and SD cards** mounted in
  `/riscos/media` (FAT, exFAT, NTFS, ext), with files not marked
  executable; a disc it can't mount described in the kernel log;
  `/riscos/mediactl/usb` for `*USBDevices`.

### Modules and the desktop (harddisc, modules)

- LinuxCodec 0.01 to 0.03: Linux's V4L2 video decoders (the Pi's H.264)
  for RISC OS programs.
- The VideoCore stand-ins (VCHIQ, BCMSupport, PCI) over `/dev/vchiq`, for
  ReelHWAccel.
- LinuxOverlay: VideoOverlay's GraphicsV calls (20-28) on rol-fb's
  overlay plane: Reel's hardware overlay.
- USBDriver: `*USBDevices` also lists Linux's own USB devices.
- !DiscIcon: HardDisc4 on the icon bar (Free, drag to copy), USB discs and
  cards with the removable drive icon.
- Configure: Updates (apt from the desktop, roll back), Linux network,
  Scroll wheel direction (KVMWheel).
- NetWatch: name servers follow the network.
- PackMan set up with riscos-packages and riscosdev.com's list.
- MbufManager (with MbufRes) for RISC OS before 5.31-7; GCCSDK's
  ARMEABISupport 1.05 installed (1.08 isn't stable on RISC OS on Linux);
  Iconv 0.91.

### FileCore discs (with RISC OS 5.31-10)

- rol-usbd recognises a disc or partition in RISC OS's FileCore format
  (the boot block's checksum and disc record), leaves it unmounted, lists
  it in `/riscos/mediactl/filecore` and hands it out, opened exclusively
  (`O_EXCL`, so Linux can't mount it meanwhile) with `O_DSYNC`, on the
  abstract socket `rol-usbd-discs`.
  Only USB discs, removable cards and SD cards in the machine's own slot
  (by the card's MMC type), never internal discs (eMMC, SATA, NVMe). A disc RISC OS
  set up (FileCore across the whole disc, even with a partition table: a
  Pi card or SSD with its FAT Loader inside) is given whole, none of its
  partitions mounted. The check also wants FileCore's ID length and a
  size that fits the disc (a FAT image is rejected).
- SCSILinux: the SCSISwitch driver (in place of SCSISoftUSB) that makes
  each one a SCSI device for RISC OS Open's SCSIFS and SCSIFiler, doing
  their commands on the disc (reads and writes of its sectors, its size,
  INQUIRY, sense data). `SCSILinux$Test` names a disc image file to use as
  a disc, for testing. One unit (LUN 0) per disc. A disc is removable (`SCSI::0` on) or fixed (`SCSI::4` on) as Linux says.
- `tests/filecore.sh`: HForm formats a disc image through SCSIFS and
  SCSILinux; a file and a directory copied on and back must be identical
  (contents and types); `*Verify`; a second boot reads again.

### Tests

- `t_select` (Socket_Select's timeout, 5.31-4), `t_sockopt` (5.31-6),
  `t_accept` (data sent before Socket_Accept, 5.31-7), `t_mimetype`
  (types from extensions, 5.31-8).

## 0.10 (2026-10-18)

Running natively: on a processor that runs 32-bit ARM Linux programs (a
Raspberry Pi 4 or 5), without QEMU, with what the test bed's QEMU adds.

- New module `NativeSupport` (C on the module framework; loaded first by
  `rol-run`, does nothing under QEMU). It patches the privileged PSR
  instructions in code loaded from disc (at Service_ModulePreInit and at
  each code synchronisation, through the kernel's cacheflush calls:
  application space, the RMA and dynamic areas, where SOManager puts
  shared libraries) into
  undefined instructions that its SIGILL handler emulates with the port's
  virtual CPU modes; `MRS Rd, CPSR` goes to a stub without a signal.
  Other privileged instructions are emulated when they trap (CP15
  including DFAR/DFSR/MIDR, SPSR, LDM/STM ^, `<op>S pc`, SWP, cache
  maintenance by address). The emulations run under a fault probe: a bad
  address fails the emulation instead of killing RISC OS.
- PMP dynamic areas (the kernel's PhysOp/LogOp/Resize/GetPages), OS_Memory
  0, OS_DynamicArea 22 and SWIs &C0002/&C0003 through the SWI vector, so
  both ARMEABISupports work; the data abort address and status; IXFS
  directory reads without gaps; the sound modules patched in the ROM.
- Whole-cache synchronisations (do_IMB_Full) are recognised: only the
  RMA is scanned for code then (code put there that isn't a module, as
  vector handlers are), not all of memory.
- VFPSupport: 32 D registers when Linux reports NEON (an arm64 kernel's
  32-bit HWCAP has no VFPD32).
- `ROL_VM=tools/native-vm/vmexec` runs `rol-run` (and `tests/run.sh`)
  natively in a virtual Cortex-A72 (`qemu-system-aarch64`), for checking
  on x86 or a Mac. All checks pass there (49, two FIFO ones skipped), and
  all 51 under QEMU.
- Iris (with GCCSDK's ARMEABISupport 1.08 and its shared libraries)
  starts natively.
- The Pi image runs the port natively; the UTM image (Apple Silicon has
  no AArch32) keeps QEMU.

## 0.9 (2026-10-18)

Floating point and NEON floating point inline: QEMU no longer calls a
helper for each VFP or NEON float instruction when the host FPU can be
shown to give the guest's exact result (`qemu/` patch item 11).

- New TCG vector opcodes (x86 AVX and aarch64 back ends): add, subtract,
  multiply, divide, square root, int/float conversions on float lanes, a
  32-bit store from a vector register, and two 64-bit halves joined. The
  ARM front end (`target/arm/tcg/riscos-fp.h`) uses them for VADD, VSUB,
  VMUL, VNMUL, VDIV, VSQRT, VMLA, VMLS, VNMLA, VNMLS (single and double),
  and NEON's VADD, VSUB, VMUL, VMLA, VMLS (also by scalar).
- A fast path is used only when its result is the guest's: the guest's
  inexact flag already set (an FPSCR write now sets it for NEON too),
  round to nearest, no denormal input where FZ would flush it, and each
  result a finite normal number (or an exact zero). Anything else runs
  the old helper, from code at the end of the translation block, so the
  fast paths run straight through from one instruction to the next.
- The VFP FZ bit and the rounding mode are part of a translation block's
  key, so "RunFast" programs (FZ set) get their own inline code, and code
  under another rounding mode the helpers as before. Code translated
  before a program's first inexact result keeps its slow paths inline
  (exact-only arithmetic doesn't leave the block on every instruction).
- Done with integer instructions: VCMP/VCMPE, VABS, VNEG, the VFP
  conversions (integer to float, single to double and back, float to
  integer toward zero), and NEON's compares (also with zero), VMAX, VMIN,
  VABD and float/integer conversions.
- NEON Q operands are loaded as their two D halves: VLD1 writes a Q
  register as two 64-bit stores, which a 128-bit load can't take from
  the store buffer (ROBench FIR 1.7x on its own).
- QEMU bug fixed on the way: its optimizer aborted on `0 - constant` for
  vectors on hosts with a vector negate (aarch64).
- Tests: `t_fpconform` covers more (NEON compares with zero, VACGT,
  64-bit NEON forms, VMUL/VMLS by scalar, VCMP with #0, VCVT.F64.U32,
  integer limits; CRC now c7541c16, 20.3 million results) and runs with
  QEMU's exact soft float, its host FPU helpers and the inline code;
  new `t_fpchain` runs long generated blocks of VFP and NEON
  instructions that feed each other, in ARM and Thumb (IT blocks), in 16
  FPSCR modes (CRC ce555057), and `t_fpchain_long`, blocks long enough
  for TCG to run out of temps and translate them again. 51 checks. Also checked under a
  `--enable-debug-tcg` build, and with two independent code reviews.
- A TB has at most 16 inline FP instructions: a slow path taken goes on
  in a new TB from the next instruction, so long straight-line FP code
  (with NaNs, say) would otherwise be translated again from each one.
- `RISCOS_NO_INLINEFP=1` turns the inline code off.
- ROBench on the Mac Studio's Linux VM (aarch64; release builds, LTO),
  against 0.8.1: Whetstone 1.4x, LINPACK 1.09x, FFT 1.3x, N-body 1.49x,
  Mandelbrot 1.13x, matrix multiply 1.26x; NEON saxpy 1.85x, FIR 1.71x,
  4x4 transform 2.35x. NEON integer code is unchanged.

## 0.8.1 (2026-10-18)

- SoundControl had the wrong SWI chunk (&51400): it is SoundCtrl's &50000,
  so Configure's Sound plug-in ("SWI &50000 not known") works. New check
  `soundctrl` calls the SWIs by name (45 checks).

## 0.8 (2026-10-18)

Sound. The port's ROM has RISC OS Open's SoundDMA (1.88, the HAL
version) and the sound modules above it, but no audio hardware, so it
never had sound. Now RISC OS Open's own modules make the sound and the
test bed passes it to Linux:

- `modules/LinuxAudio`: a HAL audio controller device (API 3, synchronous
  custom DMA) and mixer for SoundDMA, sending each buffer to a Linux FIFO
  (paced by its reader; dropped at the sample rate with none). `rol-run`
  loads it and restarts the ROM's SoundDMA, which finds it.
- `modules/SoundControl`: RISC OS Open's SoundControl 1.06 from source
  (the mixer interface: SoundCtrl SWIs, *MixVolume). SharedSound 1.20
  comes from the Pi ROM (`basicvfp/fetch.sh`), its Messages from
  LinuxAudio.
- `qemu/`: the ROM's sound modules change mode with plain MSRs, which
  Linux ignores (SoundScheduler crashed on its first event): QEMU emulates
  them, as it does for code from disc (`RISCOS_PSR_ROM_MODULES`).
  OS_Memory 0 gives the logical address for addresses outside application
  space, where the port refused (SoundDMA asks when it starts).
- `ROL_SOUND_WAV=FILE` saves the sound as a WAV file
  (`tools/rol-sndcap.py`); `ROL_SOUND=silent` keeps the silent stand-in.
- Tests: `sound-out` (a 440 Hz note panned left, a linear handler's output,
  a scheduled note, checked in the WAV file), `sounddma` now on the real
  SoundDMA (8 sample rates), `sounddma-silent`. 44 checks.

## 0.7.1 (2026-10-18)

- `modules/WindowScroll`: RISC OS Open's own WindowScroll 0.03 (source
  from gitlab.riscosopen.org, BSD-3-Clause, unchanged in `rool/`) built on
  the test bed's C module framework, with the pieces of the C library,
  RISC_OSLib and CMHG it needs (`port/`). It replaces 0.7's WheelScroll
  task: its task starts with the desktop (Service_StartWimp), it scrolls
  smoothly, follows panes to their parents, sends extended and line scroll
  requests where windows ask for them, and *WimpScroll sets the scroll
  type, line scrolling and speed as on a Pi. `tests/desktop-wheel.sh`
  tests it end to end.

## 0.7 (2026-10-18)

Floating point and NEON: the instructions go to the host's FPU and
vector unit where the answer can't differ from QEMU's exact emulation
(ROBench 1.01, which adds NEON tests). On an Apple Silicon Mac (the UTM
image): floating point 1.1-1.4 times as fast, NEON 1.9-4.7 times (FIR
filter 2.7x, 4x4 transforms 4.7x, YUV to RGB 2.0x); on x86 FP 1.2-1.4x,
NEON 1.3-3.7x. Before this, NEON code ran slower than the same code in
plain C under QEMU.

- `qemu/`: a host FPU fast path for add, subtract, multiply, divide and
  square root, single and double. The result is kept when it's a finite
  normal number (or an exact zero); the inexact flag is worked out
  exactly (TwoSum, exact products) when it isn't set yet. QEMU's own
  hardfloat only ran once inexact was already set, so exact arithmetic
  (integers held in floats) always took the soft path. NEON's four float
  lanes (VADD, VSUB, VMUL, VMLA, VMLS, by scalar too) go through the host's
  vector unit in one helper call. `RISCOS_NO_HOSTFP` turns it off.
- `qemu/`: VMRS APSR_nzcv (after every VCMP) sets the flags directly,
  without helper_cpsr_write.
- `qemu/`: one helper per instruction, vectorised by the host compiler,
  for the NEON 8- and 16-bit long ops (VADDL/VSUBL/VADDW/VSUBW, VABDL,
  VABAL, VMULL, VMLAL, VMLSL), narrowing shifts (VSHRN, VRSHRN, VQSHRUN,
  VQRSHRUN, VQSHRN, VQRSHRN), VMOVL/VSHLL and .I16 by-scalar multiplies;
  QEMU made two to six calls of 32 or 64 bits each.
  `RISCOS_NO_HOSTNEON` turns it off.
- `tests/programs/t_fpconform`: 14.4 million results (76 instructions x
  16 FPSCR modes, special values) and `t_neonint` (324 NEON integer
  instructions, every shift count), as CRCs; the same on x86-64 and
  aarch64 hosts, and the same as QEMU's exact emulation.
  (`tests/gen-neonint.py` writes t_neonint.c.)
- `modules/KVMWheel/WheelScroll`: the scroll wheel now scrolls windows.
  KVMWheel only told the kernel; scrolling windows is the job of RISC OS
  5's WindowScroll module, which the port's ROM doesn't have (the Pi
  ROM's copy is linked to its ROM's C library, so can't be loaded from
  disc). WheelScroll is a small task that does it: a Scroll_Request to
  windows that ask for them, otherwise an Open_Window_Request with the
  scroll offset moved. `tests/desktop-wheel.sh` checks it end to end.

## 0.6 (2026-10-18)

QEMU speed, profiled with perf while ROBench ran (the RISC OS benchmark,
riscos-robench). On the x86 test bed, against 0.5.1: SWIs 4.8 times as
fast, OS_ReadVarVal 2.6, byte-stream files 2.4-2.5, graphics 1.4,
BASIC 1.35, integer and floating point 1.1 (geometric means per group);
-O3 and LTO add 10-20% on top. On an Apple Silicon Mac (the UTM image):
SWIs 694k/s -> 3.4M/s.

- `qemu/`: the ROM's DynArea_PMP stubs are trapped when translated, not
  as breakpoints: with any breakpoint set QEMU checks every jump between
  translated blocks (a quarter of the time in integer code).
- `qemu/`: an SWI from RISC OS code no longer goes through a host signal
  (a signal frame and several host system calls each). The translated
  code calls a helper that does what the port's SIGSYS handler does
  (ENTER_EXCEPTION(SVC)) and carries on at the SWI vector.
  `RISCOS_NO_FASTSWI` and `RISCOS_NO_INLINE_SWI` turn it off.
- `qemu/`: __kernel_get_tls and __kernel_memory_barrier are generated
  inline (the port calls get_tls about five times per SWI), and so is
  MRS Rd, CPSR in code loaded from disc (UnixLib and SharedCLibrary read
  it often), instead of leaving the translated code.
- `qemu/`: MSR writing only the flags doesn't rebuild QEMU's cached CPU
  state.
- `qemu/build.sh`: -O3 and link-time optimisation.
- `RISCOS_EXCP_STATS` counts exits from the translated code by kind.

## 0.5.1 (2026-10-18)

- `qemu/`: MRC p15 c6 (DFAR) now gives the address of the fault being
  delivered, whatever raised it: a host SIGBUS or SIGSEGV as well as a
  data abort QEMU raises itself, and a signal frame that couldn't be
  written to the guest stack. Before, it gave the last data abort's.
  ARMEABISupport 1.08's abort handler maps a page at DFAR and retries,
  so with a stale address it mapped the same page for ever and the
  desktop stopped (TubeSearch's first HTTPS connection, through
  AcornSSL).
- `qemu/`: IOC timer 0 (&3200040, &3200044, &320004C) is emulated,
  counting down at 2 MHz from 19999. AcornSSL reads it for entropy; the
  port has no IOC, so a program's first secure connection got a data
  abort ("EMT trap" in UnixLib programs).
- `qemu/`: `RISCOS_SIG_DEBUG` lists the signals delivered to the port;
  `RISCOS_PMP_DEBUG=2` lists each abort and each pool page operation.

## 0.5 (2026-10-17)

GCCSDK's ARMEABISupport 1.08 runs on the port, and with it the Iris web
browser (riscosdev.com, April 2026: WebKit, GTK 3, Cairo).

- `qemu/`: what ARMEABISupport 1.08 needs from the kernel.
  - Physical memory pool (PMP) dynamic areas: the port creates them but
    its PhysOp, LogOp, Resize and GetPages are stubs that stop on an
    undefined instruction. Breakpoints on the stubs (found by name in the
    ROM) do them on a memfd per pool and keep the kernel node's sizes up
    to date.
  - OS_Memory 0 for application space gives a made-up physical address
    from the file the port maps at &8000 for the task: 1.08 tells
    programs apart by it.
  - MRC p15 c6/c5 read the last data abort's address and status (1.08's
    abort handler), MRC/MCR p15 c1 (SCTLR), c0 (MIDR, CTR, MPIDR).
  - An emulated exception return to Thumb code resumes in Thumb state
    (aborts taken in Iris's Thumb libraries came back in ARM state).
- `qemu/`: the port's IXFS numbered directory entries counting the
  host's "." and "..", which it hides, so OS_GBPB offsets had gaps.
  UnixLib's readdir needs them to go up by one per entry, so it saw an
  empty directory wherever it had more than about 25 entries: GTK found
  no icon theme, fontconfig no fonts. getdents now leaves "." and ".."
  out for the port. riscos-gtk's GTKThemeTest draws in full.
- `modules/ARMEABISupport` (lite): MemoryOp page allocators (as
  SOManager uses them) and AbortOp handlers, with the page mapping done
  by QEMU (SWI &C0002, abort handlers returning through SWI &C0003).
- `modules/Iconv`: new. UnixLib's iconv calls the Iconv module, which
  HardDisc4 hasn't got: UTF-8, ISO-8859-1/15, Windows-1252, ASCII,
  UTF-16 and UTF-32 (BOM or either byte order), //TRANSLIT, //IGNORE.
  `rol-run` loads it.
- `modules/KVMWheel`: the scroll wheel for SocketKVM front ends (the
  protocol has none): key numbers &1F0-&1F3 become PointerV WheelChange.
- `rol-run`: `ROL_MODULES=DIR` takes the modules from another directory
  (to run with ARMEABISupport 1.08, say).
- QEMU debugging: `RISCOS_SWI_TRACE` (every SWI RISC OS code calls),
  `RISCOS_SWI_RESULT=<hex>` (how one SWI returns), `RISCOS_PMP_DEBUG`,
  `RISCOS_REPORT` (Report_Text0 to stderr).
- Iris's JavaScript JIT works on the port too (the Thumb return fix):
  about 2.4 times faster than without on a test page.
- Tests: `iconv` (37 checks). With 1.08 in place of the lite module 33
  pass: the lite module's *ARMEABISupport_Info and nesting checks
  (`armeabisupport-info`, `system`) don't apply, and 1.08 doesn't unmap
  a hole in the middle of a mapping (`mmap`) or, in `files`, remove the
  directory (to look at).

## 0.4 (2026-10-17)

HardDisc4's applications run.

- `qemu/`: the rest of the privileged instructions code from disc uses
  are emulated with the port's virtual CPU: MRS/MSR SPSR, STM/LDM with ^,
  and exception returns (LDM {..., pc}^, MOVS/SUBS/ADDS pc). HardDisc4's
  SharedCLibrary 6.22 uses them to resume a program after a handler: the
  Mouse configuration plugin stopped with "undefined instruction". In USR
  mode, MSR and CPS now leave the mode and interrupt mask alone, as on a
  real CPU (BASIC VFP's CPSIE in USR mode crashed RISC OS, so !SciCalc
  and every *BASIC64 program in the desktop did). `RISCOS_PSR_DEBUG`
  reports undefined instructions that aren't emulated.
- `modules/SoundDMA`: a silent SoundDMA. The port has none, so programs
  that set sound up stopped with "SWI &4014x not known" (!Maestro, the
  QTM player in !Hopper). It keeps and returns the settings it's given;
  `rol-run` loads it and restarts the ROM's sound modules on it.
- `rol-run`: `ROL_KVM_CONTROL=FIFO` hands rol-kvm a control FIFO (mouse,
  keys, screenshots) in desktop mode.
- Tests: `priv-emulation`, `sounddma`, and `app-scicalc`, `app-maestro`,
  `app-hopper` (HardDisc4 applications that failed, running in the
  desktop): 36 checks. Every application on HardDisc4 was started and
  opened from its icon bar icon; all run but !Omni (its LanManager wants
  Internet 5; the port's Internet module is its own).

## 0.3.1 (2026-10-17)

- `qemu/`: where the host's `vm.mmap_min_addr` is above one page (65536
  on GitHub's runners), QEMU now maps the guest at an offset instead of at
  its own addresses. The port maps from 0x1000, which such a host refuses
  without CAP_SYS_RAWIO, so RISC OS stopped at once ("Failed to reserve
  address space") in containers, including the GitHub Actions jobs
  (outside a container, root could still map there). `RISCOS_IDENTITY_MAP`
  restores the old behaviour.
- `tests/programs/t_clib.c`: missing `<stdio.h>` (a warning).

## 0.3 (2026-10-17)

HardDisc4 5.30 runs unchanged.

- `qemu/`: privileged PSR instructions (`MRS CPSR`, `MSR CPSR` with the
  control field, `CPSID`/`CPSIE`) in code loaded from disc are emulated
  with the port's virtual CPU modes, through its PSR HAL routines (the
  port's own ROM code is unchanged). On Linux they read user mode and are
  ignored, so HardDisc4's SharedCLibrary 6.22, which enters USR mode with
  `MSR`, hung every C program. `RISCOS_NO_PSR_EMULATION` turns it off.
- `harddisc/fetch.sh`: no longer changes `!System` to keep the ROM's
  SharedCLibrary 6.15; HardDisc4's 6.22 is used.
- Tests: `psr-emulation` (MRS, MSR, CPS from BASIC's assembler, and a SWI
  back in USR mode) and `desktop-clib` (6.22 loaded by `!System`, and
  `BootVars` runs with it): 31 checks.

## 0.2 (2026-10-17)

The full RISC OS for Linux: the desktop, and threads.

- **Desktop mode** (`rol-run -d`, `-s FILE`, `-w SECONDS`, `-g WxH`): boots
  RISC OS Open's HardDisc4 (`!Boot`) into the desktop, headless, starts the
  program as a Wimp task, lets it run, and saves the screen as a PNG.
  Returns the program's status if it ended, 0 if it was still running.
  Programs can take more screenshots with `*TBShot`.
- `tools/rol-kvm.c`: a headless SocketKVM front end for the port (screen in
  memory, PNG on request, mouse and keys through a control FIFO).
- `tools/tbshot.s`: the `*TBShot` utility.
- `harddisc/`: HardDisc4 5.30 pinned by sha256 (`make harddisc`), unpacked
  in the port, filetypes as `,xxx` suffixes (`xattr2comma.py`), set up as
  the port's build does. HardDisc4's SharedCLibrary 6.22 hangs C programs
  on the port, so `!System` keeps the ROM's 6.15.
- `unixlib/`: UnixLib (riscos-unixlib edf6965) with the port's patch:
  thread switching and fault handling through the port's HAL routines
  instead of privileged instructions. `make unixlib` builds it; the test
  programs link with it; in the container image it replaces the
  toolchain's `libunixlib.a`.
- Tests: threads, VFP threads and the fault test now pass (they're
  ordinary tests when the patched UnixLib is built, XFAIL otherwise); three
  desktop tests (window in the screenshot, exit status, screen after
  close) with `tests/pngpixel.py`.
- **VFPSupport 0.91**: everything ROOL's VFPSupport 0.18 does that the
  port allows. ElementaryFunctions (ROOL's code, translated to GNU as, in
  a VFMA build and a software-FMA build chosen by HWCAP); ExceptionDump in
  full; switches atomic (interrupts off through the port's PSR device),
  so FastAPI works from interrupt code; one instance only;
  Service_ModulePostFinal on finalisation; Features 2 lists the
  elementary functions; MVFR2 on ARMv8; ROOL's errors. ROOL's own tests
  (`tests/rool`) pass but for lazy activation.
- `basicvfp/`: BASICVFP from RISC OS Open's Pi ROM (`make basicvfp`),
  loaded by rol-run, so `*BASIC64` works as on a Pi. Also takes ROOL's
  own VFPSupport out of the ROM, for a test that calls its elementary
  functions and ours on the same inputs: the same bits and flags.
- **ARMEABISupport**: a program's memory is now told apart by the stack
  the caller runs on (read through the port's PSR device), not by start
  order. Before, a desktop program that ended while one it had started was
  still running freed the other's stacks, and RISC OS hung. A program
  started by `system()` (which SharedUnixLibrary never "cleans up") now
  ends when it frees its own main stack, so its memory is freed and the
  nesting doesn't grow.
- New tests: elementary functions (both builds; correctly rounded results
  and ROOL's edge cases), switches from interrupts, two VFP desktop tasks,
  ROOL's VFPSupport tests, BASICVFP, one instance.
- riscos-crossdev 1.8.1 (its image has the autotools UnixLib's build
  needs). CI caches HardDisc4 and UnixLib; a secret `UNIXLIB_TOKEN` is used
  if riscos-unixlib is private.

## 0.1 (2026-10-15)

First version.

- `bin/rol-run`: runs an ELF (via elf2aif) or Absolute program, or a RISC OS
  command, in RISC OS for Linux, headless. It gives back the output and the
  exit status, with separate statuses for RISC OS errors, time-outs and a
  stop at the prompt.
- `qemu/`: Timothy Baldwin's QEMU changes for the port (SWI intercept,
  real-time signals, sparse ELF loading) ported to QEMU 8.2.2, on top of
  riscos-crossdev's alignment-trap patch.
- `modules/VFPSupport` 0.90: VFPSupport for the port (eager context
  switching, features from AT_HWCAP).
- `modules/ARMEABISupport` 1.90 (lite): stacks, mmap/mremap/munmap and
  Cleanup from one ordinary dynamic area. Programs are told apart by
  start-up and Cleanup, not physical pages.
- `modules/common`: building position-independent C modules with the
  riscos-crossdev compiler (GOT relocated at start, checked by
  `check-pic.py`).
- `rom/`: the ROM image pinned by sha256.
- `tests/`: 13 checks (mmap includes mremap zero-fill) and 3 known failures: pthreads and faults in UnixLib
  programs hang, because UnixLib uses privileged instructions the port
  runs in user mode.
- Container image (`docker/Dockerfile`) on riscos-crossdev 1.8; CI
  workflows.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
