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
# Makefile for eventlib
#

COMPONENT = eventlib
LIBRARYZM = ${LIBRARY}m
EXPDIR   ?= ${LIBDIR}${SEP}tboxlibs
HDRS      = event
CINCLUDES = -I${LIBDIR}${SEP}tboxlibs
CAPPFLAGS = ${C_FNAMES}
OBJS      = \
  event     \
  tbevent   \
  wimpevent \
  wimpmsg   \
  !!version
SYMLINK_DEPEND = VersionASM
VersionASM:
	ln -s ../VersionASM $@ # Extend the link farm to the copy 1 level up

include CLibrary

# Dynamic dependencies:
