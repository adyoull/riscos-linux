# Copyright 1995 Acorn Computers Ltd
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
# Makefile for inetlib

COMPONENT = inetlib
ifeq ($(filter install%,${MAKECMDGOALS}),)
EXPDIR    = ${LIBDIR}${SEP}TCPIPLibs
else
EXPDIR    = ${INSTDIR}${SEP}TCPIPLibs
endif
CINCLUDES = ${TCPIPINC}
CDEFINES  = -DINET
CFLAGS    = ${C_NOWARN_ASSIGNMENT_AS_CONDITION} ${C_NOWARN_NON_ANSI_INCLUDES}
CAPPFLAGS = ${C_FNAMES}
CMODFLAGS = ${C_NO_STKCHK}

OBJS = ether_addr getadinfo gethent gethnad getifaddrs getipnode getnainfo \
       getnbyad getnbyna getnent getprent getprnam getproto getsbyna \
       getsbypo getsent htonl if_indextoname if_nameindex if_nametoindex \
       in6addr inet_addr inet_lnaof inet_maddr inet_nof inet_ntoa \
       inet_nwork rresvport linkaddr iso_addr sysctlbyna \
       inet_nntop inet_npton inet_ntop inet_pton

include CLibrary

# Dynamic dependencies:
