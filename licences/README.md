# Licence texts

The licences of what the RISC OS on Linux images contain (see the table in
the top-level README for which part is under which):

| File | For |
|---|---|
| `GPL-2.0`, `Linux-COPYING`, `Linux-syscall-note` | the Linux kernels |
| `QEMU-COPYING`, `LGPL-2.1` | qemu-arm-riscos (GPL-2.0-or-later); libnl (LGPL-2.1) |
| `Apache-2.0` | RISC OS (most of RISC OS Open's sources and the Linux port) |
| `RISCOS-Open-BSD-3-Clause` | the RISC OS Open components under BSD-3-Clause |
| `LICENCE.wpa_supplicant`, `LICENCE.libnl` | wpa_supplicant (BSD-3-Clause), libnl |
| `LICENCE.cypress`, `LICENCE.broadcom_bcm43xx`, `LICENCE.rtlwifi_firmware.txt` | Wi-Fi firmware |
| `RaspberryPi-firmware-LICENCE.broadcom` | the Raspberry Pi start-up firmware |
| `U-Boot-copyright` | U-Boot (Ubuntu's packages) |
| `CaCertificates-certifi-LICENSE` | Mozilla's root certificates (MPL-2.0) |

Debian's packages carry their own, in `/usr/share/doc/*/copyright` on the
image. This repository's own code is MIT (`LICENSE`).
