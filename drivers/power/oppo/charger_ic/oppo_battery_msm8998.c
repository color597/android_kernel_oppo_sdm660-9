/************************************************************************************
** File: 
** VENDOR_EDIT
** Copyright (C), 2008-2016, OPPO Mobile Comm Corp., Ltd
** 
** Description: 
**      for MSM8998 charger
** 
** Version: 1.0
** Date created: 26/12/2016
** Author: Jianchao.Shi@BSP.CHG
** 
** --------------------------- Revision History: ------------------------------------------------------------
* <version>       <date>        <author>              			<desc>
* Revision 1.0    2016-12-26   Jianchao.Shi@BSP.CHG   		Created for new architecture
************************************************************************************************************/
#include "../oppo_charger.h"

#ifdef USE_8998_FILE
static int get_boot_mode(void);
static int smbchg_usb_suspend_disable(struct oppo_chg_chip *chip);
static int smbchg_charging_enble(struct oppo_chg_chip *chip);
static bool oppo_chg_is_usb_present(void);

static void dump_regs(struct oppo_chg_chip *chip)
{
//
}

static int smbchg_kick_wdt(struct oppo_chg_chip *chip)
{
	return 0;
}

static int oppo_chg_hw_init(struct oppo_chg_chip *chip)
{
	int boot_mode = get_boot_mode();

	if (boot_mode != 4/*MSM_BOOT_MODE__RF*/ && boot_mode != 5/*MSM_BOOT_MODE__WLAN*/) {
		smbchg_usb_suspend_disable(chip);
	}
	smbchg_charging_enble(chip);

	return 0;
}

static int smbchg_set_fastchg_current_raw(struct oppo_chg_chip *chip, int current_ma)
{
	int rc = 0;

	rc = vote(chip->pmic_spmi.smb2_chip->chg.fcc_votable, DEFAULT_VOTER,
			true, current_ma * 1000);
	if (rc < 0)
		chg_err("Couldn't vote fcc_votable[%d], rc=%d\n", current_ma, rc);

	return rc;
}

static void smbchg_set_aicl_point(struct oppo_chg_chip *chip, int vol)
{
	//DO Nothing
}

static int oppo_chg_set_input_current(struct oppo_chg_chip *chip, int current_ma)
{
	int rc = 0;

	rc = vote(chip->pmic_spmi.smb2_chip->chg.usb_icl_votable, USB_PSY_VOTER,
			true, current_ma * 1000);
	if (rc < 0)
		chg_err("Couldn't vote usb_icl_votable[%d], rc=%d\n", current_ma, rc);

	return rc;
}

static int smbchg_float_voltage_set(struct oppo_chg_chip *chip, int vfloat_mv)
{
	int rc = 0;

	rc = vote(chip->pmic_spmi.smb2_chip->chg.fv_votable, DEFAULT_VOTER,
			true, vfloat_mv * 1000);
	if (rc < 0)
		chg_err("Couldn't vote fv_votable[%d], rc=%d\n", vfloat_mv, rc);

	return rc;
}

static int smbchg_term_current_set(struct oppo_chg_chip *chip, int term_current)
{
	int rc = 0;
	u8 val_raw = 0;

	if (term_current < 0 || term_current > 750)
		term_current = 150;

	val_raw = term_current / 50;
	rc = smblib_masked_write(&chip->pmic_spmi.smb2_chip->chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
			TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
	if (rc < 0)
		chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);

	return rc;
}

static int smbchg_charging_enble(struct oppo_chg_chip *chip)
{
	int rc = 0;

	rc = vote(chip->pmic_spmi.smb2_chip->chg.chg_disable_votable, DEFAULT_VOTER,
			false, 0);
	if (rc < 0)
		chg_err("Couldn't enable charging, rc=%d\n", rc);

	return rc;
}

static int smbchg_charging_disble(struct oppo_chg_chip *chip)
{
	int rc = 0;

	rc = vote(chip->pmic_spmi.smb2_chip->chg.chg_disable_votable, DEFAULT_VOTER,
			true, 0);
	if (rc < 0)
		chg_err("Couldn't disable charging, rc=%d\n", rc);

	return rc;
}

