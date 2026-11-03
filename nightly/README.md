# Building the latest RISC OS Open nightly for Linux

This branch (`nightly-merge`) is Timothy Baldwin's Linux port with RISC OS
Open's development sources merged in (5.31, 29 Oct 2026). Details of that
merge are in `NIGHTLY-MERGE.md` at the top of the tree.

## Build it (on a Raspberry Pi)

RISC OS Open's DDE is needed, and it may only be run on the Pi it's installed
on, so the build runs there, natively. Any Pi that runs the Linux port works
(a Pi 4, or a Pi 5 on the 4K-page kernel).

1. Install the tools:

       sudo apt install git gcc make bubblewrap attr pkg-config \
                        libseccomp-dev libattr1-dev libsdl2-dev

2. Put the DDE next to the source, so that `../DDE/AcornC.C++` exists
   (or set `DDE=/path/to/DDE`).

3. From the top of the source:

       nightly/build-on-pi.sh

   The script checks it's on a Pi that runs 32-bit ARM code natively, then runs
   the port's own build (`make TARGET=Linux METHOD=Linux check`). That builds the
   ROM under the bootstrap RISC OS (`Unix/RISCOS.IMG`) and runs the port's tests.
   The result is `Built/RISCOS.IMG`. Use it with `./run_RISC_OS`, or copy it
   over `/opt/riscos/RISCOS.IMG` (or wherever your image keeps it).

The first build of a newly merged nightly may stop with assembler or
compiler errors. This merge has not been built yet. Errors are most likely
in the Kernel, where upstream code refers to things the Linux memory layout
doesn't have.

## Update to a newer nightly

1. Download the "IOMD HAL" development source tarball from
   <https://www.riscosopen.org/content/downloads/risc-os-tarballs>.

2. From the top of the source:

       nightly/update.sh ~/Downloads/src-iomd-dev.5.32.tar.bz2

   This commits the tarball on `rool-vendor`, which holds upstream exactly as
   released. It then runs `git merge` into the current branch, so only what
   upstream changed since the last nightly gets merged. Most nightlies will
   merge without conflicts.

3. `nightly/check.py` runs automatically after the merge. It reports the
   things that break the Linux port:
   - assembler nesting that changed;
   - Linux, PVOps or KSWI lines that went missing;
   - new raw `MSR`/`MRS` on CPSR/SPSR (RISC OS runs in user mode on Linux, so
     these need `SEI`/`CLI`/`SavePSR`/`pvMSR`/`pvMRS`);
   - new `SWI`s in the Kernel.

   Anything inside `[ :LNOT: Linux` or `[ PagedMemory` is fine as it is.

4. Commit if conflicts needed resolving, then build as above.

## Rules of thumb when resolving

- Take upstream's logic, but keep the port's Linux structure: `[ Linux`,
  `[ PagedMemory` (which is false on Linux), `pvMSR`/`pvMRS`, `KSWI`,
  `MOVS_PC_LR`.
- `hdr/KernelWS`: Linux has its own address map (ROM at &20800000, the
  workspace from &20100000). `RWBase` for Linux must match the `-RW-base` in
  `Kernel/Makefile`.
- New ROM modules upstream adds to `Components/ROOL/IOMD32` may be wanted in
  `Components/ROOL/Linux` too. Hardware drivers aren't wanted there.

Copyright (c) 2026 Andrew Youll.
