# dl/: local copies of downloads (not kept in git)

Put files here to build without downloading them:

- `qemu_8.2.2+ds.orig.tar.xz` (qemu/build.sh)
- `HardDisc4.5.30.util` (the container image build; or set ROL_HD4_FILE for `make harddisc`)
- `BCM2835.5.30.zip`, RISC OS Open's Pi ROM, for BASICVFP (or set ROL_PIROM_FILE for `make basicvfp`)
- `gcc-10.2.0.tar.gz`, or a `gcc-10.2.0/` source folder (top-level files and `config/`) together with a `riscos-gccsdk/` checkout (`autobuilder/develop/gcc`), for UnixLib's build in the container image

The container image build mounts this folder for one step; nothing in it is copied into the image.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
