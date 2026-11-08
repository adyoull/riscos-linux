# Changelog

## 1.0 (2026-10-21 to 2026-11-08; images 1.0.0019, the first public release)

From a test bed to RISC OS on Linux's images: the Linux side, the desktop
parts, and natively on real machines. What users get from this comes in
the images (1.0.0001 onwards) and through Updates (riscos-rolpkg's
CHANGELOG lists each package).

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
