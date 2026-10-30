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
# Makefile for wimplib
#

COMPONENT = wimplib
LIBRARIES = ${LIBRARY}
EXPDIR   ?= ${LIBDIR}${SEP}tboxlibs
OBJS      =  \
  waddmsg    \
  wbasesp    \
  wblockcpy  \
  wclosedown \
  wclosete   \
  wclosew    \
  wcommw     \
  wcreatei   \
  wcreatem   \
  wcreatesm  \
  wcreatew   \
  wdecodem   \
  wdeletei   \
  wdeletew   \
  wdragbox   \
  wforcere   \
  wforcere2  \
  wgetcarp   \
  wgeticst   \
  wgetmenu   \
  wgetpoi    \
  wgetrec    \
  wgetwinf   \
  wgetwinfni \
  wgetwino   \
  wgetwst    \
  wgetwst2   \
  winit      \
  wloadte    \
  wopente    \
  wopenw     \
  wopenw2    \
  wplotic    \
  wpoll      \
  wpollidle  \
  wprocessk  \
  wreadpal   \
  wredraww   \
  wremmsg    \
  wreporte   \
  wresizeic  \
  wsendm     \
  wsetcarp   \
  wsetcol    \
  wsetcolm   \
  wsetext    \
  wsetfon    \
  wseticst   \
  wsetmode   \
  wsetpal    \
  wsetpos    \
  wslotsize  \
  wspriteop  \
  wstartt    \
  wsysinfo   \
  wtextcol   \
  wtextop    \
  wtransbl   \
  wupdatew   \
  wwhichi    \
  !!version
SYMLINK_DEPEND = VersionASM
VersionASM:
	ln -s ../VersionASM $@ # Extend the link farm to the copy 1 level up

include CLibrary

# Dynamic dependencies:
