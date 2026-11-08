# RISC OS on Linux 1.0.0020 (8 November 2026): the first public release

RISC OS 5.31 (5.31-19: RISC OS Open's nightly in Timothy Baldwin's Linux
port) on Debian 13, for 64-bit ARM machines: Raspberry Pis, UEFI
machines and virtual machines, Apple Silicon Macs (UTM), the Windows
Dev Kit 2023 and the Pinebooks. RISC OS runs straight on the processor
where it can (Raspberry Pi 3, 4 and 5, the Dev Kit, the Pinebooks) and
under QEMU where it can't (Apple's processors).

This is a test release: it runs well on the machines below, but please
[report](../README.md#reporting-a-problem) anything that doesn't work.

![The desktop](../docs/screenshots/1.0.0019/00-desktop.png)

## The images

| Image | For | File |
|---|---|---|
| Generic | Raspberry Pi 3/3B+/CM3, Zero 2 W, 4/400/CM4, 5/500/CM5; UEFI machines (QEMU, ARM boards and servers); Pinebook and Pinebook Pro | `riscos-generic.img.xz` |
| UTM | Apple Silicon Macs | `RISC OS 1.0.0020.utm` (in `RISC OS 1.0.0020.utm.zip`) |
| Windows Dev Kit 2023 | Snapdragon 8cx Gen 3 ("Project Volterra") only | `riscos-wdk2023.img.xz` |

Write the .img.xz to a card or USB disc with Raspberry Pi Imager ("Use
custom") or balenaEtcher. Unzip the .utm and double-click it to add it to
UTM. Each image starts straight into the RISC OS desktop; Shutdown powers
the machine off. `SHA256SUMS` lists the files' checksums. How to install
on each machine is in [the README](../README.md#installing).

The Dev Kit image is only for the Dev Kit: anywhere else Linux finds no
screen and the machine switches off a minute later.

Already running an earlier test build (1.0.0006 or later)? **Configure →
Updates** brings it to the same versions without reinstalling.

## Updates since this release

None yet. Configure → Updates → Check for updates finds them as they
come.

## Versions in this release

| Package | Version |
|---|---|
| RISC OS (`riscos-rom`) | 5.31-19 |
| The Linux side (`riscos-support`) | 1.0.0037-1 (NativeSupport 0.33, rol-fb 3) |
| The desktop parts (`riscos-harddisc`) | 1.0.0038-1 |
| Kernels | Pi 6.6.78-6; generic and Dev Kit 7.2.0-4 |
| Linux | Debian 13 (trixie) |

## What RISC OS on Linux adds to RISC OS

RISC OS on Linux is built on Timothy Baldwin's Linux port of RISC OS.
[What it adds to his port](../docs/what-we-added.md) lists every change plainly, starting
with the open items on his to-do list. Of those, sound output, IXFS's
error handling, redrawing only what changed, the screen, keyboard and
mouse driver, running Linux programs, PMP dynamic areas, VFPSupport and a
binary release are done.


Everything RISC OS has, plus these, which native RISC OS doesn't:

**The start-up screen** says what's happening (the disc being enlarged on
the first start, the network, the clock), and the logs of every start are
copied to the card's FAT partition, so a machine with a dark screen can
still say what went wrong.

![The start-up screen](../docs/screenshots/1.0.0019/01-startup-screen.png)

**Configure** has three new plug-ins: **Linux network**, **Updates** and
**Scroll wheel**.

![Configure](../docs/screenshots/1.0.0019/02-configure.png)

**Linux network** sets up wired or Wi-Fi networking, with DHCP or a fixed
address, and shows how the network is doing. RISC OS's own Internet
module uses it as it is, and RISC OS's name servers follow Linux's, so a
network that comes up late still works.

![Configure → Linux network](../docs/screenshots/1.0.0019/03-linux-network.png)

**Screen** starts the desktop at the monitor's own size. On a Pi it shows
the monitor by name, with its modes; under UTM, the window's size.
Resizing a virtual machine's window changes the mode to fit.

![Configure → Screen](../docs/screenshots/1.0.0019/04-screen.png)

**Scroll wheel** scrolls the window under the pointer, either way round
(reversed, as on a Mac, if you like).

![Configure → Scroll wheel](../docs/screenshots/1.0.0019/05-scroll-wheel.png)

**Updates** keeps the whole machine up to date from the desktop: RISC OS
itself (the ROM), its Linux side, the kernel and the desktop parts come
as packages from [riscos-rolpkg](https://github.com/adyoull/riscos-rolpkg),
and Debian's security fixes come too.
- **Check for updates** shows the new versions.
- **Changes** says what each one changes before you install it.
- **Install updates** shows apt's progress as it goes.
- **Roll back RISC OS** puts back the build before. A new RISC OS that
  won't start goes back by itself if you put an empty `ROLLBACK` file on
  the card's FAT partition.

Here a 1.0.0018 machine is updated to this release:

![Updates finds the new versions](../docs/screenshots/1.0.0019/11-updates-found.png)

![Changes: what each new version changes](../docs/screenshots/1.0.0019/12-updates-changes.png)

![Installing](../docs/screenshots/1.0.0019/13-updates-installing.png)

![Installed: restart to use them](../docs/screenshots/1.0.0019/14-updates-installed.png)

**`*Linux`** runs a Linux command from RISC OS and prints its output, in a
task window or at the command line. For example, `*Linux apt-get install
htop`.

![*Linux in a task window](../docs/screenshots/1.0.0019/10-linux-command.png)

**Protected RISC OS:** RISC OS runs as an ordinary Linux user, so a RISC OS
program can't change Linux or reach the rest of the disc. `*Linux` and
Updates are the deliberate way through, so they ask for Linux's root
password, which you choose the first time. `*IXRun` runs Linux programs
as RISC OS's own Linux user, with no password.

![The root password, chosen the first time](../docs/screenshots/1.0.0019/09-root-password.png)

**Discs:**
- **HardDisc4** is a hard disc icon on the icon bar, with Open '$', Free
  (Linux's figures for the disc) and Dismount. It grows to fill the card
  or disc at the first start.
- **IXFS:$** shows what RISC OS can see of the Linux side.
- **USB discs and SD cards** appear on the icon bar when plugged in, named
  by their volume labels: FAT, exFAT, NTFS and ext4. Ones in RISC OS's own
  format (FileCore) appear as `SCSI::` drives, as on a native Pi. Eject
  is on their menu.

![HardDisc4's menu](../docs/screenshots/1.0.0019/06-harddisc4-menu.png)
![HardDisc4 and its Free window](../docs/screenshots/1.0.0019/07-harddisc4-free.png)
![IXFS:$](../docs/screenshots/1.0.0019/08-ixfs.png)

**And:**
- **The clock** is set from the internet as soon as the network is up, and
  kept across starts on a Pi, which has no clock battery.
- **Sound** comes from RISC OS's own sound modules, played through Linux:
  HDMI, the Pi's headphone socket or a USB sound card.
- **USB devices that Linux has no driver for** (phones, tablets and
  cameras in file transfer mode) are given to RISC OS programs through
  RISC OS's own USB interface. PDFS mounts phones this way.
- **Video:** LinuxCodec gives RISC OS programs the Pi's hardware H.264
  decoder, and Reel's video goes on the display's own overlay plane.
- **GPIO** on a Pi 3 or 4, for programs and the Raspberry Pi edition of
  ROOL's DDE.
- **Programs that come with it:** NetSurf, RDPServer and Folio (the PDF
  and ebook reader), installed as PackMan installs them, so PackMan
  upgrades them.
- **PackMan is already set up**, with nothing to add: RISC OS Open's
  two package lists, riscosdev.com's autobuilt packages (Iris, the web
  browser, among them) and [riscos-packages](https://github.com/adyoull/riscos-packages)
  (Andrew Youll's apps and ports). The certificates PackMan needs for the
  https lists are installed too (!CaCertificates).

## Tested

| Machine | |
|---|---|
| Raspberry Pi 4 | Andrew Youll: GPIO, and the Raspberry Pi DDE (RISC OS built with it) |
| Raspberry Pi 5 | Chris Gransden (1.0.0017) |
| Windows Dev Kit 2023 | Chris Gransden (1.0.0017) |
| UTM on an Apple Silicon Mac | Chris Gransden (1.0.0017); this release's QEMU in a Linux VM on an Apple Silicon Mac |
| QEMU (UEFI, Cortex-A76, natively) | this release's generic image and the Dev Kit image's root: the desktop, *Linux, RISC OS 5.31-19 and NativeSupport 0.33; with 5.31-19, a USB mouse at both mouse speeds, 4K in 16 million colours, menus and pointer moves, and the tablet; Iris 1.039 loads bbc.co.uk. With 1.0.0019's packages: Configure, the discs, 1.0.0018 updated through Updates; 39 of the 40 programs on the disc start |
| Raspberry Pi 3, Zero 2 W | untried |
| Pinebook, Pinebook Pro | untried: treat as experimental |

**Thank you to Chris Gransden** for his testing and reports: the UTM
start-up problem, the Updates error and the Iris 1.039 problem were his
finds.

## Known issues

- The Filer can stop with an abort in the kernel's sprite plotting when
  opening certain directories (seen with !RiscOSM's).
- On a Raspberry Pi, ROBench's BPut/BGet tests fail to open their file.
- Discs inside the machine in RISC OS's own format (FileCore on an
  internal SSD or NVMe) aren't offered to RISC OS; USB discs and SD cards
  are.
- Socket_Connect can report success for a connection that was refused.
- Iris isn't included: install it with PackMan (riscosdev.com's list is
  already set up).

Please send reports with the `LOGS` folder from the FAT partition.

## The full history

RISC OS on Linux started as a test bed on 15 October 2026, for running
Timothy Baldwin's Linux port of RISC OS on 64-bit ARM machines. Getting
here took 25 days: about 300 commits across the test bed, the build
(riscos-linux) and the package repository (riscos-rolpkg); 19 RISC OS
builds; 37 versions of the Linux side, 38 of the desktop parts and 10
kernel updates; and 20 image builds.

Newest first. Every package version is listed below, folded. Speed figures compare with RISC OS on Linux before that change, not with
native RISC OS. [Speed, measured](../docs/what-we-added.md#speed-measured) has both.

### 8 November, afternoon: this release (1.0.0020)

From Chris Gransden's report on a Raspberry Pi 5 with a 4K monitor, and
from EDT's and crossdev's developers:
- **RISC OS 5.31-19:**
  - Configure's mouse speed works with a mouse. Its movement now reaches
    RISC OS as movement, which RISC OS scales by the mouse speed, as on a
    Pi. It was taken as a position, so the speed was always one pixel per
    count: slow on a big screen. Tablets (UTM) work as before.
  - The screen can use up to 64 MB, so 4K and 5K work in 16 million
    colours, not only 32 thousand.
  - Programs that move the pointer move it on the screen.
  - A Thumb program's undefined instruction is reported at the right
    address; memory checks (OS_Memory 24) answer for PMP areas page by
    page under QEMU.
- **The pointer** (rol-fb): the right size on a 4K monitor (the desktop
  is 1920 × 1080 shown twice the size), and drawn where RISC OS has it
  from the start.
- **NativeSupport 0.32 and 0.33:** making or removing a dynamic area no
  longer stops the desktop with "Internal error: abort on data transfer"
  (EDT the debugger as it starts; static UnixLib programs as they end,
  during a ROM build for example).
- **UTM's monitor list** (Configure → Screen) has the common sizes from
  2880 × 1800 to 5120 × 2880.
- The images rebuilt with all of it.

### 8 November: 1.0.0019 (not published)

- **The SWI and error numbers RISC OS Open allocated:**
  - NativeSupport's SWIs are NativeSupport_PMPOp (&5AF40) and
    NativeSupport_AbortReturn (&5AF41), with names.
  - LinuxCodec's SWIs are &5AF00–&5AF08.
  - The modules' errors are &8235xx (LinuxAudio), &8236xx (LinuxCodec and
    the VideoCore stand-ins), &8237xx (LinuxShell) and &8238xx
    (NativeSupport).
- The images rebuilt with everything below.

### 6–7 November: Iris 1.039, and RISC OS 5.31-18

- **Iris 1.039 works** (NativeSupport 0.30): script-heavy pages such as
  bbc.co.uk stopped half-drawn. NativeSupport had prepared part of the
  browser's JavaScript engine (Thumb-2 code) as if it were ARM code.
- **The web browser's JavaScript compiler works**, and so do debuggers
  reading shared library memory.
- **The DDE's tools:** programs they save come out as they were written.
- **EDT starts**, and returns into Thumb-2 code keep their condition
  state.
- **RISC OS 5.31-18:**
  - RISC OS no longer stops now and then on the start-up screen.
  - An interrupt no longer spoils a program's switch back to user mode.
- **Big programs** (over 64 MB, like the web browser) **run natively**, and
  so does Iris 1.0.29 again.
- **Updates repairs damaged package lists** by itself (after a power cut
  during an update).
- **Configure → Screen** shows the monitor by name, with its modes.

### 5 November: 1.0.0018, Folio, and faster floating point

- **Folio**, the PDF and ebook reader, comes with it, and PDF files open
  in it.
- **FPA floating point about 4.5 times as fast on a Pi** (the fast FPA
  entry in the kernels).
- **The last session's logs are kept** after a restart or a power cut,
  with a note of how it ended.
- ARMEABISupport goes in !System.400, where PackMan's package puts it.
- **RISC OS starts again under QEMU** (5.31-17.1): 5.31-16 and -17 were
  installed readable only by root.

### 4 November: 1.0.0017; faster under QEMU, GPIO, StrongEd

- **Faster under QEMU** (UTM on a Mac): NEON's integer instructions run as
  the Mac's own vector instructions (ROBench's image tests 1.6 to 4.5
  times as fast). SWIs are about 1.25 times as fast.
- **GPIO on a Pi 3 or 4.** The Raspberry Pi DDE's C compiler and
  assembler run, and RISC OS itself has been built with it on a Pi 4.
- **Saving from StrongEd 4.70:** IXFS discs have a disc name, ROL.
- **Finer timing:** the hardware counter reloads every centisecond, as on
  a Pi (SDL games' frame timing).
- **Updates shows its progress** while installing. The drive icons survive
  a replacement pinboard.

### 3 November: faster natively; *IXRun; network servers; big files

- **Programs run faster natively:** NativeSupport 0.18 returns to programs
  without a Linux signal (RiscOSM's map drawing lost 10 of its 30 seconds
  to them).
- **`*IXRun` works**, as RISC OS's own Linux user, with no password; the
  new `*LinuxUser` too.
- **RDPServer and NetSurf** come with it.
- **Changes** in Updates: what an update brings, before installing it.
- **Files over 2 GB** on HardDisc4 and USB discs (up to 4 GB − 1).
- **Saving to HardDisc4 from StrongEd**, which checks the free space
  first.
- **A connection closed by the other end is seen at once** (ClawsMail's
  fetches took a minute each).
- **PDFS 0.16.2** for phones and cameras.

### 2 November: RISC OS's SWIs three times as fast; FileCore discs; video

- **The fast SWI entry** (NativeSupport 0.16 and the kernels): RISC OS's
  SWIs go straight to RISC OS instead of through Linux's security filter
  and a signal. On a Pi 4 that's 2.4 µs down to 745 ns per SWI (ROBench's
  OS_SWI 3.3 times as fast, BPUT 4.2).
- **FileCore discs:** USB sticks and SD cards in RISC OS's own format
  appear as `SCSI::` drives, with RISC OS Open's SCSIFS, and HForm can
  format them. They're handed over whole, opened exclusively, with every
  write on the disc before RISC OS is told it's done.
- **Video in hardware on a Pi:** Reel's video on the display's own plane,
  scaled, with no tearing.
- **Files get the type of their extension** (`photo/jpg` is a JPEG), as
  on DOS discs.
- **MbufManager in the ROM:** !Omni's LanManFS and Access work.
- **Network servers answer their clients** (RDPServer).
- The keyboard's LEDs follow RISC OS; SD cards in the machine's own slot
  work like USB sticks.

### 30 October – 1 November: the first packages and images

- **RISC OS 5.31:** RISC OS Open's nightly of 29 October merged into
  Timothy Baldwin's Linux port, which was RISC OS 5.29.
- **Packages and Updates:** RISC OS, its Linux side, the kernels and the
  desktop parts as Debian packages from a signed repository, kept up to
  date from Configure, with roll back.
- **Debian 13** as the base, with RISC OS protected as an ordinary user,
  and `*Linux` with a root password.
- **The images grow** to fill the card, **keep their logs** on the FAT
  partition, and **set the clock** by NTP.
- **A start-up screen**, and restart that restarts.
- **USB discs and SD cards** on the icon bar: FAT, exFAT, NTFS and ext4.
- **All the RAM Linux can give** (up to 4 GB). Proper RISC OS errors from
  HardDisc4, and the Locked attribute obeyed.
- **The Pi's H.264 decoder** for RISC OS programs (LinuxCodec).
- **Pinebook and Pinebook Pro** support, with Ubuntu's U-Boot.

### 15–29 October: the test bed

The work that made RISC OS on Linux run well, before there were packages
(test bed 0.1 to 0.10, and the images that followed):

- **Running natively** on processors that run 32-bit ARM programs (Pi 3,
  4 and 5, the Dev Kit), without QEMU: the NativeSupport module prepares
  RISC OS code for running as a Linux program and emulates what Linux
  won't do (0.10, 18 October).
- **QEMU made fast** for Macs, profiled while ROBench ran:
  - SWIs 4.8 times as fast;
  - floating point and NEON sent to the host's FPU and vector unit only
    where the answer can't differ (NEON 1.9 to 4.7 times as fast on a
    Mac);
  - checked against 20 million results bit for bit.
- **Sound:** RISC OS Open's own sound modules played through Linux.
- **The scroll wheel:** RISC OS Open's WindowScroll, built for the port.
- **Iris and ARMEABISupport 1.08:** physical memory pool areas, aborts and
  the timer that AcornSSL reads.
- **The screen** at the monitor's size, through Linux's DRM. Also USB
  keyboards and mice, wired and Wi-Fi networking, and UTM's ready-made
  machine.
- **The images:** the Raspberry Pi, UEFI, the Windows Dev Kit 2023 and UTM.

<details>
<summary><b>Every version of RISC OS (riscos-rom)</b></summary>

| Version | Date | What changed |
|---|---|---|
| 5.31-19 | 8 Nov | Configure's mouse speed works with a mouse; 4K and 5K in 16 million colours (64 MB screen); programs moving the pointer move it on screen |
| 5.31-18 | 6 Nov | No more stops on the start-up screen as the desktop starts; an interrupt no longer spoils a program's switch back to user mode (SPSR_svc kept) |
| 5.31-17.1 | 5 Nov | Starts again under QEMU (5.31-16 and -17 were readable only by root) |
| 5.31-17 | 4 Nov | GPIO on a Pi 3 or 4, for programs and the Raspberry Pi DDE |
| 5.31-16 | 4 Nov | IXFS discs have a disc name, ROL, so StrongEd 4.70 can save |
| 5.31-15 | 4 Nov | The hardware counter reloads every centisecond (finer timing) |
| 5.31-14 | 3 Nov | *IXRun through the shell, as RISC OS's own Linux user |
| 5.31-13 | 3 Nov | Files over 2 GB |
| 5.31-12 | 3 Nov | A closed connection seen at once; IXFS's free space (StrongEd) |
| 5.31-10 | 2 Nov | FileCore discs as `SCSI::` drives; the keyboard LEDs |
| 5.31-8 | 2 Nov | Files get their extension's type; MbufManager in the ROM; servers answer their clients |
| 5.31-6 | 1 Nov | Socket options for GLib programs |
| 5.31-5 | 31 Oct | The Internet module reports 5.73 (!Omni starts) |
| 5.31-4 | 31 Oct | Socket_Select's timeout; locked files can't be renamed |
| 5.31-3 | 31 Oct | RISC OS errors from HardDisc4; physical memory pools in the kernel |
| 5.31-2 | 31 Oct | All the RAM Linux can give (up to 4 GB); network statistics |
| 5.31-1 | 30 Oct | RISC OS Open's 5.31 nightly merged into the Linux port (5.29) |

</details>

<details>
<summary><b>Every version of the Linux side (riscos-support)</b></summary>

| Version | Date | What changed |
|---|---|---|
| 1.0.0037-1 | 8 Nov | rol-fb: the pointer drawn where RISC OS has it from the start |
| 1.0.0036-1 | 8 Nov | NativeSupport 0.33: the same after a dynamic area is removed (static UnixLib programs ending) |
| 1.0.0035-1 | 8 Nov | NativeSupport 0.32: making a dynamic area no longer stops the desktop |
| 1.0.0034-1 | 8 Nov | rol-fb 3: mouse movement to RISC OS as movement (its mouse speed applies, with 5.31-19); modes up to 64 MB listed |
| 1.0.0033-1 | 8 Nov | rol-fb: the pointer the right size on a 4K monitor |
| 1.0.0032-1 | 8 Nov | NativeSupport 0.31: RISC OS Open's SWI and error numbers |
| 1.0.0031-1 | 7 Nov | NativeSupport 0.30: Iris 1.039, the JavaScript compiler, the DDE's tools, EDT, debuggers |
| 1.0.0030-1 | 6 Nov | Iris 1.0.29 starts natively again |
| 1.0.0029-1 | 6 Nov | Big programs (over 64 MB) natively; Configure → Screen shows the monitor |
| 1.0.0028-1 | 5 Nov | FPA floating point about 4.5 times as fast on a Pi |
| 1.0.0027-1 | 5 Nov | The last session's logs kept, and how it ended |
| 1.0.0026-1 | 4 Nov | Faster under QEMU: NEON integer, quicker jumps |
| 1.0.0025-1 | 4 Nov | GPIO registers for protected RISC OS |
| 1.0.0024-1 | 3 Nov | Commands as RISC OS's own Linux user (*IXRun, *LinuxUser) |
| 1.0.0023-1 | 3 Nov | Each package's changes, for Updates' Changes |
| 1.0.0022-1 | 3 Nov | NativeSupport 0.18: returns without a signal; Eject retries |
| 1.0.0021-1 | 3 Nov | The FAT partition never left mounted at a restart |
| 1.0.0020-1 | 2 Nov | SD cards in the machine's own slot, like USB sticks |
| 1.0.0019-1 | 2 Nov | FileCore discs given whole; fixed and removable drives |
| 1.0.0018-1 | 2 Nov | The keyboard LEDs; FileCore discs handed to RISC OS |
| 1.0.0017-1 | 2 Nov | NativeSupport 0.16: the fast SWI entry |
| 1.0.0016-1 | 2 Nov | USB disc files not marked executable |
| 1.0.0015-1 | 2 Nov | Reel's video on the display's video plane |
| 1.0.0014-1 | 1 Nov | USB discs and SD cards in RISC OS |
| 1.0.0013-1 | 31 Oct | Shutdown leaves the disc clean |
| 1.0.0012-1 | 31 Oct | Updates shows apt's progress line by line |
| 1.0.0011-1 | 31 Oct | Only what changed on the screen is redrawn |
| 1.0.0009-1 | 31 Oct | Restart restarts |
| 1.0.0008-1 | 31 Oct | /dev/pts mounted (apt's log) |
| 1.0.0007-1 | 31 Oct | Alt+Shift+Backspace as Break |
| 1.0.0006-1 | 31 Oct | The start-up screen |
| 1.0.0004-1 | 31 Oct | The clock set by NTP and kept; RISC OS's RAM from what Linux has free |
| 1.0.0002-1 | 31 Oct | The disc grows to fill the card; logs on the FAT partition |
| 1.0.0001-1 | 30 Oct | The first |

</details>

<details>
<summary><b>Every version of the desktop parts (riscos-harddisc)</b></summary>

| Version | Date | What changed |
|---|---|---|
| 1.0.0038-1 | 8 Nov | UTM's monitor list with 4K and 5K sizes |
| 1.0.0037-1 | 8 Nov | LinuxCodec, LinuxAudio and LinuxShell: RISC OS Open's numbers |
| 1.0.0036-1 | 6 Nov | Updates repairs damaged package lists |
| 1.0.0035-1 | 6 Nov | The sound, USB and SCSI modules keep SPSR_svc in their tickers |
| 1.0.0034-1 | 5 Nov | ARMEABISupport in !System.400, where PackMan puts it |
| 1.0.0033-1 | 5 Nov | PDF files open in Folio however the boot is ordered |
| 1.0.0032-1 | 5 Nov | Folio, the PDF and ebook reader |
| 1.0.0031-1 | 5 Nov | Updates: "String too long at line 445" fixed |
| 1.0.0030-1 | 4 Nov | IXFS:$ on the icon bar, labelled IXFS:$ |
| 1.0.0029-1 | 4 Nov | Updates shows what it's doing while it installs |
| 1.0.0028-1 | 4 Nov | The drive icons start from their own boot file |
| 1.0.0027-1 | 3 Nov | NetSurf 3.11 |
| 1.0.0026-1 | 3 Nov | RDPServer 0.2 |
| 1.0.0025-1 | 3 Nov | *IXRun always through *LinuxUser |
| 1.0.0024-1 | 3 Nov | PDFS 0.16.2 beta |
| 1.0.0023-1 | 3 Nov | *IXRun as RISC OS's own Linux user |
| 1.0.0022-1 | 3 Nov | *IXRun through *Linux |
| 1.0.0021-1 | 3 Nov | Updates' Changes window |
| 1.0.0020-1 | 3 Nov | !DiscIcon: windows close on eject; a busy disc reported |
| 1.0.0019-1 | 2 Nov | SCSILinux: removable or fixed as Linux says |
| 1.0.0018-1 | 2 Nov | SCSILinux at start-up (FileCore discs) |
| 1.0.0017-1 | 2 Nov | LinuxOverlay (Reel's video in hardware); *USBDevices lists Linux's devices |
| 1.0.0016-1 | 1 Nov | The removable drive icon; MbufManager for older ROMs |
| 1.0.0015-1 | 1 Nov | GCCSDK's ARMEABISupport 1.05 |
| 1.0.0014-1 | 31 Oct | The VideoCore stand-ins (ReelHWAccel's H.264) |
| 1.0.0013-1 | 31 Oct | Updates stays open while installing |
| 1.0.0012-1 | 31 Oct | NetWatch reads the network in one go |
| 1.0.0010-1 | 31 Oct | LinuxCodec at start-up |
| 1.0.0009-1 | 31 Oct | LinuxCodec 0.03 |
| 1.0.0007-1 | 31 Oct | LinuxCodec: the Pi's H.264 decoder |
| 1.0.0006-1 | 31 Oct | NetWatch and Updates fixes |
| 1.0.0004-1 | 31 Oct | NetWatch: name servers follow the network |
| 1.0.0001-1 | 30 Oct | The first |

</details>

<details>
<summary><b>Every kernel update</b></summary>

| Kernel | Version | Date | What changed |
|---|---|---|---|
| Dev Kit | 7.2.0-4 | 6 Nov | The fast FPA entry |
| Pi | 6.6.78-6 | 5 Nov | The fast FPA entry (FPA 4.5 times as fast on a Pi 4) |
| generic | 7.2.0-4 | 5 Nov | The fast FPA entry |
| Pi | 6.6.78-5 | 4 Nov | /dev/gpiomem for RISC OS's GPIO |
| Pi | 6.6.78-4 | 2 Nov | The fast SWI entry (2.4 µs to 745 ns per SWI) |
| generic, Dev Kit | 7.2.0-3 | 2 Nov | The fast SWI entry |
| Pi | 6.6.78-3 | 1 Nov | NTFS discs |
| generic, Dev Kit | 7.2.0-2 | 1 Nov | exFAT discs and fast USB discs (UAS) |
| Pi | 6.6.78-2 | 31 Oct | The kernels keep their own versions |
| generic, Dev Kit | 7.2.0-1 | 31 Oct | The same |

</details>

The full details of every change are in
[riscos-rolpkg's CHANGELOG](https://github.com/adyoull/riscos-rolpkg/blob/main/CHANGELOG.md)
and in Configure → Updates → Changes.

## Source and licences

All the source is at [riscos-linux](https://github.com/adyoull/riscos-linux)
(RISC OS 5.31-18 on the `riscos-5.31-source` branch). Each image carries
`SOURCE.txt` and a `LICENCES` folder on its FAT partition, and
HardDisc4.Documents.Licences and .Source.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
