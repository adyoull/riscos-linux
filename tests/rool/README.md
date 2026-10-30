# RISC OS Open's VFPSupport tests

`features,ffb` and `test1,ffb` are RISC OS Open's own test programs for
VFPSupport (tokenised BBC BASIC, from `HWSupport/VFPSupport/Test` in RISC
OS Open's sources), copied unchanged under their BSD licence
(`LICENSE-ROOL`; copyright RISC OS Open Ltd). `tests/run.sh` runs them
against the test bed's VFPSupport.

`test1` ends with two checks that can't pass on RISC OS for Linux, where
VFP can't be switched off: it warns that a context "was activated
non-lazily", and reports that VFP instructions with no context active
didn't fail. `tests/run.sh` allows exactly those two.

ROOL's `test2` to `test7` exercise the support code (emulating VFP
instructions the hardware bounces), which the Linux port doesn't need.
