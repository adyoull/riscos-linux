# What RISC OS on Linux adds to Timothy Baldwin's port

RISC OS on Linux is built on Timothy Baldwin's
[Linux port of RISC OS](https://github.com/TimothyEBaldwin/RISC_OS_Linux_Source).
His work made it possible:
- the RISC OS "ROM" as a Linux program;
- its HAL on top of Linux's system calls;
- IXFS (RISC OS files on Linux's disc);
- the SDL front end, the sandbox and the build.

The version this is built on is from December 2022, with RISC OS 5.29.

This page says plainly what has been added since, first against his own
to-do list, then everything else.

## His to-do list

His README's to-do list had these items still open:

| His to-do item | Now |
|---|---|
| Add sound output | **Done.** RISC OS Open's own sound modules (SoundDMA and the modules above it) play through Linux: HDMI, the Pi's headphone socket or a USB sound card. The volume is set in Configure → Sound and with `*MixVolume`. |
| Perform proper error handling in IXFS | **Done.** HardDisc4 and USB discs give RISC OS's own errors ("The disc has no more available space", "This item is locked", "This file is already open"), obey the Locked attribute, and no longer lose failed deletes. Files can be over 2 GB (up to 4 GB − 1), and the free space is right. |
| Detect screen updates to avoid copying the full screen 20 times a second | **Done.** Only what changed is redrawn, and moving the pointer redraws nothing. |
| Finish the video, keyboard and mouse driver | **Done, as a new front end.** rol-fb drives the screen through Linux's KMS, with no desktop or SDL needed. It has: the monitor's own size and modes (by name, from its EDID); a hardware pointer; the keyboard LEDs; Break on keyboards without one; the scroll wheel; and the video overlay. |
| Provide a means to run Linux programs | **Done.** `*Linux` runs a command as root, after asking for the root password. `*LinuxUser` and `*IXRun` run one as RISC OS's own Linux user, with no password. |
| Implement PMP dynamic areas | **Done.** RISC OS's kernel does physical memory pool areas itself (RISC OS 5.31-3), so GCCSDK's ARMEABISupport 1.08 and the programs that need it (Iris) work. Memory checks answer for them page by page. |
| Implement VFPSupport | **Done.** VFPSupport for the port does everything RISC OS Open's VFPSupport 0.18 does that the port allows, including the elementary functions and FastAPI. It passes RISC OS Open's own VFPSupport tests (except lazy activation). |
| Write test cases for VFP errors | **Partly.** VFPSupport has RISC OS Open's tests and our own. Floating point and NEON are checked against over 20 million results, bit for bit, flags included. There are no tests of VFP exceptions on their own. |
| Possibly add a binary release | **Done.** Ready-made images and a signed package repository (below). |
| Fix overwriting the ROM image while rebuilding RISC OS, while it's running | **Not addressed** for building. On an installed machine, Updates replaces the ROM safely and keeps the old one to go back to. |

The items he had already ticked (the key map, the clock, timestamps, the
officially allocated names and numbers, and so on) were his.

## Everything else

**A complete system, not a program to run:**
- **The images** start straight into the RISC OS desktop on a Raspberry Pi
  3/4/5, UEFI machines, the Windows Dev Kit 2023, the Pinebooks and Apple
  Silicon Macs (UTM), on Debian 13.
- **The Linux side** (init, rol-fb, rol-netd, rol-snd, rol-cmd, rol-usbd):
  - starts RISC OS and enlarges the disc to fill the card;
  - sets the clock from the internet and keeps it across starts;
  - keeps the start-up logs on the FAT partition;
  - restarts or switches off when RISC OS asks.
- **Updates** (in Configure) keeps RISC OS, its Linux side, the kernel and
  Debian up to date from the desktop, with a list of what each update
  changes, and **going back** (Roll back, `ROLLBACK`, the old kernels kept).
- **Protected RISC OS:** RISC OS runs as an ordinary Linux user and sees
  only its own files. `*Linux` and Updates ask for the root password.

**RISC OS itself brought up to date:**
- **RISC OS 5.31:** RISC OS Open's sources of 29 October 2026 merged into
  the port, which was 5.29.
- **Since then, 18 builds:**
  - MbufManager in the ROM (!Omni);
  - socket fixes (Socket_Select, closed connections, servers answering);
  - all the RAM Linux can give (up to 4 GB);
  - file types from extensions;
  - a disc name for IXFS (StrongEd);
  - a hardware counter that ticks every centisecond;
  - GPIO on a Pi 3 or 4;
  - two interrupt fixes that stopped rare hangs.

**Running natively, and fast:**
- **NativeSupport** lets programs run as they were compiled. On
  processors that run 32-bit ARM programs (Pi 3, 4 and 5, the Dev Kit, the
  Pinebooks), it patches privileged instructions into user mode ones on
  the fly, as code is loaded or written, so programs run natively instead
  of failing on instructions Linux doesn't allow. That covers UnixLib
  programs, modules, BASIC's assembler and big programs like the web
  browser. It keeps them compatible: what a program reads back of itself,
  and the files tools save, are as they were written. So the DDE still
  compiles valid binaries on RISC OS on Linux.
- **VFP and NEON for RISC OS programs.** Timothy's port had no
  VFPSupport (it was on his to-do list), so programs couldn't use VFP or
  NEON through it. With VFPSupport for the port they can, natively and
  under QEMU, and `*BASIC64` works as on a Pi.
- **Kernel changes for speed (natively):**
  - RISC OS's SWIs go straight to RISC OS instead of through Linux's
    security filter and a signal: about 3× faster than before on a Pi 4
    (native RISC OS is still about 4.5× faster again);
  - FPA instructions go straight to the FPEmulator: 4.5× faster than
    before.
- **QEMU (Macs and other machines without 32-bit ARM):**
  - SWIs about 4.9× faster than with Timothy's QEMU changes alone;
  - floating point and NEON on the host's own FPU and vector unit: VFP 1.1
    to 1.4×, NEON 1.9 to 4.7× faster than QEMU's standard emulation, and
    checked bit for bit against it.

See [Speed, measured](#speed-measured) for the figures and what each is
compared with.

**Hardware and the desktop:**
- **Networking:** wired and Wi-Fi, set up in Configure → Linux network.
  RISC OS's name servers follow Linux's.
- **USB discs and SD cards** on the icon bar: FAT, exFAT, NTFS and ext4.
  Ones in RISC OS's own format appear as `SCSI::` drives, through RISC OS
  Open's SCSIFS.
- **HardDisc4** as a hard disc icon, with Free and Dismount.
- **USB devices Linux has no driver for** (phones, cameras) through RISC
  OS's own USB interface, so PDFS works.
- **The Pi's hardware video decoding** (LinuxCodec) and **video on the
  display's own overlay plane** (Reel).
- **The scroll wheel** (RISC OS Open's WindowScroll, built for the port).
- **Programs and PackMan:**
  - NetSurf, RDPServer and Folio come installed;
  - PackMan comes set up with RISC OS Open's lists, riscosdev.com's and
    riscos-packages.
- **The SWI and error numbers** for the new modules allocated by RISC OS
  Open.

## Speed, measured

Each figure says what it's compared with. "Before" is RISC OS on Linux
before that change. "Native RISC OS" is RISC OS 5.31 booted on the same
Raspberry Pi 4 from its SD card, with no Linux. Native RISC OS is still
faster at most things. A Mac can't run RISC OS natively, so the Mac
figures compare against QEMU's standard emulation.

**Raspberry Pi 4, natively (2–5 November 2026)**

| Test | Before | Now | Native RISC OS |
|---|---|---|---|
| One SWI | 2.4 µs (through Linux's security filter and a signal) | 745 ns (the fast SWI entry): about 3× faster | 162 ns: still about 4.5× faster than RISC OS on Linux |
| 500,000 FPA instructions | 27 cs (a Linux signal each time) | 6 cs (the fast FPA entry): 4.5× faster | not measured with this test |
| RiscOSM drawing a map | 32 s | 30 s (with the fast SWI entry; NativeSupport 0.18's later change not yet timed) | 17 s |
| Opening and closing a file | – | 118 µs | 2.45 ms: RISC OS on Linux is about 20× faster (Linux's file cache) |
| Reading 64 KB from a file already read | – | 4.4 GB/s | 21 MB/s (Linux's file cache) |
| Listing a directory (OS_GBPB 10) | – | 184 µs | 24 µs: native is about 7.6× faster |

**Apple Silicon Mac, under QEMU (UTM)**

| Test | Before | Now |
|---|---|---|
| SWIs per second | 694,000 (QEMU with Timothy's changes for the port) | 3.4 million: about 4.9× faster |
| VFP floating point (ROBench) | QEMU's standard emulation | 1.1 to 1.4× faster |
| NEON (ROBench's NEON and image tests) | QEMU's standard emulation | 1.9 to 4.7× faster |

The floating point and NEON results are checked bit for bit against
QEMU's exact emulation: over 20 million results.

The full history is in
[RELEASE-NOTES.md](../RELEASE-NOTES.md#the-full-history).

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
