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
# Makefile for FSLock
#

COMPONENT = FSLock
HEADER1   = FSLock
CUSTOMROM = custom
CUSTOMSA  = custom
SOURCES_TO_SYMLINK = $(wildcard c/*)

include AAsmModule

rom_common: ${ROM_MODULE} scrambler
	${RUN}scrambler ${ROM_MODULE}

rom_custom: rom_common
	@${ECHO} ${COMPONENT}: rom module built & scrambled

install_rom_custom: rom_common
	${CP} ${ROM_MODULE} ${INSTDIR}${SEP}${TARGET} ${CPFLAGS}
	${CP} ${ROM_MODULE}_gpa ${INSTDIR}${SEP}${TARGET}_gpa ${CPFLAGS}
	@${ECHO} ${COMPONENT}: rom module installed

standalone_custom install_custom:
	@${ECHO} ${COMPONENT}: skipped as this component is only in ROM

clean::
	${MAKE} -f scrambler${EXT}mk TARGET=scrambler clean
	${STRIPDEPEND} scrambler${EXT}mk

scrambler: scrambler.c
	${MAKE} -f scrambler${EXT}mk TARGET=scrambler THROWBACK=${THROWBACK}

# Dynamic dependencies:
