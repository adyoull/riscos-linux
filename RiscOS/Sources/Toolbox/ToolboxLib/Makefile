# Copyright 2019 RISC OS Open Ltd
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
# Makefile for ToolboxLib
#

COMPONENT = tboxlibs

include StdTools

clean:
	${MAKE} -C toolboxlib TARGET=toolboxlib clean
	${MAKE} -C wimplib    TARGET=wimplib    clean
	${MAKE} -C flexlib    TARGET=flexlib    clean
	${MAKE} -C eventlib   TARGET=eventlib   clean
	${MAKE} -C renderlib  TARGET=renderlib  clean
	@${STRIPDEPEND} toolboxlib${SEP}Makefile
	@${STRIPDEPEND} wimplib${SEP}Makefile
	@${STRIPDEPEND} flexlib${SEP}Makefile
	@${STRIPDEPEND} eventlib${SEP}Makefile
	@${STRIPDEPEND} renderlib${SEP}Makefile
	@${ECHO} ${COMPONENT}: cleaned

export: export_${PHASE}
	@${NOP}

export_hdrs:
	${MAKE} -C toolboxlib TARGET=toolboxlib export PHASE=hdrs
	${MAKE} -C wimplib    TARGET=wimplib    export PHASE=hdrs
	${MAKE} -C flexlib    TARGET=flexlib    export PHASE=hdrs
	${MAKE} -C eventlib   TARGET=eventlib   export PHASE=hdrs
	${MAKE} -C renderlib  TARGET=renderlib  export PHASE=hdrs
	@${ECHO} ${COMPONENT}: header export complete

export_libs:
	${MAKE} -C toolboxlib TARGET=toolboxlib export PHASE=libs
	${MAKE} -C wimplib    TARGET=wimplib    export PHASE=libs
	${MAKE} -C flexlib    TARGET=flexlib    export PHASE=libs
	${MAKE} -C eventlib   TARGET=eventlib   export PHASE=libs
	${MAKE} -C renderlib  TARGET=renderlib  export PHASE=libs
	@${ECHO} ${COMPONENT}: library export complete

install:
	${MAKE} -C toolboxlib TARGET=toolboxlib install EXPDIR=${INSTDIR}${SEP}tboxlibs
	${MAKE} -C wimplib    TARGET=wimplib    install EXPDIR=${INSTDIR}${SEP}tboxlibs
	${MAKE} -C flexlib    TARGET=flexlib    install EXPDIR=${INSTDIR}${SEP}tboxlibs
	${MAKE} -C eventlib   TARGET=eventlib   install EXPDIR=${INSTDIR}${SEP}tboxlibs
	${MAKE} -C renderlib  TARGET=renderlib  install EXPDIR=${INSTDIR}${SEP}tboxlibs
