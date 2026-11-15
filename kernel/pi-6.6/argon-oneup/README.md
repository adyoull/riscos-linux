# The Argon ONE UP's battery: Jeff Curless's driver

`oneUpPower.c` and `argon-oneup-battery.dts` are from
https://github.com/JeffCurless/argon-oneup (`battery/`), commit 5f1b6bd
(23 July 2026), unchanged. Copyright Jeff Curless; GPL-2.0-only (the
line at the top of `oneUpPower.c`).

The Argon ONE UP is a laptop built round a Compute Module 5. Its battery
gauge is a CellWise CW2217 on the I2C bus of the 40-pin header (i2c1) at
address &64, for which Linux has no driver of its own. This one loads the
battery's profile into the chip and gives Linux a battery (`BAT0`) and a
mains supply (`AC0`) in /sys/class/power_supply, which is what RISC OS
on Linux's battery icon reads. Below 5% and unplugged it powers the
machine off (`soc_shutdown`; 0 turns that off).

riscos-kernel-pi builds it as a module (`oneUpPower.ko`, loaded by
rol-modd when the device tree has the chip) and puts the overlay in the
boot files (`overlays/argon-oneup-battery.dtbo`). On an Argon ONE UP add
to config.txt:

    dtparam=i2c_arm=on            (above the first dtoverlay= line)
    dtoverlay=argon-oneup-battery

Built with, in the kernel tree:

    make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M=DIR modules
    scripts/dtc/dtc -@ -I dts -O dtb -o argon-oneup-battery.dtbo argon-oneup-battery.dts

where DIR holds `oneUpPower.c` and a Makefile of `obj-m += oneUpPower.o`.
