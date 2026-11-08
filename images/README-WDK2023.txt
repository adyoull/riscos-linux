RISC OS on Linux 1.0.0019 for the Windows Dev Kit 2023 ("Project Volterra")
===========================================================================
Version 1.0, build 0019 (8 November 2026): Timothy
Baldwin's Linux port of RISC OS 5 (RISC OS 5.31) with RISC OS Open's
HardDisc4 5.30, on Debian 13 (minimal) with the mainline 7.2 kernel. The same system as the generic
image (Raspberry Pi, UEFI machines, Pinebooks). The Dev Kit's processor (Snapdragon 8cx Gen 3) runs 32-bit ARM
programs, so RISC OS runs natively on it, without QEMU: the test bed's
NativeSupport module does what QEMU does on the Mac.

Only for the Dev Kit: its kernel loads the Dev Kit's own device tree, so
on anything else (UTM, QEMU, a Pi) Linux finds no screen and RISC OS
stops at once (the machine switches off a minute later). For those, use
riscos-generic.img.xz, or "RISC OS 1.0.0019.utm" on a Mac.
This build shows all Linux start-up messages on the screen, and keeps
them in /riscos/kernel-log.txt on the image's Linux partition as they
come, so if the screen goes off or it stops, they can be read afterwards
(start-riscos.sh puts them in its report).

Writing it: Raspberry Pi Imager ("Use custom"), balenaEtcher, or xz -d
then dd, to a USB stick or USB SSD of 4 GB or more. Windows on the Dev
Kit's own SSD is left alone.

Starting it:
  1. In Windows: Settings, System, Recovery, Advanced startup, Restart
     now; then Troubleshoot, Advanced options, UEFI Firmware Settings.
  2. In the firmware's settings, set Secure Boot to None (the kernel
     isn't signed by Microsoft; "Microsoft and 3rd party CA", which
     Ubuntu boots with, isn't enough), enable USB boot and put USB first
     in the boot order.
  3. Plug the stick into a USB-A port (not USB-C) and restart. If the
     image is on a disc in a USB-C port, it still starts, but leaves the
     DSPs off (no USB-C devices), because starting them resets the USB-C
     ports and would take the disc away.
To go back to Windows, take the stick out (and turn Secure Boot back on
if you want it).

Starting it from Linux instead: if USB boot doesn't work, or you'd rather
stay in the Linux already on the Dev Kit (Ubuntu, say), start-riscos.sh
(next to this file on the website) runs it from the stick or an unpacked
image file without restarting. From a text console (Ctrl+Alt+F3):
  sudo ./start-riscos.sh            (finds the stick by itself)
  sudo ./start-riscos.sh riscos-wdk2023.img
It does what a USB boot does (native mode, network, screen, NativeSupport,
!Boot); the desktop is stopped while RISC OS runs and started again after.
Shutting RISC OS down ends only RISC OS. --no-network leaves Linux's
networking alone (RISC OS then has none); --debug has NativeSupport
report every fault and undefined instruction it passes to RISC OS into
the log, with the code and stack around it; --qemu runs RISC OS under QEMU instead
of natively, to compare. After each run it writes riscos-report.txt in
the current directory (this machine's details, RISC OS's log, the kernel's
messages, and those kept from the last USB boot): send that with any
problem report.

Screen: the mini DisplayPort socket. RISC OS uses the monitor's own mode
(other RISC OS modes are scaled to fit). Keyboard and mouse: the USB-A
ports.

Network: the wired Ethernet port, with DHCP; Configure's "Linux network"
plug-in sets it. Wi-Fi isn't supported on this machine yet.

Firmware: the Dev Kit's audio and compute DSPs and its GPU need
Microsoft's firmware, which can't be put on this image. The first time
it starts, the image looks for it in Windows's driver store on the
internal SSD (read-only; Windows isn't changed) and copies it to its own
partition (/lib/firmware/qcom/sc8280xp/microsoft/blackrock): the start-up
screen says "Firmware from Windows: ..." for each file. Without it, the
screen and the USB-A ports still work; the USB-C ports (and a screen on
them) need it.

Sound: the Dev Kit has no speakers or headphone socket, and sound over
DisplayPort doesn't work on Linux yet. Use a USB sound card or USB
headset.

Native or QEMU: a file /riscos/native on the image's Linux partition
saying "off" runs RISC OS under QEMU instead, "on" always natively;
without it (or "auto"), natively. *NativeSupport shows what the
NativeSupport module has done.

Shutdown (Task manager's menu, or Ctrl-Shift-F12) powers off.

If RISC OS hangs: Ctrl+Break (Ctrl+Pause on a PC keyboard; Ctrl+Alt+
Backspace on one without a Pause key) stops it at once, like Ctrl+Break
resetting a real RISC OS machine: the USB boot then powers off, and
start-riscos.sh gives you Linux's desktop back. What RISC OS was doing
goes into the log first. Alt+Break works as on RISC OS (the Task
manager offers to stop the current application) while RISC OS still
responds.
The disc: at each start, if the USB disc is bigger than the image, the
Linux partition and its file system are enlarged to fill it (HardDisc4
gets all the space). An empty file called NOGROW on this (EFI) partition
stops that. The start-up logs are also copied to LOGS on this partition
for the first three minutes, for reading on another computer.

Start-up log: IXFS:$.riscos.log/txt in RISC OS (/riscos/log.txt on the
Linux partition).

Other USB discs and SD cards: plug one in (into the Dev Kit or a hub)
and its icon appears on the icon bar (after Apps), named by its volume
label; click to open it. Linux mounts it (FAT, exFAT, NTFS or ext4) and
RISC OS sees it through IXFS. Its menu has Free and Eject; eject before
unplugging, so everything is written. Discs are let go cleanly at
shutdown too.

Licences: the Linux kernel is GPL-2.0 (source: kernel.org, v7.2); the
device tree is the kernel's (BSD-3-Clause). RISC OS for Linux and
HardDisc4 are under their own licences (see HardDisc4).


The version (Task manager, Info, or *Fx 0) gives the date of RISC OS
Open's nightly it's built from ("5.31 (29 Oct 2026) nightly");
PackMan lists RISC OS Open's packages, riscos-packages and
riscosdev.com's autobuilt packages; and the licences
and where to get the source are in LICENCES and SOURCE.txt on the EFI
partition, and in HardDisc4.Documents.

Updates, and protecting RISC OS
-------------------------------
Debian 13 underneath, kept up to date, and RISC OS on Linux itself too:
Configure, Updates checks for and installs new builds of RISC OS, its
Linux side, the Dev Kit's kernel and the desktop parts (from
https://github.com/adyoull/riscos-rolpkg) and Debian's updates, and can
roll RISC OS back to the build before. Restart after installing.
If a new RISC OS won't start: make an empty file called ROLLBACK on this
(EFI) partition from any computer; the next start puts the old one back.
A new kernel keeps the old one here as EFI/BOOT/BOOTAA64.EFI.old.

Protected RISC OS: RISC OS runs as an ordinary Linux user and sees only
its own files. *Linux runs Linux commands (as root) and asks for Linux's
root password: the first time, you choose one; *LinuxPassword changes it.
Forgotten it? An empty file called RESETPW on this partition clears it.

HardDisc4 is a hard disc icon on the icon bar (Free, drag to copy). The
desktop starts at the monitor's resolution until a mode is chosen and
saved in Configure, Screen.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
