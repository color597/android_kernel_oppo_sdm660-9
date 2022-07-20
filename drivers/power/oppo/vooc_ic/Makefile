ifeq ($(CONFIG_OPPO_CHARGER_MTK),y)
ifeq ($(CONFIG_OPPO_CHARGER_6750T),y)
else ifeq ($(CONFIG_OPPO_CHARGER_MTK6763),y)
else ifeq ($(CONFIG_OPPO_CHARGER_MTK6771),y)
else
include $(srctree)/drivers/misc/mediatek/Makefile.custom
endif

obj-y      += oppo_vooc_fw_mtk.o
else
obj-y	+= oppo_vooc_fw_qcom.o
endif

ifeq ($(CONFIG_OPPO_CHARGER_MTK6763)_$(CONFIG_OPPO_CHARGER_MTK6771),n_n)
obj-y	+= oppo_pic16f.o
endif
ifeq ($(CONFIG_OPPO_CHARGER_MTK6771),y)
obj-y	+= oppo_n76e.o
endif
obj-y	+= oppo_stm8s.o
obj-y	+= oppo_adapter.o
