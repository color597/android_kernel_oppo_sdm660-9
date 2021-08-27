/***********************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - oppo_lowmemorymonitor.h
** Description: oppo customization lowmemory monitor for lowmemory and memory leak case
** Version: 1.0
** Date : 2017/06/15
** Author: Jiemin.Zhu@Swdp.Android.Performance.Memory
**
** ----------------------Revision History: --------------------
**  <author>	<data> 	   <version >	       <desc>
**  Jiemin.Zhu 2017/06/15     1.0           create this module
****************************************************************/
#ifndef _OPPO_LOWMEMORYMONITOR_H_
#define _OPPO_LOWMEMORYMONITOR_H_

extern int oppo_lowmemory_detect(struct task_struct *task, int tasksize);

#endif /* _OPPO_LOWMEMORYMONITOR_H_ */