# riscos-linux

The source of **RISC OS on Linux**: RISC OS 5 running on Linux (Debian 13)
on 64-bit ARM machines. The same image starts on a Raspberry Pi
3/4/5/Zero 2 W, in UTM on an Apple Silicon Mac, on other UEFI machines,
and on Pine64's Pinebook and Pinebook Pro. The Windows Dev Kit 2023 has
its own image. Where the processor runs 32-bit ARM code (a Pi, the Dev
Kit), RISC OS runs natively; elsewhere it runs under a patched QEMU.

RISC OS itself is RISC OS Open's, in Timothy Baldwin's
[Linux port](https://github.com/TimothyEBaldwin/RISC_OS_Linux_Source).
This repository holds everything else in the images, and how they're
built. Ready-built packages (RISC OS, the Linux side, the kernels) come
from [riscos-rolpkg](https://github.com/adyoull/riscos-rolpkg), which
Configure's Updates uses.

## What's where

| Part of the image | Source here | Licence |
|---|---|---|
| RISC OS 5.31 (the ROM, `/opt/riscos/RISCOS.IMG`) | branch [`riscos-5.31-source`](../../tree/riscos-5.31-source): RISC OS Open's sources of 29 October 2026 merged into the Linux port | Apache-2.0 (most), BSD-3-Clause (some); see each component's LICENSE |
| The Linux side: `init`, `rol-fb` (screen, keyboard, mouse), `rol-snd` (sound), `rol-netd` (network, Wi-Fi), `rol-cmd` (Linux commands, root password), `rol-usbd` (USB for RISC OS) | `images/fe/` | MIT |
| `qemu-arm-riscos` (RISC OS on machines without 32-bit ARM) | `qemu/` (patch to QEMU 8.2.2, `build.sh`) | GPL-2.0-or-later, as QEMU |
| RISC OS modules: NativeSupport, VFPSupport, ARMEABISupport, LinuxAudio, LinuxShell (`*Linux`), USBDriver, KVMWheel, Iconv, LinuxCodec and the VideoCore stand-ins (VCHIQ, BCMSupport, PCI), SoundControl, WindowScroll | `modules/` | MIT, except RISC OS Open's code where marked (`LICENCES.md`) |
| Desktop parts: Configure's Updates, Linux network and Scroll wheel plug-ins, the hard disc icon, CaCertificates, PackMan's sources | `harddisc/extras/` (and `harddisc/fetch.sh`, which builds HardDisc4 from RISC OS Open's) | MIT; Mozilla's certificates MPL-2.0 |
| ROBench | `robench/` | MIT |
| Linux 7.2 for UEFI machines and the Pinebooks | `kernel/generic-7.2/` (patches to mainline v7.2, config) | GPL-2.0 |
| Linux 7.2 for the Windows Dev Kit 2023 | `kernel/wdk2023-7.2/` (the same patches, config) | GPL-2.0 |
| Linux 6.6 for the Raspberry Pi | `kernel/pi-6.6/` (raspberrypi/linux unchanged at the commit in `BASE`, config) | GPL-2.0 |
| wpa_supplicant 2.11 with libnl 3.11 (static) | `wifi/` (`build.sh`, libnl's source) | BSD-3-Clause; libnl LGPL-2.1 |
| Debian 13 | Debian's own (https://www.debian.org) | as each package says |
| U-Boot for the Pinebooks | Ubuntu's `u-boot-sunxi`, `u-boot-rockchip` 2025.10-0ubuntu2.2 (source: Ubuntu) | GPL-2.0 |
| Firmware (Wi-Fi, Raspberry Pi) | linux-firmware, raspberrypi/firmware (`kernel/pi-6.6/BASE`) | as each LICENCE file says (`licences/`) |
| PDFS | [riscos-pdfs](https://github.com/adyoull/riscos-pdfs) | MIT |

The licence texts are in `licences/` and on each image's FAT partition.

## Building

- **The images:** `images/mkimage-generic.sh` (the generic image) and `images/mkimage-wdk2023.sh`
  (the Dev Kit's), from a staged Debian root. See `images/README.md`.
- **The kernels:**
  - **Generic and Dev Kit:** check out mainline v7.2, apply `kernel/generic-7.2/*.patch`, copy the config, then
    `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image`.
  - **Raspberry Pi:** raspberrypi/linux at the commit in `kernel/pi-6.6/BASE`, with `kernel/pi-6.6/config`.
  - **Firmware:** `CONFIG_EXTRA_FIRMWARE_DIR` points at `firmware/`: put the Wi-Fi firmware listed in the config there.
- **The Linux side:** `aarch64-linux-gnu-gcc -O2 -static images/fe/NAME.c -o NAME`.
- **RISC OS modules, QEMU, tests:** `make RCD_DIR=...` (needs
  [riscos-crossdev](https://github.com/adyoull/riscos-crossdev)); see `TESTBED.md`.
- **RISC OS 5.31:** the `riscos-5.31-source` branch, built as the Linux port's
  own README says.

`TESTBED.md` describes the test bed this grew from: running RISC OS programs
on Linux headless, for CI.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
