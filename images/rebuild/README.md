# Rebuilding the images from a release

`mkroot20.sh`, `mkgen20.sh` and `mkwdk20.sh` made images 1.0.0020 from the
1.0.0019 images the same way, with riscos-rom 5.31-19, riscos-support
1.0.0037-1 and riscos-harddisc 1.0.0038-1 (the kernels are 1.0.0019's):

```
mkroot20.sh riscos-generic-1.0.0019.img ROOT POOL
mkgen20.sh  riscos-generic-1.0.0019.img ROOT riscos-generic.img
OFF=$((257 * 2048)) mkroot20.sh riscos-wdk2023-1.0.0019.img WROOT POOL
mkwdk20.sh  riscos-wdk2023-1.0.0019.img WROOT riscos-wdk2023.img
```

`mkroot19.sh`, `mkgen19.sh` and `mkwdk19.sh` made images 1.0.0019 from
the published 1.0.0018 images and the newest packages in the
[riscos-rolpkg](https://github.com/adyoull/riscos-rolpkg) pool, without
rebuilding Debian:

```
mkroot19.sh riscos-generic-1.0.0018.img ROOT POOL
mkgen19.sh  riscos-generic-1.0.0018.img ROOT riscos-generic.img
OFF=$((257 * 2048)) KERNELS=wdk2023 mkroot19.sh riscos-wdk2023-1.0.0018.img WROOT POOL
mkwdk19.sh  riscos-wdk2023-1.0.0018.img WROOT riscos-wdk2023.img
```

(as root: loop mounts, and the files' owners kept). `mkroot19.sh` copies
the old image's root file system, puts the packages in as dpkg would
(riscos-rolpkg's `tools/preinstall.py`, then removing files the new
versions no longer have) and does their postinsts' work against the copy.
The others keep the old image's boot partition, replace its kernels,
device trees, README and licences, and write a new disc.

The UTM machine is the generic image as a compressed qcow2 (`qemu-img
convert -c -O qcow2`, resized to 32 GB) with the 1.0.0018 machine's
`config.plist`, given new UUIDs, a new MAC address and the new name.

`../mkimage-generic.sh` and `../mkimage-wdk2023.sh` build an image from
scratch.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
