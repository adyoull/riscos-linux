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
# Makefile for Filer Action

COMPONENT    ?= Filer_Action
TARGET       ?= FilerAct
HDRS          =
ASMHDRS       = FilerAct
INSTRES_FILES = Templates
CMHGFILE      =
OBJS          = actionwind Initialise Buttons allerrs memmanage listfiles Chains dboxlong debug
CINCLUDES     = ${RINC}
RES_OBJ       =
SA_LIBS       = ${RSTUBS}
ROM_LIBS      = ${ROMSTUBS}
ROM_SYMS      = ${ABSSYM}
CFLAGS        = ${C_STKCHK}
MODULEWRAP    = yes

include CModule

# Dynamic dependencies:
