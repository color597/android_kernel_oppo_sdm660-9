ifeq ($(CONFIG_OPPO_FINGERPRINT),y)
obj-y        += oppo_fp_common/
obj-y        += fpc/
ifneq ($(filter MSM_18081 MSM_18381 MSM_18181 MSM_18385 MSM_18097 MSM_18539 MSM_18041 MSM_18099 MSM_18397 MSM_18115 MSM_18501 MSM_18503 MSM_18119 MSM_19061 MSM_19361 MSM_18383, $(OPPO_TARGET_DEVICE)),)
obj-y        += goodix_optical_fp/
obj-y        += silead/
ifneq ($(filter MSM_18041, $(OPPO_TARGET_DEVICE)),)
obj-y        += egis/
endif
endif
ifneq ($(filter MSM_17011 MSM_17015 MSM_17081 MSM_17085, $(OPPO_TARGET_DEVICE)),)
obj-y        += goodix_fp/
endif
ifneq ($(filter full_oppo6763 full_oppo6771, $(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g')),)
ifneq ($(filter oppo6771_19531, $(OPPO_TARGET_DEVICE)),)
obj-y        += silead_optical_fp/
obj-y        += goodix_optical_fp/
else
obj-y        += goodix_fp/
endif
endif
ifneq ($(filter full_oppo6779, $(shell echo $(TARGET_PRODUCT) | sed -e 's/_1.*//g')),)
obj-y        += silead_optical_fp/
obj-y        += goodix_optical_fp/
endif
ifneq ($(filter MSM_19031 MSM_19331 MSM_19111 MSM_19513 MSM_19071 MSM_19081 MSM_19371, $(OPPO_TARGET_DEVICE)),)
obj-y        += goodix_optical_fp/
endif
ifneq ($(filter MSM_18005 MSM_18316 MSM_18321 MSM_18323 MSM_18326, $(OPPO_TARGET_DEVICE)),)
obj-y        += silead/
obj-y        += goodix_optical_fp/
endif
endif
