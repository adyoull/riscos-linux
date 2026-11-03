# Changelog

What each RISC OS on Linux ROM build changed. The ROM reports itself as
"RISC OS 5.31-N (date)". Each change is one commit on `nightly-merge`
(the commit message has the detail); `NIGHTLY-MERGE.md` says where each
one is and what to watch when maintaining it. ROL installs the ROM as the
`riscos-rom` package.

## 5.31-14 (not yet built)

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
