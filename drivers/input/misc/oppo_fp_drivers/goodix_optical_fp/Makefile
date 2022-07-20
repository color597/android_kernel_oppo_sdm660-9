DEFS = -DCONFIG_OPPO_FINGERPRINT_KERNEL_VER=$(VERSION)
ccflags-y += $(DEFS)

ifeq ($(CONFIG_OPPO_FINGERPRINT_QCOM),y)
obj-y  += gf_spi.o gf_platform.o gf_netlink.o

ifneq ($(filter MSM_18115 MSM_18501 MSM_18503 MSM_18119 MSM_19061 MSM_19361 MSM_19071 MSM_19081 MSM_19371, $(OPPO_TARGET_DEVICE)),)
ccflags-y += -DUSED_GPIO_PWR
ccflags-y += -DCONFIG_MSM_855
endif

ifneq ($(filter MSM_19081, $(OPPO_TARGET_DEVICE)),)
ccflags-y += -DCONFIG_19081_PWR
endif

ifneq ($(filter MSM_18383, $(OPPO_TARGET_DEVICE)),)
ccflags-y += -DUSED_GPIO_PWR
endif

ifneq ($(filter MSM_19031 MSM_19331 MSM_19111 MSM_19513, $(OPPO_TARGET_DEVICE)),)
ccflags-y += -DAUTO_PWR
ccflags-y += -DCONFIG_MSM_7150
endif

else ifeq ($(CONFIG_OPPO_FINGERPRINT_MTK),y)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/spi/trustzone/spi/Tlspi/inc
include $(srctree)/drivers/misc/mediatek/gud/Makefile.include

ifneq ($(filter 17331, $(shell echo $(TARGET_PRODUCT) | sed -e 's/full_oppo6771_//g')),)
DEFS_PROJ = -DCONFIG_MT6771_17331
ccflags-y += $(DEFS_PROJ)
endif

ifneq ($(filter full_oppo6779_19011 full_oppo6779_19301 full_oppo6771_19531, $(TARGET_PRODUCT)),)
ccflags-y += -DCONGIG_MTK_P90M
endif

obj-y  += gf_tee.o
gf_tee-y := gf_spi_tee.o gf_platform_mtk.o gf_netlink.o
endif
