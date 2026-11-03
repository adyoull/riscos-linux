# AGENTS.md: notes for AI coding assistants

riscos-linux-testbed runs RISC OS programs, built with riscos-crossdev, in
the Linux port of RISC OS (a RISC OS 5 ROM image that runs as a Linux
program, under `qemu-arm-riscos` on x86). People read `README.md`; this is
the short version.

## Setting up

```sh
# riscos-crossdev installed (RCD_DIR), or work inside its container image.
sudo apt-get install ninja-build python3-venv libglib2.0-dev autoconf2.69 automake1.11
pip3 install --break-system-packages meson==1.2.3
make RCD_DIR=...          # qemu (minutes), modules, ROM, tools, test programs
make unixlib RCD_DIR=...  # patched UnixLib (without it, 3 thread tests are XFAIL)
make harddisc basicvfp RCD_DIR=...  # HardDisc4, BASICVFP (from riscosopen.org)
make check RCD_DIR=...    # must end "51 passed, 0 failed, 0 expected failures, 0 unexpected passes"
```

## Rules that matter

- Run programs with `bin/rol-run`. Exit statuses: the program's own, or
  125 for a RISC OS error, 124 for a time-out, 117 for a stop at the
  prompt, 126 for set-up failure.
- The modules must stay position-independent:
  - `VFPSupport.s` must assemble with no relocations (the Makefile checks;
    R_ARM_V4BX markers are fine). Everything is one assembly unit: no
    `.global` symbols, and `adrl` for anything far away.
  - `modules/VFPSupport/elementary/v3`, `v4` are made by `objasm2gas.py`
    from ROOL's sources: change the script, not the output.
  - `ARMEABISupport` must pass `check-pic.py`. In its C, don't put
    pointers in initialised data (pointer tables, `static T *p = &x;`):
    only GOT entries are relocated.
- Module C is freestanding: no C library, no floating point
  (`-mgeneral-regs-only`). The helpers are in `modules/common/cmodule.c`.
- RISC OS on the port runs in user mode. Privileged instructions
  (MSR CPSR_c, MRS SPSR, STM/LDM ^) in code loaded at run time work only
  because the test bed's QEMU, or NativeSupport running natively,
  emulates them (each one a signal natively): don't write new code that
  needs them. Set the V flag with `msr APSR_nzcvq`.
- `NativeSupport` runs inside Linux signal handlers: no SWIs there, and
  any access to an address the interrupted code gave goes through
  `probe_call`. Check native changes with `ROL_VM=tools/native-vm/vmexec
  tests/run.sh` as well as the QEMU run.
- Don't commit the ROM image, HardDisc4, UnixLib builds or QEMU builds.
  `rom/ROM`, `harddisc/HD4` and `unixlib/UNIXLIB` pin them.
- Desktop mode (`rol-run -d`): the driver is BASIC inside `bin/rol-run`.
  Anything a program runs with OS_CLI must be a Utility or module command,
  not an application: UnixLib refuses to start a new application (that's
  why `*TBShot` is `tools/tbshot.s`).
- `unixlib/linux-port-pvops.patch` is the patch sent to UnixLib. Change it
  there first; when a UnixLib release has it, drop `unixlib/`.
- Known limitations are XFAIL tests in `tests/run.sh`. If one passes
  (XPASS), update the test, the README and the CHANGELOG.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
