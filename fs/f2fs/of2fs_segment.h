/***********************************************************
** Copyright (C), 2008-2019, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - of2fs_segment.h
** Description: oppo f2fs gc code
**
** Version: 1.0
** Date : 2019/06/28
** Author: yanwu@TECH.Storage.FS, add code for gc optimization
**
** ------------------ Revision History:------------------------
** <author> <data> <version > <desc>
** yanwu 2019/06/28 1.0 add gc optimization code
****************************************************************/

#ifndef _OF2FS_SEGMENT_H
#define _OF2FS_SEGMENT_H
#include <linux/random.h>
#include <linux/atomic.h>
#define DEF_DIRTY_STAT_INTERVAL 15 /* 15 secs */
static bool need_balance_dirty(struct f2fs_sb_info *sbi)
{
#ifdef CONFIG_F2FS_BD_STAT
	struct dirty_seglist_info *dirty_i = DIRTY_I(sbi);
	struct timespec ts = {DEF_DIRTY_STAT_INTERVAL, 0};
	unsigned long interval = timespec_to_jiffies(&ts);
	struct f2fs_bigdata_info *bd = F2FS_BD_STAT(sbi);
	int i;
	int dirty_node = 0, dirty_data = 0, all_dirty;
	long node_cnt, data_cnt;

	if (time_before(jiffies, bd->ssr_last_jiffies + interval))
		return false;

	for (i = CURSEG_HOT_DATA; i <= CURSEG_COLD_DATA; i++)
		dirty_data += dirty_i->nr_dirty[i];
	for (i = CURSEG_HOT_NODE; i <= CURSEG_COLD_NODE; i++)
		dirty_node += dirty_i->nr_dirty[i];
	all_dirty = dirty_data + dirty_node;
	if (all_dirty == 0)
		return false;

	/* how many blocks are consumed during this interval */
	bd_lock(sbi);
	node_cnt = (long)(bd->curr_node_alloc_count - bd->last_node_alloc_count);
	data_cnt = (long)(bd->curr_data_alloc_count - bd->last_data_alloc_count);
	bd->last_node_alloc_count = bd->curr_node_alloc_count;
	bd->last_data_alloc_count = bd->curr_data_alloc_count;
	bd->ssr_last_jiffies = jiffies;
	bd_unlock(sbi);

	if (dirty_data < reserved_sections(sbi) &&
		data_cnt > (long)sbi->blocks_per_seg) {
		int randnum = prandom_u32_max(100);
		int ratio = dirty_data * 100 / all_dirty;
		if (randnum > ratio)
			return true;
	}

	if (dirty_node < reserved_sections(sbi) &&
		node_cnt > (long)sbi->blocks_per_seg) {
		int randnum = prandom_u32_max(100);
		int ratio = dirty_node * 100 / all_dirty;
		if (randnum > ratio)
			return true;
	}
#endif
	return false;
}

static inline void of2fs_balance_fs(struct f2fs_sb_info *sbi)
{
	if (!gc_dc_opt) {
		if (has_not_enough_free_secs(sbi, 0, 0)) {
			mutex_lock(&sbi->gc_mutex);
			f2fs_gc(sbi, false, false, NULL_SEGNO);
		}
		return;
	}
	if (has_not_enough_free_secs(sbi, 0, 0)) {
		struct f2fs_gc_kthread *gc_th = sbi->gc_thread;
		if (gc_th != NULL) {
			DEFINE_WAIT(__wait);
			prepare_to_wait(&gc_th->fggc_wait_queue,
				&__wait, TASK_UNINTERRUPTIBLE);
			wake_up(&gc_th->gc_wait_queue_head);
			schedule();
			finish_wait(&gc_th->fggc_wait_queue, &__wait);
		} else {
			mutex_lock(&sbi->gc_mutex);
			f2fs_gc(sbi, false, false, NULL_SEGNO);
		}
	} else if (f2fs_need_SSR(sbi) && need_balance_dirty(sbi)) {
		struct f2fs_gc_kthread *gc_th = sbi->gc_thread;
		if (gc_th != NULL) {
			atomic_inc(&sbi->need_ssr_gc);
			wake_up(&gc_th->gc_wait_queue_head);
		} else {
			mutex_lock(&sbi->gc_mutex);
			f2fs_gc(sbi, true, false, NULL_SEGNO);
		}
	}
}
#endif
