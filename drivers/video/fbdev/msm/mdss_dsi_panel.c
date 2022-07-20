/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/qpnp/pin.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/qpnp/pwm.h>
#include <linux/err.h>
#include <linux/string.h>

#include "mdss_dsi.h"
#include "mdss_dba_utils.h"
#include "mdss_debug.h"

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@MultiMedia.Display.LCD.Stability, 2018/10/12,
//add for Lcd ftm \ project and mmkey
#include <soc/oppo/oppo_project.h>
#include <soc/oppo/boot_mode.h>
#include <soc/oppo/device_info.h>
#endif /*VENDOR_EDIT*/
#define DT_CMD_HDR 6
#define DEFAULT_MDP_TRANSFER_TIME 14000

#define VSYNC_DELAY msecs_to_jiffies(17)

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//add for i2c backlight
extern int lm3697_reg_init(void);
extern int lm3697_lcd_backlight_set_level(unsigned int bl_level);
extern void lm3697_bl_enable(int enable);

//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/12,
//add for get panel serial number
bool flag_lcd_off = false;
static uint print_bl = 0;
static DEFINE_MUTEX(lcd_mutex);
struct mdss_dsi_ctrl_pdata *gl_ctrl_pdata;

//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31,
//add for close bl for silence and sau mode
extern int lcd_closebl_flag;
//Shengjun.Gou@PSW.MM.Display.LCD.Stability, 2017/02/14,
//add for lcd cabc
enum
{
	CABC_CLOSE = 0,
	CABC_LOW_MODE,
	CABC_MIDDLE_MODE,
	CABC_HIGH_MODE,
};
int cabc_mode = CABC_HIGH_MODE; //default mode level 3 in dtsi file

/*
 * Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
 * add for +-5V second resource delay 2ms to 3ms
 */
#define TPS65132_DELAY_3MS 3
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/10/31,add for support aod feature, solve bug:1264744*/
extern bool request_enter_aod;
extern bool is_just_exit_aod;
extern struct mutex aod_lock;
#endif /*VENDOR_EDIT*/
#ifdef VENDOR_EDIT
//add for lcd seed
enum
{
	SEED_MODE0 = 0,
	SEED_MODE1,
	SEED_MODE2,
};
int seed_mode = SEED_MODE0;
//add for lcd esd recovery power off when tp black gesture open
int lcd_esd_status = 1;
#endif /*VENDOR_EDIT*/
DEFINE_LED_TRIGGER(bl_led_trigger);

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@MultiMedia.Display.LCD.Stability, 2018/10/12,
//add for panel vendor
int lcd_vendor=0;
int is_lcd(OPPO_LCD lcd_num){
   return (lcd_vendor == lcd_num ? 1:0);
}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31,
//add for read LCM window info
long bl_set_time = 0;
char lcm_window_color[2] = {0xff, 0xff};
int lcd_id_count = 0;

void lcm_id_read(char reg_addr, char* buf, int lenth)
{
	if(flag_lcd_off == true)
	{
		pr_err("%s lcd is off,reading lcm id is not allowed !\n", __func__);
		return;
	}

	mdss_dsi_panel_cmd_read(gl_ctrl_pdata,reg_addr,0x00,NULL,&buf[0],lenth);
	pr_info("%s Read lcm addr 0x%x  is 0x%x\n", __func__, reg_addr, buf[0]);
}
#endif /*VENDOR_EDIT*/

void mdss_dsi_panel_pwm_cfg(struct mdss_dsi_ctrl_pdata *ctrl)
{
	if (ctrl->pwm_pmi)
		return;

	ctrl->pwm_bl = pwm_request(ctrl->pwm_lpg_chan, "lcd-bklt");
	if (ctrl->pwm_bl == NULL || IS_ERR(ctrl->pwm_bl)) {
		pr_err("%s: Error: lpg_chan=%d pwm request failed",
				__func__, ctrl->pwm_lpg_chan);
	}
	ctrl->pwm_enabled = 0;
}

bool mdss_dsi_panel_pwm_enable(struct mdss_dsi_ctrl_pdata *ctrl)
{
	bool status = true;
	if (!ctrl->pwm_enabled)
		goto end;

	if (pwm_enable(ctrl->pwm_bl)) {
		pr_err("%s: pwm_enable() failed\n", __func__);
		status = false;
	}

	ctrl->pwm_enabled = 1;

end:
	return status;
}

static void mdss_dsi_panel_bklt_pwm(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	int ret;
	u32 duty;
	u32 period_ns;

	if (ctrl->pwm_bl == NULL) {
		pr_err("%s: no PWM\n", __func__);
		return;
	}

	if (level == 0) {
		if (ctrl->pwm_enabled) {
			ret = pwm_config_us(ctrl->pwm_bl, level,
					ctrl->pwm_period);
			if (ret)
				pr_err("%s: pwm_config_us() failed err=%d.\n",
						__func__, ret);
			pwm_disable(ctrl->pwm_bl);
		}
		ctrl->pwm_enabled = 0;
		return;
	}

	duty = level * ctrl->pwm_period;
	duty /= ctrl->bklt_max;

	pr_debug("%s: bklt_ctrl=%d pwm_period=%d pwm_gpio=%d pwm_lpg_chan=%d\n",
			__func__, ctrl->bklt_ctrl, ctrl->pwm_period,
				ctrl->pwm_pmic_gpio, ctrl->pwm_lpg_chan);

	pr_debug("%s: ndx=%d level=%d duty=%d\n", __func__,
					ctrl->ndx, level, duty);

	if (ctrl->pwm_period >= USEC_PER_SEC) {
		ret = pwm_config_us(ctrl->pwm_bl, duty, ctrl->pwm_period);
		if (ret) {
			pr_err("%s: pwm_config_us() failed err=%d.\n",
					__func__, ret);
			return;
		}
	} else {
		period_ns = ctrl->pwm_period * NSEC_PER_USEC;
		ret = pwm_config(ctrl->pwm_bl,
				level * period_ns / ctrl->bklt_max,
				period_ns);
		if (ret) {
			pr_err("%s: pwm_config() failed err=%d.\n",
					__func__, ret);
			return;
		}
	}

	if (!ctrl->pwm_enabled) {
		ret = pwm_enable(ctrl->pwm_bl);
		if (ret)
			pr_err("%s: pwm_enable() failed err=%d\n", __func__,
				ret);
		ctrl->pwm_enabled = 1;
	}
}

static char dcs_cmd[2] = {0x54, 0x00}; /* DTYPE_DCS_READ */
static struct dsi_cmd_desc dcs_read_cmd = {
	{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(dcs_cmd)},
	dcs_cmd
};

int mdss_dsi_panel_cmd_read(struct mdss_dsi_ctrl_pdata *ctrl, char cmd0,
		char cmd1, void (*fxn)(int), char *rbuf, int len)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;

	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			return -EINVAL;
	}

	dcs_cmd[0] = cmd0;
	dcs_cmd[1] = cmd1;
	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = &dcs_read_cmd;
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_RX | CMD_REQ_COMMIT;
	cmdreq.rlen = len;
	cmdreq.rbuf = rbuf;
	cmdreq.cb = fxn; /* call back */
	/*
	 * blocked here, until call back called
	 */

	return mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}

static void mdss_dsi_panel_apply_settings(struct mdss_dsi_ctrl_pdata *ctrl,
			struct dsi_panel_cmds *pcmds)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;

	pinfo = &(ctrl->panel_data.panel_info);
	if ((pinfo->dcs_cmd_by_left) && (ctrl->ndx != DSI_CTRL_LEFT))
		return;

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = pcmds->cmds;
	cmdreq.cmds_cnt = pcmds->cmd_cnt;
	cmdreq.flags = CMD_REQ_COMMIT;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}


static void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
			struct dsi_panel_cmds *pcmds, u32 flags)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;

	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			return;
	}

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = pcmds->cmds;
	cmdreq.cmds_cnt = pcmds->cmd_cnt;
	cmdreq.flags = flags;

	/*Panel ON/Off commands should be sent in DSI Low Power Mode*/
	if (pcmds->link_state == DSI_LP_MODE)
		cmdreq.flags  |= CMD_REQ_LP_MODE;
	else if (pcmds->link_state == DSI_HS_MODE)
		cmdreq.flags |= CMD_REQ_HS_MODE;

	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31,
//add for panel esd test
static char set_esd[2] = {0x10, 0x00};  /* DTYPE_DCS_WRITE1 */
static struct dsi_cmd_desc set_esd_cmd = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1, sizeof(set_esd)},
	set_esd
};

void set_esd_mode(int level)
{
	struct dcs_cmd_req cmdreq;


	mutex_lock(&lcd_mutex);
	if(flag_lcd_off == true)
	{
		printk(KERN_INFO "lcd is off,don't allow to set esd !\n");
		mutex_unlock(&lcd_mutex);
		return;
	}

	switch(level)
	{
		/* for esd */
		case 0:
		set_esd[1] = 0x00;
			break;
		default:
			break;
	}

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = &set_esd_cmd;
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(gl_ctrl_pdata, &cmdreq);

	mutex_unlock(&lcd_mutex);
}

//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31,
//add for adb mipi read/write lcd reg
static struct dsi_cmd_desc user_write_reg[] = {
 {{DTYPE_DCS_WRITE1, 1, 0, 0, 0, 0},NULL},
};
void send_user_write_reg(char *par, u32 cnt)
{
	struct dcs_cmd_req cmdreq;
	if(par==NULL || cnt==0)
	return;
	mutex_lock(&lcd_mutex);
	if(flag_lcd_off == true)
	{
		printk(KERN_INFO "lcd is off,don't allow to set user gamma !\n");
		mutex_unlock(&lcd_mutex);
		return;
	}
	user_write_reg->dchdr.dlen = cnt;
	user_write_reg->payload = par;
	user_write_reg->dchdr.dtype = 0x15;
	if(user_write_reg->payload[0] == 0x11 || user_write_reg->payload[0] == 0x29 || user_write_reg->payload[0] == 0x10 ||
	user_write_reg->payload[0] == 0x28 || user_write_reg->payload[0] == 0xde || user_write_reg->payload[0] == 0xdf)
	{
		user_write_reg->dchdr.dtype = 0x05;
	}
	if(cnt>2)
	{
		user_write_reg->dchdr.dtype = 0x39;
	}
	pr_err("dtype = 0x%x\n",user_write_reg->dchdr.dtype);
	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = user_write_reg;
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_COMMIT;
	mdss_dsi_cmdlist_put(gl_ctrl_pdata, &cmdreq);
	mutex_unlock(&lcd_mutex);
 return;
}

#define REG_CNT 16 //Read reg counts

void dump_lcd_reg(u32 off,u32 data,char* dump_data)
{
	int i,tot=0,len;
	char read[REG_CNT*5+1];
	if(flag_lcd_off)
	{
		return;
	}

	if(data > REG_CNT)
	{
		data = REG_CNT;
		pr_err("%s max read number is = %d\n", __func__, REG_CNT);
	}

	mdss_dsi_panel_cmd_read(gl_ctrl_pdata,off,0x00,NULL,read,data);
	for(i=0;i<data;i++)
	{
		len = sprintf(dump_data+tot,"0x%02x ",read[i]);
		tot += len;
	}
}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
/* Shengjun.Gou@PSW.MM.Display.LCD.Stability, 2017/06/13
 * add for 1024 level backlight
*/
static char new_oled_backlight[] = {0x51, 0x00, 0x00};
static struct dsi_cmd_desc new_oled_backlight_cmd = {
	{DTYPE_DCS_LWRITE, 1, 0, 0, 1, sizeof(new_oled_backlight)},
	new_oled_backlight
};
/* Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31 solve AOD flicker issue */
bool oppo_backlight_store = false;
extern bool oppo_aod_backlight_need_set;
static void oppo_dsi_panel_aod_backlight_dcs(struct mdss_dsi_ctrl_pdata *ctrl)
{
	mdss_dsi_panel_cmds_send(ctrl, &ctrl->aod_backlight_cmds, CMD_REQ_COMMIT);
}
#endif /*VENDOR_EDIT*/

static char led_pwm1[2] = {0x51, 0x0};	/* DTYPE_DCS_WRITE1 */
static struct dsi_cmd_desc backlight_cmd = {
	{DTYPE_DCS_WRITE1, 1, 0, 0, 1, sizeof(led_pwm1)},
	led_pwm1
};

static void mdss_dsi_panel_bklt_dcs(struct mdss_dsi_ctrl_pdata *ctrl, int level)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo;
#ifdef VENDOR_EDIT
/* Shengjun.Gou@PSW.MM.Display.LCD.Stability, 2017/06/13
 * add for 1024 level backlight
*/
	int	BL_MSB = 0;
	int	BL_LSB = 0;
#endif /*VENDOR_EDIT*/

	pinfo = &(ctrl->panel_data.panel_info);
	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			return;
	}

	pr_debug("%s: level=%d\n", __func__, level);

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31
//add for 1024 level backlight
	if((lcd_vendor == OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))
	{
		BL_LSB = level/256;
		BL_MSB = level%256;
		new_oled_backlight[1] = (unsigned char)BL_LSB;
		new_oled_backlight[2] = (unsigned char)BL_MSB;
	}else{
	   led_pwm1[1] = (unsigned char)level;
	}
#endif /*VENDOR_EDIT*/

	if(is_just_exit_aod == true) {
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/10/31,delay 11 frame*/
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/10/31,delay 1 frame,optimize performance and implement fast unblank*/
		mdelay(20);
		mutex_lock(&aod_lock);
		is_just_exit_aod = false;
		mutex_unlock(&aod_lock);
	} else {
		pr_debug("normal case, do nothing\n");
	}

/* Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31 solve AOD flicker issue */
	if ((lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL)
		&& oppo_aod_backlight_need_set && (level == 1))
	{
		oppo_aod_backlight_need_set = false;
		oppo_dsi_panel_aod_backlight_dcs(ctrl);
		return;
	}

/* Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/31 solve AOD flicker issue */
	if (oppo_backlight_store)
	{
		pr_info("%s bl set not allowed during AOD.", __func__);
		return;
	}

	memset(&cmdreq, 0, sizeof(cmdreq));
#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MultiMedia.Display.LCD.Stability, 2018/10/31
//add for 1024 level backlight
	if((lcd_vendor == OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))
	{
		cmdreq.cmds = &new_oled_backlight_cmd;
	}else{
		cmdreq.cmds = &backlight_cmd;
	}
#endif /*VENDOR_EDIT*/
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	if (ctrl->bklt_dcs_op_mode == DSI_HS_MODE)
		cmdreq.flags |= CMD_REQ_HS_MODE;
	else
		cmdreq.flags |= CMD_REQ_LP_MODE;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}
#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Driver.feature, 2017/03/17,
//add for HBM
static char set_hbm_state = 0x20;
static char set_hbm_mode[2] = {0x01, 0x70};
int last_hbm_level = 0;

enum hbm_level
{
	HBM_L0 = 0,  // hbm disable
	HBM_MAX_1,   // hbm level 5
	HBM_DISABLE, // hbm disable
	HBM_MAX_2,   // hbm level 5
	HBM_L1,
	HBM_L2,
	HBM_L3,
	HBM_L4,
	HBM_L5,
	HBM_L6,
	HBM_MAX,
	HBM_ON,
};

int hbm_mode = HBM_L0; //default mode off

void set_hbm_level(struct mdss_panel_data *pdata, int hbm_level, bool hbm_to_aod)
{
	mutex_lock(&lcd_mutex);

	if (pdata->oppo_fingerprint_hbm_mode != HBM_L0 &&
	    pdata->oppo_fingerprint_hbm_mode != HBM_DISABLE) {
		hbm_level = pdata->oppo_fingerprint_hbm_mode;
	} else {
		hbm_level = pdata->sysfs_hbm_mode;
	}

	/* Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/11/07,
	 * Add for repeat set hbm level error.
	 */
	if (hbm_level == last_hbm_level) {
	    printk(KERN_INFO "%s: Repeat set hbm level not allowed.\n", __func__);
	    mutex_unlock(&lcd_mutex);
	    return;
	}
	last_hbm_level = hbm_level;

	printk(KERN_INFO "%s: HBM level:%d mode: %d setted!\n",__func__, hbm_level,hbm_mode);

	switch(hbm_level)
	{
		case HBM_L0:
		case HBM_DISABLE:
			set_hbm_state = 0x20;
			hbm_mode = HBM_L0;
			set_hbm_mode[0] = 0x00;
			set_hbm_mode[1] = 0x40;
			break;
		case HBM_L1:
			set_hbm_state = 0xe0;
			hbm_mode = HBM_L1;
			set_hbm_mode[0] = 0x02;
			set_hbm_mode[1] = 0x38;
			break;
		case HBM_L2:
			set_hbm_state = 0xe0;
			hbm_mode = HBM_L2;
			set_hbm_mode[0] = 0x01;
			set_hbm_mode[1] = 0xD8;
			break;
		case HBM_L3:
			set_hbm_state = 0xe0;
			hbm_mode = HBM_L3;
			set_hbm_mode[0] = 0x01;
			set_hbm_mode[1] = 0x74;
			break;
		case HBM_L4:
			set_hbm_state = 0xe0;
			hbm_mode = HBM_L4;
			set_hbm_mode[0] = 0x01;
			set_hbm_mode[1] = 0x10;
			break;
		case HBM_L5:
			set_hbm_state = 0xe0;
			hbm_mode = HBM_L5;
			set_hbm_mode[0] = 0x00;
			set_hbm_mode[1] = 0xA8;
			break;
		case HBM_L6:
		case HBM_ON:
		case HBM_MAX:
		case HBM_MAX_1:
		case HBM_MAX_2:
			set_hbm_state = 0xe0;
			hbm_mode = HBM_L6;
			set_hbm_mode[0] = 0x00;
			set_hbm_mode[1] = 0x40;
			break;

		default:
			pr_err("%s: Unsuporrted HBM level:%d\n", __func__, hbm_level);
			mutex_unlock(&lcd_mutex);
			return;
	}

	/* hbm footswitch */
	if (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL)
	{
		if (hbm_to_aod)
		{
			gl_ctrl_pdata->hbm_cmds.cmds[2].payload[1] = 0x22;
		} else {
			gl_ctrl_pdata->hbm_cmds.cmds[2].payload[1] = set_hbm_state;
		}

		oppo_backlight_store = true;
		gl_ctrl_pdata->hbm_cmds.cmds[1].payload[1] = 0x3;
		gl_ctrl_pdata->hbm_cmds.cmds[1].payload[2] = 0xff;

		if (hbm_mode == HBM_L0)
		{
			gl_ctrl_pdata->hbm_cmds.cmds[1].payload[1] = new_oled_backlight[1];
			gl_ctrl_pdata->hbm_cmds.cmds[1].payload[2] = new_oled_backlight[2];
			oppo_backlight_store = false;
		}

	} else {
		gl_ctrl_pdata->hbm_cmds.cmds[1].payload[1] = set_hbm_state;
	}

	if(!((lcd_vendor == OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO16118_SAMSUNG_S6E3FA5_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO16051_SAMSUNG_S6E3FA5_1080P_CMD_PANEL)))
	{
		gl_ctrl_pdata->hbm_cmds.cmds[2].payload[1] = set_hbm_mode[0];
		gl_ctrl_pdata->hbm_cmds.cmds[2].payload[2] = set_hbm_mode[1];
		pr_info("request enter from hbm to aod 0x%x\n", gl_ctrl_pdata->hbm_cmds.cmds[2].payload[1]);
	}

	mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &gl_ctrl_pdata->hbm_cmds, CMD_REQ_COMMIT);

	mutex_unlock(&lcd_mutex);

	return;
}

int request_enter_form_hbm_to_aod(struct mdss_panel_data *pdata, int hbm_level)
{
	set_hbm_level(pdata, hbm_level, true);
	return 0;
}

//Guoqiang.Jiang@PSW.MM.Driver.feature, 2017/03/17,
//add for LBR
#define LBR_LEVEL_MAX 255
static int lbr_level_local = 0;

int get_lbr_mode(void)
{
	return lbr_level_local;
}
int set_lbr_mode(int lbr_level)
{
	int ret = 0;
	char lbr_mode_select = 0x00;  // on:0x80;  off:0x00;
	char lbr_mode_step = 0x00;  // step 0(mix): 0x00;  step 63(max): 0x3f

	mutex_lock(&lcd_mutex);

	if(flag_lcd_off == true)
	{
		pr_err("lcd is off,don't allow to set lbr\n");
		mutex_unlock(&lcd_mutex);
		return 0;
	}

	if (lbr_level > LBR_LEVEL_MAX) {
		lbr_level = LBR_LEVEL_MAX;
	} else if (lbr_level < 0) {
		lbr_level = 0;
	}

	lbr_level_local = lbr_level;

	if (lbr_level > 0) {
		lbr_mode_select = 0x80;
	} else {
		lbr_mode_select = 0x00;
	}

	lbr_mode_step = (char)lbr_level;
	printk(KERN_INFO "%s lbr_mode_step = 0x%x\n", __func__, lbr_mode_step);

	gl_ctrl_pdata->lbr_cmds.cmds[2].payload[1] = lbr_mode_select;
	gl_ctrl_pdata->lbr_cmds.cmds[4].payload[1] = lbr_mode_step;

	mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &gl_ctrl_pdata->lbr_cmds, CMD_REQ_COMMIT);

	mutex_unlock(&lcd_mutex);
	return ret;
}

//Guoqiang.Jiang@PSW.MM.Driver.feature, 2017/03/17,
//add for read panel serial number
typedef struct panel_serial_info
{
	int reg_index;
	uint64_t year;
	uint64_t month;
	uint64_t day;
	uint64_t hour;
	uint64_t minute;
	uint64_t second;
	uint64_t reserved[2];
} PANEL_SERIAL_INFO;

int panel_serial_number_read(char addr, uint64_t *buf, int lenth)
{
	int ret = 0;
	unsigned char read[lenth];
	PANEL_SERIAL_INFO panel_serial_info;
	if(flag_lcd_off == true)
	{
		pr_err("%s lcd is off, Not allowed to get panel's serial number\n", __func__);
		return 0;
	}
	mutex_lock(&lcd_mutex);
	ret = mdss_dsi_panel_cmd_read(gl_ctrl_pdata, addr, 0x00, NULL, read, lenth);
	mutex_unlock(&lcd_mutex);

	if(ret < 0)
	{
		pr_err("%s Get panel serial number failed!\n", __func__);
	} else {
		switch(lcd_vendor)
		{
			case OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL:
			case OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL:
				/*  0xA1               12th        13rd    14th    15th
				 *  HEX                0x32        0x0C    0x0B    0x29
				 *  Bit           [D7:D4][D3:D0] [D5:D0] [D5:D0] [D5:D0]
				 *  exp              3      2       C       B       29
				 *  Yyyy,mm,dd      2014   2m      12d     11h     41min
				*/
				panel_serial_info.reg_index = 11;

				panel_serial_info.year		= (read[panel_serial_info.reg_index] & 0xF0) >> 0x4;
				panel_serial_info.month		= read[panel_serial_info.reg_index + 1]	& 0x0F;
				panel_serial_info.day		= read[panel_serial_info.reg_index + 1]	& 0x1F;
				panel_serial_info.hour		= read[panel_serial_info.reg_index + 2]	& 0x1F;
				panel_serial_info.minute	= read[panel_serial_info.reg_index + 3]	& 0x3F;

				break;
			case OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL:
			case OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL:
				/*  0xA1               12th        13rd    14th    15th    16th
				 *  HEX                0x32        0x0C    0x0B    0x29    0x37
				 *  Bit           [D7:D4][D3:D0] [D5:D0] [D5:D0] [D5:D0] [D5:D0]
				 *  exp              3      2       C       B       29      37
				 *  Yyyy,mm,dd      2014   2m      12d     11h     41min   55sec
				*/
				panel_serial_info.reg_index = 11;

				panel_serial_info.year		= (read[panel_serial_info.reg_index] & 0xF0) >> 0x4;
				panel_serial_info.month		= read[panel_serial_info.reg_index]		& 0x0F;
				panel_serial_info.day		= read[panel_serial_info.reg_index + 1]	& 0x1F;
				panel_serial_info.hour		= read[panel_serial_info.reg_index + 2]	& 0x1F;
				panel_serial_info.minute	= read[panel_serial_info.reg_index + 3]	& 0x3F;
				panel_serial_info.second	= read[panel_serial_info.reg_index + 4]	& 0x3F;
				pr_info("%s year:0x%llx, month:0x%llx, day:0x%llx, hour:0x%llx, minute:0x%llx, second:0x%llx!\n",
					__func__,
					panel_serial_info.year,
					panel_serial_info.month,
					panel_serial_info.day,
					panel_serial_info.hour,
					panel_serial_info.minute,
					panel_serial_info.second);
				break;

			default:
				pr_err("Unsuporrted panel!\n");
		}

		*buf = (panel_serial_info.year		<< 56)\
			 + (panel_serial_info.month		<< 48)\
			 + (panel_serial_info.day		<< 40)\
			 + (panel_serial_info.hour		<< 32)\
			 + (panel_serial_info.minute	<< 24)\
			 + (panel_serial_info.second	<< 16)\
			 + (panel_serial_info.reserved[0] << 8)\
			 + (panel_serial_info.reserved[1]);

		pr_info(KERN_INFO "%s lcd_vendor:%d, Get panel serial number[0x%llx] successfully!\n", __func__, lcd_vendor, *buf);
	}

	return ret;
}

#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/02/14,
//add for lcd cabc
struct dsi_panel_cmds cabc_off_sequence;
struct dsi_panel_cmds cabc_user_interface_image_sequence;
struct dsi_panel_cmds cabc_still_image_sequence;
struct dsi_panel_cmds cabc_video_image_sequence;
int set_cabc(int level)
{
	int ret = 0;

	pr_err("mdss set_cabc %d \n",level);

	mutex_lock(&lcd_mutex);

	if(flag_lcd_off == true)
	{
		printk(KERN_INFO "lcd is off,don't allow to set cabc\n");
		cabc_mode = level;
		mutex_unlock(&lcd_mutex);
		return 0;
	}

	switch(level)
	{
		case 0:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_off_sequence, CMD_REQ_COMMIT);
			cabc_mode = CABC_CLOSE;
			break;
		case 1:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_user_interface_image_sequence, CMD_REQ_COMMIT);
			cabc_mode = CABC_LOW_MODE;
			break;
		case 2:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_still_image_sequence, CMD_REQ_COMMIT);
			cabc_mode = CABC_MIDDLE_MODE;
			break;
		case 3:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_video_image_sequence, CMD_REQ_COMMIT);
			cabc_mode = CABC_HIGH_MODE;
			break;
		default:
			pr_err("%s Leavel %d is not supported!\n",__func__,level);
			ret = -1;
			break;
	}

	mutex_unlock(&lcd_mutex);
	return ret;
}

static int set_cabc_resume_mode(int mode)
{
	int ret = 0;

	// lcd cabc resume
	if(!(is_lcd(OPPO16103_JDI_R63452_1080P_CMD_PANEL)&&is_project(OPPO_16103)))
		return 0;

	pr_err("mdss set_cabc_resume_mode:%d\n", mode);
	switch(mode)
	{
		case 0:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_off_sequence, CMD_REQ_COMMIT);
			break;
		case 1:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_user_interface_image_sequence, CMD_REQ_COMMIT);
			break;
		case 2:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_still_image_sequence, CMD_REQ_COMMIT);
			break;
		case 3:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &cabc_video_image_sequence, CMD_REQ_COMMIT);
			break;
		default:
			pr_err("%s  %d is not supported!\n",__func__,mode);
			ret = -1;
			break;
	}
	return ret;
}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/02/18,
//add for lcd seed
struct dsi_panel_cmds seed_mode0_cmds;
struct dsi_panel_cmds seed_mode1_cmds;
struct dsi_panel_cmds seed_mode2_cmds;
int set_seed_mode(int level)
{
	int ret = 0;
	if(!((is_lcd(OPPO16051_SAMSUNG_S6E3FA5_1080P_CMD_PANEL))
		||(is_lcd(OPPO16118_SAMSUNG_S6E3FA5_1080P_CMD_PANEL))
		||(is_lcd(OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL))
		||(is_lcd(OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL))
		||(is_lcd(OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL))
		||(is_lcd(OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))))
	{
		return 0;
	}

	pr_info("mdss set_seed %d \n",level);
	mutex_lock(&lcd_mutex);
	if(flag_lcd_off == true)
	{
		printk(KERN_INFO "lcd is off,don't allow to set seed\n");
		seed_mode = level;
		mutex_unlock(&lcd_mutex);
		return 0;
	}

	mdss_dsi_clk_ctrl(gl_ctrl_pdata, gl_ctrl_pdata->dsi_clk_handle,
					MDSS_DSI_ALL_CLKS, MDSS_DSI_CLK_ON);
	switch(level)
	{
		case SEED_MODE0:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &seed_mode0_cmds, CMD_REQ_COMMIT | CMD_CLK_CTRL);
			seed_mode = SEED_MODE0;
			break;
		case SEED_MODE1:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &seed_mode1_cmds, CMD_REQ_COMMIT | CMD_CLK_CTRL);
			seed_mode = SEED_MODE1;
			break;
		case SEED_MODE2:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &seed_mode2_cmds, CMD_REQ_COMMIT | CMD_CLK_CTRL);
			seed_mode = SEED_MODE2;
			break;
		default:
			pr_err("%s  seed mode %d is not supported!\n",__func__,level);
			ret = -1;
			break;
	}
	mdss_dsi_clk_ctrl(gl_ctrl_pdata, gl_ctrl_pdata->dsi_clk_handle,
					MDSS_DSI_ALL_CLKS, MDSS_DSI_CLK_OFF);
	mutex_unlock(&lcd_mutex);
	return ret;
}

static int set_seed_resume_mode(int mode)
{
	int ret = 0;

	if(!((is_lcd(OPPO16051_SAMSUNG_S6E3FA5_1080P_CMD_PANEL))
		||(is_lcd(OPPO16118_SAMSUNG_S6E3FA5_1080P_CMD_PANEL))
		||(is_lcd(OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL))
		||(is_lcd(OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL))
		||(is_lcd(OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL))
		||(is_lcd(OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))))
	{
		return 0;
	}
	pr_info("mdss set_seed_resume_mode:%d\n", mode);
	mutex_lock(&lcd_mutex);
	mdss_dsi_clk_ctrl(gl_ctrl_pdata, gl_ctrl_pdata->dsi_clk_handle,
					MDSS_DSI_ALL_CLKS, MDSS_DSI_CLK_ON);
	switch(mode)
	{
		case SEED_MODE0:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &seed_mode0_cmds, CMD_REQ_COMMIT | CMD_CLK_CTRL);
			seed_mode = SEED_MODE0;
			break;
		case SEED_MODE1:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &seed_mode1_cmds, CMD_REQ_COMMIT | CMD_CLK_CTRL);
			seed_mode = SEED_MODE1;
			break;
		case SEED_MODE2:
			mdss_dsi_panel_cmds_send(gl_ctrl_pdata, &seed_mode2_cmds, CMD_REQ_COMMIT | CMD_CLK_CTRL);
			seed_mode = SEED_MODE2;
			break;
		default:
			pr_err("%s seed mode %d not supported!\n",__func__,mode);
			ret = -1;
			break;
	}
	mdss_dsi_clk_ctrl(gl_ctrl_pdata, gl_ctrl_pdata->dsi_clk_handle,
					MDSS_DSI_ALL_CLKS, MDSS_DSI_CLK_OFF);
	mutex_unlock(&lcd_mutex);
	return ret;
}
#endif /*VENDOR_EDIT*/

static int mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int rc = 0;

	if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
		rc = gpio_request(ctrl_pdata->disp_en_gpio,
						"disp_enable");
		if (rc) {
			pr_err("request disp_en gpio failed, rc=%d\n",
				       rc);
			goto disp_en_gpio_err;
		}
	}
	rc = gpio_request(ctrl_pdata->rst_gpio, "disp_rst_n");
	if (rc) {
		pr_err("request reset gpio failed, rc=%d\n",
			rc);
		goto rst_gpio_err;
	}
	if (gpio_is_valid(ctrl_pdata->avdd_en_gpio)) {
		rc = gpio_request(ctrl_pdata->avdd_en_gpio,
						"avdd_enable");
		if (rc) {
			pr_err("request avdd_en gpio failed, rc=%d\n",
				       rc);
			goto avdd_en_gpio_err;
		}
	}
	if (gpio_is_valid(ctrl_pdata->lcd_mode_sel_gpio)) {
		rc = gpio_request(ctrl_pdata->lcd_mode_sel_gpio, "mode_sel");
		if (rc) {
			pr_err("request dsc/dual mode gpio failed,rc=%d\n",
								rc);
			goto lcd_mode_sel_gpio_err;
		}
	}

	return rc;

lcd_mode_sel_gpio_err:
	if (gpio_is_valid(ctrl_pdata->avdd_en_gpio))
		gpio_free(ctrl_pdata->avdd_en_gpio);
avdd_en_gpio_err:
	gpio_free(ctrl_pdata->rst_gpio);
rst_gpio_err:
	if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
		gpio_free(ctrl_pdata->disp_en_gpio);
disp_en_gpio_err:
	return rc;
}

int mdss_dsi_bl_gpio_ctrl(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int rc = 0, val = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	if (ctrl_pdata == NULL) {
		pr_err("%s: Invalid ctrl data\n", __func__);
		return -EINVAL;
	}

	/* if gpio is not valid */
	if (!gpio_is_valid(ctrl_pdata->bklt_en_gpio))
		return rc;

	pr_debug("%s: enable = %d\n", __func__, enable);

	/*
	 * if gpio state is false and enable (bl level) is
	 * non zero then toggle the gpio
	 */
	if (!ctrl_pdata->bklt_en_gpio_state && enable) {
		rc = gpio_request(ctrl_pdata->bklt_en_gpio, "bklt_enable");
		if (rc) {
			pr_err("request bklt gpio failed, rc=%d\n", rc);
			goto free;
		}

		if (ctrl_pdata->bklt_en_gpio_invert)
			val = 0;
		 else
			val = 1;

		rc = gpio_direction_output(ctrl_pdata->bklt_en_gpio, val);
		if (rc) {
			pr_err("%s: unable to set dir for bklt gpio val %d\n",
						__func__, val);
			goto free;
		}
		ctrl_pdata->bklt_en_gpio_state = true;
		goto ret;
	} else if (ctrl_pdata->bklt_en_gpio_state && !enable) {
		/*
		 * if gpio state is true and enable (bl level) is
		 * zero then toggle the gpio
		 */
		if (ctrl_pdata->bklt_en_gpio_invert)
			val = 1;
		 else
			val = 0;

		rc = gpio_direction_output(ctrl_pdata->bklt_en_gpio, val);
		if (rc)
			pr_err("%s: unable to set dir for bklt gpio val %d\n",
						__func__, val);
		goto free;
	}

	/* gpio state is true and bl level is non zero */
	goto ret;

free:
	pr_debug("%s: free bklt gpio\n", __func__);
	ctrl_pdata->bklt_en_gpio_state = false;
	gpio_free(ctrl_pdata->bklt_en_gpio);
ret:
	return rc;
}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//add for tp black gesture
extern int tp_gesture_enable_flag(void);
static int mdss_tp_black_gesture_status(void){
	int ret = 0;
	/*default disable tp gesture*/

	//tp add the interface for check black status to ret
	ret = tp_gesture_enable_flag();
	pr_err("%s: ret = %d\n", __func__, ret);
	return ret;
}
#endif /*VENDOR_EDIT*/

int mdss_dsi_panel_reset(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	int i, rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &(ctrl_pdata->panel_data.panel_info);
	if ((mdss_dsi_is_right_ctrl(ctrl_pdata) &&
		mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data)) ||
			pinfo->is_dba_panel) {
		pr_debug("%s:%d, right ctrl gpio configuration not needed\n",
			__func__, __LINE__);
		return rc;
	}

	if (!gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
		pr_debug("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
	}

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_debug("%s:%d, reset line not configured\n",
			   __func__, __LINE__);
		return rc;
	}

#ifndef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//Modify for panel debug
	pr_debug("%s: enable = %d\n", __func__, enable);
#else /*VENDOR_EDIT*/
	pr_err("%s: enable = %d\n", __func__, enable);
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//add for backlight log print
	print_bl = 0;
