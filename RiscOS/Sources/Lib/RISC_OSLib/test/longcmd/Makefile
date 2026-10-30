# Copyright 2023 RISC OS Open Ltd
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

COMPONENT ?= LongCmd

ifeq (${COMPONENT},LongCmd)
# LongCmd is a C application
OBJS = longcmd
include CApp
else
ifeq (${COMPONENT},LongCmdM)
# LongCmdM is a C module
OBJS = longcmdm
CMHGFILE = modhdr
CMHGDEPENDS = longcmdm
LIBS = ${ASMUTILS}
CFLAGS += ${C_STKCHK}
RES_OBJ =
include CModule
else
# LongCmdU is a C utility, using OSLib
OBJS = longcmdu mainu
INCLUDE_OSLIB ?= -IOS:
CINCLUDES = ${INCLUDE_OSLIB}
include CUtil
UTIL_LIBS = ${OSLIB}
LDFLAGS = -util
endif
endif

# Function names for debugging
CFLAGS += ${C_FNAMES}

# Extra dirs needed
${DIRS} ::
	${MKDIR} h
	${MKDIR} rm

# Dynamic dependencies:
