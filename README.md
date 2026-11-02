# ROL-ROM: the ROM of RISC OS on Linux

This is the source of the RISC OS 5 "ROM" that RISC OS on Linux (ROL) runs.
The ROM image is a Linux executable: RISC OS running as a Linux program,
natively on machines that can run 32-bit ARM code (Raspberry Pi 3, 4 and 5),
or under a slightly modified QEMU elsewhere.

It is [Timothy Baldwin's Linux port of RISC OS](https://github.com/TimothyEBaldwin/RISC_OS_Linux_Source)
brought up to date with RISC OS Open's 5.31 development sources (the nightly
of 29 October 2026), with the fixes and additions that ROL needs.

The ROM reports itself as **RISC OS 5.31-N (date)**: RISC OS Open's
version, ROL's build number (`BuildNumber`) and a date. ROL installs it from
its package site as the `riscos-rom` package, at `/opt/riscos/RISCOS.IMG`.

## Branches

| Branch | What |
|---|---|
| `nightly-merge` | The main line. ROL's ROMs are built from here. |
| `swi-fastpath` | A test branch: `nightly-merge` plus a fast path for the ROM's own SWIs (`*ROLSWIPatch`). Its builds say `5.31-N-swi`. On the Raspberry Pi the same speed-up now comes from ROL's kernel and NativeSupport, with any ROM. |

## Builds

`NIGHTLY-MERGE.md` explains how RISC OS Open's sources were merged into the
port, and lists every RISC OS on Linux change with where it is and what to
watch when maintaining it. Each change is one commit, whose message has the
detail. All of them are inside `[ Linux` / `MEMM_Type = "Linux"` conditions
or in Linux-only directories, so other machines' builds are unchanged.

| Build | Changes |
|---|---|
| 5.31-2 | RISC OS Open's 5.31 nightly (29 Oct 2026) merged into the port; native builds with the DDE on a Raspberry Pi; fixes from the first native builds (aborts, the undefined instruction handler, abortable dynamic areas); RAM size from Linux (up to 4095MB) and a free pool that counts memory not yet used; network statistics (Socket_Sysctl and Socket_InternalLookup in Internet 5's layouts, for NetStats and InetStat); the version string. |
| 5.31-3 | IXFS gives RISC OS's filing system errors (Disc full, Locked, Already open, ...) and enforces the L (locked) attribute; physical memory pool dynamic areas (OS_DynamicArea 21-23, 25); build fixes. |
| 5.31-4 | Socket_Select returns when its timeout runs out (it spun forever, hanging TubeSearch and Reel); a locked item can't be renamed. |
| 5.31-5 | The Internet module reports version 5.73, so `RMEnsure Internet 5.xx` checks pass (!Omni, !Internet). |
| 5.31-6 | Socket options: SO_TYPE (GLib), unknown TCP options refused; Socket_Sendmsg_1, and Socket_Sendmsg's flags. |
| 5.31-7 | MbufManager in the ROM, as in every RISC OS 5 ROM (!Omni's LanManFS, Access); Socket_Select sees data that arrived before Socket_Accept, so servers such as RDPServer answer their clients. |

## What's in the tree

RISC OS Open's sources are under `RiscOS/`; the Linux parts are in `mixed/Linux/`:

| Directory | What |
|---|---|
| `HAL` | The hardware layer: Linux system calls instead of hardware, signals as interrupts, the start-up and its command line options (below). |
| `IXSupport` | IXFS, the filing system that shows Linux's files to RISC OS (below). |
| `Internet` | LinuxInternet: the Internet module's SWIs on Linux's own network stack. |
| `SocketKVM` | The screen, keyboard and mouse over a socket, to a front end: ROL's `rol-fb` on a Linux console, or the SDL front end in a desktop. |
| `CryptRandom` | Random numbers from Linux. |
| `Syscalls` | The system call interface the others use. |
| `Tests` | The port's own tests. |

`Unix/` has the Linux side: the build's support files, the SDL front end,
and `run_RISC_OS`. The rest of ROL (the image's Linux programs, sound
output, video, the packages) is in other repositories; see Related below.

## Running it

ROL's images and packages run the ROM for you. To run it on another Linux
system, use `./run_RISC_OS` from the top of the tree. If RISC OS is run
directly then, unlike virtual machines such as RPCEmu, **nothing stops
RISC OS programs doing anything to the Linux system that Linux allows**.
`run_RISC_OS` runs it inside a Bubblewrap sandbox so that it can't. To run
it without the sandbox, set `RISC_OS__INSECURE` to `YES`:

    RISC_OS__INSECURE=YES ./run_RISC_OS

### Command line options

| Option | Action |
|---|---|
| `--help` | Show the options |
| `--nvram FILE` | The file that holds CMOS RAM (made 256 bytes if smaller) |
| `--highmem` | Allow virtual addresses above 2GB |
| `--isreboot` | Tell RISC OS it's a restart, not a power on |
| `--noaborts` | No data aborts: RISC OS dies with SIGSEGV instead |
| `--notimers` | No HAL timers |
| `--noseccomp` | Don't trap system calls with seccomp |
| `--cswi` | Use the HAL's SWI handler written in C |
| `--abort-on-input` | Stop if the debug terminal is read (for unattended builds) |

The environment variable `ROL_RAMSIZE_MB` sets how much RAM RISC OS has
(64 to 4095MB; 3GB if not set). ROL's start-up program sets it from the
memory Linux has free.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | RISC OS was switched off |
| 100 | RISC OS asked to restart (Ctrl+Break, Restart, `*Reset`) |
| 102 | Unrecognised command line option |
| 103 | Unable to open the NVRAM file |
| 104 | Permission denied opening the NVRAM file |
| 106 | Unable to read the NVRAM file's size |
| 107 | Unable to enlarge the NVRAM file |
| 108 | Failed to reserve address space: try `sudo sysctl vm.mmap_min_addr=12288` |
| 110 | The parent process exited |
| 111 | Cloning the ptrace thread failed |
| 112 | Failed to set the alternative signal stack |
| 113 | SWI intercept failure |
| 114 | The RISC OS kernel returned to the HAL |
| 115 | Interrupt test failed |
| 117 | The debug terminal was read with `--abort-on-input` |
| 119 | Problem starting the fork server thread |

## IXFS

IXFS is the filing system that shows Linux's files to RISC OS (`IXFS:$` is
Linux's `/`). Linux's errors are given as RISC OS's own filing system
errors (Disc full, Locked, Access violation, Already open, ...), and the L
(locked) attribute is enforced: a locked item can't be deleted, saved over
or renamed.

### File types

IXFS keeps RISC OS file types in POSIX extended attributes, and by default
only writes a `,xxx` extension to the name if writing an attribute fails.
The special field (`IXFS#X:...`) chooses how file types are handled:

| Special field | Mode | Write extension | Read and strip extension | Read extended attribute |
|:---:|---|---|---|---|
| R | Raw | Never | No | Yes |
| P | Pure | Remove if wrong | Yes | Only if no extension |
| (none) | Normal | If needed | Yes | Only if no extension |
| W | Write | Always | Yes | Only if no extension |
| X | Exclusive | Always | Yes | Never |

### Case

As is usual for RISC OS filing systems, IXFS ignores case. If two names
differ only by case, which one is used is unpredictable unless the exact
name is given. Add `C` to the special field to make names case sensitive.

## Building

RISC OS Open's DDE is needed. It is proprietary software, available from
[RISC OS Open Limited](https://www.riscosopen.org/content/sales/dde), and
may only be run on the machine it's installed on, so ROL's ROMs are built
natively on a Raspberry Pi.

### From RISC OS

`nightly/BuildROM` builds the ROM from RISC OS itself, from a copy of this
tree (ROL's `ROLSrc` zips have it ready, with the version files). It checks
for files lost in unzipping, then runs RISC OS Open's `srcbuild` for each
phase. See `nightly/README.md`.

### From Linux

`nightly/build-on-pi.sh` runs the Makefiles, which run most of the build in
RISC OS under the bootstrap ROM in `Unix/RISCOS.IMG`, with the DDE in
`../DDE`. Bash, Bubblewrap, GCC, GNU Make, libattr, libseccomp and SDL 2
are needed:

- Debian, Ubuntu and Raspberry Pi OS: `bash bubblewrap gcc g++ make libattr1-dev libseccomp-dev libsdl2-dev`
- Fedora: `bash gcc gcc-c++ glib2-devel libattr-devel libseccomp-devel patch SDL2-devel`

Bubblewrap must be setuid root, or unprivileged user namespaces enabled, or
the build run with `INSECURE=YES`. The result is `Built/RISCOS.IMG`.

| Variable | |
|---|---|
| `ACORN_CPP` | The DDE's `AcornC.C++` directory (default `../DDE/AcornC.C++`) |
| `TARGET` | What to build: `Linux`, or `IOMD32` for a RiscPC/A7000 ROM |
| `METHOD` | How to build: `Linux` (default) or `rpcemu` |
| `JOBS` | How many CPUs to use (default: all) |
| `LINUX_ROM` | The RISC OS executable to build with |
| `QEMU` | The `qemu-arm` to use where ARM code can't run natively |
| `PHASES` | The build phases to run (default `export_hdrs export_libs resources rom install_rom join`) |
| `INSECURE` | `YES` to build without the sandbox |
| `VERBOSE` | `1` for more output |

| Target | |
|---|---|
| `build` | Build RISC OS |
| `check` | Build and test it |
| `fast` | Only the `install_rom` and `join` phases, then the tests |

These are downloaded to `~/Downloads` when needed: RISC OS Open's
HardDisc4 5.28 and IOMD ROM 5.28, RPCEmu 0.9.3 (for `METHOD=rpcemu`) and
QEMU 5.2.0 (built with the port's patch, `Unix/LinuxSupport/qemu_swi.diff`,
on machines that can't run ARM code).

## Related

- ROL's packages (the ROM, the Linux side, HardDisc4, kernels):
  [adyoull/riscos-rolpkg](https://github.com/adyoull/riscos-rolpkg).
- riscos-linux-testbed: ROL's test suite (each fix here has a test there)
  and the image's Linux programs (start-up, display, sound, USB, network).

## Licences and credits

The RISC OS sources are RISC OS Open's, under the licences in each file
(mostly Apache 2.0). The Linux port is Timothy Baldwin's (Apache 2.0).
RISC OS on Linux's changes are Andrew Youll's, under the same licence as
the file they're in.

Written with Claude (Anthropic's AI) as a coding assistant. Andrew Youll
directs the work, makes the decisions, and tests on a Raspberry Pi 4.
