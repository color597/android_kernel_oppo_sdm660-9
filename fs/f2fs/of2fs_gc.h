/***********************************************************
** Copyright (C), 2008-2019, OPPO Mobile Comm Corp., Ltd.
** VENDOR_EDIT
** File: - of2fs_gc.h
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

#ifndef _OF2FS_GC_H
#define _OF2FS_GC_H
#define MIN_WAIT_MS 1000
#define DEF_GC_BALANCE_MIN_SLEEP_TIME   10000	/* milliseconds */
#define DEF_GC_FRAG_MIN_SLEEP_TIME      2000	/* milliseconds */
#define GC_URGENT_CHECK_TIME    (10*60*1000)	/* milliseconds */
#define GC_URGENT_DISABLE_BLOCKS        (16<<18)    /* 16G */
#define GC_URGENT_DISABLE_FREE_BLOCKS	(10<<18)    /* 10G */

extern int find_next_free_extent(const unsigned long *addr,
			  unsigned long size, unsigned long *offset);

static inline bool __is_frag_urgent(struct f2fs_sb_info *sbi)
{
	block_t total_blocks, valid_blocks;
	block_t free_hist[10], total = 0;
	unsigned int i;

	total_blocks = le64_to_cpu(sbi->raw_super->block_count);
	valid_blocks = valid_user_blocks(sbi);
	if (total_blocks < GC_URGENT_DISABLE_BLOCKS ||
		total_blocks - valid_blocks > GC_URGENT_DISABLE_FREE_BLOCKS)
		return false;

	memset(free_hist, 0, sizeof(free_hist));
	for (i = 0; i < MAIN_SEGS(sbi); i++) {
		struct seg_entry *se = get_seg_entry(sbi, i);
		unsigned long start = 0;
		int blks, idx;

		if (se->valid_blocks == 0 || se->valid_blocks == sbi->blocks_per_seg)
			continue;

		while (start < sbi->blocks_per_seg) {
			blks = find_next_free_extent((unsigned long *)se->cur_valid_map,
						       sbi->blocks_per_seg,
						       &start);
			if (unlikely(blks < 0))
				break;

			idx = ilog2(blks);
			if (idx < ARRAY_SIZE(free_hist)) {
				free_hist[idx] += blks;
			}
			total += blks;
		}
		cond_resched();
	}
	printk("Extent Size Range: Free Blocks\n");
	printk("4K: %u\n", free_hist[0]);
	for (i = 1; i < ARRAY_SIZE(free_hist); i++)
		printk("%dK-%dK: %u\n", 2<<i, 2<<(i+1), free_hist[i]);
	return (free_hist[0] + free_hist[1]) >= (total >> 1);
}

static inline bool is_frag_urgent(struct f2fs_sb_info *sbi)
{
	unsigned long next_check = sbi->last_frag_check +
		msecs_to_jiffies(GC_URGENT_CHECK_TIME);
	if (time_after(jiffies, next_check)) {
		sbi->last_frag_check = jiffies;
		sbi->is_frag = __is_frag_urgent(sbi);
	}
	return sbi->is_frag;
}

/*
 * GC tuning ratio [0, 100] in performance mode
 */
static inline int gc_perf_ratio(struct f2fs_sb_info *sbi)
{
	block_t reclaimable_user_blocks = sbi->user_block_count -
						written_block_count(sbi);
	return reclaimable_user_blocks == 0 ? 100 :
			100ULL * free_user_blocks(sbi) / reclaimable_user_blocks;
}

/* invaild blocks is more than 10% of total free space */
static inline bool is_invaild_blocks_enough(struct f2fs_sb_info *sbi)
{
	block_t reclaimable_user_blocks = sbi->user_block_count -
						written_block_count(sbi);

	return free_user_blocks(sbi) / 90 <  reclaimable_user_blocks / 100;
}

static inline bool is_gc_frag(struct f2fs_sb_info *sbi)
{
	return is_frag_urgent(sbi) &&
		free_segments(sbi) < 3 * overprovision_segments(sbi) &&
		is_invaild_blocks_enough(sbi);
}

static inline bool is_gc_perf(struct f2fs_sb_info *sbi)
{
	return gc_perf_ratio(sbi) < 10 &&
		free_segments(sbi) < 3 * overprovision_segments(sbi);
}

/* more than 90% of main area are valid blocks */
static inline bool is_gc_lifetime(struct f2fs_sb_info *sbi)
{
	return written_block_count(sbi) / 90 > sbi->user_block_count / 100;
}

static inline bool of2fs_gc_thread_wait(struct f2fs_sb_info *sbi, unsigned int *wait_ms)
{
	unsigned int min_wait_ms;
	struct f2fs_gc_kthread *gc_th = sbi->gc_thread;
	wait_queue_head_t *wq = &gc_th->gc_wait_queue_head;
	wait_queue_head_t *fggc_wait_queue = &gc_th->fggc_wait_queue;
	if (!gc_dc_opt) {
		wait_event_interruptible_timeout(*wq,
				kthread_should_stop() || freezing(current) ||
				gc_th->gc_wake,
				msecs_to_jiffies(*wait_ms));
		return false;
	}

	if (is_gc_frag(sbi)) {
		*wait_ms = DEF_GC_FRAG_MIN_SLEEP_TIME;
	} else if (is_gc_lifetime(sbi)) {
		gc_th->min_sleep_time = DEF_GC_THREAD_MIN_SLEEP_TIME;
	} else if (is_gc_perf(sbi)) {
		*wait_ms = max(DEF_GC_THREAD_MAX_SLEEP_TIME *
				gc_perf_ratio(sbi) / 100, MIN_WAIT_MS);
	} else {
		gc_th->min_sleep_time = DEF_GC_BALANCE_MIN_SLEEP_TIME;
	}
	min_wait_ms = f2fs_time_to_wait(sbi, REQ_TIME);
	if (*wait_ms < min_wait_ms)
		*wait_ms = min_wait_ms;

	wait_event_interruptible_timeout(*wq,
			kthread_should_stop() || freezing(current) ||
			waitqueue_active(fggc_wait_queue) ||
			atomic_read(&sbi->need_ssr_gc) > 0 ||
			gc_th->gc_wake,
			msecs_to_jiffies(*wait_ms));
	if (atomic_read(&sbi->need_ssr_gc) > 0) {
		mutex_lock(&sbi->gc_mutex);
		f2fs_gc(sbi, true, false, NULL_SEGNO);
		atomic_sub(1, &sbi->need_ssr_gc);
		if (!has_not_enough_free_secs(sbi, 0, 0) &&
				waitqueue_active(fggc_wait_queue)) {
			wake_up_all(&gc_th->fggc_wait_queue);
		}
		return true;
	} else if (waitqueue_active(fggc_wait_queue)) {
		mutex_lock(&sbi->gc_mutex);
		f2fs_gc(sbi, false, false, NULL_SEGNO);
		wake_up_all(&gc_th->fggc_wait_queue);
		return true;
	}
	return false;
}
#endif
