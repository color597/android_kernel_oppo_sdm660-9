/*
 * Copyright (C) 2016 OPPO, Inc.
 * Author: Jie Cheng <jie.cheng@oppo.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/power_supply.h>
#include <linux/mmc/host.h>
#include <linux/ufshcd-platform.h>
#include <soc/oppo/hypnus_helper.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/rcupdate.h>
#include <linux/pm_qos.h>

/* mmc */
int hypnus_mmc_scaling_enable(int index, int value){
	int ret = 0;
	if (index >= MAX_MMC_STORE_HOST || mmc_store_host[index] == NULL){
		pr_err("hypnus_mmc_scaling_enable index err!\n");
		return -1;
	}
	ret = mmc_scaling_enable(mmc_store_host[index], value);
	return ret;
}
EXPORT_SYMBOL(hypnus_mmc_scaling_enable);

/* ufs */
int hypnus_ufs_scaling_enable(int index, int scale)
{
	int ret = 0;

	if (index >= MAX_UFS_STORE_HBA || ufs_store_hba[index] == NULL) {
		pr_err("%s index err!\n", __func__);
		return -EINVAL;
	}

	ret = ufshcd_clk_scaling_enable(ufs_store_hba[index], scale);
	return ret;
}

int hypnus_storage_scaling_enable(int index, int scale)
{
	if (storage_is_mmc())
		return hypnus_mmc_scaling_enable(index, scale);
	else if (storage_is_ufs())
		return hypnus_ufs_scaling_enable(index, scale);
	return -EINVAL;
}
EXPORT_SYMBOL(hypnus_storage_scaling_enable);

/* power */
static struct pm_qos_request hypnus_qos_request;

void lpm_qos_init(void)
{
	pm_qos_add_request(&hypnus_qos_request, PM_QOS_CPU_DMA_LATENCY, PM_QOS_DEFAULT_VALUE);
}
EXPORT_SYMBOL(lpm_qos_init);

void lpm_qos_exit(void)
{
	pm_qos_remove_request(&hypnus_qos_request);
}
EXPORT_SYMBOL(lpm_qos_exit);

void lpm_qos_disable_sleep(bool disable)
{
	if (disable)
		pm_qos_update_request(&hypnus_qos_request, 43);
	else
		pm_qos_update_request(&hypnus_qos_request, PM_QOS_DEFAULT_VALUE);
}
EXPORT_SYMBOL(lpm_qos_disable_sleep);

int hypnus_get_batt_capacity()
{
	union power_supply_propval ret = {0, };
	static struct power_supply *batt_psy;

	if (batt_psy == NULL)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy && batt_psy->desc->get_property)
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_CAPACITY, &ret);
	if (ret.intval >= 0 && ret.intval <=100)
		return ret.intval;
	else
		return DEFAULT_CAPACITY;
}
EXPORT_SYMBOL(hypnus_get_batt_capacity);

bool hypnus_get_charger_status()
{
	union power_supply_propval ret = {0,};
	static struct power_supply *usb_psy;

	if (usb_psy == NULL)
		usb_psy = power_supply_get_by_name("usb");
	if (usb_psy && usb_psy->desc->get_property)
		usb_psy->desc->get_property(usb_psy, POWER_SUPPLY_PROP_PRESENT, &ret);
	if (ret.intval)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(hypnus_get_charger_status);

/* sched */
void hypnus_get_nr_running_avg(int *avg, int *iowait_avg, int *big_avg)
{
	int max_nr, big_max_nr;
	sched_get_nr_running_avg(avg, iowait_avg, big_avg, &max_nr, &big_max_nr);
	*avg *= 100;
	*big_avg *= 100;
	*iowait_avg *= 100;
}
EXPORT_SYMBOL(hypnus_get_nr_running_avg);

int sched_set_prefer_idle(unsigned int is_prefer_idle)
{
	return 0;
}
EXPORT_SYMBOL(sched_set_prefer_idle);

int sched_set_small_task(int load_pct)
{
	return 0;
}
EXPORT_SYMBOL(sched_set_small_task);

unsigned int sched_get_small_task(void)
{
	return 10;
}
EXPORT_SYMBOL(sched_get_small_task);

int sched_set_cpu_mostly_idle_load(int cpu, int mostly_idle_pct)
{
	return 0;
}
EXPORT_SYMBOL(sched_set_cpu_mostly_idle_load);

int sched_get_cpu_mostly_idle_load(int cpu)
{
	return 10;
}
EXPORT_SYMBOL(sched_get_cpu_mostly_idle_load);

int sched_set_cpu_mostly_idle_nr_run(int cpu, int nr_run)
{
	return 0;
}
EXPORT_SYMBOL(sched_set_cpu_mostly_idle_nr_run);

int sched_get_cpu_mostly_idle_nr_run(int cpu)
{
	return 1;
}
EXPORT_SYMBOL(sched_get_cpu_mostly_idle_nr_run);
