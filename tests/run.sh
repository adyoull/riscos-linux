#!/bin/bash
# tests/run.sh: run the test programs (out/tests, from tests/programs) and
# a few RISC OS commands in RISC OS for Linux with bin/rol-run, and check
# each one's exit status and output.
#
# Known limitations of the Linux port are run as XFAIL tests: they're
# expected to time out, and an XPASS (it worked!) is reported but doesn't
# fail the run.
#
#   tests/run.sh [NAME...]      (RCD_DIR or RISCOS_TOOLCHAIN must be set)
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TB=$(cd "$HERE/.." && pwd)
RUN=$TB/bin/rol-run
T=$TB/out/tests

pass=0 fail=0 xfail=0 xpass=0
reject=''        # check: a Perl regexp the output must not match ("" = none)
want=("$@")

# check NAME STATUS PATTERN TIMEOUT ENV -- ROL-RUN ARGS...
#   STATUS: the exit status wanted; PATTERN: an extended regexp the output
#   (stdout + stderr) must match ("" = anything); ENV: VAR=value settings
#   for the run ("" = none)
check () {
  local name=$1 status=$2 pattern=$3 limit=$4 envs=$5
  shift 6
  if [ ${#want[@]} -gt 0 ] && ! printf '%s\n' "${want[@]}" | grep -qx "$name"; then return; fi
  local out rc t0 secs
  t0=$(date +%s%N)
  # shellcheck disable=SC2086
  out=$(env $envs "$RUN" -t "$limit" "$@" 2>&1)
  rc=$?
  secs=$(( ($(date +%s%N) - t0) / 100000000 ))
  secs="$((secs / 10)).$((secs % 10))"
  if [ "$rc" = "$status" ] && { [ -z "$pattern" ] || grep -Eq "$pattern" <<<"$out"; } &&
     { [ -z "${reject:-}" ] || ! grep -Pq "$reject" <<<"$out"; }; then
    echo "PASS  $name (${secs}s)"
    pass=$((pass + 1))
  else
    echo "FAIL  $name: exit $rc (wanted $status)${pattern:+, output should match /$pattern/} (${secs}s)"
    sed 's/^/      | /' <<<"$out" | tail -20
    fail=$((fail + 1))
  fi
}

# xfail NAME TIMEOUT REASON -- ROL-RUN ARGS...: expected to time out.
xfail () {
  local name=$1 limit=$2 reason=$3
  shift 4
  if [ ${#want[@]} -gt 0 ] && ! printf '%s\n' "${want[@]}" | grep -qx "$name"; then return; fi
  local out rc
  out=$("$RUN" -t "$limit" "$@" 2>&1)
  rc=$?
  if [ "$rc" = 124 ]; then
    echo "XFAIL $name ($reason)"
    xfail=$((xfail + 1))
  else
    echo "XPASS $name: exit $rc instead of the expected time-out ($reason) - update tests/run.sh and the README"
    sed 's/^/      | /' <<<"$out" | tail -10
    xpass=$((xpass + 1))
  fi
}

# --- RISC OS itself and the modules -------------------------------------------
check echo 0 '^hello from RISC OS$' 60 '' -- -c 'Echo hello from RISC OS'
check riscos-error 125 'RISC OS error: File .*no-such-file. not found' 60 '' -- -c 'Run no-such-file'
check serious-error 125 'RISC OS error: Internal error: abort on data transfer' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.abort/bas'
check vfpsupport-info 0 'D registers: &000000(10|20)' 60 '' -- -c 'VFPSupport_Info'
ROOL=IXFS:\$.$(printf '%s' "${HERE#/}" | tr './' '/.').rool
# RISC OS Open's own tests (tests/rool): all of test1 but its two checks of
# lazy activation (VFP can't be switched off on the port)
check rool-vfp-features 0 'Misc 4' 60 '' -- -c "BASIC -quit $ROOL.features"
# (an error is a line starting "ERROR:"; the one allowed is the last check)
reject='^ERROR: (?!VDLM with lazy null context)'
check rool-vfp-test1 0 '^Restoring original context' 60 '' -- -c "BASIC -quit $ROOL.test1"
reject=
check vfpsupport-one-instance 0 '^error &81F106$' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.instance/bas'
# *BASIC64 runs BASICVFP (from ROOL's Pi ROM: make basicvfp) once
# VFPSupport is loaded, as on a Pi; its maths uses ElementaryFunctions.
if [ -f "$TB/out/modules/BASICVFP,ffa" ]; then
  check basicvfp 0 '^pi 3\.14159265' 60 '' -- \
    -c 'BASIC64 -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.vfpbasic/bas'
elif [ ${#want[@]} -eq 0 ]; then
  echo "SKIP  basicvfp (no out/modules/BASICVFP,ffa: make basicvfp)"
fi
# IXFS: a file past 2GB (natively needs RISC OS 5.31-13)
check ixfs-bigfile 0 '^bigfile ok' 120 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.bigfile/bas'
# IXFS's free space (OS_FSControl 49 and 55): needs RISC OS 5.31-11
check ixfs-freespace 0 '^freespace ok' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.freespace/bas'
# IXFS's disc name, ROL (canonical IXFS::ROL.$.path): needs RISC OS 5.31-16
check ixfs-discname 0 '^discname ok' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.discname/bas'

# FileCore's compaction with ScratchSpace at &20000000, and pointers set
# past the extent (IXFS and RAM disc): RISC OS 5.31-28
check ramdisc 0 '^ramdisc ok' 600 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.ramdisc/bas'
# BPUT# of two strings to a new file, 9000 times, read back (how Updates
# sends rol-cmd its password line and command; natively the write's SWI
# site gets its stub on the way)
check bput 0 '^bput ok' 600 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.bput/bas'
# a literal pool's number that reads as MRSMI r4, CPSR (256000.0) and one
# that reads as MSR CPSR_c, r0 are loaded as they are (natively:
# NativeSupport 0.47 leaves a word that a pc-relative load reads)
check literal 0 '^literal ok' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.literal/bas'
# OS_CallAfter and OS_RemoveTickerEvent against a firing ticker for 40 s
# (5.31-29: OS_RemoveTickerEvent really turns interrupts off; a soak test)
check ticker 0 '^ticker ok' 120 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.ticker/bas'
# GPIO HAL devices (RISC OS 5.31-17): a Pi 3 or 4's, through /dev/gpiomem;
# none elsewhere (here), and looking for them must not go wrong
check gpio-devices 0 '^gpio (ok|none)$' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.gpiocheck/bas'
# *IXRun (RISC OS 5.31-14): rol-cmd's @user request, as on the images, so
# a host rol-cmd (images/fe/rol-cmd.c, built here) takes it from
# /riscos/cmd. It needs root (it runs @user commands as uid 1000) and
# RISC OS seeing the host's /riscos (under QEMU, not in the native VM).
# (Without rol-cmd *IXRun uses a terminal, which reads the keyboard: under
# rol-run stdin isn't connected, so that route can't be checked here.)
ixrun_names='ixrun-path ixrun-user ixrun-status'
ixrun_wanted=1
if [ ${#want[@]} -gt 0 ]; then
  ixrun_wanted=0
  for n in $ixrun_names; do printf '%s\n' "${want[@]}" | grep -qx "$n" && ixrun_wanted=1; done
fi
if [ "$ixrun_wanted" = 1 ]; then
  if [ "$(id -u)" = 0 ] && [ -z "${ROL_VM:-}" ] && command -v cc >/dev/null &&
     mkdir -p "$T" /riscos/cmd && cc -O2 -o "$T/rol-cmd-host" "$TB/images/fe/rol-cmd.c" 2>/dev/null; then
    "$T/rol-cmd-host" & ixrun_pid=$!
    check ixrun-path 0 '^ixrun ok' 60 '' -- -c 'IXRun echo ixrun ok'
    check ixrun-user 0 '^1000$' 60 '' -- -c 'IXRun id -u'
    check ixrun-status 125 'The Linux command failed' 60 '' -- -c 'IXRun exit 3'
    kill "$ixrun_pid" 2>/dev/null; wait "$ixrun_pid" 2>/dev/null
  else
    echo "SKIP  $ixrun_names (needs root, QEMU and a C compiler for a host rol-cmd)"
  fi
fi
# The HAL counter reloads at each centisecond tick, as on hardware
# (needs RISC OS 5.31-15: it reloaded once a second)
check hal-counter 0 '^halcounter ok' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.halcounter/bas'
check armeabisupport-info 0 'Running program 0, nested 0' 60 '' -- -c 'ARMEABISupport_Info'
# Privileged PSR instructions in code from disc (qemu/: emulated with the
# port's virtual CPU modes): MRS, MSR CPSR_c, CPSID/CPSIE, then a SWI back
# in USR mode (it mustn't corrupt USR's r14).
# (Not when the ROM runs directly, without QEMU: see rol-run's ROL_QEMU.)
direct=0
if [ "${ROL_QEMU+set}" = set ]; then
  [ -z "$ROL_QEMU" ] && direct=1
else
  case $(uname -m) in armv7*|armv8l) direct=1 ;; esac
fi
if [ "$direct" = 0 ]; then
  check psr-emulation 0 '^usr &10 svc &13 cpsid &93 cpsie &13 usr &10 nosp &10$' 60 '' -- \
    -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.psr/bas'
  # the rest of what disc code uses: MSR/MRS SPSR, STM ^, LDM {pc}^, MOVS pc
  check priv-emulation 0 '^spsr &10 ldm\^ &10 movs &10 usr-sp 1$' 60 '' -- \
    -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.priv/bas'
elif [ ${#want[@]} -eq 0 ]; then
  echo "SKIP  psr-emulation, priv-emulation (the ROM runs without QEMU)"
fi
# Sound: RISC OS Open's SoundDMA (the ROM's) on LinuxAudio, and the sound
# system on it; then the old silent stand-in (modules/SoundDMA).
check sounddma 0 '^enabled 2 channels 1 handler 1 stereo 64 rates 8 voice 1$' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.sound/bas'
check sounddma-silent 0 '^enabled 2 channels 1 handler 1 stereo 64 rates 1 voice 1$' 60 ROL_SOUND=silent -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.sound/bas'
check soundctrl 0 '^channels [1-9][0-9]* block 16 mute 0 gain -96$' 60 '' -- \
  -c 'BASIC -quit IXFS:$.'"$(printf '%s' "${HERE#/}" | tr './' '/.')"'.programs.soundctrl/bas'

# --- programs built with riscos-crossdev ---------------------------------------
check exit-status 3 'arg2=two words' 60 '' -- "$T/t_exit" 3 'two words'
check stderr 0 'a line on stderr' 60 '' -- "$T/t_exit"
check vfpsupport 0 '^PASS$' 60 '' -- "$T/t_vfpsupport"
check vfpsupport-cortex-a8 0 '^PASS$' 60 QEMU_CPU=cortex-a8 -- "$T/t_vfpsupport"
check elementary-functions 0 '^PASS$' 120 '' -- "$T/t_elementary"
check elementary-functions-vfpv3 0 '^PASS$' 120 QEMU_CPU=cortex-a8 -- "$T/t_elementary"
check vfp-switch-atomic 0 '^PASS$' 60 '' -- "$T/t_vfp_irq"
# Floating point errors: the FPSCR's exception flags from VFP instructions,
# trap enables (not supported: they read as zero, and 1/0 just sets DZC),
# flags kept per context, and VFPSupport's own error numbers.
check vfp-errors 0 '^PASS$' 60 '' -- "$T/t_vfp_errors"
check vfp-errors-cortex-a8 0 '^PASS$' 60 QEMU_CPU=cortex-a8 -- "$T/t_vfp_errors"
# Socket_Select with nothing to read returns 0 when its timeout (0.5 s, 1 s,
# 5 ms, 0) runs out, and 1 at once with a datagram waiting. Needs a ROM of
# 5.31-4 or later: before, a select that timed out spun forever.
check select-timeout 0 '^PASS$' 60 '' -- "$T/t_select"
# Socket_Getsockopt SO_TYPE (GLib needs it), an unknown TCP option refused,
# Socket_Sendmsg's flags in R2, and Socket_Sendmsg_1. Needs a ROM of 5.31-6
# or later (found by the web browser project).
check sockopt 0 '^PASS$' 60 '' -- "$T/t_sockopt"
# Socket_Select on an accepted socket whose peer sent before the accept
# (RDP, TLS): readable and writable at once, and the data received. Needs
# a ROM of 5.31-7 or later (RDPServer: "server not responding").
check accept-select 0 '^PASS$' 60 '' -- "$T/t_accept"
# the other end's close seen by select and read (needs RISC OS 5.31-12)
check select-eof 0 '^PASS$' 60 '' -- "$T/t_eof"
# IXFS gives a file with no RISC OS type the type MimeMap gives its
# extension (photo/jpg is JPEG), as DOSFS does: USB discs and files Linux
# programs wrote. Needs a ROM of 5.31-8 or later.
check mimetype 0 '^PASS$' 60 '' -- "$T/t_mimetype"
# Every VFP/NEON float instruction on special values in 16 FPSCR modes, and
# the NEON integer ops QEMU does with whole-instruction helpers: CRCs of
# all the results and flags. The CRCs are those of QEMU's exact (soft)
# emulation (RISCOS_NO_HOSTFP=1 RISCOS_NO_HOSTNEON=1): the host FPU and
# host vector paths must give the same bits on x86-64 and aarch64 hosts.
# The same CRC from QEMU's exact soft float, its host FPU fast path in the
# helpers, and the floating point done inline in the translated code
# (qemu/ patch item 11), alone and together.
check fp-conformance 0 'CRC c7541c16$' 120 '' -- "$T/t_fpconform"
check fp-conformance-exact 0 'CRC c7541c16$' 120 'RISCOS_NO_HOSTFP=1 RISCOS_NO_INLINEFP=1' -- "$T/t_fpconform"
check fp-conformance-helpers 0 'CRC c7541c16$' 120 'RISCOS_NO_INLINEFP=1' -- "$T/t_fpconform"
check fp-conformance-inline 0 'CRC c7541c16$' 120 'RISCOS_NO_HOSTFP=1' -- "$T/t_fpconform"
# Long blocks of floating point instructions feeding each other (ARM and
# Thumb), translated with the inexact flag set and run in every mode: the
# inline code's gates, its slow paths at the end of the TB and what TCG's
# optimizer carries from one instruction to the next.
check fp-chain 0 'CRC ce555057$' 300 '' -- "$T/t_fpchain"
check fp-chain-exact 0 'CRC ce555057$' 300 'RISCOS_NO_HOSTFP=1 RISCOS_NO_INLINEFP=1' -- "$T/t_fpchain"
check fp-chain-long 0 'CRC cc903bbb$' 300 '' -- "$T/t_fpchain_long"
check fp-chain-long-exact 0 'CRC cc903bbb$' 300 'RISCOS_NO_HOSTFP=1 RISCOS_NO_INLINEFP=1' -- "$T/t_fpchain_long"
check neon-int-conformance 0 'CRC 2d80f4bf$' 60 '' -- "$T/t_neonint"
check neon-int-conformance-exact 0 'CRC 2d80f4bf$' 60 'RISCOS_NO_HOSTNEON=1' -- "$T/t_neonint"
check neon-perm-conformance 0 'CRC 6d60edf5$' 60 '' -- "$T/t_neonperm"
check neon-perm-conformance-exact 0 'CRC 6d60edf5$' 60 'RISCOS_NO_HOSTNEON=1' -- "$T/t_neonperm"
# The elementary functions against RISC OS Open's own (its VFPSupport from
# the Pi ROM: make basicvfp): bit for bit, flags too. The Pi ROM's are the
# software-FMA build, which ours uses on a VFPv3 CPU (Cortex-A8).
ROOLVFP=$TB/out/pirom/VFPSupport-ROOL,ffa
if [ -f "$ROOLVFP" ]; then
  check elementary-same-as-rool 0 '0 different from ROOL' 120 QEMU_CPU=cortex-a8 -- \
    "$T/t_rool_elementary" "$ROOLVFP" strict
  check elementary-same-as-rool-vfpv4 0 '^PASS$' 120 '' -- "$T/t_rool_elementary" "$ROOLVFP"
elif [ ${#want[@]} -eq 0 ]; then
  echo "SKIP  elementary-same-as-rool (no $ROOLVFP: make basicvfp)"
fi
check cxx 0 '^PASS$' 60 '' -- "$T/t_cxx"
check mmap 0 '^PASS$' 120 '' -- "$T/t_mmap"
check memory-access 0 '^PASS$' 60 '' -- "$T/t_access"
# Flags, GE, registers, VFP and ITSTATE kept across UnixLib thread switches
# (ARM and Thumb-2 blocks, 3 threads, 15 s)
check psr-keep 0 '^PASS$' 90 '' -- "$T/t_psrkeep" 15
check iconv 0 '^PASS$' 60 '' -- "$T/t_iconv"
check system 0 '^PASS$' 60 '' -- "$T/t_system"
check files 0 '^PASS$' 60 '' -- "$T/t_files"

# --- the desktop (HardDisc4's !Boot), when out/harddisc is there ---------------
# desk NAME STATUS SHOT "X Y RRGGBB ..." -- ROL-RUN ARGS...: run in desktop
#   mode; then each pixel X,Y of SHOT (relative to the screenshot's
#   directory) must be RRGGBB ("" = the status only).
desk () {
  local name=$1 status=$2 shotname=$3 pixels=$4
  shift 5
  if [ ${#want[@]} -gt 0 ] && ! printf '%s\n' "${want[@]}" | grep -qx "$name"; then return; fi
  if [ ! -d "$TB/out/harddisc/HardDisc4" ]; then
    echo "SKIP  $name (no out/harddisc/HardDisc4: make harddisc)"
    return
  fi
  local dir out rc got want_px= xy=() px=() t0 secs
  dir=$(mktemp -d)
  t0=$(date +%s%N)
  out=$("$RUN" -t 90 -s "$dir/screen.png" "$@" 2>&1)
  rc=$?
  secs=$(( ($(date +%s%N) - t0) / 100000000 ))
  secs="$((secs / 10)).$((secs % 10))"
  read -ra px <<<"$pixels"
  for ((k = 0; k < ${#px[@]}; k += 3)); do xy+=("${px[k]}" "${px[k + 1]}"); want_px+="${px[k + 2]} "; done
  got=
  [ ${#xy[@]} -eq 0 ] ||
    got=$(python3 -I "$HERE/pngpixel.py" "$dir/$shotname" "${xy[@]}" 2>&1 | tail -n +2 | tr '\n' ' ')
  if [ "$rc" = "$status" ] && [ "$got" = "$want_px" ]; then
    echo "PASS  $name (${secs}s)"
    pass=$((pass + 1))
  else
    echo "FAIL  $name: exit $rc (wanted $status), pixels '$got' (wanted '$want_px') (${secs}s)"
    sed 's/^/      | /' <<<"$out" | tail -10
    fail=$((fail + 1))
  fi
  rm -rf "$dir"
}
# t_wimp's window: white work area at 220,300, grey icon at 260,350; the
# desktop's backdrop where the window was, once it has gone.
desk desktop-window 0 screen-1.png "220 300 ffffff 260 350 dddddd" -- "$T/t_wimp"
desk desktop-exit-status 4 screen-1.png "220 300 ffffff" -- "$T/t_wimp" exit 4
desk desktop-closed 4 screen.png "220 300 666666" -- "$T/t_wimp" exit 4
# Wimp_PollIdle on an idle desktop: three null events, each 50 cs after
# asking, after a file operation each time (exit status 7).
desk desktop-pollidle 7 screen.png "" -- -w 15 "$T/t_pollidle"
# Two desktop tasks keep their own VFP registers and rounding modes across
# Wimp_Poll (the Wimp switches their contexts through VFPSupport); task A's
# window shows both results (white work area at 220,300).
desk desktop-vfp-two-tasks 0 screen-1.png "220 300 ffffff" -- -w 30 "$T/t_wimp_vfp"
# HardDisc4's own SharedCLibrary (6.22, which !System loads) and a program
# using it (BootVars), unchanged: it enters USR mode with MSR.
desk desktop-clib 0 screen.png "220 300 666666" -- "$T/t_clib"
# HardDisc4's applications that once failed on the port, still running
# after 15 s with no error box: !SciCalc (BASIC VFP, which drops to USR
# mode with CPSIE), !Maestro (sound), !Hopper (sound, the QTM player).
for app in Apps.!SciCalc Apps.!Maestro Diversions.!Hopper; do
  desk "app-$(echo "$app" | sed 's/.*!//' | tr 'A-Z' 'a-z')" 0 screen.png "" -- -w 15 -c "Run <IXFS\$HardDisc4>.$app"
done

# The scroll wheel, as the UTM image has it (KVMWheel + WheelScroll):
# tests/sound-out.sh checks the sound itself, saved as a WAV file.
if [ -n "${ROL_VM:-}" ]; then
  # (the sound comes out of a FIFO, which can't reach a virtual machine)
  if [ ${#want[@]} -eq 0 ] || printf '%s\n' "${want[@]}" | grep -qx sound-out; then
    echo "SKIP  sound-out (ROL_VM)"
  fi
elif [ ${#want[@]} -eq 0 ] || printf '%s\n' "${want[@]}" | grep -qx sound-out; then
  r=$("$HERE/sound-out.sh" 2>&1)
  if [ "$r" = PASS ]; then
    echo "PASS  sound-out"; pass=$((pass + 1))
  else
    echo "FAIL  sound-out: $r"; fail=$((fail + 1))
  fi
fi
# tests/desktop-wheel.sh scrolls a Filer window with rol-kvm key numbers.
if [ ${#want[@]} -eq 0 ] || printf '%s\n' "${want[@]}" | grep -qx desktop-wheel; then
  if [ -n "${ROL_VM:-}" ]; then
    # (its control FIFO, to rol-kvm, can't reach a virtual machine)
    echo "SKIP  desktop-wheel (ROL_VM)"
  elif [ -d "$TB/out/harddisc/HardDisc4" ]; then
    r=$("$HERE/desktop-wheel.sh" 2>&1)
    if [ "$r" = PASS ]; then
      echo "PASS  desktop-wheel"; pass=$((pass + 1))
    else
      echo "FAIL  desktop-wheel: $r"; fail=$((fail + 1))
    fi
  else
    echo "SKIP  desktop-wheel (no out/harddisc/HardDisc4: make harddisc)"
  fi
fi

# --- NativeSupport's scanner, on this machine (tools/nsscan) --------------------
# which words it rewrites, case by case (tests/nsscan-cases.txt); needs
# arm-linux-gnueabihf-gcc and qemu-arm
if command -v arm-linux-gnueabihf-gcc >/dev/null && command -v qemu-arm >/dev/null; then
  if r=$(make -s -C "$TB" nsscan-check 2>&1); then
    echo "PASS  nsscan-cases"; pass=$((pass + 1))
  else
    echo "FAIL  nsscan-cases"; sed 's/^/      | /' <<<"$r" | tail -12; fail=$((fail + 1))
  fi
else
  echo "SKIP  nsscan-cases (no arm-linux-gnueabihf-gcc or qemu-arm)"
fi

# --- threads and faults --------------------------------------------------------
# They need UnixLib with the port's patch (out/unixlib, which the test
# programs are linked with when it's there). With the toolchain's own
# UnixLib they hang: known limitations, run as XFAIL tests.
if [ -f "$TB/out/unixlib/libunixlib.a" ]; then
  check threads 0 '^PASS$' 60 '' -- "$T/t_threads"
  check vfp-threads 0 '^PASS$' 60 '' -- "$T/t_vfp_threads"
  check data-abort 199 'Fatal signal received' 60 '' -- "$T/t_crash"
else
  xfail threads 10 "UnixLib's thread switcher uses privileged instructions (make unixlib)" -- "$T/t_threads"
  xfail vfp-threads 10 "threads, as above" -- "$T/t_vfp_threads"
  xfail data-abort 10 "UnixLib's abort handler uses privileged instructions (make unixlib)" -- "$T/t_crash"
fi

echo "$pass passed, $fail failed, $xfail expected failures, $xpass unexpected passes"
[ "$fail" = 0 ]
