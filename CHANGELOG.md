# Changelog

## 0.10 (2026-10-18)

Running natively: on a processor that runs 32-bit ARM Linux programs (a
Raspberry Pi 4 or 5), without QEMU, with what the test bed's QEMU adds.

- New module `NativeSupport` (C on the module framework; loaded first by
  `rol-run`, does nothing under QEMU). It patches the privileged PSR
  instructions in code loaded from disc (at Service_ModulePreInit and at
  each code synchronisation, through the kernel's cacheflush calls:
  application space, the RMA and dynamic areas, where SOManager puts
  shared libraries) into
  undefined instructions that its SIGILL handler emulates with the port's
  virtual CPU modes; `MRS Rd, CPSR` goes to a stub without a signal.
  Other privileged instructions are emulated when they trap (CP15
  including DFAR/DFSR/MIDR, SPSR, LDM/STM ^, `<op>S pc`, SWP, cache
  maintenance by address). The emulations run under a fault probe: a bad
  address fails the emulation instead of killing RISC OS.
- PMP dynamic areas (the kernel's PhysOp/LogOp/Resize/GetPages), OS_Memory
  0, OS_DynamicArea 22 and SWIs &C0002/&C0003 through the SWI vector, so
  both ARMEABISupports work; the data abort address and status; IXFS
  directory reads without gaps; the sound modules patched in the ROM.
- Whole-cache synchronisations (do_IMB_Full) are recognised: only the
  RMA is scanned for code then (code put there that isn't a module, as
  vector handlers are), not all of memory.
- VFPSupport: 32 D registers when Linux reports NEON (an arm64 kernel's
  32-bit HWCAP has no VFPD32).
- `ROL_VM=tools/native-vm/vmexec` runs `rol-run` (and `tests/run.sh`)
  natively in a virtual Cortex-A72 (`qemu-system-aarch64`), for checking
  on x86 or a Mac. All checks pass there (49, two FIFO ones skipped), and
  all 51 under QEMU.
- Iris (with GCCSDK's ARMEABISupport 1.08 and its shared libraries)
  starts natively.
- The Pi image runs the port natively; the UTM image (Apple Silicon has
  no AArch32) keeps QEMU.

## 0.9 (2026-10-18)

Floating point and NEON floating point inline: QEMU no longer calls a
helper for each VFP or NEON float instruction when the host FPU can be
shown to give the guest's exact result (`qemu/` patch item 11).

- New TCG vector opcodes (x86 AVX and aarch64 back ends): add, subtract,
  multiply, divide, square root, int/float conversions on float lanes, a
  32-bit store from a vector register, and two 64-bit halves joined. The
  ARM front end (`target/arm/tcg/riscos-fp.h`) uses them for VADD, VSUB,
  VMUL, VNMUL, VDIV, VSQRT, VMLA, VMLS, VNMLA, VNMLS (single and double),
  and NEON's VADD, VSUB, VMUL, VMLA, VMLS (also by scalar).
- A fast path is used only when its result is the guest's: the guest's
  inexact flag already set (an FPSCR write now sets it for NEON too),
  round to nearest, no denormal input where FZ would flush it, and each
  result a finite normal number (or an exact zero). Anything else runs
  the old helper, from code at the end of the translation block, so the
  fast paths run straight through from one instruction to the next.
- The VFP FZ bit and the rounding mode are part of a translation block's
  key, so "RunFast" programs (FZ set) get their own inline code, and code
  under another rounding mode the helpers as before. Code translated
  before a program's first inexact result keeps its slow paths inline
  (exact-only arithmetic doesn't leave the block on every instruction).
- Done with integer instructions: VCMP/VCMPE, VABS, VNEG, the VFP
  conversions (integer to float, single to double and back, float to
  integer toward zero), and NEON's compares (also with zero), VMAX, VMIN,
  VABD and float/integer conversions.
- NEON Q operands are loaded as their two D halves: VLD1 writes a Q
  register as two 64-bit stores, which a 128-bit load can't take from
  the store buffer (ROBench FIR 1.7x on its own).
- QEMU bug fixed on the way: its optimizer aborted on `0 - constant` for
  vectors on hosts with a vector negate (aarch64).
- Tests: `t_fpconform` covers more (NEON compares with zero, VACGT,
  64-bit NEON forms, VMUL/VMLS by scalar, VCMP with #0, VCVT.F64.U32,
  integer limits; CRC now c7541c16, 20.3 million results) and runs with
  QEMU's exact soft float, its host FPU helpers and the inline code;
  new `t_fpchain` runs long generated blocks of VFP and NEON
  instructions that feed each other, in ARM and Thumb (IT blocks), in 16
  FPSCR modes (CRC ce555057), and `t_fpchain_long`, blocks long enough
  for TCG to run out of temps and translate them again. 51 checks. Also checked under a
  `--enable-debug-tcg` build, and with two independent code reviews.
- A TB has at most 16 inline FP instructions: a slow path taken goes on
  in a new TB from the next instruction, so long straight-line FP code
  (with NaNs, say) would otherwise be translated again from each one.
- `RISCOS_NO_INLINEFP=1` turns the inline code off.
- ROBench on the Mac Studio's Linux VM (aarch64; release builds, LTO),
  against 0.8.1: Whetstone 1.4x, LINPACK 1.09x, FFT 1.3x, N-body 1.49x,
  Mandelbrot 1.13x, matrix multiply 1.26x; NEON saxpy 1.85x, FIR 1.71x,
  4x4 transform 2.35x. NEON integer code is unchanged.

## 0.8.1 (2026-10-18)

- SoundControl had the wrong SWI chunk (&51400): it is SoundCtrl's &50000,
  so Configure's Sound plug-in ("SWI &50000 not known") works. New check
  `soundctrl` calls the SWIs by name (45 checks).

## 0.8 (2026-10-18)

Sound. The port's ROM has RISC OS Open's SoundDMA (1.88, the HAL
version) and the sound modules above it, but no audio hardware, so it
never had sound. Now RISC OS Open's own modules make the sound and the
test bed passes it to Linux:

- `modules/LinuxAudio`: a HAL audio controller device (API 3, synchronous
  custom DMA) and mixer for SoundDMA, sending each buffer to a Linux FIFO
  (paced by its reader; dropped at the sample rate with none). `rol-run`
  loads it and restarts the ROM's SoundDMA, which finds it.
- `modules/SoundControl`: RISC OS Open's SoundControl 1.06 from source
  (the mixer interface: SoundCtrl SWIs, *MixVolume). SharedSound 1.20
  comes from the Pi ROM (`basicvfp/fetch.sh`), its Messages from
  LinuxAudio.
- `qemu/`: the ROM's sound modules change mode with plain MSRs, which
  Linux ignores (SoundScheduler crashed on its first event): QEMU emulates
  them, as it does for code from disc (`RISCOS_PSR_ROM_MODULES`).
  OS_Memory 0 gives the logical address for addresses outside application
  space, where the port refused (SoundDMA asks when it starts).
- `ROL_SOUND_WAV=FILE` saves the sound as a WAV file
  (`tools/rol-sndcap.py`); `ROL_SOUND=silent` keeps the silent stand-in.
- Tests: `sound-out` (a 440 Hz note panned left, a linear handler's output,
  a scheduled note, checked in the WAV file), `sounddma` now on the real
  SoundDMA (8 sample rates), `sounddma-silent`. 44 checks.

## 0.7.1 (2026-10-18)

- `modules/WindowScroll`: RISC OS Open's own WindowScroll 0.03 (source
  from gitlab.riscosopen.org, BSD-3-Clause, unchanged in `rool/`) built on
  the test bed's C module framework, with the pieces of the C library,
  RISC_OSLib and CMHG it needs (`port/`). It replaces 0.7's WheelScroll
  task: its task starts with the desktop (Service_StartWimp), it scrolls
  smoothly, follows panes to their parents, sends extended and line scroll
  requests where windows ask for them, and *WimpScroll sets the scroll
  type, line scrolling and speed as on a Pi. `tests/desktop-wheel.sh`
  tests it end to end.

## 0.7 (2026-10-18)

Floating point and NEON: the instructions go to the host's FPU and
vector unit where the answer can't differ from QEMU's exact emulation
(ROBench 1.01, which adds NEON tests). On an Apple Silicon Mac (the UTM
image): floating point 1.1-1.4 times as fast, NEON 1.9-4.7 times (FIR
filter 2.7x, 4x4 transforms 4.7x, YUV to RGB 2.0x); on x86 FP 1.2-1.4x,
NEON 1.3-3.7x. Before this, NEON code ran slower than the same code in
plain C under QEMU.

- `qemu/`: a host FPU fast path for add, subtract, multiply, divide and
  square root, single and double. The result is kept when it's a finite
  normal number (or an exact zero); the inexact flag is worked out
  exactly (TwoSum, exact products) when it isn't set yet. QEMU's own
  hardfloat only ran once inexact was already set, so exact arithmetic
  (integers held in floats) always took the soft path. NEON's four float
  lanes (VADD, VSUB, VMUL, VMLA, VMLS, by scalar too) go through the host's
  vector unit in one helper call. `RISCOS_NO_HOSTFP` turns it off.
- `qemu/`: VMRS APSR_nzcv (after every VCMP) sets the flags directly,
  without helper_cpsr_write.
- `qemu/`: one helper per instruction, vectorised by the host compiler,
  for the NEON 8- and 16-bit long ops (VADDL/VSUBL/VADDW/VSUBW, VABDL,
  VABAL, VMULL, VMLAL, VMLSL), narrowing shifts (VSHRN, VRSHRN, VQSHRUN,
  VQRSHRUN, VQSHRN, VQRSHRN), VMOVL/VSHLL and .I16 by-scalar multiplies;
  QEMU made two to six calls of 32 or 64 bits each.
  `RISCOS_NO_HOSTNEON` turns it off.
- `tests/programs/t_fpconform`: 14.4 million results (76 instructions x
  16 FPSCR modes, special values) and `t_neonint` (324 NEON integer
  instructions, every shift count), as CRCs; the same on x86-64 and
  aarch64 hosts, and the same as QEMU's exact emulation.
  (`tests/gen-neonint.py` writes t_neonint.c.)
- `modules/KVMWheel/WheelScroll`: the scroll wheel now scrolls windows.
  KVMWheel only told the kernel; scrolling windows is the job of RISC OS
  5's WindowScroll module, which the port's ROM doesn't have (the Pi
  ROM's copy is linked to its ROM's C library, so can't be loaded from
  disc). WheelScroll is a small task that does it: a Scroll_Request to
  windows that ask for them, otherwise an Open_Window_Request with the
  scroll offset moved. `tests/desktop-wheel.sh` checks it end to end.

## 0.6 (2026-10-18)

QEMU speed, profiled with perf while ROBench ran (the RISC OS benchmark,
riscos-robench). On the x86 test bed, against 0.5.1: SWIs 4.8 times as
fast, OS_ReadVarVal 2.6, byte-stream files 2.4-2.5, graphics 1.4,
BASIC 1.35, integer and floating point 1.1 (geometric means per group);
-O3 and LTO add 10-20% on top. On an Apple Silicon Mac (the UTM image):
SWIs 694k/s -> 3.4M/s.

- `qemu/`: the ROM's DynArea_PMP stubs are trapped when translated, not
  as breakpoints: with any breakpoint set QEMU checks every jump between
  translated blocks (a quarter of the time in integer code).
- `qemu/`: an SWI from RISC OS code no longer goes through a host signal
  (a signal frame and several host system calls each). The translated
  code calls a helper that does what the port's SIGSYS handler does
  (ENTER_EXCEPTION(SVC)) and carries on at the SWI vector.
  `RISCOS_NO_FASTSWI` and `RISCOS_NO_INLINE_SWI` turn it off.
- `qemu/`: __kernel_get_tls and __kernel_memory_barrier are generated
  inline (the port calls get_tls about five times per SWI), and so is
  MRS Rd, CPSR in code loaded from disc (UnixLib and SharedCLibrary read
  it often), instead of leaving the translated code.
- `qemu/`: MSR writing only the flags doesn't rebuild QEMU's cached CPU
  state.
- `qemu/build.sh`: -O3 and link-time optimisation.
- `RISCOS_EXCP_STATS` counts exits from the translated code by kind.

## 0.5.1 (2026-10-18)

- `qemu/`: MRC p15 c6 (DFAR) now gives the address of the fault being
  delivered, whatever raised it: a host SIGBUS or SIGSEGV as well as a
  data abort QEMU raises itself, and a signal frame that couldn't be
  written to the guest stack. Before, it gave the last data abort's.
  ARMEABISupport 1.08's abort handler maps a page at DFAR and retries,
  so with a stale address it mapped the same page for ever and the
  desktop stopped (TubeSearch's first HTTPS connection, through
  AcornSSL).
- `qemu/`: IOC timer 0 (&3200040, &3200044, &320004C) is emulated,
  counting down at 2 MHz from 19999. AcornSSL reads it for entropy; the
  port has no IOC, so a program's first secure connection got a data
  abort ("EMT trap" in UnixLib programs).
- `qemu/`: `RISCOS_SIG_DEBUG` lists the signals delivered to the port;
  `RISCOS_PMP_DEBUG=2` lists each abort and each pool page operation.

## 0.5 (2026-10-17)

GCCSDK's ARMEABISupport 1.08 runs on the port, and with it the Iris web
browser (riscosdev.com, April 2026: WebKit, GTK 3, Cairo).

- `qemu/`: what ARMEABISupport 1.08 needs from the kernel.
  - Physical memory pool (PMP) dynamic areas: the port creates them but
    its PhysOp, LogOp, Resize and GetPages are stubs that stop on an
    undefined instruction. Breakpoints on the stubs (found by name in the
    ROM) do them on a memfd per pool and keep the kernel node's sizes up
    to date.
  - OS_Memory 0 for application space gives a made-up physical address
    from the file the port maps at &8000 for the task: 1.08 tells
    programs apart by it.
  - MRC p15 c6/c5 read the last data abort's address and status (1.08's
    abort handler), MRC/MCR p15 c1 (SCTLR), c0 (MIDR, CTR, MPIDR).
  - An emulated exception return to Thumb code resumes in Thumb state
    (aborts taken in Iris's Thumb libraries came back in ARM state).
- `qemu/`: the port's IXFS numbered directory entries counting the
  host's "." and "..", which it hides, so OS_GBPB offsets had gaps.
  UnixLib's readdir needs them to go up by one per entry, so it saw an
  empty directory wherever it had more than about 25 entries: GTK found
  no icon theme, fontconfig no fonts. getdents now leaves "." and ".."
  out for the port. riscos-gtk's GTKThemeTest draws in full.
- `modules/ARMEABISupport` (lite): MemoryOp page allocators (as
  SOManager uses them) and AbortOp handlers, with the page mapping done
  by QEMU (SWI &C0002, abort handlers returning through SWI &C0003).
- `modules/Iconv`: new. UnixLib's iconv calls the Iconv module, which
  HardDisc4 hasn't got: UTF-8, ISO-8859-1/15, Windows-1252, ASCII,
  UTF-16 and UTF-32 (BOM or either byte order), //TRANSLIT, //IGNORE.
  `rol-run` loads it.
- `modules/KVMWheel`: the scroll wheel for SocketKVM front ends (the
  protocol has none): key numbers &1F0-&1F3 become PointerV WheelChange.
- `rol-run`: `ROL_MODULES=DIR` takes the modules from another directory
  (to run with ARMEABISupport 1.08, say).
- QEMU debugging: `RISCOS_SWI_TRACE` (every SWI RISC OS code calls),
  `RISCOS_SWI_RESULT=<hex>` (how one SWI returns), `RISCOS_PMP_DEBUG`,
  `RISCOS_REPORT` (Report_Text0 to stderr).
- Iris's JavaScript JIT works on the port too (the Thumb return fix):
  about 2.4 times faster than without on a test page.
- Tests: `iconv` (37 checks). With 1.08 in place of the lite module 33
  pass: the lite module's *ARMEABISupport_Info and nesting checks
  (`armeabisupport-info`, `system`) don't apply, and 1.08 doesn't unmap
  a hole in the middle of a mapping (`mmap`) or, in `files`, remove the
  directory (to look at).

## 0.4 (2026-10-17)

HardDisc4's applications run.

- `qemu/`: the rest of the privileged instructions code from disc uses
  are emulated with the port's virtual CPU: MRS/MSR SPSR, STM/LDM with ^,
  and exception returns (LDM {..., pc}^, MOVS/SUBS/ADDS pc). HardDisc4's
  SharedCLibrary 6.22 uses them to resume a program after a handler: the
  Mouse configuration plugin stopped with "undefined instruction". In USR
  mode, MSR and CPS now leave the mode and interrupt mask alone, as on a
  real CPU (BASIC VFP's CPSIE in USR mode crashed RISC OS, so !SciCalc
  and every *BASIC64 program in the desktop did). `RISCOS_PSR_DEBUG`
  reports undefined instructions that aren't emulated.
- `modules/SoundDMA`: a silent SoundDMA. The port has none, so programs
  that set sound up stopped with "SWI &4014x not known" (!Maestro, the
  QTM player in !Hopper). It keeps and returns the settings it's given;
  `rol-run` loads it and restarts the ROM's sound modules on it.
- `rol-run`: `ROL_KVM_CONTROL=FIFO` hands rol-kvm a control FIFO (mouse,
  keys, screenshots) in desktop mode.
- Tests: `priv-emulation`, `sounddma`, and `app-scicalc`, `app-maestro`,
  `app-hopper` (HardDisc4 applications that failed, running in the
  desktop): 36 checks. Every application on HardDisc4 was started and
  opened from its icon bar icon; all run but !Omni (its LanManager wants
  Internet 5; the port's Internet module is its own).

## 0.3.1 (2026-10-17)

- `qemu/`: where the host's `vm.mmap_min_addr` is above one page (65536
  on GitHub's runners), QEMU now maps the guest at an offset instead of at
  its own addresses. The port maps from 0x1000, which such a host refuses
  without CAP_SYS_RAWIO, so RISC OS stopped at once ("Failed to reserve
  address space") in containers, including the GitHub Actions jobs
  (outside a container, root could still map there). `RISCOS_IDENTITY_MAP`
  restores the old behaviour.
- `tests/programs/t_clib.c`: missing `<stdio.h>` (a warning).

## 0.3 (2026-10-17)

HardDisc4 5.30 runs unchanged.

- `qemu/`: privileged PSR instructions (`MRS CPSR`, `MSR CPSR` with the
  control field, `CPSID`/`CPSIE`) in code loaded from disc are emulated
  with the port's virtual CPU modes, through its PSR HAL routines (the
  port's own ROM code is unchanged). On Linux they read user mode and are
  ignored, so HardDisc4's SharedCLibrary 6.22, which enters USR mode with
  `MSR`, hung every C program. `RISCOS_NO_PSR_EMULATION` turns it off.
- `harddisc/fetch.sh`: no longer changes `!System` to keep the ROM's
  SharedCLibrary 6.15; HardDisc4's 6.22 is used.
- Tests: `psr-emulation` (MRS, MSR, CPS from BASIC's assembler, and a SWI
  back in USR mode) and `desktop-clib` (6.22 loaded by `!System`, and
  `BootVars` runs with it): 31 checks.

## 0.2 (2026-10-17)

The full RISC OS for Linux: the desktop, and threads.

- **Desktop mode** (`rol-run -d`, `-s FILE`, `-w SECONDS`, `-g WxH`): boots
  RISC OS Open's HardDisc4 (`!Boot`) into the desktop, headless, starts the
  program as a Wimp task, lets it run, and saves the screen as a PNG.
  Returns the program's status if it ended, 0 if it was still running.
  Programs can take more screenshots with `*TBShot`.
- `tools/rol-kvm.c`: a headless SocketKVM front end for the port (screen in
  memory, PNG on request, mouse and keys through a control FIFO).
- `tools/tbshot.s`: the `*TBShot` utility.
- `harddisc/`: HardDisc4 5.30 pinned by sha256 (`make harddisc`), unpacked
  in the port, filetypes as `,xxx` suffixes (`xattr2comma.py`), set up as
  the port's build does. HardDisc4's SharedCLibrary 6.22 hangs C programs
  on the port, so `!System` keeps the ROM's 6.15.
- `unixlib/`: UnixLib (riscos-unixlib edf6965) with the port's patch:
  thread switching and fault handling through the port's HAL routines
  instead of privileged instructions. `make unixlib` builds it; the test
  programs link with it; in the container image it replaces the
  toolchain's `libunixlib.a`.
- Tests: threads, VFP threads and the fault test now pass (they're
  ordinary tests when the patched UnixLib is built, XFAIL otherwise); three
  desktop tests (window in the screenshot, exit status, screen after
  close) with `tests/pngpixel.py`.
- **VFPSupport 0.91**: everything ROOL's VFPSupport 0.18 does that the
  port allows. ElementaryFunctions (ROOL's code, translated to GNU as, in
  a VFMA build and a software-FMA build chosen by HWCAP); ExceptionDump in
  full; switches atomic (interrupts off through the port's PSR device),
  so FastAPI works from interrupt code; one instance only;
  Service_ModulePostFinal on finalisation; Features 2 lists the
  elementary functions; MVFR2 on ARMv8; ROOL's errors. ROOL's own tests
  (`tests/rool`) pass but for lazy activation.
- `basicvfp/`: BASICVFP from RISC OS Open's Pi ROM (`make basicvfp`),
  loaded by rol-run, so `*BASIC64` works as on a Pi. Also takes ROOL's
  own VFPSupport out of the ROM, for a test that calls its elementary
  functions and ours on the same inputs: the same bits and flags.
- **ARMEABISupport**: a program's memory is now told apart by the stack
  the caller runs on (read through the port's PSR device), not by start
  order. Before, a desktop program that ended while one it had started was
  still running freed the other's stacks, and RISC OS hung. A program
  started by `system()` (which SharedUnixLibrary never "cleans up") now
  ends when it frees its own main stack, so its memory is freed and the
  nesting doesn't grow.
- New tests: elementary functions (both builds; correctly rounded results
  and ROOL's edge cases), switches from interrupts, two VFP desktop tasks,
  ROOL's VFPSupport tests, BASICVFP, one instance.
- riscos-crossdev 1.8.1 (its image has the autotools UnixLib's build
  needs). CI caches HardDisc4 and UnixLib; a secret `UNIXLIB_TOKEN` is used
  if riscos-unixlib is private.

## 0.1 (2026-10-15)

First version.

- `bin/rol-run`: runs an ELF (via elf2aif) or Absolute program, or a RISC OS
  command, in RISC OS for Linux, headless. It gives back the output and the
  exit status, with separate statuses for RISC OS errors, time-outs and a
  stop at the prompt.
- `qemu/`: Timothy Baldwin's QEMU changes for the port (SWI intercept,
  real-time signals, sparse ELF loading) ported to QEMU 8.2.2, on top of
  riscos-crossdev's alignment-trap patch.
- `modules/VFPSupport` 0.90: VFPSupport for the port (eager context
  switching, features from AT_HWCAP).
- `modules/ARMEABISupport` 1.90 (lite): stacks, mmap/mremap/munmap and
  Cleanup from one ordinary dynamic area. Programs are told apart by
  start-up and Cleanup, not physical pages.
- `modules/common`: building position-independent C modules with the
  riscos-crossdev compiler (GOT relocated at start, checked by
  `check-pic.py`).
- `rom/`: the ROM image pinned by sha256.
- `tests/`: 13 checks (mmap includes mremap zero-fill) and 3 known failures: pthreads and faults in UnixLib
  programs hang, because UnixLib uses privileged instructions the port
  runs in user mode.
- Container image (`docker/Dockerfile`) on riscos-crossdev 1.8; CI
  workflows.
