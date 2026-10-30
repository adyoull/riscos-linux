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
# Makefile for TCPIPLibs headers

COMPONENT = include
LIBRARIES =

ifeq ($(filter install%,${MAKECMDGOALS}),)
EXPDIR    = ${LIBDIR}${SEP}TCPIPLibs
else
EXPDIR    = ${INSTDIR}${SEP}TCPIPLibs
endif

EXPORTS = create_export_dirs ${EXPORTING_HDRS} export_docs create_export_dirs
VPATH = arpa machine net netccitt netinet netiso protocols sys

SOURCES_TO_SYMLINK = \
 $(wildcard arpa/h/*) \
 $(wildcard machine/h/*) \
 $(wildcard net/h/*) \
 $(wildcard netccitt/h/*) \
 $(wildcard netinet/h/*) \
 $(wildcard netiso/h/*) \
 $(wildcard protocols/h/*) \
 $(wildcard sys/h/*) \

SYMLINK_DEPEND = symlink_extra
symlink_extra:
	[ -L LibraryDoc ] || ln -s ../LibraryDoc .
	[ -L VersionNum ] || ln -s ../VersionNum .

HDRS  = bitstring dirent err errno ifaddrs kvm netdb \
        nlist paths pwd resolv riscos sgtty sysexits unistd utmp
# arpa/*
HDRS += ftp inet nameser telnet tftp
# machine/*
HDRS += ansi cpufunc endian in_cksum limits machine/param machine/proc \
        machine/signal stdarg machine/types
# net/*
HDRS += ethernet if if_arp if_dl if_llc if_types if_var netisr radix \
        raw_cb route
# netccitt/*
HDRS += x25
# netinet/*
HDRS += icmp_var if_ether igmp igmp_var in in_pcb in_systm in_var \
        ip ip_fw ip_icmp ip_mroute ip_var \
        sctp sctp_constants sctp_header sctp_uio \
        tcp tcp_debug tcp_fsm tcpip tcp_seq tcp_timer tcp_var \
        udp udp_var
# netiso/*
HDRS += iso iso_var
# protocols/*
HDRS += bootp dhcp routed
# sys/*
HDRS += acct buf callout capsicum cdefs clist conf dcistructs dir \
        sys/dirent domain sys/errno fcntl file filedesc filio ioccom \
        ioctl ioctl_compat ipc kernel linker_set malloc mbuf mount namei \
        sys/param _posix sys/proc protosw queue resource rtprio select \
        sem sys/signal signalvar socket socketvar sockio stat syscall \
        sysctl syslimits syslog systm termios time timeb times tty \
        ttychars ttycom ttydefaults ttydev sys/types ucred uio un \
        sys/unistd unpcb user vnode wait

include CLibrary

export_docs:
	${CP} ${PARENT}${SEP}LibraryDoc ${EXPDIR}${SEP}LibraryDoc ${CPFLAGS}
	${CP} ${PARENT}${SEP}VersionNum ${EXPDIR}${SEP}LibVersion ${CPFLAGS}

SUBDIR_HEADER := .h
SUBDIR_HEADER := ${SUBDIR_HEADER:${SUFFIX_HEADER}=}

create_export_dirs:
	${MKDIR} ${EXPDIR}${SEP}arpa${SUBDIR_HEADER}
	${MKDIR} ${EXPDIR}${SEP}machine${SUBDIR_HEADER}
	${MKDIR} ${EXPDIR}${SEP}net${SUBDIR_HEADER}
	${MKDIR} ${EXPDIR}${SEP}netccitt${SUBDIR_HEADER}
	${MKDIR} ${EXPDIR}${SEP}netinet${SUBDIR_HEADER}
	${MKDIR} ${EXPDIR}${SEP}netiso${SUBDIR_HEADER}
	${MKDIR} ${EXPDIR}${SEP}protocols${SUBDIR_HEADER}
	${MKDIR} ${EXPDIR}${SEP}sys${SUBDIR_HEADER}

# Where a header name appears in more than one subdirectory, VPATH cannot be
# used to specify both of them from the leafname alone. GNU make can figure
# out what to do if we explicitly include the subdirectory in HDRS, but for
# amu, it can't work out which implicit rule to use, so provide corresponding
# explicit rules below:
exphdr.machine.param:;  ${CP} machine.h.param  ${EXPDIR}.machine.h.param  ${CPFLAGS}
exphdr.machine.proc:;   ${CP} machine.h.proc   ${EXPDIR}.machine.h.proc   ${CPFLAGS}
exphdr.machine.signal:; ${CP} machine.h.signal ${EXPDIR}.machine.h.signal ${CPFLAGS}
exphdr.machine.types:;  ${CP} machine.h.types  ${EXPDIR}.machine.h.types  ${CPFLAGS}
exphdr.sys.dirent:;     ${CP} sys.h.dirent     ${EXPDIR}.sys.h.dirent     ${CPFLAGS}
exphdr.sys.errno:;      ${CP} sys.h.errno      ${EXPDIR}.sys.h.errno      ${CPFLAGS}
exphdr.sys.param:;      ${CP} sys.h.param      ${EXPDIR}.sys.h.param      ${CPFLAGS}
exphdr.sys.proc:;       ${CP} sys.h.proc       ${EXPDIR}.sys.h.proc       ${CPFLAGS}
exphdr.sys.signal:;     ${CP} sys.h.signal     ${EXPDIR}.sys.h.signal     ${CPFLAGS}
exphdr.sys.types:;      ${CP} sys.h.types      ${EXPDIR}.sys.h.types      ${CPFLAGS}
exphdr.sys.unistd:;     ${CP} sys.h.unistd     ${EXPDIR}.sys.h.unistd     ${CPFLAGS}

# By contrast, where there is a top-level file which shares a name with a file
# from a subdirectory, GNU make (but not amu) unhelpfully picks an implicit
# rule for the subdirectory when there is no subdirectory specified in HDRS,
# so provide explicit rules for these also below:
dirent.exphdr: dirent.h; ${CP} $< ${EXPDIR}${SEP}$< ${CPFLAGS}
errno.exphdr:  errno.h;  ${CP} $< ${EXPDIR}${SEP}$< ${CPFLAGS}
unistd.exphdr: unistd.h; ${CP} $< ${EXPDIR}${SEP}$< ${CPFLAGS}

# cppcheck goes into an infinite inclusion loop where a few of our headers
# include the equivalently-named standard header file (because in the absence
# of a directory spec, it assumes the target header is in the same directory
# as the including one). To work around this without modifying the source-
# controlled version of these headers, rewrite them during export to include
# an explicit "CLib/" directory. As cppcheck doesn't run natively, only do
# this in the cross-compiling case.
ifneq (,${MAKE_VERSION})
limits.exphdr:     limits.h;     sed -e 's/<limits.h>/<CLib\/limits.h>/' $< > ${EXPDIR}${SEP}$<
stdarg.exphdr:     stdarg.h;     sed -e 's/<stdarg.h>/<CLib\/stdarg.h>/' $< > ${EXPDIR}${SEP}$<
sys/signal.exphdr: sys/signal.h; sed -e 's/<signal.h>/<CLib\/signal.h>/' $< > ${EXPDIR}${SEP}$<
time.exphdr:       time.h;       sed -e 's/<time.h>/<CLib\/time.h>/'     $< > ${EXPDIR}${SEP}$<
endif

# Dynamic dependencies:
