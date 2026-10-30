# Copyright (c) 2021, RISC OS Open Ltd
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met: 
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of RISC OS Open Ltd nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
# 
# Makefile for standalone AbortTrap implementation
#

DEBUG ?= FALSE

ifeq ($(DEBUG),TRUE)
CFLAGS += -DABORTTRAP_DEBUG
CMHGFLAGS += -DABORTTRAP_DEBUG
endif

USE_SYNCLIB    ?= TRUE

# It looks like there's a compiler bug that causes inlining to break things
# (FNldmia_exc test fails in assorted ways)
# https://www.riscosopen.org/tracker/tickets/517
CFLAGS += -DABORTTRAP_INLINE=0

COMPONENT       = AbortTrap
OBJS            = cmodule aborttrap atarm atcontext atinstr aterrors atmem
CMHGFILE        = modhead
ASFLAGS        += -PD "USE_SYNCLIB SETL {${USE_SYNCLIB}}"
ifeq (${USE_SYNCLIB},TRUE)
CFLAGS	       += -DUSE_SYNCLIB
LIBS            = ${SYNCLIB}
endif

CFLAGS         += -ff -wp -wc
CMHGDEFINES     = -DCOMPONENT=${COMPONENT}
CMHGDEPENDS     = cmodule

RES_OBJ = # fudge

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

ifneq (,$(findstring $(MACHINE),IOMD))
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

include StdTools
include ModStdRule
include ModuleLibs
include DbgRules
include CModule

dirs ::
	${MKDIR} h

clean ::
        @IfThere c.atarm     Then delete c.atarm

ABORTTRAP_ARM_DEPS = $(addprefix ^.^.^.aborttrap.actions.,${ABORTTRAP_ACTIONS_ARM})

c.atarm: $(ABORTTRAP_ARM_DEPS) c.atpre $(ABORTTRAP_ENCODINGS_ARM)
        $(DECGEN) -bits=32 -e "-DCDP={ne(coproc,1)}" "-DLDC_STC={ne(coproc,1)}{ne(coproc,2)}" "-DMRC_MCR={ne(coproc,1)}" -DVFP1=(cond:4) "-DVFP2={ne(cond,15)}" -DAS1(X)=1111001[X] -DAS2=11110100 -DAS3=(cond:4)1110 "-DAS4={ne(cond,15)}" "-DCC={ne(cond,15)}" $(ABORTTRAP_ENCODINGS_ARM) -valid -a $(addprefix ../../../aborttrap/actions/,${ABORTTRAP_ACTIONS_ARM}) -default=DEFAULT -o atarm.c -name=aborttrap_arm -pre atpre.c -updatecache ../../../aborttrap/cache/${ABORTTRAP_CACHE}

o.atarm: c.atarm
	${CC} ${CFLAGS} -o $@ c.atarm

od.atarm: c.atarm
	${CC} $(filter-out ${C_NO_FNAMES},${CFLAGS}) ${CDFLAGS} -o $@ c.atarm

# Dynamic dependencies:
