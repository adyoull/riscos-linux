RISC OS on Linux 1.0.0020 for 64-bit ARM machines (8 November 2026)
===================================================================
One image for:
  - Raspberry Pi 3 B / 3 B+ / CM3, Zero 2 W, 4 / 400 / CM4 and 5 / 500 / CM5
    (the Pi's own firmware starts kernel8.img);
  - 64-bit ARM machines that boot from UEFI: the firmware starts
    EFI/BOOT/BOOTAA64.EFI from this partition. That covers virtual
    machines (UTM, QEMU, Parallels: on an Apple Silicon Mac the ready-made
    "RISC OS 1.0.0020.utm" is easier) and ARM boards and servers with
    standard UEFI firmware (ACPI, or a device tree from the firmware).
    The Windows Dev Kit 2023 needs its own device tree: use its own image
    (riscos-wdk2023) instead;
  - Pine64's Pinebook (Allwinner A64) and Pinebook Pro (Rockchip RK3399),
    with U-Boot (below) starting EFI/BOOT/BOOTAA64.EFI.

Version 1.0, build 0020: RISC OS 5.31 (5.31-19: RISC OS Open's
sources merged into Timothy Baldwin's Linux port) with
RISC OS Open's HardDisc4 5.30, on Debian 13 (the slim base
Debian's own container images use, about 100 MB: no Linux desktop, no
systemd; it still starts straight into RISC OS).
Raspberry Pis use Raspberry Pi's kernel 6.6; UEFI machines use a mainline
7.2 kernel. RISC OS runs straight on the processor (natively) when it can
run 32-bit ARM programs, otherwise under QEMU (slower). The start-up
screen says "Starting RISC OS (native)..." when it runs natively.
*NativeSupport shows what NativeSupport (the module that makes native
running work) has done.

New in build 0020 (every package update since build 0019): RISC OS
5.31-19 (Configure's mouse speed works with a mouse; 4K and 5K screens
in 16 million colours; programs that move the pointer move it on the
screen); the pointer the right size on a 4K monitor, and where RISC OS
has it from the start; NativeSupport 0.33 (making or removing a dynamic
area, as static UnixLib programs do when they end, no longer stops the
desktop); UTM's monitor list has 4K and 5K sizes.

New in build 0019: RISC OS 5.31-18 (no more stops on the start-up
screen; a program's switch back to user mode no longer goes wrong now
and then); NativeSupport 0.31 (Iris 1.039 and the web browser's
JavaScript compiler work, big programs run natively, FPA floating point
about 4.5 times as fast on a Pi); the last session's logs kept after a
restart; Configure, Screen shows the monitor's own name and modes; the
Pi kernel 6.6.78-6 and the generic kernel 7.2.0-4.

Native or QEMU: a file /riscos/native on the Linux partition saying "off"
runs RISC OS under QEMU, "on" always natively; without it (or "auto") it
runs natively when the processor can (Pi 3, 4 and 5 can; Apple's
processors, and many ARM servers' cores, can't).

Writing it: Raspberry Pi Imager, "Use custom", pick riscos-generic.img.xz
(or balenaEtcher, or xz -d then dd). Any 4 GB or bigger card or USB
stick. For a virtual machine, use the uncompressed .img as its disk.

Starting: it boots straight into the RISC OS desktop at the monitor's own
resolution. Keyboard and mouse: USB. On UEFI machines the screen is
whatever the firmware set up (the EFI framebuffer), or virtio-gpu in a
virtual machine.

Sound: RISC OS Open's own sound modules, played through Linux: HDMI if
the screen takes sound, else the Pi's headphone socket; a USB sound card
comes first when one is plugged in. To choose, put a line "device=CARD"
in /riscos/sound/config on the Linux partition. Volume: Configure, Sound,
or *MixVolume.

Network: wired Ethernet with DHCP, as it comes. Configure's "Linux
network" plug-in sets it: wired or Wi-Fi (Pi 3 B, 3 B+, Zero 2 W, 4, 5,
500, CM4, CM5, Pinebook, Pinebook Pro), DHCP or a manual address.
The settings, Wi-Fi password included, are kept in plain text in
/riscos/net/config.

Protected RISC OS: RISC OS runs as an ordinary Linux user
("riscos"), not as root, and sees only its own files: IXFS:$ shows
riscos (HardDisc4 and its settings), opt and etc (read only), and its own
dev, proc, tmp and run. A program in RISC OS can't change Linux or reach
the rest of the disc. To run it as before (root, the whole disc), change
/etc/riscos/protect on the Linux partition to say "off" (RISC OS itself
can't change that file). *Linux and !Updates run their commands as root:
they're the deliberate way through, so they ask for the root password.

The root password: Linux's root account starts with no
password (it can't be logged into). The first time *Linux or !Updates
runs a command, it asks you to choose one; after that it asks for it,
and doesn't ask again until it has gone unused for five minutes.
*LinuxPassword changes it (from a task window or the command line: it
asks for the current one, then the new one twice). It is Linux's real
root password, kept hashed in /etc/shadow, which RISC OS programs can't
read. Five wrong tries in a row: wait a minute.
Forgotten it? Put the card in any computer and create an empty file
called RESETPW on the image's small FAT partition (the one with
kernel8.img). The next start clears the root password and removes the
file, and the next *Linux or !Updates asks you to choose a new one.
What it guards against: a RISC OS program quietly running Linux commands
as root. It can't stop a program that watches what you type (RISC OS
programs all share one Linux user).

USB discs and SD cards: plug one in and its icon appears on the icon
bar (after Apps), named by its volume label; click to open it. Linux
mounts it (FAT, exFAT, NTFS or ext4) and RISC OS sees it through IXFS.
Its menu has Free and Eject; eject before unplugging, so everything is
written. Discs are let go cleanly at shutdown too.

USB devices for RISC OS programs: Linux drives the USB
hardware, so RISC OS's own USB stack can't run; instead the image's
USBDriver gives RISC OS programs RISC OS's USB device interface (DeviceFS
"USB<n>" devices, Service_USB) on top of Linux's. It has the devices
Linux itself has no driver for: phones, tablets and cameras in file
transfer (MTP/PTP) mode, and the like (keyboards, mice, discs, sound and
network adapters stay Linux's). *USBDevices lists them. So PDFS works
unchanged: HardDisc4.TestBed.PDFS has PDFS 0.16.2 beta (*RMLoad it, or put
it in PreDesk as on a Pi, then *PDFSMount). Tried with QEMU's MTP
device: browsing and copying files both ways; not yet with a real phone.

Keeping Linux up to date: HardDisc4.Utilities.!Updates
checks for and installs Debian's updates (security fixes included) from
the desktop, through apt-get; it needs the network. The *Linux command
runs any Linux command from RISC OS (as root) and prints its output, e.g.
*Linux apt-get install htop in a task window. RISC OS itself, its
Linux side and the kernels are packages too (see Updates below).

HardDisc4 on the icon bar is a hard disc icon, like ADFS's
or SCSI's, rather than a folder: Select opens it; Menu has Open '$',
Free (RISC OS's own Free window, with Linux's figures for the disc) and
Dismount (removes the icon until the next start). Files and directories
dropped on the icon are copied into HardDisc4's top level,
with the Filer's usual copy window; a program's Save box dragged to it
saves there.

Screen mode: the desktop starts at the screen's own size
(the VM's window, or the monitor's native resolution, in 16 million
colours; halved on very large screens, a 4K monitor starting at 1920 x
1080 shown twice the size). 4K in 16 million colours is a choice in
Configure, Screen (RISC OS's screen can use up to 64 MB).
Choose another in Configure, Screen and click Set: that one is then used
at every start. In a
VM, resizing the window still changes the mode to fit, for that session.

Updates: Configure, Updates keeps RISC OS on Linux itself up
to date as well as Debian: RISC OS (the ROM), its Linux side, the kernel
this machine starts and the desktop parts come as packages from
https://github.com/adyoull/riscos-rolpkg. Check for updates, Install
updates (then restart to use them), or Roll back RISC OS to the build
before. If a new RISC OS won't start, create an empty file called ROLLBACK
on the FAT partition from any computer: the next start puts the old one
back. A new kernel keeps the old one there as *.old. (Utilities.!Updates
opens the same window.)

The version (Task manager, Info, or *Fx 0) gives the date of RISC OS
Open's nightly it's built from ("5.31 (29 Oct 2026) nightly");
PackMan lists RISC OS Open's packages, riscos-packages and
riscosdev.com's autobuilt packages; and the licences
and where to get the source are in LICENCES and SOURCE.txt on the FAT
partition, and in HardDisc4.Documents.

Mouse: left = Select, middle = Menu, right = Adjust; the wheel scrolls
the window under the pointer.

Shutdown (Task manager's menu, or Ctrl-Shift-F12) powers off.

Pinebook and Pinebook Pro (not yet tried on the machines)
---------------------------------------------------------
These have no UEFI firmware of their own: U-Boot starts the image, the
same way Debian and Ubuntu do it. The U-Boot is Ubuntu's (u-boot-sunxi
and u-boot-rockchip 2025.10-0ubuntu2.2, the build Ubuntu 26.04 ships),
in the u-boot folder of this image's FAT partition. Write the image to
the card first, then that machine's U-Boot into the gap before the first
partition (it starts at 16 MB, so nothing else is touched). On a Mac,
with the card as /dev/diskN (diskutil list; diskutil unmountDisk
/dev/diskN first), from the card's RISCOS volume:
  Pinebook (A64):
    sudo dd if=u-boot/pinebook/u-boot-sunxi-with-spl.bin of=/dev/rdiskN bs=1k seek=8 conv=notrunc
  Pinebook Pro (RK3399):
    sudo dd if=u-boot/pinebook-pro/u-boot-rockchip.bin of=/dev/rdiskN bs=512 seek=64 conv=notrunc
(Linux: of=/dev/sdX or /dev/mmcblk0, same numbers.) A card can carry one
of the two, not both (they overlap); a card with either still starts on
a Raspberry Pi or a UEFI machine as before.

The Pinebook starts from the SD card ahead of its eMMC, so its U-Boot on
the card is what runs. The Pinebook Pro starts from SPI flash or its eMMC
before the SD card: if that has a recent U-Boot (Tow-Boot, or the U-Boot
of Manjaro, Armbian or Debian), it will usually find and start this
card's EFI/BOOT/BOOTAA64.EFI by itself (the card is checked first), and
the dd above isn't needed. u-boot-rockchip-spi.bin is the same U-Boot
for SPI flash, for those who want it there.

The kernel has the A64's and RK3399's drivers built in: the internal
screens (eDP), HDMI, SD and eMMC, USB (the keyboard and touchpad are
USB), sound, battery and charging, and Wi-Fi: the
Pinebook's RTL8723CS (Linux's rtw88 driver, Realtek's firmware) and the
Pinebook Pro's AP6256 (BCM43456, Linux's brcmfmac driver, the firmware
and settings Armbian ships for it). Set it up in Configure, "Linux
network", as on a Pi. Not supported: the Pinebook Pro's USB-C video. RISC OS runs natively on
both (their processors run 32-bit ARM programs).

Video decoding and the video overlay (Pi 3 and 4): the Pi
kernel has VideoCore's codec drivers (bcm2835-codec), and the
LinuxCodec module lets RISC OS programs use its H.264 decoder and show
the pictures on the display hardware's overlay, scaled, under the
desktop. Everything is in HardDisc4.TestBed.Codec: the module, two
tests (CodecTest decodes a test clip and reports the speed; OverlayTest
plays it on the overlay) and a ReadMe with the SWIs. Not yet tried on a
real Pi: please report what CodecTest and OverlayTest say.

The disc: at each start, if the card or disc (or a VM's disc) is bigger
than the image, the Linux partition and its file system are enlarged to
fill it, so HardDisc4 gets all the space. An empty file called NOGROW on
the FAT partition stops that.

Programs that come with it, besides RISC OS's own: NetSurf (web
browser) and RDPServer (remote desktop) in Apps.Network, and Folio (PDF
and ebook reader) in Apps.Document, installed as PackMan installs them,
so PackMan upgrades them. PDF files open in Folio when double-clicked
(delete !Boot.Choices.Boot.Tasks.FolioPDF to choose another viewer).

Starting up: a start-up screen says what's happening (the disc being
enlarged on the first start, the network, the clock, RISC OS starting),
and again when RISC OS ends, while Linux saves the clock and logs and
switches off.

The time: Linux sets the clock from the internet (NTP, pool.ntp.org) as
soon as the network is up, however late that is, and every hour after;
RISC OS reads Linux's clock. A Pi has no clock battery, so the time is
also kept on the card and put back at the next start. RISC OS's name
servers follow Linux's network too (NetWatch), so a network that comes
up after the desktop still works for PackMan, NetSurf and the rest.

A dark screen? The start-up logs are copied to the FAT partition, which
any computer can read: start the machine, wait two minutes, switch off,
and look in LOGS (riscos-log.txt, kernel-log.txt) on the card.

Problems: create an empty file HardDisc4.NativeDebug (*Create
<IXFS$HardDisc4>.NativeDebug) and restart; NativeSupport's fault reports
then go into the start-up log (/riscos/log.txt on the Linux partition,
IXFS:$.riscos.log/txt in RISC OS, also on the serial port). Ctrl+Break
stops a hung RISC OS and logs where it was.

Licences: the Wi-Fi chips' firmware is built into the kernels and is
redistributable under its makers' licences: Cypress's (Infineon) for the
Pis and the Pinebook Pro (LICENCE.cypress, LICENCE.broadcom_bcm43xx) and
Realtek's for the Pinebook (LICENCE.rtlwifi_firmware.txt).
wpa_supplicant 2.11 is BSD licensed: LICENCE.wpa_supplicant; libnl:
LICENCE.libnl.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
