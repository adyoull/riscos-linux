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
# Makefile for internal tboxlib
#

COMPONENT ?= tboxlib
TARGET     = tboxlibint
LIBRARY    = tboxlib
LIBRARYZM  = ${LIBRARY}m
LIBRARYDZM = ${LIBRARY}dm
LIBRARIES  = ${LIBRARY} ${LIBRARYZM} ${LIBRARYD} ${LIBRARYDZM}
VPATH      = objects
EXPORTDIR ?= <export$dir>
OBJHDRS    = \
  colourdbox \
  colourmenu \
  DCS        \
  fileinfo   \
  fontdbox   \
  fontmenu   \
  gadgets    \
  generic    \
  iconbar    \
  menu       \
  printdbox  \
  proginfo   \
  Quit       \
  saveas     \
  scale      \
  toolbox    \
  window
HDRS       = \
  ${OBJHDRS} \
  colpicker  \
  const      \
  debug      \
  macros     \
  mem        \
  messages   \
  objmodule  \
  objsupport \
  os         \
  rmensure   \
  slist      \
  string32   \
  style      \
  toolboxmem \
  twimp
ASMHDRS    = \
  Toolbox    \
  Window
CFLAGS    += ${C_NOWARN_ASSIGNMENT_AS_CONDITION} ${C_NOWARN_NON_ANSI_INCLUDES}
CAPPFLAGS  = ${C_STKCHK}
CMODFLAGS  = ${C_NO_STKCHK}
OBJS       = mem messages string32 rmensure objmodule \
             objsupport slist toolboxmem
DBG_OBJS   = ${OBJS} debug
SOURCES_TO_SYMLINK = $(wildcard hdr/*) $(wildcard objects/h/*)

# Static rule to place ASMHDRS into Hdr/Interface rather than Lib/tboxlibint
.SUFFIXES: .expinterface .hdr
.hdr.expinterface:; ${CP} $< ${EXPORTDIR}${SEP}$* ${CPFLAGS}

ifeq (,${MAKE_VERSION})

# RISC OS / amu case

create_expobj_dir:
	${MKDIR} ${EXPDIR}.objects.h

EXPORTS    = ${EXPORTING_HDRS} create_expobj_dir $(addprefix expinterface.,${ASMHDRS})

else

# Posix / gmake case

create_expobj_dir:
	${MKDIR} ${EXPDIR}/objects

EXPORTS    = $(addsuffix .expinterface,${ASMHDRS}) create_expobj_dir ${EXPORTING_HDRS}

endif

include CLibrary

# Dynamic dependencies:
