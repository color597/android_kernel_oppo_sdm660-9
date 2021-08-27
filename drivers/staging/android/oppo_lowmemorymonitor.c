/***********************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - oppo_lowmemorymonitor.c
** Description: oppo customization lowmemory monitor for lowmemory and memory leak case
** Version: 1.0
** Date : 2017/06/15
** Author: Jiemin.Zhu@Swdp.Android.Performance.Memory
**
** ----------------------Revision History: --------------------
**  <author>	<data> 	   <version >	       <desc>
**  Jiemin.Zhu 2017/06/15     1.0           create this module
****************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/swap.h>
#include <linux/fs.h>

#include "oppo_lowmemorymonitor.h"

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

//from bionic/libc/malloc_debug/Config.cpp:backtrace_signal
#define SIGBACKTRACE_ENABLE   SIGRTMAX - 19
#define SIGOOM                SIGRTMAX - 17

//1/3 = 1/4 + 1/16 + 1/64
#define ONETHIRD(X) ((X >> 2) + (X >> 4) + (X >> 6))

#if defined(OPPO_AGING_TEST) || defined(CONFIG_OPPO_DAILY_BUILD)
static int memleak_print_times = 0;
#endif

int oppo_lowmemory_detect(struct task_struct *task, int tasksize)
{
#if defined(OPPO_AGING_TEST) || defined(CONFIG_OPPO_DAILY_BUILD)
	//if tasksize is more than half of totalram, we consider this process has memleak
	//then, we send signal to print leak stack for three times
	//finally, we kill it.
	//else if tasksie is more than one third of totalram, enable task backtrace
	if (tasksize > (totalram_pages >> 1)) {
		send_sig(SIGOOM, task, 0);
		if (memleak_print_times++ > 3) {
			send_sig(SIGKILL, task, 0);
			memleak_print_times = 0;
			return 1;
		}
	} else if (tasksize > ONETHIRD(totalram_pages)) {
		send_sig(SIGBACKTRACE_ENABLE, task, 0);
		send_sig(SIGOOM, task, 0);
	}
#endif

	return 0;
}
EXPORT_SYMBOL(oppo_lowmemory_detect);

static int __init oppo_lowmem_init(void)
{
	return 0;
}

static void __exit oppo_lowmem_exit(void)
{
}

module_init(oppo_lowmem_init);
module_exit(oppo_lowmem_exit);
