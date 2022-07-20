dts-dirs += altera
dts-dirs += amd
dts-dirs += apm
dts-dirs += arm
dts-dirs += broadcom
dts-dirs += cavium
dts-dirs += exynos
dts-dirs += freescale
dts-dirs += hisilicon
dts-dirs += marvell
dts-dirs += mediatek
#ifndef VENDOR_EDIT
#Fuchun.Liao@BSP.CHG.Basic 2017/01/19 modify for 16051/16103 dts
#dts-dirs += qcom
#else
dts-dirs += 16051
dts-dirs += 16103
dts-dirs += 16118
dts-dirs += 17011
dts-dirs += 17021
dts-dirs += 17081
dts-dirs += 17085
#dts-dirs += 17086
dts-dirs += 18005
dts-dirs += 18323
#endif
dts-dirs += rockchip
dts-dirs += sprd
dts-dirs += xilinx

subdir-y	:= $(dts-dirs)

dtstree		:= $(srctree)/$(src)

dtb-$(CONFIG_OF_ALL_DTBS) := $(patsubst $(dtstree)/%.dts,%.dtb, $(foreach d,$(dts-dirs), $(wildcard $(dtstree)/$(d)/*.dts)))

always		:= $(dtb-y)
targets += dtbs
