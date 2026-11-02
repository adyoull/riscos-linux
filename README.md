# ROL-ROM: the ROM of RISC OS on Linux

This is the source of the RISC OS 5 "ROM" that RISC OS on Linux (ROL) runs:
a Linux program that is RISC OS, running natively on 32-bit capable ARM
machines such as the Raspberry Pi 3, 4 and 5, or under QEMU elsewhere.

It is [Timothy Baldwin's Linux port of RISC OS](https://github.com/TimothyEBaldwin/RISC_OS_Linux_Source)
brought up to date with RISC OS Open's 5.31 development sources (the nightly
of 29 October 2026), with the fixes and additions that ROL needs. Baldwin's
original README is kept below, unchanged.

The ROM reports itself as **RISC OS 5.31-N (date)**: RISC OS Open's version,
the date of the nightly it was merged from (`NightlyDate`), and ROL's build
number (`BuildNumber`). ROL installs it from its package site as the
`riscos-rom` package (`/opt/riscos/RISCOS.IMG`).

## Branches

| Branch | What |
|---|---|
| `nightly-merge` | The main line: Baldwin's port with RISC OS Open's 5.31 nightly merged in, and ROL's changes. ROL's ROMs are built from here. |
| `swi-fastpath` | A test branch: `nightly-merge` plus a fast path for the ROM's own SWIs (`*ROLSWIPatch`). Its builds say `5.31-N-swi`. On the Raspberry Pi the same speed-up now comes from ROL's kernel and NativeSupport instead, with any ROM. |

## What has changed

`NIGHTLY-MERGE.md` explains how the nightly was merged, and has a table of
every RISC OS on Linux change with where it is and what to watch when
maintaining it. Each change is one commit, whose message has the detail.
Every change is inside `[ Linux` / `MEMM_Type = "Linux"` conditions or in
Linux-only directories, so other machines' builds are unchanged.

| Build | Changes |
|---|---|
| 5.31-2 (29 Oct 2026) | RISC OS Open's 5.31 nightly merged into the port; native builds with the DDE on a Raspberry Pi (`nightly/BuildROM`); fixes from the first native builds (aborts, the undefined instruction handler, abortable dynamic areas); RAM size from Linux (up to 4095MB) and a free pool that counts memory not yet used; network statistics (Socket_Sysctl and Socket_InternalLookup in Internet 5's layouts, for NetStats and InetStat); the version string. |
| 5.31-3 | IXFS gives RISC OS's filing system errors (Disc full, Locked, Already open, ...) and enforces the L (locked) attribute; physical memory pool dynamic areas (OS_DynamicArea 21-23, 25); build fixes. |
| 5.31-4 | Socket_Select returns when its timeout runs out (it spun forever, hanging TubeSearch and Reel); a locked item can't be renamed. |
| 5.31-5 | The Internet module reports version 5.73, so `RMEnsure Internet 5.xx` checks pass (!Omni, !Internet). |
| 5.31-6 | Socket options: SO_TYPE (GLib), unknown TCP options refused; Socket_Sendmsg_1, and Socket_Sendmsg's flags. |
| 5.31-7 | MbufManager in the ROM, as in every RISC OS 5 ROM (!Omni's LanManFS, Access); Socket_Select sees data that arrived before Socket_Accept, so servers such as RDPServer answer their clients. |

## Building

RISC OS Open's DDE is needed, and builds run natively on a Raspberry Pi:
see `nightly/README.md`. `nightly/BuildROM` builds from RISC OS itself;
`nightly/build-on-pi.sh` uses the port's Makefiles from Linux.

## Related

- ROL's packages (the ROM, the Linux side, HardDisc4, kernels) are at
  [adyoull/riscos-rolpkg](https://github.com/adyoull/riscos-rolpkg).
- riscos-linux-testbed holds ROL's test suite (each fix here has a test
  there) and the image's Linux programs (init, the display, sound, USB,
  network).

## Licences and credits

