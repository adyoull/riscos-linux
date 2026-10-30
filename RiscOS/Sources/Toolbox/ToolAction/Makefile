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
# Makefile for ToolAction module
#

COMPONENT     = ToolAction
OBJS          = main toolact
CMHGDEPENDS   = main
ROMCDEFINES   = -DROM
CINCLUDES     = -I${CEXPORTDIR} ${OSINC} ${TBOXINC}
CUSTOMEXP     = custom
LIBS          = ${TBOXINTLIB}
SOURCES_TO_SYMLINK = Documents/def/ToolAction

include CModule

ifeq (,${MAKE_VERSION})

# RISC OS / amu case

DEF_TOOLACTION = Documents.def.ToolAction
PREFIX_HEADER  = .h
PREFIX_LIBRARY = .o
VIA_FILE       = -via ViaFile

clean::
	${RM} h.ToolAction
	${RM} hdr.ToolAction
	${RM} ViaFile
	${XWIPE} DefMod ${WFLAGS}

else

# Posix / gmake case

DEF_TOOLACTION = Documents/ToolAction.def
SUFFIX_LIBRARY = .a
VIA_FILE       = DefMod/*.o

endif

export_hdrs_custom: h.ToolAction hdr.ToolAction
	${MKDIR} ${CEXPORTDIR}${PREFIX_HEADER}
	${MKDIR} ${EXPORTDIR}
	${CP} h.ToolAction   ${CEXPORTDIR}${PREFIX_HEADER}${SEP}ToolAction${SUFFIX_HEADER} ${CPFLAGS}
	${CP} hdr.ToolAction ${EXPORTDIR}${SEP}ToolAction ${CPFLAGS}
	@${ECHO} ${COMPONENT}: header export complete

export_libs_custom: o.ToolAction
	${MKDIR} ${CEXPORTDIR}${PREFIX_LIBRARY}
	${CP} o.ToolAction   ${CEXPORTDIR}${PREFIX_LIBRARY}${SEP}ToolAction${SUFFIX_LIBRARY} ${CPFLAGS}
	@${ECHO} ${COMPONENT}: library export complete

h.ToolAction: ${DEF_TOOLACTION}
	${DEFMOD} -h > h.ToolAction < ${DEF_TOOLACTION}

hdr.ToolAction: ${DEF_TOOLACTION}
	${MKDIR} hdr
	${DEFMOD} -hdr > hdr.ToolAction < ${DEF_TOOLACTION}

o.ToolAction: ${DEF_TOOLACTION}
	${MKDIR} o
	${DEFMOD} -l -o DefMod < ${DEF_TOOLACTION}
	${AR} ${ARFLAGS} o.ToolAction ${VIA_FILE}

# Dynamic dependencies:
