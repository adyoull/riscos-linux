# Changelog

What each RISC OS on Linux ROM build changed. The ROM reports itself as
"RISC OS 5.31-N (date)". Each change is one commit on `nightly-merge`
(the commit message has the detail); `NIGHTLY-MERGE.md` says where each
one is and what to watch when maintaining it. ROL installs the ROM as the
`riscos-rom` package.

## 5.31-28 (built 11 Nov 2026; released as riscos-rom 5.31-28)

- **FileCore's zone compaction no longer aborts** (RAM discs, and SCSI
  discs in FileCore format): two places in `CompactZone`
  (`FileCore/s/FileCore32`) were only right with ScratchSpace in low
  memory (&4000, as on every other machine); on RISC OS on Linux it is at
  &20000000.
  - "Double exact fit" worked out a bit address as the size table's
    address times 16, modulo 2^32, and so read &1800 instead of &20001800:
    a data abort inside FileCore, which was then left threaded ("FileCore
    in use" for every later call). The lost top bits of the byte address
    are put back. Chris Gransden's report: a ROM build on a 2 GB RAM disc
    failed after a few seconds (1,312 aborts in his log, all at
    &209AFB88).
  - Ordering the moves read one word at or just past the end of
    ScratchSpace before testing for the end: at &8000 that is the start of
    application space and can be read; here nothing is mapped at
    &20004000 (abort at &209B029C). The read is now conditional. The web
    browser project's report: OS_Args 1 past the extent of a new file on
    a RAM disc, after 100 files had been written and deleted.
  - The same code reads further words past ScratchSpace's end where a
    table fills it (found with the new test, `ramdisc.bas`: abort at
    &209B0244 in 5.31-26). Rather than change FileCore's algorithm, the
    page after ScratchSpace is now memory, as on other machines
    (`HAL/c/Main`).
- **IXFS: a pointer set past the extent extends the file at once, with
  zeros**, as RISC OS does (PRM, OS_Args 1): it left the file's length
  alone until something was written, as Linux does. `IXSupport/c/fs`
  (FSEntry_Args, fsargs_SetPTR): regular files only, and left alone where
  the file can't be extended (open for reading). The web browser
  project's report (UnixLib's lseek and SQLite behaved differently here
  and on a Pi).
- **BuildROM relinks Edit every time:** Edit is linked for its address in
  the ROM, and its makefile doesn't notice when that address moves in a
  build over an earlier one (5.31-26: "ROM application !Edit cannot be
  RAM loaded", and no Edit). BuildROM removes `EditApp.linked.!RunImage`
  first.
- Tested under QEMU (DDE 32d build): the testbed's new `ramdisc.bas`
  (pointer past the extent on IXFS and a RAM disc; 600 steps of files
  written, extended, checked and deleted on an 8 MB RAM disc) fails on
  5.31-24 as reported and passes; 1,644 files copied to a 512 MB RAM
  disc; Edit, Draw and Paint start.
- 5.31-25 (the first FileCore fix only) and 5.31-26 (both FileCore fixes
  and IXFS, without the page after ScratchSpace) were test builds; not
  packaged. 5.31-27 mapped the page at the wrong address (ADDRESS is the
  ROM's base, not ScratchSpace's) and didn't start; not packaged.

## 5.31-24 (built 11 Nov 2026; released as riscos-rom 5.31-24)

- **A new dynamic area goes where it would on any other RISC OS machine:**
  at the lowest free address, working upwards (first fit through the area
  list, from the RMA up), as the kernel does everywhere else. The Linux
  build let Linux choose, which packs areas from the top down, each new
  one directly below the last: running off the end of an area then read
  or wrote the area made before it, where real RISC OS aborts (the owner's
  MemCompare2 logs, native against ROL, 11 Nov 2026; the web browser
  project's report). Areas also no longer start at top-bit-set addresses.
  If Linux has something at a candidate address the search moves up 1MB;
  after 64 refusals, or past &C0000000, Linux chooses as before.
  `Kernel/s/ChangeDyn` (AllocateAreaAddress, GetNextRange).

## 5.31-23 (built 11 Nov 2026)

- **BuildROM works with DDE 32d.** Its !SetPaths reaches the tools through
  `Tools:`, which the build's own set-up repoints at ROLSrc's Library, so
  amu was "not found". BuildROM now puts the DDE's tool folders on
  Run$Path by name. DDE 31d is unaffected. No change to the ROM's source.

## 5.31-22 (to be built)

The number after 5.31 now counts builds: each ROM built and tested gets
the next one, released or not. 5.31-20 was built (09 Nov 2026) and tested
but not released: its RAM disc couldn't be given any size (below). 5.31-21
(09 Nov 2026, the same source with that fixed) was built and tested but
not released: it had no Resources.BASIC.VFPData, as the s.VFPData that
BASIC's GenData wrote during the build lacked one line ("VFP_Op_cc EQU 2")
and objasm failed on it. GenData run again six times on the same Pi
(NativeSupport 0.38, SWI stubs on and off) and under QEMU made the whole
file every time, so 5.31-22 is the same source built again.

- **The RAM disc can be up to 2 GB** (it was 128 MB), as on native RISC OS
  5.3x: the kernel makes it a physical memory pool area (PMPRAMFS), which
  Timothy Baldwin's port had turned off on Linux (2017) because it had no
  PMP areas then. The kernel has done PMP areas itself since 5.31-3, and
  RISC OS Open's RAMFS uses either kind. Chris Gransden's report.
  OS_ChangeDynamicArea now hands PMP areas to their handlers on Linux too,
  as on native RISC OS (the Linux build has no sparse areas, DA_Batman,
  and the check was inside that): the 5.31-20 build (09 Nov) gave
  "Unable to move memory" for any RAM disc size, from Configure, the Task
  Manager or *ChangeDynamicArea. Checked with that build patched the same
  way, natively under QEMU: the RAM disc grows to 64 MB and 1 GB, a 4 MB
  file copies in, it refuses to change size while it has files ("can only
  be changed when it is empty", as native), and goes back to 0.
  Checked before building, on 5.31-19 natively under QEMU: a PMP area with
  a 1 GB physical pool (262,144 pages claimed one block at a time, 0.6 s)
  maps and reads back. To check on the build: Configure's RAM disc at
  1 GB and more, files copied in, the RAM disc made smaller and larger.
- **A pointer turned off goes off** (SocketKVM): when a program turns the
  pointer off (`*FX 106 0`, as !RDPClient does over its window, where it
  draws the remote computer's pointer itself), the screen kept showing the
  last pointer, frozen where it was since 5.31-19 (before, it followed the
  mouse). The kernel gives SocketKVM no shape then, and SocketKVM passed the
  pointer's visibility on only with a new shape. Front ends from version 3
  (rol-fb) are now told once, with a pointer message that has no rows; the
  SDL front ends aren't (they'd read rows from a height of 0). Chris
  Gransden's report, found with !RDPClient's developers.
- **Room for SWIs without a trap** (for NativeSupport 0.34): the HAL's
  pvops table gets an entry, PVirt_SWIStubs (offset 140, after
  VModeOffsets), pointing at a block of three words: "SWIS", then the
  start and end of an area of code. NativeSupport puts stubs there that
  enter the SWI handler directly from the busiest SWIs in the ROM, so no
  trap to Linux and back. The HAL's IRQ signal handler now leaves an IRQ
  pending (as it does when the I bit is set) while the interrupted code is
  in that area, since a stub is half-way through switching to SVC mode;
  the IRQ is taken when the stub's SWI handler next sets the PSR. With the
  block left at zero (no NativeSupport, or an older one) nothing changes.
  Tried before building on 5.31-19 with the same change patched into the
  ROM, natively under QEMU: 320 ROM SWI sites given stubs, the desktop and
  BASIC's SYS run through them, and SYS ColourTrans_SetGCOL takes about
  30% less time (QEMU makes the trap cheap; a Pi should gain more).

## 5.31-19 (8 November 2026)

- **The mouse speed in Configure works** (and *Configure MouseStep): a
  mouse's movement reaches RISC OS as a relative move (SocketKVM's new
  ev_mouse_rel), which the kernel scales by the mouse speed and keeps in
  the pointer's bounding box. The front end used to add the movement up
  itself and give RISC OS the pointer's position, which the mouse speed
  never applies to: on a 4K screen the pointer crawled. SocketKVM now
  tells the front end where the pointer is as it moves (c_pointer_pos),
  so it's drawn where RISC OS has it, and a program that moves the
  pointer moves it on the screen too. Front ends say they can (version 3,
  rol-fb from riscos-support 1.0.0034-1); older ones, and tablets (UTM),
  go on as before. SocketKVM's version to the front end is 3.

- **An undefined instruction in Thumb code gives lr = the instruction + 2**,
  as on a real ARM (it was + 4, the ARM state's offset, for both): the
  HAL's SIGILL handler now takes the state into account. Debuggers that
  put undefined instructions in Thumb code as breakpoints (EDT) saw the
  wrong address.

- **4K in 16 million colours:** the screen memory can be 64MB (it was
  16MB), enough for 3840 x 2160 in 16 million colours (31.6MB). The memory
  comes from Linux as it's used, so this costs nothing until a big mode is
  chosen.

- **OS_Memory 24 (and OS_ValidateAddress) answer for a PMP area page by
  page**, as RISC OS does from its page tables: only the pages mapped in
  the area are accessible, with the area's access privilege, and the
  internal flags no longer leak into bits 26-31 of the answer. Debuggers
  and anything else checking addresses got the wrong answer for
  ARMEABISupport 1.08's stacks and mmaps and SOManager's shared libraries.
  (NativeSupport 0.28 answers these checks itself natively, so this
  matters under QEMU.) Its code is a macro of its own (CheckMemoryAccess_LinuxPMP):
  inside CheckMemoryAccess_DynamicAreas it took that macro past objasm's
  size limit ("Macro definition too big"). objasm's numbered labels don't
  reach across macros, so it ends with the Z flag saying whether the area is
  done, and CheckMemoryAccess_DynamicAreas branches on that.

## 5.31-18 (6 November 2026)

- **An interrupt no longer changes SPSR_svc under the code it interrupts.**
  The kernel's IRQ entry, in the Linux build, already kept r14_svc across
  the interrupt's handlers; it now keeps SPSR_svc too. Their SWIs
  (CallEvery routines' OS_AddCallBack, ScreenBlanker's ticker, TickerV
  claimants) write it, and the kernel's tick code doesn't keep it, so SVC
  mode code between setting SPSR_svc and its MOVS pc came back in SVC mode
  with IRQs off. On a real ARM that's a two-instruction window; natively
  each instruction is a trap, and 0.2% of MSR SPSR, MOVS pc pairs run with
  IRQs on went wrong (UnixLib's mode changes, the web browser's test
  program: a SWI then loops). Checked with a binary-patched 5.31-17.1:
  none in 20,000 (31-66 before).

- **The desktop no longer stops now and then as it starts** (the RISC OS 5
  banner stays, usually under QEMU/UTM, more rarely natively). Sound's
  Level1 fill runs voice code in IRQ mode with IRQs enabled; its mode
  changes go through SetCPSR_c_unstack, which calls __PVirt_GetVCPU (LDR
  pc, =&FFFF0FE0) with IRQ mode's lr holding the return address. An IRQ
  landing on GetVCPU's instruction (the kuser helper itself was already
  protected, GetVCPU wasn't) was taken from IRQ mode, and its return left
  lr = pc, so get_tls returned to itself for ever. The IRQ handler now
  finishes a GetVCPU/get_tls call it lands in (r0 = vcpu, pc = lr; every
  BL to GetVCPU has lr dead afterwards) and takes the IRQ, or defers it
  inside SetCPSR as before, at the return address.

## 5.31-17 (4 November 2026)

- **GPIO HAL devices on a Raspberry Pi 3 or 4**, as RISC OS's own BCM2835
  HAL has them (HALDeviceComms_GPIO, HALDeviceID_GPIO_BCM2835, API 1.0:
  pins 0-31 and 32-53): Linux's /dev/gpiomem maps the board's GPIO
  registers, which are each device's Address, and the functions (data,
  direction, modes, pulls, edges, Enumerate) work on them as the BCM2835
  HAL's do. Only the pins the board brings out can be changed (the 40-pin
  header's GPIO 0-27; all of them on a Compute Module 3, GPIO 2-27 on a
  CM4), so Linux's own (the SD card's, Wi-Fi's) are left alone. Programs
  that look for the Pi's GPIO device find it: the Raspberry Pi edition of
  ROOL's DDE (cc, objasm) checks for it and refused to run. Needs the Pi
  kernel with /dev/gpiomem (riscos-kernel-pi 6.6.78-5) and, protected,
  riscos-support 1.0.0025-1. Elsewhere there are no GPIO devices, as
  before.

## 5.31-16 (4 November 2026)

- **IXFS has a disc name, ROL, as FileCore's discs do.** Full (canonical)
  names are now `IXFS::ROL.$.path`, like `SDFS::RISCOSpi.$.path`, where
  they were `IXFS:$.path`. Some programs take a name with `.$.` in it as
  the sign of a full path: StrongEd 4.70 refused to save anywhere on IXFS
  (HardDisc4, USB discs and SD cards included) with "Don't know where to
  save", shown in its own window. A disc name given (`IXFS::Other.$.x`)
  is ignored as before, and `IXFS:$.path` still works everywhere.

## 5.31-15 (4 November 2026)

- **The HAL counter reloads at each centisecond tick, as on hardware.**
  HAL_CounterPeriod said one second (equal to the 1 GHz rate), so the
  counter counted down for a whole second: the Internet module's
  microtime added up to a second (0 to 999,999 µs, and tv_usec could
  reach about 1,990,000) to its centisecond time, and programs that
  check the counter's period (UnixLib's clock_gettime, Mesa's tests,
  DrawSpy 0.01) only got centiseconds. The counter is now timer 0's
  countdown at nanosecond resolution (Linux's timer_gettime: time to the
  next tick), period rate/100 with the usual 10 ms tick, reading
  period-1 down to 0; before timer 0 runs it reloads every 10 ms of
  CLOCK_MONOTONIC. The rate and the readings both come from
  CLOCK_MONOTONIC (the rate came from CLOCK_MONOTONIC_RAW).

## 5.31-14 (3 November 2026)

- ***IXRun runs Linux commands through rol-cmd**, as RISC OS's own Linux
  user (riscos), the way *LinuxUser does: no password and no privileges,
  through `/bin/sh -c`, so a command's name is enough (`*IXRun ls -l`), in
  protected RISC OS too (whose view of the files has no Linux programs or
  /dev/ptmx, so IXSupport's own *IXRun always gave ENOENT there). It needs
  nothing loaded at start-up (until now riscos-harddisc set an alias for
  *LinuxUser in the boot sequence). Output is printed as it comes; Escape
  stops the command; a non-zero exit status gives "(exit status N)" and
  "The Linux command failed". Without rol-cmd (no /riscos/cmd, or nothing
  takes the request in 5 seconds) *IXRun works through a terminal as
  before, but now looks for a command name without a "/" in /usr/local/bin,
  /usr/bin, /bin, /usr/sbin and /sbin (it ran only full paths, so
  `*IXRun ls` always gave ENOENT).

## 5.31-13 (3 November 2026)

- **IXFS files over 2GB** (up to RISC OS's 4GB - 1). IXFS opened files
  without O_LARGEFILE and moved through them with the 32-bit lseek and
  ftruncate, so natively a file stopped at 2GB ("File too large"; LanMan98
  copies failed there) and its pointer couldn't be put past 2GB (EINVAL).
  Now O_LARGEFILE, _llseek and ftruncate64, with the pointer, extent and
  sizes as unsigned 32 bits (a file over 4GB reads as 4GB - 1). Test:
  riscos-linux-testbed `ixfs-bigfile`.
- Tested in the test bed: 61/61 under QEMU; `ixfs-bigfile` natively too
  (it fails there on 5.31-12).

## 5.31-12 (3 November 2026)

- **A connection closed by the other end is seen.** After a read, the
  Internet module kept a socket readable only if more data was waiting,
  so a close that had already arrived was never reported: Socket_Select
  (UnixLib's `select()`) waited out its timeout instead of the program's
  `read()` getting 0. ClawsMail's HTTP fetches (feeds, calendars) each
  took 60 s. A close or an error now also raises an async socket's
  Internet event. Test: riscos-linux-testbed `select-eof` (`t_eof`).
- **IXFS free space** (OS_FSControl 49 and 55) works; it always failed,
  so editors that check the space before saving (StrongEd) couldn't save
  to HardDisc4. Test: `ixfs-freespace`. (5.31-11's change; 5.31-12
  includes it.)
- Tested in the test bed: 60/60 under QEMU (`select-eof` and
  `ixfs-freespace` pass; they fail on 5.31-10).

## 5.31-10 (2 November 2026)

- **FileCore discs (USB sticks and SD cards formatted on RISC OS).** The
  ROM has RISC OS Open's SCSI layers as a native Raspberry Pi ROM does:
  SCSISwitch, SCSIFS and SCSIFiler (`SCSI::` drives with their icon-bar
  icons). ROL's SCSILinux module, loaded from HardDisc4, takes the place
  of SCSISoftUSB and passes the discs' commands to Linux.
- Tested in the test bed (riscos-linux-testbed `tests/filecore.sh`): HForm
  formats a disc image through SCSIFS and SCSILinux; files and a directory
  copy on and back identical, with their types; `*Verify` passes; a second
  boot reads them again.
- Includes 5.31-9's keyboard LEDs.

## 5.31-9 (2 November 2026, not built: its change is in 5.31-10)

- **Keyboard LEDs.** Caps Lock, Num Lock and Scroll Lock light as RISC OS
  has them: SocketKVM passes KeyV_NotifyLEDState to the front end
  (`c_leds`, to front ends that report protocol version 2 or later: ROL's
  rol-fb sets them on the keyboards it has). Before, nothing passed them
  on, and the LEDs never changed.

## 5.31-8 (2 November 2026)

- **Files get the type of their extension.** A regular file that isn't
  executable and has no RISC OS type (no `,xxx` and no extended
  attribute: files on FAT, exFAT and NTFS discs, files Linux programs
  wrote) gets the type MimeMap gives its extension, as DOSFS does:
  `photo.jpg` (`photo/jpg`) is a JPEG. Names never change. Saving where
  the type can't be stored adds `,xxx` only when the name doesn't already
  say the type. `M` in the special field turns it off.
- IXFS asks MimeMap first when the desktop starts, and again whenever
  MimeMap starts, from a callback: MimeMap reads its table on first use,
  usually from IXFS itself, so it is never asked from inside IXFS until
  its table is in.

## 5.31-7 (2 November 2026)

- **MbufManager in the ROM** (RISC OS Open's MManagerBlob 0.30, as in
  every RISC OS 5 ROM): !Omni's LanManFS and Access need it.
- **Network servers answer their clients.** Socket_Select reported
  nothing on an accepted connection whose client had sent its first data
  before the accept (RDP and TLS clients do): Linux signals only new
  events, and the signal was set up in Socket_Accept. Socket_Accept now
  checks what's already there. RDPServer had accepted connections and
  then never answered ("server not responding").

## 5.31-6 (1 November 2026)

- Socket_Getsockopt answers SO_TYPE (GLib's `g_socket_new_from_fd()`
  needs it); an unknown TCP option is refused instead of being taken as a
  socket-level one; Socket_Sendmsg_1 (UnixLib's `sendmsg()`), and
  Socket_Sendmsg's flags from R2.

## 5.31-5 (31 October 2026)

- The Internet module reports version 5.73 (it said 0.00), so
  `RMEnsure Internet 5.xx` checks pass (!Omni, !Internet).

## 5.31-4 (31 October 2026)

- Socket_Select returns 0 when its timeout runs out. It spun forever,
  hanging the machine in TubeSearch and Reel. Timeouts under a
  centisecond round up.
- IXFS: a locked item can't be renamed.

## 5.31-3 (31 October 2026)

- IXFS gives RISC OS's filing system errors (Disc full, Locked, Already
  open, Directory not empty...) instead of Linux's names, enforces the L
  (locked) attribute, and Delete's errors are no longer lost.
- Physical memory pool dynamic areas (OS_DynamicArea 21-23, 25) in the
  kernel, for GCCSDK's ARMEABISupport 1.08.
- Build fixes from the first native builds (FPEmulator's workspace
  anchor, the Internet module's handlers).

## 5.31-2 (31 October 2026)

- RAM from Linux: `ROL_RAMSIZE_MB` (up to 4095MB) sets RISC OS's RAM
  (3GB if not given), and the free pool counts the memory not yet used,
  so the Task Manager's Free is right.
- Network statistics in Internet 5's layouts (Socket_Sysctl for the
  interface list, routes, ARP and TCP/UDP/IP counters and connections;
  Socket_InternalLookup), from Linux: NetStats and InetStat work.
- The version string with the build number: "RISC OS 5.31-2 (29 Oct
  2026)".

## 5.31-1 (30 October 2026): from 5.29 to 5.31

The first build: Timothy Baldwin's Linux port, which was RISC OS 5.29
(its kernel 6.57 of April 2021, last updated in December 2022), brought
up to RISC OS Open's 5.31 development sources (the nightly of 29 October
2026), and built natively with the DDE on a Raspberry Pi. Released as
`riscos-rom` 1.0.0001-1 in the first images. What it took:

**The merge** (`NIGHTLY-MERGE.md` has the detail)

- Baldwin's own "merge with RISC OS Open" commits carry Linux changes of
  their own, so every file was merged three ways against the version first
  imported into the port (2017), not against a later upstream. Of the
  files that differ: 669 taken from upstream (the port never changed
  them), 128 kept from the port (upstream never changed them), 155 merged
  cleanly, **266 conflicts resolved by hand**, and 21 that both had added
  taken from upstream. Lines that carry the port's Linux structure
  (`[ Linux`, `[ PagedMemory`, KSWI, ROMLimit...) were never resolved
  automatically, and every changed assembler file was checked to have the
  same `[`/`]` nesting as the port's.
- Kernel: KernelWS rebuilt as upstream's plus the port's Linux layout;
  `s.HAL` is the port's with upstream's later changes; Linux keeps its own
  abort veneers (upstream's AbortTrap ones are for real hardware);
  upstream's new separate RW/ZI data segment placed at &24400000 for
  Linux, with the link address to match; KSWI restored wherever the port
  had it; raw MSR/MRS that upstream added in code Linux runs converted to
  the port's PVOps macros; `TaskControl_ResetStacks` from upstream, in
  PVOps form.
- The port's copies of files upstream has since renamed or retyped
  dropped (they collide on RISC OS's case-insensitive discs), 77 files
  upstream had deleted removed, and 110 files the port never changed taken
  at upstream's current version.

**Building it natively** (the DDE runs only on the machine it's installed
on, so the first builds were on Andrew's Raspberry Pi)

- `nightly/BuildROM`: an Obey file that builds the ROM from RISC OS
  itself, from a source zip: checks for files lost in unzipping, finds the
  DDE's C library, runs RISC OS Open's `srcbuild` once per phase and stops
  at the first failure. The port's build-it-first `srcbuild` wrapper
  replaced by upstream's binary; BuildSys makefiles back to upstream's.
- First build: `Hdr:CPU.PVOps` back in Generic32 (behind most of the
  "Unknown opcode pvMSR/pvMRS" errors); DAReason_AlterFile back for
  SocketKVM; FileSwitch's internal `object_image` renamed (upstream now
  defines one); CModule accepts the port's `CUSTOMEXP=no`.
- Second build: PVOps import and jump-table lines the merge had lost
  (RISC_OSLib, TaskWindow, FileSwitch, BufferManager, DeviceFS,
  FPEmulator); `s.MemInfo`'s OS_Memory 64+ dispatch and ReservePages the
  merge had duplicated; L1PT only with ShortDesc.
- Linking as the port did: modules that call the Linux HAL can't be
  linked at base 0, so the `rom` phase only assembles and linking waits
  for `rom_link` with the ROM address; RISC_OSLib's linked libraries
  relinked when the ROM address changes.
- TerritoryManager: the port's assembler version, since upstream's C one
  needs a newer cmhg than DDE 31's.

**Fixes from the first runs on a real Pi**

- Boot hung in AbortTrap's set-up: reading CP15's MIDR raises SIGILL in
  user mode before RISC OS's handler exists (QEMU emulates the read, so
  it had booted there). Not read on Linux.
- Errors reported "abort on data transfer at &FFFFFFF8": the undefined
  instruction handler reset the stacks over its own register dump. The
  dump is copied first on Linux.
- Abortable dynamic areas created as ordinary ones (aborts reach the port
  through its own veneers, not AbortTrap): ARMEABISupport 1.08 had relied
  on their handlers and Iris failed with "Internal error".
- BASIC's RENUMBER message and modulewrap's error path as upstream's.
- RAM: 3GB instead of 1GB.
- The version: "RISC OS 5.31 (29 Oct 2026) nightly", the nightly's date
  instead of the kernel module's own (24 Apr 2024).

## Faster SWIs (31 October to 2 November 2026)

SWIs (RISC OS's system calls) were the biggest cost of running RISC OS on
Linux. This is what was found and done.

**Why they were slow.** RISC OS's SWI instruction is the one Linux uses
for its own system calls, so each SWI went: Linux's seccomp filter →
a SIGSYS signal (a full signal frame with all the registers and the
floating point state, the signal mask) → the HAL's SWI handler → RISC OS.
Measured with SWICost on the same Raspberry Pi 4: native RISC OS 5.31
does `XOS_ReadMonotonicTime` in 206 ns and `OS_Byte 135` in 1.9 µs; ROL
5.31-6 took 2.6 µs and 3.1 µs - about 2.4 µs more for every SWI, roughly
twelve times native. A Linux system call through the HAL's own gateway
cost 0.6 µs, so the signal delivery was what cost.

**First try: patching the ROM's SWIs (the `swi-fastpath` branch, builds
`5.31-N-swi`).** Each ROM SWI that traps is rewritten once to branch to a
stub that enters RISC OS directly (`*ROLSWIPatch On|Count|Off`). It took
five rounds to work natively:

- the 128KB of stubs moved to the end of the kernel, and a literal pool
  before them (they had put other code's ADRLs and literals out of reach);
- the hook kept out of line, so the HAL's SWI handler and the test bed's
  QEMU and NativeSupport still find the routines they look for;
- `*ROLSWIPatch` reads the HAL's counts through functions (a C module
  can't address the HAL's variables directly);
- the SWI that has just been patched returns through its stub (the
  kernel reads the SWI number from the word before the return address,
  which had just become the branch: the first patched SWI jumped to 0);
- an interrupt arriving while the HAL's patching code was on the signal
  stack is left pending (it had been taken as RISC OS's own, which put the
  signal stack into the SVC registers: random aborts);
- only SWIs made in a privileged mode are patched, never OS_EnterOS (the
  stub pushes on the caller's stack, which user-mode code may not have).

It worked (the suites passed natively and under QEMU, and every
application tried started), but counting with `*ROLSWIPatch` across six
applications showed 85–95% of SWIs come from outside the ROM - the
applications, UnixLib, modules from disc - and most of the ROM's own are
made in user mode, so it sped up a few percent of them.

**Then: a fast SWI entry in Linux itself.** A small patch to the Pi's
(6.6) and the other machines' (7.2) kernels, `0200-riscos-fast-swi.patch`
in riscos-linux: RISC OS's process registers its SWI handler and its one
gateway for real Linux calls once (`prctl PR_RISCOS_SWI`), and from then
on any other SVC goes straight to the handler with a small frame of just
the registers - no seccomp, no signal frame, no floating point state, no
signal mask. Linux calls from the gateway are untouched, and no other
process is affected. NativeSupport 0.16 turns it on at start-up
(`*NativeSupport FastSWI On|Off`; on other kernels it says so and SWIs go
the usual way). It needs no change to the ROM: it works with any ROM.

**Results on the Raspberry Pi 4:**

- a SWI: about 2.4 µs down to 745 ns (SWICost);
- ROBench: OS_SWI 3.3 times faster, OS_ReadVarVal 2.4, BPUT 4.2,
  BGET 2.0, Draw strokes 2.8; the CPU, floating point, memory and NEON
  tests unchanged, as they make no SWIs.

Released as riscos-support 1.0.0017-1 (NativeSupport 0.16) and the
kernels riscos-kernel-pi 6.6.78-4, riscos-kernel-generic and
riscos-kernel-wdk2023 7.2.0-3. (riscos-support 1.0.0015-1 and 1.0.0016-1
had shipped NativeSupport 0.15, which replaced a 0.16 installed by hand:
a ROBench on 5.31-8 showed SWIs back at the slow speed.)

## The `swi-fastpath` test branch

Builds `5.31-N-swi` (from 5.31-3): the ROM's own SWIs that trap are
patched to go straight to RISC OS (`*ROLSWIPatch On|Count|Off`). Measured
in applications, 85–95% of SWIs come from outside the ROM, so this helps
little; on the Raspberry Pi the kernel's fast SWI entry (riscos-linux
kernels with NativeSupport 0.16) speeds up every SWI instead, with any
ROM. The branch carries every `nightly-merge` change.

Copyright (c) 2026 Andrew Youll.