static int smbchg_get_charge_enable(struct oppo_chg_chip *chip)
{
	int rc = 0;
	u8 temp = 0;

	rc = smblib_read(&chip->pmic_spmi.smb2_chip->chg, CHARGING_ENABLE_CMD_REG, &temp);
	if (rc < 0) {
		chg_err("Couldn't read CHARGING_ENABLE_CMD_REG rc=%d\n", rc);
		return 0;
	}
	rc = temp & CHARGING_ENABLE_CMD_BIT;

	return rc;
}

static int smbchg_usb_suspend_enable(struct oppo_chg_chip *chip)
{
	int rc = 0;

	rc = smblib_set_usb_suspend(&chip->pmic_spmi.smb2_chip->chg, true);
	if (rc < 0)
		chg_err("Couldn't write enable to USBIN_SUSPEND_BIT rc=%d\n", rc);

	return rc;
}

static int smbchg_usb_suspend_disable(struct oppo_chg_chip *chip)
{
	int rc = 0;

	rc = smblib_set_usb_suspend(&chip->pmic_spmi.smb2_chip->chg, false);
	if (rc < 0)
		chg_err("Couldn't write disable to USBIN_SUSPEND_BIT rc=%d\n", rc);

	return rc;
}

static int smbchg_set_rechg_vol(struct oppo_chg_chip *chip, int rechg_vol)
{
	return 0;
}

static int smbchg_reset_charger(struct oppo_chg_chip *chip)
{
	return 0;
}

static int smbchg_read_full(struct oppo_chg_chip *chip)
{
	int rc = 0;
	u8 stat = 0;

	if (!oppo_chg_is_usb_present())
		return 0;

	rc = smblib_read(&chip->pmic_spmi.smb2_chip->chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
	if (rc < 0) {
		chg_err("Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n", rc);
		return 0;
	}
	stat = stat & BATTERY_CHARGER_STATUS_MASK;

	if (stat == TERMINATE_CHARGE || stat == INHIBIT_CHARGE)
		return 1;
	return 0;
}

static int smbchg_otg_enable(void)
{
	return 0;
}

static int smbchg_otg_disable(void)
{
	return 0;
}

static bool qcom_check_charger_resume(struct oppo_chg_chip *chip)
{
	return true;
}

static int smbchg_get_chargerid_volt(struct oppo_chg_chip *chip)
{
	return 0;
}

static void smbchg_set_chargerid_switch_val(struct oppo_chg_chip *chip, int value)
{
//
}

static int smbchg_get_chargerid_switch_val(struct oppo_chg_chip *chip)
{
	return 0;
}

static bool smbchg_need_to_check_ibatt(struct oppo_chg_chip *chip)
{
	return false;
}

static int smbchg_get_chg_current_step(struct oppo_chg_chip *chip)
{
	return 50;
}

static int opchg_get_charger_type(void)
{
	if (!g_oppo_chip)
		return POWER_SUPPLY_TYPE_UNKNOWN;

	return g_oppo_chip->pmic_spmi.smb2_chip->chg.usb_psy_desc.type;
}

static int qpnp_get_prop_charger_voltage_now(void)
{
	int val = 0;
	struct smb_charger *chg = NULL;

	if (!g_oppo_chip)
		return 0;

	//if (!oppo_chg_is_usb_present())
	//	return 0;

	chg = &g_oppo_chip->pmic_spmi.smb2_chip->chg;
	if (!chg->iio.usbin_v_chan || PTR_ERR(chg->iio.usbin_v_chan) == -EPROBE_DEFER)
		chg->iio.usbin_v_chan = iio_channel_get(chg->dev, "usbin_v");

	if (IS_ERR(chg->iio.usbin_v_chan))
		return PTR_ERR(chg->iio.usbin_v_chan);

	iio_read_channel_processed(chg->iio.usbin_v_chan, &val);

	return val / 1000;
}

static bool oppo_chg_is_usb_present(void)
{
	int rc = 0;
	u8 stat = 0;
	bool vbus_rising = false;

	if (!g_oppo_chip)
		return false;

	rc = smblib_read(&g_oppo_chip->pmic_spmi.smb2_chip->chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		chg_err("Couldn't read USB_INT_RT_STS, rc=%d\n", rc);
		return false;
	}
	vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);

	return vbus_rising;
}

