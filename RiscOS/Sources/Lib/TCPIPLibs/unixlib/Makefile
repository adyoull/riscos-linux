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
# Makefile for unixlib

COMPONENT = unixlib
ifeq ($(filter install%,${MAKECMDGOALS}),)
EXPDIR    = ${LIBDIR}${SEP}TCPIPLibs
else
EXPDIR    = ${INSTDIR}${SEP}TCPIPLibs
endif
CINCLUDES = ${TCPIPINC}
CDEFINES  = -DINET
CFLAGS    = ${C_NOWARN_NON_ANSI_INCLUDES}
CAPPFLAGS = ${C_FNAMES}
CMODFLAGS = ${C_NO_STKCHK}

OBJS = access alloc chdir chmod clockgtime close filestat flushinput fstat \
       getdtabsiz getegid geteuid getgroups gethostnam getlogin getpass \
       getpid getpwent getpwnam getpwuid getuid getusernam getwd \
       gtimeofday herror index ioctl killfile lseek memops osreadc read \
       readdir readv strcasecmp stubs write writev xgets xperror xputchar \
       getopt strtonum strsep kvm err errlst xstrerror strdup strlcat \
       strlcpy sleep trimdomain ffs asm

include CLibrary

# Dynamic dependencies:
