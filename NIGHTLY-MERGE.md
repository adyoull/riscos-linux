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

## RISC OS on Linux changes since the merge

Each is one commit on this branch; the commit message has the detail. All
are inside `[ Linux` / `MEMM_Type = "Linux"` conditions or Linux-only
directories, so other machines' builds are unchanged.

| Change | Where | Notes for whoever maintains it |
|---|---|---|
| Version string `RISC OS 5.31-N (date)` | `NightlyDate`, `BuildNumber`, `Unix/LinuxSupport/build.mk` (writes `Kernel/hdr/AutoVersion`) | Bump `BuildNumber` for each ROM published; `NightlyDate` when merging a newer nightly. The native DDE build (BuildROM) uses the `AutoVersion` in the source zip, so regenerate it when making the zip. |
| RAM size from Linux | HAL `c.Main` (`read_ramsize`, `__HAL_rampages`), `HALSyms`, Kernel `s.Linux` (RAMLIMIT) | `ROL_RAMSIZE_MB` comes from the image's init (MemAvailable less what Linux needs). Kept below 4GB: the kernel's sums are 32-bit. |
| Free pool as a count | Kernel `c.LinuxC` (`free_take`, `free_give`), `s.ChangeDyn` | Memory comes from Linux as it's used; the free pool is only bookkeeping, so the Task Manager's Free is right. Anything that takes or gives memory must go through these two. |
| Network statistics (Internet 5's interface) | `mixed/Linux/Internet/c/sysctl`, `c/main`, `cmhg/InternetLHdr` | Socket_Sysctl (iflist, routes, tcp/udp/ip stats, tcp/udp pcblist, name2oid), Socket_InternalLookup (_tcpstat...), Socket_Version 573. The struct layouts are RISC OS's TCPIPLibs ones (u_quad_t is 32 bits): if TCPIPLibs changes, re-check them (the offsets are listed in the file). Host test: compile `c/sysctl` with `-DROL_HOSTTEST`. |
| IXFS errors and the L attribute | `mixed/Linux/IXSupport/c/fs` (`fs_error`, `lock_error`, `is_locked_*`) | Linux errors become FileCore's (low byte as programs test it) in IXFS's range &1C5xx. Errors not in `fs_error` still get IXSupport's `convert_error` names. |
| Physical memory pool areas | Kernel `c.LinuxC` (`do_PMP_*`), `s.Linux` (veneers), `s.ChangeDyn` (creation, `DAR_delink`) | A memory file per area; c.LinuxC's records hang off one extra word at the end of the kernel's pool array (`phys[PMPMaxSize]`), so anything that resizes that array must keep the extra word. Tested on Linux with the C compiled for ARM (see the commit). |
| `fallocate`, `mprotect` | `mixed/Linux/Syscalls/GenSyscall` | LARGE/SMALL arguments are now stripped from every argument of the prototype. |
| Socket_Select's timeout | `mixed/Linux/Internet/c/main` (`do_select`, `sigio_handler`) | The wait loop ends when `select_timeout` (s.asm, a ticker event) sets the pollword's timeout byte: a pollword that is non-zero makes `pollword_sleep` return at once, so any change to that loop must keep the check or it spins. Timeouts round up to whole centiseconds. Test: `t_select` in riscos-linux-testbed. |
| Internet module version | `mixed/Linux/Internet/cmhg/InternetLHdr` (`help-string`) | Reports 5.73, matching `Socket_Version`. Programs check it with `RMEnsure Internet 5.xx` (!Omni 5.02, !Internet 5.40); without a number cmhg made it 0.00. Keep it in step if `Socket_Version` changes. |

Not in this tree (they're in riscos-linux-testbed / riscos-linux): the image's
init, rol-fb, LinuxCodec, NetWatch, the packages.
