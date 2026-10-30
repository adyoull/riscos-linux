# Copyright 2000 Pace Micro Technology plc
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
# Makefile for IOMD HAL
#

COMPONENT  = IOMD HAL
TARGET     = IOMD
OBJS       = Top Boot ATA IIC Interrupts MachineID NVRAM \
             Timers Video cbits KbdScan Debug Post IOMD21DMA RTC

include HAL

CFLAGS    += -APCS 3/32bit/nofp/noswst
ASFLAGS   += -APCS 3/nofp/noswst

# Dynamic dependencies:
