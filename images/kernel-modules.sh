#!/bin/bash
# kernel-modules.sh TREE CONFIG LOCALVERSION [OUTDIR]: makes a kernel
# configuration with loadable modules from one without.
#
# TREE is the kernel source, CONFIG a configuration (kernel-*.config as
# they were up to 1.0.0020: no modules). It turns modules on, adds the
# drivers for Wi-Fi, Ethernet and mobile broadband adapters that people
# plug in or fit as modules, sets the kernel's local version, and leaves
# the result as OUTDIR/.config (OUTDIR: a build directory for make O=;
# TREE itself if not given). Nothing that was built in becomes a module:
# the machines' own drivers stay in the kernel.
#
# kernel-6.6-pi.config, kernel-7.2-generic.config and
# kernel-7.2-wdk2023.config are its output: it need only be run again to
# add a driver to the list below.
#
# Firmware: the kernels read compressed firmware too (FW_LOADER_COMPRESS),
# as Debian's firmware packages may hold it.
#
# Left out on purpose: the old USB host-to-host cable drivers (cdc_subset,
# net1080, plusb, zaurus). They claim devices cdc_ether should have.
#
# Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE).
set -euo pipefail
tree=$1; cfg=$(readlink -f "$2"); lv=$3; o=$(readlink -f "${4:-$1}")
cd "$tree"
mkdir -p "$o"
cp "$cfg" "$o/.config"
cp "$cfg" "$o/.config.before"
c() { scripts/config --file "$o/.config" -k "$@"; }
mk() { if [ "$o" = "$PWD" ]; then make "$@"; else make O="$o" "$@"; fi; }
c -e FW_LOADER_COMPRESS -e FW_LOADER_COMPRESS_XZ -e FW_LOADER_COMPRESS_ZSTD
c -e MODULES -e MODULE_UNLOAD -d MODVERSIONS -d MODULE_SIG -d MODULE_COMPRESS \
  -d MODULE_FORCE_LOAD -d MODULE_SRCVERSION_ALL -d TRIM_UNUSED_KSYMS \
  --set-str LOCALVERSION "$lv" -d LOCALVERSION_AUTO
# vendors (menus) that must be on for the drivers below
for v in WLAN_VENDOR_ATH WLAN_VENDOR_BROADCOM WLAN_VENDOR_INTEL WLAN_VENDOR_MEDIATEK \
         WLAN_VENDOR_RALINK WLAN_VENDOR_REALTEK WLAN_VENDOR_MARVELL WLAN_VENDOR_ZYDAS \
         NET_VENDOR_REALTEK NET_VENDOR_INTEL NET_VENDOR_BROADCOM NET_VENDOR_AQUANTIA \
         NET_VENDOR_ATHEROS NET_VENDOR_MARVELL NET_VENDOR_ASIX NET_VENDOR_MICROCHIP \
         USB_NET_DRIVERS WLAN ETHERNET RT2X00 MT76_CORE; do
  grep -q "^CONFIG_$v=y" "$o/.config" || c -e $v 2>/dev/null || true
done
mods="
RTL8XXXU RTL8192CU RTL8192EE RTL8192DE RTL8188EE RTL8723AE RTL8723BE RTL8821AE RTL8192SE RTL8192CE
RTW88 RTW88_8821CU RTW88_8822BU RTW88_8822CU RTW88_8723DU RTW88_8821AU RTW88_8812AU RTW88_8814AU
RTW88_8822BE RTW88_8822CE RTW88_8821CE RTW88_8723DE RTW88_8814AE RTW88_8822BS RTW88_8822CS RTW88_8821CS RTW88_8723DS
RTW89 RTW89_8851BE RTW89_8852AE RTW89_8852BE RTW89_8852CE RTW89_8922AE RTW89_8851BU RTW89_8852BU RTW89_8852AU RTW89_8852CU
MT7601U MT76x0U MT76x2U MT76x0E MT76x2E MT7603E MT7615E MT7663U MT7915E MT7921E MT7921U MT7921S MT7925E MT7925U MT7996E
RT2X00 RT2800USB RT2500USB RT73USB RT2800PCI
ATH9K ATH9K_HTC CARL9170 ATH10K ATH10K_PCI ATH10K_USB ATH11K ATH11K_PCI ATH12K AR5523 ATH5K
IWLWIFI IWLDVM IWLMVM IWLMLD
BRCMSMAC MWIFIEX MWIFIEX_USB MWIFIEX_PCIE
USB_NET_AX8817X USB_NET_AX88179_178A USB_NET_CDCETHER USB_NET_CDC_EEM USB_NET_CDC_NCM
USB_NET_HUAWEI_CDC_NCM USB_NET_CDC_MBIM USB_NET_QMI_WWAN USB_NET_RNDIS_HOST
USB_NET_DM9601 USB_NET_SR9700 USB_NET_SR9800 USB_NET_MCS7830
USB_NET_SMSC75XX USB_NET_AQC111 USB_PEGASUS USB_RTL8150 USB_IPHETH
USB_SIERRA_NET USB_VL600 USB_NET_CH9200 USB_LAN78XX
USB_WDM USB_SERIAL USB_SERIAL_OPTION USB_SERIAL_WWAN USB_ACM
R8169 E1000 E1000E IGB IGBVF IGC IXGBE ATLANTIC TIGON3 BNX2 ALX ATL1 ATL1E ATL1C ATL2 SKGE SKY2 8139CP 8139TOO
E100 NE2K_PCI FORCEDETH VIA_RHINE VIA_VELOCITY JME TEHUTI_TN40 R8125 AQTION MICROCHIP_T1 LAN743X STMMAC_PCI
"
for m in $mods; do
  # never demote something built in
  grep -q "^CONFIG_$m=y" "$o/.config" && continue
  c -m $m
done
mk ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig >/dev/null
# things that default to on once modules are possible, and aren't wanted
c -d KPROBES -d RTLWIFI_DEBUG -d IWLWIFI_DEVICE_TRACING -d RTW88_DEBUG -d RTW89_DEBUGMSG -d ATH10K_DEBUG
mk ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig >/dev/null
# anything built in before that came out as a module: back to built in
B="$o/.config.before"; C="$o/.config"
back=$(comm -12 <(grep '=y$' "$B" | sed 's/=y$//' | sort) <(grep '=m$' "$C" | sed 's/=m$//' | sort) || true)
if [ -n "$back" ]; then
  for s in $back; do c -e ${s#CONFIG_}; done
  mk ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig >/dev/null
fi
echo "built in before, missing now:"; comm -23 <(grep '=y$' "$B" | sort) <(grep '=y$' "$C" | sort) | head -20
echo "demoted: $(comm -12 <(grep '=y$' "$B" | sed 's/=y$//' | sort) <(grep '=m$' "$C" | sed 's/=m$//' | sort) | tr '\n' ' ')"
echo "modules: $(grep -c '=m$' "$C")   new built-ins: $(comm -13 <(grep '=y$' "$B" | sort) <(grep '=y$' "$C" | sort) | wc -l)"
comm -13 <(grep '=y$' "$B" | sort) <(grep '=y$' "$C" | sort) | tr '\n' ' '