static int qpnp_get_battery_voltage(void)
{
	return 3800;//Not use anymore
}

static int get_boot_mode(void)
{
	return 0;
}

static int smbchg_get_boot_reason(void)
{
	return 0;
}

static int oppo_chg_get_shutdown_soc(void)
{
	return 0;
}

static int oppo_chg_backup_soc(int backup_soc)
{
	return 0;
}

static int smbchg_get_aicl_level_ma(struct oppo_chg_chip *chip)
{
	return 0;
}

static void smbchg_rerun_aicl(struct oppo_chg_chip *chip)
{
//
}

static int smbchg_force_tlim_en(struct oppo_chg_chip *chip, bool enable)
{
	return 0;
}

static int smbchg_system_temp_level_set(struct oppo_chg_chip *chip, int lvl_sel)
{
	return 0;
}

static int smbchg_set_prop_flash_active(struct oppo_chg_chip *chip, enum skip_reason reason, bool disable)
{
	return 0;
}

static int smbchg_dp_dm(struct oppo_chg_chip *chip, int val)
{
	return 0;
}

static int smbchg_calc_max_flash_current(struct oppo_chg_chip *chip)
{
	return 0;
}

struct oppo_chg_operations  smb2_chg_ops = {
	.dump_registers = dump_regs,
	.kick_wdt = smbchg_kick_wdt,
	.hardware_init = oppo_chg_hw_init,
	.charging_current_write_fast = smbchg_set_fastchg_current_raw,
	.set_aicl_point = smbchg_set_aicl_point,
	.input_current_write = oppo_chg_set_input_current,
	.float_voltage_write = smbchg_float_voltage_set,
	.term_current_set = smbchg_term_current_set,
	.charging_enable = smbchg_charging_enble,
	.charging_disable = smbchg_charging_disble,
	.get_charging_enable = smbchg_get_charge_enable,
	.charger_suspend = smbchg_usb_suspend_enable,
	.charger_unsuspend = smbchg_usb_suspend_disable,
	.set_rechg_vol = smbchg_set_rechg_vol,
	.reset_charger = smbchg_reset_charger,
	.read_full = smbchg_read_full,
	.otg_enable = smbchg_otg_enable,
	.otg_disable = smbchg_otg_disable,
	.check_charger_resume = qcom_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = smbchg_need_to_check_ibatt,
	.get_chg_current_step = smbchg_get_chg_current_step,
#ifdef CONFIG_OPPO_CHARGER_MTK
	.get_charger_type = mt_power_supply_type_check,
	.get_charger_volt = battery_meter_get_charger_voltage,
	.check_chrdet_status = pmic_chrdet_status,
	.get_instant_vbatt = battery_meter_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = get_boot_reason,
#ifdef CONFIG_MTK_HAFG_20
	.get_rtc_soc = get_rtc_spare_oppo_fg_value,
	.set_rtc_soc = set_rtc_spare_oppo_fg_value,
#else
	.get_rtc_soc = get_rtc_spare_fg_value,
	.set_rtc_soc = set_rtc_spare_fg_value,
#endif	/* CONFIG_MTK_HAFG_20 */
	.set_power_off = mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
#else
	.get_charger_type = opchg_get_charger_type,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.check_chrdet_status = oppo_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oppo_chg_get_shutdown_soc,
	.set_rtc_soc = oppo_chg_backup_soc,
	.get_aicl_ma = smbchg_get_aicl_level_ma,
	.rerun_aicl = smbchg_rerun_aicl,
	.tlim_en = smbchg_force_tlim_en,
	.set_system_temp_level = smbchg_system_temp_level_set,
	.otg_pulse_skip_disable = smbchg_set_prop_flash_active,
	.set_dp_dm = smbchg_dp_dm,
	.calc_flash_current = smbchg_calc_max_flash_current,
#endif	/* CONFIG_OPPO_CHARGER_MTK */
};
#endif