The RISC OS sources are RISC OS Open's, under the licences in each file
(mostly Apache 2.0). The Linux port is Timothy Baldwin's (Apache 2.0).
RISC OS on Linux's changes are Andrew Youll's, under the same licence as
the file they're in.

Written with Claude (Anthropic's AI) as a coding assistant. Andrew Youll
directs the work, makes the decisions, and tests on a Raspberry Pi 4.

---

# Timothy Baldwin's original README

It is currently being discussed on the [RISC OS Open website](https://www.riscosopen.org/forum/forums/3/topics/9068). There is no sound support at present, and is incomplete in other ways. You should not reguard any new APIs to be stable.

It should run on a 32-bit capable ARM Linux system natively, or slow on other Linux systems with the aid of a slightly [modified](mixed/Linux/Support/qemu_swi.diff) QEMU. Unfortunately server class ARM hardware is typically 64-bit only.

The RISC OS "ROM" Image is a Linux executable.

If you run RISC OS directly then, unlike virtual machines such as RPCEmu, there is **no restriction on what RISC OS programs can do to the Linux environment** beyond that provided by Linux. If you use the provided scripts then RISC OS should be prevented from causing harm.

You can disable the secure sandbox by seting the environment variable `RISC_OS__INSECURE` to `YES` when running `run_RISC_OS`, for example:

```
RISC_OS__INSECURE=YES ./run_RISC_OS
```

RISC OS Open Limited’s DDE is required to build RISC OS, this is proprietary software that may be brought from [RISC OS Open Limited](https://www.riscosopen.org/content/sales/dde)

### Compiling on RISC OS

This Linux port of RISC OS can be built in similar fashion to the traditional method as documented on the RISC OS website, it is not necessary to run InstallTools as the relevant files will be accessed from the DDE without copying. However this will not build the SDL front-end which connects RISC OS to the Linux graphics system, without it RISC OS is limited to a text only interface.

### Compiling on Linux

It can also be built on Linux using the Makefiles, which runs the majority of the build on RISC OS, by default using a binary of the Linux port that is automatically downloaded from [github](https://github.com/TimothyEBaldwin/RO_Linux) and RISC OS Open Limited’s DDE in ../DDE. Bash, Bubblewrap, GCC, GNU Make, Libattr, Libseccomp and SDL 2 are also required by Makefiles and support code. Either Bubblewrap must be setuid root, or unprivileged user namespaces must be enabled, or insecure mode enabled.

On Debian and Ubuntu the dependencies are in the packages "bash bubblewrap gcc g++ make libattr1-dev libseccomp-dev libsdl2-dev", on Fedora the dependencies are in the packages "bash gcc gcc-c++ glib2-devel libattr-devel libseccomp-devel patch SDL2-devel".

Additional packages are required for building QEMU.

The following variables are accepted by the Makefiles:

| Variable   | |
| ---        | ---    |
| ACORN_CPP  | Path to AcornC.C++ diectory of DDE, default is `../DDE/AcornC.C++`
| TARGET     | What to build, such as `IOMD32` for a RiscPC/A7000 ROM image. Only `Linux` and `IOMD32` are supported by this source. |
| METHOD     | How to build, valid values are `rpcemu` and `Linux`. |
| JOBS       | Number of CPUs to use for building, if 0 or unset build sequentially. |
| LINUX_ROM  | Location of Linux RISC OS executable to use for building. |
| QEMU       | Location of qemu-arm executable. |
| PHASES     | Space separated list of build phases to run. |
| INSECURE   | Set to YES to enable insecure mode. |
| VERBOSE    | Set to 1 to make build more verbose. |

The following targets are provided by the Makefiles:

| Target | Action |
| ---    | ---    |
| build  | Build RISC OS (or some other thing). |
| check  | Build and test RISC OS. |
| fast   | Runs only rom_install and join phases and tests. |

This following items will be download to ~/Downloads if needed:
* RISC OS 5.24 Disc Archive
* RISC OS 5.24 IOMD ROM Image
* RPCEmu 0.8.15
* QEMU 5.0.0

### IXFS

### File type handling

Unlike other software IXFS by default stores RISC OS file types in POSIX extended attributes, by default only writing extensions if writing a POSIX extended attribute fails. How IXFS handles file types can be configured using the special field.

| Special Field | Mode Name | Write extension | Read and strip extension | Read extended attribute |
|     :---:     | ---       | ---             | ---                      | ---                     |
|       R       | Raw       | Never           | No                       | Yes                     |
|       P       | Pure      | Remove if wrong | Yes                      | Only if no extension    |
|               | Normal    | If needed       | Yes                      | Only if no extension    |
|       W       | Write     | Always          | Yes                      | Only if no extension    |
|       X       | Exclusive | Always          | Yes                      | Never                   |

### Case sensitivity

As is standard for RISC OS filing systems IXFS is case insensitive by default, if 2 filenames only differ by case it is unpredictable which file is acted upon unless the exact filename is provided to IXFS. Case sensitivity may be enabled by adding "C" to the special field.

### Linux RISC OS command line options

| Option           | Action                                          |
| ---              | ---                                             |
| --abort-on-input | Abort on debug terminal input                   |
| --help           | Display help message                            |
| --highmem        | Allow use of virtual memory addresses above 2GB |
| --isreboot       | Indicate hard reset to RISC OS (not power on)   |
| --nvram FILE     | Filename for nvram                              |
| --noaborts       | Disable aborts - RISC OS will die with SIGSEGV  |
| --noseccomp      | Disable seccomp support                         |
| --notimers       | Disable HAL timers                              |

### Linux RISC OS error codes

| Return Code | Error |
| ---         | ---   |
|     100     | Reboot requested |
|     101     | Memory mapping failed, try running "sudo sysctl vm.mmap_min_addr=8192" |
|     102     | Unrecognised command line option |
|     103     | Unable to open NVRAM file. |
|     104     | Permission denied when attempting to open NVRAM file. |
|     106     | Unable to read size of NVRAM file. |
|     107     | Unable to enlarge NVRAM file. |
|     108     | Failed to reserve address space, try running "sudo sysctl vm.mmap_min_addr=12288." |
|     109     | Unable to fork |
|     110     | Parent process exited (no message) |
|     111     | Cloning ptrace thread failed |
|     112     | Failed to set alternative signal stack. |
|     113     | SWI intercept failure |
|     114     | RISC OS kernel returned to HAL |
|     115     | Interrupt test failed |
|     116     | Exec failed |
|     117     | HAL_DebugRX called and --abort-on-input given. |
|     118     | Failed to reserve address space for system heap. |
|     119     | Problem starting fork server thread. |


### To do List

- [x] Complete the key map.
- [x] Fix the realtime clock.
- [x] Make data aborts raise errors.
- [x] Support writing timestamps in IXFS.
- [x] Support reading timestamps in IXFS to centisecond resolution.
- [x] Test timestamps.
- [x] Add sound modules.
- [ ] Add sound output.
- [x] Officially allocate names and SWI numbers etc.
- [x] Fix failure to truncate of unbuffered files when opened for output, and associated race condition.
- [x] Fix spurious calls of FSEntry_Args 6 (flush file to disc) when files are closed.
- [ ] Perform proper error handling in IXFS.
- [x] Split video keyboard and mouse driver into separate module.
- [ ] Detect screen updates to avoid copying full screen 20 times a second.
- [ ] Finish video keyboard and mouse driver.
- [ ] Fix overwriting of ROM image when rebuilding RISC OS, which will cause a crash if the overwritten ROM image is currently being run.
- [ ] Provide a means to run Linux programs.
- [ ] Implement PMP dynamic areas.
- [x] Replace the changes that are labeled "Hack"
- [x] Replace the branch table at the beginning of the HAL.
- [ ] Write test cases for VFP errors.
- [ ] Implement VFPSupport.
- [ ] Possibly add a binary release to the ABRelase phase.