#endif /*VENDOR_EDIT*/

	if (enable) {
		rc = mdss_dsi_request_gpios(ctrl_pdata);
		if (rc) {
			pr_err("gpio request failed\n");
			return rc;
		}
		if (!pinfo->cont_splash_enabled) {
#ifndef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//modify for 16103 panel power setting
			if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
				rc = gpio_direction_output(
					ctrl_pdata->disp_en_gpio, 1);
				if (rc) {
					pr_err("%s: unable to set dir for en gpio\n",
						__func__);
					goto exit;
				}
			}

			if (pdata->panel_info.rst_seq_len) {
				rc = gpio_direction_output(ctrl_pdata->rst_gpio,
					pdata->panel_info.rst_seq[0]);
				if (rc) {
					pr_err("%s: unable to set dir for rst gpio\n",
						__func__);
					goto exit;
				}
			}

			for (i = 0; i < pdata->panel_info.rst_seq_len; ++i) {
				gpio_set_value((ctrl_pdata->rst_gpio),
					pdata->panel_info.rst_seq[i]);
				if (pdata->panel_info.rst_seq[++i])
					usleep_range(pinfo->rst_seq[i] * 1000, pinfo->rst_seq[i] * 1000);
			}

			if (gpio_is_valid(ctrl_pdata->avdd_en_gpio)) {
				if (ctrl_pdata->avdd_en_gpio_invert) {
					rc = gpio_direction_output(
						ctrl_pdata->avdd_en_gpio, 0);
				} else {
					rc = gpio_direction_output(
						ctrl_pdata->avdd_en_gpio, 1);
				}
				if (rc) {
					pr_err("%s: unable to set dir for avdd_en gpio\n",
						__func__);
					goto exit;
				}
			}

#else /*VENDOR_EDIT*/
			/*
			* Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
			* add for lcd power timing
			*/
			if (is_lcd(OPPO18136_HIMAX_NT36772A_1080_2340_VOD_PANEL)
				|| is_lcd(OPPO18136_HIMAX_HX83112A_1080_2340_VOD_PANEL)
				|| is_lcd(OPPO18321_DPT_NT36672A_1080_2340_VOD_PANEL))
			{
				mdelay(TPS65132_DELAY_3MS);
				if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
					rc = gpio_direction_output(ctrl_pdata->disp_en_gpio, 1);
					if (rc) {
						pr_err("%s: unable to set dir for en gpio\n",
							__func__);
						goto exit;
					}
				}

				/*
				 * add for +-5V second resource delay 2ms to 3ms
				 */
				mdelay(TPS65132_DELAY_3MS);

				if (gpio_is_valid(ctrl_pdata->disp_enn_gpio)) {
					rc = gpio_request(ctrl_pdata->disp_enn_gpio, "disp_enable_neg");
					if (rc) {
						pr_err("request disp enn gpio failed,rc=%d\n", rc);
						goto exit;
					} else {
						rc = gpio_direction_output(ctrl_pdata->disp_enn_gpio, 1);
						if (rc) {
							pr_err("%s: unable to set dir for disp_enable_neg gpio\n",
								__func__);
							goto exit;
						}
					}
				}

				mdelay(12);

				if (pdata->panel_info.rst_seq_len) {
					rc = gpio_direction_output(ctrl_pdata->rst_gpio,
						pdata->panel_info.rst_seq[0]);
					if (rc) {
						pr_err("%s: unable to set dir for rst gpio\n",
							__func__);
						goto exit;
					}
				}

				for (i = 0; i < pdata->panel_info.rst_seq_len; ++i) {
					gpio_set_value((ctrl_pdata->rst_gpio), pdata->panel_info.rst_seq[i]);
					if (pdata->panel_info.rst_seq[++i])
						usleep_range(pinfo->rst_seq[i] * 1000, pinfo->rst_seq[i] * 1000);
				}
				mdelay(50);
			} else if(is_lcd(OPPO16103_JDI_R63452_1080P_CMD_PANEL)){
				lm3697_bl_enable(1);
				//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
				//add for lcd power timing
				if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
					rc = gpio_direction_output(ctrl_pdata->disp_en_gpio, 1);
					if (rc) {
						pr_err("%s: unable to set dir for en gpio\n",
							__func__);
						goto exit;
					}
				}

				/*
				 * add for +-5V second resource delay 2ms to 3ms
				 */
				mdelay(TPS65132_DELAY_3MS);

				if (gpio_is_valid(ctrl_pdata->bklt_en_gpio)) {
					rc = gpio_direction_output(ctrl_pdata->bklt_en_gpio, 1);
					if (rc) {
						pr_err("%s: unable to set dir for bklt gpio\n",
							__func__);
						goto exit;
					}
				}
				/*
				 * add for 16103 LCD delay between -5v and RST, which need >10ms
				 */
				mdelay(12);

				if (pdata->panel_info.rst_seq_len) {
					rc = gpio_direction_output(ctrl_pdata->rst_gpio,
						pdata->panel_info.rst_seq[0]);
					if (rc) {
						pr_err("%s: unable to set dir for rst gpio\n",
							__func__);
						goto exit;
					}
				}

				for (i = 0; i < pdata->panel_info.rst_seq_len; ++i) {
					gpio_set_value((ctrl_pdata->rst_gpio), pdata->panel_info.rst_seq[i]);
					if (pdata->panel_info.rst_seq[++i])
						usleep_range(pinfo->rst_seq[i] * 1000, pinfo->rst_seq[i] * 1000);
				}

			}else{
				if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
					rc = gpio_direction_output(
						ctrl_pdata->disp_en_gpio, 1);
					if (rc) {
						pr_err("%s: unable to set dir for en gpio\n",
							__func__);
						goto exit;
					}
				}

				/*
				 * add for delay between -5v and RST, which need >10ms
				 */
				mdelay(12);

				if (pdata->panel_info.rst_seq_len) {
					rc = gpio_direction_output(ctrl_pdata->rst_gpio,
						pdata->panel_info.rst_seq[0]);
					if (rc) {
						pr_err("%s: unable to set dir for rst gpio\n",
							__func__);
						goto exit;
					}
				}

				for (i = 0; i < pdata->panel_info.rst_seq_len; ++i) {
					gpio_set_value((ctrl_pdata->rst_gpio),
						pdata->panel_info.rst_seq[i]);
					if (pdata->panel_info.rst_seq[++i])
						usleep_range(pinfo->rst_seq[i] * 1000, pinfo->rst_seq[i] * 1000);
				}

				if (gpio_is_valid(ctrl_pdata->bklt_en_gpio)) {
					rc = gpio_direction_output(
						ctrl_pdata->bklt_en_gpio, 1);
					if (rc) {
						pr_err("%s: unable to set dir for bklt gpio\n",
							__func__);
						goto exit;
					}
				}
			}
		#endif /*VEDNOR_EDIT*/
		}

#ifndef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//delete for not used
		if (gpio_is_valid(ctrl_pdata->lcd_mode_sel_gpio)) {
			bool out = false;

			if ((pinfo->mode_sel_state == MODE_SEL_SINGLE_PORT) ||
				(pinfo->mode_sel_state == MODE_GPIO_HIGH))
				out = true;
			else if ((pinfo->mode_sel_state == MODE_SEL_DUAL_PORT)
				|| (pinfo->mode_sel_state == MODE_GPIO_LOW))
				out = false;

			rc = gpio_direction_output(
					ctrl_pdata->lcd_mode_sel_gpio, out);
			if (rc) {
				pr_err("%s: unable to set dir for mode gpio\n",
					__func__);
				goto exit;
			}
		}
#endif /*VENDOR_EDIT*/

		if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
			pr_debug("%s: Panel Not properly turned OFF\n",
						__func__);
			ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
			pr_debug("%s: Reset panel done\n", __func__);
		}
	} else {
#ifndef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//remove for not used
		if (gpio_is_valid(ctrl_pdata->avdd_en_gpio)) {
			if (ctrl_pdata->avdd_en_gpio_invert)
				gpio_set_value((ctrl_pdata->avdd_en_gpio), 1);
			else
				gpio_set_value((ctrl_pdata->avdd_en_gpio), 0);

			gpio_free(ctrl_pdata->avdd_en_gpio);
		}
		if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
			gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
			gpio_free(ctrl_pdata->disp_en_gpio);
		}
		gpio_set_value((ctrl_pdata->rst_gpio), 0);
		gpio_free(ctrl_pdata->rst_gpio);
		if (gpio_is_valid(ctrl_pdata->lcd_mode_sel_gpio)) {
			gpio_set_value(ctrl_pdata->lcd_mode_sel_gpio, 0);
			gpio_free(ctrl_pdata->lcd_mode_sel_gpio);
		}
#else /*VENDOR_EDIT*/
		if(is_lcd(OPPO16103_JDI_R63452_1080P_CMD_PANEL)){
			lm3697_bl_enable(0);

			/*
			 * add for lcd esd recovery power off when tp black gesture open
			 */
			if((0 != mdss_tp_black_gesture_status())&& lcd_esd_status){
				pr_err("mdss_dsi_panel_reset: synaptics black tp on, keep lcd power on\n");
				if (gpio_is_valid(ctrl_pdata->bklt_en_gpio))
					gpio_free(ctrl_pdata->bklt_en_gpio);
				gpio_free(ctrl_pdata->rst_gpio);
				if (gpio_is_valid(ctrl_pdata->disp_en_gpio))
					gpio_free(ctrl_pdata->disp_en_gpio);
				return 0;
			}

			gpio_set_value((ctrl_pdata->rst_gpio), 0);
			gpio_free(ctrl_pdata->rst_gpio);
			mdelay(8);

			if (gpio_is_valid(ctrl_pdata->bklt_en_gpio)) {
				gpio_set_value((ctrl_pdata->bklt_en_gpio), 0);
				gpio_free(ctrl_pdata->bklt_en_gpio);
			}
			mdelay(12);
			if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
				gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
				gpio_free(ctrl_pdata->disp_en_gpio);
			}
			mdelay(12);
		} else if(is_lcd(OPPO18136_HIMAX_NT36772A_1080_2340_VOD_PANEL)
			|| is_lcd(OPPO18136_HIMAX_HX83112A_1080_2340_VOD_PANEL)
			|| is_lcd(OPPO18321_DPT_NT36672A_1080_2340_VOD_PANEL))
		{
			if((0 != mdss_tp_black_gesture_status())&& lcd_esd_status){
				pr_err("mdss_dsi_panel_reset: synaptics black tp on, keep lcd power on\n");
				if (gpio_is_valid(ctrl_pdata->rst_gpio)) {
					gpio_free(ctrl_pdata->rst_gpio);
				}

				if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
					gpio_free(ctrl_pdata->disp_en_gpio);
				}

				if (gpio_is_valid(ctrl_pdata->disp_enn_gpio)) {
					gpio_free(ctrl_pdata->disp_enn_gpio);
				}
			} else {
				if (!is_lcd(OPPO18321_DPT_NT36672A_1080_2340_VOD_PANEL))
				{
					if (gpio_is_valid(ctrl_pdata->rst_gpio)) {
						gpio_set_value((ctrl_pdata->rst_gpio), 0);
						gpio_free(ctrl_pdata->rst_gpio);
					}
				} else {
					if (gpio_is_valid(ctrl_pdata->rst_gpio)) {
						gpio_free(ctrl_pdata->rst_gpio);
					}
				}
				mdelay(8);

				if (gpio_is_valid(ctrl_pdata->disp_enn_gpio)) {
					gpio_set_value((ctrl_pdata->disp_enn_gpio), 0);
					gpio_free(ctrl_pdata->disp_enn_gpio);
				}
				mdelay(5);

				if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
					gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
					gpio_free(ctrl_pdata->disp_en_gpio);
				}
				mdelay(100);
			}
		}else{
			/* add delay make sure mipi off before rst */
			mdelay(5);
			gpio_set_value((ctrl_pdata->rst_gpio), 0);
			gpio_free(ctrl_pdata->rst_gpio);
			if (is_lcd(OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
				|| is_lcd(OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)) {
				mdelay(20);
			} else {
				mdelay(10);
			}


			if (gpio_is_valid(ctrl_pdata->bklt_en_gpio)) {
				gpio_set_value((ctrl_pdata->bklt_en_gpio), 0);
				gpio_free(ctrl_pdata->bklt_en_gpio);
			}
			mdelay(5);

			if (gpio_is_valid(ctrl_pdata->disp_en_gpio)) {
				gpio_set_value((ctrl_pdata->disp_en_gpio), 0);
				gpio_free(ctrl_pdata->disp_en_gpio);
			}
			mdelay(5);

			if (gpio_is_valid(ctrl_pdata->lcd_mode_sel_gpio)) {
				gpio_set_value(ctrl_pdata->lcd_mode_sel_gpio, 0);
				gpio_free(ctrl_pdata->lcd_mode_sel_gpio);
			}
		}
#endif /*VEDNOR_EDIT*/
	}

exit:
	return rc;
}

#ifdef VENDOR_EDIT
/*
* Guoqiang.Jiang@PSW.MM.Display.LCD.Machine, 2018/10/30,
* add for lcd rst before lp11
*/
int oppo_reset_before_lp11(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	int i, rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	/* Do not do rst_gpio reset on other panel. */
	if (!is_lcd(OPPO18321_DPT_NT36672A_1080_2340_VOD_PANEL))
	{
		return 0;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_debug("%s:%d, reset line not configured\n",__func__, __LINE__);
		return rc;
	}

	//rc = mdss_dsi_request_gpios(ctrl_pdata);
	if (rc) {
		pr_err("gpio request failed\n");
		return rc;
	}

	if (pdata->panel_info.rst_seq_len) {
		rc = gpio_direction_output(ctrl_pdata->rst_gpio,
			pdata->panel_info.rst_seq[0]);
		if (rc) {
			pr_err("%s: unable to set dir for rst gpio\n",__func__);
			goto exit;
		}
	}

	for (i = 2; i < pdata->panel_info.rst_seq_len; ++i) {
		gpio_set_value((ctrl_pdata->rst_gpio),
			pdata->panel_info.rst_seq[i]);
		if (pdata->panel_info.rst_seq[++i])
			usleep_range(pinfo->rst_seq[i] * 1000,
				     pinfo->rst_seq[i] * 1000);
	}
	pr_debug("%s: done\n", __func__);
exit:
	return rc;
}
#endif /*VENDOR_EDIT*/

/**
 * mdss_dsi_roi_merge() -  merge two roi into single roi
 *
 * Function used by partial update with only one dsi intf take 2A/2B
 * (column/page) dcs commands.
 */
static int mdss_dsi_roi_merge(struct mdss_dsi_ctrl_pdata *ctrl,
					struct mdss_rect *roi)
{
	struct mdss_panel_info *l_pinfo;
	struct mdss_rect *l_roi;
	struct mdss_rect *r_roi;
	struct mdss_dsi_ctrl_pdata *other = NULL;
	int ans = 0;

	if (ctrl->ndx == DSI_CTRL_LEFT) {
		other = mdss_dsi_get_ctrl_by_index(DSI_CTRL_RIGHT);
		if (!other)
			return ans;
		l_pinfo = &(ctrl->panel_data.panel_info);
		l_roi = &(ctrl->panel_data.panel_info.roi);
		r_roi = &(other->panel_data.panel_info.roi);
	} else  {
		other = mdss_dsi_get_ctrl_by_index(DSI_CTRL_LEFT);
		if (!other)
			return ans;
		l_pinfo = &(other->panel_data.panel_info);
		l_roi = &(other->panel_data.panel_info.roi);
		r_roi = &(ctrl->panel_data.panel_info.roi);
	}

	if (l_roi->w == 0 && l_roi->h == 0) {
		/* right only */
		*roi = *r_roi;
		roi->x += l_pinfo->xres;/* add left full width to x-offset */
	} else {
		/* left only and left+righ */
		*roi = *l_roi;
		roi->w +=  r_roi->w; /* add right width */
		ans = 1;
	}

	return ans;
}

static char caset[] = {0x2a, 0x00, 0x00, 0x03, 0x00};	/* DTYPE_DCS_LWRITE */
static char paset[] = {0x2b, 0x00, 0x00, 0x05, 0x00};	/* DTYPE_DCS_LWRITE */

/*
 * Some panels can support multiple ROIs as part of the below commands
 */
static char caset_dual[] = {0x2a, 0x00, 0x00, 0x03, 0x00, 0x03,
				0x00, 0x00, 0x00, 0x00};/* DTYPE_DCS_LWRITE */
static char paset_dual[] = {0x2b, 0x00, 0x00, 0x05, 0x00, 0x03,
				0x00, 0x00, 0x00, 0x00};/* DTYPE_DCS_LWRITE */

/* pack into one frame before sent */
static struct dsi_cmd_desc set_col_page_addr_cmd[] = {
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 1, sizeof(caset)}, caset},	/* packed */
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 1, sizeof(paset)}, paset},
};

/* pack into one frame before sent */
static struct dsi_cmd_desc set_dual_col_page_addr_cmd[] = {	/*packed*/
	{{DTYPE_DCS_LWRITE, 0, 0, 0, 1, sizeof(caset_dual)}, caset_dual},
	{{DTYPE_DCS_LWRITE, 1, 0, 0, 1, sizeof(paset_dual)}, paset_dual},
};


static void __mdss_dsi_send_col_page_addr(struct mdss_dsi_ctrl_pdata *ctrl,
		struct mdss_rect *roi, bool dual_roi)
{
	if (dual_roi) {
		struct mdss_rect *first, *second;

		first = &ctrl->panel_data.panel_info.dual_roi.first_roi;
		second = &ctrl->panel_data.panel_info.dual_roi.second_roi;

		caset_dual[1] = (((first->x) & 0xFF00) >> 8);
		caset_dual[2] = (((first->x) & 0xFF));
		caset_dual[3] = (((first->x - 1 + first->w) & 0xFF00) >> 8);
		caset_dual[4] = (((first->x - 1 + first->w) & 0xFF));
		/* skip the MPU setting byte*/
		caset_dual[6] = (((second->x) & 0xFF00) >> 8);
		caset_dual[7] = (((second->x) & 0xFF));
		caset_dual[8] = (((second->x - 1 + second->w) & 0xFF00) >> 8);
		caset_dual[9] = (((second->x - 1 + second->w) & 0xFF));
		set_dual_col_page_addr_cmd[0].payload = caset_dual;

		paset_dual[1] = (((first->y) & 0xFF00) >> 8);
		paset_dual[2] = (((first->y) & 0xFF));
		paset_dual[3] = (((first->y - 1 + first->h) & 0xFF00) >> 8);
		paset_dual[4] = (((first->y - 1 + first->h) & 0xFF));
		/* skip the MPU setting byte */
		paset_dual[6] = (((second->y) & 0xFF00) >> 8);
		paset_dual[7] = (((second->y) & 0xFF));
		paset_dual[8] = (((second->y - 1 + second->h) & 0xFF00) >> 8);
		paset_dual[9] = (((second->y - 1 + second->h) & 0xFF));
		set_dual_col_page_addr_cmd[1].payload = paset_dual;
	} else {
		caset[1] = (((roi->x) & 0xFF00) >> 8);
		caset[2] = (((roi->x) & 0xFF));
		caset[3] = (((roi->x - 1 + roi->w) & 0xFF00) >> 8);
		caset[4] = (((roi->x - 1 + roi->w) & 0xFF));
		set_col_page_addr_cmd[0].payload = caset;

		paset[1] = (((roi->y) & 0xFF00) >> 8);
		paset[2] = (((roi->y) & 0xFF));
		paset[3] = (((roi->y - 1 + roi->h) & 0xFF00) >> 8);
		paset[4] = (((roi->y - 1 + roi->h) & 0xFF));
		set_col_page_addr_cmd[1].payload = paset;
	}
	pr_debug("%s Sending 2A 2B cmnd with dual_roi=%d\n", __func__,
			dual_roi);

}
static void mdss_dsi_send_col_page_addr(struct mdss_dsi_ctrl_pdata *ctrl,
				struct mdss_rect *roi, int unicast)
{
	struct dcs_cmd_req cmdreq;
	struct mdss_panel_info *pinfo = &ctrl->panel_data.panel_info;
	bool dual_roi = pinfo->dual_roi.enabled;

	__mdss_dsi_send_col_page_addr(ctrl, roi, dual_roi);

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds_cnt = 2;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	if (unicast)
		cmdreq.flags |= CMD_REQ_UNICAST;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	/* Send default or dual roi 2A/2B cmd */
	cmdreq.cmds = dual_roi ? set_dual_col_page_addr_cmd :
		set_col_page_addr_cmd;
	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
}

