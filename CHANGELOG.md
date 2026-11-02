# Changelog

What each RISC OS on Linux ROM build changed. The ROM reports itself as
"RISC OS 5.31-N (date)". Each change is one commit on `nightly-merge`
(the commit message has the detail); `NIGHTLY-MERGE.md` says where each
one is and what to watch when maintaining it. ROL installs the ROM as the
`riscos-rom` package.

## 5.31-9 (2 November 2026, not yet built)

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

- **RISC OS Open's 5.31 nightly (29 October 2026) merged into Timothy
  Baldwin's port**, built natively with the DDE on a Raspberry Pi
  (`nightly/BuildROM`).
- Fixes from the first native builds: aborts (no CP15 reads in user
  mode), the undefined instruction handler, abortable dynamic areas.
- RAM from Linux: `ROL_RAMSIZE_MB` (up to 4095MB) sets RISC OS's RAM, and
  the free pool counts the memory not yet used, so the Task Manager's
  Free is right.
- Network statistics in Internet 5's layouts (Socket_Sysctl,
  Socket_InternalLookup), for NetStats and InetStat.
- The version string: "RISC OS 5.31-2 (29 Oct 2026)".

## The `swi-fastpath` test branch

Builds `5.31-N-swi` (from 5.31-3): the ROM's own SWIs that trap are
patched to go straight to RISC OS (`*ROLSWIPatch On|Count|Off`). Measured
in applications, 85–95% of SWIs come from outside the ROM, so this helps
little; on the Raspberry Pi the kernel's fast SWI entry (riscos-linux
kernels with NativeSupport 0.16) speeds up every SWI instead, with any
ROM. The branch carries every `nightly-merge` change.
