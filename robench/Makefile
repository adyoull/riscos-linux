# ROBench: make RCD_DIR=<riscos-crossdev>   (RISC OS build, out/robench,ff8)
#          make native                       (host build, out/robench-native)
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
RCD_DIR ?= ../riscos-crossdev
include $(RCD_DIR)/riscos.mk
SRC := src/main.c src/cpu.c src/neon.c src/riscos.c
HDR := src/bench.h src/jpegdata.h

all: out/robench,ff8 out/robench-native

out/robench: $(SRC) $(HDR) | out
	$(RO_CC) $(RISCOS_CFLAGS) -Wall -Wextra -c -o out/neon.o src/neon.c -mfpu=neon -ffp-contract=off
	$(RO_CC) $(RISCOS_CFLAGS) -Wall -Wextra -o $@ $(filter-out src/neon.c,$(SRC)) out/neon.o $(RISCOS_LDFLAGS) -lm

out/robench,ff8: out/robench
	$(ELF2AIF) -e $< $@

native: out/robench-native
out/robench-native: $(SRC) $(HDR) | out
	cc -O2 -ffp-contract=off -Wall -Wextra -o $@ $(SRC) -lm

out:
	mkdir -p out

clean:
	rm -rf out
.PHONY: all native clean
