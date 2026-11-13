# The test bed's modules, built with the riscos-crossdev toolchain:
#   VFPSupport      assembler, position-independent as written
#   ARMEABISupport  C (+ header.s), position-independent but for its GOT,
#                   which it relocates when it starts; check-pic.py checks
#                   that by linking it at two addresses.
#   SoundDMA        assembler: a silent stand-in (the port has no sound)
#   Iconv           C: character set conversion for UnixLib's iconv
#   KVMWheel        assembler: the scroll wheel, from SocketKVM key numbers
#   WindowScroll    C: RISC OS Open's (rool/, BSD licence) on the C module
#                   framework (port/: what it needs from the C library,
#                   RISC_OSLib and CMHG)
# Output: $(OUT)/VFPSupport,ffa, $(OUT)/ARMEABISupport,ffa, $(OUT)/SoundDMA,ffa
#
#   make -C modules [OUT=dir] [RISCOS_TOOLCHAIN=...]
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).

RISCOS_TOOLCHAIN ?= $(RCD_DIR)/toolchain/env
OUT ?= ../out/modules
OBJ := $(OUT)/obj
T := $(RISCOS_TOOLCHAIN)/bin/arm-riscos-gnueabihf-
CC := $(T)gcc
AS := $(T)as
OBJCOPY := $(T)objcopy
NM := $(T)nm

CFLAGS := -O2 -marm -march=armv7-a -mno-unaligned-access -fpie -fvisibility=hidden \
          -mgeneral-regs-only -ffreestanding -fno-common -fno-stack-protector \
          -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
          -isystem $(shell $(CC) -print-file-name=include) \
          -Wall -Wextra -Werror -Icommon
LDFLAGS := -nostdlib -static -Wl,--no-dynamic-linker -Wl,-T,common/module.ld \
           -Wl,--build-id=none -Wl,-z,norelro

all: $(OUT)/VFPSupport,ffa $(OUT)/ARMEABISupport,ffa $(OUT)/SoundDMA,ffa $(OUT)/Iconv,ffa $(OUT)/KVMWheel,ffa $(OUT)/WindowScroll,ffa $(OUT)/LinuxAudio,ffa $(OUT)/SoundControl,ffa $(OUT)/NativeSupport,ffa $(OUT)/LinuxCodec,ffa $(OUT)/VCHIQ,ffa $(OUT)/BCMSupport,ffa $(OUT)/PCI,ffa $(OUT)/LinuxShell,ffa $(OUT)/USBDriver,ffa $(OUT)/MbufRes,ffa $(OUT)/LinuxOverlay,ffa $(OUT)/SCSILinux,ffa $(OUT)/MemMapEntries,ffa

$(OBJ) $(OUT):
	mkdir -p $@

# --- VFPSupport --------------------------------------------------------------
$(OBJ)/tables-save.inc: VFPSupport/gen-tables.py | $(OBJ)
	python3 -B VFPSupport/gen-tables.py $(OBJ)

$(OBJ)/VFPSupport.o: VFPSupport/VFPSupport.s $(OBJ)/tables-save.inc $(wildcard VFPSupport/elementary/*/*.s VFPSupport/elementary/*.inc) | $(OBJ)
	$(AS) -I $(OBJ) -I VFPSupport/elementary -o $@ $<
	@# (R_ARM_V4BX only marks BX instructions; it changes nothing)
	@if $(T)readelf -r $@ | grep R_ARM_ | grep -vq R_ARM_V4BX; then \
	  echo "VFPSupport.o has relocations: it must be position-independent" >&2; exit 1; fi

$(OUT)/VFPSupport,ffa: $(OBJ)/VFPSupport.o | $(OUT)
	$(OBJCOPY) -O binary $< $@

# --- SoundDMA (silent stand-in) ----------------------------------------------
$(OBJ)/SoundDMA.o: SoundDMA/SoundDMA.s | $(OBJ)
	$(AS) -o $@ $<
	@if $(T)readelf -r $@ | grep R_ARM_ | grep -vq R_ARM_V4BX; then \
	  echo "SoundDMA.o has relocations: it must be position-independent" >&2; exit 1; fi

$(OUT)/SoundDMA,ffa: $(OBJ)/SoundDMA.o | $(OUT)
	$(OBJCOPY) -O binary $< $@

# --- ARMEABISupport ------------------------------------------------------------
AE_OBJS := $(OBJ)/ae-header.o $(OBJ)/ae-armeabi.o $(OBJ)/ae-cmodule.o

$(OBJ)/ae-header.o: ARMEABISupport/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/ae-armeabi.o: ARMEABISupport/armeabi.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJ)/ae-cmodule.o: common/cmodule.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/ARMEABISupport.elf: $(AE_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(AE_OBJS) -lgcc
$(OBJ)/ARMEABISupport-at64k.elf: $(AE_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(AE_OBJS) -lgcc

$(OUT)/ARMEABISupport,ffa: $(OBJ)/ARMEABISupport.elf $(OBJ)/ARMEABISupport-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/ARMEABISupport.elf $(OBJ)/ae0.bin
	$(OBJCOPY) -O binary $(OBJ)/ARMEABISupport-at64k.elf $(OBJ)/ae1.bin
	python3 -B common/check-pic.py $(OBJ)/ae0.bin $(OBJ)/ae1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/ARMEABISupport.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/ARMEABISupport.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/ae0.bin $@

# --- KVMWheel (the scroll wheel for SocketKVM front ends) ---------------------
$(OBJ)/KVMWheel.o: KVMWheel/KVMWheel.s | $(OBJ)
	$(AS) -o $@ $<
	@if $(T)readelf -r $@ | grep R_ARM_ | grep -vq R_ARM_V4BX; then \
	  echo "KVMWheel.o has relocations: it must be position-independent" >&2; exit 1; fi

$(OUT)/KVMWheel,ffa: $(OBJ)/KVMWheel.o | $(OUT)
	$(OBJCOPY) -O binary $< $@

# --- MbufRes (MbufManager's Messages in ResourceFS, for the softloaded one) -
$(OBJ)/res-mbuf.inc: MbufRes/Messages MbufRes/mkres.py | $(OBJ)
	python3 -B MbufRes/mkres.py $< $@

$(OBJ)/MbufRes.o: MbufRes/MbufRes.s $(OBJ)/res-mbuf.inc | $(OBJ)
	$(AS) -I $(OBJ) -o $@ $<
	@if $(T)readelf -r $@ | grep R_ARM_ | grep -vq R_ARM_V4BX; then \
	  echo "MbufRes.o has relocations: it must be position-independent" >&2; exit 1; fi

$(OUT)/MbufRes,ffa: $(OBJ)/MbufRes.o | $(OUT)
	$(OBJCOPY) -O binary $< $@

# --- LinuxAudio (a HAL audio device for the ROM's SoundDMA) -----------------
LA_OBJS := $(OBJ)/la-header.o $(OBJ)/la-linuxaudio.o $(OBJ)/ae-cmodule.o

$(OBJ)/la-resources.inc: LinuxAudio/rool-sharedsound/Messages SoundControl/port/mkres.py | $(OBJ)
	python3 -B SoundControl/port/mkres.py SSound LinuxAudio/rool-sharedsound/Messages - $@
$(OBJ)/la-header.o: LinuxAudio/header.s common/cmodule.inc $(OBJ)/la-resources.inc | $(OBJ)
	$(AS) -I common -I $(OBJ) -o $@ $<
$(OBJ)/la-linuxaudio.o: LinuxAudio/linuxaudio.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/LinuxAudio.elf: $(LA_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(LA_OBJS) -lgcc
$(OBJ)/LinuxAudio-at64k.elf: $(LA_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(LA_OBJS) -lgcc

$(OUT)/LinuxAudio,ffa: $(OBJ)/LinuxAudio.elf $(OBJ)/LinuxAudio-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/LinuxAudio.elf $(OBJ)/la0.bin
	$(OBJCOPY) -O binary $(OBJ)/LinuxAudio-at64k.elf $(OBJ)/la1.bin
	python3 -B common/check-pic.py $(OBJ)/la0.bin $(OBJ)/la1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/LinuxAudio.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/LinuxAudio.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/la0.bin $@

# --- LinuxOverlay (GraphicsV's overlay calls, for VideoOverlay) -------------
LO_OBJS := $(OBJ)/lo-header.o $(OBJ)/lo-overlay.o $(OBJ)/ae-cmodule.o

$(OBJ)/lo-header.o: LinuxOverlay/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/lo-overlay.o: LinuxOverlay/linuxoverlay.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/LinuxOverlay.elf: $(LO_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(LO_OBJS) -lgcc
$(OBJ)/LinuxOverlay-at64k.elf: $(LO_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(LO_OBJS) -lgcc

$(OUT)/LinuxOverlay,ffa: $(OBJ)/LinuxOverlay.elf $(OBJ)/LinuxOverlay-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/LinuxOverlay.elf $(OBJ)/lo0.bin
	$(OBJCOPY) -O binary $(OBJ)/LinuxOverlay-at64k.elf $(OBJ)/lo1.bin
	python3 -B common/check-pic.py $(OBJ)/lo0.bin $(OBJ)/lo1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/LinuxOverlay.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/LinuxOverlay.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/lo0.bin $@

# --- SCSILinux (FileCore discs through Linux, for SCSIFS) -------------------
SL_OBJS := $(OBJ)/sl-header.o $(OBJ)/sl-scsi.o $(OBJ)/ae-cmodule.o

$(OBJ)/sl-header.o: SCSILinux/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/sl-scsi.o: SCSILinux/scsilinux.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/SCSILinux.elf: $(SL_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SL_OBJS) -lgcc
$(OBJ)/SCSILinux-at64k.elf: $(SL_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(SL_OBJS) -lgcc

$(OUT)/SCSILinux,ffa: $(OBJ)/SCSILinux.elf $(OBJ)/SCSILinux-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/SCSILinux.elf $(OBJ)/sl0.bin
	$(OBJCOPY) -O binary $(OBJ)/SCSILinux-at64k.elf $(OBJ)/sl1.bin
	python3 -B common/check-pic.py $(OBJ)/sl0.bin $(OBJ)/sl1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/SCSILinux.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/SCSILinux.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/sl0.bin $@

# --- LinuxCodec (Linux's V4L2 video decoders for RISC OS programs) ----------
LC_OBJS := $(OBJ)/lc-header.o $(OBJ)/lc-codec.o $(OBJ)/ae-cmodule.o

$(OBJ)/lc-header.o: LinuxCodec/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/lc-codec.o: LinuxCodec/linuxcodec.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/LinuxCodec.elf: $(LC_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(LC_OBJS) -lgcc
$(OBJ)/LinuxCodec-at64k.elf: $(LC_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(LC_OBJS) -lgcc

$(OUT)/LinuxCodec,ffa: $(OBJ)/LinuxCodec.elf $(OBJ)/LinuxCodec-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/LinuxCodec.elf $(OBJ)/lc0.bin
	$(OBJCOPY) -O binary $(OBJ)/LinuxCodec-at64k.elf $(OBJ)/lc1.bin
	python3 -B common/check-pic.py $(OBJ)/lc0.bin $(OBJ)/lc1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/LinuxCodec.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/LinuxCodec.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/lc0.bin $@

# --- VCHIQ, BCMSupport, PCI: stand-ins for the Pi's modules (LinuxCodec/) ---
define STANDIN
$$(OBJ)/$(1)-header.o: LinuxCodec/$(2)-header.s common/cmodule.inc | $$(OBJ)
	$$(AS) -I common -o $$@ $$<
$$(OBJ)/$(1)-c.o: LinuxCodec/$(2).c LinuxCodec/rolsys.h common/cmodule.h | $$(OBJ)
	$$(CC) $$(CFLAGS) -I common -c -o $$@ $$<
$$(OBJ)/$(1).elf: $$(OBJ)/$(1)-header.o $$(OBJ)/$(1)-c.o $$(OBJ)/ae-cmodule.o common/module.ld
	$$(CC) $$(CFLAGS) $$(LDFLAGS) -o $$@ $$(OBJ)/$(1)-header.o $$(OBJ)/$(1)-c.o $$(OBJ)/ae-cmodule.o -lgcc
$$(OBJ)/$(1)-at64k.elf: $$(OBJ)/$(1)-header.o $$(OBJ)/$(1)-c.o $$(OBJ)/ae-cmodule.o common/module.ld
	$$(CC) $$(CFLAGS) $$(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $$@ $$(OBJ)/$(1)-header.o $$(OBJ)/$(1)-c.o $$(OBJ)/ae-cmodule.o -lgcc
$$(OUT)/$(1),ffa: $$(OBJ)/$(1).elf $$(OBJ)/$(1)-at64k.elf common/check-pic.py | $$(OUT)
	$$(OBJCOPY) -O binary $$(OBJ)/$(1).elf $$(OBJ)/$(1)0.bin
	$$(OBJCOPY) -O binary $$(OBJ)/$(1)-at64k.elf $$(OBJ)/$(1)1.bin
	python3 -B common/check-pic.py $$(OBJ)/$(1)0.bin $$(OBJ)/$(1)1.bin 0x10000 \
	  0x$$$$($$(NM) $$(OBJ)/$(1).elf | awk '$$$$3=="__got_start"{print $$$$1}') \
	  0x$$$$($$(NM) $$(OBJ)/$(1).elf | awk '$$$$3=="__got_end"{print $$$$1}')
	cp $$(OBJ)/$(1)0.bin $$@
endef
$(eval $(call STANDIN,VCHIQ,vchiq))
$(eval $(call STANDIN,BCMSupport,bcmsupport))
$(eval $(call STANDIN,PCI,pci))

# --- LinuxShell: *Linux (through the images' rol-cmd daemon) ----------------
$(OBJ)/LinuxShell-header.o: LinuxShell/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/LinuxShell-c.o: LinuxShell/linuxshell.c LinuxCodec/rolsys.h common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -I common -c -o $@ $<
LSH_OBJS := $(OBJ)/LinuxShell-header.o $(OBJ)/LinuxShell-c.o $(OBJ)/ae-cmodule.o
$(OBJ)/LinuxShell.elf: $(LSH_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(LSH_OBJS) -lgcc
$(OBJ)/LinuxShell-at64k.elf: $(LSH_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(LSH_OBJS) -lgcc
$(OUT)/LinuxShell,ffa: $(OBJ)/LinuxShell.elf $(OBJ)/LinuxShell-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/LinuxShell.elf $(OBJ)/lsh0.bin
	$(OBJCOPY) -O binary $(OBJ)/LinuxShell-at64k.elf $(OBJ)/lsh1.bin
	python3 -B common/check-pic.py $(OBJ)/lsh0.bin $(OBJ)/lsh1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/LinuxShell.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/LinuxShell.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/lsh0.bin $@

# --- MemMapEntries: OS_Find/Read/SetMemMapEntries for the port (memmap.c) ----
$(OBJ)/MemMapEntries-header.o: MemMapEntries/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/MemMapEntries-c.o: MemMapEntries/memmap.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -I common -c -o $@ $<
MME_OBJS := $(OBJ)/MemMapEntries-header.o $(OBJ)/MemMapEntries-c.o $(OBJ)/ae-cmodule.o
$(OBJ)/MemMapEntries.elf: $(MME_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(MME_OBJS) -lgcc
$(OBJ)/MemMapEntries-at64k.elf: $(MME_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(MME_OBJS) -lgcc
$(OUT)/MemMapEntries,ffa: $(OBJ)/MemMapEntries.elf $(OBJ)/MemMapEntries-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/MemMapEntries.elf $(OBJ)/mme0.bin
	$(OBJCOPY) -O binary $(OBJ)/MemMapEntries-at64k.elf $(OBJ)/mme1.bin
	python3 -B common/check-pic.py $(OBJ)/mme0.bin $(OBJ)/mme1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/MemMapEntries.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/MemMapEntries.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/mme0.bin $@

# --- USBDriver (RISC OS for Linux): RISC OS's USB device interface on usbfs ---
$(OBJ)/USBLinux-header.o: USBLinux/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/USBLinux-c.o: USBLinux/usblinux.c LinuxCodec/rolsys.h common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -I common -c -o $@ $<
USBL_OBJS := $(OBJ)/USBLinux-header.o $(OBJ)/USBLinux-c.o $(OBJ)/ae-cmodule.o
$(OBJ)/USBDriver.elf: $(USBL_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(USBL_OBJS) -lgcc
$(OBJ)/USBDriver-at64k.elf: $(USBL_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(USBL_OBJS) -lgcc
$(OUT)/USBDriver,ffa: $(OBJ)/USBDriver.elf $(OBJ)/USBDriver-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/USBDriver.elf $(OBJ)/usbl0.bin
	$(OBJCOPY) -O binary $(OBJ)/USBDriver-at64k.elf $(OBJ)/usbl1.bin
	python3 -B common/check-pic.py $(OBJ)/usbl0.bin $(OBJ)/usbl1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/USBDriver.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/USBDriver.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/usbl0.bin $@

# --- NativeSupport (RISC OS for Linux running natively: see native.c) -------
NS_OBJS := $(OBJ)/ns-header.o $(OBJ)/ns-native.o $(OBJ)/ae-cmodule.o

$(OBJ)/ns-header.o: NativeSupport/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/ns-native.o: NativeSupport/native.c $(wildcard NativeSupport/*.c) common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/NativeSupport.elf: $(NS_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(NS_OBJS) -lgcc
$(OBJ)/NativeSupport-at64k.elf: $(NS_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(NS_OBJS) -lgcc

$(OUT)/NativeSupport,ffa: $(OBJ)/NativeSupport.elf $(OBJ)/NativeSupport-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/NativeSupport.elf $(OBJ)/ns0.bin
	$(OBJCOPY) -O binary $(OBJ)/NativeSupport-at64k.elf $(OBJ)/ns1.bin
	python3 -B common/check-pic.py $(OBJ)/ns0.bin $(OBJ)/ns1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/NativeSupport.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/NativeSupport.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/ns0.bin $@

# --- Iconv --------------------------------------------------------------------
IC_OBJS := $(OBJ)/ic-header.o $(OBJ)/ic-iconv.o $(OBJ)/ae-cmodule.o

$(OBJ)/ic-header.o: Iconv/header.s common/cmodule.inc | $(OBJ)
	$(AS) -I common -o $@ $<
$(OBJ)/ic-iconv.o: Iconv/iconv.c common/cmodule.h | $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/Iconv.elf: $(IC_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(IC_OBJS) -lgcc
$(OBJ)/Iconv-at64k.elf: $(IC_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(IC_OBJS) -lgcc

$(OUT)/Iconv,ffa: $(OBJ)/Iconv.elf $(OBJ)/Iconv-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/Iconv.elf $(OBJ)/ic0.bin
	$(OBJCOPY) -O binary $(OBJ)/Iconv-at64k.elf $(OBJ)/ic1.bin
	python3 -B common/check-pic.py $(OBJ)/ic0.bin $(OBJ)/ic1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/Iconv.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/Iconv.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/ic0.bin $@

# --- WindowScroll (RISC OS Open's) ---------------------------------------------
# ROOL's sources are used as they are, but for one change made here: a
# table of string pointers becomes a table of strings (the framework
# relocates only the GOT, and initialised pointers aren't in it).
WS := WindowScroll
WSO := $(OBJ)/ws
WS_OBJS := $(WSO)/header.o $(WSO)/cmodule.o $(WSO)/task.o $(WSO)/utils.o $(WSO)/port.o $(OBJ)/ae-cmodule.o
WS_CFLAGS := $(filter-out -Werror -Wextra,$(CFLAGS)) -Wno-unknown-pragmas -Wno-sign-compare \
             -I$(WS)/port -I$(WSO)/h -DSTANDALONE

$(WSO) $(WSO)/h:
	mkdir -p $@
$(WSO)/h/%.h: $(WS)/rool/h/% | $(WSO)/h
	cp $< $@
WS_HDRS := $(WSO)/h/globals.h $(WSO)/h/utils.h $(wildcard $(WS)/port/*.h $(WS)/port/*/*.h)
$(WSO)/cmodule.c: $(WS)/rool/c/cmodule | $(WSO)
	sed 's/^static const char\* scroll_types\[\] = {/static const char scroll_types[][16] = {/' $< > $@
	grep -q 'scroll_types\[\]\[16\]' $@
$(WSO)/%.c: $(WS)/rool/c/% | $(WSO)
	cp $< $@
$(WSO)/%.o: $(WSO)/%.c $(WS_HDRS)
	$(CC) $(WS_CFLAGS) -c -o $@ $<
$(WSO)/port.o: $(WS)/port/port.c $(WS_HDRS) | $(WSO)
	$(CC) $(CFLAGS) -I$(WS)/port -c -o $@ $<
$(WSO)/resources.inc: $(WS)/rool/Resources/UK/Messages $(WS)/rool/Resources/UK/CmdHelp $(WS)/port/mkres.py | $(WSO)
	python3 -B $(WS)/port/mkres.py $(WS)/rool/Resources/UK/Messages $(WS)/rool/Resources/UK/CmdHelp $@
$(WSO)/header.o: $(WS)/port/header.s common/cmodule.inc $(WSO)/resources.inc
	$(AS) -I common -I $(WSO) -o $@ $<

$(OBJ)/WindowScroll.elf: $(WS_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(WS_OBJS) -lgcc
$(OBJ)/WindowScroll-at64k.elf: $(WS_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(WS_OBJS) -lgcc

$(OUT)/WindowScroll,ffa: $(OBJ)/WindowScroll.elf $(OBJ)/WindowScroll-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/WindowScroll.elf $(OBJ)/ws0.bin
	$(OBJCOPY) -O binary $(OBJ)/WindowScroll-at64k.elf $(OBJ)/ws1.bin
	python3 -B common/check-pic.py $(OBJ)/ws0.bin $(OBJ)/ws1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/WindowScroll.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/WindowScroll.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/ws0.bin $@

# --- SoundControl (RISC OS Open's: the mixer interface) ------------------------
# ROOL's sources as they are (rool/), but for one change made here: a mixer
# device's GetMix returns its result in r0 and r1 (Norcroft's
# __value_in_regs), which GCC can't declare, so module.c's two calls go
# through port.c's mixer_getmix.
SC := SoundControl
SCO := $(OBJ)/sc
SC_OBJS := $(SCO)/header.o $(SCO)/module.o $(SCO)/mess.o $(SCO)/global.o $(SCO)/port.o $(OBJ)/ae-cmodule.o
SC_CFLAGS := $(filter-out -Werror -Wextra,$(CFLAGS)) -Wno-unknown-pragmas -Wno-sign-compare \
             -Wno-builtin-declaration-mismatch -I$(SC)/port -I$(SCO)/h -DROM_MODULE_NOT

$(SCO) $(SCO)/h:
	mkdir -p $@
$(SCO)/h/%.h: $(SC)/rool/h/% | $(SCO)/h
	cp $< $@
SC_HDRS := $(SCO)/h/global.h $(SCO)/h/mess.h $(SCO)/h/resmess.h $(wildcard $(SC)/port/*.h $(SC)/port/*/*.h)
$(SCO)/module.c: $(SC)/rool/c/module | $(SCO)
	sed 's/system->device->GetMix(system->device, channel)/mixer_getmix(system->device, channel)/' $< > $@
	test $$(grep -c 'mixer_getmix(system->device, channel)' $@) = 2
$(SCO)/%.c: $(SC)/rool/c/% | $(SCO)
	cp $< $@
$(SCO)/%.o: $(SCO)/%.c $(SC_HDRS)
	$(CC) $(SC_CFLAGS) -c -o $@ $<
$(SCO)/port.o: $(SC)/port/port.c $(SC_HDRS) | $(SCO)
	$(CC) $(CFLAGS) -I$(SC)/port -c -o $@ $<
$(SCO)/resources.inc: $(SC)/rool/Resources/UK/Messages $(SC)/rool/Resources/UK/CmdHelp $(SC)/port/mkres.py | $(SCO)
	python3 -B $(SC)/port/mkres.py SoundCtrl $(SC)/rool/Resources/UK/Messages $(SC)/rool/Resources/UK/CmdHelp $@
$(SCO)/header.o: $(SC)/port/header.s common/cmodule.inc $(SCO)/resources.inc
	$(AS) -I common -I $(SCO) -o $@ $<

$(OBJ)/SoundControl.elf: $(SC_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SC_OBJS) -lgcc
$(OBJ)/SoundControl-at64k.elf: $(SC_OBJS) common/module.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=__link_base=0x10000 -o $@ $(SC_OBJS) -lgcc

$(OUT)/SoundControl,ffa: $(OBJ)/SoundControl.elf $(OBJ)/SoundControl-at64k.elf common/check-pic.py | $(OUT)
	$(OBJCOPY) -O binary $(OBJ)/SoundControl.elf $(OBJ)/sc0.bin
	$(OBJCOPY) -O binary $(OBJ)/SoundControl-at64k.elf $(OBJ)/sc1.bin
	python3 -B common/check-pic.py $(OBJ)/sc0.bin $(OBJ)/sc1.bin 0x10000 \
	  0x$$($(NM) $(OBJ)/SoundControl.elf | awk '$$3=="__got_start"{print $$1}') \
	  0x$$($(NM) $(OBJ)/SoundControl.elf | awk '$$3=="__got_end"{print $$1}')
	cp $(OBJ)/sc0.bin $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
