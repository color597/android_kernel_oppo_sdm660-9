/* drivers/misc/lowmemorykiller.c
 *
 * The lowmemorykiller driver lets user-space specify a set of memory thresholds
 * where processes with a range of oom_score_adj values will get killed. Specify
 * the minimum oom_score_adj values in
 * /sys/module/lowmemorykiller/parameters/adj and the number of free pages in
 * /sys/module/lowmemorykiller/parameters/minfree. Both files take a comma
 * separated list of numbers in ascending order.
 *
 * For example, write "0,8" to /sys/module/lowmemorykiller/parameters/adj and
 * "1024,4096" to /sys/module/lowmemorykiller/parameters/minfree to kill
 * processes with a oom_score_adj value of 8 or higher when the free memory
 * drops below 4096 pages and kill processes with a oom_score_adj value of 0 or
 * higher when the free memory drops below 1024 pages.
 *
 * The driver considers memory used for caches to be free, but if a large
 * percentage of the cached memory is locked this can be very inaccurate
 * and processes may not get killed until the normal oom killer is triggered.
 *
 * Copyright (C) 2007-2008 Google, Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/swap.h>
#include <linux/rcupdate.h>
#include <linux/profile.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/cpuset.h>
#include <linux/vmpressure.h>
#include <linux/zcache.h>

#ifdef VENDOR_EDIT
/* fuzicheng@archermind.BSP 2019/08/23: dump KGSL_PAGE; */
#include <../../gpu/msm/kgsl.h>
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2016/05/31, Add for lowmemorykiller uevent
#include <linux/module.h>
#endif /* VENDOR_EDIT */

#define CREATE_TRACE_POINTS
#include <trace/events/almk.h>
#ifdef VENDOR_EDIT
/*yixue.ge@PSW.BSP.Kernel.Driver 20170808 modify for get some data about performance */
#include <linux/proc_fs.h>
#endif /*VENDOR_EDIT*/

#ifdef CONFIG_HIGHMEM
#define _ZONE ZONE_HIGHMEM
#else
#define _ZONE ZONE_NORMAL
#endif

#define CREATE_TRACE_POINTS
#include "trace/lowmemorykiller.h"
#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2017/06/29, Add for monitor memleak
#include "oppo_lowmemorymonitor.h"
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2016/05/31, Add for lowmemorykiller uevent
static struct kobject *lmk_module_kobj = NULL;
static struct work_struct lowmemorykiller_work;
static char *lmklowmem[2] = { "LMK=LOWMEM", NULL };
static int uevent_threshold[6] = {0, 0, 0, 0, }; // 1: 58, 2: 117, 3: 176
static int last_selected_adj = 0;
static void lowmemorykiller_uevent(short adj, int index);
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM 2018-03-12 modify for using aggressive for lowmem*/
static unsigned int agrlmk_swap_ratio1 = 8;
static unsigned int agrlmk_totalram_ratio = 5;
static bool agrlmk_enable = false;
module_param_named(agrlmk_swap_ratio1, agrlmk_swap_ratio1, uint, S_IRUGO | S_IWUSR);
module_param_named(agrlmk_totalram_ratio, agrlmk_totalram_ratio, uint, S_IRUGO | S_IWUSR);
module_param_named(agrlmk_enable, agrlmk_enable, bool, S_IRUGO | S_IWUSR);
#endif /*VENDOR_EDIT*/

static uint32_t lowmem_debug_level = 1;
static short lowmem_adj[6] = {
	0,
	1,
	6,
	12,
};
static int lowmem_adj_size = 4;
static int lowmem_minfree[6] = {
	3 * 512,	/* 6MB */
	2 * 1024,	/* 8MB */
	4 * 1024,	/* 16MB */
	16 * 1024,	/* 64MB */
};
static int lowmem_minfree_size = 4;
static int lmk_fast_run = 1;
#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM 2018-01-15 modify for lowmemkill count */
static bool lmk_cnt_enable = false;
static unsigned int almk_totalram_ratio = 6;
static unsigned long adaptive_lowmem_kill_count = 0;
static unsigned long tatal_lowmem_kill_count = 0;
module_param_named(almk_totalram_ratio, almk_totalram_ratio, uint, 0644);
#endif /*VENDOR_EDIT*/

static unsigned long lowmem_deathpending_timeout;

#define lowmem_print(level, x...)			\
	do {						\
		if (lowmem_debug_level >= (level))	\
			pr_info(x);			\
	} while (0)

static unsigned long lowmem_count(struct shrinker *s,
				  struct shrink_control *sc)
{
	return global_page_state(NR_ACTIVE_ANON) +
		global_page_state(NR_ACTIVE_FILE) +
		global_page_state(NR_INACTIVE_ANON) +
		global_page_state(NR_INACTIVE_FILE);
}

static atomic_t shift_adj = ATOMIC_INIT(0);
static short adj_max_shift = 353;
module_param_named(adj_max_shift, adj_max_shift, short,
                   S_IRUGO | S_IWUSR);

/* User knob to enable/disable adaptive lmk feature */
static int enable_adaptive_lmk;
module_param_named(enable_adaptive_lmk, enable_adaptive_lmk, int,
		   S_IRUGO | S_IWUSR);

/*
 * This parameter controls the behaviour of LMK when vmpressure is in
 * the range of 90-94. Adaptive lmk triggers based on number of file
 * pages wrt vmpressure_file_min, when vmpressure is in the range of
 * 90-94. Usually this is a pseudo minfree value, higher than the
 * highest configured value in minfree array.
 */
static int vmpressure_file_min;
module_param_named(vmpressure_file_min, vmpressure_file_min, int,
		   S_IRUGO | S_IWUSR);

enum {
	VMPRESSURE_NO_ADJUST = 0,
	VMPRESSURE_ADJUST_ENCROACH,
	VMPRESSURE_ADJUST_NORMAL,
};

int adjust_minadj(short *min_score_adj)
{
	int ret = VMPRESSURE_NO_ADJUST;

	if (!enable_adaptive_lmk)
		return 0;

	if (atomic_read(&shift_adj) &&
	    (*min_score_adj > adj_max_shift)) {
		if (*min_score_adj == OOM_SCORE_ADJ_MAX + 1)
			ret = VMPRESSURE_ADJUST_ENCROACH;
		else
			ret = VMPRESSURE_ADJUST_NORMAL;
		*min_score_adj = adj_max_shift;
#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM 2018-01-15 modify for adaptive lowmemkill count */
/*Maybe it can not select task to kill, it's just a rough number */
		if (lmk_cnt_enable)
			adaptive_lowmem_kill_count++;
	}
#endif /*VENDOR_EDIT*/
	atomic_set(&shift_adj, 0);

	return ret;
}

static int lmk_vmpressure_notifier(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	int other_free = 0, other_file = 0;
	unsigned long pressure = action;
	int array_size = ARRAY_SIZE(lowmem_adj);

	if (!enable_adaptive_lmk)
		return 0;

	if (pressure >= 95) {
		other_file = global_page_state(NR_FILE_PAGES) + zcache_pages() -
			global_page_state(NR_SHMEM) -
			total_swapcache_pages();
		other_free = global_page_state(NR_FREE_PAGES);
#ifdef VENDOR_EDIT
/*Huacai.Zhou@PSW.Tech.Kernel.Performance, 2019-02-18, do not kill precess when memory is greater than 1GB*/
		if ((other_free + other_file) <  totalram_pages/almk_totalram_ratio)
			atomic_set(&shift_adj, 1);
#else
		atomic_set(&shift_adj, 1);
#endif /*VENDOR_EDIT*/
		trace_almk_vmpressure(pressure, other_free, other_file);
	} else if (pressure >= 90) {
		if (lowmem_adj_size < array_size)
			array_size = lowmem_adj_size;
		if (lowmem_minfree_size < array_size)
			array_size = lowmem_minfree_size;

		other_file = global_page_state(NR_FILE_PAGES) + zcache_pages() -
			global_page_state(NR_SHMEM) -
			total_swapcache_pages();

		other_free = global_page_state(NR_FREE_PAGES);

		if ((other_free < lowmem_minfree[array_size - 1]) &&
		    (other_file < vmpressure_file_min)) {
			atomic_set(&shift_adj, 1);
			trace_almk_vmpressure(pressure, other_free, other_file);
		}
	} else if (atomic_read(&shift_adj)) {
		other_file = global_page_state(NR_FILE_PAGES) + zcache_pages() -
			global_page_state(NR_SHMEM) -
			total_swapcache_pages();
		other_free = global_page_state(NR_FREE_PAGES);

		/*
		 * shift_adj would have been set by a previous invocation
		 * of notifier, which is not followed by a lowmem_shrink yet.
		 * Since vmpressure has improved, reset shift_adj to avoid
		 * false adaptive LMK trigger.
		 */
		trace_almk_vmpressure(pressure, other_free, other_file);
		atomic_set(&shift_adj, 0);
	}

	return 0;
}

static struct notifier_block lmk_vmpr_nb = {
	.notifier_call = lmk_vmpressure_notifier,
};

static int test_task_flag(struct task_struct *p, int flag)
{
	struct task_struct *t;

	for_each_thread(p, t) {
		task_lock(t);
		if (test_tsk_thread_flag(t, flag)) {
			task_unlock(t);
			return 1;
		}
		task_unlock(t);
	}

	return 0;
}

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2015/06/17, Modify for 8939/16 5.1 for orphan task
static void orphan_foreground_task_kill(struct task_struct *task, short adj, short min_score_adj)
{
		if (min_score_adj == 0)
		return;

		if (task->parent->pid == 1 && adj == 0) {
		lowmem_print(1, "kill orphan foreground task %s, pid %d, adj %hd, min_score_adj %hd\n",
			task->comm, task->pid, adj, min_score_adj);
		send_sig(SIGKILL, task, 0);
		}
}
#endif /* VENDOR_EDIT */

static int test_task_state(struct task_struct *p, int state)
{
	struct task_struct *t;

	for_each_thread(p, t) {
		task_lock(t);
		if (t->state & state) {
			task_unlock(t);
			return 1;
		}
		task_unlock(t);
	}

	return 0;
}

static DEFINE_MUTEX(scan_mutex);

int can_use_cma_pages(gfp_t gfp_mask)
{
	int can_use = 0;
	int mtype = gfpflags_to_migratetype(gfp_mask);
	int i = 0;
	int *mtype_fallbacks = get_migratetype_fallbacks(mtype);

	if (is_migrate_cma(mtype)) {
		can_use = 1;
	} else {
		for (i = 0;; i++) {
			int fallbacktype = mtype_fallbacks[i];

			if (is_migrate_cma(fallbacktype)) {
				can_use = 1;
				break;
			}

			if (fallbacktype == MIGRATE_TYPES)
				break;
		}
	}
	return can_use;
}

void tune_lmk_zone_param(struct zonelist *zonelist, int classzone_idx,
					int *other_free, int *other_file,
					int use_cma_pages)
{
	struct zone *zone;
	struct zoneref *zoneref;
	int zone_idx;

	for_each_zone_zonelist(zone, zoneref, zonelist, MAX_NR_ZONES) {
		zone_idx = zonelist_zone_idx(zoneref);
		if (zone_idx == ZONE_MOVABLE) {
			if (!use_cma_pages && other_free)
				*other_free -=
				    zone_page_state(zone, NR_FREE_CMA_PAGES);
			continue;
		}

		if (zone_idx > classzone_idx) {
			if (other_free != NULL)
				*other_free -= zone_page_state(zone,
							       NR_FREE_PAGES);
			if (other_file != NULL)
				*other_file -= zone_page_state(zone,
							       NR_FILE_PAGES)
					- zone_page_state(zone, NR_SHMEM)
					- zone_page_state(zone, NR_SWAPCACHE);
		} else if (zone_idx < classzone_idx) {
			if (zone_watermark_ok(zone, 0, 0, classzone_idx, 0) &&
			    other_free) {
				if (!use_cma_pages) {
					*other_free -= min(
					  zone->lowmem_reserve[classzone_idx] +
					  zone_page_state(
					    zone, NR_FREE_CMA_PAGES),
					  zone_page_state(
					    zone, NR_FREE_PAGES));
				} else {
					*other_free -=
					  zone->lowmem_reserve[classzone_idx];
				}
			} else {
				if (other_free)
					*other_free -=
					  zone_page_state(zone, NR_FREE_PAGES);
			}
		}
	}
}

#ifdef CONFIG_HIGHMEM
void adjust_gfp_mask(gfp_t *gfp_mask)
{
	struct zone *preferred_zone;
	struct zonelist *zonelist;
	enum zone_type high_zoneidx;

	if (current_is_kswapd()) {
		zonelist = node_zonelist(0, *gfp_mask);
		high_zoneidx = gfp_zone(*gfp_mask);
		first_zones_zonelist(zonelist, high_zoneidx, NULL,
				     &preferred_zone);

		if (high_zoneidx == ZONE_NORMAL) {
			if (zone_watermark_ok_safe(
					preferred_zone, 0,
					high_wmark_pages(preferred_zone), 0))
				*gfp_mask |= __GFP_HIGHMEM;
		} else if (high_zoneidx == ZONE_HIGHMEM) {
			*gfp_mask |= __GFP_HIGHMEM;
		}
	}
}
#else
void adjust_gfp_mask(gfp_t *unused)
{
}
#endif

void tune_lmk_param(int *other_free, int *other_file, struct shrink_control *sc)
{
	gfp_t gfp_mask;
	struct zone *preferred_zone;
	struct zonelist *zonelist;
	enum zone_type high_zoneidx, classzone_idx;
	unsigned long balance_gap;
	int use_cma_pages;

	gfp_mask = sc->gfp_mask;
	adjust_gfp_mask(&gfp_mask);

	zonelist = node_zonelist(0, gfp_mask);
	high_zoneidx = gfp_zone(gfp_mask);
	first_zones_zonelist(zonelist, high_zoneidx, NULL, &preferred_zone);
	classzone_idx = zone_idx(preferred_zone);
	use_cma_pages = can_use_cma_pages(gfp_mask);

	balance_gap = min(low_wmark_pages(preferred_zone),
			  (preferred_zone->present_pages +
			   KSWAPD_ZONE_BALANCE_GAP_RATIO-1) /
			   KSWAPD_ZONE_BALANCE_GAP_RATIO);

	if (likely(current_is_kswapd() && zone_watermark_ok(preferred_zone, 0,
			  high_wmark_pages(preferred_zone) + SWAP_CLUSTER_MAX +
			  balance_gap, 0, 0))) {
		if (lmk_fast_run)
			tune_lmk_zone_param(zonelist, classzone_idx, other_free,
				       other_file, use_cma_pages);
		else
			tune_lmk_zone_param(zonelist, classzone_idx, other_free,
				       NULL, use_cma_pages);

		if (zone_watermark_ok(preferred_zone, 0, 0, _ZONE, 0)) {
			if (!use_cma_pages) {
				*other_free -= min(
				  preferred_zone->lowmem_reserve[_ZONE]
				  + zone_page_state(
				    preferred_zone, NR_FREE_CMA_PAGES),
				  zone_page_state(
				    preferred_zone, NR_FREE_PAGES));
			} else {
				*other_free -=
				  preferred_zone->lowmem_reserve[_ZONE];
			}
		} else {
			*other_free -= zone_page_state(preferred_zone,
						      NR_FREE_PAGES);
		}

		lowmem_print(4, "lowmem_shrink of kswapd tunning for highmem "
			     "ofree %d, %d\n", *other_free, *other_file);
	} else {
		tune_lmk_zone_param(zonelist, classzone_idx, other_free,
			       other_file, use_cma_pages);

		if (!use_cma_pages) {
			*other_free -=
			  zone_page_state(preferred_zone, NR_FREE_CMA_PAGES);
		}

		lowmem_print(4, "lowmem_shrink tunning for others ofree %d, "
			     "%d\n", *other_free, *other_file);
	}
}


#ifdef VENDOR_EDIT
/*yixue.ge@PSW.BSP.Kernel.Driver 20170808 modify for get some data about performance */
static ssize_t lowmem_kill_count_proc_read(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	if (!lmk_cnt_enable)
		return 0;

	len = sprintf(&page[len],"adaptive_lowmem_kill_count:%lu\ntotal_lowmem_kill_count:%lu\n",
				adaptive_lowmem_kill_count, tatal_lowmem_kill_count);

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

struct file_operations lowmem_kill_count_proc_fops = {
	.read = lowmem_kill_count_proc_read,
};

static int __init setup_lowmem_killinfo(void)
{

	proc_create("lowmemkillcounts", S_IRUGO, NULL, &lowmem_kill_count_proc_fops);
	return 0;
}
module_init(setup_lowmem_killinfo);
#endif /* VENDOR_EDIT */

static unsigned long lowmem_scan(struct shrinker *s, struct shrink_control *sc)
{
	struct task_struct *tsk;
	struct task_struct *selected = NULL;
	unsigned long rem = 0;
	int tasksize;
	int i;
	int ret = 0;
	short min_score_adj = OOM_SCORE_ADJ_MAX + 1;
	int minfree = 0;
	int selected_tasksize = 0;
	short selected_oom_score_adj;
	int array_size = ARRAY_SIZE(lowmem_adj);
	int other_free;
	int other_file;

#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM, 2019/12/03, add aggressive lmk to speed up memory free*/
	unsigned long swap_pages = atomic_long_read(&nr_swap_pages);
	int to_be_aggressive = 0;
	short amr_adj = OOM_SCORE_ADJ_MAX + 1;
#if defined(CONFIG_SWAP)
		unsigned long swap_orig_nrpages;
		unsigned long swap_comp_nrpages;
		int swap_rss;
		int selected_swap_rss;
		int orig_tasksize;

		swap_orig_nrpages = get_swap_orig_data_nrpages();
		swap_comp_nrpages = get_swap_comp_pool_nrpages();
#endif
#endif /*VENDOR_EDIT*/
	if (!mutex_trylock(&scan_mutex))
		return 0;

	other_free = global_page_state(NR_FREE_PAGES);

	if (global_page_state(NR_SHMEM) + total_swapcache_pages() <
		global_page_state(NR_FILE_PAGES) + zcache_pages())
		other_file = global_page_state(NR_FILE_PAGES) + zcache_pages() -
						global_page_state(NR_SHMEM) -
						global_page_state(NR_UNEVICTABLE) -
						total_swapcache_pages();
	else
		other_file = 0;

	tune_lmk_param(&other_free, &other_file, sc);
#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM, 2019/12/03, add aggressive lmk to speed up memory free*/
		if (agrlmk_enable &&
			((other_free + other_file) < totalram_pages/agrlmk_totalram_ratio)) {
				if ((swap_pages * agrlmk_swap_ratio1 < total_swap_pages))
					to_be_aggressive++;
			}

		i = lowmem_adj_size - 1 - to_be_aggressive;
		if (to_be_aggressive > 0 && i >= 0)
			amr_adj = lowmem_adj[i];
#endif /*VENDOR_EDIT*/

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;
	if (lowmem_minfree_size < array_size)
		array_size = lowmem_minfree_size;
	for (i = 0; i < array_size; i++) {
		minfree = lowmem_minfree[i];
		if (other_free < minfree && other_file < minfree) {
#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM, 2019/12/03, add aggressive lmk to speed up memory free*/
	if (to_be_aggressive != 0 && i > 1) {
		i -= to_be_aggressive;
		if (i < 1)
			i = 1;
	}
#endif /*VENDOR_EDIT*/
			min_score_adj = lowmem_adj[i];
			break;
		}
	}

	ret = adjust_minadj(&min_score_adj);

#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM, 2019/12/03, add aggressive lmk to speed up memory free*/
	min_score_adj = min(min_score_adj, amr_adj);
#endif /*VENDOR_EDIT*/
	lowmem_print(3, "lowmem_scan %lu, %x, ofree %d %d, ma %hd\n",
			sc->nr_to_scan, sc->gfp_mask, other_free,
			other_file, min_score_adj);

	if (min_score_adj == OOM_SCORE_ADJ_MAX + 1) {
		trace_almk_shrink(0, ret, other_free, other_file, 0);
		lowmem_print(5, "lowmem_scan %lu, %x, return 0\n",
			     sc->nr_to_scan, sc->gfp_mask);
		mutex_unlock(&scan_mutex);
		return 0;
	}

	selected_oom_score_adj = min_score_adj;

	rcu_read_lock();
	for_each_process(tsk) {
		struct task_struct *p;
		short oom_score_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		/* if task no longer has any memory ignore it */
		if (test_task_flag(tsk, TIF_MM_RELEASED))
			continue;

		if (time_before_eq(jiffies, lowmem_deathpending_timeout)) {
			if (test_task_flag(tsk, TIF_MEMDIE)) {
				rcu_read_unlock();
				mutex_unlock(&scan_mutex);
				return 0;
			}
		}

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2016/01/06, Add for D status process issue
		if (p->state & TASK_UNINTERRUPTIBLE) {
			task_unlock(p);
			continue;
		}
		//resolve kill coredump process, it may continue long time
		if (p->signal != NULL && (p->signal->flags & SIGNAL_GROUP_COREDUMP) ){
			task_unlock(p);
			continue;
		}
#endif /* VENDOR_EDIT */

		oom_score_adj = p->signal->oom_score_adj;
		if (oom_score_adj < min_score_adj) {
#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2015/06/17, Modify for 8939/16 5.1 for orphan task
			tasksize = get_mm_rss(p->mm);
#endif /* VENDOR_EIDT */
			task_unlock(p);
#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2015/06/17, Modify for 8939/16 5.1 for orphan task
			if (tasksize > 0) {
				orphan_foreground_task_kill(p, oom_score_adj, min_score_adj);
			}
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2017/06/29, Add for monitor memleak
			oppo_lowmemory_detect(p, tasksize);
#endif /* VENDOR_EIDT */
			continue;
		}
		tasksize = get_mm_rss(p->mm);
#if defined(VENDOR_EDIT) && defined(CONFIG_SWAP)
/*Huacai.Zhou@Tech.Kernel.MM, 2020-01-13,add swap rss for lowmemkiller*/
		swap_rss = get_mm_counter(p->mm, MM_SWAPENTS) *
				swap_comp_nrpages / swap_orig_nrpages;
		tasksize += swap_rss;
#endif
		task_unlock(p);
		if (tasksize <= 0)
			continue;
#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2017/06/29, Add for monitor memleak
		if (oppo_lowmemory_detect(p, tasksize)) {
			continue;
		}
#endif /* VENDOR_EDIT */
		if (selected) {
			if (oom_score_adj < selected_oom_score_adj)
				continue;
			if (oom_score_adj == selected_oom_score_adj &&
			    tasksize <= selected_tasksize)
				continue;
		}
		selected = p;
		selected_tasksize = tasksize;
#if defined(VENDOR_EDIT) && defined(CONFIG_SWAP)
/*Huacai.Zhou@Tech.Kernel.MM, 2020-01-13,add swap rss for lowmemkiller*/
		selected_swap_rss = swap_rss;
		orig_tasksize = selected_tasksize - selected_swap_rss;
#endif
		selected_oom_score_adj = oom_score_adj;
		lowmem_print(3, "select '%s' (%d), adj %hd, size %d, to kill\n",
			     p->comm, p->pid, oom_score_adj, tasksize);
	}
	if (selected) {
		long cache_size, cache_limit, free;

		if (test_task_flag(selected, TIF_MEMDIE) &&
		    (test_task_state(selected, TASK_UNINTERRUPTIBLE))) {
			lowmem_print(2, "'%s' (%d) is already killed\n",
				     selected->comm,
				     selected->pid);
			rcu_read_unlock();
			mutex_unlock(&scan_mutex);
			return 0;
		}

		task_lock(selected);
		send_sig(SIGKILL, selected, 0);
		/*
		 * FIXME: lowmemorykiller shouldn't abuse global OOM killer
		 * infrastructure. There is no real reason why the selected
		 * task should have access to the memory reserves.
		 */
		if (selected->mm)
			mark_oom_victim(selected);
		task_unlock(selected);
		cache_size = other_file * (long)(PAGE_SIZE / 1024);
		cache_limit = minfree * (long)(PAGE_SIZE / 1024);
		free = other_free * (long)(PAGE_SIZE / 1024);
		trace_lowmemory_kill(selected, cache_size, cache_limit, free);
#ifdef VENDOR_EDIT
/*yixue.ge@PSW.BSP.Kernel.Driver 20170808 modify for get some data about performance */
		if (lmk_cnt_enable)
			tatal_lowmem_kill_count++;
#endif /* VENDOR_EDIT */
		lowmem_print(1, "Killing '%s' (%d) (tgid %d), adj %hd,\n" \
//#if defined(VENDOR_EDIT) && defined(CONFIG_SWAP)
/*Huacai.Zhou@Tech.Kernel.MM, 2020-01-13,add swap rss for lowmemkiller*/
			        "   to free %ldkB (%ldKB %ldKB) on behalf of '%s' (%d) because\n" \
//#endif
			        "   cache %ldkB is below limit %ldkB for oom_score_adj %hd\n" \
//#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM, 2019/12/03, add aggressive lmk to speed up memory free*/
				"   (decrease %d level\n" \
//#endif /* VENDOR_EDIT */
				"   Free memory is %ldkB above reserved.\n" \
				"   Free CMA is %ldkB\n" \
				"   Total reserve is %ldkB\n" \
				"   Total free pages is %ldkB\n" \
				"   Total file cache is %ldkB\n" \
				"   Total zcache is %ldkB\n" \
				"   GFP mask is 0x%x\n",
			     selected->comm, selected->pid, selected->tgid,
			     selected_oom_score_adj,
#if defined(VENDOR_EDIT) && defined(CONFIG_SWAP)
/*Huacai.Zhou@Tech.Kernel.MM, 2020-01-13,add swap rss for lowmemkiller*/
			     selected_tasksize * (long)(PAGE_SIZE / 1024),
			     orig_tasksize * (long)(PAGE_SIZE / 1024),
			     selected_swap_rss * (long)(PAGE_SIZE / 1024),
#else
			     selected_tasksize * (long)(PAGE_SIZE / 1024),
#endif
			     current->comm, current->pid,
			     cache_size, cache_limit,
			     min_score_adj,
#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM, 2019/12/03, add aggressive lmk to speed up memory free*/
				 to_be_aggressive,
#endif /* VENDOR_EDIT */
			     free,
			     global_page_state(NR_FREE_CMA_PAGES) *
				(long)(PAGE_SIZE / 1024),
			     totalreserve_pages * (long)(PAGE_SIZE / 1024),
			     global_page_state(NR_FREE_PAGES) *
				(long)(PAGE_SIZE / 1024),
			     global_page_state(NR_FILE_PAGES) *
				(long)(PAGE_SIZE / 1024),
			     (long)zcache_pages() * (long)(PAGE_SIZE / 1024),
			     sc->gfp_mask);

#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM. 2018/01/15, modify for show more meminfo*/
			show_mem(SHOW_MEM_FILTER_NODES);
			/* fuzicheng@archermind.BSP 2019/08/23: dump KGSL_PAGE; */
			lowmem_print(1, "kgsl driver page_alloc test: KGSL_PAGE_ALLOC = %ld\n", atomic_long_read(&kgsl_driver.stats.page_alloc));
			/* yinchao@archermind.BSP 2019/08/20: dump tasks for debug; */
			if (selected_oom_score_adj <= 300)
				dump_tasks(NULL, NULL);
#endif /*VENDOR_EDIT*/

		if (lowmem_debug_level >= 2 && selected_oom_score_adj == 0) {
#ifndef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM. 2018/01/15, modify for show more meminfo*/
			show_mem(SHOW_MEM_FILTER_NODES);
#endif /*VENDOR_EDIT*/
			dump_tasks(NULL, NULL);
		}

		lowmem_deathpending_timeout = jiffies + HZ;

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2016/05/31, Add for lowmemorykiller uevent
		if (selected_oom_score_adj == 0) {
			lowmem_print(1, "Killing %s, adj is %hd, so send uevent to userspace\n",
					selected->comm, selected_oom_score_adj);
			schedule_work(&lowmemorykiller_work);
		} else {
			for (i = 1; i < 3; i++) {
				if (selected_oom_score_adj == lowmem_adj[i]) {
					//uevent must be continuous adj record
					if (last_selected_adj != selected_oom_score_adj) {
						last_selected_adj = selected_oom_score_adj;
						uevent_threshold[i] = 0;
						break;
					}
					uevent_threshold[i]++;
					if (uevent_threshold[i] == i * 5) {
						lowmemorykiller_uevent(selected_oom_score_adj, i);
						uevent_threshold[i] = 0;
					}
					break;
				}
			}
		}
#endif /* VENDOR_EDIT */

		rem += selected_tasksize;
		rcu_read_unlock();
		/* give the system time to free up the memory */
		msleep_interruptible(20);
		trace_almk_shrink(selected_tasksize, ret,
				  other_free, other_file,
				  selected_oom_score_adj);
	} else {
		trace_almk_shrink(1, ret, other_free, other_file, 0);
		rcu_read_unlock();
	}

	lowmem_print(4, "lowmem_scan %lu, %x, return %lu\n",
		     sc->nr_to_scan, sc->gfp_mask, rem);
	mutex_unlock(&scan_mutex);
	return rem;
}

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2016/05/31, Add for lowmemorykiller uevent
void lowmemorykiller_work_func(struct work_struct *work)
{
	kobject_uevent_env(lmk_module_kobj, KOBJ_CHANGE, lmklowmem);
	lowmem_print(1, "lowmemorykiller send uevent: %s\n", lmklowmem[0]);
}
static void lowmemorykiller_uevent(short adj, int index)
{
	lowmem_print(1, "kill adj %hd more than %d times and so send uevent to userspace\n", adj, index * 5);
	schedule_work(&lowmemorykiller_work);
}
#endif /* VENDOR_EDIT */

static struct shrinker lowmem_shrinker = {
	.scan_objects = lowmem_scan,
	.count_objects = lowmem_count,
	.seeks = DEFAULT_SEEKS * 16,
	.flags = SHRINKER_LMK
};

static int __init lowmem_init(void)
{
	register_shrinker(&lowmem_shrinker);
	vmpressure_notifier_register(&lmk_vmpr_nb);

#ifdef VENDOR_EDIT
//Jiemin.Zhu@PSW.AD.Performance.Memory.1139862, 2016/05/31, Add for lowmemorykiller uevent
	lmk_module_kobj = kset_find_obj(module_kset, KBUILD_MODNAME);
	lowmem_print(1, "kernel obj name %s\n", lmk_module_kobj->name);
	INIT_WORK(&lowmemorykiller_work, lowmemorykiller_work_func);
#endif /* VENDOR_EDIT */

	return 0;
}
device_initcall(lowmem_init);

#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER_AUTODETECT_OOM_ADJ_VALUES
static short lowmem_oom_adj_to_oom_score_adj(short oom_adj)
{
	if (oom_adj == OOM_ADJUST_MAX)
		return OOM_SCORE_ADJ_MAX;
	else
		return (oom_adj * OOM_SCORE_ADJ_MAX) / -OOM_DISABLE;
}

static void lowmem_autodetect_oom_adj_values(void)
{
	int i;
	short oom_adj;
	short oom_score_adj;
	int array_size = ARRAY_SIZE(lowmem_adj);

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;

	if (array_size <= 0)
		return;

	oom_adj = lowmem_adj[array_size - 1];
	if (oom_adj > OOM_ADJUST_MAX)
		return;

	oom_score_adj = lowmem_oom_adj_to_oom_score_adj(oom_adj);
	if (oom_score_adj <= OOM_ADJUST_MAX)
		return;

	lowmem_print(1, "lowmem_shrink: convert oom_adj to oom_score_adj:\n");
	for (i = 0; i < array_size; i++) {
		oom_adj = lowmem_adj[i];
		oom_score_adj = lowmem_oom_adj_to_oom_score_adj(oom_adj);
		lowmem_adj[i] = oom_score_adj;
		lowmem_print(1, "oom_adj %d => oom_score_adj %d\n",
			     oom_adj, oom_score_adj);
	}
}

static int lowmem_adj_array_set(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_array_ops.set(val, kp);

	/* HACK: Autodetect oom_adj values in lowmem_adj array */
	lowmem_autodetect_oom_adj_values();

	return ret;
}

static int lowmem_adj_array_get(char *buffer, const struct kernel_param *kp)
{
	return param_array_ops.get(buffer, kp);
}

static void lowmem_adj_array_free(void *arg)
{
	param_array_ops.free(arg);
}

static struct kernel_param_ops lowmem_adj_array_ops = {
	.set = lowmem_adj_array_set,
	.get = lowmem_adj_array_get,
	.free = lowmem_adj_array_free,
};

static const struct kparam_array __param_arr_adj = {
	.max = ARRAY_SIZE(lowmem_adj),
	.num = &lowmem_adj_size,
	.ops = &param_ops_short,
	.elemsize = sizeof(lowmem_adj[0]),
	.elem = lowmem_adj,
};
#endif

/*
 * not really modular, but the easiest way to keep compat with existing
 * bootargs behaviour is to continue using module_param here.
 */
module_param_named(cost, lowmem_shrinker.seeks, int, S_IRUGO | S_IWUSR);
#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER_AUTODETECT_OOM_ADJ_VALUES
module_param_cb(adj, &lowmem_adj_array_ops,
		.arr = &__param_arr_adj,
		S_IRUGO | S_IWUSR);
__MODULE_PARM_TYPE(adj, "array of short");
#else
module_param_array_named(adj, lowmem_adj, short, &lowmem_adj_size,
			 S_IRUGO | S_IWUSR);
#endif
module_param_array_named(minfree, lowmem_minfree, uint, &lowmem_minfree_size,
			 S_IRUGO | S_IWUSR);
module_param_named(debug_level, lowmem_debug_level, uint, S_IRUGO | S_IWUSR);
module_param_named(lmk_fast_run, lmk_fast_run, int, S_IRUGO | S_IWUSR);

#ifdef VENDOR_EDIT
/*huacai.zhou@PSW.BSP.Kernel.MM 2018-01-15 modify for lowmemkill count */
module_param_named(lmk_cnt_enable, lmk_cnt_enable, bool, S_IRUGO | S_IWUSR);
#endif /*VENDOR_EDIT*/
