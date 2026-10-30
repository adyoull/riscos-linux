# Copyright 1997 Acorn Computers Ltd
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
# Makefile for toolboxlib
#

COMPONENT = toolboxlib
LIBRARIES = ${LIBRARY}
EXPDIR   ?= ${LIBDIR}${SEP}tboxlibs
HDRS      =  \
  colourdbox \
  colourmenu \
  dcs        \
  fileinfo   \
  fontdbox   \
  fontmenu   \
  gadgets    \
  iconbar    \
  menu       \
  printdbox  \
  proginfo   \
  quit       \
  saveas     \
  scale      \
  Scrollbar  \
  ScrollList \
  tabs       \
  TextArea   \
  toolbox    \
  treeview   \
  wimp       \
  window
CINCLUDES = -I.
CAPPFLAGS = ${C_NO_STKCHK}
CUSTOMLIB = custom
SOURCES_TO_SYMLINK = $(wildcard sources/*/c/*)

include CLibrary

ifeq (,${MAKE_VERSION})

# RISC OS / amu case

${DIRS}::
	${MKDIR} sources.ActionButt.o
	${MKDIR} sources.button.o
	${MKDIR} sources.colourdbox.o
	${MKDIR} sources.colourmenu.o
	${MKDIR} sources.dcs.o
	${MKDIR} sources.displayfie.o
	${MKDIR} sources.draggable.o
	${MKDIR} sources.fileinfo.o
	${MKDIR} sources.fontdbox.o
	${MKDIR} sources.fontmenu.o
	${MKDIR} sources.Gadgets.o
	${MKDIR} sources.iconbar.o
	${MKDIR} sources.menu.o
	${MKDIR} sources.numberrang.o
	${MKDIR} sources.optionbutt.o
	${MKDIR} sources.popup.o
	${MKDIR} sources.printdbox.o
	${MKDIR} sources.proginfo.o
	${MKDIR} sources.quit.o
	${MKDIR} sources.radiobutto.o
	${MKDIR} sources.saveas.o
	${MKDIR} sources.scale.o
	${MKDIR} sources.scrollbar.o
	${MKDIR} sources.scrolllist.o
	${MKDIR} sources.slider.o
	${MKDIR} sources.stringset.o
	${MKDIR} sources.tabs.o
	${MKDIR} sources.textarea.o
	${MKDIR} sources.toolbox.o
	${MKDIR} sources.treeview.o
	${MKDIR} sources.version.o
	${MKDIR} sources.window.o
	${MKDIR} sources.writable.o

clean::
	${WIPE} sources.ActionButt.o ${WFLAGS}
	${WIPE} sources.button.o     ${WFLAGS}
	${WIPE} sources.colourdbox.o ${WFLAGS}
	${WIPE} sources.colourmenu.o ${WFLAGS}
	${WIPE} sources.dcs.o        ${WFLAGS}
	${WIPE} sources.displayfie.o ${WFLAGS}
	${WIPE} sources.draggable.o  ${WFLAGS}
	${WIPE} sources.fileinfo.o   ${WFLAGS}
	${WIPE} sources.fontdbox.o   ${WFLAGS}
	${WIPE} sources.fontmenu.o   ${WFLAGS}
	${WIPE} sources.Gadgets.o    ${WFLAGS}
	${WIPE} sources.iconbar.o    ${WFLAGS}
	${WIPE} sources.menu.o       ${WFLAGS}
	${WIPE} sources.numberrang.o ${WFLAGS}
	${WIPE} sources.optionbutt.o ${WFLAGS}
	${WIPE} sources.popup.o      ${WFLAGS}
	${WIPE} sources.printdbox.o  ${WFLAGS}
	${WIPE} sources.proginfo.o   ${WFLAGS}
	${WIPE} sources.quit.o       ${WFLAGS}
	${WIPE} sources.radiobutto.o ${WFLAGS}
	${WIPE} sources.saveas.o     ${WFLAGS}
	${WIPE} sources.scale.o      ${WFLAGS}
	${WIPE} sources.scrollbar.o  ${WFLAGS}
	${WIPE} sources.scrolllist.o ${WFLAGS}
	${WIPE} sources.slider.o     ${WFLAGS}
	${WIPE} sources.stringset.o  ${WFLAGS}
	${WIPE} sources.tabs.o       ${WFLAGS}
	${WIPE} sources.textarea.o   ${WFLAGS}
	${WIPE} sources.toolbox.o    ${WFLAGS}
	${WIPE} sources.treeview.o   ${WFLAGS}
	${WIPE} sources.version.o    ${WFLAGS}
	${WIPE} sources.window.o     ${WFLAGS}
	${WIPE} sources.writable.o   ${WFLAGS}

include ObjFileA-Z # Far quicker than a wildcarded directory enumeration

${LIBEXT}.${LIBRARY}: ${ObjectsA-M} ${ObjectsN-S} ${ObjectsT-Z} LibVersion ${DIRS}
	${AR} ${ARFLAGS} $@ -v ViaFileA-M -v ViaFileN-S -v ViaFileT-Z

LibVersion:
	${CP} ^.VersionNum ${EXPDIR}.LibVersion ${CPFLAGS}

else

# Posix / gmake case

ObjectsA-Z := $(subst .c,.o,$(wildcard sources/*/*.c))

${LIBRARY}.${LIBEXT}: ${DIRS} LibVersion ${ObjectsA-Z}
	${AR} ${ARFLAGS} $@ ${ObjectsA-Z}

LibVersion:
	${CP} ../../VersionNum ${EXPDIR}/LibVersion ${CPFLAGS}

endif

# Dynamic dependencies:
