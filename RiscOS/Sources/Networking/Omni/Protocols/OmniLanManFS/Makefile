# Copyright 1998 Acorn Computers Ltd
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
# Makefile for LanManFS
#

COMPONENT    = LanManFS
OBJS         = Xlate Transact Stats SMB SMB2 SMB2Xport SMB2Unicode RPC \
               Printers Omni NBIP NameCache Cache Logon LanMan \
               CoreFn Workspace buflib Attr Interface Auth md5c md4c
CINCLUDES    = ${TCPIPINC} ${MBEDTLSINC}
HDRS         =
CMHGFILE     = LanMan_MH
CMHGDEPENDS  = LanMan Logon NBIP Omni CoreFn
LIBS         = ${SPNEGOLIB} ${KDFLIB} ${MBEDTLSLIB} ${SRHEAPLIB}
LIBS        += ${NET5LIBS} ${ASMUTILS}
RES_PATH     = ThirdParty.OmniClient
CDEFINES     = -DRISCOS -DMBEDTLS_CONFIG_FILE="\"ro_config.h\"" ${OPTIONS}
CFLAGS       = ${C_NOWARN_NON_ANSI_INCLUDES}
CDFLAGS      = -DDEBUG -DDEBUGLIB
ROMCDEFINES  = -DROM
CMHGDEFINES  = ${OPTIONS}
ifeq ("${CMDHELP}","None")
CMHGDEFINES += -DNO_INTERNATIONAL_HELP
endif
# The per-mount cache is built in unless CACHE=FALSE, which compiles Cache.c to functions
# that do nothing, so every mount runs uncached and the rest of the filing system can be
# debugged with the cache out of the picture.
ifneq ("${CACHE}","FALSE")
CDEFINES    += -DLM_CACHE
endif
# Carry our own ThirdParty resources, don't put them in the Messages module
CUSTOMRES    = no
RESDIR       = ${MERGEDRDIR}
OBJS        += ${RES_OBJ}
INSTRES_FILES = ROM.Sprites
INSTRES_VERSION = Messages

include CModule

# Dynamic dependencies:
