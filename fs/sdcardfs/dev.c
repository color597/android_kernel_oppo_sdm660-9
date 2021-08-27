/************************************************************
 * Copyright 2017 OPPO Mobile Comm Corp., Ltd.
 * All rights reserved.
 *
 * Description     : record /sdcard/DCIM/Camera and Screenshots, and send uevent
 *
 *
 ** Version: 1
 ** Date created: 2016/01/06
 ** Author: Jiemin.Zhu@AD.Android.SdcardFs
 ** ------------------------------- Revision History: ---------------------------------------
 **        <author>      <data>           <desc>
 **      Jiemin.Zhu    2017/12/12    create this file
 **      Jiemin.Zhu    2018/08/08    modify for adding more protected directorys
 ************************************************************/
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

#include "sdcardfs.h"

static DEFINE_MUTEX(dcim_mutex);
static struct kobject *dcim_kobject;
#define MAX_EVENT_PARAM    5
#define MAX_SKIPD_UID_NUMBER	20

#define AID_APP_START 10000 /* first app user */

static int skipd_enable = 1;
module_param_named(skipd_enable, skipd_enable, int, S_IRUGO | S_IWUSR);

static int skipd_uid[MAX_SKIPD_UID_NUMBER];
static int serial = 0;
static struct proc_dir_entry *sdcardfs_procdir;

int dcim_delete_skip(void)
{
	int i = 0;

	if (!skipd_enable)
		return 0;

	//system app, do skip
	if (current_uid().val < AID_APP_START)
		return 0;
	for (i = 0; i < MAX_SKIPD_UID_NUMBER; i++) {
		if (skipd_uid[i] == current_uid().val)
			return 0;

		if (skipd_uid[i] == 0)
			break;
	}

	return 1;
}

int dcim_delete_uevent(struct dentry *dentry)
{
	char *buf, *path;
	char *denied_param[MAX_EVENT_PARAM] = { "DELETE_STAT=DCIM", NULL };
	int i;
	struct sdcardfs_inode_info *info = SDCARDFS_I(d_inode(dentry));

	if (info->data->dcim_uid == from_kuid(&init_user_ns, current_uid())) {
		printk("sdcardfs: app[uid %u] delete it's own picture %s, do unlink directly\n",
			info->data->dcim_uid, dentry->d_name.name);
		return -1;
	}

	mutex_lock(&dcim_mutex);

	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (buf == NULL) {
		mutex_unlock(&dcim_mutex);
		return -1;
	}
	path = dentry_path_raw(dentry, buf, PATH_MAX);
	if (IS_ERR(path)) {
		kfree(buf);
		mutex_unlock(&dcim_mutex);
		return -1;
	}

	for(i = 1; i < MAX_EVENT_PARAM - 1; i++) {
		denied_param[i] = kzalloc(PATH_MAX, GFP_KERNEL);
		if (!denied_param[i]) {
			goto free_memory;
		}
	}

	sprintf(denied_param[1], "UID=%u", from_kuid(&init_user_ns, current_uid()));
	sprintf(denied_param[2], "PATH=%s", path);
	sprintf(denied_param[3], "PID=%u", current->pid);
	printk("sdcardfs: send delete dcim uevent %s %s\n", denied_param[1], denied_param[2]);

	if (dcim_kobject) {
		kobject_uevent_env(dcim_kobject, KOBJ_CHANGE, denied_param);
	}

free_memory:
    for(i--; i > 0; i--)
        kfree(denied_param[i]);

	kfree(buf);

	mutex_unlock(&dcim_mutex);

	return 0;
}

static int sdfdev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int sdfdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations sdfdev_fops = {
	.owner = THIS_MODULE,
	.open = sdfdev_open,
	.release = sdfdev_release,
};

static struct miscdevice sdcardfs_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sdcardfs",
	.fops = &sdfdev_fops,
};

static int proc_sdcardfs_skip_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "skipd uid is: \n");
	for (i = 0; i < MAX_SKIPD_UID_NUMBER; i++) {
		seq_printf(m, "%d\n", skipd_uid[i]);
	}

	return 0;
}

static int proc_sdcardfs_skip_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_sdcardfs_skip_show, NULL);
}

static ssize_t proc_sdcardfs_skip_write(struct file *file, const char __user *buffer,
				   size_t count, loff_t *offp)
{
	int uid, ret;
	char *tmp;

	mutex_lock(&dcim_mutex);

	tmp = kzalloc((count + 1), GFP_KERNEL);
	if (!tmp) {
		mutex_unlock(&dcim_mutex);
		return -ENOMEM;
	}

	if(copy_from_user(tmp, buffer, count)) {
		kfree(tmp);
		mutex_unlock(&dcim_mutex);
		return -EINVAL;
	}

	ret = kstrtoint(tmp, 10, &uid);
	if (ret < 0) {
		kfree(tmp);
		mutex_unlock(&dcim_mutex);
		return ret;
	}

	if (uid < AID_APP_START) {
		kfree(tmp);
		mutex_unlock(&dcim_mutex);
		return -EINVAL;
	}

	skipd_uid[serial] = uid;
	serial++;

	if (serial >= MAX_SKIPD_UID_NUMBER)
		serial = 0;

	kfree(tmp);
	mutex_unlock(&dcim_mutex);

	return count;
}

static const struct file_operations proc_sdcardfs_skip_fops = {
	.open		= proc_sdcardfs_skip_open,
	.read		= seq_read,
	.write		= proc_sdcardfs_skip_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init dcim_event_init(void)
{
	int retval;
	struct proc_dir_entry *skipd_entry;

	retval = misc_register(&sdcardfs_device);
	dcim_kobject = kset_find_obj(module_kset, "sdcardfs");
	if (dcim_kobject == NULL) {
		printk("sdcardfs: sdcardfs uevent kobject is null");
		misc_deregister(&sdcardfs_device);
		return -1;
	}
	sdcardfs_procdir = proc_mkdir("fs/sdcardfs", NULL);
	skipd_entry = proc_create_data("skipd_delete", 664, sdcardfs_procdir,
				&proc_sdcardfs_skip_fops, NULL);

	return retval;
}

static void __exit dcim_event_exit(void)
{
	misc_deregister(&sdcardfs_device);
}

module_init(dcim_event_init);
module_exit(dcim_event_exit);

MODULE_AUTHOR("jiemin.zhu@oppo.com");
MODULE_LICENSE("GPL");