static int mdss_dsi_set_col_page_addr(struct mdss_panel_data *pdata,
		bool force_send)
{
	struct mdss_panel_info *pinfo;
	struct mdss_rect roi = {0};
	struct mdss_rect *p_roi;
	struct mdss_rect *c_roi;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_dsi_ctrl_pdata *other = NULL;
	int left_or_both = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &pdata->panel_info;
	p_roi = &pinfo->roi;

	/*
	 * to avoid keep sending same col_page info to panel,
	 * if roi_merge enabled, the roi of left ctrl is used
	 * to compare against new merged roi and saved new
	 * merged roi to it after comparing.
	 * if roi_merge disabled, then the calling ctrl's roi
	 * and pinfo's roi are used to compare.
	 */
	if (pinfo->partial_update_roi_merge) {
		left_or_both = mdss_dsi_roi_merge(ctrl, &roi);
		other = mdss_dsi_get_ctrl_by_index(DSI_CTRL_LEFT);
		c_roi = &other->roi;
	} else {
		c_roi = &ctrl->roi;
		roi = *p_roi;
	}

	/* roi had changed, do col_page update */
	if (force_send || !mdss_rect_cmp(c_roi, &roi)) {
		pr_debug("%s: ndx=%d x=%d y=%d w=%d h=%d\n",
				__func__, ctrl->ndx, p_roi->x,
				p_roi->y, p_roi->w, p_roi->h);

		*c_roi = roi; /* keep to ctrl */
		if (c_roi->w == 0 || c_roi->h == 0) {
			/* no new frame update */
			pr_debug("%s: ctrl=%d, no partial roi set\n",
						__func__, ctrl->ndx);
			return 0;
		}

		if (pinfo->dcs_cmd_by_left) {
			if (left_or_both && ctrl->ndx == DSI_CTRL_RIGHT) {
				/* 2A/2B sent by left already */
				return 0;
			}
		}

		if (!mdss_dsi_sync_wait_enable(ctrl)) {
			if (pinfo->dcs_cmd_by_left)
				ctrl = mdss_dsi_get_ctrl_by_index(
							DSI_CTRL_LEFT);
			mdss_dsi_send_col_page_addr(ctrl, &roi, 0);
		} else {
			/*
			 * when sync_wait_broadcast enabled,
			 * need trigger at right ctrl to
			 * start both dcs cmd transmission
			 */
			other = mdss_dsi_get_other_ctrl(ctrl);
			if (!other)
				goto end;

			if (mdss_dsi_is_left_ctrl(ctrl)) {
				if (pinfo->partial_update_roi_merge) {
					/*
					 * roi is the one after merged
					 * to dsi-1 only
					 */
					mdss_dsi_send_col_page_addr(other,
							&roi, 0);
				} else {
					mdss_dsi_send_col_page_addr(ctrl,
							&ctrl->roi, 1);
					mdss_dsi_send_col_page_addr(other,
							&other->roi, 1);
				}
			} else {
				if (pinfo->partial_update_roi_merge) {
					/*
					 * roi is the one after merged
					 * to dsi-1 only
					 */
					mdss_dsi_send_col_page_addr(ctrl,
							&roi, 0);
				} else {
					mdss_dsi_send_col_page_addr(other,
							&other->roi, 1);
					mdss_dsi_send_col_page_addr(ctrl,
							&ctrl->roi, 1);
				}
			}
		}
	}

end:
	return 0;
}

static int mdss_dsi_panel_apply_display_setting(struct mdss_panel_data *pdata,
							u32 mode)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct dsi_panel_cmds *lp_on_cmds;
	struct dsi_panel_cmds *lp_off_cmds;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	lp_on_cmds = &ctrl->lp_on_cmds;
	lp_off_cmds = &ctrl->lp_off_cmds;

	/* Apply display settings for low-persistence mode */
	if ((mode == MDSS_PANEL_LOW_PERSIST_MODE_ON) &&
			(lp_on_cmds->cmd_cnt))
		mdss_dsi_panel_apply_settings(ctrl, lp_on_cmds);
	else if ((mode == MDSS_PANEL_LOW_PERSIST_MODE_OFF) &&
			(lp_on_cmds->cmd_cnt))
		mdss_dsi_panel_apply_settings(ctrl, lp_off_cmds);
	else
		return -EINVAL;

	pr_debug("%s: Persistence mode %d applied\n", __func__, mode);
	return 0;
}

static void mdss_dsi_panel_switch_mode(struct mdss_panel_data *pdata,
							int mode)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mipi_panel_info *mipi;
	struct dsi_panel_cmds *pcmds;
	u32 flags = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	mipi  = &pdata->panel_info.mipi;

	if (!mipi->dms_mode)
		return;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (mipi->dms_mode != DYNAMIC_MODE_RESOLUTION_SWITCH_IMMEDIATE) {
		flags |= CMD_REQ_COMMIT;
		if (mode == SWITCH_TO_CMD_MODE)
			pcmds = &ctrl_pdata->video2cmd;
		else
			pcmds = &ctrl_pdata->cmd2video;
	} else if ((mipi->dms_mode ==
				DYNAMIC_MODE_RESOLUTION_SWITCH_IMMEDIATE)
			&& pdata->current_timing
			&& !list_empty(&pdata->timings_list)) {
		struct dsi_panel_timing *pt;

		pt = container_of(pdata->current_timing,
				struct dsi_panel_timing, timing);

		pr_debug("%s: sending switch commands\n", __func__);
		pcmds = &pt->switch_cmds;
		flags |= CMD_REQ_DMA_TPG;
		flags |= CMD_REQ_COMMIT;
	} else {
		pr_warn("%s: Invalid mode switch attempted\n", __func__);
		return;
	}

	if ((pdata->panel_info.compression_mode == COMPRESSION_DSC) &&
			(pdata->panel_info.send_pps_before_switch))
		mdss_dsi_panel_dsc_pps_send(ctrl_pdata, &pdata->panel_info);

	mdss_dsi_panel_cmds_send(ctrl_pdata, pcmds, flags);

	if ((pdata->panel_info.compression_mode == COMPRESSION_DSC) &&
			(!pdata->panel_info.send_pps_before_switch))
		mdss_dsi_panel_dsc_pps_send(ctrl_pdata, &pdata->panel_info);
}
#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display, 2018/10/30
//modify for high brightness mode
unsigned int current_brightness = 0;
extern unsigned long outdoor_mode;
#endif /*VENDOR_EDIT*/

static void mdss_dsi_panel_bl_ctrl(struct mdss_panel_data *pdata,
							u32 bl_level)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_dsi_ctrl_pdata *sctrl = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/02/14,
//add for close bl for silence and sau mode
	if(lcd_closebl_flag){
		pr_info("%s -- MSM_BOOT_MODE__SILENCE\n",__func__);
		bl_level = 0;
	}
#endif /*VENDOR_EDIT*/
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/*
	 * Some backlight controllers specify a minimum duty cycle
	 * for the backlight brightness. If the brightness is less
	 * than it, the controller can malfunction.
	 */
#ifndef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//modify for backlight log print
	pr_debug("%s: bl_level:%d\n", __func__, bl_level);
#else /*VENDOR_EDIT*/
	if((print_bl < 3) || (bl_level < 10)){
		pr_err("%s: set bl_level=%d\n", __func__, bl_level);
		print_bl++;
	}
#endif /*VEDNOR_EDIT*/

	/* do not allow backlight to change when panel in disable mode */
	if (pdata->panel_disable_mode && (bl_level != 0))
		return;

	if ((bl_level < pdata->panel_info.bl_min) && (bl_level != 0))
		bl_level = pdata->panel_info.bl_min;

	/* enable the backlight gpio if present */
#ifndef VENDOR_EDIT
	mdss_dsi_bl_gpio_ctrl(pdata, bl_level);
#else /*VENDOR_EDIT*/
/* Shengjun.Gou@PSW.MM.Display.LCD.Stability, 2017/04/03, for backlight gpio is controled by vendor */
	if(!(is_lcd(OPPO16103_JDI_R63452_1080P_CMD_PANEL)&&is_project(OPPO_16103)))
	{
		mdss_dsi_bl_gpio_ctrl(pdata, bl_level);
	}
#endif /*VEDNOR_EDIT*/
#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/04/03,
//add for i2c backlight control
	if(is_lcd(OPPO16103_JDI_R63452_1080P_CMD_PANEL)){
		/*Ling.Guo@Swdp.MultiMedia.Display, 2017/04/28,modify for high brightness mode */
		current_brightness = bl_level;
		if(bl_level > 1){
			if(outdoor_mode == 0){
				bl_level = (bl_level*93)/100;
            }
        }

        lm3697_lcd_backlight_set_level(bl_level);
        return;
    }


#endif /*VENDOR_EDIT*/

	switch (ctrl_pdata->bklt_ctrl) {
	case BL_WLED:
#ifdef VENDOR_EDIT
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
 *add for wled debug
*/
		pr_debug("%s wled set backlight value:%d.\n",
			__func__, bl_level);
#endif /* VENDOR_EDIT */
		led_trigger_event(bl_led_trigger, bl_level);
		break;
	case BL_PWM:
		mdss_dsi_panel_bklt_pwm(ctrl_pdata, bl_level);
		break;
	case BL_DCS_CMD:
		if (!mdss_dsi_sync_wait_enable(ctrl_pdata)) {
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
			break;
		}
		/*
		 * DCS commands to update backlight are usually sent at
		 * the same time to both the controllers. However, if
		 * sync_wait is enabled, we need to ensure that the
		 * dcs commands are first sent to the non-trigger
		 * controller so that when the commands are triggered,
		 * both controllers receive it at the same time.
		 */
		sctrl = mdss_dsi_get_other_ctrl(ctrl_pdata);
		if (mdss_dsi_sync_wait_trigger(ctrl_pdata)) {
			if (sctrl)
				mdss_dsi_panel_bklt_dcs(sctrl, bl_level);
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
		} else {
			mdss_dsi_panel_bklt_dcs(ctrl_pdata, bl_level);
			if (sctrl)
				mdss_dsi_panel_bklt_dcs(sctrl, bl_level);
		}
		break;
	default:
		pr_err("%s: Unknown bl_ctrl configuration\n",
			__func__);
		break;
	}
}

static int mdss_dsi_panel_on(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;
	struct dsi_panel_cmds *on_cmds;
#ifdef VENDOR_EDIT
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/1/31,add for support aod feature, solve bug:1264744*/
	struct dsi_panel_cmds *doze_on_cmds = NULL;
#endif /*VENDOR_EDIT*/
	int ret = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

#ifndef VENDOR_EDIT
//Guoqiang.Jiang@MultiMedia.Display.LCD.Stability, 2018/10/30,
//modify for panel debug
	pr_debug("%s: ndx=%d\n", __func__, ctrl->ndx);
#else /*VENDOR_EDIT*/
	pr_err("%s: ndx=%d\n", __func__, ctrl->ndx);
#endif /*VEDNOR_EDIT*/

	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			goto end;
	}

	on_cmds = &ctrl->on_cmds;

	if ((pinfo->mipi.dms_mode == DYNAMIC_MODE_SWITCH_IMMEDIATE) &&
			(pinfo->mipi.boot_mode != pinfo->mipi.mode))
		on_cmds = &ctrl->post_dms_on_cmds;

	pr_debug("%s: ndx=%d cmd_cnt=%d\n", __func__,
				ctrl->ndx, on_cmds->cmd_cnt);

#ifdef VENDOR_EDIT
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/1/31,add for support aod feature, solve bug:1264744*/
	if((lcd_vendor == OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))
	{
		if(request_enter_aod == false) {
			if (on_cmds->cmd_cnt)
				mdss_dsi_panel_cmds_send(ctrl, on_cmds, CMD_REQ_COMMIT);
		} else if(request_enter_aod == true) {
			doze_on_cmds = &ctrl->aod_on_cmds;
			if (doze_on_cmds->cmd_cnt)
				mdss_dsi_panel_cmds_send(ctrl, doze_on_cmds, CMD_REQ_COMMIT);
		}
	}else {
		if (on_cmds->cmd_cnt)
			mdss_dsi_panel_cmds_send(ctrl, on_cmds, CMD_REQ_COMMIT);
	}
#endif /*VENDOR_EDIT*/
	if (pinfo->compression_mode == COMPRESSION_DSC)
		mdss_dsi_panel_dsc_pps_send(ctrl, pinfo);

	if (ctrl->ds_registered)
		mdss_dba_utils_video_on(pinfo->dba_data, pinfo);

	/* Ensure low persistence mode is set as before */
	mdss_dsi_panel_apply_display_setting(pdata, pinfo->persist_mode);

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@MultiMedia.Display.LCD.Stability, 2018/10/30,
//add for lcd cabc
	if(is_lcd(OPPO16103_JDI_R63452_1080P_CMD_PANEL)){
		lm3697_reg_init();

		if(cabc_mode != CABC_HIGH_MODE){
			set_cabc_resume_mode(cabc_mode);
		}
		//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
		//add for lcd esd recovery power off when tp black gesture open
		if(!lcd_esd_status){
			lcd_esd_status = 1;
			pr_debug("%s: lcd_esd_status=%d\n", __func__, lcd_esd_status);
		}
	}

	if(is_lcd(OPPO18136_HIMAX_HX83112A_1080_2340_VOD_PANEL)
		|| is_lcd(OPPO18136_HIMAX_NT36772A_1080_2340_VOD_PANEL)
		|| is_lcd(OPPO18321_DPT_NT36672A_1080_2340_VOD_PANEL))
	{
		lcd_esd_status = 1;
	}

	//Guoqiang.Jiang@MM.Display.LCD.Stability, 2017/02/18,
	//add for seed mode
	if(is_lcd(OPPO16051_SAMSUNG_S6E3FA5_1080P_CMD_PANEL) || is_lcd(OPPO16118_SAMSUNG_S6E3FA5_1080P_CMD_PANEL)
		|| is_lcd(OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL) || is_lcd(OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| is_lcd(OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL) || is_lcd(OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL)){
		if(seed_mode != SEED_MODE0){
			set_seed_resume_mode(seed_mode);
		}
	}
	#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@MultiMedia.Display.LCD.Stability, 2017/01/24,
//add for lcd debug
/*jie.hu@PSW.MM.Display.LCD.Stability,2018/2/14,add for support ffl feature better, solve bug:1208496*/
	if(request_enter_aod == false) {
		mutex_lock(&lcd_mutex);
		flag_lcd_off = false;
		mutex_unlock(&lcd_mutex);
	} else if(request_enter_aod == true) {
		mutex_lock(&lcd_mutex);
		flag_lcd_off = true;
		mutex_unlock(&lcd_mutex);
	}
#endif /*VENDOR_EDIT*/
end:
#ifndef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/30,
//modify for panel debug
	pr_debug("%s:-\n", __func__);
#else /*VENDOR_EDIT*/
	pr_err("%s:-\n", __func__);
#endif /*VEDNOR_EDIT*/
	return ret;
}

static int mdss_dsi_post_panel_on(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;
	struct dsi_panel_cmds *cmds;
	u32 vsync_period = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s: ctrl=%pK ndx=%d\n", __func__, ctrl, ctrl->ndx);

	pinfo = &pdata->panel_info;
	if (pinfo->dcs_cmd_by_left && ctrl->ndx != DSI_CTRL_LEFT)
			goto end;

	cmds = &ctrl->post_panel_on_cmds;
	if (cmds->cmd_cnt) {
		msleep(VSYNC_DELAY);	/* wait for a vsync passed */
		mdss_dsi_panel_cmds_send(ctrl, cmds, CMD_REQ_COMMIT);
	}

	if (pinfo->is_dba_panel && pinfo->is_pluggable) {
		/* ensure at least 1 frame transfers to down stream device */
		vsync_period = (MSEC_PER_SEC / pinfo->mipi.frame_rate) + 1;
		msleep(vsync_period);
		mdss_dba_utils_hdcp_enable(pinfo->dba_data, true);
	}

end:
	pr_debug("%s:-\n", __func__);
	return 0;
}

static int mdss_dsi_panel_off(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s: ctrl=%pK ndx=%d\n", __func__, ctrl, ctrl->ndx);

	if (pinfo->dcs_cmd_by_left) {
		if (ctrl->ndx != DSI_CTRL_LEFT)
			goto end;
	}

	if (ctrl->off_cmds.cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, &ctrl->off_cmds, CMD_REQ_COMMIT);

	if (ctrl->ds_registered && pinfo->is_pluggable) {
		mdss_dba_utils_video_off(pinfo->dba_data);
		mdss_dba_utils_hdcp_enable(pinfo->dba_data, false);
	}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/01/24,
//add for panel debug
	mutex_lock(&lcd_mutex);
	flag_lcd_off = true;
	mutex_unlock(&lcd_mutex);

	/*
	 * add for cabc default mode
	 */
	cabc_mode = CABC_HIGH_MODE;
#endif /*VENDOR_EDIT*/

end:
	pr_debug("%s:-\n", __func__);
	return 0;
}

static int mdss_dsi_panel_low_power_config(struct mdss_panel_data *pdata,
	int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct mdss_panel_info *pinfo;
#ifdef VENDOR_EDIT
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/1/31,add for support aod feature, solve bug:1264744*/
	struct dsi_panel_cmds *doze_off_cmds = NULL;
#endif /*VENDOR_EDIT*/

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	pinfo = &pdata->panel_info;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s: ctrl=%pK ndx=%d enable=%d\n", __func__, ctrl, ctrl->ndx,
		enable);

	/* Any panel specific low power commands/config */
#ifdef VENDOR_EDIT
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/1/31,add for support aod feature, solve bug:1264744*/
	if((lcd_vendor == OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))
	{
		doze_off_cmds = &ctrl->aod_off_cmds;
		if(enable == true) {
			pr_debug("%s: enter aod but do noting at here\n", __func__);
		}
		else {

			mutex_lock(&aod_lock);
			request_enter_aod = false;
			is_just_exit_aod = true;
			mutex_unlock(&aod_lock);
			if (doze_off_cmds->cmd_cnt)
			{
				mdss_dsi_panel_cmds_send(ctrl, doze_off_cmds, CMD_REQ_COMMIT);
				/*add for set seed mode when aod off.*/
				set_seed_resume_mode(seed_mode);
			}
/*jie.hu@PSW.MM.Display.LCD.Stability,2018/2/14,add for support ffl feature better, solve bug:1208496*/
			mutex_lock(&lcd_mutex);
			flag_lcd_off = false;
			mutex_unlock(&lcd_mutex);
		}
	}
#endif /*VENDOR_EDIT*/

	pr_debug("%s:-\n", __func__);
	return 0;
}

static void mdss_dsi_parse_mdp_kickoff_threshold(struct device_node *np,
	struct mdss_panel_info *pinfo)
{
	int len, rc;
	const u32 *src;
	u32 tmp;
	u32 max_delay_us;

	pinfo->mdp_koff_thshold = false;
	src = of_get_property(np, "qcom,mdss-mdp-kickoff-threshold", &len);
	if (!src || (len == 0))
		return;

	rc = of_property_read_u32(np, "qcom,mdss-mdp-kickoff-delay", &tmp);
	if (!rc)
		pinfo->mdp_koff_delay = tmp;
	else
		return;

	if (pinfo->mipi.frame_rate == 0) {
		pr_err("cannot enable guard window, unexpected panel fps\n");
		return;
	}

	pinfo->mdp_koff_thshold_low = be32_to_cpu(src[0]);
	pinfo->mdp_koff_thshold_high = be32_to_cpu(src[1]);
	max_delay_us = 1000000 / pinfo->mipi.frame_rate;

	/* enable the feature if threshold is valid */
	if ((pinfo->mdp_koff_thshold_low < pinfo->mdp_koff_thshold_high) &&
	   ((pinfo->mdp_koff_delay > 0) ||
	    (pinfo->mdp_koff_delay < max_delay_us)))
		pinfo->mdp_koff_thshold = true;

	pr_debug("panel kickoff thshold:[%d, %d] delay:%d (max:%d) enable:%d\n",
		pinfo->mdp_koff_thshold_low,
		pinfo->mdp_koff_thshold_high,
		pinfo->mdp_koff_delay,
		max_delay_us,
		pinfo->mdp_koff_thshold);
}

static void mdss_dsi_parse_trigger(struct device_node *np, char *trigger,
		char *trigger_key)
{
	const char *data;

	*trigger = DSI_CMD_TRIGGER_SW;
	data = of_get_property(np, trigger_key, NULL);
	if (data) {
		if (!strcmp(data, "none"))
			*trigger = DSI_CMD_TRIGGER_NONE;
		else if (!strcmp(data, "trigger_te"))
			*trigger = DSI_CMD_TRIGGER_TE;
		else if (!strcmp(data, "trigger_sw_seof"))
			*trigger = DSI_CMD_TRIGGER_SW_SEOF;
		else if (!strcmp(data, "trigger_sw_te"))
			*trigger = DSI_CMD_TRIGGER_SW_TE;
	}
}


static int mdss_dsi_parse_dcs_cmds(struct device_node *np,
		struct dsi_panel_cmds *pcmds, char *cmd_key, char *link_key)
{
	const char *data;
	int blen = 0, len;
	char *buf, *bp;
	struct dsi_ctrl_hdr *dchdr;
	int i, cnt;

	data = of_get_property(np, cmd_key, &blen);
	if (!data) {
		pr_err("%s: failed, key=%s\n", __func__, cmd_key);
		return -ENOMEM;
	}

	buf = kzalloc(sizeof(char) * blen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, data, blen);

	/* scan dcs commands */
	bp = buf;
	len = blen;
	cnt = 0;
	while (len >= sizeof(*dchdr)) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		dchdr->dlen = ntohs(dchdr->dlen);
		if (dchdr->dlen > len) {
			pr_err("%s: dtsi cmd=%x error, len=%d",
				__func__, dchdr->dtype, dchdr->dlen);
			goto exit_free;
		}
		bp += sizeof(*dchdr);
		len -= sizeof(*dchdr);
		bp += dchdr->dlen;
		len -= dchdr->dlen;
		cnt++;
	}

	if (len != 0) {
		pr_err("%s: dcs_cmd=%x len=%d error!",
				__func__, buf[0], blen);
		goto exit_free;
	}

	pcmds->cmds = kzalloc(cnt * sizeof(struct dsi_cmd_desc),
						GFP_KERNEL);
	if (!pcmds->cmds)
		goto exit_free;

	pcmds->cmd_cnt = cnt;
	pcmds->buf = buf;
	pcmds->blen = blen;

	bp = buf;
	len = blen;
	for (i = 0; i < cnt; i++) {
		dchdr = (struct dsi_ctrl_hdr *)bp;
		len -= sizeof(*dchdr);
		bp += sizeof(*dchdr);
		pcmds->cmds[i].dchdr = *dchdr;
		pcmds->cmds[i].payload = bp;
		bp += dchdr->dlen;
		len -= dchdr->dlen;
	}

	/*Set default link state to LP Mode*/
	pcmds->link_state = DSI_LP_MODE;

	if (link_key) {
		data = of_get_property(np, link_key, NULL);
		if (data && !strcmp(data, "dsi_hs_mode"))
			pcmds->link_state = DSI_HS_MODE;
		else
			pcmds->link_state = DSI_LP_MODE;
	}

	pr_debug("%s: dcs_cmd=%x len=%d, cmd_cnt=%d link_state=%d\n", __func__,
		pcmds->buf[0], pcmds->blen, pcmds->cmd_cnt, pcmds->link_state);

	return 0;

exit_free:
	kfree(buf);
	return -ENOMEM;
}


int mdss_panel_get_dst_fmt(u32 bpp, char mipi_mode, u32 pixel_packing,
				char *dst_format)
{
	int rc = 0;
	switch (bpp) {
	case 3:
		*dst_format = DSI_CMD_DST_FORMAT_RGB111;
		break;
	case 8:
		*dst_format = DSI_CMD_DST_FORMAT_RGB332;
		break;
	case 12:
		*dst_format = DSI_CMD_DST_FORMAT_RGB444;
		break;
	case 16:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB565;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB565;
			break;
		default:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB565;
			break;
		}
		break;
	case 18:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			if (pixel_packing == 0)
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666;
			else
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666_LOOSE;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB666;
			break;
		default:
			if (pixel_packing == 0)
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666;
			else
				*dst_format = DSI_VIDEO_DST_FORMAT_RGB666_LOOSE;
			break;
		}
		break;
	case 24:
		switch (mipi_mode) {
		case DSI_VIDEO_MODE:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB888;
			break;
		case DSI_CMD_MODE:
			*dst_format = DSI_CMD_DST_FORMAT_RGB888;
			break;
		default:
			*dst_format = DSI_VIDEO_DST_FORMAT_RGB888;
			break;
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int mdss_dsi_parse_fbc_params(struct device_node *np,
			struct mdss_panel_timing *timing)
{
	int rc, fbc_enabled = 0;
	u32 tmp;
	struct fbc_panel_info *fbc = &timing->fbc;

	fbc_enabled = of_property_read_bool(np,	"qcom,mdss-dsi-fbc-enable");
	if (fbc_enabled) {
		pr_debug("%s:%d FBC panel enabled.\n", __func__, __LINE__);
		fbc->enabled = 1;
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-bpp", &tmp);
		fbc->target_bpp = (!rc ? tmp : 24);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-packing",
				&tmp);
		fbc->comp_mode = (!rc ? tmp : 0);
		fbc->qerr_enable = of_property_read_bool(np,
			"qcom,mdss-dsi-fbc-quant-error");
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-bias", &tmp);
		fbc->cd_bias = (!rc ? tmp : 0);
		fbc->pat_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-pat-mode");
		fbc->vlc_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-vlc-mode");
		fbc->bflc_enable = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-bflc-mode");
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-h-line-budget",
				&tmp);
		fbc->line_x_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-budget-ctrl",
				&tmp);
		fbc->block_x_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-block-budget",
				&tmp);
		fbc->block_budget = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossless-threshold", &tmp);
		fbc->lossless_mode_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossy-threshold", &tmp);
		fbc->lossy_mode_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np, "qcom,mdss-dsi-fbc-rgb-threshold",
				&tmp);
		fbc->lossy_rgb_thd = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-lossy-mode-idx", &tmp);
		fbc->lossy_mode_idx = (!rc ? tmp : 0);
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-slice-height", &tmp);
		fbc->slice_height = (!rc ? tmp : 0);
		fbc->pred_mode = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-2d-pred-mode");
		fbc->enc_mode = of_property_read_bool(np,
				"qcom,mdss-dsi-fbc-ver2-mode");
		rc = of_property_read_u32(np,
				"qcom,mdss-dsi-fbc-max-pred-err", &tmp);
		fbc->max_pred_err = (!rc ? tmp : 0);

		timing->compression_mode = COMPRESSION_FBC;
	} else {
		pr_debug("%s:%d Panel does not support FBC.\n",
				__func__, __LINE__);
		fbc->enabled = 0;
		fbc->target_bpp = 24;
	}
	return 0;
}

void mdss_dsi_panel_dsc_pps_send(struct mdss_dsi_ctrl_pdata *ctrl,
				struct mdss_panel_info *pinfo)
{
	struct dsi_panel_cmds pcmds;
	struct dsi_cmd_desc cmd;

	if (!pinfo || (pinfo->compression_mode != COMPRESSION_DSC))
		return;

	memset(&pcmds, 0, sizeof(pcmds));
	memset(&cmd, 0, sizeof(cmd));

	cmd.dchdr.dlen = mdss_panel_dsc_prepare_pps_buf(&pinfo->dsc,
		ctrl->pps_buf, 0);
	cmd.dchdr.dtype = DTYPE_PPS;
	cmd.dchdr.last = 1;
	cmd.dchdr.wait = 10;
	cmd.dchdr.vc = 0;
	cmd.dchdr.ack = 0;
	cmd.payload = ctrl->pps_buf;

	pcmds.cmd_cnt = 1;
	pcmds.cmds = &cmd;
	pcmds.link_state = DSI_LP_MODE;

	mdss_dsi_panel_cmds_send(ctrl, &pcmds, CMD_REQ_COMMIT);
}

static int mdss_dsi_parse_hdr_settings(struct device_node *np,
		struct mdss_panel_info *pinfo)
{
	int rc = 0;
	struct mdss_panel_hdr_properties *hdr_prop;

	if (!np) {
		pr_err("%s: device node pointer is NULL\n", __func__);
		return -EINVAL;
	}

	if (!pinfo) {
		pr_err("%s: panel info is NULL\n", __func__);
		return -EINVAL;
	}

	hdr_prop = &pinfo->hdr_properties;
	hdr_prop->hdr_enabled = of_property_read_bool(np,
		"qcom,mdss-dsi-panel-hdr-enabled");

	if (hdr_prop->hdr_enabled) {
		rc = of_property_read_u32_array(np,
				"qcom,mdss-dsi-panel-hdr-color-primaries",
				hdr_prop->display_primaries,
				DISPLAY_PRIMARIES_COUNT);
		if (rc) {
			pr_info("%s:%d, Unable to read color primaries,rc:%u",
					__func__, __LINE__,
					hdr_prop->hdr_enabled = false);
			}

		rc = of_property_read_u32(np,
			"qcom,mdss-dsi-panel-peak-brightness",
			&(hdr_prop->peak_brightness));
		if (rc) {
			pr_info("%s:%d, Unable to read hdr brightness, rc:%u",
				__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
		}

		rc = of_property_read_u32(np,
			"qcom,mdss-dsi-panel-blackness-level",
			&(hdr_prop->blackness_level));
		if (rc) {
			pr_info("%s:%d, Unable to read hdr brightness, rc:%u",
				__func__, __LINE__, rc);
			hdr_prop->hdr_enabled = false;
		}
	}
	return 0;
}

static int mdss_dsi_parse_split_link_settings(struct device_node *np,
		struct mdss_panel_info *pinfo)
{
	u32 tmp;
	int rc = 0;

	if (!np) {
		pr_err("%s: device node pointer is NULL\n", __func__);
		return -EINVAL;
	}

	if (!pinfo) {
		pr_err("%s: panel info is NULL\n", __func__);
		return -EINVAL;
	}

	pinfo->split_link_enabled = of_property_read_bool(np,
		"qcom,split-link-enabled");

	if (pinfo->split_link_enabled) {
		rc = of_property_read_u32(np,
			"qcom,sublinks-count", &tmp);
		/* default num of sublink is 1*/
		pinfo->mipi.num_of_sublinks = (!rc ? tmp : 1);

		rc = of_property_read_u32(np,
			"qcom,lanes-per-sublink", &tmp);
		/* default num of lanes per sublink is 1 */
		pinfo->mipi.lanes_per_sublink = (!rc ? tmp : 1);
	}

	pr_info("%s: enable %d sublinks-count %d lanes per sublink %d\n",
		__func__, pinfo->split_link_enabled,
		pinfo->mipi.num_of_sublinks,
		pinfo->mipi.lanes_per_sublink);
	return 0;
}

static int mdss_dsi_parse_dsc_version(struct device_node *np,
		struct mdss_panel_timing *timing)
{
	u32 data;
	int rc = 0;
	struct dsc_desc *dsc = &timing->dsc;

	rc = of_property_read_u32(np, "qcom,mdss-dsc-version", &data);
	if (rc) {
		dsc->version = 0x11;
		rc = 0;
	} else {
		dsc->version = data & 0xff;
		/* only support DSC 1.1 rev */
		if (dsc->version != 0x11) {
			pr_err("%s: DSC version:%d not supported\n", __func__,
				dsc->version);
			rc = -EINVAL;
			goto end;
		}
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-scr-version", &data);
	if (rc) {
		dsc->scr_rev = 0x0;
		rc = 0;
	} else {
		dsc->scr_rev = data & 0xff;
		/* only one scr rev supported */
		if (dsc->scr_rev > 0x1) {
			pr_err("%s: DSC scr version:%d not supported\n",
				__func__, dsc->scr_rev);
			rc = -EINVAL;
			goto end;
		}
	}

end:
	return rc;
}

static int mdss_dsi_parse_dsc_params(struct device_node *np,
		struct mdss_panel_timing *timing, bool is_split_display)
{
	u32 data, intf_width;
	int rc = 0;
	struct dsc_desc *dsc = &timing->dsc;

	if (!np) {
		pr_err("%s: device node pointer is NULL\n", __func__);
		return -EINVAL;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-encoders", &data);
	if (rc) {
		if (!of_find_property(np, "qcom,mdss-dsc-encoders", NULL)) {
			/* property is not defined, default to 1 */
			data = 1;
		} else {
			pr_err("%s: Error parsing qcom,mdss-dsc-encoders\n",
				__func__);
			goto end;
		}
	}

	timing->dsc_enc_total = data;

	if (is_split_display && (timing->dsc_enc_total > 1)) {
		pr_err("%s: Error: for split displays, more than 1 dsc encoder per panel is not allowed.\n",
			__func__);
		goto end;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-slice-height", &data);
	if (rc)
		goto end;
	dsc->slice_height = data;

	rc = of_property_read_u32(np, "qcom,mdss-dsc-slice-width", &data);
	if (rc)
		goto end;
	dsc->slice_width = data;
	intf_width = timing->xres;

	if (intf_width % dsc->slice_width) {
		pr_err("%s: Error: multiple of slice-width:%d should match panel-width:%d\n",
			__func__, dsc->slice_width, intf_width);
		goto end;
	}

	data = intf_width / dsc->slice_width;
	if (((timing->dsc_enc_total > 1) && ((data != 2) && (data != 4))) ||
	    ((timing->dsc_enc_total == 1) && (data > 2))) {
		pr_err("%s: Error: max 2 slice per encoder. slice-width:%d should match panel-width:%d dsc_enc_total:%d\n",
			__func__, dsc->slice_width,
			intf_width, timing->dsc_enc_total);
		goto end;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsc-slice-per-pkt", &data);
	if (rc)
		goto end;
	dsc->slice_per_pkt = data;

	/*
	 * slice_per_pkt can be either 1 or all slices_per_intf
	 */
	if ((dsc->slice_per_pkt > 1) && (dsc->slice_per_pkt !=
			DIV_ROUND_UP(intf_width, dsc->slice_width))) {
		pr_err("Error: slice_per_pkt can be either 1 or all slices_per_intf\n");
		pr_err("%s: slice_per_pkt=%d, slice_width=%d intf_width=%d\n",
			__func__,
			dsc->slice_per_pkt, dsc->slice_width, intf_width);
		rc = -EINVAL;
		goto end;
	}

	pr_debug("%s: num_enc:%d :slice h=%d w=%d s_pkt=%d\n", __func__,
		timing->dsc_enc_total, dsc->slice_height,
		dsc->slice_width, dsc->slice_per_pkt);

	rc = of_property_read_u32(np, "qcom,mdss-dsc-bit-per-component", &data);
	if (rc)
		goto end;
	dsc->bpc = data;

	rc = of_property_read_u32(np, "qcom,mdss-dsc-bit-per-pixel", &data);
	if (rc)
		goto end;
	dsc->bpp = data;

	pr_debug("%s: bpc=%d bpp=%d\n", __func__,
		dsc->bpc, dsc->bpp);

	dsc->block_pred_enable = of_property_read_bool(np,
			"qcom,mdss-dsc-block-prediction-enable");

	dsc->enable_422 = 0;
	dsc->convert_rgb = 1;
	dsc->vbr_enable = 0;

	dsc->config_by_manufacture_cmd = of_property_read_bool(np,
		"qcom,mdss-dsc-config-by-manufacture-cmd");

	mdss_panel_dsc_parameters_calc(&timing->dsc);
	mdss_panel_dsc_pclk_param_calc(&timing->dsc, intf_width);

	timing->dsc.full_frame_slices =
		DIV_ROUND_UP(intf_width, timing->dsc.slice_width);

	timing->compression_mode = COMPRESSION_DSC;

end:
	return rc;
}

static struct device_node *mdss_dsi_panel_get_dsc_cfg_np(
		struct device_node *np, struct mdss_panel_data *panel_data,
		bool default_timing)
{
	struct device_node *dsc_cfg_np = NULL;


	/* Read the dsc config node specified by command line */
	if (default_timing) {
		dsc_cfg_np = of_get_child_by_name(np,
				panel_data->dsc_cfg_np_name);
		if (!dsc_cfg_np)
			pr_warn_once("%s: cannot find dsc config node:%s\n",
				__func__, panel_data->dsc_cfg_np_name);
	}

	/*
	 * Fall back to default from DT as nothing is specified
	 * in command line.
	 */
	if (!dsc_cfg_np && of_find_property(np, "qcom,config-select", NULL)) {
		dsc_cfg_np = of_parse_phandle(np, "qcom,config-select", 0);
		if (!dsc_cfg_np)
			pr_warn_once("%s:err parsing qcom,config-select\n",
					__func__);
	}

	return dsc_cfg_np;
}

static int mdss_dsi_parse_topology_config(struct device_node *np,
	struct dsi_panel_timing *pt, struct mdss_panel_data *panel_data,
	bool default_timing)
{
	int rc = 0;
	bool is_split_display = panel_data->panel_info.is_split_display;
	const char *data;
	struct mdss_panel_timing *timing = &pt->timing;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo;
	struct device_node *cfg_np = NULL;

	ctrl_pdata = container_of(panel_data, struct mdss_dsi_ctrl_pdata,
							panel_data);
	pinfo = &ctrl_pdata->panel_data.panel_info;

	cfg_np = mdss_dsi_panel_get_dsc_cfg_np(np,
				&ctrl_pdata->panel_data, default_timing);

	if (cfg_np) {
		if (!of_property_read_u32_array(cfg_np, "qcom,lm-split",
		    timing->lm_widths, 2)) {
			if (mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data)
			    && (timing->lm_widths[1] != 0)) {
				pr_err("%s: lm-split not allowed with split display\n",
					__func__);
				rc = -EINVAL;
				goto end;
			}
		}

		if (!of_property_read_string(cfg_np, "qcom,split-mode",
		    &data) && !strcmp(data, "pingpong-split"))
			pinfo->use_pingpong_split = true;

		if (((timing->lm_widths[0]) || (timing->lm_widths[1])) &&
		    pinfo->use_pingpong_split) {
			pr_err("%s: pingpong_split cannot be used when lm-split[%d,%d] is specified\n",
				__func__,
				timing->lm_widths[0], timing->lm_widths[1]);
			return -EINVAL;
		}

		pr_info("%s: cfg_node name %s lm_split:%dx%d pp_split:%s\n",
			__func__, cfg_np->name,
			timing->lm_widths[0], timing->lm_widths[1],
			pinfo->use_pingpong_split ? "yes" : "no");
	}

	if (!pinfo->use_pingpong_split &&
	    (timing->lm_widths[0] == 0) && (timing->lm_widths[1] == 0))
		timing->lm_widths[0] = pt->timing.xres;

	data = of_get_property(np, "qcom,compression-mode", NULL);
	if (data) {
		if (cfg_np && !strcmp(data, "dsc")) {
			rc = mdss_dsi_parse_dsc_version(np, &pt->timing);
			if (rc)
				goto end;

			pinfo->send_pps_before_switch =
				of_property_read_bool(np,
				"qcom,mdss-dsi-send-pps-before-switch");

			rc = mdss_dsi_parse_dsc_params(cfg_np, &pt->timing,
					is_split_display);
		} else if (!strcmp(data, "fbc")) {
			rc = mdss_dsi_parse_fbc_params(np, &pt->timing);
		}
	}

end:
	of_node_put(cfg_np);
	return rc;
}

static void mdss_panel_parse_te_params(struct device_node *np,
		struct mdss_panel_timing *timing)
{
	struct mdss_mdp_pp_tear_check *te = &timing->te;
	u32 tmp;
	int rc = 0;
	/*
	 * TE default: dsi byte clock calculated base on 70 fps;
	 * around 14 ms to complete a kickoff cycle if te disabled;
	 * vclk_line base on 60 fps; write is faster than read;
	 * init == start == rdptr;
	 */
	te->tear_check_en =
		!of_property_read_bool(np, "qcom,mdss-tear-check-disable");
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-cfg-height", &tmp);
	te->sync_cfg_height = (!rc ? tmp : 0xfff0);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-init-val", &tmp);
	te->vsync_init_val = (!rc ? tmp : timing->yres);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-threshold-start", &tmp);
	te->sync_threshold_start = (!rc ? tmp : 4);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-sync-threshold-continue", &tmp);
	te->sync_threshold_continue = (!rc ? tmp : 4);
	rc = of_property_read_u32(np, "qcom,mdss-tear-check-frame-rate", &tmp);
	te->refx100 = (!rc ? tmp : 6000);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-start-pos", &tmp);
	te->start_pos = (!rc ? tmp : timing->yres);
	rc = of_property_read_u32
		(np, "qcom,mdss-tear-check-rd-ptr-trigger-intr", &tmp);
	te->rd_ptr_irq = (!rc ? tmp : timing->yres + 1);
	te->wr_ptr_irq = 0;
}


static int mdss_dsi_parse_reset_seq(struct device_node *np,
		u32 rst_seq[MDSS_DSI_RST_SEQ_LEN], u32 *rst_len,
		const char *name)
{
	int num = 0, i;
	int rc;
	struct property *data;
	u32 tmp[MDSS_DSI_RST_SEQ_LEN];
	*rst_len = 0;
	data = of_find_property(np, name, &num);
	num /= sizeof(u32);
	if (!data || !num || num > MDSS_DSI_RST_SEQ_LEN || num % 2) {
		pr_debug("%s:%d, error reading %s, length found = %d\n",
			__func__, __LINE__, name, num);
	} else {
		rc = of_property_read_u32_array(np, name, tmp, num);
		if (rc)
			pr_debug("%s:%d, error reading %s, rc = %d\n",
				__func__, __LINE__, name, rc);
		else {
			for (i = 0; i < num; ++i)
				rst_seq[i] = tmp[i];
			*rst_len = num;
		}
	}
	return 0;
}

static bool mdss_dsi_cmp_panel_reg_v2(struct mdss_dsi_ctrl_pdata *ctrl)
{
	int i, j = 0;
	int len = 0, *lenp;
	int group = 0;

	lenp = ctrl->status_valid_params ?: ctrl->status_cmds_rlen;

	for (i = 0; i < ctrl->status_cmds.cmd_cnt; i++)
		len += lenp[i];

	for (j = 0; j < ctrl->groups; ++j) {
		for (i = 0; i < len; ++i) {
#ifndef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/03/08,
//modify for esd return value check log
			pr_debug("[%i] return:0x%x status:0x%x\n",
				i, ctrl->return_buf[i],
				(unsigned int)ctrl->status_value[group + i]);
			MDSS_XLOG(ctrl->ndx, ctrl->return_buf[i],
					ctrl->status_value[group + i]);
			if (ctrl->return_buf[i] !=
				ctrl->status_value[group + i])
				break;
#else /*VENDOR_EDIT*/
			if (ctrl->return_buf[i] != ctrl->status_value[group + i]){
				pr_err("%s: Esd return Value is [0x%x] is not equal to status Value 0x%x.\n",
						__func__, ctrl->return_buf[i], ctrl->status_value[group + i]);
			break;
			}
#endif /*VEDNOR_EDIT*/
		}
		if (i == len)
			return true;
		group += len;
	}

	return false;
}

static int mdss_dsi_gen_read_status(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (!mdss_dsi_cmp_panel_reg_v2(ctrl_pdata)) {
		pr_err("%s: Read back value from panel is incorrect\n",
							__func__);
		return -EINVAL;
	} else {
		return 1;
	}
}

static int mdss_dsi_nt35596_read_status(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (!mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
		ctrl_pdata->status_value, 0)) {
		ctrl_pdata->status_error_count = 0;
		pr_err("%s: Read back value from panel is incorrect\n",
							__func__);
		return -EINVAL;
	} else {
		if (!mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
			ctrl_pdata->status_value, 3)) {
			ctrl_pdata->status_error_count = 0;
		} else {
			if (mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
				ctrl_pdata->status_value, 4) ||
				mdss_dsi_cmp_panel_reg(ctrl_pdata->status_buf,
				ctrl_pdata->status_value, 5))
				ctrl_pdata->status_error_count = 0;
			else
				ctrl_pdata->status_error_count++;
			if (ctrl_pdata->status_error_count >=
					ctrl_pdata->max_status_error_count) {
				ctrl_pdata->status_error_count = 0;
				pr_err("%s: Read value bad. Error_cnt = %i\n",
					 __func__,
					ctrl_pdata->status_error_count);
				return -EINVAL;
			}
		}
		return 1;
	}
}

static void mdss_dsi_parse_roi_alignment(struct device_node *np,
		struct dsi_panel_timing *pt)
{
	int len = 0;
	u32 value[6];
	struct property *data;
	struct mdss_panel_timing *timing = &pt->timing;

	data = of_find_property(np, "qcom,panel-roi-alignment", &len);
	len /= sizeof(u32);
	if (!data || (len != 6)) {
		pr_debug("%s: Panel roi alignment not found", __func__);
	} else {
		int rc = of_property_read_u32_array(np,
				"qcom,panel-roi-alignment", value, len);
		if (rc)
			pr_debug("%s: Error reading panel roi alignment values",
					__func__);
		else {
			timing->roi_alignment.xstart_pix_align = value[0];
			timing->roi_alignment.ystart_pix_align = value[1];
			timing->roi_alignment.width_pix_align = value[2];
			timing->roi_alignment.height_pix_align = value[3];
			timing->roi_alignment.min_width = value[4];
			timing->roi_alignment.min_height = value[5];
		}

		pr_debug("%s: ROI alignment: [%d, %d, %d, %d, %d, %d]",
			__func__, timing->roi_alignment.xstart_pix_align,
			timing->roi_alignment.width_pix_align,
			timing->roi_alignment.ystart_pix_align,
			timing->roi_alignment.height_pix_align,
			timing->roi_alignment.min_width,
			timing->roi_alignment.min_height);
	}
}

static void mdss_dsi_parse_dms_config(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_panel_info *pinfo = &ctrl->panel_data.panel_info;
	const char *data;
	bool dms_enabled;

	dms_enabled = of_property_read_bool(np,
		"qcom,dynamic-mode-switch-enabled");

	if (!dms_enabled) {
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_DISABLED;
		goto exit;
	}

	/* default mode is suspend_resume */
	pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_SUSPEND_RESUME;
	data = of_get_property(np, "qcom,dynamic-mode-switch-type", NULL);
	if (data && !strcmp(data, "dynamic-resolution-switch-immediate")) {
		if (!list_empty(&ctrl->panel_data.timings_list))
			pinfo->mipi.dms_mode =
				DYNAMIC_MODE_RESOLUTION_SWITCH_IMMEDIATE;
		else
			pinfo->mipi.dms_mode =
				DYNAMIC_MODE_SWITCH_DISABLED;
		goto exit;
	}

	if (data && !strcmp(data, "dynamic-switch-immediate"))
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_IMMEDIATE;
	else
		pr_debug("%s: default dms suspend/resume\n", __func__);

	mdss_dsi_parse_dcs_cmds(np, &ctrl->video2cmd,
		"qcom,video-to-cmd-mode-switch-commands",
		"qcom,mode-switch-commands-state");

	mdss_dsi_parse_dcs_cmds(np, &ctrl->cmd2video,
		"qcom,cmd-to-video-mode-switch-commands",
		"qcom,mode-switch-commands-state");

	mdss_dsi_parse_dcs_cmds(np, &ctrl->post_dms_on_cmds,
		"qcom,mdss-dsi-post-mode-switch-on-command",
		"qcom,mdss-dsi-post-mode-switch-on-command-state");

	if (pinfo->mipi.dms_mode == DYNAMIC_MODE_SWITCH_IMMEDIATE &&
		!ctrl->post_dms_on_cmds.cmd_cnt) {
		pr_warn("%s: No post dms on cmd specified\n", __func__);
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_DISABLED;
	}

	if (!ctrl->video2cmd.cmd_cnt || !ctrl->cmd2video.cmd_cnt) {
		pr_warn("%s: No commands specified for dynamic switch\n",
			__func__);
		pinfo->mipi.dms_mode = DYNAMIC_MODE_SWITCH_DISABLED;
	}
exit:
	pr_info("%s: dynamic switch feature enabled: %d\n", __func__,
		pinfo->mipi.dms_mode);
	return;
}

/* the length of all the valid values to be checked should not be great
 * than the length of returned data from read command.
 */
static bool
mdss_dsi_parse_esd_check_valid_params(struct mdss_dsi_ctrl_pdata *ctrl)
{
	int i;

	for (i = 0; i < ctrl->status_cmds.cmd_cnt; ++i) {
		if (ctrl->status_valid_params[i] > ctrl->status_cmds_rlen[i]) {
			pr_debug("%s: ignore valid params!\n", __func__);
			return false;
		}
	}

	return true;
}

static bool mdss_dsi_parse_esd_status_len(struct device_node *np,
	char *prop_key, u32 **target, u32 cmd_cnt)
{
	int tmp;

	if (!of_find_property(np, prop_key, &tmp))
		return false;

	tmp /= sizeof(u32);
	if (tmp != cmd_cnt) {
		pr_err("%s: request property number(%d) not match command count(%d)\n",
			__func__, tmp, cmd_cnt);
		return false;
	}

	*target = kcalloc(tmp, sizeof(u32), GFP_KERNEL);
	if (IS_ERR_OR_NULL(*target)) {
		pr_err("%s: Error allocating memory for property\n",
			__func__);
		return false;
	}

	if (of_property_read_u32_array(np, prop_key, *target, tmp)) {
		pr_err("%s: cannot get values from dts\n", __func__);
		kfree(*target);
		*target = NULL;
		return false;
	}

	return true;
}

static void mdss_dsi_parse_esd_params(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	u32 tmp;
	u32 i, status_len, *lenp;
	int rc;
	struct property *data;
	const char *string;
	struct mdss_panel_info *pinfo = &ctrl->panel_data.panel_info;

	pinfo->esd_check_enabled = of_property_read_bool(np,
		"qcom,esd-check-enabled");

	if (!pinfo->esd_check_enabled)
		return;

	ctrl->status_mode = ESD_MAX;
	rc = of_property_read_string(np,
			"qcom,mdss-dsi-panel-status-check-mode", &string);
	if (!rc) {
		if (!strcmp(string, "bta_check")) {
			ctrl->status_mode = ESD_BTA;
		} else if (!strcmp(string, "reg_read")) {
			ctrl->status_mode = ESD_REG;
			ctrl->check_read_status =
				mdss_dsi_gen_read_status;
		} else if (!strcmp(string, "reg_read_nt35596")) {
			ctrl->status_mode = ESD_REG_NT35596;
			ctrl->status_error_count = 0;
			ctrl->check_read_status =
				mdss_dsi_nt35596_read_status;
		} else if (!strcmp(string, "te_signal_check")) {
			if (pinfo->mipi.mode == DSI_CMD_MODE) {
				ctrl->status_mode = ESD_TE;
			} else {
				pr_err("TE-ESD not valid for video mode\n");
				goto error;
			}
		} else {
			pr_err("No valid panel-status-check-mode string\n");
			goto error;
		}
	}

	if ((ctrl->status_mode == ESD_BTA) || (ctrl->status_mode == ESD_TE) ||
			(ctrl->status_mode == ESD_MAX))
		return;

	mdss_dsi_parse_dcs_cmds(np, &ctrl->status_cmds,
			"qcom,mdss-dsi-panel-status-command",
				"qcom,mdss-dsi-panel-status-command-state");

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-max-error-count",
		&tmp);
	ctrl->max_status_error_count = (!rc ? tmp : 0);

	if (!mdss_dsi_parse_esd_status_len(np,
		"qcom,mdss-dsi-panel-status-read-length",
		&ctrl->status_cmds_rlen, ctrl->status_cmds.cmd_cnt)) {
		pinfo->esd_check_enabled = false;
		return;
	}

	if (mdss_dsi_parse_esd_status_len(np,
		"qcom,mdss-dsi-panel-status-valid-params",
		&ctrl->status_valid_params, ctrl->status_cmds.cmd_cnt)) {
		if (!mdss_dsi_parse_esd_check_valid_params(ctrl))
			goto error1;
	}

	status_len = 0;
	lenp = ctrl->status_valid_params ?: ctrl->status_cmds_rlen;
	for (i = 0; i < ctrl->status_cmds.cmd_cnt; ++i)
		status_len += lenp[i];

	data = of_find_property(np, "qcom,mdss-dsi-panel-status-value", &tmp);
	tmp /= sizeof(u32);
	if (!IS_ERR_OR_NULL(data) && tmp != 0 && (tmp % status_len) == 0) {
		ctrl->groups = tmp / status_len;
	} else {
		pr_err("%s: Error parse panel-status-value\n", __func__);
		goto error1;
	}

	ctrl->status_value = kzalloc(sizeof(u32) * status_len * ctrl->groups,
				GFP_KERNEL);
	if (!ctrl->status_value)
		goto error1;

	ctrl->return_buf = kcalloc(status_len * ctrl->groups,
			sizeof(unsigned char), GFP_KERNEL);
	if (!ctrl->return_buf)
		goto error2;

	rc = of_property_read_u32_array(np,
		"qcom,mdss-dsi-panel-status-value",
		ctrl->status_value, ctrl->groups * status_len);
	if (rc) {
		pr_debug("%s: Error reading panel status values\n",
				__func__);
		memset(ctrl->status_value, 0, ctrl->groups * status_len);
	}

	return;

error2:
	kfree(ctrl->status_value);
error1:
	kfree(ctrl->status_valid_params);
	kfree(ctrl->status_cmds_rlen);
error:
	pinfo->esd_check_enabled = false;
}

static void mdss_dsi_parse_partial_update_caps(struct device_node *np,
		struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_panel_info *pinfo;
	const char *data;

	pinfo = &ctrl->panel_data.panel_info;

	data = of_get_property(np, "qcom,partial-update-enabled", NULL);
	if (data && !strcmp(data, "single_roi"))
		pinfo->partial_update_supported =
			PU_SINGLE_ROI;
	else if (data && !strcmp(data, "dual_roi"))
		pinfo->partial_update_supported =
			PU_DUAL_ROI;
	else if (data && !strcmp(data, "none"))
		pinfo->partial_update_supported =
			PU_NOT_SUPPORTED;
	else
		pinfo->partial_update_supported =
			PU_NOT_SUPPORTED;

	if (pinfo->mipi.mode == DSI_CMD_MODE) {
		pinfo->partial_update_enabled = pinfo->partial_update_supported;
		pr_info("%s: partial_update_enabled=%d\n", __func__,
					pinfo->partial_update_enabled);
		ctrl->set_col_page_addr = mdss_dsi_set_col_page_addr;
		if (pinfo->partial_update_enabled) {
			pinfo->partial_update_roi_merge =
					of_property_read_bool(np,
					"qcom,partial-update-roi-merge");
		}
	}
}

static int mdss_dsi_parse_panel_features(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_panel_info *pinfo;

	if (!np || !ctrl) {
		pr_err("%s: Invalid arguments\n", __func__);
		return -ENODEV;
	}

	pinfo = &ctrl->panel_data.panel_info;

	mdss_dsi_parse_partial_update_caps(np, ctrl);

	pinfo->dcs_cmd_by_left = of_property_read_bool(np,
		"qcom,dcs-cmd-by-left");

	pinfo->ulps_feature_enabled = of_property_read_bool(np,
		"qcom,ulps-enabled");
	pr_info("%s: ulps feature %s\n", __func__,
		(pinfo->ulps_feature_enabled ? "enabled" : "disabled"));

	pinfo->ulps_suspend_enabled = of_property_read_bool(np,
		"qcom,suspend-ulps-enabled");
	pr_info("%s: ulps during suspend feature %s", __func__,
		(pinfo->ulps_suspend_enabled ? "enabled" : "disabled"));

	mdss_dsi_parse_dms_config(np, ctrl);

	pinfo->panel_ack_disabled = pinfo->sim_panel_mode ?
		1 : of_property_read_bool(np, "qcom,panel-ack-disabled");

	pinfo->allow_phy_power_off = of_property_read_bool(np,
		"qcom,panel-allow-phy-poweroff");

	mdss_dsi_parse_esd_params(np, ctrl);

	if (pinfo->panel_ack_disabled && pinfo->esd_check_enabled) {
		pr_warn("ESD should not be enabled if panel ACK is disabled\n");
		pinfo->esd_check_enabled = false;
	}

	if (ctrl->disp_en_gpio <= 0) {
		ctrl->disp_en_gpio = of_get_named_gpio(
			np,
			"qcom,5v-boost-gpio", 0);

		if (!gpio_is_valid(ctrl->disp_en_gpio))
			pr_debug("%s:%d, Disp_en gpio not specified\n",
					__func__, __LINE__);
	}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/02/21,
//add for ftm mode, disable esd check and ulps
	if(MSM_BOOT_MODE__FACTORY == get_boot_mode()){
		pinfo->esd_check_enabled = false;
		pinfo->ulps_feature_enabled = false;
	}
#endif /*VENDOR_EDIT*/

	mdss_dsi_parse_dcs_cmds(np, &ctrl->lp_on_cmds,
			"qcom,mdss-dsi-lp-mode-on", NULL);

	mdss_dsi_parse_dcs_cmds(np, &ctrl->lp_off_cmds,
			"qcom,mdss-dsi-lp-mode-off", NULL);

	return 0;
}

static void mdss_dsi_parse_panel_horizintal_line_idle(struct device_node *np,
	struct mdss_dsi_ctrl_pdata *ctrl)
{
	const u32 *src;
	int i, len, cnt;
	struct panel_horizontal_idle *kp;

	if (!np || !ctrl) {
		pr_err("%s: Invalid arguments\n", __func__);
		return;
	}

	src = of_get_property(np, "qcom,mdss-dsi-hor-line-idle", &len);
	if (!src || len == 0)
		return;

	cnt = len % 3; /* 3 fields per entry */
	if (cnt) {
		pr_err("%s: invalid horizontal idle len=%d\n", __func__, len);
		return;
	}

	cnt = len / sizeof(u32);

	kp = kzalloc(sizeof(*kp) * (cnt / 3), GFP_KERNEL);
	if (kp == NULL) {
		pr_err("%s: No memory\n", __func__);
		return;
	}

