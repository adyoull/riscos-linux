# riscos-linux-testbed: run RISC OS programs built with riscos-crossdev in
# RISC OS for Linux (Timothy Baldwin's Linux port of RISC OS), headless.
#
#   make             everything below except check
#   make qemu        out/bin/qemu-arm-riscos (QEMU 8.2.2 + the port's changes)
#   make modules     out/modules/VFPSupport,ffa and ARMEABISupport,ffa
#   make rom         out/rom/RISCOS.IMG (downloaded, checked by sha256)
#   make tools       out/bin/rol-kvm (the headless screen) and TBShot,ffc
#   make unixlib     out/unixlib/libunixlib.a (UnixLib + the port's patch;
#                    not part of all: it clones riscos-unixlib and GCCSDK)
#   make harddisc    out/harddisc/HardDisc4 (downloaded, unpacked in RISC OS)
#   make basicvfp    out/modules/BASICVFP,ffa (from RISC OS Open's Pi ROM)
#   make programs    the test programs -> out/tests
#   make check       tests/run.sh
#   make image       the container image (docker/Dockerfile)
#   make clean       remove out/ (and qemu/work)
#
# RCD_DIR is the riscos-crossdev checkout (default /opt/riscos-crossdev,
# as in its container image).
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

RCD_DIR ?= /opt/riscos-crossdev
export RCD_DIR
RISCOS_TOOLCHAIN ?= $(RCD_DIR)/toolchain/env
export RISCOS_TOOLCHAIN

all: qemu modules rom tools programs

# Built when missing (delete it, or make clean, after changing the patch):
# CI restores it from a cache keyed on qemu/*, so its date means nothing.
qemu: out/bin/qemu-arm-riscos
out/bin/qemu-arm-riscos:
	qemu/build.sh

modules:
	$(MAKE) -C modules OUT=$(CURDIR)/out/modules

rom: out/rom/RISCOS.IMG
out/rom/RISCOS.IMG: rom/ROM
	rom/fetch.sh

T := $(RISCOS_TOOLCHAIN)/bin/arm-riscos-gnueabihf-
tools: out/bin/rol-kvm out/bin/TBShot,ffc
out/bin/rol-kvm: tools/rol-kvm.c
	mkdir -p out/bin
	$(CC) -O2 -Wall -Wextra -o $@ $<
# rol-kvm for an aarch64 Linux: tools/native-vm runs it in its VM
out/bin/rol-kvm-aarch64: tools/rol-kvm.c
	mkdir -p out/bin
	aarch64-linux-gnu-gcc -O2 -static -Wall -Wextra -o $@ $<
out/bin/TBShot,ffc: tools/tbshot.s
	mkdir -p out/bin
	$(T)as -o out/bin/tbshot.o $<
	$(T)objcopy -O binary out/bin/tbshot.o $@
	rm -f out/bin/tbshot.o

unixlib: out/unixlib/libunixlib.a
out/unixlib/libunixlib.a: unixlib/UNIXLIB unixlib/linux-port-pvops.patch unixlib/build.sh
	unixlib/build.sh

# Not part of all (a 5.4 MB download from riscosopen.org): *BASIC64 needs it.
basicvfp: out/modules/BASICVFP,ffa
out/modules/BASICVFP,ffa: basicvfp/PIROM basicvfp/fetch.sh
	basicvfp/fetch.sh

# Not part of all (it's 18 MB, from riscosopen.org): rol-run -d needs it.
harddisc: out/harddisc/HardDisc4
out/harddisc/HardDisc4: harddisc/HD4 harddisc/fetch.sh harddisc/xattr2comma.py | qemu rom
	harddisc/fetch.sh

# With make -j, link the programs only after the UnixLib asked for with them.
programs: $(filter unixlib,$(MAKECMDGOALS))
	$(MAKE) -C tests/programs OUT=$(CURDIR)/out/tests

check: all
	@echo "== scripts parse"
	@for s in bin/rol-run tests/run.sh rom/fetch.sh harddisc/fetch.sh basicvfp/fetch.sh unixlib/build.sh qemu/build.sh; do bash -n $$s || exit 1; done
	@echo "== tests in RISC OS for Linux"
	tests/run.sh

image:
	docker build -f docker/Dockerfile -t riscos-linux-testbed .

clean:
	rm -rf out qemu/work

.PHONY: all qemu modules rom tools unixlib harddisc basicvfp programs check image clean
