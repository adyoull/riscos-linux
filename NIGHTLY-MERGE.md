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

## Building

It builds natively with the DDE on a Raspberry Pi (see nightly/README.md):
every ROL ROM from 5.31-2 has been built that way, and is tested with
the riscos-linux-testbed suite under QEMU and natively. The fixes the first
builds needed are commits on this branch ("Fixes from the ... build").

RiscOS/Sources/SystemRes/InetRes/Sources/md5/md5.1 has conflict markers in
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
| Socket options and Socket_Sendmsg | `mixed/Linux/Internet/c/main` (`convert_sockopt`, the SWI switch) | SO_TYPE is answered (GLib's `g_socket_new_from_fd()` needs it; the SOCK_ types are the same numbers on Linux). An IPPROTO_TCP option not listed is refused instead of falling into the SOL_SOCKET list. Socket_Sendmsg_1 (UnixLib's and TCPIPLibs' `sendmsg()`) shares Socket_Sendmsg's case, and both take their flags from R2. Found by the web browser project. Test: `t_sockopt` in riscos-linux-testbed. |
| Select on accepted sockets | `mixed/Linux/Internet/c/main` (`do_accept`) | Socket_Select answers from per-socket flags that Linux's I/O signal sets, and Linux signals only new events. An accepted socket's signal is set up in Socket_Accept, so data the client sent before that (RDP, TLS: clients send straight after connecting) and room to write were never reported: `do_accept` now polls the new socket once. Found by RDPServer ("server not responding" after the connection was accepted). Test: `t_accept` in riscos-linux-testbed. |
| File types from extensions | `mixed/Linux/IXSupport/c/fs` (`mime_type`, `mime_prime_handler`, `read_meta`, `add_filetype`), `cmhg/IXSupportHdr` (the `mime_prime` veneer) | A regular, non-executable file with no RISC OS type (no `,xxx`, no extended attribute) gets the type MimeMap gives its extension, as DOSFS does: USB discs (FAT, exFAT, NTFS keep no attributes) and files Linux programs wrote. Saving where attributes fail adds `,xxx` only if the name doesn't already say the type. MimeMap reads its table on first use, usually from IXFS, so asking it from inside IXFS would reenter IXFS (pd1/pd2 are shared): it's asked first from a callback (when the desktop starts, Service_StartWimp, and whenever MimeMap starts, Service_ModulePostInit: a restarted MimeMap has forgotten its table) and only used from IXFS once that has worked. Special field M turns it off (Baldwin had reserved M/T as `nomime`). Test: `t_mimetype`. |
| Keyboard LEDs | `mixed/Linux/SocketKVM/c/main` (`Keyv_handler`, `send_leds`), `Unix/SocketKVMFrontends/SocketKVM_Protocol.h` (`c_leds`) | KeyV_NotifyLEDState (R1: 1 Caps, 2 Num, 4 Scroll; the PRM is wrong) goes to the front end as c_leds, only to front ends reporting version 2 or later (rol-fb, which writes EV_LED to the keyboards it has grabbed); the SDL front end reports 1 and leaves LEDs to its host. |
| MbufManager in the ROM | `RiscOS/BuildSys/Components/ROOL/Linux` (`MManager`, ROOL's MManagerBlob 0.30) | Every RISC OS 5 ROM has the DCI4 mbuf manager; Linux's left it out because LinuxInternet doesn't use mbufs. Programs do: !Omni's LanManFS (`RMEnsure MbufManager 0.13`, "LanManager requires a DCI4 MbufManager") and Access/AppleTalk. HardDisc4's `!System.310` copy is 0.17, 26-bit, so not a substitute. |

Not in this tree (they're in riscos-linux-testbed / riscos-linux): the image's
init, rol-fb, LinuxCodec, NetWatch, the packages.