	ctrl->line_idle = kp;
	for (i = 0; i < cnt; i += 3) {
		kp->min = be32_to_cpu(src[i]);
		kp->max = be32_to_cpu(src[i+1]);
		kp->idle = be32_to_cpu(src[i+2]);
		kp++;
		ctrl->horizontal_idle_cnt++;
	}

	/*
	 * idle is enabled for this controller, this will be used to
	 * enable/disable burst mode since both features are mutually
	 * exclusive.
	 */
	ctrl->idle_enabled = true;

	pr_debug("%s: horizontal_idle_cnt=%d\n", __func__,
				ctrl->horizontal_idle_cnt);
}

static int mdss_dsi_set_refresh_rate_range(struct device_node *pan_node,
		struct mdss_panel_info *pinfo)
{
	int rc = 0;
	rc = of_property_read_u32(pan_node,
			"qcom,mdss-dsi-min-refresh-rate",
			&pinfo->min_fps);
	if (rc) {
		pr_warn("%s:%d, Unable to read min refresh rate\n",
				__func__, __LINE__);

		/*
		 * If min refresh rate is not specified, set it to the
		 * default panel refresh rate.
		 */
		pinfo->min_fps = pinfo->mipi.frame_rate;
		rc = 0;
	}

	rc = of_property_read_u32(pan_node,
			"qcom,mdss-dsi-max-refresh-rate",
			&pinfo->max_fps);
	if (rc) {
		pr_warn("%s:%d, Unable to read max refresh rate\n",
				__func__, __LINE__);

		/*
		 * Since max refresh rate was not specified when dynamic
		 * fps is enabled, using the default panel refresh rate
		 * as max refresh rate supported.
		 */
		pinfo->max_fps = pinfo->mipi.frame_rate;
		rc = 0;
	}

	pr_info("dyn_fps: min = %d, max = %d\n",
			pinfo->min_fps, pinfo->max_fps);
	return rc;
}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Feature, 2018/01/03,
//add for dynamic mipi dsi clk
static void mdss_dsi_parse_dynamic_dsitiming_config
		(struct device_node *pan_node,
		struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int dynamic_dsitiming = 0;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	dynamic_dsitiming = of_property_read_bool(pan_node,
				"qcom,dynamic-dsi-timing-enable");

	if (dynamic_dsitiming)
		pinfo->dynamic_dsitiming = true;
	else
		pinfo->dynamic_dsitiming = false;

	pr_debug("%s:dynamic_dsitiming=%d\n", __func__,
			pinfo->dynamic_dsitiming);
}
#endif /*VENDOR_EDIT*/

static void mdss_dsi_parse_dfps_config(struct device_node *pan_node,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	const char *data;
	bool dynamic_fps, dynamic_bitclk;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);
	int rc = 0;

	dynamic_fps = of_property_read_bool(pan_node,
			"qcom,mdss-dsi-pan-enable-dynamic-fps");

	if (!dynamic_fps)
		goto dynamic_bitclk;

	pinfo->dynamic_fps = true;
	data = of_get_property(pan_node, "qcom,mdss-dsi-pan-fps-update", NULL);
	if (data) {
		if (!strcmp(data, "dfps_suspend_resume_mode")) {
			pinfo->dfps_update = DFPS_SUSPEND_RESUME_MODE;
			pr_debug("dfps mode: suspend/resume\n");
		} else if (!strcmp(data, "dfps_immediate_clk_mode")) {
			pinfo->dfps_update = DFPS_IMMEDIATE_CLK_UPDATE_MODE;
			pr_debug("dfps mode: Immediate clk\n");
		} else if (!strcmp(data, "dfps_immediate_porch_mode_hfp")) {
			pinfo->dfps_update =
				DFPS_IMMEDIATE_PORCH_UPDATE_MODE_HFP;
			pr_debug("dfps mode: Immediate porch HFP\n");
		} else if (!strcmp(data, "dfps_immediate_porch_mode_vfp")) {
			pinfo->dfps_update =
				DFPS_IMMEDIATE_PORCH_UPDATE_MODE_VFP;
			pr_debug("dfps mode: Immediate porch VFP\n");
		} else {
			pinfo->dfps_update = DFPS_SUSPEND_RESUME_MODE;
			pr_debug("default dfps mode: suspend/resume\n");
		}
	} else {
		pinfo->dynamic_fps = false;
		pr_debug("dfps update mode not configured: disable\n");
	}
	pinfo->new_fps = pinfo->mipi.frame_rate;
	pinfo->current_fps = pinfo->mipi.frame_rate;

dynamic_bitclk:
	dynamic_bitclk = of_property_read_bool(pan_node,
			"qcom,mdss-dsi-pan-enable-dynamic-bitclk");
	if (!dynamic_bitclk)
		return;

	of_find_property(pan_node, "qcom,mdss-dsi-dynamic-bitclk_freq",
		&pinfo->supp_bitclk_len);
	pinfo->supp_bitclk_len = pinfo->supp_bitclk_len/sizeof(u32);
	if (pinfo->supp_bitclk_len < 1)
		return;

	pinfo->supp_bitclks = kzalloc((sizeof(u32) * pinfo->supp_bitclk_len),
		GFP_KERNEL);
	if (!pinfo->supp_bitclks)
		return;

	rc = of_property_read_u32_array(pan_node,
		"qcom,mdss-dsi-dynamic-bitclk_freq", pinfo->supp_bitclks,
		pinfo->supp_bitclk_len);
	if (rc) {
		pr_err("Error from dynamic bitclk freq u64 array read\n");
		return;
	}
	pinfo->dynamic_bitclk = true;
	return;
}

int mdss_panel_parse_bl_settings(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	const char *data;
	int rc = 0;
	u32 tmp;

	ctrl_pdata->bklt_ctrl = UNKNOWN_CTRL;
	data = of_get_property(np, "qcom,mdss-dsi-bl-pmic-control-type", NULL);
	if (data) {
		if (!strcmp(data, "bl_ctrl_wled")) {
			led_trigger_register_simple("bkl-trigger",
				&bl_led_trigger);
			pr_debug("%s: SUCCESS-> WLED TRIGGER register\n",
				__func__);
			ctrl_pdata->bklt_ctrl = BL_WLED;
		} else if (!strcmp(data, "bl_ctrl_pwm")) {
			ctrl_pdata->bklt_ctrl = BL_PWM;
			ctrl_pdata->pwm_pmi = of_property_read_bool(np,
					"qcom,mdss-dsi-bl-pwm-pmi");
			rc = of_property_read_u32(np,
				"qcom,mdss-dsi-bl-pmic-pwm-frequency", &tmp);
			if (rc) {
				pr_err("%s:%d, Error, panel pwm_period\n",
						__func__, __LINE__);
				return -EINVAL;
			}
			ctrl_pdata->pwm_period = tmp;
			if (ctrl_pdata->pwm_pmi) {
				ctrl_pdata->pwm_bl = of_pwm_get(np, NULL);
				if (IS_ERR(ctrl_pdata->pwm_bl)) {
					pr_err("%s: Error, pwm device\n",
								__func__);
					ctrl_pdata->pwm_bl = NULL;
					return -EINVAL;
				}
			} else {
				rc = of_property_read_u32(np,
					"qcom,mdss-dsi-bl-pmic-bank-select",
								 &tmp);
				if (rc) {
					pr_err("%s:%d, Error, lpg channel\n",
							__func__, __LINE__);
					return -EINVAL;
				}
				ctrl_pdata->pwm_lpg_chan = tmp;
				tmp = of_get_named_gpio(np,
					"qcom,mdss-dsi-pwm-gpio", 0);
				ctrl_pdata->pwm_pmic_gpio = tmp;
				pr_debug("%s: Configured PWM bklt ctrl\n",
								 __func__);
			}
		} else if (!strcmp(data, "bl_ctrl_dcs")) {
			ctrl_pdata->bklt_ctrl = BL_DCS_CMD;
			data = of_get_property(np,
				"qcom,mdss-dsi-bl-dcs-command-state", NULL);
			if (data && !strcmp(data, "dsi_hs_mode"))
				ctrl_pdata->bklt_dcs_op_mode = DSI_HS_MODE;
			else
				ctrl_pdata->bklt_dcs_op_mode = DSI_LP_MODE;

			pr_debug("%s: Configured DCS_CMD bklt ctrl\n",
								__func__);
		}
	}
	return 0;
}

int mdss_dsi_panel_timing_switch(struct mdss_dsi_ctrl_pdata *ctrl,
			struct mdss_panel_timing *timing)
{
	struct dsi_panel_timing *pt;
	struct mdss_panel_info *pinfo = &ctrl->panel_data.panel_info;
	int i;

	if (!timing)
		return -EINVAL;

	if (timing == ctrl->panel_data.current_timing) {
		pr_warn("%s: panel timing \"%s\" already set\n", __func__,
				timing->name);
		return 0; /* nothing to do */
	}

	pr_debug("%s: ndx=%d switching to panel timing \"%s\"\n", __func__,
			ctrl->ndx, timing->name);

	mdss_panel_info_from_timing(timing, pinfo);

	pt = container_of(timing, struct dsi_panel_timing, timing);
	pinfo->mipi.t_clk_pre = pt->t_clk_pre;
	pinfo->mipi.t_clk_post = pt->t_clk_post;

	for (i = 0; i < ARRAY_SIZE(pt->phy_timing); i++)
		pinfo->mipi.dsi_phy_db.timing[i] = pt->phy_timing[i];

	for (i = 0; i < ARRAY_SIZE(pt->phy_timing_8996); i++)
		pinfo->mipi.dsi_phy_db.timing_8996[i] = pt->phy_timing_8996[i];

	ctrl->on_cmds = pt->on_cmds;
	ctrl->post_panel_on_cmds = pt->post_panel_on_cmds;

	ctrl->panel_data.current_timing = timing;
	if (!timing->clk_rate)
		ctrl->refresh_clk_rate = true;
	mdss_dsi_clk_refresh(&ctrl->panel_data, ctrl->update_phy_timing);

	return 0;
}

void mdss_dsi_unregister_bl_settings(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (ctrl_pdata->bklt_ctrl == BL_WLED)
		led_trigger_unregister_simple(bl_led_trigger);
}

static int mdss_dsi_panel_timing_from_dt(struct device_node *np,
		struct dsi_panel_timing *pt,
		struct mdss_panel_data *panel_data)
{
	u32 tmp;
	u64 tmp64;
	int rc, i, len;
	const char *data;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata;
	struct mdss_panel_info *pinfo;
	bool phy_timings_present = false;

	pinfo = &panel_data->panel_info;

	ctrl_pdata = container_of(panel_data, struct mdss_dsi_ctrl_pdata,
				panel_data);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-width", &tmp);
	if (rc) {
		pr_err("%s:%d, panel width not specified\n",
						__func__, __LINE__);
		return -EINVAL;
	}
	pt->timing.xres = tmp;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-height", &tmp);
	if (rc) {
		pr_err("%s:%d, panel height not specified\n",
						__func__, __LINE__);
		return -EINVAL;
	}
	pt->timing.yres = tmp;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-front-porch", &tmp);
	pt->timing.h_front_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-back-porch", &tmp);
	pt->timing.h_back_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-pulse-width", &tmp);
	pt->timing.h_pulse_width = (!rc ? tmp : 2);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-sync-skew", &tmp);
	pt->timing.hsync_skew = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-back-porch", &tmp);
	pt->timing.v_back_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-front-porch", &tmp);
	pt->timing.v_front_porch = (!rc ? tmp : 6);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-pulse-width", &tmp);
	pt->timing.v_pulse_width = (!rc ? tmp : 2);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-left-border", &tmp);
	pt->timing.border_left = !rc ? tmp : 0;
	rc = of_property_read_u32(np, "qcom,mdss-dsi-h-right-border", &tmp);
	pt->timing.border_right = !rc ? tmp : 0;

	/* overriding left/right borders for split display cases */
	if (mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data)) {
		if (panel_data->next)
			pt->timing.border_right = 0;
		else
			pt->timing.border_left = 0;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-top-border", &tmp);
	pt->timing.border_top = !rc ? tmp : 0;
	rc = of_property_read_u32(np, "qcom,mdss-dsi-v-bottom-border", &tmp);
	pt->timing.border_bottom = !rc ? tmp : 0;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-panel-framerate", &tmp);
	pt->timing.frame_rate = !rc ? tmp : DEFAULT_FRAME_RATE;
	rc = of_property_read_u64(np, "qcom,mdss-dsi-panel-clockrate", &tmp64);
	if (rc == -EOVERFLOW) {
		tmp64 = 0;
		rc = of_property_read_u32(np,
			"qcom,mdss-dsi-panel-clockrate", (u32 *)&tmp64);
	}
	pt->timing.clk_rate = !rc ? tmp64 : 0;

	data = of_get_property(np, "qcom,mdss-dsi-panel-timings", &len);
	if ((!data) || (len != 12)) {
		pr_debug("%s:%d, Unable to read Phy timing settings",
		       __func__, __LINE__);
	} else {
		for (i = 0; i < len; i++)
			pt->phy_timing[i] = data[i];
		phy_timings_present = true;
	}

	data = of_get_property(np, "qcom,mdss-dsi-panel-timings-phy-v2", &len);
	if ((!data) || (len != 40)) {
		pr_debug("%s:%d, Unable to read phy-v2 lane timing settings",
		       __func__, __LINE__);
	} else {
		for (i = 0; i < len; i++)
			pt->phy_timing_8996[i] = data[i];
		phy_timings_present = true;
	}
	if (!phy_timings_present) {
		pr_err("%s: phy timing settings not present\n", __func__);
		return -EINVAL;
	}

	rc = of_property_read_u32(np, "qcom,mdss-dsi-t-clk-pre", &tmp);
	pt->t_clk_pre = (!rc ? tmp : 0x24);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-t-clk-post", &tmp);
	pt->t_clk_post = (!rc ? tmp : 0x03);

	if (np->name) {
		pt->timing.name = kstrdup(np->name, GFP_KERNEL);
		pr_info("%s: found new timing \"%s\" (%pK)\n", __func__,
				np->name, &pt->timing);
	}

	return 0;
}

static int  mdss_dsi_panel_config_res_properties(struct device_node *np,
		struct dsi_panel_timing *pt,
		struct mdss_panel_data *panel_data,
		bool default_timing)
{
	int rc = 0;

	mdss_dsi_parse_roi_alignment(np, pt);

	mdss_dsi_parse_dcs_cmds(np, &pt->on_cmds,
		"qcom,mdss-dsi-on-command",
		"qcom,mdss-dsi-on-command-state");

	mdss_dsi_parse_dcs_cmds(np, &pt->post_panel_on_cmds,
		"qcom,mdss-dsi-post-panel-on-command", NULL);

	mdss_dsi_parse_dcs_cmds(np, &pt->switch_cmds,
		"qcom,mdss-dsi-timing-switch-command",
		"qcom,mdss-dsi-timing-switch-command-state");

	rc = mdss_dsi_parse_topology_config(np, pt, panel_data, default_timing);
	if (rc) {
		pr_err("%s: parsing compression params failed. rc:%d\n",
			__func__, rc);
		return rc;
	}

	mdss_panel_parse_te_params(np, &pt->timing);
	return rc;
}

static int mdss_panel_parse_display_timings(struct device_node *np,
		struct mdss_panel_data *panel_data)
{
	struct mdss_dsi_ctrl_pdata *ctrl;
	struct dsi_panel_timing *modedb;
	struct device_node *timings_np;
	struct device_node *entry;
	int num_timings, rc;
	int i = 0, active_ndx = 0;
	bool default_timing = false;

	ctrl = container_of(panel_data, struct mdss_dsi_ctrl_pdata, panel_data);

	INIT_LIST_HEAD(&panel_data->timings_list);

	timings_np = of_get_child_by_name(np, "qcom,mdss-dsi-display-timings");
	if (!timings_np) {
		struct dsi_panel_timing pt;
		memset(&pt, 0, sizeof(struct dsi_panel_timing));

		/*
		 * display timings node is not available, fallback to reading
		 * timings directly from root node instead
		 */
		pr_debug("reading display-timings from panel node\n");
		rc = mdss_dsi_panel_timing_from_dt(np, &pt, panel_data);
		if (!rc) {
			mdss_dsi_panel_config_res_properties(np, &pt,
					panel_data, true);
			rc = mdss_dsi_panel_timing_switch(ctrl, &pt.timing);
		}
		return rc;
	}

	num_timings = of_get_child_count(timings_np);
	if (num_timings == 0) {
		pr_err("no timings found within display-timings\n");
		rc = -EINVAL;
		goto exit;
	}

	modedb = kcalloc(num_timings, sizeof(*modedb), GFP_KERNEL);
	if (!modedb) {
		rc = -ENOMEM;
		goto exit;
	}

	for_each_child_of_node(timings_np, entry) {
		rc = mdss_dsi_panel_timing_from_dt(entry, (modedb + i),
				panel_data);
		if (rc) {
			kfree(modedb);
			goto exit;
		}

		default_timing = of_property_read_bool(entry,
				"qcom,mdss-dsi-timing-default");
		if (default_timing)
			active_ndx = i;

		mdss_dsi_panel_config_res_properties(entry, (modedb + i),
				panel_data, default_timing);

		list_add(&modedb[i].timing.list,
				&panel_data->timings_list);
		i++;
	}

	/* Configure default timing settings */
	rc = mdss_dsi_panel_timing_switch(ctrl, &modedb[active_ndx].timing);
	if (rc)
		pr_err("unable to configure default timing settings\n");

exit:
	of_node_put(timings_np);

	return rc;
}

static int mdss_panel_parse_dt(struct device_node *np,
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	u32 tmp;
	int rc, len = 0;
	const char *data;
	static const char *pdest;
	const char *bridge_chip_name;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (mdss_dsi_is_hw_config_split(ctrl_pdata->shared_data))
		pinfo->is_split_display = true;

	rc = of_property_read_u32(np,
		"qcom,mdss-pan-physical-width-dimension", &tmp);
	pinfo->physical_width = (!rc ? tmp : 0);
	rc = of_property_read_u32(np,
		"qcom,mdss-pan-physical-height-dimension", &tmp);
	pinfo->physical_height = (!rc ? tmp : 0);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-bpp", &tmp);
	if (rc) {
		pr_err("%s:%d, bpp not specified\n", __func__, __LINE__);
		return -EINVAL;
	}
	pinfo->bpp = (!rc ? tmp : 24);
	pinfo->mipi.mode = DSI_VIDEO_MODE;
	data = of_get_property(np, "qcom,mdss-dsi-panel-type", NULL);
	if (data && !strncmp(data, "dsi_cmd_mode", 12))
		pinfo->mipi.mode = DSI_CMD_MODE;
	pinfo->mipi.boot_mode = pinfo->mipi.mode;
	tmp = 0;
	data = of_get_property(np, "qcom,mdss-dsi-pixel-packing", NULL);
	if (data && !strcmp(data, "loose"))
		pinfo->mipi.pixel_packing = 1;
	else
		pinfo->mipi.pixel_packing = 0;
	rc = mdss_panel_get_dst_fmt(pinfo->bpp,
		pinfo->mipi.mode, pinfo->mipi.pixel_packing,
		&(pinfo->mipi.dst_format));
	if (rc) {
		pr_debug("%s: problem determining dst format. Set Default\n",
			__func__);
		pinfo->mipi.dst_format =
			DSI_VIDEO_DST_FORMAT_RGB888;
	}
	pdest = of_get_property(np,
		"qcom,mdss-dsi-panel-destination", NULL);

	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-underflow-color", &tmp);
	pinfo->lcdc.underflow_clr = (!rc ? tmp : 0xff);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-border-color", &tmp);
	pinfo->lcdc.border_clr = (!rc ? tmp : 0);
	data = of_get_property(np, "qcom,mdss-dsi-panel-orientation", NULL);
	if (data) {
		pr_debug("panel orientation is %s\n", data);
		if (!strcmp(data, "180"))
			pinfo->panel_orientation = MDP_ROT_180;
		else if (!strcmp(data, "hflip"))
			pinfo->panel_orientation = MDP_FLIP_LR;
		else if (!strcmp(data, "vflip"))
			pinfo->panel_orientation = MDP_FLIP_UD;
	}

	rc = of_property_read_u32(np, "qcom,mdss-brightness-max-level", &tmp);
	pinfo->brightness_max = (!rc ? tmp : MDSS_MAX_BL_BRIGHTNESS);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-bl-min-level", &tmp);
	pinfo->bl_min = (!rc ? tmp : 0);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-bl-max-level", &tmp);
	pinfo->bl_max = (!rc ? tmp : 255);
	ctrl_pdata->bklt_max = pinfo->bl_max;

	rc = of_property_read_u32(np, "qcom,mdss-dsi-interleave-mode", &tmp);
	pinfo->mipi.interleave_mode = (!rc ? tmp : 0);

	pinfo->mipi.vsync_enable = of_property_read_bool(np,
		"qcom,mdss-dsi-te-check-enable");

	if (pinfo->sim_panel_mode == SIM_SW_TE_MODE)
		pinfo->mipi.hw_vsync_mode = false;
	else
		pinfo->mipi.hw_vsync_mode = of_property_read_bool(np,
			"qcom,mdss-dsi-te-using-te-pin");

	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-h-sync-pulse", &tmp);
	pinfo->mipi.pulse_mode_hsa_he = (!rc ? tmp : false);

	pinfo->mipi.hfp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hfp-power-mode");
	pinfo->mipi.hsa_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hsa-power-mode");
	pinfo->mipi.hbp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-hbp-power-mode");
	pinfo->mipi.last_line_interleave_en = of_property_read_bool(np,
		"qcom,mdss-dsi-last-line-interleave");
	pinfo->mipi.bllp_power_stop = of_property_read_bool(np,
		"qcom,mdss-dsi-bllp-power-mode");
	pinfo->mipi.eof_bllp_power_stop = of_property_read_bool(
		np, "qcom,mdss-dsi-bllp-eof-power-mode");
	pinfo->mipi.traffic_mode = DSI_NON_BURST_SYNCH_PULSE;
	data = of_get_property(np, "qcom,mdss-dsi-traffic-mode", NULL);
	if (data) {
		if (!strcmp(data, "non_burst_sync_event"))
			pinfo->mipi.traffic_mode = DSI_NON_BURST_SYNCH_EVENT;
		else if (!strcmp(data, "burst_mode"))
			pinfo->mipi.traffic_mode = DSI_BURST_MODE;
	}
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-dcs-command", &tmp);
	pinfo->mipi.insert_dcs_cmd =
			(!rc ? tmp : 1);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-wr-mem-continue", &tmp);
	pinfo->mipi.wr_mem_continue =
			(!rc ? tmp : 0x3c);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-wr-mem-start", &tmp);
	pinfo->mipi.wr_mem_start =
			(!rc ? tmp : 0x2c);
	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-te-pin-select", &tmp);
	pinfo->mipi.te_sel =
			(!rc ? tmp : 1);
	rc = of_property_read_u32(np, "qcom,mdss-dsi-virtual-channel-id", &tmp);
	pinfo->mipi.vc = (!rc ? tmp : 0);
	pinfo->mipi.rgb_swap = DSI_RGB_SWAP_RGB;
	data = of_get_property(np, "qcom,mdss-dsi-color-order", NULL);
	if (data) {
		if (!strcmp(data, "rgb_swap_rbg"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_RBG;
		else if (!strcmp(data, "rgb_swap_bgr"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_BGR;
		else if (!strcmp(data, "rgb_swap_brg"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_BRG;
		else if (!strcmp(data, "rgb_swap_grb"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_GRB;
		else if (!strcmp(data, "rgb_swap_gbr"))
			pinfo->mipi.rgb_swap = DSI_RGB_SWAP_GBR;
	}
	pinfo->mipi.data_lane0 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-0-state");
	pinfo->mipi.data_lane1 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-1-state");
	pinfo->mipi.data_lane2 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-2-state");
	pinfo->mipi.data_lane3 = of_property_read_bool(np,
		"qcom,mdss-dsi-lane-3-state");

	/* parse split link properties */
	rc = mdss_dsi_parse_split_link_settings(np, pinfo);
	if (rc)
		return rc;

	rc = mdss_panel_parse_display_timings(np, &ctrl_pdata->panel_data);
	if (rc)
		return rc;

	rc = mdss_dsi_parse_hdr_settings(np, pinfo);
	if (rc)
		return rc;

	pinfo->mipi.rx_eot_ignore = of_property_read_bool(np,
		"qcom,mdss-dsi-rx-eot-ignore");
	pinfo->mipi.tx_eot_append = of_property_read_bool(np,
		"qcom,mdss-dsi-tx-eot-append");

	rc = of_property_read_u32(np, "qcom,mdss-dsi-stream", &tmp);
	pinfo->mipi.stream = (!rc ? tmp : 0);

	data = of_get_property(np, "qcom,mdss-dsi-mode-sel-gpio-state", NULL);
	if (data) {
		if (!strcmp(data, "single_port"))
			pinfo->mode_sel_state = MODE_SEL_SINGLE_PORT;
		else if (!strcmp(data, "dual_port"))
			pinfo->mode_sel_state = MODE_SEL_DUAL_PORT;
		else if (!strcmp(data, "high"))
			pinfo->mode_sel_state = MODE_GPIO_HIGH;
		else if (!strcmp(data, "low"))
			pinfo->mode_sel_state = MODE_GPIO_LOW;
	} else {
		/* Set default mode as SPLIT mode */
		pinfo->mode_sel_state = MODE_SEL_DUAL_PORT;
	}

	rc = of_property_read_u32(np, "qcom,mdss-mdp-transfer-time-us", &tmp);
	pinfo->mdp_transfer_time_us = (!rc ? tmp : DEFAULT_MDP_TRANSFER_TIME);

	mdss_dsi_parse_mdp_kickoff_threshold(np, pinfo);

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/02/14,
//add for lcd cabc
	if(is_lcd(OPPO16103_JDI_R63452_1080P_CMD_PANEL)
		|| is_lcd(OPPO18136_HIMAX_HX83112A_1080_2340_VOD_PANEL)
		|| is_lcd(OPPO18321_DPT_NT36672A_1080_2340_VOD_PANEL))
	{
		mdss_dsi_parse_dcs_cmds(np, &cabc_off_sequence,
			"qcom,mdss-dsi-cabc-off-command", "qcom,mdss-dsi-panel-status-command-state");
		mdss_dsi_parse_dcs_cmds(np, &cabc_user_interface_image_sequence,
			"qcom,mdss-dsi-cabc-ui-command", "qcom,mdss-dsi-panel-status-command-state");
		mdss_dsi_parse_dcs_cmds(np, &cabc_still_image_sequence,
			"qcom,mdss-dsi-cabc-still-image-command", "qcom,mdss-dsi-panel-status-command-state");
		mdss_dsi_parse_dcs_cmds(np, &cabc_video_image_sequence,
			"qcom,mdss-dsi-cabc-video-command", "qcom,mdss-dsi-panel-status-command-state");
	}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Driver.feature, 2017/03/17,
//add for LBR
	if((lcd_vendor == OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO16051_SAMSUNG_S6E3FA5_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO16118_SAMSUNG_S6E3FA5_1080P_CMD_PANEL))
	{
		mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->lbr_cmds,
			"qcom,mdss-dsi-lbr-command", "qcom,mdss-dsi-off-command-state");
//Guoqiang.Jiang@Multimedia.Driver.feature, 2017/03/17,
//add for HBM
		mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->hbm_cmds,
			"qcom,mdss-dsi-hbm-command", "qcom,mdss-dsi-panel-status-command-state");
	}

	if(lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL)
	{
		mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->aod_backlight_cmds,
			 "qcom,mdss-dsi-aod-backlight-command", "qcom,mdss-dsi-panel-status-command-state");
	}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
/*Guoqiang.Jiang@PSW.MM.Display.LCD.Stability,2018/1/31,add for support aod feature, solve bug:1264744*/
	if((lcd_vendor == OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL)
		|| (lcd_vendor == OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))
	{
		mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->aod_on_cmds,
			 "qcom,mdss-dsi-aod-on-command", "qcom,mdss-dsi-off-command-state");

		mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->aod_off_cmds,
			 "qcom,mdss-dsi-aod-off-command", "qcom,mdss-dsi-off-command-state");
	}
#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2017/02/18,
//add for lcd seed mode

	if((is_project(OPPO_16051)&&is_lcd(OPPO16051_SAMSUNG_S6E3FA5_1080P_CMD_PANEL))
		||(is_lcd(OPPO16118_SAMSUNG_S6E3FA5_1080P_CMD_PANEL)&&is_project(OPPO_16118))
		|| is_lcd(OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| is_lcd(OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL)
		|| is_lcd(OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL)
		|| is_lcd(OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL))
	{
		pr_info("%s: seed get command start\n",__func__);
	mdss_dsi_parse_dcs_cmds(np, &seed_mode0_cmds,
		"qcom,mdss-dsi-seed-0-command", "qcom,mdss-dsi-off-command-state");
	mdss_dsi_parse_dcs_cmds(np, &seed_mode1_cmds,
		"qcom,mdss-dsi-seed-1-command", "qcom,mdss-dsi-off-command-state");
	mdss_dsi_parse_dcs_cmds(np, &seed_mode2_cmds,
		"qcom,mdss-dsi-seed-2-command", "qcom,mdss-dsi-off-command-state");
	 pr_info("%s: seed mode get command end\n",__func__);
	}
#endif /*VENDOR_EDIT*/

	pinfo->mipi.lp11_init = of_property_read_bool(np,
					"qcom,mdss-dsi-lp11-init");
	rc = of_property_read_u32(np, "qcom,mdss-dsi-init-delay-us", &tmp);
	pinfo->mipi.init_delay = (!rc ? tmp : 0);

	rc = of_property_read_u32(np, "qcom,mdss-dsi-post-init-delay", &tmp);
	pinfo->mipi.post_init_delay = (!rc ? tmp : 0);

	mdss_dsi_parse_trigger(np, &(pinfo->mipi.mdp_trigger),
		"qcom,mdss-dsi-mdp-trigger");

	mdss_dsi_parse_trigger(np, &(pinfo->mipi.dma_trigger),
		"qcom,mdss-dsi-dma-trigger");

	mdss_dsi_parse_reset_seq(np, pinfo->rst_seq, &(pinfo->rst_seq_len),
		"qcom,mdss-dsi-reset-sequence");

	mdss_dsi_parse_dcs_cmds(np, &ctrl_pdata->off_cmds,
		"qcom,mdss-dsi-off-command", "qcom,mdss-dsi-off-command-state");

	rc = of_property_read_u32(np, "qcom,adjust-timer-wakeup-ms", &tmp);
	pinfo->adjust_timer_delay_ms = (!rc ? tmp : 0);

	pinfo->mipi.force_clk_lane_hs = of_property_read_bool(np,
		"qcom,mdss-dsi-force-clock-lane-hs");

	rc = mdss_dsi_parse_panel_features(np, ctrl_pdata);
	if (rc) {
		pr_err("%s: failed to parse panel features\n", __func__);
		goto error;
	}

	mdss_dsi_parse_panel_horizintal_line_idle(np, ctrl_pdata);

	mdss_dsi_parse_dfps_config(np, ctrl_pdata);

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Feature, 2018/01/03,
//add for dynamic mipi dsi clk
	mdss_dsi_parse_dynamic_dsitiming_config(np, ctrl_pdata);
#endif /*VENDOR_EDIT*/

	mdss_dsi_set_refresh_rate_range(np, pinfo);

	pinfo->is_dba_panel = of_property_read_bool(np,
			"qcom,dba-panel");

	if (pinfo->is_dba_panel) {
		bridge_chip_name = of_get_property(np,
			"qcom,bridge-name", &len);
		if (!bridge_chip_name || len <= 0) {
			pr_err("%s:%d Unable to read qcom,bridge_name, data=%pK,len=%d\n",
				__func__, __LINE__, bridge_chip_name, len);
			rc = -EINVAL;
			goto error;
		}
		strlcpy(ctrl_pdata->bridge_name, bridge_chip_name,
			MSM_DBA_CHIP_NAME_MAX_LEN);
	}

	rc = of_property_read_u32(np,
		"qcom,mdss-dsi-host-esc-clk-freq-hz",
		&pinfo->esc_clk_rate_hz);
	if (rc)
		pinfo->esc_clk_rate_hz = MDSS_DSI_MAX_ESC_CLK_RATE_HZ;
	pr_debug("%s: esc clk %d\n", __func__, pinfo->esc_clk_rate_hz);

	return 0;

error:
	return -EINVAL;
}

int mdss_dsi_panel_init(struct device_node *node,
	struct mdss_dsi_ctrl_pdata *ctrl_pdata,
	int ndx)
{
	int rc = 0;
	static const char *panel_name;
	struct mdss_panel_info *pinfo;
#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/12
//Add for registe panel info
	static const char *panel_manufacture;
	static const char *panel_version;
#endif /*VENDOR_EDIT*/

	if (!node || !ctrl_pdata) {
		pr_err("%s: Invalid arguments\n", __func__);
		return -ENODEV;
	}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/12,
//add for panel debug
	gl_ctrl_pdata = ctrl_pdata;
#endif /*VENDOR_EDIT*/

	pinfo = &ctrl_pdata->panel_data.panel_info;

	pr_debug("%s:%d\n", __func__, __LINE__);
	pinfo->panel_name[0] = '\0';
	panel_name = of_get_property(node, "qcom,mdss-dsi-panel-name", NULL);
	if (!panel_name) {
		pr_info("%s:%d, Panel name not specified\n",
						__func__, __LINE__);
	} else {
		pr_info("%s: Panel Name = %s\n", __func__, panel_name);
		strlcpy(&pinfo->panel_name[0], panel_name, MDSS_MAX_PANEL_LEN);
	}

#ifdef VENDOR_EDIT
//Guoqiang.Jiang@PSW.MM.Display.LCD.Stability, 2018/10/12,
//add for 16118 Lcd vendor info check
	if(!strcmp(panel_name,"oppo16103jdi r63452 1080p cmd mode dsi panel")){
		lcd_vendor = OPPO16103_JDI_R63452_1080P_CMD_PANEL;
		pr_err("%s:lcd_vendor is oppo16103jdi r63452 1080p cmd mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo16051samsung s6e3fa3 1080p cmd mode dsi panel")){
		lcd_vendor = OPPO16051_SAMSUNG_S6E3FA5_1080P_CMD_PANEL;
		pr_err("%s:lcd_dev is oppo16051samsung s6e3fa3 1080p cmd mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo16118samsung s6e3fa3 1080p cmd mode dsi panel")){
		lcd_vendor = OPPO16118_SAMSUNG_S6E3FA5_1080P_CMD_PANEL;
		pr_err("%s:lcd_dev is oppo16118samsung s6e3fa3 1080p cmd mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo17011samsung sofeg01_s 1080p cmd mode dsi panel")){
		lcd_vendor = OPPO17011_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL;
		pr_err("%s:lcd_dev is oppo17011samsung sofeg01_s 1080p cmd mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo17021samsung sofeg01_s 1080p cmd mode dsi panel")){
		lcd_vendor = OPPO17021_SAMSUNG_SOFEG01_S_1080P_CMD_PANEL;
		pr_err("%s:lcd_dev is oppo17021samsung sofeg01_s 1080p cmd mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo17081samsung ams596w401 1080 2280 cmd mode dsi panel")){
		lcd_vendor = OPPO17081_SAMSUNG_AMS596W401_1080P_CMD_PANEL;
		pr_err("%s:lcd_dev is oppo17081samsung ams596w401 1080 2280 cmd mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo18316himax nt36672 1080 2340 video mode dsi panel")){
		lcd_vendor = OPPO18136_HIMAX_NT36772A_1080_2340_VOD_PANEL;
		pr_err("%s:lcd_dev is oppo18316himax nt36672 1080 2340 video mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo18316himax hx83112a 1080 2340 video mode dsi panel")){
		lcd_vendor = OPPO18136_HIMAX_HX83112A_1080_2340_VOD_PANEL;
		pr_err("%s:lcd_dev is oppo18316himax hx83312a 1080 2340 video mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo18321dpt nt36672a 1080 2340 video mode dsi panel")){
		lcd_vendor = OPPO18321_DPT_NT36672A_1080_2340_VOD_PANEL;
		pr_err("%s:lcd_dev is oppo18321dpt nt36672a 1080 2340 video mode dsi panel\n", __func__);
	}else if(!strcmp(panel_name,"oppo18005samsung ams641rw01 1080 2340 cmd mode dsi panel")){
		lcd_vendor = OPPO18005_SAMSUNG_AMS641RW01_1080P_CMD_PANEL;
		pr_err("%s:lcd_dev is oppo18005samsung ams641w401 1080 2340 cmd mode dsi panel\n", __func__);
	}else{
		lcd_vendor = LCD_UNKNOW;
		pr_err("lcd_dev is unkowned\n");
	}

	panel_manufacture = of_get_property(node, "qcom,mdss-dsi-panel-manufacture", NULL);
	if (!panel_manufacture)
		pr_info("%s:%d, panel manufacture not specified\n", __func__, __LINE__);
	else
		pr_info("%s: Panel Manufacture = %s\n", __func__, panel_manufacture);
	panel_version = of_get_property(node, "qcom,mdss-dsi-panel-version", NULL);
	if (!panel_version)
		pr_info("%s:%d, panel version not specified\n", __func__, __LINE__);
	else
		pr_info("%s: Panel Version = %s\n", __func__, panel_version);

	register_device_proc("lcd", (char *)panel_version, (char *)panel_manufacture);
#endif /*VENDOR_EDIT*/

	rc = mdss_panel_parse_dt(node, ctrl_pdata);
	if (rc) {
		pr_err("%s:%d panel dt parse failed\n", __func__, __LINE__);
		return rc;
	}

	pinfo->dynamic_switch_pending = false;
	pinfo->is_lpm_mode = false;
	pinfo->esd_rdy = false;
	pinfo->persist_mode = false;

	ctrl_pdata->on = mdss_dsi_panel_on;
	ctrl_pdata->post_panel_on = mdss_dsi_post_panel_on;
	ctrl_pdata->off = mdss_dsi_panel_off;
	ctrl_pdata->low_power_config = mdss_dsi_panel_low_power_config;
	ctrl_pdata->panel_data.set_backlight = mdss_dsi_panel_bl_ctrl;
	ctrl_pdata->panel_data.apply_display_setting =
			mdss_dsi_panel_apply_display_setting;
	ctrl_pdata->switch_mode = mdss_dsi_panel_switch_mode;

	return 0;
}
