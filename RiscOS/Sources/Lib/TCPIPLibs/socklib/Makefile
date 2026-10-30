# Copyright 1995 Acorn Computers Ltd
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
# Makefile for socklib

COMPONENT = socklib
LIBRARY   = socklib5
ifeq ($(filter install%,${MAKECMDGOALS}),)
EXPDIR    = ${LIBDIR}${SEP}TCPIPLibs
else
EXPDIR    = ${INSTDIR}${SEP}TCPIPLibs
endif
CDEFINES  = -DINET
CFLAGS    = -pcc
CAPPFLAGS = ${C_FNAMES}
CMODFLAGS = ${C_NO_STKCHK}
SOURCES_TO_SYMLINK = mkveneers,102 Prototypes hdr/InetSWIs

OBJS = accept5 bind connect copyerror getpeerna5 getsockna5 getsockopt \
       getstabsiz listen recv5 recvfrom5 recvmsg5 select send5 sendmsg5 \
       sendto setsockopt shutdown sockclose socket socketstat sockioctl \
       sockread sockreadv sockwrite sockwritev sendtosm sysctl \
       kvm_nlist sockver \
       _inet_err

include CLibrary

CINCLUDES := ${TCPIPINC} ${CINCLUDES} # TCPIPLibs has to come first

veneers: mkveneers${SUFFIX_PERL} Prototypes ${DIRS}
	${PERL}  mkveneers${SUFFIX_PERL} Prototypes
	${TOUCH} veneers

.SUFFIXES: .sn .sz

ifeq (,${MAKE_VERSION})

# RISC OS / amu case

${DIRS} ::
	${MKDIR} sn
	${MKDIR} sz

${APP_OBJS_}: veneers
${MOD_OBJS_}: veneers

.sn.o:;         ${AS} ${ASFLAGS} -o $@ $<
.sz.oz:;        ${AS} ${ASFLAGS} -o $@ $<

else

# Posix / gmake case

$(addsuffix .sn,${APP_OBJS}): veneers
$(addsuffix .sz,${MOD_OBJS}): veneers

.sn.o:;         ${AS} ${ASFLAGS} -depend $(subst .o,.d,$@) -o $@ $<
.sz.oz:;        ${AS} ${ASFLAGS} -depend $(subst .oz,.dz,$@) -o $@ $<

endif

clean${MAKE_VERSION}::
	${WIPE} sn ${WFLAGS}
	${WIPE} sz ${WFLAGS}
	${RM} veneers

# Dynamic dependencies:
