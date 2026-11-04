# riscos-linux-testbed

Run RISC OS programs built with
[riscos-crossdev](https://github.com/adyoull/riscos-crossdev) in real RISC
OS, on Linux, headless: in CI, in a container, or on your own machine.

The RISC OS here is Timothy Baldwin's
[Linux port of RISC OS](https://github.com/TimothyEBaldwin/RISC_OS_Linux_Source):
a RISC OS 5 ROM image that runs as a Linux program. On a 32-bit ARM Linux it
runs directly; anywhere else it runs under a patched `qemu-arm`. It starts in
about 0.3 seconds, runs one program, and gives back the program's output and
exit status:

```sh
$ rol-run out/hello
Hello, world! This machine has been up for 0:00:00.
$ echo $?
0
```

This is the next layer after riscos-crossdev's own Linux tests (the Fake
RISC OS, QEMU with RISC OS's alignment traps). Those test your code. This one
runs the whole program on the RISC OS kernel, filing system and modules, with
UnixLib and SharedUnixLibrary as they are on a Pi. It doesn't replace the Pi
test: see [What doesn't work](#what-doesnt-work).

It can also boot the full desktop (RISC OS Open's HardDisc4 and its
`!Boot`), headless, start a Wimp program in it and save a screenshot:

```sh
$ rol-run -s screen.png out/mywimpapp
rol-run: screenshot screen.png
```

## What it adds to the Linux port

The port can't run programs built with riscos-crossdev as it comes, because
they're hard-float (VFP) EABI programs:

| Piece | Why |
|---|---|
| `modules/VFPSupport` | The port has no VFPSupport (its to-do list says so), and UnixLib needs one at start-up. This is one for the port that does what ROOL's VFPSupport 0.18 does: see [VFPSupport](#vfpsupport). |
| `modules/ARMEABISupport` | GCCSDK's ARMEABISupport can't run on the port: it tells programs apart by the physical page at &8000 (the port has no physical pages) and builds stacks and mmap areas from PMP dynamic areas (not implemented in the port). This "lite" one answers the SWIs UnixLib uses (stacks, mmap/mremap/munmap, Cleanup) from one ordinary dynamic area, and page allocators (MemoryOp) and abort handlers (AbortOp) as SOManager uses them, with QEMU doing the page mapping. GCCSDK's ARMEABISupport 1.08 also runs on the test bed's QEMU (it provides the PMP areas): run with `ROL_MODULES` pointing at a directory holding it. |
| `modules/Iconv` | Iconv (lite): character set conversion for UnixLib's iconv (UTF-8, Latin-1/9, CP1252, ASCII, UTF-16/32). HardDisc4 has no Iconv module. |
| `modules/LinuxAudio`, `modules/SoundControl` | Sound: see [Sound](#sound). |
| `qemu/` | The port needs three changes to QEMU's `linux-user`: SWI interception, real-time signal numbers, and loading a sparse ELF. Baldwin's patch is for QEMU 5.2; this is the same three changes on QEMU 8.2.2, on top of riscos-crossdev's alignment-trap patch. A fourth is ours: code loaded from disc (modules, programs, HardDisc4's CLib) that uses the privileged `MRS`/`MSR CPSR`/`CPS` instructions gets them emulated with the port's virtual CPU modes, through its HAL routines, as on a real machine (on Linux they would read user mode and ignore the change). The port's own ROM code is left as it is, but for its sound modules (see [Sound](#sound)). `RISCOS_NO_PSR_EMULATION=1` turns it off. OS_Memory 0 for addresses outside application space gives the logical address (the port refuses; SoundDMA asks when it starts). Also ours: what GCCSDK's ARMEABISupport 1.08 needs from the kernel (physical memory pool areas, whose operations are stubs in the port; OS_Memory 0 for application space; the data abort address through CP15), page mapping for the lite ARMEABISupport's allocators, and IXFS directory offsets without gaps (getdents leaves out "." and ".."; UnixLib's readdir stopped at the first gap). Debugging: `RISCOS_PSR_DEBUG` (undefined instructions not emulated), `RISCOS_SWI_TRACE` (every SWI RISC OS code calls), `RISCOS_SWI_RESULT=<hex SWI>` (how one returns), `RISCOS_PMP_DEBUG`, `RISCOS_REPORT` (Report_Text0 to stderr). Speed: SWIs without host signals (0.6); floating point on the host FPU when the result is an ordinary number and whole-instruction NEON helpers (0.7): `RISCOS_NO_HOSTFP`, `RISCOS_NO_HOSTNEON` turn those off; VFP and NEON floating point inline in the translated code, with host FPU instructions where the result is shown to be the guest's and the old helper otherwise (0.9): `RISCOS_NO_INLINEFP` turns that off. |
| `modules/NativeSupport` | Running natively: on a processor that runs 32-bit ARM Linux programs (a Raspberry Pi 4 or 5 with a 64-bit Linux, say), without QEMU, this module does what the test bed's QEMU does. See [Running natively](#running-natively). It does nothing under QEMU. |
| `unixlib/` | UnixLib's thread switcher and fault handlers use privileged instructions, which do nothing (or fault) in the port's user-mode RISC OS, so threads and faults hang. The port offers those operations as HAL routines; a patch to UnixLib (sent to UnixLib, not in a release yet) uses them. `unixlib/build.sh` builds riscos-unixlib with it, and the test bed links with that library. |
| `bin/rol-run` | Boots the ROM with an Obey file as `!Boot` and loads the modules. It runs the program (converting an ELF with `elf2aif`) and catches any RISC OS error. Then it leaves RISC OS with the program's return code. In desktop mode it boots HardDisc4 instead. |
| `tools/rol-kvm` | The screen for desktop mode: the port's SocketKVM front end, without a window. It keeps the screen in memory and saves it as PNG files. |
| `harddisc/` | Downloads RISC OS Open's HardDisc4, unpacks it in the port, and sets it up as the port's own build does. |

Both modules are built with the riscos-crossdev toolchain: no DDE needed.

## Quick start

With the riscos-crossdev container image (1.8.1 or later):

```sh
git clone <this repository> && cd riscos-linux-testbed
docker run --rm -v "$PWD":/w -w /w ghcr.io/adyoull/riscos-crossdev:1.8.1 sh -c '
  apt-get update && apt-get install -y ninja-build python3-venv libglib2.0-dev &&
  pip3 install --break-system-packages meson==1.2.3 && make && make unixlib harddisc basicvfp && make check'
```

Or build this repository's own image (`make image`) and use `rol-run` from
it: `docker run --rm -v "$PWD":/src riscos-linux-testbed rol-run out/myprog`.

On a machine with riscos-crossdev installed (x86-64 Ubuntu 24.04):

```sh
sudo apt-get install ninja-build python3-venv libglib2.0-dev autoconf2.69 automake1.11
pip3 install --break-system-packages meson==1.2.3   # QEMU's build needs it
make RCD_DIR=~/riscos-crossdev          # QEMU (a few minutes), modules, ROM, tools, test programs
make unixlib RCD_DIR=~/riscos-crossdev  # UnixLib with the port's patch (threads; clones from GitHub)
make harddisc RCD_DIR=~/riscos-crossdev # HardDisc4, for desktop mode (18 MB download)
make basicvfp RCD_DIR=~/riscos-crossdev # BASICVFP from the Pi ROM (5.4 MB download)
make check RCD_DIR=~/riscos-crossdev    # tests/run.sh
export RCD_DIR=~/riscos-crossdev PATH=$PWD/bin:$PATH
rol-run path/to/your/program args
```

`make rom` downloads the ROM image from Baldwin's
[RISC_OS_Linux_Binary](https://github.com/TimothyEBaldwin/RISC_OS_Linux_Binary)
and checks it against the sha256 in `rom/ROM`. `make harddisc` downloads
HardDisc4 from riscosopen.org (pinned in `harddisc/HD4`), and `make unixlib`
clones riscos-unixlib and GCCSDK and downloads GCC 10.2.0's source from
GitHub. None of those are kept in this repository.

**The patched UnixLib.** `make unixlib` builds `out/unixlib/libunixlib.a`.
To use it for your own programs, link with `-L .../out/unixlib` (before the
toolchain's library is searched). In this repository's container image it
has replaced the toolchain's `libunixlib.a` (the original is kept next to
it as `libunixlib.a.crossdev`), so everything built in the image uses it.
The patch changes nothing on other RISC OS machines, but it isn't in a
UnixLib release yet: build release binaries with riscos-crossdev's own.

## rol-run

```
rol-run [options] PROGRAM [ARGS...]     an ELF (converted with elf2aif) or an Absolute (AIF) file
rol-run [options] -c 'COMMAND'          any RISC OS command line

  -t SECONDS   time limit (default 120)        -C DIR   directory to run in
  -m FILE      RMLoad another module first     -v       also show RISC OS's console log
  -k           keep the work directory         -h       help

Desktop mode (-d, or any of these):
  -s FILE      save the screen here (default rol-run.png)
  -w SECONDS   let the program run this long first (default 5; less if it ends)
  -g WxH       screen size (default 1024x768)
```

**Exit status.** It returns what the program returned. There are also these codes:

| Status | Meaning |
|---|---|
| 0–255 | the program's return code (`Sys$ReturnCode`) |
| 125 | it ended with a RISC OS error, normal or serious (an abort): printed to stderr |
| 124 | time limit reached |
| 117 | RISC OS stopped at its prompt waiting for keyboard input (stdin isn't connected) |
| 126 | the test bed couldn't start (no ROM, no modules, not an ELF/AIF...) |

**Output.** Everything the program printed goes to stdout. That includes
stdout and stderr, because both go to the RISC OS screen. For separate
streams, use UnixLib's own redirection in the arguments:
`rol-run prog '2>' err.txt` writes stderr to `err.txt` in the run directory.
Don't use a path under `/tmp` for this: see Files.

**Files.** RISC OS sees the Linux file system through IXFS:
`IXFS:$.home.me.x/c` is `/home/me/x.c`. The program runs in the current
directory (or `-C DIR`). UnixLib's `/tmp` is `Wimp$ScrapDir`, a fresh
directory for each run. RISC OS has the same access to your files as you
do. Run untrusted programs in a container.

**Environment.** `RCD_DIR` or `RISCOS_TOOLCHAIN` (riscos-crossdev; set in its
image) and `TB_DIR` (this repository; found from the script). Optional:
`ROL_ROM`, `ROL_QEMU` (empty = run the ROM directly, on an ARM machine that
runs 32-bit programs), `QEMU_CPU` (e.g. `cortex-a8` for a VFPv3 CPU without
VFPv4), `ROL_TIMEOUT`.

## VFPSupport

`modules/VFPSupport` does what RISC OS Open's VFPSupport 0.18 does, for
the Linux port:

- **All the SWIs** (CheckContext, CreateContext, DestroyContext,
  ChangeContext, ExamineContext, FastAPI, ActiveContext, Version,
  Features 0-3, ExceptionDump, ElementaryFunctions), ROOL's context
  layout, error numbers and messages. One instance only (&81F106), and
  Service_ModulePostFinal when it's killed, so clients can tidy up.
- **Switches are atomic**: interrupts are off while one runs, as on
  RISC OS (through the port's PSR HAL device), so FastAPI's
  ChangeContext can be called from interrupt code.
- **The Wimp's task switching** (it saves and restores each task's
  context through ChangeContext) works: desktop programs keep their own
  VFP registers.
- **ElementaryFunctions** is ROOL's own code (sin ... pow and fused
  multiply-add, single and double), translated to GNU as, in ROOL's two
  builds: with VFMA for VFPv4 cores and FMA in software for the others,
  chosen from Linux's HWCAP. See `modules/VFPSupport/elementary`.
  They give bit for bit the same results and FPSCR flags as ROOL's own
  VFPSupport from the Raspberry Pi ROM, on every value tested (the test
  calls both). Against correctly rounded results they're within 1 ulp, as
  ROOL's own test bed requires, but for two 2-ulp cases in ROOL's code
  (log10 in single precision near 2^-106; fma in double precision when x*y
  and z nearly cancel).
- **Features** are made up from Linux's HWCAP (FPSID, MVFR0/1/2 are
  privileged registers): what a Pi's VFPSupport would say about the same
  CPU. No trappable exceptions and no short vectors, as on every Cortex-A
  core.
- `*VFPSupport_Info` (ours) shows the HWCAP, the made-up ID registers,
  which build of the elementary functions is in use and the active
  context.

What it doesn't do, because the port runs RISC OS as a Linux process:
lazy context switching and switching VFP off (there's no FPEXC), and the
support code that emulates instructions the VFP hardware bounces (Linux or
QEMU does that). ROOL's own test1 passes but for those two checks.

BASICVFP (`*BASIC64` runs it whenever VFPSupport is there, as on a Pi) isn't
in the port's ROM: `make basicvfp` takes it from RISC OS Open's Raspberry
Pi ROM (5.30), and rol-run loads it.

## Desktop mode

`rol-run -d` (or `-s`, `-w`, `-g`) boots a fresh copy of HardDisc4 (`make
harddisc`) into the desktop, about 5 seconds under QEMU, with the screen in
memory (`tools/rol-kvm`). A task in `Choices:Boot.Tasks` loads the modules,
waits 2 seconds for the rest of the boot sequence, starts the program (or
`-c` command) as a Wimp task, and lets it run for the `-w` time. Then RISC
OS ends and the screen is saved as a PNG.

- **Exit status:** the program's, if it ended within that time; 0 if it
  was still running; 125 if the Wimp refused to start it. Its text output
  isn't collected: the screen is the result.
- **More screenshots:** the program can run `*TBShot` (e.g.
  `_kernel_oscli("TBShot")`) at any moment; those go to `FILE-1.png`,
  `FILE-2.png` and so on.
- **Error boxes** stop the whole desktop until someone answers them. That
  includes "File not found" when a `-c` command names a missing program.
  So a run that shows one goes on to the time limit, which is the `-w` time plus
  40 seconds by default. The status is then 124, and the screenshot shows
  the box.
- `tests/pngpixel.py FILE X Y...` prints pixel colours, for checking a
  screenshot in a test.

HardDisc4 runs unchanged, with its own SharedCLibrary (6.22, which
`!System` loads over the ROM's 6.15). That CLib switches to USR mode with
an `MSR` instruction, which on Linux does nothing, so it hung every C
program; the test bed's QEMU emulates such instructions (see `qemu/`).
Linux itself can't, so desktop mode uses QEMU on a 32-bit ARM host too.
Every application on HardDisc4 runs, but !Omni (its LanManager needs
Internet 5; the port's Internet module is its own).

## Sound

The sound system is RISC OS Open's own modules; the test bed adds only an
interface to Linux:

- **SoundDMA** 1.88 (the HAL version, as on a Pi), **SoundChannels**,
  **SoundScheduler**, **WaveSynth**, **StringLib**, **Percussion**: the
  port's ROM has them, but no audio hardware for SoundDMA, so it never
  started. The ROM's sound modules still change processor mode with plain
  `MSR`s (they were never converted to the port's paravirtualised mode
  changes); the test bed's QEMU emulates those, as for code from disc.
- **`modules/LinuxAudio`** (ours): a HAL audio controller device (API 3,
  synchronous "custom DMA") and its mixer (system volume, "speaker"), of
  the kind the Pi's BCMSound provides. `rol-run` loads it and starts the
  ROM's SoundDMA again, which finds it. Each buffer SoundDMA fills (16-bit
  stereo, 8 to 96 kHz) goes to a Linux FIFO (`LinuxAudio$Path`, default
  `/run/riscos-sound`) as a 12-byte header (`RSND`, rate, length) and the
  samples; the reader paces it (it keeps about 50 ms ahead). With no
  reader the sound is dropped, at the sample rate. `*LinuxAudio` shows
  where it's going.
- **SoundControl** 1.06 (`modules/SoundControl`: RISC OS Open's source,
  Apache-2.0, on the C module framework; the Pi ROM's copy is tied to the
  ROM's C library): the mixer interface (`SoundCtrl_*`, `*MixVolume`,
  Configure's volume).
- **SharedSound** 1.20, from RISC OS Open's Pi ROM (`make basicvfp` takes
  it out; LinuxAudio holds its Messages file, which is in the ROM's
  ResourceFS on a Pi).

`ROL_SOUND_WAV=FILE rol-run ...` saves what RISC OS plays as a WAV file
(`tools/native-vm/vmexec       runs a job natively in a virtual Cortex-A72 (ROL_VM)
tools/rol-sndcap.py` reads the FIFO, in real time); `ROL_SOUND=silent`
uses the old silent stand-in (`modules/SoundDMA`) instead.
`tests/sound-out.sh` checks a note, a linear handler's output and a
scheduled note in such a file. The UTM and Pi images play it through
ALSA (their `rol-snd`).

## In your port's CI

In a job that runs in this repository's image, after your build:

```yaml
    - run: rol-run -t 60 out/myprog --self-test
```

or, in riscos-crossdev's image, check this repository out, run `make` in it,
and call its `bin/rol-run`. `.github/workflows/ci.yml` here is a worked
example.

## Tests

`make check` runs `tests/run.sh`:

- RISC OS commands: an ordinary error, a serious error (an abort in BASIC),
  and each module's `*Info` command.
- Programs from `tests/programs`:
  - arguments and exit status
  - VFPSupport's SWIs: two contexts with all 32 D registers and their own
    FPSCR, switched while the program uses its own (also on an emulated
    Cortex-A8); ChangeContext as the Wimp calls it; FastAPI;
    ExceptionDump; Features 0-3
  - VFPSupport's elementary functions, both builds, against correctly
    rounded results and ROOL's edge cases, and against ROOL's own (its
    VFPSupport module from the Pi ROM, called directly): bit for bit
  - context switches from an interrupt (OS_CallEvery) while the
    foreground switches: they're atomic
  - RISC OS Open's own VFPSupport tests (`tests/rool`), and BASICVFP
    (`*BASIC64`) when `make basicvfp` has fetched it
  - C++: exceptions, iostreams, containers, static constructors
  - mmap, mremap (moving, and the zeros a grown block must read as),
    munmap of a tail and a hole, a file mapping, a 64 MB malloc
  - `system()` running a nested program, with the parent's maps and stack
    checked afterwards
  - files and directories through IXFS
  - threads: create, yield and join; four threads with their own VFP
    state and rounding modes; and a fault reported as "Fatal signal"
    (these need the patched UnixLib; with the toolchain's they run as
    expected failures)
  - the desktop (when `out/harddisc` is there): a Wimp program's window
    in the screenshot, its exit status, and the screen after it closes;
    two desktop programs that each keep their own VFP registers and
    rounding mode while the Wimp switches between them; the scroll wheel
    scrolling a Filer window (`tests/desktop-wheel.sh`: KVMWheel and
    RISC OS Open's WindowScroll)
  - floating point and NEON, bit for bit (`t_fpconform`, `t_neonint`):
    every VFP and NEON float instruction on zeros, denormals, infinities,
    NaNs, integer limits and random values in 16 FPSCR modes, and the NEON
    integer instructions QEMU runs through the test bed's own helpers.
    The CRCs are those of QEMU's exact emulation, so the host FPU and
    host vector paths (see `qemu/`) must give the same results and flags;
    `t_fpconform` runs with each of them off and on.
    `t_neonperm` (generated by `tests/gen-neonperm.py`) checks VZIP,
    VUZP and the 8- and 16-bit VLD2/VLD4/VST2/VST4 forms (registers,
    memory written and write-back, at several alignments), which QEMU
    does as host vector instructions.
    `t_fpchain` and `t_fpchain_long` run generated straight-line blocks of
    VFP and NEON floating point instructions (in ARM and Thumb, 16 FPSCR
    modes), with QEMU's inline code and with its exact emulation.

Known limitations run as **XFAIL** tests. They're expected to time out, and
the run says **XPASS** if one ever works.

## Running natively

On a processor that runs 32-bit ARM programs (AArch32 at EL0: a Raspberry
Pi 4, a Pi 5, most Cortex-A7x boards) the port runs straight on the
hardware with no QEMU at all, several times faster. The port as it comes
does that already, but then what the test bed's QEMU adds is missing: code
from disc using the privileged instructions, PMP areas, OS_Memory 0, the
data abort address, directory reads without gaps. `modules/NativeSupport`
puts those back. `rol-run` loads it first; under QEMU it sees that QEMU is
doing the work and does nothing.

What it does:

- Code loaded from disc (modules, programs, CLib, shared libraries) is
  scanned when it is loaded or synchronised (Service_ModulePreInit, and
  the kernel's cacheflush calls for OS_SynchroniseCodeAreas and program
  starts; a whole-cache synchronisation scans the RMA). Each
  `MRS`/`MSR CPSR`/`CPS` is replaced by an undefined instruction that the
  module's SIGILL handler emulates with the port's virtual CPU modes, as
  the QEMU patch does; `MRS Rd, CPSR` (the common one, in IRQ-disabling
  code) becomes a branch to a stub that reads the virtual CPSR with no
  signal at all. Other privileged instructions that trap (CP15, SPSR,
  `LDM`/`STM` with `^`, `<op>S pc`, `SWP`) are emulated when they do.
- The kernel's PMP operations, OS_Memory 0, OS_DynamicArea 22 and the
  test bed's &C0002/&C0003 SWIs (for ARMEABISupport, lite or GCCSDK's
  1.08) through the SWI vector, with Linux memory mapping.
- Data aborts give the faulting address and status through CP15 (DFAR,
  DFSR), and the IXFS directory reads leave out "." and "..".
- `*NativeSupport` shows what it has done. `NativeSupport$Debug` (set
  before it loads) reports each emulation on Linux's stderr;
  `NativeSupport$NoStubs` emulates `MRS` with signals too.

It needs a Linux kernel with `CONFIG_COMPAT`, `CONFIG_KUSER_HELPERS`,
`CONFIG_SECCOMP_FILTER` and `CONFIG_COMPAT_32BIT_TIME` (Raspberry Pi OS's
64-bit kernel has them all). The port's own sound modules are patched in
the ROM as under QEMU, so desktop sound works natively too.

To test natively on a machine that can't (x86, an Apple Silicon Mac: no
AArch32), `tools/native-vm/vmexec` runs each job in a virtual Cortex-A72:

```sh
ROL_VM=tools/native-vm/vmexec bin/rol-run out/hello
ROL_VM=tools/native-vm/vmexec tests/run.sh
```

It needs `qemu-system-aarch64` and the UTM image's disk and kernel (see
the script). It is slow (whole-system emulation) and only for checking;
the two tests that talk to `rol-kvm` or the sound through a FIFO are
skipped.

## What doesn't work

- **pthreads and faults with an unpatched UnixLib.** A program linked
  with riscos-crossdev's own UnixLib hangs when a second thread first runs,
  and on a fault (instead of printing "Fatal signal"). UnixLib's thread
  switcher and abort handlers use privileged instructions (MSR CPSR, STM/LDM
  with ^, MRS/MSR SPSR), and the port runs all of RISC OS in user mode.
  Link with the patched UnixLib (`out/unixlib`; the default in the image)
  until a UnixLib release has the fix. A fault in a non-UnixLib program
  (BASIC, say) is reported normally (status 125).
- **VFP with no context active works** on the port, where on RISC OS it
  faults: a Linux process can't switch VFP off. Contexts are switched
  eagerly, not lazily, for the same reason. Neither changes what a correct
  program sees.
- **Desktop input.** Desktop mode doesn't click or type for you yet
  (`rol-kvm` can, through its control FIFO, but `rol-run` doesn't use it).
- **What the port lacks** (its README): sound output (the test bed adds
  it: [Sound](#sound)), PMP dynamic areas (the test bed's QEMU adds them),
  a complete video driver.
- **Speed.** Under QEMU on x86 it's a lot slower than a Pi for CPU-heavy
  work. Use `-t`.
- **Natively, checked in a virtual Cortex-A72**, not yet on a real Pi:
  in particular `LDM`/`STM` with `^` and exception returns at EL0 (which
  ARMv8 may treat differently from the emulator) and `SWP`.
- **Not a Pi.** No VideoCore, no GPIO, no Pi memory map. Alignment faults
  follow Linux's rules, not RISC OS's: riscos-crossdev's
  `qemu-arm-aligntrap` tests catch those.

## Layout

```
bin/rol-run                  the runner
tools/rol-kvm.c              the headless screen (SocketKVM front end) for desktop mode
tools/tbshot.s               *TBShot, a screenshot from inside RISC OS
harddisc/                    HardDisc4: which one (HD4), fetch.sh, xattr2comma.py
unixlib/                     UnixLib with the port's patch: UNIXLIB, the patch, build.sh
modules/VFPSupport/          VFPSupport for the port (assembler)
modules/VFPSupport/elementary/  ROOL's elementary functions in GNU as (v3, v4) and objasm2gas.py
basicvfp/                    BASICVFP: which Pi ROM (PIROM), fetch.sh
tests/rool/                  RISC OS Open's VFPSupport tests
modules/ARMEABISupport/      ARMEABISupport (lite) (C + header)
modules/NativeSupport/       running natively without QEMU (C + header)
modules/Iconv/               Iconv (lite) (C + header)
modules/LinuxAudio/          the audio device for SoundDMA (C + header), SharedSound's Messages
modules/SoundControl/        RISC OS Open's SoundControl (rool/) and what it needs (port/)
tools/rol-sndcap.py          saves the sound as a WAV file (ROL_SOUND_WAV)
modules/common/              the C module framework: veneers, GOT relocation, link script, check-pic.py
qemu/                        the QEMU patch and build.sh
rom/ROM, rom/fetch.sh        which ROM image, and how to get it
tests/programs/, tests/run.sh, tests/pngpixel.py
docker/Dockerfile            this test bed on top of the riscos-crossdev image
.riscos-crossdev             the riscos-crossdev version (image tag) CI builds on
```

## Licences

This repository's own files are under the MIT licence (`LICENSE`). See
`LICENCES.md` for the QEMU patch (GPL-2.0-or-later), the UnixLib patch,
the ROM image, HardDisc4, and the interfaces the modules follow.

## How this was made

Written with Claude (Anthropic's AI) as a coding assistant. Andrew Youll
directs the work, makes the decisions, and tests on a Raspberry Pi 4.
