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
# Makefile for Territory Manager
#

COMPONENT    = TerritoryManager
OBJS         = TerrMgr Daylight MsgCode Subcall SWIs Time
CINCLUDES    = ${TCPIPINC}
CMHGFILE     = TerritoryMgrHdr
CMHGDEPENDS  = TerrMgr Daylight MsgCode SWIs
ROMCDEFINES  = -DROM
LIBS         = ${ASMUTILS}
HDRS         =
ASMHDRS      = Territory
ASMCHDRS     = Territory
RESFSDIR     = ${RESDIR}${SEP}TerrMgr
INSTRES_FILES = Prompt
PROMPT       = Resources${SEP}Prompt${SUFFIX_DRAWFILE}
PROMPTSRC    = Resources${SEP}PromptSrc${SUFFIX_DRAWFILE}
INSTRES_DEPENDS = ${PROMPT}

include CModule

${PROMPT}: ${PROMPTSRC}
	${SQUASH} ${PROMPTSRC} $@

clean::
	${RM} ${PROMPT}

# Dynamic dependencies:
