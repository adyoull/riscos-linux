# VFPSupport's elementary functions

VFPSupport_ElementaryFunctions (sin, cos, tan, asin, acos, atan, atan2,
log, log10, exp, pow and fused multiply-add, in single and double
precision) is RISC OS Open's own code: `s/Trig32`, `Trig64`, `ArcTrig32`,
`ArcTrig64`, `Power32`, `Power64`, `Fused32`, `Fused64` and `CMath` from
ROOL's VFPSupport 0.18 (`HWSupport/VFPSupport` in RISC OS Open's sources),
translated from objasm to GNU as by `objasm2gas.py`. Each file keeps its
copyright and licence notices (BSD-style: RISC OS Open Ltd; parts from
Sun Microsystems' fdlibm, AMD's aocl-libm and FreeBSD); see LICENCES.md.

There are two builds, as ROOL builds a ROM for each kind of machine:

- `v3/`: for cores without VFPv4 (FMA done in software by
  fused32/64_muladd), as ROOL's builds for the BeagleBoard and Pi 1;
- `v4/`: for VFPv4 cores (the VFMA instruction), as ROOL's builds for the
  Pi 2 and later.

VFPSupport chooses one at start-up from Linux's HWCAP. Both avoid ARMv6T2
instructions, so they also run on an ARM11 (Pi 1).

To make them again (from ROOL's sources, e.g. a newer release):

    python3 objasm2gas.py <ROOL>/HWSupport/VFPSupport/s v3 v3
    python3 objasm2gas.py <ROOL>/HWSupport/VFPSupport/s v4 v4

The translation was checked by comparing each file's instructions with the
original's (the same counts of each, apart from the parts left out by the
conditional assembly), and the functions are tested by
`tests/programs/t_elementary.c` against correctly rounded results and
ROOL's own edge cases (special inputs and the C standard's exception
flags), with both builds.
