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
# Makefile for Colour Translation module
#

COMPONENT = ColourTrans
TARGET    = Colours
ROM_SOURCE = Header.s
HEADER1   = ColourTran
INSTRES_FILES = Tables.4greys:Tables   Palettes.4greys:Palettes \
                Tables.8greys:Tables   Palettes.8greys:Palettes \
                Tables.4desktop:Tables Palettes.4desktop:Palettes \
                Tables.8desktop:Tables Palettes.8desktop:Palettes
INSTRES_DEPENDS = maketables
SOURCES_TO_SYMLINK = MkTables/c/maketables

include AAsmModule

clean::
	${XWIPE} Tables ${WFLAGS}
	${XWIPE} Palettes ${WFLAGS}
	${MAKE} -f MkTables${SEP}maketables${EXT}mk clean COMPONENT=maketables TARGET=maketables
	${STRIPDEPEND} MkTables${SEP}maketables${EXT}mk

#
# Static dependencies:
#
maketables: ${DIRS}
	${MAKE} -f MkTables${SEP}maketables${EXT}mk COMPONENT=maketables TARGET=maketables THROWBACK=${THROWBACK}
	${XWIPE} Palettes ${WFLAGS}
	${XWIPE} Tables ${WFLAGS}
	${MKDIR} Palettes
	${MKDIR} Tables
	${RUN}maketables
	${SQUASH} Tables${SEP}4greys${SUFFIX_DATA}
	${SQUASH} Tables${SEP}8greys${SUFFIX_DATA}
	${SQUASH} Tables${SEP}4desktop${SUFFIX_DATA}

# Dynamic dependencies:
