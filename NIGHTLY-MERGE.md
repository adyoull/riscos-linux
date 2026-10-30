# Merging RISC OS Open's nightly into the Linux port

Upstream: RISC OS Open's IOMDHALDev source tarball, 5.31, built 2026-10-29 00:32
(src-iomd-dev.5.31.tar.bz2), plus the DiscDev tarball for the components the
IOMD tree doesn't carry. Port: Timothy Baldwin's Linux2 (1234a4b, Dec 2022)
plus the 3GB RAM limit commit (469d9d2).

## How it was merged

Baldwin's "Merge with RISC OS Open" commits carry Linux changes of their own
(KSWI, the ModuleDB entries, ...), so the base for each file is the version
first added to the port (the 2017 import), and every file that differs was
merged three ways (port, base, upstream) with `git merge-file --diff3`:

- theirs 669: the port never changed the file, upstream's version taken
- ours 128: upstream never changed it, the port's version kept
- clean 155: both changed it, merged without conflicts
- conflict 266: resolved by hand (below)
- portcreated 21: files the port added that upstream has since added too,
  upstream's version taken

Port-only lines that carry Linux structure (`[ Linux`, `[ PagedMemory`, `]`,
ROMLimit, KSVC, ...) are never auto-resolved. Afterwards every changed
assembler file was checked to have the same `[`/`]` nesting as the port, and
for Linux/PVOps lines that went missing.

## Decisions worth knowing

- **Kernel RW data.** Upstream now links the Kernel with a separate RW/ZI
  segment (`-RW-base`, kstrip). For Linux `RWBase` is `RMAAddress - 4MB`
  (&24400000, in the gap between the system heap and the RMA) and the
  Makefile links with `-RW-base 0x24400000`; HAL maps it with
  `Init_MapInRAM_Clear_Linux` like any other area. The two must agree.
- **Aborts.** Linux keeps the port's PAbPreVeneer and s.Linux's DAbPreVeneer;
  upstream's AbortTrap veneers (CP15 fault registers) are `:LNOT: Linux`.
  OS_AbortTrap's C code is built but the abort path never reaches it.
  `RISCOS_LogToPhys` on Linux is an identity-mapping stub, now exported for
  the AbortTrap C code.
- **Exit handler and stack reset** use upstream's `TaskControl_ResetStacks`,
  converted to pvMRS/pvMSR.
- **s.HAL** is the port's version plus upstream's later changes
  (doubleword-aligned HAL workspace, NoARMv5 BLX, RW/ZI segment,
  RISCOS_LogToPhys page flags, LTORG in the page table loop).
- **KSWI** restored wherever the port had it; raw MSR/MRS that upstream added
  in code Linux runs converted to pvMSR/pvMRS/SavePSR.
- **TerritoryManager** is C upstream, so SharedRISC_OSLib comes first in
  Components/ROOL/Linux (as IOMD32 does).
- KernelWS: DebuggerSpace &3000, Abort32_dumparea 6 words (same size as
  upstream's 3 + 3 spare), MaxInterrupts 768, PVSys SWIDespatch room, as the
  port had.

## Not done / to check when it builds

- It has not been assembled or compiled: the DDE must only be run on a real
  Raspberry Pi, so the first build is on Andrew's Pi (see nightly/README.md).
  Expect a few assembler errors from upstream code that references
  symbols the Linux layout doesn't define.
- RiscOS/Sources/SystemRes/InetRes/Sources/md5/md5.1 has conflict markers in
  upstream's own copy; left alone.

## This branch

A snapshot of the merge (port commit 8c7c47521 on top of Timothy Baldwin's
RISC_OS_Linux_Source, 30 October 2026), as built into the RISC OS on Linux
images' riscos-rom 5.31.0.1. Its history is in Timothy Baldwin's repository
(https://github.com/TimothyEBaldwin/RISC_OS_Linux_Source) and RISC OS Open's
(https://gitlab.riscosopen.org). Unix/RISC_OS is a submodule there.
