# Image build files

How the generic AArch64 image (Raspberry Pi 3/4/5, UEFI machines, Pinebook,
Pinebook Pro) is put together. These are the sources the image's Linux-side
programs are built from (`aarch64-linux-gnu-gcc -O2 -static`):

| File | What |
|---|---|
| `fe/init.c` | `/sbin/init`: mounts, network (rol-netd), rol-cmd, sound (rol-snd), SNTP, then starts RISC OS (natively, or under QEMU) with rol-fb as its screen. When RISC OS ends it saves the clock and the logs, then switches off, or restarts the machine if RISC OS asked to restart (exit status 100: Ctrl+Break, Restart, `*Reset`) |
| `fe/rol-fb.c` | the screen, keyboard and mouse: KMS (the first card with a screen connected), the video overlay for LinuxCodec. Only what changed is redrawn: RISC OS's screen memory is compared row by row with a copy, and the changed rectangle is converted, scaled and sent; the pointer is drawn on top (two 32x32 squares when it moves). `ROL_FB_FULL=1` in its environment redraws everything every time, for comparison |
| `fe/rol-netd.c` | wired and Wi-Fi networking (DHCP, wpa_supplicant) for Configure's Linux network plug-in |
| `fe/rol-snd.c` | sound: plays what LinuxAudio sends through ALSA |
| `fe/rol-cmd.c` | runs Linux commands for RISC OS (*Linux, !Updates) through files in /riscos/cmd |
| `mkimage-generic.sh` | builds the image: MBR (partition 1 from 16 MiB), FAT boot partition, Debian 13 slim root with RISC OS in /opt/riscos and /riscos |
| `mbr.py` | writes the MBR |
| `kernel-*.config` | the kernels' configurations (Raspberry Pi's 6.6, mainline 7.2) |

The root file system is Debian 13 "trixie" slim for arm64, from Debian's
official container image build (debuerreotype, the `dist-arm64v8` branch of
github.com/debuerreotype/docker-debian-artifacts), with
`/etc/dpkg/dpkg.cfg.d/docker-apt-speedup` (unsafe I/O) removed.
