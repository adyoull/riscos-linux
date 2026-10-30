# Copyright 1996 Acorn Computers Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Makefile for Kernel
#

COMPONENT       = Kernel

ifeq (${MAKECMDGOALS},install)
EXP_HDR         = ${INSTDIR}.Hdr.Interface
C_EXP_HDR       = ${INSTDIR}.C.Global.h
else
C_EXP_HDR       = <cexport$dir>.Global.h
endif

# Keep SyncLib out of the kernel for now:
# 1. We don't have a way of unlocking mutexes/spinlocks when recovering from
#    aborts
USE_SYNCLIB    ?= FALSE

TOKHELPSRC      = ${TOKENSOURCE}
HELPSRC         = HelpStrs
OBJS            = GetAll
KERNEL_MODULE   = bin${SEP}${COMPONENT}
ASFLAGS        += -PD "FreezeDevRel SETL {${FREEZE_DEV_REL}}" -PD "USE_SYNCLIB SETL {${USE_SYNCLIB}}" -PD "RISCOS_KERNEL SETL {TRUE}"
CFLAGS         += -ff -APCS 3/32bit/nofp/noswst -DRISCOS_KERNEL
CUSTOMROM       = custom
CUSTOMEXP       = custom
CUSTOMSA        = custom
EXPORTS         = ${EXP_HDR}.AMBControl \
                  ${EXP_HDR}.DBellDevice \
                  ${EXP_HDR}.EnvNumbers \
                  ${EXP_HDR}.HALDevice \
                  ${EXP_HDR}.HALEntries \
                  ${EXP_HDR}.ModHand \
                  ${EXP_HDR}.OSEntries \
                  ${EXP_HDR}.OSMem \
                  ${EXP_HDR}.OSMisc \
                  ${EXP_HDR}.OSRSI6 \
                  ${EXP_HDR}.PL310 \
                  ${EXP_HDR}.PublicWS \
                  ${EXP_HDR}.RISCOS \
                  ${EXP_HDR}.Variables \
                  ${EXP_HDR}.VduExt \
                  ${EXP_HDR}.VIDCList \
                  ${EXP_HDR}.VideoDevice \
                  ${C_EXP_HDR}.HALDevice \
                  ${C_EXP_HDR}.HALEntries \
                  ${C_EXP_HDR}.ModHand \
                  ${C_EXP_HDR}.OSEntries \
                  ${C_EXP_HDR}.OSMem \
                  ${C_EXP_HDR}.OSMisc \
                  ${C_EXP_HDR}.OSRSI6 \
                  ${C_EXP_HDR}.RISCOS \
                  ${C_EXP_HDR}.Variables \
                  ${C_EXP_HDR}.VduExt \
                  ${C_EXP_HDR}.VIDCList
ifeq (${USE_SYNCLIB},TRUE)
CFLAGS	       += -DUSE_SYNCLIB
LIBS            = ${SYNCLIB}k
endif

#
# AbortTrap:
#
VPATH += aborttrap
OBJS += aborttrap atarm atcontext atinstr aterrors atmem

DECGEN = <Tools$Dir>.Misc.decgen.decgen

# Work out which instructions to include support for; this is just to reduce
# code size, and doesn't affect the handling of the instructions
# Note that FPA is only included in IOMD builds
ABORTTRAP_ACTIONS_ARM = ARMv3 ARMv4 ARMv5TE ARMv6 ARMv6K ARMv6T2 ARMv8 VFP ASIMD

ABORTTRAP_ENCODINGS_ARM = Build:decgen.encodings.ARMv7 \
                          Build:decgen.encodings.ARMv7_ASIMD \
                          Build:decgen.encodings.ARMv7_VFP \
                          Build:decgen.encodings.ARMv8_AArch32 \
                          Build:decgen.encodings.FPA

ifneq (,$(findstring $(MACHINE),IOMD Lazarus))
ABORTTRAP_ACTIONS_ARM = ARMv3 ARMv4 FPA
endif
ifneq (,$(findstring $(MACHINE),Tungsten))
ABORTTRAP_ACTIONS_ARM = ARMv3 ARMv4 ARMv5TE
endif
ifneq (,$(findstring $(MACHINE),ARM11ZF))
ABORTTRAP_ACTIONS_ARM = ARMv3 ARMv4 ARMv5TE ARMv6 ARMv6K VFP
endif
ifneq (,$(findstring $(MACHINE),CortexA7 CortexA8 CortexA9))
ABORTTRAP_ACTIONS_ARM = ARMv3 ARMv4 ARMv5TE ARMv6 ARMv6K ARMv6T2 VFP ASIMD
endif

ABORTTRAP_ACTIONS = ${ABORTTRAP_ACTIONS_ARM}

# Have decgen use a pre-warmed cache file to speed up decoder generation. Please
# remember to submit the files if they change!
ABORTTRAP_CACHE = $(subst $(subst x,,x x),_,$(strip ${ABORTTRAP_ACTIONS_ARM}))

CFLAGS += $(addprefix -DABORTTRAP_,${ABORTTRAP_ACTIONS})

ifeq (${Machine},Linux)
CFLAGS += -DMACHINE_LINUX
OBJS += LinuxC
LIBS += ${PVLIBS}
endif

include StdTools
include AAsmModule
include StdRules
ifeq (${USE_SYNCLIB},TRUE)
include AppLibs
endif

# Override this to "TRUE" in the components file if
# you want an odd-numbered (development) build to be
# a 'freezable' build - e.g. with no ROM debug output
FREEZE_DEV_REL ?= FALSE

ROM_OBJECTS = $(addsuffix .o,${OBJS})

#
# AbortTrap:
#

clean ::
        @IfThere aborttrap.c.atarm     Then delete aborttrap.c.atarm

ABORTTRAP_ARM_DEPS = $(addprefix aborttrap.actions.,${ABORTTRAP_ACTIONS_ARM})

aborttrap.c.atarm: $(ABORTTRAP_ARM_DEPS) aborttrap.c.atpre $(ABORTTRAP_ENCODINGS_ARM)
        $(DECGEN) -bits=32 -e "-DCDP={ne(coproc,1)}" "-DLDC_STC={ne(coproc,1)}{ne(coproc,2)}" "-DMRC_MCR={ne(coproc,1)}" -DVFP1=(cond:4) "-DVFP2={ne(cond,15)}" -DAS1(X)=1111001[X] -DAS2=11110100 -DAS3=(cond:4)1110 "-DAS4={ne(cond,15)}" "-DCC={ne(cond,15)}" $(ABORTTRAP_ENCODINGS_ARM) -valid -a $(addprefix aborttrap/actions/,${ABORTTRAP_ACTIONS_ARM}) -default=DEFAULT -o aborttrap/atarm.c -name=aborttrap_arm -pre aborttrap/atpre.c -updatecache aborttrap/cache/${ABORTTRAP_CACHE}

o.atarm: aborttrap.c.atarm
	${CC} ${CFLAGS} -o $@ aborttrap.c.atarm

od.atarm: aborttrap.c.atarm
	${CC} $(filter-out ${C_NO_FNAMES},${CFLAGS}) ${CDFLAGS} -o $@ aborttrap.c.atarm

#
# Custom ROM:
#
rom_custom: ${KERNEL_MODULE}
	@${ECHO} ${COMPONENT}: rom module built

install_rom_custom: ${KERNEL_MODULE}
	${CP} ${KERNEL_MODULE} ${INSTDIR}${SEP}${TARGET} ${CPFLAGS}
	${CP} ${KERNEL_MODULE}_gpa ${INSTDIR}${SEP}${TARGET}_gpa ${CPFLAGS}
	@${ECHO} ${COMPONENT}: rom module installed

inst_dirs:
	${MKDIR} ${EXP_HDR}
	${MKDIR} ${C_EXP_HDR}

install_custom: ${EXPORTS} inst_dirs
	@${ECHO} ${COMPONENT}: header files installed

# Linux: -RW-base must equal RWBase in hdr.KernelWS (RMAAddress - 4MB = &20800000 + 60MB)
${KERNEL_MODULE}: ${ROM_OBJECTS} ${DIRS} ${LIBS} kstrip
	${MKDIR} bin
ifeq (${Machine},Linux)
	SetEval KernelBase &20800000 + ( HALSize LEFT ( LEN HALSize - 1 ) ) * 1024
	Do ${LD} -aif -base <KernelBase> -RW-base 0x24400000 -bin -d -o ${KERNEL_MODULE}_aif ${ROM_OBJECTS} ${LIBS}
	Do kstrip ${KERNEL_MODULE}_aif ${KERNEL_MODULE}
else
	SetEval KernelBase "4" + STR ( 227858432 + ( HALSize LEFT ( LEN HALSize - 1 ) ) * 1024 )
	Do ${LD} -aif -base <KernelBase> -RW-base 0xff000000 -bin -d -o ${KERNEL_MODULE}_aif ${ROM_OBJECTS} ${LIBS}
	Do kstrip ${KERNEL_MODULE}_aif ${KERNEL_MODULE}
endif
	${TOGPA} -s ${KERNEL_MODULE}_aif ${KERNEL_MODULE}_gpa

GetAll.o: ${TOKHELPSRC} hdr.AutoVersion

hdr.AutoVersion:
	Echo <32>END { > hdr.AutoVersion }
#
# Internal C header
#
o.LinuxC: h.Kernel

h.Kernel: o.GenHeader
	${LD} -bin -o $@ o.GenHeader
	SetType $@ FFF

#
# Custom exports:
#
export_hdrs_custom: ${EXPORTS}
	@${ECHO} ${COMPONENT}: header export complete

export_libs_custom:
        @${ECHO} ${COMPONENT}: no exported libraries

${EXP_HDR}.AMBControl: hdr.AMBControl
	${CP} hdr.AMBControl $@ ${CPFLAGS}


${EXP_HDR}.EnvNumbers: hdr.EnvNumbers
	${CP} hdr.EnvNumbers $@ ${CPFLAGS}

${EXP_HDR}.DBellDevice: hdr.DBellDevice
	${CP} hdr.DBellDevice $@ ${CPFLAGS}

${EXP_HDR}.HALDevice: hdr.HALDevice
	${CP} hdr.HALDevice $@ ${CPFLAGS}

${EXP_HDR}.HALEntries: hdr.HALEntries
	${CP} hdr.HALEntries $@ ${CPFLAGS}

${EXP_HDR}.ModHand: hdr.ModHand
	${CP} hdr.ModHand $@ ${CPFLAGS}

${EXP_HDR}.OSEntries: hdr.OSEntries
	${CP} hdr.OSEntries $@ ${CPFLAGS}

${EXP_HDR}.OSMem: hdr.OSMem
	${CP} hdr.OSMem $@ ${CPFLAGS}

${EXP_HDR}.OSMisc: hdr.OSMisc
	${CP} hdr.OSMisc $@ ${CPFLAGS}

${EXP_HDR}.OSRSI6: hdr.OSRSI6
	${CP} hdr.OSRSI6 $@ ${CPFLAGS}

${EXP_HDR}.PL310: hdr.PL310
	${CP} hdr.PL310 $@ ${CPFLAGS}

${EXP_HDR}.PublicWS: hdr.PublicWS
	${CP} hdr.PublicWS $@ ${CPFLAGS}

${EXP_HDR}.RISCOS: hdr.RISCOS
	${CP} hdr.RISCOS $@ ${CPFLAGS}

${EXP_HDR}.Variables: hdr.Variables
	${CP} hdr.Variables $@ ${CPFLAGS}

${EXP_HDR}.VduExt: hdr.VduExt
	${CP} hdr.VduExt $@ ${CPFLAGS}

${EXP_HDR}.VIDCList: hdr.VIDCList
	${CP} hdr.VIDCList $@ ${CPFLAGS}

${EXP_HDR}.VideoDevice: hdr.VideoDevice
	${CP} hdr.VideoDevice $@ ${CPFLAGS}

${C_EXP_HDR}.HALDevice: Global.h.HALDevice h.HALDevice
	${FAPPEND} $@ h.HALDevice Global.h.HALDevice

${C_EXP_HDR}.HALEntries: hdr.HALEntries
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.HALEntries $@

${C_EXP_HDR}.ModHand: hdr.ModHand
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.ModHand $@

${C_EXP_HDR}.OSEntries: Global.h.OSEntries h.OSEntries
	${FAPPEND} $@ h.OSEntries Global.h.OSEntries

${C_EXP_HDR}.OSMem: hdr.OSMem
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.OSMem $@

${C_EXP_HDR}.OSMisc: hdr.OSMisc
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.OSMisc $@

${C_EXP_HDR}.OSRSI6: hdr.OSRSI6
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.OSRSI6 $@

${C_EXP_HDR}.RISCOS: hdr.RISCOS
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.RISCOS $@

${C_EXP_HDR}.Variables: hdr.Variables
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.Variables $@

${C_EXP_HDR}.VduExt: hdr.VduExt
	${MKDIR} ${C_EXP_HDR}
	${HDR2H} hdr.VduExt $@

${C_EXP_HDR}.VIDCList: Global.h.VIDCList h.VIDCList
	${FAPPEND} $@ h.VIDCList Global.h.VIDCList

Global.h.HALDevice: hdr.HALDevice
	${MKDIR} Global.h
	${HDR2H} hdr.HALDevice $@

Global.h.OSEntries: hdr.OSEntries
	${MKDIR} Global.h
	${HDR2H} hdr.OSEntries $@

Global.h.VIDCList: hdr.VIDCList
	${MKDIR} Global.h
	${HDR2H} hdr.VIDCList $@

clean::
	${XWIPE} Global ${WFLAGS}
	${XWIPE} bin    ${WFLAGS}
	${RM} kstrip

kstrip: kstrip.c
	${MAKE} -f kstrip/mk COMPONENT=kstrip THROWBACK=${THROWBACK}

# Dynamic dependencies:
