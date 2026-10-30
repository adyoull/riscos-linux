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
# Makefile for rpclib

COMPONENT = rpclib
LIBRARY   = rpclib5
LIBRARIES = ${LIBRARY} ${LIBRARYZM} rpc_data rpc_datazm
ifeq ($(filter install%,${MAKECMDGOALS}),)
EXPDIR    = ${LIBDIR}${SEP}TCPIPLibs${SEP}rpc
else
EXPDIR    = ${INSTDIR}${SEP}TCPIPLibs${SEP}rpc
endif
CINCLUDES = ${TCPIPINC}
CDEFINES  = -Darm -DRISCOS -DSUPPORT_TCP
CFLAGS    = -pcc
CAPPFLAGS = ${C_FNAMES}
CMODFLAGS = ${C_NO_STKCHK}

HDRS = auth auth_unix clnt netdb pmap_clnt pmap_prot pmap_rmt rpc rpc_msg \
       svc svc_auth types xdr

OBJS = auth_none auth_unix authunix_prot bindresvport clnt_generic \
       clnt_perror clnt_raw clnt_simple clnt_udp rpc_dtablesize \
       get_myaddress getrpcent getrpcport pmap_clnt pmap_getmaps \
       pmap_getport pmap_prot pmap_prot2 pmap_rmt rpc_prot rpc_callmsg \
       xdr xdr_array xdr_mem clnt_tcp xdr_rec xdr_reference xdr_stdio \
       rpclibmem

include CLibrary

ifeq (,${MAKE_VERSION})

# RISC OS / amu case

o.rpc_data: o.rpc_commondata
	${TOUCH} $@

o.rpc_datazm: oz.rpc_commondata
	${TOUCH} $@

explib.rpc_data:
	${CP} o.rpc_commondata ${EXPDIR}.o.rpc_data ${CPFLAGS}

explib.rpc_datazm:
	${CP} oz.rpc_commondata ${EXPDIR}.o.rpc_datazm ${CPFLAGS}

else

# Posix / gmake case

rpc_data.a: rpc_commondata.o
	${TOUCH} $@

rpc_datazm.a: rpc_commondata.oz
	${TOUCH} $@

rpc_data.explib:
	${CP} rpc_commondata.o ${EXPDIR}/rpc_data.o ${CPFLAGS}

rpc_datazm.explib:
	${CP} rpc_commondata.oz ${EXPDIR}/rpc_datazm.o ${CPFLAGS}

endif

# Dynamic dependencies:
