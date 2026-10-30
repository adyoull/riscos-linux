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
# Makefile for DOSFS
#

COMPONENT    = DOSFS
OBJS         = DOSFS DOSclusters DOSdirs DOSnaming DOSctl Helpers MsgTrans \
               OpsArgs OpsFind OpsFunc OpsFile OpsGetPut \
               Statics TIMEconv Accessors
CDEFINES     = -DNONFLOPPIES
ROMCDEFINES  = -DROM
RES_AREA     = dosfs_msgarea
CMHGDEFINES  = ${CDEFINES}
CMHGDEPENDS  = DOSFS DOSctl MsgTrans
ASMHDRS      = DOSFS
HDRS         =
LIBS         = ${ASMUTILS}

ifeq ("${CMDHELP}","None")
CMHGDEFINES += -DNO_INTERNATIONAL_HELP
endif

include CModule

CDFLAGS     += -DDEBUGLIB

# Dynamic dependencies:
