obj-$(CONFIG_OPPO_COMMON_SOFT)	+=  oppo_project/
obj-$(CONFIG_OPPO_DEVICE_IFNO)	+=  oppo_devinfo/
obj-$(CONFIG_OPPO_DEBUG_ASSERT)	+=  oppo_trace/
obj-$(CONFIG_RECORD_MDMRST)     +=  oppo_mdmrst/
#ifdef VENDOR_EDIT
#//John.Xu@PhoneSw.AudioDriver, 2016/02/22, Add for critical_log
obj-y     +=  oppo_criticallog/
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
#//jie.cheng@swdh.sh, 2016/04/15, Add hypnus helper
obj-y     +=  oppo_hypnus/
#endif /* VENDOR_EDIT */
#ifdef VENDOR_EDIT
#//wenbin.liu@PSW.Kernel.MM, 2018/05/23, Add for Healthinfo monitor
obj-y     +=  oppo_healthinfo/
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
#wen.luo@PSW.Kernel.MM, 2018/05/23, Add for Healthinfo monitor
obj-$(CONFIG_OPPO_SHUTDOWN_DETECT)	+=  oppo_shutdown_detect/
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
#Kun.Hu@TECH.BSP.Stability.PHOENIX_PROJECT 2019/06/11, Add for phoenix project
obj-$(CONFIG_OPPO_PHOENIX)   += oppo_phoenix/
#endif

#GaoTing.Gan@PSW.MultiMedia.MediaServer, 2019/03/07, Add for kevent feedback
obj-y     +=  oppo_kevent_feedback/
#endif /* VENDOR_EDIT */
