#!/bin/bash
# static aarch64 libnl 3.11 + wpa_supplicant 2.11 (nl80211, internal TLS)
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -euo pipefail
# W: a work directory holding src/libnl-3.11.0 (from libnl-3.11.0.tar.gz,
# here) and src/wpa_supplicant-2.11 (https://w1.fi/releases/wpa_supplicant-2.11.tar.gz)
W=${W:-$(cd "$(dirname "$0")" && pwd)/work}; P=$W/prefix
rm -rf $W/build/libnl $P; mkdir -p $W/build/libnl $P
cd $W/build/libnl
$W/src/libnl-3.11.0/configure --host=aarch64-linux-gnu --prefix=$P --disable-shared --enable-static --disable-cli --disable-debug >cfg.log 2>&1
make -j8 lib/libnl-3.la lib/libnl-genl-3.la >make.log 2>&1 || make -j8 >make.log 2>&1
make install-libLTLIBRARIES install-nobase_libnlincludeHEADERS >inst.log 2>&1 || make install >inst.log 2>&1 || true
ls $P/lib $P/include
cd $W/src/wpa_supplicant-2.11/wpa_supplicant
cat > .config <<'CFG'
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_CTRL_IFACE=y
CONFIG_BACKEND=file
CONFIG_TLS=internal
CONFIG_INTERNAL_LIBTOMMATH=y
CONFIG_INTERNAL_LIBTOMMATH_FAST=y
CONFIG_CRYPTO=internal
CONFIG_SAE=y
CONFIG_IEEE80211W=y
CONFIG_IEEE80211N=y
CONFIG_IEEE80211AC=y
CONFIG_IEEE80211AX=y
CONFIG_IEEE8021X_EAPOL=y
CONFIG_EAP_PEAP=y
CONFIG_EAP_MSCHAPV2=y
CONFIG_EAP_TTLS=y
CONFIG_EAP_TLS=y
CONFIG_EAP_GTC=y
CONFIG_EAP_MD5=y
CONFIG_ELOOP=eloop
CONFIG_L2_PACKET=linux
CONFIG_NO_RANDOM_POOL=y
CONFIG_GETRANDOM=y
CFG
make clean >/dev/null 2>&1 || true
make -j8 CC=aarch64-linux-gnu-gcc \
  CFLAGS="-O2 -I$P/include/libnl3 -DCONFIG_LIBNL20 -D_GNU_SOURCE" \
  LDFLAGS="-static -L$P/lib" LIBNL_INC="$P/include/libnl3" \
  PKG_CONFIG=/bin/false wpa_supplicant wpa_cli > $W/build/wpa.log 2>&1
aarch64-linux-gnu-strip -o $W/build/wpa_supplicant wpa_supplicant
aarch64-linux-gnu-strip -o $W/build/wpa_cli wpa_cli
file $W/build/wpa_supplicant $W/build/wpa_cli; ls -l $W/build/wpa_supplicant $W/build/wpa_cli
