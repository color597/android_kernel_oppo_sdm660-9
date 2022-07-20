#ifndef _OPPO_BATTERY_QCOM_MSM8998_H_
#define _OPPO_BATTERY_QCOM_MSM8998_H_

#include <linux/of.h>

#include "../../../../kernel/msm-4.4/drivers/power/supply/qcom/smb-lib.h"

//extern struct oppo_chg_operations  smb2_chg_ops;

enum skip_reason {
	REASON_OTG_ENABLED	= BIT(0),
	REASON_FLASH_ENABLED	= BIT(1)
};

#define STEP_CHARGING_MAX_STEPS	5
struct smb_dt_props {
	int	fcc_ua;
	int	usb_icl_ua;
	int	dc_icl_ua;
	int	boost_threshold_ua;
	int	fv_uv;
	int	wipower_max_uw;
	int     min_freq_khz;
	int     max_freq_khz;
	u32	step_soc_threshold[STEP_CHARGING_MAX_STEPS - 1];
	s32	step_cc_delta[STEP_CHARGING_MAX_STEPS];
	struct	device_node *revid_dev_node;
	int	float_option;
	int	chg_inhibit_thr_mv;
	bool	no_battery;
	bool	hvdcp_disable;
	bool	auto_recharge_soc;
};

struct smb2 {
	struct smb_charger	chg;
	struct dentry		*dfs_root;
	struct smb_dt_props	dt;
	bool			bad_part;
};

struct qcom_pmic {
	struct smb2 *smb2_chip;
	struct qpnp_vadc_chip	*pm660_vadc_dev;

	/* for complie*/
	bool			otg_pulse_skip_dis;
	int			pulse_cnt;
	unsigned int	therm_lvl_sel;
	bool			psy_registered;
	int			usb_online;
	
	/* copy from msm8976_pmic begin */
	int			bat_charging_state;
	bool	 		suspending;
	bool			aicl_suspend;
	bool			usb_hc_mode;
	int    		usb_hc_count;
	bool			hc_mode_flag;
	/* copy form msm8976_pmic end */
};

#endif

