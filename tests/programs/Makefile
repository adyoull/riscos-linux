# Test programs, built with riscos-crossdev (RCD_DIR), run by tests/run.sh.
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
RCD_DIR ?= /opt/riscos-crossdev
include $(RCD_DIR)/riscos.mk
OUT ?= ../../out/tests
# The patched UnixLib (unixlib/build.sh), when it has been built: linked
# instead of the toolchain's, so that threads and faults work on the port.
UNIXLIB_DIR ?= ../../out/unixlib
UNIXLIB_A := $(wildcard $(UNIXLIB_DIR)/libunixlib.a)
UL_LDFLAGS := $(if $(UNIXLIB_A),-L$(abspath $(UNIXLIB_DIR)))
PROGS := t_exit t_vfpsupport t_cxx t_mmap t_system t_files t_crash t_threads t_vfp_threads t_wimp t_wimp_vfp t_elementary t_vfp_irq t_rool_elementary t_clib t_iconv t_fpconform t_neonint t_fpchain t_fpchain_long t_vfp_errors

all: $(addprefix $(OUT)/,$(PROGS))

$(OUT):
	mkdir -p $@
$(OUT)/%: %.c $(UNIXLIB_A) | $(OUT) riscos-toolchain-present
	$(RO_CC) $(RISCOS_CFLAGS) -Wall -Wextra $(UL_LDFLAGS) $(RISCOS_LDFLAGS) -o $@ $< -lpthread
$(OUT)/%: %.cc $(UNIXLIB_A) | $(OUT) riscos-toolchain-present
	$(RO_CXX) $(RISCOS_CFLAGS) -Wall -Wextra $(UL_LDFLAGS) $(RISCOS_LDFLAGS) -o $@ $<

$(OUT)/t_elementary $(OUT)/t_rool_elementary: elementary_data.h

# VFPv4 and NEON instructions (VFMA, the Q forms): run on ARMv7 with NEON
$(OUT)/t_fpconform $(OUT)/t_neonint: $(OUT)/%: %.c | $(OUT) riscos-toolchain-present
	$(RO_CC) $(RISCOS_CFLAGS) -mfpu=neon-vfpv4 -Wall -Wextra $(UL_LDFLAGS) $(RISCOS_LDFLAGS) -o $@ $<

# Generated blocks of floating point instructions (fpchain_gen.py), built
# for ARM and for Thumb (IT blocks), and the program that runs them
$(OUT)/t_fpchain: t_fpchain.c chain_blocks.c t_fpchain_blocks.h | $(OUT) riscos-toolchain-present
	$(RO_CC) $(RISCOS_CFLAGS) -mfpu=neon-vfpv4 -marm -Wall -c -o $(OUT)/fpchain_arm.o chain_blocks.c
	$(RO_CC) $(RISCOS_CFLAGS) -mfpu=neon-vfpv4 -mthumb -DTHUMB -Wall -c -o $(OUT)/fpchain_thumb.o chain_blocks.c
	$(RO_CC) $(RISCOS_CFLAGS) -mfpu=neon-vfpv4 -Wall -Wextra $(UL_LDFLAGS) $(RISCOS_LDFLAGS) -o $@ t_fpchain.c $(OUT)/fpchain_arm.o $(OUT)/fpchain_thumb.o
# The same with long blocks (fpchain_gen.py 77 3 450) and no warm-up.
$(OUT)/t_fpchain_long: t_fpchain.c chain_blocks.c t_fpchain_long_blocks.h | $(OUT) riscos-toolchain-present
	$(RO_CC) $(RISCOS_CFLAGS) -mfpu=neon-vfpv4 -DLONG -marm -Wall -c -o $(OUT)/fpchainl_arm.o chain_blocks.c
	$(RO_CC) $(RISCOS_CFLAGS) -mfpu=neon-vfpv4 -DLONG -mthumb -DTHUMB -Wall -c -o $(OUT)/fpchainl_thumb.o chain_blocks.c
	$(RO_CC) $(RISCOS_CFLAGS) -mfpu=neon-vfpv4 -DLONG -Wall -Wextra $(UL_LDFLAGS) $(RISCOS_LDFLAGS) -o $@ t_fpchain.c $(OUT)/fpchainl_arm.o $(OUT)/fpchainl_thumb.o

clean:
	rm -rf $(OUT)
.PHONY: all clean
