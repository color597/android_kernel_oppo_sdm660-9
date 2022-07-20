/* Copyright (c) 2018 The Linux Foundation. All rights reserved.
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

#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/irq.h>
#include <linux/pmic-voter.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/of_batterydata.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/log2.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/input/qpnp-power-on.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>

#include <soc/oppo/boot_mode.h>
#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>

#ifdef VENDOR_EDIT
/* Yichun Chen  PSW.BSP.CHG  2018/04/25  OPPO_CHARGE */
#include "../oppo_charger.h"
#include "../oppo_gauge.h"
#include "../oppo_vooc.h"
#include "../oppo_short.h"

struct oppo_chg_chip *g_oppo_chip = NULL;
struct smb_charger *g_smb_chip = NULL;
static bool fv_adjust_enable = false;
static int fv_adjust_count = 0;

int schgm_flash_get_vreg_ok(struct smb_charger *chg, int *val);
int schgm_flash_init(struct smb_charger *chg);

irqreturn_t schgm_flash_default_irq_handler(int irq, void *data);
irqreturn_t schgm_flash_ilim2_irq_handler(int irq, void *data);
irqreturn_t schgm_flash_state_change_irq_handler(int irq, void *data);

void smbchg_set_chargerid_switch_val(struct oppo_chg_chip *chip, int value);
static int smbchg_chargerid_switch_gpio_init(struct oppo_chg_chip *chip);
extern void oppo_chg_turn_off_charging(struct oppo_chg_chip *chip);
extern void oppo_chg_turn_on_charging(struct oppo_chg_chip *chip);
bool oppo_usbid_check_is_gpio(struct oppo_chg_chip *chip);
static int oppo_usbid_gpio_init(struct oppo_chg_chip *chip);
static void oppo_usbid_irq_init(struct oppo_chg_chip *chip);
static void oppo_set_usbid_active(struct oppo_chg_chip *chip);
static void oppo_set_usbid_sleep(struct oppo_chg_chip *chip);
#ifdef VENDOR_EDIT
/*Yichun.Chen  PWS.BSP.CHG  2018/05/15  shortc*/
static bool oppo_shortc_check_is_gpio(struct oppo_chg_chip *chip);
static int oppo_shortc_gpio_init(struct oppo_chg_chip *chip);
#endif

int oppo_tbatt_power_off_task_init(struct oppo_chg_chip *chip);

/* wenbin.liu  add for TP issue */
void __attribute__((weak)) switch_usb_state(int usb_state) {return;}

#define OPPO_CHG_MONITOR_INTERVAL round_jiffies_relative(msecs_to_jiffies(5000))
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25  operate register */
#define BIT0  BIT(0)
#define BIT1  BIT(1)
#define BIT2  BIT(2)
#define BIT3  BIT(3)
#define BIT4  BIT(4)
#define BIT5  BIT(5)
#define BIT6  BIT(6)
#define BIT7  BIT(7)
#endif

#define smblib_err(chg, fmt, ...)               \
        pr_err("%s: %s: " fmt, chg->name,       \
                __func__, ##__VA_ARGS__)        \

#define smblib_dbg(chg, reason, fmt, ...)                       \
        do {                                                    \
                if (*chg->debug_mask & (reason))                \
                        pr_info("%s: %s: " fmt, chg->name,      \
                                __func__, ##__VA_ARGS__);       \
                else                                            \
                        pr_debug("%s: %s: " fmt, chg->name,     \
                                __func__, ##__VA_ARGS__);       \
        } while (0)

int smblib_read(struct smb_charger *chg, u16 addr, u8 *val)
{
        unsigned int value;
        int rc = 0;

        rc = regmap_read(chg->regmap, addr, &value);
        if (rc >= 0)
                *val = (u8)value;

        return rc;
}

int smblib_batch_read(struct smb_charger *chg, u16 addr, u8 *val,
                        int count)
{
        return regmap_bulk_read(chg->regmap, addr, val, count);
}

int smblib_write(struct smb_charger *chg, u16 addr, u8 val)
{
        return regmap_write(chg->regmap, addr, val);
}

int smblib_batch_write(struct smb_charger *chg, u16 addr, u8 *val,
                        int count)
{
        return regmap_bulk_write(chg->regmap, addr, val, count);
}

int smblib_masked_write(struct smb_charger *chg, u16 addr, u8 mask, u8 val)
{
        return regmap_update_bits(chg->regmap, addr, mask, val);
}

int smblib_get_jeita_cc_delta(struct smb_charger *chg, int *cc_delta_ua)
{
        int rc, cc_minus_ua;
        u8 stat;

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_7_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
                        rc);
                return rc;
        }

        if (stat & BAT_TEMP_STATUS_HOT_SOFT_BIT) {
                rc = smblib_get_charge_param(chg, &chg->param.jeita_cc_comp_hot,
                                        &cc_minus_ua);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't get jeita cc minus rc=%d\n",
                                        rc);
                        return rc;
                }
        } else if (stat & BAT_TEMP_STATUS_COLD_SOFT_BIT) {
                rc = smblib_get_charge_param(chg,
                                        &chg->param.jeita_cc_comp_cold,
                                        &cc_minus_ua);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't get jeita cc minus rc=%d\n",
                                        rc);
                        return rc;
                }
        } else {
                cc_minus_ua = 0;
        }

        *cc_delta_ua = -cc_minus_ua;

        return 0;
}

int smblib_stat_sw_override_cfg(struct smb_charger *chg, bool override)
{
        int rc = 0;

        /* override  = 1, SW STAT override; override = 0, HW auto mode */
        rc = smblib_masked_write(chg, MISC_SMB_EN_CMD_REG,
                                SMB_EN_OVERRIDE_BIT,
                                override ? SMB_EN_OVERRIDE_BIT : 0);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure SW STAT override rc=%d\n",
                        rc);
                return rc;
        }

        return rc;
}

static void smblib_notify_extcon_props(struct smb_charger *chg, int id)
{
        union extcon_property_value val;
        union power_supply_propval prop_val;

        if (chg->connector_type == POWER_SUPPLY_CONNECTOR_TYPEC) {
                smblib_get_prop_typec_cc_orientation(chg, &prop_val);
                val.intval = ((prop_val.intval == 2) ? 1 : 0);
                extcon_set_property(chg->extcon, id,
                                EXTCON_PROP_USB_TYPEC_POLARITY, val);
        }

        val.intval = true;
        extcon_set_property(chg->extcon, id,
                                EXTCON_PROP_USB_SS, val);
}

static void smblib_notify_device_mode(struct smb_charger *chg, bool enable)
{
        if (enable) {
                smblib_notify_extcon_props(chg, EXTCON_USB);
        }

        extcon_set_state_sync(chg->extcon, EXTCON_USB, enable);
}

static void smblib_notify_usb_host(struct smb_charger *chg, bool enable)
{
        if (enable) {
                smblib_notify_extcon_props(chg, EXTCON_USB_HOST);
        }

        extcon_set_state_sync(chg->extcon, EXTCON_USB_HOST, enable);
}

/********************
 * REGISTER GETTERS *
 ********************/

int smblib_get_charge_param(struct smb_charger *chg,
                            struct smb_chg_param *param, int *val_u)
{
        int rc = 0;
        u8 val_raw;

        rc = smblib_read(chg, param->reg, &val_raw);
        if (rc < 0) {
                smblib_err(chg, "%s: Couldn't read from 0x%04x rc=%d\n",
                        param->name, param->reg, rc);
                return rc;
        }

        if (param->get_proc) {
                *val_u = param->get_proc(param, val_raw);
        } else {
                *val_u = val_raw * param->step_u + param->min_u;
        }
        smblib_dbg(chg, PR_REGISTER, "%s = %d (0x%02x)\n",
                   param->name, *val_u, val_raw);

        return rc;
}

int smblib_get_usb_suspend(struct smb_charger *chg, int *suspend)
{
        int rc = 0;
        u8 temp;

        rc = smblib_read(chg, USBIN_CMD_IL_REG, &temp);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read USBIN_CMD_IL rc=%d\n", rc);
                return rc;
        }
        *suspend = temp & USBIN_SUSPEND_BIT;

        return rc;
}

struct apsd_result {
        const char * const name;
        const u8 bit;
        const enum power_supply_type pst;
};

enum {
        UNKNOWN,
        SDP,
        CDP,
        DCP,
        OCP,
        FLOAT,
        HVDCP2,
        HVDCP3,
        MAX_TYPES
};

static const struct apsd_result smblib_apsd_results[] = {
        [UNKNOWN] = {
                .name   = "UNKNOWN",
                .bit    = 0,
                .pst    = POWER_SUPPLY_TYPE_UNKNOWN
        },
        [SDP] = {
                .name   = "SDP",
                .bit    = SDP_CHARGER_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB
        },
        [CDP] = {
                .name   = "CDP",
                .bit    = CDP_CHARGER_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB_CDP
        },
        [DCP] = {
                .name   = "DCP",
                .bit    = DCP_CHARGER_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB_DCP
        },
        [OCP] = {
                .name   = "OCP",
                .bit    = OCP_CHARGER_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB_DCP
        },
#ifndef VENDOR_EDIT
// wenbin.liu@BSP.CHG.Basic, 2017/11/17
// Add for float charger to DCP
        [FLOAT] = {
                .name   = "FLOAT",
                .bit    = FLOAT_CHARGER_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB_FLOAT
        },
#else
        [FLOAT] = {
                .name   = "FLOAT",
                .bit    = FLOAT_CHARGER_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB_DCP
        },
#endif
        [HVDCP2] = {
                .name   = "HVDCP2",
                .bit    = DCP_CHARGER_BIT | QC_2P0_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB_HVDCP
        },
        [HVDCP3] = {
                .name   = "HVDCP3",
                .bit    = DCP_CHARGER_BIT | QC_3P0_BIT,
                .pst    = POWER_SUPPLY_TYPE_USB_HVDCP_3,
        },
};

static const struct apsd_result *smblib_get_apsd_result(struct smb_charger *chg)
{
        int rc, i;
        u8 apsd_stat, stat;
        const struct apsd_result *result = &smblib_apsd_results[UNKNOWN];

        rc = smblib_read(chg, APSD_STATUS_REG, &apsd_stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read APSD_STATUS rc=%d\n", rc);
                return result;
        }
        smblib_dbg(chg, PR_REGISTER, "APSD_STATUS = 0x%02x\n", apsd_stat);

        if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
                return result;
        }

        rc = smblib_read(chg, APSD_RESULT_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read APSD_RESULT_STATUS rc=%d\n",
                        rc);
                return result;
        }
        stat &= APSD_RESULT_STATUS_MASK;

        for (i = 0; i < ARRAY_SIZE(smblib_apsd_results); i++) {
                if (smblib_apsd_results[i].bit == stat) {
                        result = &smblib_apsd_results[i];
                }
        }

        if (apsd_stat & QC_CHARGER_BIT) {
                /* since its a qc_charger, either return HVDCP3 or HVDCP2 */
#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/25, sjc Modify for charging */
                if (result != &smblib_apsd_results[HVDCP3]) {
                        result = &smblib_apsd_results[HVDCP2];
                }
#else
                if (result != &smblib_apsd_results[HVDCP3] && result->bit == (DCP_CHARGER_BIT | QC_2P0_BIT)) {
                        result = &smblib_apsd_results[HVDCP2];
                }
#endif
        }

        return result;
}

/********************
 * REGISTER SETTERS *
 ********************/
 
static const struct buck_boost_freq chg_freq_list[] = {
        [0] = {
                .freq_khz       = 2400,
                .val            = 7,
        },
        [1] = {
                .freq_khz       = 2100,
                .val            = 8,
        },
        [2] = {
                .freq_khz       = 1600,
                .val            = 11,
        },
        [3] = {
                .freq_khz       = 1200,
                .val            = 15,
        },
};

int smblib_set_chg_freq(struct smb_chg_param *param,
                                int val_u, u8 *val_raw)
{
        u8 i;

        if (val_u > param->max_u || val_u < param->min_u) {
                return -EINVAL;
        }

        /* Charger FSW is the configured freqency / 2 */
        val_u *= 2;
        for (i = 0; i < ARRAY_SIZE(chg_freq_list); i++) {
                if (chg_freq_list[i].freq_khz == val_u) {
                        break;
                }
        }
        if (i == ARRAY_SIZE(chg_freq_list)) {
                pr_err("Invalid frequency %d Hz\n", val_u / 2);
                return -EINVAL;
        }

        *val_raw = chg_freq_list[i].val;

        return 0;
}

int smblib_set_opt_switcher_freq(struct smb_charger *chg, int fsw_khz)
{
        union power_supply_propval pval = {0, };
        int rc = 0;

        rc = smblib_set_charge_param(chg, &chg->param.freq_switcher, fsw_khz);
        if (rc < 0) {
                dev_err(chg->dev, "Error in setting freq_buck rc=%d\n", rc);
        }

        if (chg->mode == PARALLEL_MASTER && chg->pl.psy) {
                pval.intval = fsw_khz;
                /*
                 * Some parallel charging implementations may not have
                 * PROP_BUCK_FREQ property - they could be running
                 * with a fixed frequency
                 */
                power_supply_set_property(chg->pl.psy,
                                POWER_SUPPLY_PROP_BUCK_FREQ, &pval);
        }

        return rc;
}

int smblib_set_charge_param(struct smb_charger *chg,
                            struct smb_chg_param *param, int val_u)
{
        int rc = 0;
        u8 val_raw;

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25  reduce log */
        int value_old, value_new;

        rc = smblib_get_charge_param(chg, param, &value_old);
        if (rc < 0) {
                chg_err("error!!! Couldn't get param %s\n", param->name);
                return rc;
        }
#endif

        if (param->set_proc) {
                rc = param->set_proc(param, val_u, &val_raw);
                if (rc < 0) {
                        return -EINVAL;
                }
        } else {
                if (val_u > param->max_u || val_u < param->min_u) {
                        smblib_dbg(chg, PR_MISC,
                                "%s: %d is out of range [%d, %d]\n",
                                param->name, val_u, param->min_u, param->max_u);
                }

                if (val_u > param->max_u) {
                        val_u = param->max_u;
                }
                if (val_u < param->min_u) {
                        val_u = param->min_u;
                }

                val_raw = (val_u - param->min_u) / param->step_u;
        }

        rc = smblib_write(chg, param->reg, val_raw);
        if (rc < 0) {
                smblib_err(chg, "%s: Couldn't write 0x%02x to 0x%04x rc=%d\n",
                        param->name, val_raw, param->reg, rc);
                return rc;
        }

        smblib_dbg(chg, PR_REGISTER, "%s = %d (0x%02x)\n",
                   param->name, val_u, val_raw);

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25  debug */
        rc = smblib_get_charge_param(chg, param, &value_new);
        if (rc < 0) {
                chg_err("error!!! Couldn't get param %s\n", param->name);
                return rc;
        }

        if (value_new != value_old) {
                chg_debug("name = %s, value_new = %d, value_old = %d, input_para = %d\n",
                        param->name, value_new, value_old, val_u);
        }
#endif

        return rc;
}

int smblib_set_usb_suspend(struct smb_charger *chg, bool suspend)
{
        int rc = 0;
        int irq = chg->irq_info[USBIN_ICL_CHANGE_IRQ].irq;

        if (suspend && irq) {
                if (chg->usb_icl_change_irq_enabled) {
                        disable_irq_nosync(irq);
                        chg->usb_icl_change_irq_enabled = false;
                }
        }

        rc = smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT,
                                 suspend ? USBIN_SUSPEND_BIT : 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write %s to USBIN_SUSPEND_BIT rc=%d\n",
                        suspend ? "suspend" : "resume", rc);
        }

        if (!suspend && irq) {
                if (!chg->usb_icl_change_irq_enabled) {
                        enable_irq(irq);
                        chg->usb_icl_change_irq_enabled = true;
                }
        }

        return rc;
}

int smblib_set_dc_suspend(struct smb_charger *chg, bool suspend)
{
        int rc = 0;

        rc = smblib_masked_write(chg, DCIN_CMD_IL_REG, DCIN_SUSPEND_BIT,
                                 suspend ? DCIN_SUSPEND_BIT : 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write %s to DCIN_SUSPEND_BIT rc=%d\n",
                        suspend ? "suspend" : "resume", rc);
        }

        return rc;
}

static int smblib_set_adapter_allowance(struct smb_charger *chg,
                                        u8 allowed_voltage)
{
        int rc = 0;

        rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG, allowed_voltage);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
                        allowed_voltage, rc);
                return rc;
        }

        return rc;
}

#define MICRO_5V        5000000
#define MICRO_9V        9000000
#define MICRO_12V       12000000
static int smblib_set_usb_pd_allowed_voltage(struct smb_charger *chg,
                                        int min_allowed_uv, int max_allowed_uv)
{
        int rc;
        u8 allowed_voltage;

        if (min_allowed_uv == MICRO_5V && max_allowed_uv == MICRO_5V) {
                allowed_voltage = USBIN_ADAPTER_ALLOW_5V;
                smblib_set_opt_switcher_freq(chg, chg->chg_freq.freq_5V);
        } else if (min_allowed_uv == MICRO_9V && max_allowed_uv == MICRO_9V) {
                allowed_voltage = USBIN_ADAPTER_ALLOW_9V;
                smblib_set_opt_switcher_freq(chg, chg->chg_freq.freq_9V);
        } else if (min_allowed_uv == MICRO_12V && max_allowed_uv == MICRO_12V) {
                allowed_voltage = USBIN_ADAPTER_ALLOW_12V;
                smblib_set_opt_switcher_freq(chg, chg->chg_freq.freq_12V);
        } else if (min_allowed_uv < MICRO_9V && max_allowed_uv <= MICRO_9V) {
                allowed_voltage = USBIN_ADAPTER_ALLOW_5V_TO_9V;
        } else if (min_allowed_uv < MICRO_9V && max_allowed_uv <= MICRO_12V) {
                allowed_voltage = USBIN_ADAPTER_ALLOW_5V_TO_12V;
        } else if (min_allowed_uv < MICRO_12V && max_allowed_uv <= MICRO_12V) {
                allowed_voltage = USBIN_ADAPTER_ALLOW_9V_TO_12V;
        } else {
                smblib_err(chg, "invalid allowed voltage [%d, %d]\n",
                        min_allowed_uv, max_allowed_uv);
                return -EINVAL;
        }

        rc = smblib_set_adapter_allowance(chg, allowed_voltage);
        if (rc < 0) {
                smblib_err(chg, "Couldn't configure adapter allowance rc=%d\n",
                                rc);
                return rc;
        }

        return rc;
}

/********************
 * HELPER FUNCTIONS *
 ********************/
 
int smblib_configure_hvdcp_apsd(struct smb_charger *chg, bool enable)
{
        int rc;
        u8 mask = HVDCP_EN_BIT | BC1P2_SRC_DETECT_BIT;

        if (chg->pd_disabled) {
                return 0;
        }

        rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG, mask,
                                                enable ? mask : 0);
        if (rc < 0) {
                smblib_err(chg, "failed to write USBIN_OPTIONS_1_CFG rc=%d\n",
                                rc);
        }

        return rc;
}

static int smblib_request_dpdm(struct smb_charger *chg, bool enable)
{
        int rc = 0;

        /* fetch the DPDM regulator */
        if (!chg->dpdm_reg && of_get_property(chg->dev->of_node,
                                "dpdm-supply", NULL)) {
                chg->dpdm_reg = devm_regulator_get(chg->dev, "dpdm");
                if (IS_ERR(chg->dpdm_reg)) {
                        rc = PTR_ERR(chg->dpdm_reg);
                        smblib_err(chg, "Couldn't get dpdm regulator rc=%d\n",
                                        rc);
                        chg->dpdm_reg = NULL;
                        return rc;
                }
        }

        if (enable) {
                if (chg->dpdm_reg && !regulator_is_enabled(chg->dpdm_reg)) {
                        smblib_dbg(chg, PR_MISC, "enabling DPDM regulator\n");
                        rc = regulator_enable(chg->dpdm_reg);
                        if (rc < 0) {
                                smblib_err(chg,
                                        "Couldn't enable dpdm regulator rc=%d\n",
                                        rc);
                        }
                }
        } else {
                if (chg->dpdm_reg && regulator_is_enabled(chg->dpdm_reg)) {
                        smblib_dbg(chg, PR_MISC, "disabling DPDM regulator\n");
                        rc = regulator_disable(chg->dpdm_reg);
                        if (rc < 0) {
                                smblib_err(chg,
                                        "Couldn't disable dpdm regulator rc=%d\n",
                                        rc);
                        }
                }
        }

        return rc;
}

static void smblib_rerun_apsd(struct smb_charger *chg)
{
        int rc;

        smblib_dbg(chg, PR_MISC, "re-running APSD\n");

        chg_debug("[plugin]re-running APSD\n");

        rc = smblib_masked_write(chg, CMD_APSD_REG,
                                APSD_RERUN_BIT, APSD_RERUN_BIT);
        if (rc < 0) {
                smblib_err(chg, "Couldn't re-run APSD rc=%d\n", rc);
        }
}

static const struct apsd_result *smblib_update_usb_type(struct smb_charger *chg)
{
        const struct apsd_result *apsd_result = smblib_get_apsd_result(chg);

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25  debug */
        chg_debug("[plugin]***********  type_name = %s, real_type = %d **************\n",
                        apsd_result->name, chg->real_charger_type);
#endif

        /* if PD is active, APSD is disabled so won't have a valid result */
        if (chg->pd_active) {
                chg->real_charger_type = POWER_SUPPLY_TYPE_USB_PD;
        } else {
                /*
                 * Update real charger type only if its not FLOAT
                 * detected as as SDP
                 */
                if (!(apsd_result->pst == POWER_SUPPLY_TYPE_USB_FLOAT &&
                        chg->real_charger_type == POWER_SUPPLY_TYPE_USB)) {
                        chg->real_charger_type = apsd_result->pst;
                }
        }

        smblib_dbg(chg, PR_MISC, "APSD=%s PD=%d\n",
                                        apsd_result->name, chg->pd_active);

        return apsd_result;
}

static int smblib_notifier_call(struct notifier_block *nb,
                unsigned long ev, void *v)
{
        struct power_supply *psy = v;
        struct smb_charger *chg = container_of(nb, struct smb_charger, nb);

        if (!strcmp(psy->desc->name, "bms")) {
                if (!chg->bms_psy) {
                        chg->bms_psy = psy;
                }
                if (ev == PSY_EVENT_PROP_CHANGED) {
                        schedule_work(&chg->bms_update_work);
                }
                if (!chg->jeita_configured) {
                        schedule_work(&chg->jeita_update_work);
                }
        }

        if (!chg->pl.psy && !strcmp(psy->desc->name, "parallel")) {
                chg->pl.psy = psy;
                schedule_work(&chg->pl_update_work);
        }

        return NOTIFY_OK;
}

static int smblib_register_notifier(struct smb_charger *chg)
{
        int rc;

        chg->nb.notifier_call = smblib_notifier_call;
        rc = power_supply_reg_notifier(&chg->nb);
        if (rc < 0) {
                smblib_err(chg, "Couldn't register psy notifier rc = %d\n", rc);
                return rc;
        }

        return 0;
}

int smblib_mapping_soc_from_field_value(struct smb_chg_param *param,
                                             int val_u, u8 *val_raw)
{
        if (val_u > param->max_u || val_u < param->min_u) {
                return -EINVAL;
        }

        *val_raw = val_u << 1;

        return 0;
}

int smblib_mapping_cc_delta_to_field_value(struct smb_chg_param *param,
                                           u8 val_raw)
{
        int val_u  = val_raw * param->step_u + param->min_u;

        if (val_u > param->max_u) {
                val_u -= param->max_u * 2;
        }

        return val_u;
}

int smblib_mapping_cc_delta_from_field_value(struct smb_chg_param *param,
                                             int val_u, u8 *val_raw)
{
        if (val_u > param->max_u || val_u < param->min_u - param->max_u) {
                return -EINVAL;
        }

        val_u += param->max_u * 2 - param->min_u;
        val_u %= param->max_u * 2;
        *val_raw = val_u / param->step_u;

        return 0;
}

#define USBIN_100MA     100000
static void smblib_uusb_removal(struct smb_charger *chg)
{
        int rc;
        struct smb_irq_data *data;
        struct storm_watch *wdata;

        cancel_delayed_work_sync(&chg->pl_enable_work);

        if (chg->wa_flags & BOOST_BACK_WA) {
                data = chg->irq_info[SWITCHER_POWER_OK_IRQ].irq_data;
                if (data) {
                        wdata = &data->storm_data;
                        update_storm_count(wdata, WEAK_CHG_STORM_COUNT);
                        vote(chg->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
                        vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
                                        false, 0);
                }
        }
        vote(chg->pl_disable_votable, PL_DELAY_VOTER, true, 0);
        vote(chg->awake_votable, PL_DELAY_VOTER, false, 0);

        /* reset both usbin current and voltage votes */
        vote(chg->pl_enable_votable_indirect, USBIN_I_VOTER, false, 0);
        vote(chg->pl_enable_votable_indirect, USBIN_V_VOTER, false, 0);
        vote(chg->usb_icl_votable, SW_QC3_VOTER, false, 0);

        /* reconfigure allowed voltage for HVDCP */
        rc = smblib_set_adapter_allowance(chg,
                        USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V);
        if (rc < 0) {
                smblib_err(chg, "Couldn't set USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V rc=%d\n",
                        rc);
        }

        chg->voltage_min_uv = MICRO_5V;
        chg->voltage_max_uv = MICRO_5V;
        chg->usb_icl_delta_ua = 0;
        chg->pulse_cnt = 0;
        chg->uusb_apsd_rerun_done = false;

        /* write back the default FLOAT charger configuration */
        rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
                                (u8)FLOAT_OPTIONS_MASK, chg->float_cfg);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write float charger options rc=%d\n",
                        rc);
        }

        /* leave the ICL configured to 100mA for next insertion */
        vote(chg->usb_icl_votable, DEFAULT_100MA_VOTER, true, USBIN_100MA);

        /* clear USB ICL vote for USB_PSY_VOTER */
        rc = vote(chg->usb_icl_votable, USB_PSY_VOTER, false, 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't un-vote for USB ICL rc=%d\n", rc);
        }

        /* clear USB ICL vote for DCP_VOTER */
        rc = vote(chg->usb_icl_votable, DCP_VOTER, false, 0);
        if (rc < 0) {
                smblib_err(chg,
                        "Couldn't un-vote DCP from USB ICL rc=%d\n", rc);
        }
}

void smblib_suspend_on_debug_battery(struct smb_charger *chg)
{
        int rc;
        union power_supply_propval val;

        rc = power_supply_get_property(chg->bms_psy,
                        POWER_SUPPLY_PROP_DEBUG_BATTERY, &val);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get debug battery prop rc=%d\n", rc);
                return;
        }
        if (chg->suspend_input_on_debug_batt) {
                vote(chg->usb_icl_votable, DEBUG_BOARD_VOTER, val.intval, 0);
                vote(chg->dc_suspend_votable, DEBUG_BOARD_VOTER, val.intval, 0);
                if (val.intval) {
                        pr_info("Input suspended: Fake battery\n");
                }
        } else {
                vote(chg->chg_disable_votable, DEBUG_BOARD_VOTER,
                                        val.intval, 0);
        }
}

int smblib_rerun_apsd_if_required(struct smb_charger *chg)
{
        union power_supply_propval val;
        int rc;
/* wenbin.liu@BSP.CHG.Basic, 2017/12/06   Add for not rerun  if not usb */
        const struct apsd_result *apsd_result;

        rc = smblib_get_prop_usb_present(chg, &val);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get usb present rc = %d\n", rc);
                return rc;
        }

        if (!val.intval) {
                return 0;
        }

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/06/19, sjc Add to rerun apsd */
        apsd_result = smblib_get_apsd_result(chg);
        if ((apsd_result->pst != POWER_SUPPLY_TYPE_UNKNOWN)
                && (apsd_result->pst != POWER_SUPPLY_TYPE_USB)
                && (apsd_result->pst != POWER_SUPPLY_TYPE_USB_CDP)) {
                /* if type is not usb or unknown no need to rerun apsd */
                return 0;
        }
#endif

        rc = smblib_request_dpdm(chg, true);
        if (rc < 0) {
                smblib_err(chg, "Couldn't to enable DPDM rc=%d\n", rc);
        }

        chg->uusb_apsd_rerun_done = true;
        smblib_rerun_apsd(chg);

        return 0;
}

static int smblib_get_pulse_cnt(struct smb_charger *chg, int *count)
{
        *count = chg->pulse_cnt;
        return 0;
}

#define USBIN_25MA      25000
#define USBIN_150MA     150000
#define USBIN_500MA     500000
#define USBIN_900MA     900000
static int set_sdp_current(struct smb_charger *chg, int icl_ua)
{
        int rc;
        u8 icl_options;
        const struct apsd_result *apsd_result = smblib_get_apsd_result(chg);

        /* power source is SDP */
        switch (icl_ua) {
        case USBIN_100MA:
                /* USB 2.0 100mA */
                icl_options = 0;
                break;
        case USBIN_150MA:
                /* USB 3.0 150mA */
                icl_options = CFG_USB3P0_SEL_BIT;
                break;
        case USBIN_500MA:
                /* USB 2.0 500mA */
                icl_options = USB51_MODE_BIT;
                break;
        case USBIN_900MA:
                /* USB 3.0 900mA */
                icl_options = CFG_USB3P0_SEL_BIT | USB51_MODE_BIT;
                break;
        default:
                smblib_err(chg, "ICL %duA isn't supported for SDP\n", icl_ua);
                return -EINVAL;
        }
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/20, sjc Add for charging */
        if (icl_ua <= USBIN_150MA) {
                icl_options = 0;
        } else {
                icl_options = USB51_MODE_BIT;
        }
#endif

        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB &&
                apsd_result->pst == POWER_SUPPLY_TYPE_USB_FLOAT) {
                /*
                 * change the float charger configuration to SDP, if this
                 * is the case of SDP being detected as FLOAT
                 */
                rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
                        FORCE_FLOAT_SDP_CFG_BIT, FORCE_FLOAT_SDP_CFG_BIT);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't set float ICL options rc=%d\n",
                                                rc);
                        return rc;
                }
        }

        rc = smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
                CFG_USB3P0_SEL_BIT | USB51_MODE_BIT, icl_options);
        if (rc < 0) {
                smblib_err(chg, "Couldn't set ICL options rc=%d\n", rc);
                return rc;
        }

        return rc;
}

static int get_sdp_current(struct smb_charger *chg, int *icl_ua)
{
        int rc;
        u8 icl_options;
        bool usb3 = false;

        rc = smblib_read(chg, USBIN_ICL_OPTIONS_REG, &icl_options);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get ICL options rc=%d\n", rc);
                return rc;
        }

        usb3 = (icl_options & CFG_USB3P0_SEL_BIT);

        if (icl_options & USB51_MODE_BIT) {
                *icl_ua = usb3 ? USBIN_900MA : USBIN_500MA;
        } else {
                *icl_ua = usb3 ? USBIN_150MA : USBIN_100MA;
        }

        return rc;
}

int smblib_set_icl_current(struct smb_charger *chg, int icl_ua)
{
        int rc = 0;
        bool hc_mode = false;

        /* suspend and return if 25mA or less is requested */
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/11, sjc Add for charging */
        int boot_mode = get_boot_mode();
        if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN) {
                icl_ua = 0;
        }
#endif

        if (icl_ua <= USBIN_25MA) {
                return smblib_set_usb_suspend(chg, true);
        }

        if (icl_ua == INT_MAX) {
                goto set_mode;
        }

        /* configure current */
        if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)
                || (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                && (chg->real_charger_type == POWER_SUPPLY_TYPE_USB)) {
                rc = set_sdp_current(chg, icl_ua);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't set SDP ICL rc=%d\n", rc);
                        goto out;
                }
        } else {
                set_sdp_current(chg, 100000);
                rc = smblib_set_charge_param(chg, &chg->param.usb_icl, icl_ua);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't set HC ICL rc=%d\n", rc);
                        goto out;
                }
                hc_mode = true;
        }

set_mode:
        rc = smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
                USBIN_MODE_CHG_BIT, hc_mode ? USBIN_MODE_CHG_BIT : 0);

        /* unsuspend after configuring current and override */
        rc = smblib_set_usb_suspend(chg, false);
        if (rc < 0) {
                smblib_err(chg, "Couldn't resume input rc=%d\n", rc);
                goto out;
        }

        /* Re-run AICL */
        if (chg->real_charger_type != POWER_SUPPLY_TYPE_USB) {
                rc = smblib_rerun_aicl(chg);
        }

out:
        return rc;
}

int smblib_get_icl_current(struct smb_charger *chg, int *icl_ua)
{
        int rc = 0;
        u8 load_cfg;
        bool override;

        if ((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT
                || chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
                && (chg->usb_psy->desc->type == POWER_SUPPLY_TYPE_USB)) {
                rc = get_sdp_current(chg, icl_ua);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't get SDP ICL rc=%d\n", rc);
                        return rc;
                }
        } else {
                rc = smblib_read(chg, USBIN_LOAD_CFG_REG, &load_cfg);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't get load cfg rc=%d\n", rc);
                        return rc;
                }
                override = load_cfg & ICL_OVERRIDE_AFTER_APSD_BIT;
                if (!override) {
                        return INT_MAX;
                }

                /* override is set */
                rc = smblib_get_charge_param(chg, &chg->param.usb_icl, icl_ua);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't get HC ICL rc=%d\n", rc);
                        return rc;
                }
        }

        return 0;
}

/*********************
 * VOTABLE CALLBACKS *
 *********************/

static int smblib_dc_suspend_vote_callback(struct votable *votable, void *data,
                        int suspend, const char *client)
{
        struct smb_charger *chg = data;

        if (chg->smb_version == PMI632_SUBTYPE) {
                return 0;
        }

        /* resume input if suspend is invalid */
        if (suspend < 0) {
                suspend = 0;
        }

        return smblib_set_dc_suspend(chg, (bool)suspend);
}

static int smblib_awake_vote_callback(struct votable *votable, void *data,
                        int awake, const char *client)
{
        struct smb_charger *chg = data;

        if (awake) {
                pm_stay_awake(chg->dev);
        } else {
                pm_relax(chg->dev);
        }

        return 0;
}

static int smblib_chg_disable_vote_callback(struct votable *votable, void *data,
                        int chg_disable, const char *client)
{
        struct smb_charger *chg = data;
        int rc;

        rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
                                 CHARGING_ENABLE_CMD_BIT,
                                 chg_disable ? 0 : CHARGING_ENABLE_CMD_BIT);
        if (rc < 0) {
                smblib_err(chg, "Couldn't %s charging rc=%d\n",
                        chg_disable ? "disable" : "enable", rc);
                return rc;
        }

        return 0;
}

static int smblib_usb_irq_enable_vote_callback(struct votable *votable,
                                void *data, int enable, const char *client)
{
        struct smb_charger *chg = data;

        if (!chg->irq_info[INPUT_CURRENT_LIMITING_IRQ].irq ||
                                !chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq) {
                return 0;
        }

        if (enable) {
                enable_irq(chg->irq_info[INPUT_CURRENT_LIMITING_IRQ].irq);
                enable_irq(chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq);
        } else {
                disable_irq_nosync(
                        chg->irq_info[INPUT_CURRENT_LIMITING_IRQ].irq);
                disable_irq_nosync(chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq);
        }

        return 0;
}

/*******************
 * VCONN REGULATOR *
 * *****************/

int smblib_vconn_regulator_enable(struct regulator_dev *rdev)
{
        int rc = 0;
        u8 stat, orientation;

        struct smb_charger *chg = rdev_get_drvdata(rdev);

        smblib_dbg(chg, PR_OTG, "enabling VCONN\n");
        
        rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_STATUS_4 rc=%d\n", rc);
                return rc;
        }
        
        /* VCONN orientation is opposite to that of CC */
        orientation =
                stat & TYPEC_CCOUT_VALUE_BIT ? 0 : VCONN_EN_ORIENTATION_BIT;
        rc = smblib_masked_write(chg, TYPE_C_VCONN_CONTROL_REG,
                                VCONN_EN_VALUE_BIT | VCONN_EN_ORIENTATION_BIT,
                                VCONN_EN_VALUE_BIT | orientation);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_CCOUT_CONTROL_REG rc=%d\n",
                        rc);
                return rc;
        }
        
        return 0;
}

int smblib_vconn_regulator_disable(struct regulator_dev *rdev)
{
        struct smb_charger *chg = rdev_get_drvdata(rdev);
        int rc = 0;

        smblib_dbg(chg, PR_OTG, "disabling VCONN\n");
        rc = smblib_masked_write(chg, TYPE_C_VCONN_CONTROL_REG,
                                 VCONN_EN_VALUE_BIT, 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't disable vconn regulator rc=%d\n", rc);
        }

        return 0;
}

int smblib_vconn_regulator_is_enabled(struct regulator_dev *rdev)
{
        struct smb_charger *chg = rdev_get_drvdata(rdev);
        int rc;
        u8 cmd;

        rc = smblib_read(chg, TYPE_C_VCONN_CONTROL_REG, &cmd);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_INTRPT_ENB_SOFTWARE_CTRL rc=%d\n",
                        rc);
                return rc;
        }

        return (cmd & VCONN_EN_VALUE_BIT) ? 1 : 0;
}

/*****************
 * OTG REGULATOR *
 *****************/

int smblib_vbus_regulator_enable(struct regulator_dev *rdev)
{
        struct smb_charger *chg = rdev_get_drvdata(rdev);
        int rc;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/08/29, sjc Add for using gpio as OTG ID */
        struct oppo_chg_chip *chip = g_oppo_chip;

        if (chip && oppo_usbid_check_is_gpio(chip) && chip->chg_ops->check_chrdet_status()) {
                return -EINVAL;
        }
#endif
        smblib_dbg(chg, PR_OTG, "enabling OTG\n");

        rc = smblib_masked_write(chg, DCDC_CMD_OTG_REG, OTG_EN_BIT, OTG_EN_BIT);
        if (rc < 0) {
                smblib_err(chg, "Couldn't enable OTG rc=%d\n", rc);
                return rc;
        }

        return 0;
}

int smblib_vbus_regulator_disable(struct regulator_dev *rdev)
{
        struct smb_charger *chg = rdev_get_drvdata(rdev);
        int rc;

        smblib_dbg(chg, PR_OTG, "disabling OTG\n");

        rc = smblib_masked_write(chg, DCDC_CMD_OTG_REG, OTG_EN_BIT, 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't disable OTG regulator rc=%d\n", rc);
                return rc;
        }

        return 0;
}

int smblib_vbus_regulator_is_enabled(struct regulator_dev *rdev)
{
        struct smb_charger *chg = rdev_get_drvdata(rdev);
        int rc = 0;
        u8 cmd;

        rc = smblib_read(chg, DCDC_CMD_OTG_REG, &cmd);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read CMD_OTG rc=%d", rc);
                return rc;
        }

        return (cmd & OTG_EN_BIT) ? 1 : 0;
}

/********************
 * BATT PSY GETTERS *
 ********************/

int smblib_get_prop_input_suspend(struct smb_charger *chg,
                                  union power_supply_propval *val)
{
        val->intval
                = (get_client_vote(chg->usb_icl_votable, USER_VOTER) == 0)
                 && get_client_vote(chg->dc_suspend_votable, USER_VOTER);
        return 0;
}

int smblib_get_prop_batt_present(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, BATIF_BASE + INT_RT_STS_OFFSET, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATIF_INT_RT_STS rc=%d\n", rc);
                return rc;
        }

        val->intval = !(stat & (BAT_THERM_OR_ID_MISSING_RT_STS_BIT
                                        | BAT_TERMINAL_MISSING_RT_STS_BIT));

        return rc;
}

int smblib_get_prop_batt_capacity(struct smb_charger *chg,
                                  union power_supply_propval *val)
{
        int rc = -EINVAL;

        if (chg->fake_capacity >= 0) {
                val->intval = chg->fake_capacity;
                return 0;
        }

        if (chg->bms_psy) {
                rc = power_supply_get_property(chg->bms_psy,
                                POWER_SUPPLY_PROP_CAPACITY, val);
        }
        return rc;
}

int smblib_get_prop_batt_status(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        union power_supply_propval pval = {0, };
        bool usb_online, dc_online;
        u8 stat;
        int rc;

        rc = smblib_get_prop_usb_online(chg, &pval);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get usb online property rc=%d\n",
                        rc);
                return rc;
        }
        usb_online = (bool)pval.intval;

        rc = smblib_get_prop_dc_online(chg, &pval);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get dc online property rc=%d\n",
                        rc);
                return rc;
        }
        dc_online = (bool)pval.intval;

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
                        rc);
                return rc;
        }
        stat = stat & BATTERY_CHARGER_STATUS_MASK;

        if (!usb_online && !dc_online) {
                switch (stat) {
                case TERMINATE_CHARGE:
                case INHIBIT_CHARGE:
                        val->intval = POWER_SUPPLY_STATUS_FULL;
                        break;
                default:
                        val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
                        break;
                }
                return rc;
        }

        switch (stat) {
        case TRICKLE_CHARGE:
        case PRE_CHARGE:
        case FULLON_CHARGE:
        case TAPER_CHARGE:
                val->intval = POWER_SUPPLY_STATUS_CHARGING;
                break;
        case TERMINATE_CHARGE:
        case INHIBIT_CHARGE:
                val->intval = POWER_SUPPLY_STATUS_FULL;
                break;
        case DISABLE_CHARGE:
        case PAUSE_CHARGE:
                val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
                break;
        default:
                val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
                break;
        }

        if (val->intval != POWER_SUPPLY_STATUS_CHARGING) {
                return 0;
        }

        if (!usb_online && dc_online
                && chg->fake_batt_status == POWER_SUPPLY_STATUS_FULL) {
                val->intval = POWER_SUPPLY_STATUS_FULL;
                return 0;
        }

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_5_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
                                rc);
                        return rc;
        }

        stat &= ENABLE_TRICKLE_BIT | ENABLE_PRE_CHARGING_BIT |
                                                ENABLE_FULLON_MODE_BIT;

        if (!stat) {
                val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
        }

        return 0;
}

int smblib_get_prop_batt_charge_type(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
                        rc);
                return rc;
        }

        switch (stat & BATTERY_CHARGER_STATUS_MASK) {
        case TRICKLE_CHARGE:
        case PRE_CHARGE:
                val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
                break;
        case FULLON_CHARGE:
                val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
                break;
        case TAPER_CHARGE:
                val->intval = POWER_SUPPLY_CHARGE_TYPE_TAPER;
                break;
        default:
                val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
        }

        return rc;
}

int smblib_get_prop_batt_health(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        union power_supply_propval pval;
        int rc;
        int effective_fv_uv;
        u8 stat;

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_2_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
                        rc);
                return rc;
        }
        smblib_dbg(chg, PR_REGISTER, "BATTERY_CHARGER_STATUS_2 = 0x%02x\n",
                   stat);

        if (stat & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
                rc = smblib_get_prop_batt_voltage_now(chg, &pval);
                if (!rc) {
                        /*
                         * If Vbatt is within 40mV above Vfloat, then don't
                         * treat it as overvoltage.
                         */
                        effective_fv_uv = get_effective_result(chg->fv_votable);
                        if (pval.intval >= effective_fv_uv + 40000) {
                                val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
                                smblib_err(chg, "battery over-voltage vbat_fg = %duV, fv = %duV\n",
                                                pval.intval, effective_fv_uv);
                                goto done;
                        }
                }
        }

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_7_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
                        rc);
                return rc;
        }
        if (stat & BAT_TEMP_STATUS_TOO_COLD_BIT) {
                val->intval = POWER_SUPPLY_HEALTH_COLD;
        } else if (stat & BAT_TEMP_STATUS_TOO_HOT_BIT) {
                val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
        } else if (stat & BAT_TEMP_STATUS_COLD_SOFT_BIT) {
                val->intval = POWER_SUPPLY_HEALTH_COOL;
        } else if (stat & BAT_TEMP_STATUS_HOT_SOFT_BIT) {
                val->intval = POWER_SUPPLY_HEALTH_WARM;
        } else {
                val->intval = POWER_SUPPLY_HEALTH_GOOD;
        }

done:
        return rc;
}

int smblib_get_prop_system_temp_level(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        val->intval = chg->system_temp_level;
        return 0;
}

int smblib_get_prop_system_temp_level_max(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        val->intval = chg->thermal_levels;
        return 0;
}

int smblib_get_prop_input_current_limited(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        u8 stat;
        int rc;

        if (chg->fake_input_current_limited >= 0) {
                val->intval = chg->fake_input_current_limited;
                return 0;
        }

        rc = smblib_read(chg, AICL_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read AICL_STATUS rc=%d\n", rc);
                return rc;
        }
        val->intval = (stat & SOFT_ILIMIT_BIT) || chg->is_hdc;
        return 0;
}

int smblib_get_prop_batt_voltage_now(struct smb_charger *chg,
                                     union power_supply_propval *val)
{
        int rc;

        if (!chg->bms_psy) {
                return -EINVAL;
        }

        rc = power_supply_get_property(chg->bms_psy,
                                       POWER_SUPPLY_PROP_VOLTAGE_NOW, val);
        return rc;
}

int smblib_get_prop_batt_current_now(struct smb_charger *chg,
                                     union power_supply_propval *val)
{
        int rc;

        if (!chg->bms_psy) {
                return -EINVAL;
        }

        rc = power_supply_get_property(chg->bms_psy,
                                       POWER_SUPPLY_PROP_CURRENT_NOW, val);
        return rc;
}

int smblib_get_prop_batt_temp(struct smb_charger *chg,
                              union power_supply_propval *val)
{
        int rc;

        if (!chg->bms_psy) {
                return -EINVAL;
        }

        rc = power_supply_get_property(chg->bms_psy,
                                       POWER_SUPPLY_PROP_TEMP, val);
        return rc;
}

int smblib_get_prop_batt_charge_done(struct smb_charger *chg,
                                        union power_supply_propval *val)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
                        rc);
                return rc;
        }

        stat = stat & BATTERY_CHARGER_STATUS_MASK;
        val->intval = (stat == TERMINATE_CHARGE);
        return 0;
}

int smblib_get_prop_batt_charge_counter(struct smb_charger *chg,
                                     union power_supply_propval *val)
{
        int rc;

        if (!chg->bms_psy) {
                return -EINVAL;
        }

        rc = power_supply_get_property(chg->bms_psy,
                                       POWER_SUPPLY_PROP_CHARGE_COUNTER, val);
        return rc;
}

/***********************
 * BATTERY PSY SETTERS *
 ***********************/

int smblib_set_prop_input_suspend(struct smb_charger *chg,
                                  const union power_supply_propval *val)
{
        int rc;

        /* vote 0mA when suspended */
        rc = vote(chg->usb_icl_votable, USER_VOTER, (bool)val->intval, 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't vote to %s USB rc=%d\n",
                        (bool)val->intval ? "suspend" : "resume", rc);
                return rc;
        }

        rc = vote(chg->dc_suspend_votable, USER_VOTER, (bool)val->intval, 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't vote to %s DC rc=%d\n",
                        (bool)val->intval ? "suspend" : "resume", rc);
                return rc;
        }

        power_supply_changed(chg->batt_psy);
        return rc;
}

int smblib_set_prop_batt_capacity(struct smb_charger *chg,
                                  const union power_supply_propval *val)
{
        chg->fake_capacity = val->intval;

        power_supply_changed(chg->batt_psy);

        return 0;
}

int smblib_set_prop_batt_status(struct smb_charger *chg,
                                  const union power_supply_propval *val)
{
        /* Faking battery full */
        if (val->intval == POWER_SUPPLY_STATUS_FULL) {
                chg->fake_batt_status = val->intval;
        } else {
                chg->fake_batt_status = -EINVAL;
        }

        power_supply_changed(chg->batt_psy);

        return 0;
}

int smblib_set_prop_system_temp_level(struct smb_charger *chg,
                                const union power_supply_propval *val)
{
        if (val->intval < 0) {
                return -EINVAL;
        }

        if (chg->thermal_levels <= 0) {
                return -EINVAL;
        }

        if (val->intval > chg->thermal_levels) {
                return -EINVAL;
        }

        chg->system_temp_level = val->intval;
        /* disable parallel charge in case of system temp level */
        vote(chg->pl_disable_votable, THERMAL_DAEMON_VOTER,
                        chg->system_temp_level ? true : false, 0);

        if (chg->system_temp_level == chg->thermal_levels) {
                return vote(chg->chg_disable_votable,
                        THERMAL_DAEMON_VOTER, true, 0);
        }

        vote(chg->chg_disable_votable, THERMAL_DAEMON_VOTER, false, 0);
        if (chg->system_temp_level == 0) {
                return vote(chg->fcc_votable, THERMAL_DAEMON_VOTER, false, 0);
        }

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-19  avoid limit FCC */
        vote(chg->fcc_votable, THERMAL_DAEMON_VOTER, true,
                        chg->thermal_mitigation[chg->system_temp_level]);
#endif

        return 0;
}

int smblib_set_prop_input_current_limited(struct smb_charger *chg,
                                const union power_supply_propval *val)
{
        chg->fake_input_current_limited = val->intval;
        return 0;
}

int smblib_rerun_aicl(struct smb_charger *chg)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n",
                                                                rc);
                return rc;
        }

        /* USB is suspended so skip re-running AICL */
        if (stat & USBIN_SUSPEND_STS_BIT) {
                return rc;
        }

        smblib_dbg(chg, PR_MISC, "re-running AICL\n");

        rc = smblib_masked_write(chg, AICL_CMD_REG, RERUN_AICL_BIT,
                                RERUN_AICL_BIT);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write to AICL_CMD_REG rc=%d\n",
                                rc);
        }
        return 0;
}

static int smblib_dp_pulse(struct smb_charger *chg)
{
        int rc;

        /* QC 3.0 increment */
        rc = smblib_masked_write(chg, CMD_HVDCP_2_REG, SINGLE_INCREMENT_BIT,
                        SINGLE_INCREMENT_BIT);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write to CMD_HVDCP_2_REG rc=%d\n",
                                rc);
        }

        return rc;
}

static int smblib_dm_pulse(struct smb_charger *chg)
{
        int rc;

        /* QC 3.0 decrement */
        rc = smblib_masked_write(chg, CMD_HVDCP_2_REG, SINGLE_DECREMENT_BIT,
                        SINGLE_DECREMENT_BIT);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write to CMD_HVDCP_2_REG rc=%d\n",
                                rc);
        }

        return rc;
}

int smblib_force_vbus_voltage(struct smb_charger *chg, u8 val)
{
        int rc;

        rc = smblib_masked_write(chg, CMD_HVDCP_2_REG, val, val);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write to CMD_HVDCP_2_REG rc=%d\n",
                                rc);
        }

        return rc;
}

int smblib_dp_dm(struct smb_charger *chg, int val)
{
        int target_icl_ua, rc = 0;
        union power_supply_propval pval;

        switch (val) {
        case POWER_SUPPLY_DP_DM_DP_PULSE:
                rc = smblib_dp_pulse(chg);
                if (!rc)
                        chg->pulse_cnt++;
                smblib_dbg(chg, PR_PARALLEL, "DP_DM_DP_PULSE rc=%d cnt=%d\n",
                                rc, chg->pulse_cnt);
                break;
        case POWER_SUPPLY_DP_DM_DM_PULSE:
                rc = smblib_dm_pulse(chg);
                if (!rc && chg->pulse_cnt)
                        chg->pulse_cnt--;
                smblib_dbg(chg, PR_PARALLEL, "DP_DM_DM_PULSE rc=%d cnt=%d\n",
                                rc, chg->pulse_cnt);
                break;
        case POWER_SUPPLY_DP_DM_ICL_DOWN:
                target_icl_ua = get_effective_result(chg->usb_icl_votable);
                if (target_icl_ua < 0) {
                        /* no client vote, get the ICL from charger */
                        rc = power_supply_get_property(chg->usb_psy,
                                        POWER_SUPPLY_PROP_HW_CURRENT_MAX,
                                        &pval);
                        if (rc < 0) {
                                smblib_err(chg, "Couldn't get max curr rc=%d\n",
                                        rc);
                                return rc;
                        }
                        target_icl_ua = pval.intval;
                }

                /*
                 * Check if any other voter voted on USB_ICL in case of
                 * voter other than SW_QC3_VOTER reset and restart reduction
                 * again.
                 */
                if (target_icl_ua != get_client_vote(chg->usb_icl_votable,
                                                        SW_QC3_VOTER)) {
                        chg->usb_icl_delta_ua = 0;
                }

                chg->usb_icl_delta_ua += 100000;
                vote(chg->usb_icl_votable, SW_QC3_VOTER, true,
                                                target_icl_ua - 100000);
                smblib_dbg(chg, PR_PARALLEL, "ICL DOWN ICL=%d reduction=%d\n",
                                target_icl_ua, chg->usb_icl_delta_ua);
                break;
        case POWER_SUPPLY_DP_DM_FORCE_5V:
                rc = smblib_force_vbus_voltage(chg, FORCE_5V_BIT);
                if (rc < 0) {
                        pr_err("Failed to force 5V\n");
                }
                break;
        case POWER_SUPPLY_DP_DM_FORCE_9V:
                rc = smblib_force_vbus_voltage(chg, FORCE_9V_BIT);
                if (rc < 0) {
                        pr_err("Failed to force 9V\n");
                }
                break;
        case POWER_SUPPLY_DP_DM_FORCE_12V:
                rc = smblib_force_vbus_voltage(chg, FORCE_12V_BIT);
                if (rc < 0) {
                        pr_err("Failed to force 12V\n");
                }
                break;
        case POWER_SUPPLY_DP_DM_ICL_UP:
        default:
                break;
        }

        return rc;
}

int smblib_disable_hw_jeita(struct smb_charger *chg, bool disable)
{
        int rc;
        u8 mask;

        /*
         * Disable h/w base JEITA compensation if s/w JEITA is enabled
         */
        mask = JEITA_EN_COLD_SL_FCV_BIT
                | JEITA_EN_HOT_SL_FCV_BIT
                | JEITA_EN_HOT_SL_CCC_BIT
                | JEITA_EN_COLD_SL_CCC_BIT,
        rc = smblib_masked_write(chg, JEITA_EN_CFG_REG, mask,
                        disable ? 0 : mask);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure s/w jeita rc=%d\n",
                                rc);
                return rc;
        }
        return 0;
}

int smblib_configure_wdog(struct smb_charger *chg, bool enable)
{
        int rc;
        u8 val = 0;

        if (enable) {
                val = WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT;
        }

        /* enable WD BARK and enable it on plugin */
        rc = smblib_masked_write(chg, WD_CFG_REG,
                                WATCHDOG_TRIGGER_AFP_EN_BIT |
                                WDOG_TIMER_EN_ON_PLUGIN_BIT |
                                BARK_WDOG_INT_EN_BIT, val);
        if (rc < 0) {
                pr_err("Couldn't configue WD config rc=%d\n", rc);
                return rc;
        }

        return 0;
}

/*******************
 * DC PSY GETTERS *
 *******************/

int smblib_get_prop_dc_present(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, DCIN_BASE + INT_RT_STS_OFFSET, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read DCIN_RT_STS rc=%d\n", rc);
                return rc;
        }

        val->intval = (bool)(stat & DCIN_PLUGIN_RT_STS_BIT);
        return 0;
}

int smblib_get_prop_dc_online(struct smb_charger *chg,
                               union power_supply_propval *val)
{
        int rc = 0;
        u8 stat;

        if (get_client_vote(chg->dc_suspend_votable, USER_VOTER)) {
                val->intval = false;
                return rc;
        }

        rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n",
                        rc);
                return rc;
        }
        smblib_dbg(chg, PR_REGISTER, "POWER_PATH_STATUS = 0x%02x\n",
                   stat);

        val->intval = (stat & USE_DCIN_BIT) &&
                      (stat & VALID_INPUT_POWER_SOURCE_STS_BIT);

        return rc;
}

/*******************
 * USB PSY GETTERS *
 *******************/

int smblib_get_prop_usb_present(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read USBIN_RT_STS rc=%d\n", rc);
                return rc;
        }

        val->intval = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);
        return 0;
}

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/05, sjc Add for charging */
/*  when set USBIN_SUSPEND_BIT, use present instead of online */
        static bool usb_online_status = false;
#endif
int smblib_get_prop_usb_online(struct smb_charger *chg,
                               union power_supply_propval *val)
{
        int rc = 0;
        u8 stat;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/05, sjc Add for charging */
        if (usb_online_status == true) {
                rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
                if (rc < 0) {
                        chg_err("usb_online_status: Couldn't read USBIN_RT_STS rc=%d\n", rc);
                        return rc;
                }

                val->intval = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);
                return rc;
        }
#endif

        if (get_client_vote_locked(chg->usb_icl_votable, USER_VOTER) == 0) {
                val->intval = false;
                return rc;
        }

        rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n",
                        rc);
                return rc;
        }
        smblib_dbg(chg, PR_REGISTER, "POWER_PATH_STATUS = 0x%02x\n",
                   stat);


#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-07  avoid flash shoot cause USB connect off */
        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP
                        || chg->real_charger_type == POWER_SUPPLY_TYPE_USB) {
                val->intval = (stat & USE_USBIN_BIT);
                return rc;
        }
#endif

        val->intval = (stat & USE_USBIN_BIT) &&
                      (stat & VALID_INPUT_POWER_SOURCE_STS_BIT);

        return rc;
}

int smblib_get_prop_usb_voltage_max(struct smb_charger *chg,
                                    union power_supply_propval *val)
{
        switch (chg->real_charger_type) {
        case POWER_SUPPLY_TYPE_USB_HVDCP:
        case POWER_SUPPLY_TYPE_USB_HVDCP_3:
        case POWER_SUPPLY_TYPE_USB_PD:
                if (chg->smb_version == PMI632_SUBTYPE) {
                        val->intval = MICRO_9V;
                } else {
                        val->intval = MICRO_12V;
                }
                break;
        default:
                val->intval = MICRO_5V;
                break;
        }

        return 0;
}

int smblib_get_prop_typec_cc_orientation(struct smb_charger *chg,
                                         union power_supply_propval *val)
{
        int rc = 0;
        u8 stat;

        rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_STATUS_4 rc=%d\n", rc);
                return rc;
        }
        smblib_dbg(chg, PR_REGISTER, "TYPE_C_STATUS_4 = 0x%02x\n", stat);

        if (stat & CC_ATTACHED_BIT) {
                val->intval = (bool)(stat & CC_ORIENTATION_BIT) + 1;
        } else {
                val->intval = 0;
        }

        return rc;
}

static const char * const smblib_typec_mode_name[] = {
        [POWER_SUPPLY_TYPEC_NONE]                 = "NONE",
        [POWER_SUPPLY_TYPEC_SOURCE_DEFAULT]       = "SOURCE_DEFAULT",
        [POWER_SUPPLY_TYPEC_SOURCE_MEDIUM]        = "SOURCE_MEDIUM",
        [POWER_SUPPLY_TYPEC_SOURCE_HIGH]          = "SOURCE_HIGH",
        [POWER_SUPPLY_TYPEC_NON_COMPLIANT]        = "NON_COMPLIANT",
        [POWER_SUPPLY_TYPEC_SINK]                 = "SINK",
        [POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE]   = "SINK_POWERED_CABLE",
        [POWER_SUPPLY_TYPEC_SINK_DEBUG_ACCESSORY] = "SINK_DEBUG_ACCESSORY",
        [POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER]   = "SINK_AUDIO_ADAPTER",
        [POWER_SUPPLY_TYPEC_POWERED_CABLE_ONLY]   = "POWERED_CABLE_ONLY",
};

static int smblib_get_prop_ufp_mode(struct smb_charger *chg)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, TYPE_C_SNK_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_STATUS_1 rc=%d\n", rc);
                return POWER_SUPPLY_TYPEC_NONE;
        }
        smblib_dbg(chg, PR_REGISTER, "TYPE_C_STATUS_1 = 0x%02x\n", stat);

        switch (stat & DETECTED_SRC_TYPE_MASK) {
        case SNK_RP_STD_BIT:
                return POWER_SUPPLY_TYPEC_SOURCE_DEFAULT;
        case SNK_RP_1P5_BIT:
                return POWER_SUPPLY_TYPEC_SOURCE_MEDIUM;
        case SNK_RP_3P0_BIT:
                return POWER_SUPPLY_TYPEC_SOURCE_HIGH;
        default:
                break;
        }

        return POWER_SUPPLY_TYPEC_NONE;
}

static int smblib_get_prop_dfp_mode(struct smb_charger *chg)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, TYPE_C_SRC_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_SRC_STATUS_REG rc=%d\n",
                                rc);
                return POWER_SUPPLY_TYPEC_NONE;
        }
        smblib_dbg(chg, PR_REGISTER, "TYPE_C_SRC_STATUS_REG = 0x%02x\n", stat);

        switch (stat & DETECTED_SNK_TYPE_MASK) {
        case AUDIO_ACCESS_RA_RA_BIT:
                return POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER;
        case SRC_DEBUG_ACCESS_BIT:
                return POWER_SUPPLY_TYPEC_SINK_DEBUG_ACCESSORY;
        case SRC_RD_RA_VCONN_BIT:
                return POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE;
        case SRC_RD_OPEN_BIT:
                return POWER_SUPPLY_TYPEC_SINK;
        default:
                break;
        }

        return POWER_SUPPLY_TYPEC_NONE;
}

static int smblib_get_prop_typec_mode(struct smb_charger *chg)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_MISC_STATUS_REG rc=%d\n",
                                rc);
                return 0;
        }
        smblib_dbg(chg, PR_REGISTER, "TYPE_C_MISC_STATUS_REG = 0x%02x\n", stat);

        if (stat & SNK_SRC_MODE_BIT) {
                return smblib_get_prop_dfp_mode(chg);
        } else {
                return smblib_get_prop_ufp_mode(chg);
        }
}

int smblib_get_prop_typec_power_role(struct smb_charger *chg,
                                     union power_supply_propval *val)
{
        int rc = 0;
        u8 ctrl;

        rc = smblib_read(chg, TYPE_C_MODE_CFG_REG, &ctrl);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_MODE_CFG_REG rc=%d\n",
                        rc);
                return rc;
        }
        smblib_dbg(chg, PR_REGISTER, "TYPE_C_MODE_CFG_REG = 0x%02x\n",
                   ctrl);

        if (ctrl & TYPEC_DISABLE_CMD_BIT) {
                val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
                return rc;
        }

        switch (ctrl & (EN_SRC_ONLY_BIT | EN_SNK_ONLY_BIT)) {
        case 0:
                val->intval = POWER_SUPPLY_TYPEC_PR_DUAL;
                break;
        case EN_SRC_ONLY_BIT:
                val->intval = POWER_SUPPLY_TYPEC_PR_SOURCE;
                break;
        case EN_SNK_ONLY_BIT:
                val->intval = POWER_SUPPLY_TYPEC_PR_SINK;
                break;
        default:
                val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
                smblib_err(chg, "unsupported power role 0x%02lx\n",
                        ctrl & (EN_SRC_ONLY_BIT | EN_SNK_ONLY_BIT));
                return -EINVAL;
        }

        return rc;
}

int smblib_get_prop_input_current_settled(struct smb_charger *chg,
                                          union power_supply_propval *val)
{
        return smblib_get_charge_param(chg, &chg->param.icl_stat, &val->intval);
}

#define HVDCP3_STEP_UV  200000
int smblib_get_prop_input_voltage_settled(struct smb_charger *chg,
                                                union power_supply_propval *val)
{
        int rc, pulses;

        switch (chg->real_charger_type) {
        case POWER_SUPPLY_TYPE_USB_HVDCP_3:
                rc = smblib_get_pulse_cnt(chg, &pulses);
                if (rc < 0) {
                        smblib_err(chg,
                                "Couldn't read QC_PULSE_COUNT rc=%d\n", rc);
                        return 0;
                }
                val->intval = MICRO_5V + HVDCP3_STEP_UV * pulses;
                break;
        case POWER_SUPPLY_TYPE_USB_PD:
                val->intval = chg->voltage_min_uv;
                break;
        default:
                val->intval = MICRO_5V;
                break;
        }

        return 0;
}

int smblib_get_prop_pd_in_hard_reset(struct smb_charger *chg,
                               union power_supply_propval *val)
{
        val->intval = chg->pd_hard_reset;
        return 0;
}

int smblib_get_pe_start(struct smb_charger *chg,
                               union power_supply_propval *val)
{
        val->intval = chg->ok_to_pd;
        return 0;
}

int smblib_get_prop_die_health(struct smb_charger *chg,
                                                union power_supply_propval *val)
{
        int rc;
        u8 stat;

        rc = smblib_read(chg, TEMP_RANGE_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TEMP_RANGE_STATUS_REG rc=%d\n",
                                                                        rc);
                return rc;
        }

        /* TEMP_RANGE bits are mutually exclusive */
        switch (stat & TEMP_RANGE_MASK) {
        case TEMP_BELOW_RANGE_BIT:
                val->intval = POWER_SUPPLY_HEALTH_COOL;
                break;
        case TEMP_WITHIN_RANGE_BIT:
                val->intval = POWER_SUPPLY_HEALTH_WARM;
                break;
        case TEMP_ABOVE_RANGE_BIT:
                val->intval = POWER_SUPPLY_HEALTH_HOT;
                break;
        case ALERT_LEVEL_BIT:
                val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
                break;
        default:
                val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
        }

        return 0;
}

#define SDP_CURRENT_UA                  500000
#define CDP_CURRENT_UA                  1500000
#ifndef VENDOR_EDIT
// wenbin.liu@BSP.CHG.Basic, 2017/11/27 
// Delete for oppo dcp 2A 
#define DCP_CURRENT_UA                  1500000
#else
#define DCP_CURRENT_UA                  2000000
#endif
#define HVDCP_CURRENT_UA                3000000
#define TYPEC_DEFAULT_CURRENT_UA        900000
#define TYPEC_MEDIUM_CURRENT_UA         1500000
#define TYPEC_HIGH_CURRENT_UA           3000000
static int get_rp_based_dcp_current(struct smb_charger *chg, int typec_mode)
{
        int rp_ua;

        switch (typec_mode) {
        case POWER_SUPPLY_TYPEC_SOURCE_HIGH:
                rp_ua = TYPEC_HIGH_CURRENT_UA;
                break;
        case POWER_SUPPLY_TYPEC_SOURCE_MEDIUM:
        case POWER_SUPPLY_TYPEC_SOURCE_DEFAULT:
        /* fall through */
        default:
                rp_ua = DCP_CURRENT_UA;
        }

        return rp_ua;
}

/*******************
 * USB PSY SETTERS *
 * *****************/

int smblib_set_prop_pd_current_max(struct smb_charger *chg,
                                    const union power_supply_propval *val)
{
        int rc;

        if (chg->pd_active) {
                rc = vote(chg->usb_icl_votable, PD_VOTER, true, val->intval);
        } else {
                rc = -EPERM;
        }

        return rc;
}

static int smblib_handle_usb_current(struct smb_charger *chg,
                                        int usb_current)
{
        int rc = 0, rp_ua, typec_mode;
        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_FLOAT) {
                if (usb_current == -ETIMEDOUT) {
                        /*
                         * Valid FLOAT charger, report the current based
                         * of Rp
                         */
                        if (chg->connector_type ==
                                        POWER_SUPPLY_CONNECTOR_TYPEC) {
                                typec_mode = smblib_get_prop_typec_mode(chg);
                                rp_ua = get_rp_based_dcp_current(chg,
                                                                typec_mode);
                                rc = vote(chg->usb_icl_votable,
                                                DYNAMIC_RP_VOTER, true, rp_ua);
                                if (rc < 0) {
                                        pr_err("Couldn't vote ICL DYNAMIC_RP_VOTER rc=%d\n",
                                                        rc);
                                        return rc;
                                }
                        }
                        /* No specific handling required for micro-USB */
                } else {
                        /*
                         * FLOAT charger detected as SDP by USB driver,
                         * charge with the requested current and update the
                         * real_charger_type
                         */
                        chg->real_charger_type = POWER_SUPPLY_TYPE_USB;
                        rc = vote(chg->usb_icl_votable, USB_PSY_VOTER,
                                                        true, usb_current);
                        if (rc < 0) {
                                pr_err("Couldn't vote ICL USB_PSY_VOTER rc=%d\n",
                                                rc);
                                return rc;
                        }
                }
        } else {
                rc = vote(chg->usb_icl_votable, USB_PSY_VOTER,
                                        true, usb_current);
                if (rc < 0) {
                        pr_err("Couldn't vote ICL USB_PSY_VOTER rc=%d\n", rc);
                        return rc;
                }
        }

        rc = vote(chg->usb_icl_votable, DEFAULT_100MA_VOTER, false, 0);
        if (rc < 0) {
                pr_err("Couldn't unvote ICL DEFAULT_100MA_VOTER rc=%d\n", rc);
                return rc;
        }

        return 0;
}

int smblib_set_prop_sdp_current_max(struct smb_charger *chg,
                                    const union power_supply_propval *val)
{
        int rc = 0;

        if (!chg->pd_active) {
                rc = smblib_handle_usb_current(chg, val->intval);
        } else if (chg->system_suspend_supported) {
                if (val->intval <= USBIN_25MA) {
                        rc = vote(chg->usb_icl_votable,
                                PD_SUSPEND_SUPPORTED_VOTER, true, val->intval);
                } else {
                        rc = vote(chg->usb_icl_votable,
                                PD_SUSPEND_SUPPORTED_VOTER, false, 0);
                }
        }
        return rc;
}

int smblib_set_prop_boost_current(struct smb_charger *chg,
                                        const union power_supply_propval *val)
{
        int rc = 0;

        rc = smblib_set_charge_param(chg, &chg->param.freq_switcher,
                                val->intval <= chg->boost_threshold_ua ?
                                chg->chg_freq.freq_below_otg_threshold :
                                chg->chg_freq.freq_above_otg_threshold);
        if (rc < 0) {
                dev_err(chg->dev, "Error in setting freq_boost rc=%d\n", rc);
                return rc;
        }

        chg->boost_current_ua = val->intval;
        return rc;
}

int smblib_set_prop_typec_power_role(struct smb_charger *chg,
                                     const union power_supply_propval *val)
{
        int rc = 0;
        u8 power_role;

        if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                return 0;
        }

        switch (val->intval) {
        case POWER_SUPPLY_TYPEC_PR_NONE:
                power_role = TYPEC_DISABLE_CMD_BIT;
                break;
        case POWER_SUPPLY_TYPEC_PR_DUAL:
                power_role = 0;
                break;
        case POWER_SUPPLY_TYPEC_PR_SINK:
                power_role = EN_SNK_ONLY_BIT;
                break;
        case POWER_SUPPLY_TYPEC_PR_SOURCE:
                power_role = EN_SRC_ONLY_BIT;
                break;
        default:
                smblib_err(chg, "power role %d not supported\n", val->intval);
                return -EINVAL;
        }

        rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
                                 TYPEC_POWER_ROLE_CMD_MASK, power_role);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write 0x%02x to TYPE_C_INTRPT_ENB_SOFTWARE_CTRL rc=%d\n",
                        power_role, rc);
                return rc;
        }

        return rc;
}

int smblib_set_prop_pd_voltage_min(struct smb_charger *chg,
                                    const union power_supply_propval *val)
{
        int rc, min_uv;

        min_uv = min(val->intval, chg->voltage_max_uv);
        rc = smblib_set_usb_pd_allowed_voltage(chg, min_uv,
                                               chg->voltage_max_uv);
        if (rc < 0) {
                smblib_err(chg, "invalid max voltage %duV rc=%d\n",
                        val->intval, rc);
                return rc;
        }

        chg->voltage_min_uv = min_uv;
        power_supply_changed(chg->usb_main_psy);

        return rc;
}

int smblib_set_prop_pd_voltage_max(struct smb_charger *chg,
                                    const union power_supply_propval *val)
{
        int rc, max_uv;

        max_uv = max(val->intval, chg->voltage_min_uv);
        rc = smblib_set_usb_pd_allowed_voltage(chg, chg->voltage_min_uv,
                                               max_uv);
        if (rc < 0) {
                smblib_err(chg, "invalid min voltage %duV rc=%d\n",
                        val->intval, rc);
                return rc;
        }

        chg->voltage_max_uv = max_uv;
        power_supply_changed(chg->usb_main_psy);

        return rc;
}

int smblib_set_prop_pd_active(struct smb_charger *chg,
                                const union power_supply_propval *val)
{
        int rc = 0;

        chg->pd_active = val->intval;

        if (chg->pd_active) {
                vote(chg->usb_irq_enable_votable, PD_VOTER, true, 0);

                /*
                 * Enforce 500mA for PD until the real vote comes in later.
                 * It is guaranteed that pd_active is set prior to
                 * pd_current_max
                 */
                vote(chg->usb_icl_votable, PD_VOTER, true, USBIN_500MA);
        } else {
                vote(chg->usb_icl_votable, PD_VOTER, false, 0);
                vote(chg->usb_irq_enable_votable, PD_VOTER, false, 0);

                /* PD hard resets failed, rerun apsd */
                if (chg->ok_to_pd) {
                        chg->ok_to_pd = false;
                        rc = smblib_configure_hvdcp_apsd(chg, true);
                        if (rc < 0) {
                                dev_err(chg->dev,
                                        "Couldn't enable APSD rc=%d\n", rc);
                                return rc;
                        }
                        smblib_rerun_apsd_if_required(chg);
                }
        }

        smblib_update_usb_type(chg);
        power_supply_changed(chg->usb_psy);
        return rc;
}

int smblib_set_prop_ship_mode(struct smb_charger *chg,
                                const union power_supply_propval *val)
{
        int rc;

        smblib_dbg(chg, PR_MISC, "Set ship mode: %d!!\n", !!val->intval);

        rc = smblib_masked_write(chg, SHIP_MODE_REG, SHIP_MODE_EN_BIT,
                        !!val->intval ? SHIP_MODE_EN_BIT : 0);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't %s ship mode, rc=%d\n",
                                !!val->intval ? "enable" : "disable", rc);
        }

        return rc;
}

int smblib_set_prop_pd_in_hard_reset(struct smb_charger *chg,
                                const union power_supply_propval *val)
{
        int rc = 0;

        if (chg->pd_hard_reset == val->intval) {
                return rc;
        }

        chg->pd_hard_reset = val->intval;
        rc = smblib_masked_write(chg, TYPE_C_EXIT_STATE_CFG_REG,
                        EXIT_SNK_BASED_ON_CC_BIT,
                        (chg->pd_hard_reset) ? EXIT_SNK_BASED_ON_CC_BIT : 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't set EXIT_SNK_BASED_ON_CC rc=%d\n",
                                rc);
        }

        return rc;
}

static int smblib_recover_from_soft_jeita(struct smb_charger *chg)
{
        u8 stat1, stat7;
        int rc;

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat1);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
                                rc);
                return rc;
        }

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_7_REG, &stat7);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n",
                                rc);
                return rc;
        }

        if ((chg->jeita_status && !(stat7 & BAT_TEMP_STATUS_SOFT_LIMIT_MASK) &&
                ((stat1 & BATTERY_CHARGER_STATUS_MASK) == TERMINATE_CHARGE))) {
                /*
                 * We are moving from JEITA soft -> Normal and charging
                 * is terminated
                 */
                rc = smblib_write(chg, CHARGING_ENABLE_CMD_REG, 0);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't disable charging rc=%d\n",
                                                rc);
                        return rc;
                }
                rc = smblib_write(chg, CHARGING_ENABLE_CMD_REG,
                                                CHARGING_ENABLE_CMD_BIT);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't enable charging rc=%d\n",
                                                rc);
                        return rc;
                }
        }

        chg->jeita_status = stat7 & BAT_TEMP_STATUS_SOFT_LIMIT_MASK;

        return 0;
}

/************************
 * USB MAIN PSY GETTERS *
 ************************/
 
int smblib_get_prop_fcc_delta(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        int rc, jeita_cc_delta_ua = 0;

        if (chg->sw_jeita_enabled) {
                val->intval = 0;
                return 0;
        }

        rc = smblib_get_jeita_cc_delta(chg, &jeita_cc_delta_ua);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get jeita cc delta rc=%d\n", rc);
                jeita_cc_delta_ua = 0;
        }

        val->intval = jeita_cc_delta_ua;
        return 0;
}

/************************
 * USB MAIN PSY SETTERS *
 ************************/
 
int smblib_get_charge_current(struct smb_charger *chg,
                                int *total_current_ua)
{
        const struct apsd_result *apsd_result = smblib_get_apsd_result(chg);
        union power_supply_propval val = {0, };
        int rc = 0, typec_source_rd, current_ua;
        bool non_compliant;
        u8 stat;

        if (chg->pd_active) {
                *total_current_ua =
                        get_client_vote_locked(chg->usb_icl_votable, PD_VOTER);
                return rc;
        }

        rc = smblib_read(chg, LEGACY_CABLE_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_STATUS_5 rc=%d\n", rc);
                return rc;
        }
        non_compliant = stat & TYPEC_NONCOMP_LEGACY_CABLE_STATUS_BIT;

        /* get settled ICL */
        rc = smblib_get_prop_input_current_settled(chg, &val);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get settled ICL rc=%d\n", rc);
                return rc;
        }

        typec_source_rd = smblib_get_prop_ufp_mode(chg);

        /* QC 2.0/3.0 adapter */
        if (apsd_result->bit & (QC_3P0_BIT | QC_2P0_BIT)) {
                *total_current_ua = HVDCP_CURRENT_UA;
                return 0;
        }

        if (non_compliant) {
                switch (apsd_result->bit) {
                case CDP_CHARGER_BIT:
                        current_ua = CDP_CURRENT_UA;
                        break;
                case DCP_CHARGER_BIT:
                case OCP_CHARGER_BIT:
                case FLOAT_CHARGER_BIT:
                        current_ua = DCP_CURRENT_UA;
                        break;
                default:
                        current_ua = 0;
                        break;
                }

                *total_current_ua = max(current_ua, val.intval);
                return 0;
        }

        switch (typec_source_rd) {
        case POWER_SUPPLY_TYPEC_SOURCE_DEFAULT:
                switch (apsd_result->bit) {
                case CDP_CHARGER_BIT:
                        current_ua = CDP_CURRENT_UA;
                        break;
                case DCP_CHARGER_BIT:
                case OCP_CHARGER_BIT:
                case FLOAT_CHARGER_BIT:
                        current_ua = chg->default_icl_ua;
                        break;
                default:
                        current_ua = 0;
                        break;
                }
                break;
        case POWER_SUPPLY_TYPEC_SOURCE_MEDIUM:
                current_ua = TYPEC_MEDIUM_CURRENT_UA;
                break;
        case POWER_SUPPLY_TYPEC_SOURCE_HIGH:
                current_ua = TYPEC_HIGH_CURRENT_UA;
                break;
        case POWER_SUPPLY_TYPEC_NON_COMPLIANT:
        case POWER_SUPPLY_TYPEC_NONE:
        default:
                current_ua = 0;
                break;
        }

        *total_current_ua = max(current_ua, val.intval);
        return 0;
}

/**********************
 * INTERRUPT HANDLERS *
 **********************/

irqreturn_t default_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);
        return IRQ_HANDLED;
}

irqreturn_t chg_state_change_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
        u8 stat;
        int rc;

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);

        rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n",
                                rc);
                return IRQ_HANDLED;
        }

        stat = stat & BATTERY_CHARGER_STATUS_MASK;
        power_supply_changed(chg->batt_psy);
        return IRQ_HANDLED;
}

irqreturn_t batt_temp_changed_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
        int rc;

        rc = smblib_recover_from_soft_jeita(chg);
        if (rc < 0) {
                smblib_err(chg, "Couldn't recover chg from soft jeita rc=%d\n",
                                rc);
                return IRQ_HANDLED;
        }

        rerun_election(chg->fcc_votable);
        power_supply_changed(chg->batt_psy);
        return IRQ_HANDLED;
}

irqreturn_t batt_psy_changed_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);
        power_supply_changed(chg->batt_psy);
        return IRQ_HANDLED;
}

irqreturn_t usbin_uv_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
        struct storm_watch *wdata;

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);
        if (!chg->irq_info[SWITCHER_POWER_OK_IRQ].irq_data) {
                return IRQ_HANDLED;
        }

        wdata = &chg->irq_info[SWITCHER_POWER_OK_IRQ].irq_data->storm_data;
        reset_storm_count(wdata);
        return IRQ_HANDLED;
}

#define USB_WEAK_INPUT_UA       1400000
#define ICL_CHANGE_DELAY_MS     1000
irqreturn_t icl_change_irq_handler(int irq, void *data)
{
        u8 stat;
        int rc, settled_ua, delay = ICL_CHANGE_DELAY_MS;
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;

        if (chg->mode == PARALLEL_MASTER) {
                rc = smblib_read(chg, AICL_STATUS_REG, &stat);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't read AICL_STATUS rc=%d\n",
                                        rc);
                        return IRQ_HANDLED;
                }

                rc = smblib_get_charge_param(chg, &chg->param.icl_stat,
                                        &settled_ua);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't get ICL status rc=%d\n", rc);
                        return IRQ_HANDLED;
                }

                /* If AICL settled then schedule work now */
                if (settled_ua == get_effective_result(chg->usb_icl_votable)) {
                        delay = 0;
                }

                cancel_delayed_work_sync(&chg->icl_change_work);
                schedule_delayed_work(&chg->icl_change_work,
                                                msecs_to_jiffies(delay));
        }

        return IRQ_HANDLED;
}

static void smblib_micro_usb_plugin(struct smb_charger *chg, bool vbus_rising)
{
        if (!vbus_rising) {
                smblib_update_usb_type(chg);
                smblib_notify_device_mode(chg, false);
                smblib_uusb_removal(chg);
        }
}

void smblib_usb_plugin_hard_reset_locked(struct smb_charger *chg)
{
        int rc;
        u8 stat;
        bool vbus_rising;
        struct smb_irq_data *data;
        struct storm_watch *wdata;

        rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read USB_INT_RT_STS rc=%d\n", rc);
                return;
        }

        vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);


#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25  debug */
        chg_debug("*****************usb_plugin: [%d]*****************\n", vbus_rising);
#endif

        if (!vbus_rising) {
#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-04-25  POPO_CHARGE */
                oppo_vooc_reset_fastchg_after_usbout();
                if (oppo_vooc_get_fastchg_started() == false && g_oppo_chip) {
                        smbchg_set_chargerid_switch_val(g_oppo_chip, 0);
                        g_oppo_chip->chargerid_volt = 0;
                        g_oppo_chip->chargerid_volt_got = false;
                        g_oppo_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
                        oppo_chg_wake_update_work();
                }
                chg->pre_current_ma = -1;
#endif

                if (chg->wa_flags & BOOST_BACK_WA) {
                        data = chg->irq_info[SWITCHER_POWER_OK_IRQ].irq_data;
                        if (data) {
                                wdata = &data->storm_data;
                                update_storm_count(wdata,
                                                WEAK_CHG_STORM_COUNT);
                                vote(chg->usb_icl_votable, BOOST_BACK_VOTER,
                                                false, 0);
                                vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
                                                false, 0);
                        }
                }
        }

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/25, sjc Add for charging */
        if (vbus_rising) {
                cancel_delayed_work_sync(&chg->chg_monitor_work);
                schedule_delayed_work(&chg->chg_monitor_work, OPPO_CHG_MONITOR_INTERVAL);
        } else {
                cancel_delayed_work_sync(&chg->chg_monitor_work);
        }
#endif

#ifdef VENDOR_EDIT
/* MingQiang.Guo@BSP.TP.Init, 2018/04/30, Add for notify touchpanel status */
        if (vbus_rising) {
                switch_usb_state(1);
        } else {
                switch_usb_state(0);
        }
#endif

        power_supply_changed(chg->usb_psy);
        smblib_dbg(chg, PR_INTERRUPT, "IRQ: usbin-plugin %s\n",
                                vbus_rising ? "attached" : "detached");
}

#define PL_DELAY_MS     30000
void smblib_usb_plugin_locked(struct smb_charger *chg)
{
        int rc;
        u8 stat;
        bool vbus_rising;
        struct smb_irq_data *data;
        struct storm_watch *wdata;

        rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read USB_INT_RT_STS rc=%d\n", rc);
                return;
        }

        vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);

#ifndef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-28  avoid flash current ripple when flash work */
        smblib_set_opt_switcher_freq(chg, vbus_rising ? chg->chg_freq.freq_5V :
                                                chg->chg_freq.freq_removal);
#else
        if (g_smb_chip->flash_active == false || vbus_rising == false) {
                smblib_set_opt_switcher_freq(chg, vbus_rising ? chg->chg_freq.freq_5V :
                          chg->chg_freq.freq_removal);  
        }
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25  debug */
        chg_debug("*****************usb_plugin: [%d]****************\n", vbus_rising);
#endif

        if (vbus_rising) {
                rc = smblib_request_dpdm(chg, true);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't to enable DPDM rc=%d\n", rc);
                }

                /* Schedule work to enable parallel charger */
                vote(chg->awake_votable, PL_DELAY_VOTER, true, 0);
                schedule_delayed_work(&chg->pl_enable_work,
                                        msecs_to_jiffies(PL_DELAY_MS));
        } else {
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging */
                oppo_vooc_reset_fastchg_after_usbout();
                if (oppo_vooc_get_fastchg_started() == false && g_oppo_chip) {
                        smbchg_set_chargerid_switch_val(g_oppo_chip, 0);
                        g_oppo_chip->chargerid_volt = 0;
                        g_oppo_chip->chargerid_volt_got = false;
                        g_oppo_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
                        oppo_chg_wake_update_work();
                }
                chg->pre_current_ma = -1;
#endif
                if (chg->wa_flags & BOOST_BACK_WA) {
                        data = chg->irq_info[SWITCHER_POWER_OK_IRQ].irq_data;
                        if (data) {
                                wdata = &data->storm_data;
                                update_storm_count(wdata,
                                                WEAK_CHG_STORM_COUNT);
                                vote(chg->usb_icl_votable, BOOST_BACK_VOTER,
                                                false, 0);
                                vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
                                                false, 0);
                        }
                }

                rc = smblib_request_dpdm(chg, false);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't disable DPDM rc=%d\n", rc);
                }
        }

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/25, sjc Add for charging */
        if (vbus_rising) {
                cancel_delayed_work_sync(&chg->chg_monitor_work);
                schedule_delayed_work(&chg->chg_monitor_work, OPPO_CHG_MONITOR_INTERVAL);
        } else {
                cancel_delayed_work_sync(&chg->chg_monitor_work);
        }
#endif

#ifdef VENDOR_EDIT
/* Mingqiang.Guo@BSP.TP.Init, 2018/04/30, Add for notify touchpanel status */
        if (vbus_rising) {
                switch_usb_state(1);
        } else {
                switch_usb_state(0);
        }
#endif

        if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                smblib_micro_usb_plugin(chg, vbus_rising);
        }

        power_supply_changed(chg->usb_psy);
        smblib_dbg(chg, PR_INTERRUPT, "IRQ: usbin-plugin %s\n",
                                vbus_rising ? "attached" : "detached");
}

irqreturn_t usb_plugin_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;

        if (chg->pd_hard_reset) {
                smblib_usb_plugin_hard_reset_locked(chg);
        } else {
                smblib_usb_plugin_locked(chg);
        }

        return IRQ_HANDLED;
}

static void smblib_handle_slow_plugin_timeout(struct smb_charger *chg,
                                              bool rising)
{
        smblib_dbg(chg, PR_INTERRUPT, "IRQ: slow-plugin-timeout %s\n",
                   rising ? "rising" : "falling");
}

static void smblib_handle_sdp_enumeration_done(struct smb_charger *chg,
                                               bool rising)
{
        smblib_dbg(chg, PR_INTERRUPT, "IRQ: sdp-enumeration-done %s\n",
                   rising ? "rising" : "falling");
}

#define QC3_PULSES_FOR_6V       5
#define QC3_PULSES_FOR_9V       20
#define QC3_PULSES_FOR_12V      35
static void smblib_hvdcp_adaptive_voltage_change(struct smb_charger *chg)
{
        int rc;
        u8 stat;
        int pulses;

        power_supply_changed(chg->usb_main_psy);
        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP) {
                rc = smblib_read(chg, QC_CHANGE_STATUS_REG, &stat);
                if (rc < 0) {
                        smblib_err(chg,
                                "Couldn't read QC_CHANGE_STATUS rc=%d\n", rc);
                        return;
                }

                switch (stat & QC_2P0_STATUS_MASK) {
                case QC_5V_BIT:
                        smblib_set_opt_switcher_freq(chg,
                                        chg->chg_freq.freq_5V);
                        break;
                case QC_9V_BIT:
                        smblib_set_opt_switcher_freq(chg,
                                        chg->chg_freq.freq_9V);
                        break;
                case QC_12V_BIT:
                        smblib_set_opt_switcher_freq(chg,
                                        chg->chg_freq.freq_12V);
                        break;
                default:
                        smblib_set_opt_switcher_freq(chg,
                                        chg->chg_freq.freq_removal);
                        break;
                }
        }

        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP_3) {
                rc = smblib_get_pulse_cnt(chg, &pulses);
                if (rc < 0) {
                        smblib_err(chg,
                                "Couldn't read QC_PULSE_COUNT rc=%d\n", rc);
                        return;
                }

                if (pulses < QC3_PULSES_FOR_6V) {
                        smblib_set_opt_switcher_freq(chg,
                                chg->chg_freq.freq_5V);
                } else if (pulses < QC3_PULSES_FOR_9V) {
                        smblib_set_opt_switcher_freq(chg,
                                chg->chg_freq.freq_6V_8V);
                } else if (pulses < QC3_PULSES_FOR_12V) {
                        smblib_set_opt_switcher_freq(chg,
                                chg->chg_freq.freq_9V);
                } else {
                        smblib_set_opt_switcher_freq(chg,
                                chg->chg_freq.freq_12V);
                }
        }
}

/* triggers when HVDCP 3.0 authentication has finished */
static void smblib_handle_hvdcp_3p0_auth_done(struct smb_charger *chg,
                                              bool rising)
{
        const struct apsd_result *apsd_result;

        if (!rising) {
                return;
        }

        if (chg->mode == PARALLEL_MASTER) {
                vote(chg->pl_enable_votable_indirect, USBIN_V_VOTER, true, 0);
        }

        /* the APSD done handler will set the USB supply type */
        apsd_result = smblib_get_apsd_result(chg);
        smblib_dbg(chg, PR_INTERRUPT, "IRQ: hvdcp-3p0-auth-done rising; %s detected\n",
                   apsd_result->name);
}

static void smblib_handle_hvdcp_check_timeout(struct smb_charger *chg,
                                              bool rising, bool qc_charger)
{
        if (rising) {
                /* enable HDC and ICL irq for QC2/3 charger */
                if (qc_charger) {
                        vote(chg->usb_irq_enable_votable, QC_VOTER, true, 0);
                } else {
                /*
                 * HVDCP detection timeout done
                 * If adapter is not QC2.0/QC3.0 - it is a plain old DCP.
                 * enforce DCP ICL if specified
                 */
                        vote(chg->usb_icl_votable, DCP_VOTER,
                                chg->dcp_icl_ua != -EINVAL, chg->dcp_icl_ua);
                }
        }

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s %s\n", __func__,
                   rising ? "rising" : "falling");
}

/* triggers when HVDCP is detected */
static void smblib_handle_hvdcp_detect_done(struct smb_charger *chg,
                                            bool rising)
{
        smblib_dbg(chg, PR_INTERRUPT, "IRQ: hvdcp-detect-done %s\n",
                   rising ? "rising" : "falling");
}

static void smblib_handle_apsd_done(struct smb_charger *chg, bool rising)
{
        const struct apsd_result *apsd_result;

        if (!rising) {
                return;
        }

        apsd_result = smblib_update_usb_type(chg);

        switch (apsd_result->bit) {
        case SDP_CHARGER_BIT:
        case CDP_CHARGER_BIT:
#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/05/11, sjc Delete for charging*/
        case FLOAT_CHARGER_BIT:
#endif
                if ((chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
                                || chg->use_extcon) {
                        smblib_notify_device_mode(chg, true);
                }
                break;
        case OCP_CHARGER_BIT:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/05/11, sjc Add for charging*/
        case FLOAT_CHARGER_BIT:
#endif
        case DCP_CHARGER_BIT:
                vote(chg->usb_icl_votable, DEFAULT_100MA_VOTER, false, 0);
                break;
        default:
                break;
        }

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-15  recognize CDP */
        if (apsd_result->bit == CDP_CHARGER_BIT) {
                vote(chg->usb_icl_votable, DEFAULT_100MA_VOTER, false, 0);
        }
#endif

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: apsd-done rising; %s detected\n",
                   apsd_result->name);

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25  debug */
        chg_debug("[plugin]apsd-done, type_name = %s\n", apsd_result->name);
#endif
}

irqreturn_t usb_source_change_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
        int rc = 0;
        u8 stat;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/06/19, sjc Modify to rerun apsd */
        u8 reg_value = 0;
#endif

        /*
         * Prepared to run PD or PD is active. At this moment, APSD is disabled,
         * but there still can be irq on apsd_done from previously unfinished
         * APSD run, skip it.
         */
        if (chg->ok_to_pd)
                return IRQ_HANDLED;

        rc = smblib_read(chg, APSD_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read APSD_STATUS rc=%d\n", rc);
                return IRQ_HANDLED;
        }
        smblib_dbg(chg, PR_REGISTER, "APSD_STATUS = 0x%02x\n", stat);

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-20  debug */
        chg_debug("[plugin]APSD_STATUS_REG = 0x%02x, apsd_rerun_done = %d\n", stat, chg->uusb_apsd_rerun_done);
#endif

        if ((chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
                && (stat & APSD_DTC_STATUS_DONE_BIT)
                && !chg->uusb_apsd_rerun_done) {
                /*
                 * Force re-run APSD to handle slow insertion related
                 * charger-mis-detection.
                 */
#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/06/19, sjc Modify to rerun apsd */
                chg->uusb_apsd_rerun_done = true;
                smblib_rerun_apsd(chg);
                return IRQ_HANDLED;
#else
                rc = smblib_read(chg, APSD_RESULT_STATUS_REG, &reg_value);
                if (rc < 0) {
                        chg_debug("couldn't read APSD_RESULT_STATUS_REG, rc = %d\n", rc);
                        return IRQ_HANDLED;
                }
                if (reg_value & (CDP_CHARGER_BIT | SDP_CHARGER_BIT)) {
                        chg->uusb_apsd_rerun_done = true;
                        smblib_rerun_apsd_if_required(chg);
                        return IRQ_HANDLED;
                }
#endif
        }

        smblib_handle_apsd_done(chg,
                (bool)(stat & APSD_DTC_STATUS_DONE_BIT));
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/25, sjc Add for charging */
        if ((bool)(stat & APSD_DTC_STATUS_DONE_BIT)) {
                oppo_chg_wake_update_work();
        }
#endif

        smblib_handle_hvdcp_detect_done(chg,
                (bool)(stat & QC_CHARGER_BIT));

        smblib_handle_hvdcp_check_timeout(chg,
                (bool)(stat & HVDCP_CHECK_TIMEOUT_BIT),
                (bool)(stat & QC_CHARGER_BIT));

        smblib_handle_hvdcp_3p0_auth_done(chg,
                (bool)(stat & QC_AUTH_DONE_STATUS_BIT));

        smblib_handle_sdp_enumeration_done(chg,
                (bool)(stat & ENUMERATION_DONE_BIT));

        smblib_handle_slow_plugin_timeout(chg,
                (bool)(stat & SLOW_PLUGIN_TIMEOUT_BIT));

        smblib_hvdcp_adaptive_voltage_change(chg);

        power_supply_changed(chg->usb_psy);

        rc = smblib_read(chg, APSD_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read APSD_STATUS rc=%d\n", rc);
                return IRQ_HANDLED;
        }
        smblib_dbg(chg, PR_REGISTER, "APSD_STATUS = 0x%02x\n", stat);

        return IRQ_HANDLED;
}

static void typec_sink_insertion(struct smb_charger *chg)
{
        vote(chg->usb_icl_votable, OTG_VOTER, true, 0);

        if (chg->use_extcon) {
                smblib_notify_usb_host(chg, true);
                chg->otg_present = true;
        }

        if (!chg->pr_swap_in_progress) {
                chg->ok_to_pd = !(*chg->pd_disabled) || chg->early_usb_attach;
        }
}

static void typec_src_insertion(struct smb_charger *chg)
{
        int rc = 0;
        u8 stat;

        if (chg->pr_swap_in_progress) {
                return;
        }

        rc = smblib_read(chg, LEGACY_CABLE_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_STATE_MACHINE_STATUS_REG rc=%d\n",
                        rc);
                return;
        }

        chg->typec_legacy = stat & TYPEC_LEGACY_CABLE_STATUS_BIT;
        chg->ok_to_pd = !(chg->typec_legacy || *chg->pd_disabled)
                                                || chg->early_usb_attach;
        if (!chg->ok_to_pd) {
                rc = smblib_configure_hvdcp_apsd(chg, true);
                if (rc < 0) {
                        dev_err(chg->dev,
                                "Couldn't enable APSD rc=%d\n", rc);
                        return;
                }
                smblib_rerun_apsd_if_required(chg);
        }
}

static void typec_sink_removal(struct smb_charger *chg)
{
        vote(chg->usb_icl_votable, OTG_VOTER, false, 0);

        if (chg->use_extcon) {
                if (chg->otg_present) {
                        smblib_notify_usb_host(chg, false);
                }
                chg->otg_present = false;
        }
}

static void typec_src_removal(struct smb_charger *chg)
{
        int rc;
        struct smb_irq_data *data;
        struct storm_watch *wdata;

        /* disable apsd */
        rc = smblib_configure_hvdcp_apsd(chg, false);
        if (rc < 0) {
                smblib_err(chg, "Couldn't disable APSD rc=%d\n", rc);
        }

        smblib_update_usb_type(chg);

        if (chg->wa_flags & BOOST_BACK_WA) {
                data = chg->irq_info[SWITCHER_POWER_OK_IRQ].irq_data;
                if (data) {
                        wdata = &data->storm_data;
                        update_storm_count(wdata, WEAK_CHG_STORM_COUNT);
                        vote(chg->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
                        vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
                                        false, 0);
                }
        }

        cancel_delayed_work_sync(&chg->pl_enable_work);

        /* reset input current limit voters */
        vote(chg->usb_icl_votable, DEFAULT_100MA_VOTER, true, USBIN_100MA);
        vote(chg->usb_icl_votable, PD_VOTER, false, 0);
        vote(chg->usb_icl_votable, USB_PSY_VOTER, false, 0);
        vote(chg->usb_icl_votable, DCP_VOTER, false, 0);
        vote(chg->usb_icl_votable, PL_USBIN_USBIN_VOTER, false, 0);
        vote(chg->usb_icl_votable, SW_QC3_VOTER, false, 0);
        vote(chg->usb_icl_votable, OTG_VOTER, false, 0);
        vote(chg->usb_icl_votable, CTM_VOTER, false, 0);
        vote(chg->usb_icl_votable, DYNAMIC_RP_VOTER, false, 0);

        /* reset usb irq voters */
        vote(chg->usb_irq_enable_votable, PD_VOTER, false, 0);
        vote(chg->usb_irq_enable_votable, QC_VOTER, false, 0);

        /* reset parallel voters */
        vote(chg->pl_disable_votable, PL_DELAY_VOTER, true, 0);
        vote(chg->pl_disable_votable, PL_FCC_LOW_VOTER, false, 0);
        vote(chg->pl_enable_votable_indirect, USBIN_I_VOTER, false, 0);
        vote(chg->pl_enable_votable_indirect, USBIN_V_VOTER, false, 0);
        vote(chg->awake_votable, PL_DELAY_VOTER, false, 0);

        chg->pulse_cnt = 0;
        chg->usb_icl_delta_ua = 0;
        chg->voltage_min_uv = MICRO_5V;
        chg->voltage_max_uv = MICRO_5V;

        /* write back the default FLOAT charger configuration */
        rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
                                (u8)FLOAT_OPTIONS_MASK, chg->float_cfg);
        if (rc < 0) {
                smblib_err(chg, "Couldn't write float charger options rc=%d\n",
                        rc);
        }

        /* reconfigure allowed voltage for HVDCP */
        rc = smblib_set_adapter_allowance(chg,
                        USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V);
        if (rc < 0) {
                smblib_err(chg, "Couldn't set USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V rc=%d\n",
                        rc);
        }

        if (chg->use_extcon) {
                smblib_notify_device_mode(chg, false);
        }

        chg->typec_legacy = false;
}

static void smblib_handle_rp_change(struct smb_charger *chg, int typec_mode)
{
        int rp_ua;
        const struct apsd_result *apsd = smblib_get_apsd_result(chg);

        if ((apsd->pst != POWER_SUPPLY_TYPE_USB_DCP)
                && (apsd->pst != POWER_SUPPLY_TYPE_USB_FLOAT)) {
                return;
        }

        /*
         * if APSD indicates FLOAT and the USB stack had detected SDP,
         * do not respond to Rp changes as we do not confirm that its
         * a legacy cable
         */
        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB) {
                return;
        }
        /*
         * We want the ICL vote @ 100mA for a FLOAT charger
         * until the detection by the USB stack is complete.
         * Ignore the Rp changes unless there is a
         * pre-existing valid vote.
         */
        if (apsd->pst == POWER_SUPPLY_TYPE_USB_FLOAT &&
                (get_client_vote(chg->usb_icl_votable, DEFAULT_100MA_VOTER)
                                        <= USBIN_100MA)) {
                return;
        }

        /*
         * handle Rp change for DCP/FLOAT/OCP.
         * Update the current only if the Rp is different from
         * the last Rp value.
         */
        smblib_dbg(chg, PR_MISC, "CC change old_mode=%d new_mode=%d\n",
                                                chg->typec_mode, typec_mode);

        rp_ua = get_rp_based_dcp_current(chg, typec_mode);
        vote(chg->usb_icl_votable, DYNAMIC_RP_VOTER, true, rp_ua);
}

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Add for using gpio as OTG ID*/
irqreturn_t usbid_change_handler(int irq, void *data)
{
        struct oppo_chg_chip *chip = data;
        if (oppo_usbid_check_is_gpio(chip) == true) {
                if (g_smb_chip->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                        cancel_delayed_work_sync(&g_smb_chip->uusb_otg_work);
                        vote(g_smb_chip->awake_votable, OTG_DELAY_VOTER, true, 0);
                        chg_debug("Scheduling OTG work\n");
                        schedule_delayed_work(&g_smb_chip->uusb_otg_work,
                                        msecs_to_jiffies(g_smb_chip->otg_delay_ms));
                        return IRQ_HANDLED;
                }
        } else {
                chg_err("usbid_change_handler, oppo_usbid_check_is_gpio == false!\n");
        }

        return IRQ_HANDLED;
}
#endif

irqreturn_t typec_or_rid_detection_change_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;

        if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                cancel_delayed_work_sync(&chg->uusb_otg_work);
                vote(chg->awake_votable, OTG_DELAY_VOTER, true, 0);
                smblib_dbg(chg, PR_INTERRUPT, "Scheduling OTG work\n");
                schedule_delayed_work(&chg->uusb_otg_work,
                                msecs_to_jiffies(chg->otg_delay_ms));
                return IRQ_HANDLED;
        }

        return IRQ_HANDLED;
}

irqreturn_t typec_state_change_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
        int typec_mode;

        if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                smblib_dbg(chg, PR_INTERRUPT,
                                "Ignoring for micro USB\n");
                return IRQ_HANDLED;
        }

        typec_mode = smblib_get_prop_typec_mode(chg);
        if (chg->sink_src_mode != UNATTACHED_MODE
                        && (typec_mode != chg->typec_mode)) {
                smblib_handle_rp_change(chg, typec_mode);
        }
        chg->typec_mode = typec_mode;

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: cc-state-change; Type-C %s detected\n",
                                smblib_typec_mode_name[chg->typec_mode]);

        power_supply_changed(chg->usb_psy);

        return IRQ_HANDLED;
}

irqreturn_t typec_attach_detach_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
        u8 stat;
        int rc;

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);

        rc = smblib_read(chg, TYPE_C_STATE_MACHINE_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_STATE_MACHINE_STATUS_REG rc=%d\n",
                        rc);
                return IRQ_HANDLED;
        }

        if (stat & TYPEC_ATTACH_DETACH_STATE_BIT) {
                rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't read TYPE_C_MISC_STATUS_REG rc=%d\n",
                                rc);
                        return IRQ_HANDLED;
                }

                if (stat & SNK_SRC_MODE_BIT) {
                        chg->sink_src_mode = SRC_MODE;
                        typec_sink_insertion(chg);
                } else {
                        chg->sink_src_mode = SINK_MODE;
                        typec_src_insertion(chg);
                }

        } else {
                switch (chg->sink_src_mode) {
                case SRC_MODE:
                        typec_sink_removal(chg);
                        break;
                case SINK_MODE:
                        typec_src_removal(chg);
                        break;
                default:
                        break;
                }

                if (!chg->pr_swap_in_progress) {
                        chg->ok_to_pd = false;
                        chg->sink_src_mode = UNATTACHED_MODE;
                        chg->early_usb_attach = false;
                }
        }

        power_supply_changed(chg->usb_psy);

        return IRQ_HANDLED;
}

irqreturn_t dc_plugin_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/09, sjc Add for charging */
        if (chg->dc_psy)
#endif
        power_supply_changed(chg->dc_psy);
        return IRQ_HANDLED;
}

irqreturn_t high_duty_cycle_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;

        chg->is_hdc = true;
        /*
         * Disable usb IRQs after the flag set and re-enable IRQs after
         * the flag cleared in the delayed work queue, to avoid any IRQ
         * storming during the delays
         */
        if (chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq) {
                disable_irq_nosync(chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq);
        }

        schedule_delayed_work(&chg->clear_hdc_work, msecs_to_jiffies(60));

        return IRQ_HANDLED;
}

static void smblib_bb_removal_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                bb_removal_work.work);

        vote(chg->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
        vote(chg->awake_votable, BOOST_BACK_VOTER, false, 0);
}

#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/19, sjc Delete for charging */
#define BOOST_BACK_UNVOTE_DELAY_MS              750
#define BOOST_BACK_STORM_COUNT                  3
#endif
#define WEAK_CHG_STORM_COUNT                    8
irqreturn_t switcher_power_ok_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/19, sjc Delete for charging */
        struct storm_watch *wdata = &irq_data->storm_data;
#endif
        int rc, usb_icl;
        u8 stat;

        if (!(chg->wa_flags & BOOST_BACK_WA)) {
                return IRQ_HANDLED;
        }

        rc = smblib_read(chg, POWER_PATH_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read POWER_PATH_STATUS rc=%d\n", rc);
                return IRQ_HANDLED;
        }

        /* skip suspending input if its already suspended by some other voter */
        usb_icl = get_effective_result(chg->usb_icl_votable);
        if ((stat & USE_USBIN_BIT) && usb_icl >= 0 && usb_icl <= USBIN_25MA) {
                return IRQ_HANDLED;
        }

        if (stat & USE_DCIN_BIT) {
                return IRQ_HANDLED;
        }

        if (is_storming(&irq_data->storm_data)) {
#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2018/01/19, sjc Modiy for charging */
                /* This could be a weak charger reduce ICL */
                if (!is_client_vote_enabled(chg->usb_icl_votable,
                                                WEAK_CHARGER_VOTER)) {
                        smblib_err(chg,
                                "Weak charger detected: voting %dmA ICL\n",
                                *chg->weak_chg_icl_ua / 1000);
                        vote(chg->usb_icl_votable, WEAK_CHARGER_VOTER,
                                        true, *chg->weak_chg_icl_ua);
                        /*
                         * reset storm data and set the storm threshold
                         * to 3 for reverse boost detection.
                         */
                        update_storm_count(wdata, BOOST_BACK_STORM_COUNT);
                } else {
                        smblib_err(chg,
                                "Reverse boost detected: voting 0mA to suspend input\n");
                        vote(chg->usb_icl_votable, BOOST_BACK_VOTER, true, 0);
                        vote(chg->awake_votable, BOOST_BACK_VOTER, true, 0);
                        /*
                         * Remove the boost-back vote after a delay, to avoid
                         * permanently suspending the input if the boost-back
                         * condition is unintentionally hit.
                         */
                        schedule_delayed_work(&chg->bb_removal_work,
                                msecs_to_jiffies(BOOST_BACK_UNVOTE_DELAY_MS));
                }
#else
                if (printk_ratelimit()) {
                        smblib_err(chg, "Reverse boost detected: voting 0mA to suspend input\n");
                }
                if (chg->real_charger_type != POWER_SUPPLY_TYPE_USB_CDP
                                && chg->real_charger_type != POWER_SUPPLY_TYPE_USB) {
                        vote(chg->usb_icl_votable, BOOST_BACK_VOTER, true, 0);
                }
#endif
        }

        return IRQ_HANDLED;
}

irqreturn_t wdog_bark_irq_handler(int irq, void *data)
{
        struct smb_irq_data *irq_data = data;
        struct smb_charger *chg = irq_data->parent_data;
        int rc;

        smblib_dbg(chg, PR_INTERRUPT, "IRQ: %s\n", irq_data->name);

        rc = smblib_write(chg, BARK_BITE_WDOG_PET_REG, BARK_BITE_WDOG_PET_BIT);
        if (rc < 0) {
                smblib_err(chg, "Couldn't pet the dog rc=%d\n", rc);
        }

        if (chg->step_chg_enabled || chg->sw_jeita_enabled) {
                power_supply_changed(chg->batt_psy);
        }

        return IRQ_HANDLED;
}

/**************
 * Additional USB PSY getters/setters
 * that call interrupt functions
 ***************/

int smblib_get_prop_pr_swap_in_progress(struct smb_charger *chg,
                                union power_supply_propval *val)
{
        val->intval = chg->pr_swap_in_progress;
        return 0;
}

int smblib_set_prop_pr_swap_in_progress(struct smb_charger *chg,
                                const union power_supply_propval *val)
{
        int rc;
        u8 stat = 0, orientation;

        chg->pr_swap_in_progress = val->intval;

        rc = smblib_masked_write(chg, TYPE_C_DEBOUNCE_OPTION_REG,
                        REDUCE_TCCDEBOUNCE_TO_2MS_BIT,
                        val->intval ? REDUCE_TCCDEBOUNCE_TO_2MS_BIT : 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't set tCC debounce rc=%d\n", rc);
        }

        rc = smblib_masked_write(chg, TYPE_C_EXIT_STATE_CFG_REG,
                        BYPASS_VSAFE0V_DURING_ROLE_SWAP_BIT,
                        val->intval ? BYPASS_VSAFE0V_DURING_ROLE_SWAP_BIT : 0);
        if (rc < 0) {
                smblib_err(chg, "Couldn't set exit state cfg rc=%d\n", rc);
        }

        if (chg->pr_swap_in_progress) {
                rc = smblib_read(chg, TYPE_C_MISC_STATUS_REG, &stat);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't read TYPE_C_STATUS_4 rc=%d\n",
                                rc);
                }

                orientation =
                        stat & CC_ORIENTATION_BIT ? TYPEC_CCOUT_VALUE_BIT : 0;
                rc = smblib_masked_write(chg, TYPE_C_CCOUT_CONTROL_REG,
                        TYPEC_CCOUT_SRC_BIT | TYPEC_CCOUT_BUFFER_EN_BIT
                                        | TYPEC_CCOUT_VALUE_BIT,
                        TYPEC_CCOUT_SRC_BIT | TYPEC_CCOUT_BUFFER_EN_BIT
                                        | orientation);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't read TYPE_C_CCOUT_CONTROL_REG rc=%d\n",
                                rc);
                }
        } else {
                rc = smblib_masked_write(chg, TYPE_C_CCOUT_CONTROL_REG,
                        TYPEC_CCOUT_SRC_BIT, 0);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't read TYPE_C_CCOUT_CONTROL_REG rc=%d\n",
                                rc);
                }

                /* enable DRP */
                rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
                                 TYPEC_POWER_ROLE_CMD_MASK, 0);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't enable DRP rc=%d\n", rc);
                }
        }

        return 0;
}

/***************
 * Work Queues *
 ***************/
 
static void smblib_uusb_otg_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                uusb_otg_work.work);
        int rc;
        u8 stat;
        bool otg;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Modify for using gpio as OTG ID*/
        struct oppo_chg_chip *chip = g_oppo_chip;
        union power_supply_propval ret = {0,};

        chg->usb_psy->desc->get_property(chg->usb_psy, POWER_SUPPLY_PROP_OTG_SWITCH, &ret);

        if (oppo_usbid_check_is_gpio(chip) == true) {
                if (ret.intval == false) {
                        otg = 0;
                } else {
                        otg = !gpio_get_value(chip->normalchg_gpio.usbid_gpio);
                }
                /*the following 3 lines are just for compling*/
                rc = stat = 0;
                if (rc != 0) {
                        goto out;
                }
        } else {
                rc = smblib_read(chg, TYPEC_U_USB_STATUS_REG, &stat);
                if (rc < 0) {
                        chg_err("Couldn't read TYPEC_U_USB_STATUS_REG rc=%d\n", rc);
                        goto out;
                }

                otg = !!(stat & U_USB_GROUND_NOVBUS_BIT);
        }
#else
        rc = smblib_read(chg, TYPEC_U_USB_STATUS_REG, &stat);
        if (rc < 0) {
                smblib_err(chg, "Couldn't read TYPE_C_STATUS_3 rc=%d\n", rc);
                goto out;
        }
        otg = !!(stat & U_USB_GROUND_NOVBUS_BIT);
#endif

        if (chg->otg_present != otg) {
                smblib_notify_usb_host(chg, otg);
        } else {
                goto out;
        }

        chg->otg_present = otg;
        if (!otg) {
                chg->boost_current_ua = 0;
        }

        rc = smblib_set_charge_param(chg, &chg->param.freq_switcher,
                                otg ? chg->chg_freq.freq_below_otg_threshold
                                        : chg->chg_freq.freq_removal);
        if (rc < 0) {
                dev_err(chg->dev, "Error in setting freq_boost rc=%d\n", rc);
        }

        smblib_dbg(chg, PR_REGISTER, "TYPE_C_U_USB_STATUS = 0x%02x OTG=%d\n",
                        stat, otg);
        power_supply_changed(chg->usb_psy);

out:
        vote(chg->awake_votable, OTG_DELAY_VOTER, false, 0);
}

static void bms_update_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                bms_update_work);

        smblib_suspend_on_debug_battery(chg);

        if (chg->batt_psy) {
                power_supply_changed(chg->batt_psy);
        }
}

static void pl_update_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                pl_update_work);

        smblib_stat_sw_override_cfg(chg, false);
}

static void clear_hdc_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                clear_hdc_work.work);

        chg->is_hdc = 0;
        if (chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq) {
                enable_irq(chg->irq_info[HIGH_DUTY_CYCLE_IRQ].irq);
        }
}

static void smblib_icl_change_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                        icl_change_work.work);
        int rc, settled_ua;

        rc = smblib_get_charge_param(chg, &chg->param.icl_stat, &settled_ua);
        if (rc < 0) {
                smblib_err(chg, "Couldn't get ICL status rc=%d\n", rc);
                return;
        }

        power_supply_changed(chg->usb_main_psy);

        smblib_dbg(chg, PR_INTERRUPT, "icl_settled=%d\n", settled_ua);
}

static void smblib_pl_enable_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                        pl_enable_work.work);

        smblib_dbg(chg, PR_PARALLEL, "timer expired, enabling parallel\n");
        vote(chg->pl_disable_votable, PL_DELAY_VOTER, false, 0);
        vote(chg->awake_votable, PL_DELAY_VOTER, false, 0);
}

#define JEITA_SOFT                      0
#define JEITA_HARD                      1
static int smblib_update_jeita(struct smb_charger *chg, u32 *thresholds,
                                                                int type)
{
        int rc;
        u16 temp, base;

        base = CHGR_JEITA_THRESHOLD_BASE_REG(type);

        temp = thresholds[1] & 0xFFFF;
        temp = ((temp & 0xFF00) >> 8) | ((temp & 0xFF) << 8);
        rc = smblib_batch_write(chg, base, (u8 *)&temp, 2);
        if (rc < 0) {
                smblib_err(chg,
                        "Couldn't configure Jeita %s hot threshold rc=%d\n",
                        (type == JEITA_SOFT) ? "Soft" : "Hard", rc);
                return rc;
        }

        temp = thresholds[0] & 0xFFFF;
        temp = ((temp & 0xFF00) >> 8) | ((temp & 0xFF) << 8);
        rc = smblib_batch_write(chg, base + 2, (u8 *)&temp, 2);
        if (rc < 0) {
                smblib_err(chg,
                        "Couldn't configure Jeita %s cold threshold rc=%d\n",
                        (type == JEITA_SOFT) ? "Soft" : "Hard", rc);
                return rc;
        }

        smblib_dbg(chg, PR_MISC, "%s Jeita threshold configured\n",
                                (type == JEITA_SOFT) ? "Soft" : "Hard");

        return 0;
}

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-10  use oppo battery */
#define NONSTD_BAT_ID_OHM       151000
#endif
static void jeita_update_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                jeita_update_work);
        struct device_node *node = chg->dev->of_node;
        struct device_node *batt_node, *pnode;
        union power_supply_propval val;
        int rc;
        u32 jeita_thresholds[2];

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-10  use oppo battery */
        bool bat_thermal_1k;

        bat_thermal_1k = false;
#endif

        batt_node = of_find_node_by_name(node, "qcom,battery-data");
        if (!batt_node) {
                smblib_err(chg, "Batterydata not available\n");
                goto out;
        }

        rc = power_supply_get_property(chg->bms_psy,
                        POWER_SUPPLY_PROP_RESISTANCE_ID, &val);
        if (rc < 0) {
                smblib_err(chg, "Failed to get batt-id rc=%d\n", rc);
                goto out;
        }

        pnode = of_batterydata_get_best_profile(batt_node,
                                        val.intval / 1000, NULL);

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-10  use oppo battery */
        if (IS_ERR(pnode)) {
                chg_err("Failed to detect valid battery profile, try nonstd profile\n");
                pnode = of_batterydata_get_best_profile(batt_node,
                                        NONSTD_BAT_ID_OHM / 1000, NULL);
                if (IS_ERR(pnode)) {
                        rc = PTR_ERR(pnode);
                        chg_err("Failed to detect nonstd profile %d\n", rc);
                        goto out;
                }
        }
#else
        if (IS_ERR(pnode)) {
                rc = PTR_ERR(pnode);
                smblib_err(chg, "Failed to detect valid battery profile %d\n",
                                rc);
                goto out;
        }
#endif

#ifndef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-10  use oppo battery*/
        rc = of_property_read_u32_array(pnode, "qcom,jeita-hard-thresholds",
                                jeita_thresholds, 2);
#else
        if (bat_thermal_1k == true) {
                rc = of_property_read_u32_array(pnode, "qcom,jeita-hard-thresholds_1k",
                                        jeita_thresholds, 2);
        } else {
                rc = of_property_read_u32_array(pnode, "qcom,jeita-hard-thresholds_5p1k",
                                        jeita_thresholds, 2);
        }
#endif

        if (!rc) {
                rc = smblib_update_jeita(chg, jeita_thresholds, JEITA_HARD);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't configure Hard Jeita rc=%d\n",
                                        rc);
                        goto out;
                }
        }

#ifndef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-10  use oppo battery */
        rc = of_property_read_u32_array(pnode, "qcom,jeita-soft-thresholds",
                                jeita_thresholds, 2);
#else
        if (bat_thermal_1k == true) {
                rc = of_property_read_u32_array(pnode, "qcom,jeita-soft-thresholds_1k",
                                        jeita_thresholds, 2);
        } else {
                rc = of_property_read_u32_array(pnode, "qcom,jeita-soft-thresholds_5p1k",
                                        jeita_thresholds, 2);
        }
#endif

        if (!rc) {
                rc = smblib_update_jeita(chg, jeita_thresholds, JEITA_SOFT);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't configure Soft Jeita rc=%d\n",
                                        rc);
                        goto out;
                }
        }

out:
        chg->jeita_configured = true;
}

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/25, sjc Add for charging */
static int oppo_chg_get_fv_monitor(struct oppo_chg_chip *chip)
{
        int default_fv = 0;

        if (!chip) {
                return 0;
        }

        switch(chip->tbatt_status) {
                case BATTERY_STATUS__INVALID:
                case BATTERY_STATUS__REMOVED:
                case BATTERY_STATUS__LOW_TEMP:
                case BATTERY_STATUS__HIGH_TEMP:
                        break;
                case BATTERY_STATUS__COLD_TEMP:
                        default_fv = chip->limits.temp_cold_vfloat_mv;
                        break;
                case BATTERY_STATUS__LITTLE_COLD_TEMP:
                        default_fv = chip->limits.temp_little_cold_vfloat_mv;
                        break;
                case BATTERY_STATUS__COOL_TEMP:
                        default_fv = chip->limits.temp_cool_vfloat_mv;
                        break;
                case BATTERY_STATUS__LITTLE_COOL_TEMP:
                        default_fv = chip->limits.temp_little_cool_vfloat_mv;
                        break;
                case BATTERY_STATUS__NORMAL:
                        if (oppo_vooc_get_fastchg_to_normal() && chip->charging_state != CHARGING_STATUS_FULL)
                                default_fv = chip->limits.temp_normal_vfloat_mv_voocchg;
                        else
                                default_fv = chip->limits.temp_normal_vfloat_mv_normalchg;
                        break;
                case BATTERY_STATUS__WARM_TEMP:
                        default_fv = chip->limits.temp_warm_vfloat_mv;
                        break;
                default:
                        break;
        }
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        if (oppo_short_c_batt_is_prohibit_chg(chip) && default_fv > chip->limits.short_c_bat_vfloat_mv) {
                default_fv = chip->limits.short_c_bat_vfloat_mv;
        }
#endif  
        return default_fv;
}

static int oppo_chg_get_vbatt_full_vol_sw(struct oppo_chg_chip *chip)
{
        int default_fv = 0;

        if (!chip) {
                return 0;
        }

        switch(chip->tbatt_status) {
                case BATTERY_STATUS__INVALID:
                case BATTERY_STATUS__REMOVED:
                case BATTERY_STATUS__LOW_TEMP:
                case BATTERY_STATUS__HIGH_TEMP:
                        break;
                case BATTERY_STATUS__COLD_TEMP:
                        default_fv = chip->limits.cold_vfloat_sw_limit;
                        break;
                case BATTERY_STATUS__LITTLE_COLD_TEMP:
                        default_fv = chip->limits.little_cold_vfloat_sw_limit;
                        break;
                case BATTERY_STATUS__COOL_TEMP:
                        default_fv = chip->limits.cool_vfloat_sw_limit;
                        break;
                case BATTERY_STATUS__LITTLE_COOL_TEMP:
                        default_fv = chip->limits.little_cool_vfloat_sw_limit;
                        break;
                case BATTERY_STATUS__NORMAL:
                        default_fv = chip->limits.normal_vfloat_sw_limit;
                        break;
                case BATTERY_STATUS__WARM_TEMP:
                        default_fv = chip->limits.warm_vfloat_sw_limit;
                        break;
                default:
                        break;
        }
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        if (oppo_short_c_batt_is_prohibit_chg(chip) && default_fv > chip->limits.short_c_bat_vfloat_sw_limit) {
                default_fv = chip->limits.short_c_bat_vfloat_sw_limit;
        }
#endif
	return default_fv;
}

/* When charger voltage is setting to < 4.3V and then resume to 5V, cannot charge, so... */
static void oppo_chg_monitor_work(struct work_struct *work)
{
        struct smb_charger *chg = container_of(work, struct smb_charger,
                                                        chg_monitor_work.work);
        struct oppo_chg_chip *chip = g_oppo_chip;
        int boot_mode = get_boot_mode();
        static int counts = 0;
        int rechg_vol;
        int rc;
        u8 stat;

        if (!chip || !chip->charger_exist || !chip->batt_exist || !chip->mmi_chg) {
                return;
        }
        if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB || chg->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
                return;
        }
        if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN) {
                return;
        }
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        if (oppo_short_c_batt_is_disable_rechg(chip)) {
                return;
        }
#endif
        if (oppo_vooc_get_fastchg_started() == true || chip->charger_volt < 4400) {
                goto rerun_work;
        }

        if (chip->tbatt_status == BATTERY_STATUS__COLD_TEMP) {
                rechg_vol = oppo_chg_get_fv_monitor(chip) - 300;
        } else if (chip->tbatt_status == BATTERY_STATUS__LITTLE_COLD_TEMP) {
                rechg_vol = oppo_chg_get_fv_monitor(chip) - 200;
        } else {
                rechg_vol = oppo_chg_get_fv_monitor(chip) - 100;
        }

        if ((chip->batt_volt > rechg_vol - 10) && chip->batt_full) {
                goto rerun_work;
        } else if (chip->batt_volt > oppo_chg_get_vbatt_full_vol_sw(chip) - 10) {
                goto rerun_work;
        }

        if (chip->icharging >= 0) {
                counts++;
        } else if (chip->icharging < 0 && (chip->icharging * -1) <= chip->limits.iterm_ma / 2) {
                counts++;
        } else {
                counts = 0;
        }

        if (counts > 10) {
                counts = 10;
        }

        if (counts >= (chip->batt_full ? 8 : 3)) {//because rechg counts=6
                rc = smblib_read(chg, 0x100E, &stat);
                if (rc < 0) {
                        chg_err("Couldn't get BATTERY_CHARGER_STATUS_8_REG status rc=%d\n", rc);
                        goto rerun_work;
                }
                if (stat & BIT2) {
                        usb_online_status = true;
                        chg_debug("PRE_TERM_BIT is set[0x%x], clear it\n", stat);
                        rc = smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 1);
                        if (rc < 0) {
                                chg_err("Couldn't set USBIN_SUSPEND_BIT rc=%d\n", rc);
                                goto rerun_work;
                        }
                        msleep(50);
                        rc = smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 0);
                        if (rc < 0) {
                                chg_err("Couldn't clear USBIN_SUSPEND_BIT rc=%d\n", rc);
                                goto rerun_work;
                        }
                        msleep(10);
                        rc = smblib_masked_write(chg, AICL_CMD_REG, BIT1, BIT1);
                        if (rc < 0) {
                                chg_err("Couldn't set RESTART_AICL_BIT rc=%d\n", rc);
                                goto rerun_work;
                        }
                        chg_debug(" ichg[%d], fv[%d]\n", chip->icharging, oppo_chg_get_fv_monitor(chip));
                }
                counts = 0;
        }

rerun_work:
        usb_online_status = false;
        schedule_delayed_work(&chg->chg_monitor_work, OPPO_CHG_MONITOR_INTERVAL);
}
#endif

static int smblib_create_votables(struct smb_charger *chg)
{
        int rc = 0;

        chg->fcc_votable = find_votable("FCC");
        if (chg->fcc_votable == NULL) {
                rc = -EINVAL;
                smblib_err(chg, "Couldn't find FCC votable rc=%d\n", rc);
                return rc;
        }

        chg->fv_votable = find_votable("FV");
        if (chg->fv_votable == NULL) {
                rc = -EINVAL;
                smblib_err(chg, "Couldn't find FV votable rc=%d\n", rc);
                return rc;
        }

        chg->usb_icl_votable = find_votable("USB_ICL");
        if (chg->usb_icl_votable == NULL) {
                rc = -EINVAL;
                smblib_err(chg, "Couldn't find USB_ICL votable rc=%d\n", rc);
                return rc;
        }

        chg->pl_disable_votable = find_votable("PL_DISABLE");
        if (chg->pl_disable_votable == NULL) {
                rc = -EINVAL;
                smblib_err(chg, "Couldn't find votable PL_DISABLE rc=%d\n", rc);
                return rc;
        }

        chg->pl_enable_votable_indirect = find_votable("PL_ENABLE_INDIRECT");
        if (chg->pl_enable_votable_indirect == NULL) {
                rc = -EINVAL;
                smblib_err(chg,
                        "Couldn't find votable PL_ENABLE_INDIRECT rc=%d\n",
                        rc);
                return rc;
        }

        vote(chg->pl_disable_votable, PL_DELAY_VOTER, true, 0);

        chg->dc_suspend_votable = create_votable("DC_SUSPEND", VOTE_SET_ANY,
                                        smblib_dc_suspend_vote_callback,
                                        chg);
        if (IS_ERR(chg->dc_suspend_votable)) {
                rc = PTR_ERR(chg->dc_suspend_votable);
                chg->dc_suspend_votable = NULL;
                return rc;
        }

        chg->awake_votable = create_votable("AWAKE", VOTE_SET_ANY,
                                        smblib_awake_vote_callback,
                                        chg);
        if (IS_ERR(chg->awake_votable)) {
                rc = PTR_ERR(chg->awake_votable);
                chg->awake_votable = NULL;
                return rc;
        }

        chg->chg_disable_votable = create_votable("CHG_DISABLE", VOTE_SET_ANY,
                                        smblib_chg_disable_vote_callback,
                                        chg);
        if (IS_ERR(chg->chg_disable_votable)) {
                rc = PTR_ERR(chg->chg_disable_votable);
                chg->chg_disable_votable = NULL;
                return rc;
        }

        chg->usb_irq_enable_votable = create_votable("USB_IRQ_DISABLE",
                                        VOTE_SET_ANY,
                                        smblib_usb_irq_enable_vote_callback,
                                        chg);
        if (IS_ERR(chg->usb_irq_enable_votable)) {
                rc = PTR_ERR(chg->usb_irq_enable_votable);
                chg->usb_irq_enable_votable = NULL;
                return rc;
        }

        return rc;
}

static void smblib_destroy_votables(struct smb_charger *chg)
{
        if (chg->dc_suspend_votable) {
                destroy_votable(chg->dc_suspend_votable);
        }
        if (chg->usb_icl_votable) {
                destroy_votable(chg->usb_icl_votable);
        }
        if (chg->awake_votable) {
                destroy_votable(chg->awake_votable);
        }
        if (chg->chg_disable_votable) {
                destroy_votable(chg->chg_disable_votable);
        }
}

int smblib_init(struct smb_charger *chg)
{
        int rc = 0;

        mutex_init(&chg->lock);
        INIT_WORK(&chg->bms_update_work, bms_update_work);
        INIT_WORK(&chg->pl_update_work, pl_update_work);
        INIT_WORK(&chg->jeita_update_work, jeita_update_work);
        INIT_DELAYED_WORK(&chg->clear_hdc_work, clear_hdc_work);
        INIT_DELAYED_WORK(&chg->icl_change_work, smblib_icl_change_work);
        INIT_DELAYED_WORK(&chg->pl_enable_work, smblib_pl_enable_work);
        INIT_DELAYED_WORK(&chg->uusb_otg_work, smblib_uusb_otg_work);
        INIT_DELAYED_WORK(&chg->bb_removal_work, smblib_bb_removal_work);

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/25, sjc Add for charging */
        INIT_DELAYED_WORK(&chg->chg_monitor_work, oppo_chg_monitor_work);
#endif

        chg->fake_capacity = -EINVAL;
        chg->fake_input_current_limited = -EINVAL;
        chg->fake_batt_status = -EINVAL;
        chg->jeita_configured = false;
        chg->sink_src_mode = UNATTACHED_MODE;

        switch (chg->mode) {
        case PARALLEL_MASTER:
                rc = qcom_batt_init(chg->smb_version);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't init qcom_batt_init rc=%d\n",
                                rc);
                        return rc;
                }

                rc = qcom_step_chg_init(chg->dev, chg->step_chg_enabled,
                                                chg->sw_jeita_enabled);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't init qcom_step_chg_init rc=%d\n",
                                rc);
                        return rc;
                }

                rc = smblib_create_votables(chg);
                if (rc < 0) {
                        smblib_err(chg, "Couldn't create votables rc=%d\n",
                                rc);
                        return rc;
                }

                chg->bms_psy = power_supply_get_by_name("bms");
                chg->pl.psy = power_supply_get_by_name("parallel");
                if (chg->pl.psy) {
                        rc = smblib_stat_sw_override_cfg(chg, false);
                        if (rc < 0) {
                                smblib_err(chg,
                                        "Couldn't config stat sw rc=%d\n", rc);
                                return rc;
                        }
                }
                rc = smblib_register_notifier(chg);
                if (rc < 0) {
                        smblib_err(chg,
                                "Couldn't register notifier rc=%d\n", rc);
                        return rc;
                }
                break;
        case PARALLEL_SLAVE:
                break;
        default:
                smblib_err(chg, "Unsupported mode %d\n", chg->mode);
                return -EINVAL;
        }

        return rc;
}

int smblib_deinit(struct smb_charger *chg)
{
        switch (chg->mode) {
        case PARALLEL_MASTER:
                cancel_work_sync(&chg->bms_update_work);
                cancel_work_sync(&chg->jeita_update_work);
                cancel_work_sync(&chg->pl_update_work);
                cancel_delayed_work_sync(&chg->clear_hdc_work);
                cancel_delayed_work_sync(&chg->icl_change_work);
                cancel_delayed_work_sync(&chg->pl_enable_work);
                cancel_delayed_work_sync(&chg->uusb_otg_work);
                cancel_delayed_work_sync(&chg->bb_removal_work);
                power_supply_unreg_notifier(&chg->nb);
                smblib_destroy_votables(chg);
                qcom_step_chg_deinit();
                qcom_batt_deinit();
                break;
        case PARALLEL_SLAVE:
                break;
        default:
                smblib_err(chg, "Unsupported mode %d\n", chg->mode);
                return -EINVAL;
        }

        return 0;
}

static struct smb_params smb5_pmi632_params = {
        .fcc                    = {
                .name   = "fast charge current",
                .reg    = CHGR_FAST_CHARGE_CURRENT_CFG_REG,
                .min_u  = 0,
                .max_u  = 3000000,
                .step_u = 50000,
        },
        .fv                     = {
                .name   = "float voltage",
                .reg    = CHGR_FLOAT_VOLTAGE_CFG_REG,
                .min_u  = 3600000,
                .max_u  = 4800000,
                .step_u = 10000,
        },
        .usb_icl                = {
                .name   = "usb input current limit",
                .reg    = USBIN_CURRENT_LIMIT_CFG_REG,
                .min_u  = 0,
                .max_u  = 3000000,
                .step_u = 50000,
        },
        .icl_stat               = {
                .name   = "input current limit status",
                .reg    = AICL_ICL_STATUS_REG,
                .min_u  = 0,
                .max_u  = 3000000,
                .step_u = 50000,
        },
        .otg_cl                 = {
                .name   = "usb otg current limit",
                .reg    = DCDC_OTG_CURRENT_LIMIT_CFG_REG,
                .min_u  = 500000,
                .max_u  = 1000000,
                .step_u = 250000,
        },
        .jeita_cc_comp_hot      = {
                .name   = "jeita fcc reduction",
                .reg    = JEITA_CCCOMP_CFG_HOT_REG,
                .min_u  = 0,
                .max_u  = 1575000,
                .step_u = 25000,
        },
        .jeita_cc_comp_cold     = {
                .name   = "jeita fcc reduction",
                .reg    = JEITA_CCCOMP_CFG_COLD_REG,
                .min_u  = 0,
                .max_u  = 1575000,
                .step_u = 25000,
        },
        .freq_switcher          = {
                .name   = "switching frequency",
                .reg    = DCDC_FSW_SEL_REG,
                .min_u  = 600,
                .max_u  = 1200,
                .step_u = 400,
                .set_proc = smblib_set_chg_freq,
        },
};

static struct smb_params smb5_pmi855_params = {
        .fcc                    = {
                .name   = "fast charge current",
                .reg    = CHGR_FAST_CHARGE_CURRENT_CFG_REG,
                .min_u  = 0,
                .max_u  = 8000000,
                .step_u = 25000,
        },
        .fv                     = {
                .name   = "float voltage",
                .reg    = CHGR_FLOAT_VOLTAGE_CFG_REG,
                .min_u  = 3600000,
                .max_u  = 4790000,
                .step_u = 10000,
        },
        .usb_icl                = {
                .name   = "usb input current limit",
                .reg    = USBIN_CURRENT_LIMIT_CFG_REG,
                .min_u  = 0,
                .max_u  = 5000000,
                .step_u = 50000,
        },
        .icl_stat               = {
                .name   = "input current limit status",
                .reg    = AICL_ICL_STATUS_REG,
                .min_u  = 0,
                .max_u  = 5000000,
                .step_u = 50000,
        },
        .otg_cl                 = {
                .name   = "usb otg current limit",
                .reg    = DCDC_OTG_CURRENT_LIMIT_CFG_REG,
                .min_u  = 500000,
                .max_u  = 3000000,
                .step_u = 500000,
        },
        .jeita_cc_comp_hot      = {
                .name   = "jeita fcc reduction",
                .reg    = JEITA_CCCOMP_CFG_HOT_REG,
                .min_u  = 0,
                .max_u  = 8000000,
                .step_u = 25000,
                .set_proc = NULL,
        },
        .jeita_cc_comp_cold     = {
                .name   = "jeita fcc reduction",
                .reg    = JEITA_CCCOMP_CFG_COLD_REG,
                .min_u  = 0,
                .max_u  = 8000000,
                .step_u = 25000,
                .set_proc = NULL,
        },
        .freq_switcher          = {
                .name   = "switching frequency",
                .reg    = DCDC_FSW_SEL_REG,
                .min_u  = 1200,
                .max_u  = 2400,
                .step_u = 400,
                .set_proc = NULL,
        },
};

#ifndef VENDOR_EDIT
/* Yichun Chen  PSW.BSP.CHG  2018-05-03  OPPO_CHARGE */
struct smb_dt_props {
        int                     usb_icl_ua;
        struct device_node      *revid_dev_node;
        enum float_options      float_option;
        int                     chg_inhibit_thr_mv;
        bool                    no_battery;
        bool                    hvdcp_disable;
        int                     auto_recharge_soc;
        int                     auto_recharge_vbat_mv;
        int                     wd_bark_time;
        int                     batt_profile_fcc_ua;
        int                     batt_profile_fv_uv;
};

struct smb5 {
        struct smb_charger      chg;
        struct dentry           *dfs_root;
        struct smb_dt_props     dt;
};
#endif

static int __debug_mask;
module_param_named(
        debug_mask, __debug_mask, int, 0600
);

static int __pd_disabled;
module_param_named(
        pd_disabled, __pd_disabled, int, 0600
);

static int __weak_chg_icl_ua = 500000;
module_param_named(
        weak_chg_icl_ua, __weak_chg_icl_ua, int, 0600
);

enum {
        USBIN_CURRENT,
        USBIN_VOLTAGE,
};

#define PMI632_MAX_ICL_UA       3000000
static int smb5_chg_config_init(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        struct pmic_revid_data *pmic_rev_id;
        struct device_node *revid_dev_node;
        int rc = 0;

        revid_dev_node = of_parse_phandle(chip->chg.dev->of_node,
                                          "qcom,pmic-revid", 0);
        if (!revid_dev_node) {
                pr_err("Missing qcom,pmic-revid property\n");
                return -EINVAL;
        }

        pmic_rev_id = get_revid_data(revid_dev_node);
        if (IS_ERR_OR_NULL(pmic_rev_id)) {
                /*
                 * the revid peripheral must be registered, any failure
                 * here only indicates that the rev-id module has not
                 * probed yet.
                 */
                rc =  -EPROBE_DEFER;
                goto out;
        }

        switch (pmic_rev_id->pmic_subtype) {
        case PM855B_SUBTYPE:
                chip->chg.smb_version = PM855B_SUBTYPE;
                chg->param = smb5_pmi855_params;
                chg->name = "pm855b_charger";
                break;
        case PMI632_SUBTYPE:
                chip->chg.smb_version = PMI632_SUBTYPE;
                chg->param = smb5_pmi632_params;
                chg->use_extcon = true;
                chg->name = "pmi632_charger";
                /* PMI632 does not support PD */
                __pd_disabled = 1;
                chg->hw_max_icl_ua =
                        (chip->dt.usb_icl_ua > 0) ? chip->dt.usb_icl_ua
                                                : PMI632_MAX_ICL_UA;
                chg->chg_freq.freq_5V                   = 600;
                chg->chg_freq.freq_6V_8V                = 800;
                chg->chg_freq.freq_9V                   = 1050;
                chg->chg_freq.freq_removal              = 1050;
                chg->chg_freq.freq_below_otg_threshold  = 800;
                chg->chg_freq.freq_above_otg_threshold  = 800;
                break;
        default:
                pr_err("PMIC subtype %d not supported\n",
                                pmic_rev_id->pmic_subtype);
                rc = -EINVAL;
        }

out:
        of_node_put(revid_dev_node);
        return rc;
}

#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/27, sjc Modify for OTG current limit (V3.1)  */
#define MICRO_1P5A              1500000
#else
#define MICRO_1P5A              1000000
#endif

#define MICRO_P1A                       100000
#define OTG_DEFAULT_DEGLITCH_TIME_MS    50
#define MIN_WD_BARK_TIME                16
#define DEFAULT_WD_BARK_TIME            64
#define BITE_WDOG_TIMEOUT_8S            0x3
#define BARK_WDOG_TIMEOUT_MASK          GENMASK(3, 2)
#define BARK_WDOG_TIMEOUT_SHIFT         2
static int smb5_parse_dt(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        struct device_node *node = chg->dev->of_node;
        int rc, byte_len;

        if (!node) {
                pr_err("device tree node missing\n");
                return -EINVAL;
        }

        chg->step_chg_enabled = of_property_read_bool(node,
                                "qcom,step-charging-enable");

        chg->sw_jeita_enabled = of_property_read_bool(node,
                                "qcom,sw-jeita-enable");

        rc = of_property_read_u32(node, "qcom,wd-bark-time-secs",
                                        &chip->dt.wd_bark_time);
        if (rc < 0 || chip->dt.wd_bark_time < MIN_WD_BARK_TIME) {
                chip->dt.wd_bark_time = DEFAULT_WD_BARK_TIME;
        }

        chip->dt.no_battery = of_property_read_bool(node,
                                                "qcom,batteryless-platform");

        rc = of_property_read_u32(node,
                        "qcom,fcc-max-ua", &chip->dt.batt_profile_fcc_ua);
        if (rc < 0) {
                chip->dt.batt_profile_fcc_ua = -EINVAL;
        }

        rc = of_property_read_u32(node,
                                "qcom,fv-max-uv", &chip->dt.batt_profile_fv_uv);
        if (rc < 0) {
                chip->dt.batt_profile_fv_uv = -EINVAL;
        }

        rc = of_property_read_u32(node,
                                "qcom,usb-icl-ua", &chip->dt.usb_icl_ua);
        if (rc < 0) {
                chip->dt.usb_icl_ua = -EINVAL;
        }

        rc = of_property_read_u32(node,
                                "qcom,otg-cl-ua", &chg->otg_cl_ua);
        if (rc < 0) {
                chg->otg_cl_ua = MICRO_1P5A;
        }

        if (of_find_property(node, "qcom,thermal-mitigation", &byte_len)) {
                chg->thermal_mitigation = devm_kzalloc(chg->dev, byte_len,
                        GFP_KERNEL);

                if (chg->thermal_mitigation == NULL) {
                        return -ENOMEM;
                }

                chg->thermal_levels = byte_len / sizeof(u32);
                rc = of_property_read_u32_array(node,
                                "qcom,thermal-mitigation",
                                chg->thermal_mitigation,
                                chg->thermal_levels);
                if (rc < 0) {
                        dev_err(chg->dev,
                                "Couldn't read threm limits rc = %d\n", rc);
                        return rc;
                }
        }

        rc = of_property_read_u32(node, "qcom,float-option",
                                                &chip->dt.float_option);
        if (!rc && (chip->dt.float_option < 0 || chip->dt.float_option > 4)) {
                pr_err("qcom,float-option is out of range [0, 4]\n");
                return -EINVAL;
        }

        chip->dt.hvdcp_disable = of_property_read_bool(node,
                                                "qcom,hvdcp-disable");


        rc = of_property_read_u32(node, "qcom,chg-inhibit-threshold-mv",
                                &chip->dt.chg_inhibit_thr_mv);
        if (!rc && (chip->dt.chg_inhibit_thr_mv < 0 ||
                                chip->dt.chg_inhibit_thr_mv > 300)) {
                pr_err("qcom,chg-inhibit-threshold-mv is incorrect\n");
                return -EINVAL;
        }

        chip->dt.auto_recharge_soc = -EINVAL;
        rc = of_property_read_u32(node, "qcom,auto-recharge-soc",
                                &chip->dt.auto_recharge_soc);
        if (!rc && (chip->dt.auto_recharge_soc < 0 ||
                        chip->dt.auto_recharge_soc > 100)) {
                pr_err("qcom,auto-recharge-soc is incorrect\n");
                return -EINVAL;
        }
        chg->auto_recharge_soc = chip->dt.auto_recharge_soc;

        chip->dt.auto_recharge_vbat_mv = -EINVAL;
        rc = of_property_read_u32(node, "qcom,auto-recharge-vbat-mv",
                                &chip->dt.auto_recharge_vbat_mv);
        if (!rc && (chip->dt.auto_recharge_vbat_mv < 0)) {
                pr_err("qcom,auto-recharge-vbat-mv is incorrect\n");
                return -EINVAL;
        }

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
        if (g_oppo_chip) {
                g_oppo_chip->normalchg_gpio.chargerid_switch_gpio = 
                                of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
                if (g_oppo_chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
                        chg_err("Couldn't read chargerid_switch-gpio rc = %d, chargerid_switch_gpio:%d\n", 
                                        rc, g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
                } else {
                                rc = gpio_request(g_oppo_chip->normalchg_gpio.chargerid_switch_gpio, "charging-switch1-gpio");
                                if (rc) {
                                        chg_err("unable to request chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
                                } else {
                                        smbchg_chargerid_switch_gpio_init(g_oppo_chip);
                                }
                        chg_debug("chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
                }
        }
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Add for using gpio as OTG ID*/
        if (g_oppo_chip) {
                g_oppo_chip->normalchg_gpio.usbid_gpio =
                                of_get_named_gpio(node, "qcom,usbid-gpio", 0);
                if (g_oppo_chip->normalchg_gpio.usbid_gpio <= 0) {
                        chg_err("Couldn't read qcom,usbid-gpio rc=%d, qcom,usbid-gpio:%d\n",
                                        rc, g_oppo_chip->normalchg_gpio.usbid_gpio);
                } else {
                        if (oppo_usbid_check_is_gpio(g_oppo_chip) == true) {
                                rc = gpio_request(g_oppo_chip->normalchg_gpio.usbid_gpio, "usbid-gpio");
                                if (rc) {
                                        chg_err("unable to request usbid-gpio:%d\n",
                                                        g_oppo_chip->normalchg_gpio.usbid_gpio);
                                } else {
                                        oppo_usbid_gpio_init(g_oppo_chip);
                                        oppo_usbid_irq_init(g_oppo_chip);
                                }
                        }
                        chg_debug("usbid-gpio:%d\n", g_oppo_chip->normalchg_gpio.usbid_gpio);
                }
        }
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-15  shortc */
        if (g_oppo_chip) {
                g_oppo_chip->normalchg_gpio.shortc_gpio = 
                                of_get_named_gpio(node, "qcom,shortc-gpio", 0);
                        if (g_oppo_chip->normalchg_gpio.shortc_gpio <= 0) {
                                chg_err("Couldn't read qcom,shortc-gpio rc = %d, qcom,shortc-gpio:%d\n", 
                                                rc, g_oppo_chip->normalchg_gpio.shortc_gpio);
                        } else {
                                if(oppo_shortc_check_is_gpio(g_oppo_chip) == true) {
                                        chg_debug("This project use gpio for shortc hw check\n");
                                        rc = gpio_request(g_oppo_chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
                                        if(rc){
                                                chg_err("unable to request shortc-gpio:%d\n", 
                                                        g_oppo_chip->normalchg_gpio.shortc_gpio);
                                        } else {
                                                oppo_shortc_gpio_init(g_oppo_chip);
                                        }
                                } else {
                                        chg_err("chip->normalchg_gpio.shortc_gpio is not valid or get_PCB_Version() < V0.3:%d\n", 
                                                        get_PCB_Version());
                                }
                                chg_debug("shortc-gpio:%d\n", g_oppo_chip->normalchg_gpio.shortc_gpio);
                        }
        }
#endif

#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/28, sjc Modify for charging */
        chg->dcp_icl_ua = chip->dt.usb_icl_ua;
#else
        chg->dcp_icl_ua = -EINVAL;
#endif

        chg->suspend_input_on_debug_batt = of_property_read_bool(node,
                                        "qcom,suspend-input-on-debug-batt");

        rc = of_property_read_u32(node, "qcom,otg-deglitch-time-ms",
                                        &chg->otg_delay_ms);
        if (rc < 0) {
                chg->otg_delay_ms = OTG_DEFAULT_DEGLITCH_TIME_MS;
        }

        return 0;
}

#define BATIF_ADC_CHANNEL_EN_REG		(BATIF_BASE + 0x82)
#define IBATT_CHANNEL_EN_BIT			BIT(6)
static int smb5_get_adc_data(struct smb_charger *chg, int channel,
				union power_supply_propval *val)
{
	int rc = 0;
	struct qpnp_vadc_result result;
	u8 reg;

	if (!chg->vadc_dev) {
		if (of_find_property(chg->dev->of_node, "qcom,chg-vadc",
					NULL)) {
			chg->vadc_dev = qpnp_get_vadc(chg->dev, "chg");
			if (IS_ERR(chg->vadc_dev)) {
				rc = PTR_ERR(chg->vadc_dev);
				if (rc != -EPROBE_DEFER)
					pr_debug("Failed to find VADC node, rc=%d\n",
							rc);
				else
					chg->vadc_dev = NULL;

				return rc;
			}
		} else {
			return -ENODATA;
		}
	}

	if (IS_ERR(chg->vadc_dev))
		return PTR_ERR(chg->vadc_dev);

	mutex_lock(&chg->vadc_lock);

	switch (channel) {
	case USBIN_VOLTAGE:
		/* Store ADC channel config */
		rc = smblib_read(chg, BATIF_ADC_CHANNEL_EN_REG, &reg);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't read ADC config rc=%d\n", rc);
			goto done;
		}

		/* Disable all ADC channels except IBAT channel */
		rc = smblib_write(chg, BATIF_ADC_CHANNEL_EN_REG,
				IBATT_CHANNEL_EN_BIT);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't write ADC config rc=%d\n", rc);
			goto done;
		}

		rc = qpnp_vadc_read(chg->vadc_dev, VADC_USB_IN_V_DIV_16_PM5,
				&result);
		if (rc < 0)
			pr_err("Failed to read USBIN_V over vadc, rc=%d\n", rc);
		else
			val->intval = result.physical;

		/* Restore ADC channel config */
		rc |= smblib_write(chg, BATIF_ADC_CHANNEL_EN_REG, reg);
		if (rc < 0)
			dev_err(chg->dev,
				"Couldn't write ADC config rc=%d\n", rc);

		break;
	case USBIN_CURRENT:
		rc = qpnp_vadc_read(chg->vadc_dev, VADC_USB_IN_I_PM5, &result);
		if (rc < 0) {
			pr_err("Failed to read USBIN_I over vadc, rc=%d\n", rc);
			goto done;
		}
		val->intval = result.physical;
		break;
	default:
		pr_debug("Invalid channel\n");
		rc = -EINVAL;
		break;
	}

done:
	mutex_unlock(&chg->vadc_lock);
	return rc;
}

/************************
 * USB PSY REGISTRATION *
 ************************/
 
static enum power_supply_property smb5_usb_props[] = {
        POWER_SUPPLY_PROP_PRESENT,
        POWER_SUPPLY_PROP_ONLINE,
        POWER_SUPPLY_PROP_PD_CURRENT_MAX,
        POWER_SUPPLY_PROP_CURRENT_MAX,
        POWER_SUPPLY_PROP_TYPE,
        POWER_SUPPLY_PROP_TYPEC_MODE,
        POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
        POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
        POWER_SUPPLY_PROP_PD_ACTIVE,
        POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
        POWER_SUPPLY_PROP_INPUT_CURRENT_NOW,
        POWER_SUPPLY_PROP_BOOST_CURRENT,
        POWER_SUPPLY_PROP_PE_START,
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        POWER_SUPPLY_PROP_OTG_SWITCH,
        POWER_SUPPLY_PROP_OTG_ONLINE,
#endif
        POWER_SUPPLY_PROP_CTM_CURRENT_MAX,
        POWER_SUPPLY_PROP_HW_CURRENT_MAX,
        POWER_SUPPLY_PROP_REAL_TYPE,
        POWER_SUPPLY_PROP_PR_SWAP,
        POWER_SUPPLY_PROP_PD_VOLTAGE_MAX,
        POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
        POWER_SUPPLY_PROP_SDP_CURRENT_MAX,
        POWER_SUPPLY_PROP_CONNECTOR_TYPE,
        POWER_SUPPLY_PROP_VOLTAGE_MAX,
        POWER_SUPPLY_PROP_SCOPE,
        POWER_SUPPLY_PROP_VOLTAGE_NOW,
        POWER_SUPPLY_PROP_HVDCP_OPTI_ALLOWED,
};

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
bool __attribute__((weak)) oppo_get_otg_switch_status(void)
{
        return false;
}

bool __attribute__((weak)) oppo_get_otg_online_status(void)
{
        return false;
}

void __attribute__((weak)) oppo_set_otg_switch_status(bool value)
{
        printk(KERN_ERR "USE weak oppo_set_otg_switch_status\n");
}
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/26, sjc Add for charging */
static bool use_present_status = false;
#endif
static int smb5_usb_get_prop(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
        struct smb5 *chip = power_supply_get_drvdata(psy);
        struct smb_charger *chg = &chip->chg;
        union power_supply_propval pval;
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_PRESENT:
                rc = smblib_get_prop_usb_present(chg, val);
                break;
        case POWER_SUPPLY_PROP_ONLINE:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/26, sjc Modify for charging */
                if (use_present_status) {
                        rc = smblib_get_prop_usb_present(chg, val);
                } else {
                        rc = smblib_get_prop_usb_online(chg, val);
                }
#else
                rc = smblib_get_prop_usb_online(chg, val);
#endif
                if (!val->intval)
                        break;

#ifndef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-11  avoid not recognize USB */
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                   (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && (chg->real_charger_type == POWER_SUPPLY_TYPE_USB)) {
#else
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                        (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && ((chg->real_charger_type == POWER_SUPPLY_TYPE_USB) ||
                        (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP))) {
#endif
                        val->intval = 0;
                } else {
                        val->intval = 1;
                }

                if (chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
                        val->intval = 0;
                break;
        case POWER_SUPPLY_PROP_VOLTAGE_MAX:
                rc = smblib_get_prop_usb_voltage_max(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
                val->intval = get_client_vote(chg->usb_icl_votable, PD_VOTER);
                break;
        case POWER_SUPPLY_PROP_CURRENT_MAX:
                rc = smblib_get_prop_input_current_settled(chg, val);
                break;
        case POWER_SUPPLY_PROP_TYPE:
                val->intval = POWER_SUPPLY_TYPE_USB_PD;
                break;
        case POWER_SUPPLY_PROP_REAL_TYPE:
                val->intval = chg->real_charger_type;
                break;
        case POWER_SUPPLY_PROP_TYPEC_MODE:
                if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                        val->intval = POWER_SUPPLY_TYPEC_NONE;
                } else {
                        val->intval = chg->typec_mode;
                }
                break;
        case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
                if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                        val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
                } else {
                        rc = smblib_get_prop_typec_power_role(chg, val);
                }
                break;
        case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
                if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                        val->intval = 0;
                } else {
                        rc = smblib_get_prop_typec_cc_orientation(chg, val);
                }
                break;
        case POWER_SUPPLY_PROP_PD_ACTIVE:
                val->intval = chg->pd_active;
                break;
        case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
                rc = smblib_get_prop_input_current_settled(chg, val);
                break;
        case POWER_SUPPLY_PROP_BOOST_CURRENT:
                val->intval = chg->boost_current_ua;
                break;
        case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
                rc = smblib_get_prop_pd_in_hard_reset(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
                val->intval = chg->system_suspend_supported;
                break;
        case POWER_SUPPLY_PROP_PE_START:
                rc = smblib_get_pe_start(chg, val);
                break;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        case POWER_SUPPLY_PROP_OTG_SWITCH:
                val->intval = oppo_get_otg_switch_status();
                break;
        case POWER_SUPPLY_PROP_OTG_ONLINE:
                val->intval = oppo_get_otg_online_status();
                break;
#endif
        case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
                val->intval = get_client_vote(chg->usb_icl_votable, CTM_VOTER);
                break;
        case POWER_SUPPLY_PROP_HW_CURRENT_MAX:
                rc = smblib_get_charge_current(chg, &val->intval);
                break;
        case POWER_SUPPLY_PROP_PR_SWAP:
                rc = smblib_get_prop_pr_swap_in_progress(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
                val->intval = chg->voltage_max_uv;
                break;
        case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
                val->intval = chg->voltage_min_uv;
                break;
        case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
                val->intval = get_client_vote(chg->usb_icl_votable,
                                              USB_PSY_VOTER);
                break;
        case POWER_SUPPLY_PROP_CONNECTOR_TYPE:
                val->intval = chg->connector_type;
                break;
        case POWER_SUPPLY_PROP_SCOPE:
                val->intval = POWER_SUPPLY_SCOPE_UNKNOWN;
                rc = smblib_get_prop_usb_present(chg, &pval);
                if (rc < 0) {
                        break;
                }
                val->intval = pval.intval ? POWER_SUPPLY_SCOPE_DEVICE
                                : chg->otg_present ? POWER_SUPPLY_SCOPE_SYSTEM
                                                : POWER_SUPPLY_SCOPE_UNKNOWN;
                break;
        case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW:
                rc = smblib_get_prop_usb_present(chg, &pval);
                if (rc < 0 || !pval.intval) {
                        val->intval = 0;
                        return rc;
                }
                if (chg->smb_version == PMI632_SUBTYPE) {
                        rc = smb5_get_adc_data(chg, USBIN_CURRENT, val);
                }
                break;
        case POWER_SUPPLY_PROP_VOLTAGE_NOW:
                if (chg->smb_version == PMI632_SUBTYPE) {
                        rc = smb5_get_adc_data(chg, USBIN_VOLTAGE, val);
                }
                break;
        case POWER_SUPPLY_PROP_HVDCP_OPTI_ALLOWED:
                val->intval = !chg->flash_active;
                break;
        default:
                pr_err("get prop %d is not supported in usb\n", psp);
                rc = -EINVAL;
                break;
        }

        if (rc < 0) {
                pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
                return -ENODATA;
        }

        return 0;
}

static int smb5_usb_set_prop(struct power_supply *psy,
                enum power_supply_property psp,
                const union power_supply_propval *val)
{
        struct smb5 *chip = power_supply_get_drvdata(psy);
        struct smb_charger *chg = &chip->chg;
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
                rc = smblib_set_prop_pd_current_max(chg, val);
                break;
        case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
                rc = smblib_set_prop_typec_power_role(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_ACTIVE:
                rc = smblib_set_prop_pd_active(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
                rc = smblib_set_prop_pd_in_hard_reset(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
                chg->system_suspend_supported = val->intval;
                break;
        case POWER_SUPPLY_PROP_BOOST_CURRENT:
                rc = smblib_set_prop_boost_current(chg, val);
                break;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        case POWER_SUPPLY_PROP_OTG_SWITCH:
                oppo_set_otg_switch_status(!!val->intval);
                break;
#endif
        case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
                rc = vote(chg->usb_icl_votable, CTM_VOTER,
                                                val->intval >= 0, val->intval);
                break;
        case POWER_SUPPLY_PROP_PR_SWAP:
                rc = smblib_set_prop_pr_swap_in_progress(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
                rc = smblib_set_prop_pd_voltage_max(chg, val);
                break;
        case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
                rc = smblib_set_prop_pd_voltage_min(chg, val);
                break;
        case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
                rc = smblib_set_prop_sdp_current_max(chg, val);
                break;
        default:
                pr_err("set prop %d is not supported\n", psp);
                rc = -EINVAL;
                break;
        }

        return rc;
}

static int smb5_usb_prop_is_writeable(struct power_supply *psy,
                enum power_supply_property psp)
{
        switch (psp) {
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        case POWER_SUPPLY_PROP_OTG_SWITCH:
#endif
        case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
                return 1;
        default:
                break;
        }

        return 0;
}

static struct power_supply_desc usb_psy_desc = {
        .name = "usb",
#ifndef VENDOR_EDIT
// wenbin.liu@BSP.CHG.Basic, 2017/09/22 
// Delete for usb init unknow 
        .type = POWER_SUPPLY_TYPE_USB_PD,
#else
        .type = POWER_SUPPLY_TYPE_UNKNOWN,
#endif
        .properties = smb5_usb_props,
        .num_properties = ARRAY_SIZE(smb5_usb_props),
        .get_property = smb5_usb_get_prop,
        .set_property = smb5_usb_set_prop,
        .property_is_writeable = smb5_usb_prop_is_writeable,
};

static int smb5_init_usb_psy(struct smb5 *chip)
{
        struct power_supply_config usb_cfg = {};
        struct smb_charger *chg = &chip->chg;

        usb_cfg.drv_data = chip;
        usb_cfg.of_node = chg->dev->of_node;
        chg->usb_psy = devm_power_supply_register(chg->dev,
                                                  &usb_psy_desc,
                                                  &usb_cfg);
        if (IS_ERR(chg->usb_psy)) {
                pr_err("Couldn't register USB power supply\n");
                return PTR_ERR(chg->usb_psy);
        }

        return 0;
}

/********************************
 * USB PC_PORT PSY REGISTRATION *
 ********************************/
 
static enum power_supply_property smb5_usb_port_props[] = {
        POWER_SUPPLY_PROP_TYPE,
        POWER_SUPPLY_PROP_ONLINE,
        POWER_SUPPLY_PROP_VOLTAGE_MAX,
        POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int smb5_usb_port_get_prop(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
        struct smb5 *chip = power_supply_get_drvdata(psy);
        struct smb_charger *chg = &chip->chg;
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_TYPE:
                val->intval = POWER_SUPPLY_TYPE_USB;
                break;
        case POWER_SUPPLY_PROP_ONLINE:
#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-15  recognize CDP */
                if (use_present_status) {
                        rc = smblib_get_prop_usb_present(chg, val);
                } else {
                        rc = smblib_get_prop_usb_online(chg, val);
                }
#else
                rc = smblib_get_prop_usb_online(chg, val);
#endif
                if (!val->intval) {
                        break;
                }

#ifndef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-11  avoid not recognize USB */
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                   (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && (chg->real_charger_type == POWER_SUPPLY_TYPE_USB)) {
#else
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                        (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && ((chg->real_charger_type == POWER_SUPPLY_TYPE_USB) ||
                        (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP))) {
#endif
                        val->intval = 1;
                } else {
                        val->intval = 0;
                }

                break;
        case POWER_SUPPLY_PROP_VOLTAGE_MAX:
                val->intval = 5000000;
                break;
        case POWER_SUPPLY_PROP_CURRENT_MAX:
                rc = smblib_get_prop_input_current_settled(chg, val);
                break;
        default:
                pr_err_ratelimited("Get prop %d is not supported in pc_port\n",
                                psp);
                return -EINVAL;
        }

        if (rc < 0) {
                pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
                return -ENODATA;
        }

        return 0;
}

static int smb5_usb_port_set_prop(struct power_supply *psy,
                enum power_supply_property psp,
                const union power_supply_propval *val)
{
        int rc = 0;

        switch (psp) {
        default:
                pr_err_ratelimited("Set prop %d is not supported in pc_port\n",
                                psp);
                rc = -EINVAL;
                break;
        }

        return rc;
}

static const struct power_supply_desc usb_port_psy_desc = {
        .name           = "pc_port",
        .type           = POWER_SUPPLY_TYPE_USB,
        .properties     = smb5_usb_port_props,
        .num_properties = ARRAY_SIZE(smb5_usb_port_props),
        .get_property   = smb5_usb_port_get_prop,
        .set_property   = smb5_usb_port_set_prop,
};

static int smb5_init_usb_port_psy(struct smb5 *chip)
{
        struct power_supply_config usb_port_cfg = {};
        struct smb_charger *chg = &chip->chg;

        usb_port_cfg.drv_data = chip;
        usb_port_cfg.of_node = chg->dev->of_node;
        chg->usb_port_psy = devm_power_supply_register(chg->dev,
                                                  &usb_port_psy_desc,
                                                  &usb_port_cfg);
        if (IS_ERR(chg->usb_port_psy)) {
                pr_err("Couldn't register USB pc_port power supply\n");
                return PTR_ERR(chg->usb_port_psy);
        }

        return 0;
}

/*****************************
 * USB MAIN PSY REGISTRATION *
 *****************************/

static enum power_supply_property smb5_usb_main_props[] = {
        POWER_SUPPLY_PROP_VOLTAGE_MAX,
        POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
        POWER_SUPPLY_PROP_TYPE,
        POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
        POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED,
        POWER_SUPPLY_PROP_FCC_DELTA,
        POWER_SUPPLY_PROP_CURRENT_MAX,
        POWER_SUPPLY_PROP_FLASH_ACTIVE,
        POWER_SUPPLY_PROP_FLASH_TRIGGER,
};

static int smb5_usb_main_get_prop(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
        struct smb5 *chip = power_supply_get_drvdata(psy);
        struct smb_charger *chg = &chip->chg;
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_VOLTAGE_MAX:
                rc = smblib_get_charge_param(chg, &chg->param.fv, &val->intval);
                break;
        case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
                rc = smblib_get_charge_param(chg, &chg->param.fcc,
                                                        &val->intval);
                break;
        case POWER_SUPPLY_PROP_TYPE:
                val->intval = POWER_SUPPLY_TYPE_MAIN;
                break;
        case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
                rc = smblib_get_prop_input_current_settled(chg, val);
                break;
        case POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED:
                rc = smblib_get_prop_input_voltage_settled(chg, val);
                break;
        case POWER_SUPPLY_PROP_FCC_DELTA:
                rc = smblib_get_prop_fcc_delta(chg, val);
                break;
        case POWER_SUPPLY_PROP_CURRENT_MAX:
                rc = smblib_get_icl_current(chg, &val->intval);
                break;
        case POWER_SUPPLY_PROP_FLASH_ACTIVE:
                val->intval = chg->flash_active;
                break;
        case POWER_SUPPLY_PROP_FLASH_TRIGGER:
                rc = schgm_flash_get_vreg_ok(chg, &val->intval);
                break;
        default:
                pr_debug("get prop %d is not supported in usb-main\n", psp);
                rc = -EINVAL;
                break;
        }
        if (rc < 0) {
                pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
                return -ENODATA;
        }

        return 0;
}

static int smb5_usb_main_set_prop(struct power_supply *psy,
                enum power_supply_property psp,
                const union power_supply_propval *val)
{
        struct smb5 *chip = power_supply_get_drvdata(psy);
        struct smb_charger *chg = &chip->chg;
        union power_supply_propval pval = {0, };
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_VOLTAGE_MAX:
                rc = smblib_set_charge_param(chg, &chg->param.fv, val->intval);
                break;
        case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
                rc = smblib_set_charge_param(chg, &chg->param.fcc, val->intval);
                break;
        case POWER_SUPPLY_PROP_CURRENT_MAX:
                rc = smblib_set_icl_current(chg, val->intval);
                break;
        case POWER_SUPPLY_PROP_FLASH_ACTIVE:
                if ((chg->smb_version == PMI632_SUBTYPE)
                                && (chg->flash_active != val->intval)) {
                        chg->flash_active = val->intval;
#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-28  avoid flash current ripple when flash work */
                        smblib_set_opt_switcher_freq(chg,
                                chg->flash_active ? chg->chg_freq.freq_removal : chg->chg_freq.freq_5V);
#endif
                        rc = smblib_get_prop_usb_present(chg, &pval);
                        if (rc < 0) {
                                pr_err("Failed to get USB preset status rc=%d\n",
                                                rc);
                        }

                        if (pval.intval) {
                                rc = smblib_force_vbus_voltage(chg,
                                        chg->flash_active ? FORCE_5V_BIT
                                                                : IDLE_BIT);
                                if (rc < 0) {
                                        pr_err("Failed to force 5V\n");
                                } else {
                                        chg->pulse_cnt = 0;
                                }
                        }

                        pr_debug("flash active VBUS 5V restriction %s\n",
                                chg->flash_active ? "applied" : "removed");

                        /* Update userspace */
                        if (chg->batt_psy) {
                                power_supply_changed(chg->batt_psy);
                        }
                }
                break;
        default:
                pr_err("set prop %d is not supported\n", psp);
                rc = -EINVAL;
                break;
        }

        return rc;
}

static const struct power_supply_desc usb_main_psy_desc = {
        .name           = "main",
        .type           = POWER_SUPPLY_TYPE_MAIN,
        .properties     = smb5_usb_main_props,
        .num_properties = ARRAY_SIZE(smb5_usb_main_props),
        .get_property   = smb5_usb_main_get_prop,
        .set_property   = smb5_usb_main_set_prop,
};

static int smb5_init_usb_main_psy(struct smb5 *chip)
{
        struct power_supply_config usb_main_cfg = {};
        struct smb_charger *chg = &chip->chg;

        usb_main_cfg.drv_data = chip;
        usb_main_cfg.of_node = chg->dev->of_node;
        chg->usb_main_psy = devm_power_supply_register(chg->dev,
                                                  &usb_main_psy_desc,
                                                  &usb_main_cfg);
        if (IS_ERR(chg->usb_main_psy)) {
                pr_err("Couldn't register USB main power supply\n");
                return PTR_ERR(chg->usb_main_psy);
        }

        return 0;
}

/*************************
 * DC PSY REGISTRATION   *
 *************************/

#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Delete for charging*/
static enum power_supply_property smb5_dc_props[] = {
        POWER_SUPPLY_PROP_INPUT_SUSPEND,
        POWER_SUPPLY_PROP_PRESENT,
        POWER_SUPPLY_PROP_ONLINE,
        POWER_SUPPLY_PROP_REAL_TYPE,
};

static int smb5_dc_get_prop(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
        struct smb5 *chip = power_supply_get_drvdata(psy);
        struct smb_charger *chg = &chip->chg;
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_INPUT_SUSPEND:
                val->intval = get_effective_result(chg->dc_suspend_votable);
                break;
        case POWER_SUPPLY_PROP_PRESENT:
                rc = smblib_get_prop_dc_present(chg, val);
                break;
        case POWER_SUPPLY_PROP_ONLINE:
                rc = smblib_get_prop_dc_online(chg, val);
                break;
        case POWER_SUPPLY_PROP_REAL_TYPE:
                val->intval = POWER_SUPPLY_TYPE_WIPOWER;
                break;
        default:
                return -EINVAL;
        }
        if (rc < 0) {
                pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
                return -ENODATA;
        }
        return 0;
}

static int smb5_dc_set_prop(struct power_supply *psy,
                enum power_supply_property psp,
                const union power_supply_propval *val)
{
        struct smb5 *chip = power_supply_get_drvdata(psy);
        struct smb_charger *chg = &chip->chg;
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_INPUT_SUSPEND:
                rc = vote(chg->dc_suspend_votable, WBC_VOTER,
                                (bool)val->intval, 0);
                break;
        default:
                return -EINVAL;
        }

        return rc;
}

static int smb5_dc_prop_is_writeable(struct power_supply *psy,
                enum power_supply_property psp)
{
        int rc;

        switch (psp) {
        default:
                rc = 0;
                break;
        }

        return rc;
}

static const struct power_supply_desc dc_psy_desc = {
        .name = "dc",
        .type = POWER_SUPPLY_TYPE_WIRELESS,
        .properties = smb5_dc_props,
        .num_properties = ARRAY_SIZE(smb5_dc_props),
        .get_property = smb5_dc_get_prop,
        .set_property = smb5_dc_set_prop,
        .property_is_writeable = smb5_dc_prop_is_writeable,
};

static int smb5_init_dc_psy(struct smb5 *chip)
{
        struct power_supply_config dc_cfg = {};
        struct smb_charger *chg = &chip->chg;

        dc_cfg.drv_data = chip;
        dc_cfg.of_node = chg->dev->of_node;
        chg->dc_psy = devm_power_supply_register(chg->dev,
                                                  &dc_psy_desc,
                                                  &dc_cfg);
        if (IS_ERR(chg->dc_psy)) {
                pr_err("Couldn't register USB power supply\n");
                return PTR_ERR(chg->dc_psy);
        }

        return 0;
}
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/07, sjc Add for charging*/
/*************************
 * AC PSY REGISTRATION *
 *************************/
 
static enum power_supply_property ac_props[] = {
        POWER_SUPPLY_PROP_ONLINE,
};

static int ac_get_property(struct power_supply *psy,
        enum power_supply_property psp,
        union power_supply_propval *val)
{
        int rc = 0;

        if (!g_oppo_chip)
                return -EINVAL;

        if (g_oppo_chip->charger_exist) {
                if ((g_oppo_chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) || (oppo_vooc_get_fastchg_started() == true)
                        || (oppo_vooc_get_fastchg_to_normal() == true) || (oppo_vooc_get_fastchg_to_warm() == true)
                        || (oppo_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) || (oppo_vooc_get_btb_temp_over() == true)) {
                        g_oppo_chip->ac_online = true;
                } else {
                        g_oppo_chip->ac_online = false;
                }
        } else {
                if ((oppo_vooc_get_fastchg_started() == true) || (oppo_vooc_get_fastchg_to_normal() == true)
                        || (oppo_vooc_get_fastchg_to_warm() == true) || (oppo_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE)
                        || (oppo_vooc_get_btb_temp_over() == true) || g_oppo_chip->mmi_fastchg == 0) {
                        g_oppo_chip->ac_online = true;
                } else {
                        g_oppo_chip->ac_online = false;
                }
        }
#ifdef CONFIG_OPPO_CHARGER_MTK
        if (g_oppo_chip->ac_online) {
                chg_debug("chg_exist:%d, ac_online:%d\n",g_oppo_chip->charger_exist,g_oppo_chip->ac_online);
        }
#endif
        switch (psp) {
        case POWER_SUPPLY_PROP_ONLINE:
                val->intval = g_oppo_chip->ac_online;
                 break;
        default:
                rc = -EINVAL;
                break;
        }
        return rc;
}

static const struct power_supply_desc ac_psy_desc = {
        .name = "ac",
        .type = POWER_SUPPLY_TYPE_MAINS,
        .properties = ac_props,
        .num_properties = ARRAY_SIZE(ac_props),
        .get_property = ac_get_property,
};

static int smb5_init_ac_psy(struct smb5 *chip)
{
        struct power_supply_config ac_cfg = {};
        struct smb_charger *chg = &chip->chg;

        ac_cfg.drv_data = chip;
        ac_cfg.of_node = chg->dev->of_node;
        chg->ac_psy = devm_power_supply_register(chg->dev,
                                                  &ac_psy_desc,
                                                  &ac_cfg);
        if (IS_ERR(chg->ac_psy)) {
                chg_err("Couldn't register AC power supply\n");
                return PTR_ERR(chg->ac_psy);
        } else {
                chg_debug("success register AC power supply\n");
        }
        return 0;
}
#endif

/*************************
 * BATT PSY REGISTRATION *
 *************************/
 
static enum power_supply_property smb5_batt_props[] = {
        POWER_SUPPLY_PROP_INPUT_SUSPEND,
        POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
        POWER_SUPPLY_PROP_STATUS,
        POWER_SUPPLY_PROP_HEALTH,
        POWER_SUPPLY_PROP_PRESENT,
        POWER_SUPPLY_PROP_CHARGE_TYPE,
        POWER_SUPPLY_PROP_CAPACITY,
        POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED,
        POWER_SUPPLY_PROP_VOLTAGE_NOW,
        POWER_SUPPLY_PROP_VOLTAGE_MAX,
        POWER_SUPPLY_PROP_CURRENT_NOW,
        POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
        POWER_SUPPLY_PROP_TEMP,
        POWER_SUPPLY_PROP_TECHNOLOGY,
        POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED,
        POWER_SUPPLY_PROP_SW_JEITA_ENABLED,
        POWER_SUPPLY_PROP_CHARGE_DONE,
        POWER_SUPPLY_PROP_PARALLEL_DISABLE,
        POWER_SUPPLY_PROP_SET_SHIP_MODE,
        POWER_SUPPLY_PROP_DIE_HEALTH,
        POWER_SUPPLY_PROP_RERUN_AICL,
        POWER_SUPPLY_PROP_DP_DM,
        POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
        POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
        POWER_SUPPLY_PROP_CHARGE_COUNTER,
        POWER_SUPPLY_PROP_RECHARGE_SOC,
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        POWER_SUPPLY_PROP_CHARGE_NOW,
        POWER_SUPPLY_PROP_AUTHENTICATE,
        POWER_SUPPLY_PROP_CHARGE_TIMEOUT,
        POWER_SUPPLY_PROP_CHARGE_TECHNOLOGY,
        POWER_SUPPLY_PROP_FAST_CHARGE,
        POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE,
        POWER_SUPPLY_PROP_BATTERY_FCC,
        POWER_SUPPLY_PROP_BATTERY_SOH,
        POWER_SUPPLY_PROP_BATTERY_CC,
        POWER_SUPPLY_PROP_BATTERY_RM,
        POWER_SUPPLY_PROP_BATTERY_NOTIFY_CODE,
        POWER_SUPPLY_PROP_ADAPTER_FW_UPDATE,
        POWER_SUPPLY_PROP_VOOCCHG_ING,
        POWER_SUPPLY_PROP_CALL_MODE,
#ifdef CONFIG_OPPO_CHECK_CHARGERID_VOLT
        POWER_SUPPLY_PROP_CHARGERID_VOLT,
#endif
#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        POWER_SUPPLY_PROP_SHIP_MODE,
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE,
        POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE,
        POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS,
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
        POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE,
        POWER_SUPPLY_PROP_SHORT_C_HW_STATUS,
#endif
#endif
};

static int smb5_batt_get_prop(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
        struct smb_charger *chg = power_supply_get_drvdata(psy);
        int rc = 0;

        switch (psp) {
        case POWER_SUPPLY_PROP_STATUS:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        if (oppo_chg_show_vooc_logo_ornot() == 1) {
                                val->intval = POWER_SUPPLY_STATUS_CHARGING;
                        } else if (!g_oppo_chip->authenticate) {
                                val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
                        } else {
                                val->intval = g_oppo_chip->prop_status;
                        } 
                } else {
                        rc = smblib_get_prop_batt_status(chg, val);
                }
#else
                rc = smblib_get_prop_batt_status(chg, val);
#endif
                break;
        case POWER_SUPPLY_PROP_HEALTH:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = oppo_chg_get_prop_batt_health(g_oppo_chip);
                } else {
                        rc = smblib_get_prop_batt_health(chg, val);
                }
#else
                rc = smblib_get_prop_batt_health(chg, val);
#endif
                break;
        case POWER_SUPPLY_PROP_PRESENT:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_exist;
                } else {
                        rc = smblib_get_prop_batt_present(chg, val);
                }
#else
                rc = smblib_get_prop_batt_present(chg, val);
#endif
                break;
        case POWER_SUPPLY_PROP_INPUT_SUSPEND:
                rc = smblib_get_prop_input_suspend(chg, val);
                break;
        case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
                val->intval = !get_client_vote(chg->chg_disable_votable, USER_VOTER);
                break;
        case POWER_SUPPLY_PROP_CHARGE_TYPE:
                rc = smblib_get_prop_batt_charge_type(chg, val);
                break;
        case POWER_SUPPLY_PROP_CAPACITY:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->ui_soc;
                } else {
                        rc = smblib_get_prop_batt_capacity(chg, val);
                }
#else
                rc = smblib_get_prop_batt_capacity(chg, val);
#endif
                break;
        case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
                rc = smblib_get_prop_system_temp_level(chg, val);
                break;
        case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
                rc = smblib_get_prop_system_temp_level_max(chg, val);
                break;
        case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
                rc = smblib_get_prop_input_current_limited(chg, val);
                break;
        case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
                val->intval = chg->step_chg_enabled;
                break;
        case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
                val->intval = chg->sw_jeita_enabled;
                break;
        case POWER_SUPPLY_PROP_VOLTAGE_NOW:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_volt * 1000;
                } else {
                        rc = smblib_get_prop_batt_voltage_now(chg, val);
                } 
#else
                rc = smblib_get_prop_batt_voltage_now(chg, val);
#endif
                break;
        case POWER_SUPPLY_PROP_VOLTAGE_MAX:
                val->intval = get_client_vote(chg->fv_votable,
                                BATT_PROFILE_VOTER);
                break;
        case POWER_SUPPLY_PROP_CURRENT_NOW:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->icharging;
                } else {
                        rc = smblib_get_prop_batt_current_now(chg, val);
                }
#else
                rc = smblib_get_prop_batt_current_now(chg, val);
#endif
                break;
        case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
                val->intval = get_client_vote(chg->fcc_votable,
                                              BATT_PROFILE_VOTER);
                break;
        case POWER_SUPPLY_PROP_TEMP:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->temperature;
                } else {
                        rc = smblib_get_prop_batt_temp(chg, val);
                }
#else
                rc = smblib_get_prop_batt_temp(chg, val);
#endif
                break;
        case POWER_SUPPLY_PROP_TECHNOLOGY:
                val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
                break;
        case POWER_SUPPLY_PROP_CHARGE_DONE:
                rc = smblib_get_prop_batt_charge_done(chg, val);
                break;
        case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
                val->intval = get_client_vote(chg->pl_disable_votable,
                                              USER_VOTER);
                break;
        case POWER_SUPPLY_PROP_SET_SHIP_MODE:
                /* Not in ship mode as long as device is active */
                val->intval = 0;
                break;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        case POWER_SUPPLY_PROP_CHARGE_NOW:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->charger_volt;
                } else {
                        val->intval = 0;
                }
                break;

        case POWER_SUPPLY_PROP_AUTHENTICATE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->authenticate;
                } else {
                        val->intval = true;
                }
                break;

        case POWER_SUPPLY_PROP_CHARGE_TIMEOUT:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->chging_over_time;
                } else {
                        val->intval = false;
                }
                break;

        case POWER_SUPPLY_PROP_CHARGE_TECHNOLOGY:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->vooc_project;
                } else {
                        val->intval = true;
                }
                break;

        case POWER_SUPPLY_PROP_FAST_CHARGE:
                if (g_oppo_chip) {
                        val->intval = oppo_chg_show_vooc_logo_ornot();
                } else {
                        val->intval = 0;
                }
                break;

        case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:     //add for MMI_CHG TEST
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->mmi_chg;
                } else {
                        val->intval = 1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_FCC:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_fcc;
                } else {
                        val->intval = -1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_SOH:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_soh;
                } else {
                        val->intval = -1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_CC:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_cc;
                } else {
                        val->intval = -1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_RM:
                if (g_oppo_chip)
                        val->intval = g_oppo_chip->batt_rm;
                else
                        val->intval = -1;
                break;

        case POWER_SUPPLY_PROP_BATTERY_NOTIFY_CODE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->notify_code;
                } else {
                        val->intval = 0;
                }
                break;

        case POWER_SUPPLY_PROP_ADAPTER_FW_UPDATE:
                val->intval = oppo_vooc_get_adapter_update_status();
                break;

        case POWER_SUPPLY_PROP_VOOCCHG_ING:
                val->intval = oppo_vooc_get_fastchg_ing();
                break;

        case POWER_SUPPLY_PROP_CALL_MODE:
                val->intval = g_oppo_chip->calling_on;
                break;

#ifdef CONFIG_OPPO_CHECK_CHARGERID_VOLT
        case POWER_SUPPLY_PROP_CHARGERID_VOLT:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->chargerid_volt;
                } else {
                        val->intval = 0;
                }
                break;
#endif

#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        case POWER_SUPPLY_PROP_SHIP_MODE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->enable_shipmode;
                }
                break;
#endif

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->short_c_batt.update_change;
                }
                break;

        case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
                if (g_oppo_chip) {
                        val->intval = (int)g_oppo_chip->short_c_batt.in_idle;
                }
                break;

        case POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS:
                if (g_oppo_chip) {
                        val->intval = (int)oppo_short_c_batt_get_cv_status(g_oppo_chip);
                }
                break;
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
                case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
                        if (g_oppo_chip) {
                                val->intval = g_oppo_chip->short_c_batt.is_feature_hw_on;
                        }
                        break;  
                        
                case POWER_SUPPLY_PROP_SHORT_C_HW_STATUS:
                        if (g_oppo_chip) {
                                val->intval = g_oppo_chip->short_c_batt.shortc_gpio_status;
                        }
                        break;
#endif
#endif
        case POWER_SUPPLY_PROP_DIE_HEALTH:
                if (chg->die_health == -EINVAL) {
                        rc = smblib_get_prop_die_health(chg, val);
                } else {
                        val->intval = chg->die_health;
                }
                break;
        case POWER_SUPPLY_PROP_DP_DM:
                val->intval = chg->pulse_cnt;
                break;
        case POWER_SUPPLY_PROP_RERUN_AICL:
                val->intval = 0;
                break;
        case POWER_SUPPLY_PROP_CHARGE_COUNTER:
                rc = smblib_get_prop_batt_charge_counter(chg, val);
                break;
        case POWER_SUPPLY_PROP_RECHARGE_SOC:
                val->intval = chg->auto_recharge_soc;
                break;
        default:
                pr_err("batt power supply prop %d not supported\n", psp);
                return -EINVAL;
        }

        if (rc < 0) {
                pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
                return -ENODATA;
        }

        return 0;
}

static int smb5_batt_set_prop(struct power_supply *psy,
                enum power_supply_property prop,
                const union power_supply_propval *val)
{
        int rc = 0;
        struct smb_charger *chg = power_supply_get_drvdata(psy);
        bool enable;

        switch (prop) {
        case POWER_SUPPLY_PROP_STATUS:
                rc = smblib_set_prop_batt_status(chg, val);
                break;
        case POWER_SUPPLY_PROP_INPUT_SUSPEND:
                rc = smblib_set_prop_input_suspend(chg, val);
                break;
        case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
                vote(chg->chg_disable_votable, USER_VOTER, !val->intval, 0);
                break;
        case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
                rc = smblib_set_prop_system_temp_level(chg, val);
                break;
        case POWER_SUPPLY_PROP_CAPACITY:
                rc = smblib_set_prop_batt_capacity(chg, val);
                break;
        case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
                vote(chg->pl_disable_votable, USER_VOTER, (bool)val->intval, 0);
                break;
        case POWER_SUPPLY_PROP_VOLTAGE_MAX:
                chg->batt_profile_fv_uv = val->intval;
                vote(chg->fv_votable, BATT_PROFILE_VOTER, true, val->intval);
                break;
        case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
                enable = !!val->intval || chg->sw_jeita_enabled;
                rc = smblib_configure_wdog(chg, enable);
                if (rc == 0) {
                        chg->step_chg_enabled = !!val->intval;
                }
                break;
        case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
                if (chg->sw_jeita_enabled != (!!val->intval)) {
                        rc = smblib_disable_hw_jeita(chg, !!val->intval);
                        enable = !!val->intval || chg->step_chg_enabled;
                        rc |= smblib_configure_wdog(chg, enable);
                        if (rc == 0) {
                                chg->sw_jeita_enabled = !!val->intval;
                        }
                }
                break;
        case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
                chg->batt_profile_fcc_ua = val->intval;
                vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, val->intval);
                break;
        case POWER_SUPPLY_PROP_SET_SHIP_MODE:
                /* Not in ship mode as long as the device is active */
                if (!val->intval) {
                        break;
                }
                if (chg->pl.psy) {
                        power_supply_set_property(chg->pl.psy,
                                POWER_SUPPLY_PROP_SET_SHIP_MODE, val);
                }
                rc = smblib_set_prop_ship_mode(chg, val);
                break;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:
                if (g_oppo_chip) {
                        if (val->intval == 0) {
                                chg_debug("mmi_chg: set 0\n");
                                g_oppo_chip->mmi_chg = 0;
                                oppo_chg_turn_off_charging(g_oppo_chip);
                                if (oppo_vooc_get_fastchg_started() == true) {
                                        oppo_chg_set_chargerid_switch_val(0);
                                        oppo_vooc_switch_mode(NORMAL_CHARGER_MODE);
                                        g_oppo_chip->mmi_fastchg = 0;
                                }
                        } else {
                                chg_debug("mmi_chg: set 1\n");
                                g_oppo_chip->mmi_chg = 1;
                                if (g_oppo_chip->mmi_fastchg == 0) {
                                        oppo_chg_clear_chargerid_info();
                                }
                                g_oppo_chip->mmi_fastchg = 1;
                                oppo_chg_turn_on_charging(g_oppo_chip);
                        }
                }
                break;

        case POWER_SUPPLY_PROP_CALL_MODE:
                g_oppo_chip->calling_on = val->intval;
                break;

#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        case POWER_SUPPLY_PROP_SHIP_MODE:
                if (g_oppo_chip) {
                        g_oppo_chip->enable_shipmode = val->intval;
                }
                break;
#endif

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
                if (g_oppo_chip) {
                        chg_debug("[OPPO_CHG] [short_c_batt]: set update change[%d]\n", val->intval);
                        oppo_short_c_batt_update_change(g_oppo_chip, val->intval);
                        g_oppo_chip->short_c_batt.update_change = val->intval;
                }
                break;

        case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
                if (g_oppo_chip) {
                        chg_debug("[short_c_batt]: set in idle[%d]\n", !!val->intval);
                        g_oppo_chip->short_c_batt.in_idle = !!val->intval;
                }
                break;
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
                case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
                        if (g_oppo_chip) {
                                printk(KERN_ERR "[OPPO_CHG] [short_c_hw_check]: set is_feature_hw_on [%d]\n", val->intval);
                                g_oppo_chip->short_c_batt.is_feature_hw_on = val->intval;
                        }
                        break;
        
                case POWER_SUPPLY_PROP_SHORT_C_HW_STATUS:
                        break;
#endif
#endif

        case POWER_SUPPLY_PROP_RERUN_AICL:
                rc = smblib_rerun_aicl(chg);
                break;
        case POWER_SUPPLY_PROP_DP_DM:
                if (!chg->flash_active) {
                        rc = smblib_dp_dm(chg, val->intval);
                }
                break;
        case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
                rc = smblib_set_prop_input_current_limited(chg, val);
                break;
        case POWER_SUPPLY_PROP_DIE_HEALTH:
                chg->die_health = val->intval;
                power_supply_changed(chg->batt_psy);
                break;
        case POWER_SUPPLY_PROP_RECHARGE_SOC:
                if (chg->smb_version == PMI632_SUBTYPE) {
                        /* toggle charging to force recharge */
                        vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER, true, 0);
                        /* charge disable delay */
                        msleep(50);
                        vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER, false, 0);
                }
                break;
        default:
                rc = -EINVAL;
        }

        return rc;
}

static int smb5_batt_prop_is_writeable(struct power_supply *psy,
                enum power_supply_property psp)
{
        switch (psp) {
        case POWER_SUPPLY_PROP_STATUS:
        case POWER_SUPPLY_PROP_INPUT_SUSPEND:
        case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
        case POWER_SUPPLY_PROP_CAPACITY:
        case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
        case POWER_SUPPLY_PROP_DP_DM:
        case POWER_SUPPLY_PROP_RERUN_AICL:
        case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
        case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
        case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
        case POWER_SUPPLY_PROP_DIE_HEALTH:
        case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
                return 1;
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:
        case POWER_SUPPLY_PROP_CALL_MODE:
#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        case POWER_SUPPLY_PROP_SHIP_MODE:
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
        case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
        case POWER_SUPPLY_PROP_SHORT_C_HW_STATUS:
#endif
                return 1;
#endif
        default:
                break;
        }

        return 0;
}

static const struct power_supply_desc batt_psy_desc = {
        .name = "battery",
        .type = POWER_SUPPLY_TYPE_BATTERY,
        .properties = smb5_batt_props,
        .num_properties = ARRAY_SIZE(smb5_batt_props),
        .get_property = smb5_batt_get_prop,
        .set_property = smb5_batt_set_prop,
        .property_is_writeable = smb5_batt_prop_is_writeable,
};

static int smb5_init_batt_psy(struct smb5 *chip)
{
        struct power_supply_config batt_cfg = {};
        struct smb_charger *chg = &chip->chg;
        int rc = 0;

        batt_cfg.drv_data = chg;
        batt_cfg.of_node = chg->dev->of_node;
        chg->batt_psy = devm_power_supply_register(chg->dev,
                                           &batt_psy_desc,
                                           &batt_cfg);
        if (IS_ERR(chg->batt_psy)) {
                pr_err("Couldn't register battery power supply\n");
                return PTR_ERR(chg->batt_psy);
        }

        return rc;
}

/******************************
 * VBUS REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb5_vbus_reg_ops = {
        .enable = smblib_vbus_regulator_enable,
        .disable = smblib_vbus_regulator_disable,
        .is_enabled = smblib_vbus_regulator_is_enabled,
};

static int smb5_init_vbus_regulator(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        struct regulator_config cfg = {};
        int rc = 0;

        chg->vbus_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vbus_vreg),
                                      GFP_KERNEL);
        if (!chg->vbus_vreg) {
                return -ENOMEM;
        }

        cfg.dev = chg->dev;
        cfg.driver_data = chip;

        chg->vbus_vreg->rdesc.owner = THIS_MODULE;
        chg->vbus_vreg->rdesc.type = REGULATOR_VOLTAGE;
        chg->vbus_vreg->rdesc.ops = &smb5_vbus_reg_ops;
        chg->vbus_vreg->rdesc.of_match = "qcom,smb5-vbus";
        chg->vbus_vreg->rdesc.name = "qcom,smb5-vbus";

        chg->vbus_vreg->rdev = devm_regulator_register(chg->dev,
                                                &chg->vbus_vreg->rdesc, &cfg);
        if (IS_ERR(chg->vbus_vreg->rdev)) {
                rc = PTR_ERR(chg->vbus_vreg->rdev);
                chg->vbus_vreg->rdev = NULL;
                if (rc != -EPROBE_DEFER) {
                        pr_err("Couldn't register VBUS regulator rc=%d\n", rc);
                }
        }

        return rc;
}

/******************************
 * VCONN REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb5_vconn_reg_ops = {
        .enable = smblib_vconn_regulator_enable,
        .disable = smblib_vconn_regulator_disable,
        .is_enabled = smblib_vconn_regulator_is_enabled,
};

static int smb5_init_vconn_regulator(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        struct regulator_config cfg = {};
        int rc = 0;

        if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
                return 0;
        }

        chg->vconn_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vconn_vreg),
                                      GFP_KERNEL);
        if (!chg->vconn_vreg) {
                return -ENOMEM;
        }

        cfg.dev = chg->dev;
        cfg.driver_data = chip;

        chg->vconn_vreg->rdesc.owner = THIS_MODULE;
        chg->vconn_vreg->rdesc.type = REGULATOR_VOLTAGE;
        chg->vconn_vreg->rdesc.ops = &smb5_vconn_reg_ops;
        chg->vconn_vreg->rdesc.of_match = "qcom,smb5-vconn";
        chg->vconn_vreg->rdesc.name = "qcom,smb5-vconn";

        chg->vconn_vreg->rdev = devm_regulator_register(chg->dev,
                                                &chg->vconn_vreg->rdesc, &cfg);
        if (IS_ERR(chg->vconn_vreg->rdev)) {
                rc = PTR_ERR(chg->vconn_vreg->rdev);
                chg->vconn_vreg->rdev = NULL;
                if (rc != -EPROBE_DEFER) {
                        pr_err("Couldn't register VCONN regulator rc=%d\n", rc);
                }
        }

        return rc;
}

/***************************
 * HARDWARE INITIALIZATION *
 ***************************/
 
static int smb5_configure_typec(struct smb_charger *chg)
{
        int rc;
        u8 val = 0;

        rc = smblib_read(chg, LEGACY_CABLE_STATUS_REG, &val);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't read Legacy status rc=%d\n", rc);
                return rc;
        }
        /*
         * If Legacy cable is detected re-trigger Legacy detection
         * by disabling/enabling typeC mode.
         */
        if (val & TYPEC_LEGACY_CABLE_STATUS_BIT) {
                rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
                                TYPEC_DISABLE_CMD_BIT, TYPEC_DISABLE_CMD_BIT);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't disable TYPEC rc=%d\n", rc);
                        return rc;
                }

                /* delay before enabling typeC */
                msleep(500);

                rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
                                TYPEC_DISABLE_CMD_BIT, 0);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't enable TYPEC rc=%d\n", rc);
                        return rc;
                }
        }

        /* disable apsd */
        rc = smblib_configure_hvdcp_apsd(chg, false);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't disable APSD rc=%d\n", rc);
                return rc;
        }

        rc = smblib_write(chg, TYPE_C_INTERRUPT_EN_CFG_1_REG,
                                TYPEC_CCOUT_DETACH_INT_EN_BIT |
                                TYPEC_CCOUT_ATTACH_INT_EN_BIT);
        if (rc < 0) {
                dev_err(chg->dev,
                        "Couldn't configure Type-C interrupts rc=%d\n", rc);
                return rc;
        }

        rc = smblib_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
                                TYPEC_WATER_DETECTION_INT_EN_BIT);
        if (rc < 0) {
                dev_err(chg->dev,
                        "Couldn't configure Type-C interrupts rc=%d\n", rc);
                return rc;
        }

        /* configure VCONN for software control */
        rc = smblib_masked_write(chg, TYPE_C_VCONN_CONTROL_REG,
                                 VCONN_EN_SRC_BIT | VCONN_EN_VALUE_BIT,
                                 VCONN_EN_SRC_BIT);
        if (rc < 0) {
                dev_err(chg->dev,
                        "Couldn't configure VCONN for SW control rc=%d\n", rc);
                return rc;
        }

        return rc;
}

static int smb5_configure_micro_usb(struct smb_charger *chg)
{
        int rc;

        rc = smblib_masked_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
                                        MICRO_USB_STATE_CHANGE_INT_EN_BIT,
                                        MICRO_USB_STATE_CHANGE_INT_EN_BIT);
        if (rc < 0) {
                dev_err(chg->dev,
                        "Couldn't configure Type-C interrupts rc=%d\n", rc);
                return rc;
        }

        return rc;
}

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/24, sjc Add for otg id value change support */
static void otg_enable_pmic_id_value (void)
{
        return;
}

static void otg_disable_pmic_id_value (void)
{
        return;
}

void otg_enable_id_value (void)
{
        if (oppo_usbid_check_is_gpio(g_oppo_chip) == true) {
                oppo_set_usbid_active(g_oppo_chip);
                usbid_change_handler(0, g_oppo_chip);
                printk(KERN_ERR "[OPPO_CHG][%s]: usbid_gpio=%d\n",
                                __func__, gpio_get_value(g_oppo_chip->normalchg_gpio.usbid_gpio));
        } else {
                otg_enable_pmic_id_value();
        }
}

void otg_disable_id_value (void)
{
        if (oppo_usbid_check_is_gpio(g_oppo_chip) == true) {
                oppo_set_usbid_sleep(g_oppo_chip);
                usbid_change_handler(0, g_oppo_chip);
                printk(KERN_ERR "[OPPO_CHG][%s]: usbid_gpio=%d\n",
                                __func__, gpio_get_value(g_oppo_chip->normalchg_gpio.usbid_gpio));
        } else {
                otg_disable_pmic_id_value();
        }
}
#endif

static int smb5_init_hw(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        int rc, type = 0;
        u8 val = 0;

        if (chip->dt.no_battery) {
                chg->fake_capacity = 50;
        }

        if (chip->dt.batt_profile_fcc_ua < 0) {
                smblib_get_charge_param(chg, &chg->param.fcc,
                                &chg->batt_profile_fcc_ua);
        }

        if (chip->dt.batt_profile_fv_uv < 0) {
                smblib_get_charge_param(chg, &chg->param.fv,
                                &chg->batt_profile_fv_uv);
        }

        smblib_get_charge_param(chg, &chg->param.usb_icl,
                                &chg->default_icl_ua);

        /* Use SW based VBUS control, disable HW autonomous mode */
        rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
                HVDCP_AUTH_ALG_EN_CFG_BIT | HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT,
                HVDCP_AUTH_ALG_EN_CFG_BIT);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure HVDCP rc=%d\n", rc);
                return rc;
        }

        /*
         * PMI632 can have the connector type defined by a dedicated register
         * TYPEC_MICRO_USB_MODE_REG or by a common TYPEC_U_USB_CFG_REG.
         */
        if (chg->smb_version == PMI632_SUBTYPE) {
                rc = smblib_read(chg, TYPEC_MICRO_USB_MODE_REG, &val);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't read USB mode rc=%d\n", rc);
                        return rc;
                }
                type = !!(val & MICRO_USB_MODE_ONLY_BIT);
        }

        /*
         * If TYPEC_MICRO_USB_MODE_REG is not set and for all non-PMI632
         * check the connector type using TYPEC_U_USB_CFG_REG.
         */
        if (!type) {
                rc = smblib_read(chg, TYPEC_U_USB_CFG_REG, &val);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't read U_USB config rc=%d\n",
                                        rc);
                        return rc;
                }

                type = !!(val & EN_MICRO_USB_MODE_BIT);
        }

        pr_debug("Connector type=%s\n", type ? "Micro USB" : "TypeC");

        if (type) {
                chg->connector_type = POWER_SUPPLY_CONNECTOR_MICRO_USB;
                rc = smb5_configure_micro_usb(chg);
        } else {
                chg->connector_type = POWER_SUPPLY_CONNECTOR_TYPEC;
                rc = smb5_configure_typec(chg);
        }
        if (rc < 0) {
                dev_err(chg->dev,
                        "Couldn't configure TypeC/micro-USB mode rc=%d\n", rc);
                return rc;
        }

        /*
         * PMI632 based hw init:
         * - Rerun APSD to ensure proper charger detection if device
         *   boots with charger connected.
         * - Initialize flash module for PMI632
         */
        if (chg->smb_version == PMI632_SUBTYPE) {
                schgm_flash_init(chg);
                smblib_rerun_apsd_if_required(chg);
        }

        /* vote 0mA on usb_icl for non battery platforms */
        vote(chg->usb_icl_votable,
                DEFAULT_VOTER, chip->dt.no_battery, 0);
        vote(chg->dc_suspend_votable,
                DEFAULT_VOTER, chip->dt.no_battery, 0);
        vote(chg->fcc_votable, HW_LIMIT_VOTER,
                chip->dt.batt_profile_fcc_ua > 0, chip->dt.batt_profile_fcc_ua);
        vote(chg->fv_votable, HW_LIMIT_VOTER,
                chip->dt.batt_profile_fv_uv > 0, chip->dt.batt_profile_fv_uv);
        vote(chg->fcc_votable,
                BATT_PROFILE_VOTER, chg->batt_profile_fcc_ua > 0,
                chg->batt_profile_fcc_ua);
        vote(chg->fv_votable,
                BATT_PROFILE_VOTER, chg->batt_profile_fv_uv > 0,
                chg->batt_profile_fv_uv);

        /* Some h/w limit maximum supported ICL */
        vote(chg->usb_icl_votable, HW_LIMIT_VOTER,
                        chg->hw_max_icl_ua > 0, chg->hw_max_icl_ua);

        /*
         * AICL configuration:
         * AICL ADC disable
         */
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/06, sjc Modify for charging */
        rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
                        SUSPEND_ON_COLLAPSE_USBIN_BIT | USBIN_AICL_START_AT_MAX_BIT
                                | USBIN_AICL_ADC_EN_BIT | USBIN_AICL_RERUN_EN_BIT, USBIN_AICL_RERUN_EN_BIT);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure AICL rc=%d\n", rc);
                return rc;
        }
#else
        if (chg->smb_version != PMI632_SUBTYPE) {
                rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
                                USBIN_AICL_ADC_EN_BIT, 0);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't config AICL rc=%d\n", rc);
                        return rc;
                }
        }
#endif

        /* enable the charging path */
        rc = vote(chg->chg_disable_votable, DEFAULT_VOTER, false, 0);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't enable charging rc=%d\n", rc);
                return rc;
        }

        /* configure VBUS for software control */
        rc = smblib_masked_write(chg, DCDC_OTG_CFG_REG, OTG_EN_SRC_CFG_BIT, 0);
        if (rc < 0) {
                dev_err(chg->dev,
                        "Couldn't configure VBUS for SW control rc=%d\n", rc);
                return rc;
        }

        /*
         * configure the one time watchdong periodic interval and
         * disable "watchdog bite disable charging".
         */
        val = (ilog2(chip->dt.wd_bark_time / 16) << BARK_WDOG_TIMEOUT_SHIFT)
                        & BARK_WDOG_TIMEOUT_MASK;
        val |= BITE_WDOG_TIMEOUT_8S;
        rc = smblib_masked_write(chg, SNARL_BARK_BITE_WD_CFG_REG,
                        BITE_WDOG_DISABLE_CHARGING_CFG_BIT |
                        BARK_WDOG_TIMEOUT_MASK | BITE_WDOG_TIMEOUT_MASK,
                        val);

        if (rc < 0) {
                pr_err("Couldn't configue WD config rc=%d\n", rc);
                return rc;
        }

        /* configure float charger options */
        switch (chip->dt.float_option) {
        case FLOAT_DCP:
                rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
                                FLOAT_OPTIONS_MASK, 0);
                break;
        case FLOAT_SDP:
                rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
                                FLOAT_OPTIONS_MASK, FORCE_FLOAT_SDP_CFG_BIT);
                break;
        case DISABLE_CHARGING:
                rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
                                FLOAT_OPTIONS_MASK, FLOAT_DIS_CHGING_CFG_BIT);
                break;
        case SUSPEND_INPUT:
                rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
                                FLOAT_OPTIONS_MASK, SUSPEND_FLOAT_CFG_BIT);
                break;
        default:
                rc = 0;
                break;
        }

        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure float charger options rc=%d\n",
                        rc);
                return rc;
        }

        rc = smblib_read(chg, USBIN_OPTIONS_2_CFG_REG, &chg->float_cfg);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't read float charger options rc=%d\n",
                        rc);
                return rc;
        }

        switch (chip->dt.chg_inhibit_thr_mv) {
        case 50:
                rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
                                CHARGE_INHIBIT_THRESHOLD_MASK,
                                INHIBIT_ANALOG_VFLT_MINUS_50MV);
                break;
        case 100:
                rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
                                CHARGE_INHIBIT_THRESHOLD_MASK,
                                INHIBIT_ANALOG_VFLT_MINUS_100MV);
                break;
        case 200:
                rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
                                CHARGE_INHIBIT_THRESHOLD_MASK,
                                INHIBIT_ANALOG_VFLT_MINUS_200MV);
                break;
        case 300:
                rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
                                CHARGE_INHIBIT_THRESHOLD_MASK,
                                INHIBIT_ANALOG_VFLT_MINUS_300MV);
                break;
        case 0:
                rc = smblib_masked_write(chg, CHGR_CFG2_REG,
                                CHARGER_INHIBIT_BIT, 0);
        default:
                break;
        }

        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure charge inhibit threshold rc=%d\n",
                        rc);
                return rc;
        }

        rc = smblib_masked_write(chg, CHGR_CFG2_REG, RECHG_MASK,
                                (chip->dt.auto_recharge_vbat_mv != -EINVAL) ?
                                VBAT_BASED_RECHG_BIT : 0);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure VBAT-rechg CHG_CFG2_REG rc=%d\n",
                        rc);
                return rc;
        }

        /* program the auto-recharge VBAT threshold */
        if (chip->dt.auto_recharge_vbat_mv != -EINVAL) {
                u32 temp = VBAT_TO_VRAW_ADC(chip->dt.auto_recharge_vbat_mv);

                temp = ((temp & 0xFF00) >> 8) | ((temp & 0xFF) << 8);
                rc = smblib_batch_write(chg,
                        CHGR_ADC_RECHARGE_THRESHOLD_MSB_REG, (u8 *)&temp, 2);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't configure ADC_RECHARGE_THRESHOLD REG rc=%d\n",
                                rc);
                        return rc;
                }
                /* Program the sample count for VBAT based recharge to 3 */
                rc = smblib_masked_write(chg, CHGR_NO_SAMPLE_TERM_RCHG_CFG_REG,
                                        NO_OF_SAMPLE_FOR_RCHG,
                                        2 << NO_OF_SAMPLE_FOR_RCHG_SHIFT);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't configure CHGR_NO_SAMPLE_FOR_TERM_RCHG_CFG rc=%d\n",
                                rc);
                        return rc;
                }
        }

        rc = smblib_masked_write(chg, CHGR_CFG2_REG, RECHG_MASK,
                                (chip->dt.auto_recharge_soc != -EINVAL) ?
                                SOC_BASED_RECHG_BIT : VBAT_BASED_RECHG_BIT);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure SOC-rechg CHG_CFG2_REG rc=%d\n",
                        rc);
                return rc;
        }

        /* program the auto-recharge threshold */
        if (chip->dt.auto_recharge_soc != -EINVAL) {
                rc = smblib_write(chg, CHARGE_RCHG_SOC_THRESHOLD_CFG_REG,
                                (chip->dt.auto_recharge_soc * 255) / 100);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't configure CHG_RCHG_SOC_REG rc=%d\n",
                                rc);
                        return rc;
                }
                /* Program the sample count for SOC based recharge to 1 */
                rc = smblib_masked_write(chg, CHGR_NO_SAMPLE_TERM_RCHG_CFG_REG,
                                                NO_OF_SAMPLE_FOR_RCHG, 0);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't configure CHGR_NO_SAMPLE_FOR_TERM_RCHG_CFG rc=%d\n",
                                rc);
                        return rc;
                }
        }

        if (chg->sw_jeita_enabled) {
                rc = smblib_disable_hw_jeita(chg, true);
                if (rc < 0) {
                        dev_err(chg->dev, "Couldn't set hw jeita rc=%d\n", rc);
                        return rc;
                }
        }

        rc = smblib_configure_wdog(chg,
                        chg->step_chg_enabled || chg->sw_jeita_enabled);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure watchdog rc=%d\n", rc);
                return rc;
        }

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-11  init JEITA range */
        if (0) {                                      /* when 1k BAT_THERMAL resistance    */
                smblib_write(chg, 0x1094, 0x07);
                smblib_write(chg, 0x1095, 0xF2);       /*      soft hot threshold   90C     */
                smblib_write(chg, 0x1096, 0x5D);
                smblib_write(chg, 0x1097, 0xFC);       /*      soft cold threshold -30C     */
                smblib_write(chg, 0x1098, 0x07);
                smblib_write(chg, 0x1099, 0x67);       /*      hard hot threshold   95C     */
                smblib_write(chg, 0x109A, 0x59);
                smblib_write(chg, 0x109B, 0x68);       /*      hard cold threshold -35C     */
        } else {                                      /* when 5p1k BAT_THERMAL resistance  */
                smblib_write(chg, 0x1094, 0x13);
                smblib_write(chg, 0x1095, 0xBF);       /*      soft hot threshold   90C     */
                smblib_write(chg, 0x1096, 0x5E);
                smblib_write(chg, 0x1097, 0x68);       /*      soft cold threshold -30C     */
                smblib_write(chg, 0x1098, 0x13);
                smblib_write(chg, 0x1099, 0x63);       /*      hard hot threshold   95C     */
                smblib_write(chg, 0x109A, 0x5A);
                smblib_write(chg, 0x109B, 0x12);       /*      hard cold threshold -35C     */
        }
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-08  increase OTG_CURRENT_LIMIT to recognize 500G Seagate disk */
        smblib_write(chg, 0x1152, 0x02);
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-06-27  reduce DCD time */
        rc = smblib_masked_write(chg, 0x1363, 0x20, 0);
	if (rc < 0) {
		chg_err("failed to config DCD time rc = %d\n", rc);
		return rc;
	}
#endif

        return rc;
}

static int smb5_post_init(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        union power_supply_propval pval;
        int rc;

        /*
         * In case the usb path is suspended, we would have missed disabling
         * the icl change interrupt because the interrupt could have been
         * not requested
         */
        rerun_election(chg->usb_icl_votable);

        /* configure power role for dual-role */
        pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
        rc = smblib_set_prop_typec_power_role(chg, &pval);
        if (rc < 0) {
                dev_err(chg->dev, "Couldn't configure DRP role rc=%d\n",
                                rc);
                return rc;
        }

        rerun_election(chg->usb_irq_enable_votable);

        return 0;
}

/****************************
 * DETERMINE INITIAL STATUS *
 ****************************/

static int smb5_determine_initial_status(struct smb5 *chip)
{
        struct smb_irq_data irq_data = {chip, "determine-initial-status"};
        struct smb_charger *chg = &chip->chg;
        union power_supply_propval val;
        int rc;

        rc = smblib_get_prop_usb_present(chg, &val);
        if (rc < 0) {
                pr_err("Couldn't get usb present rc=%d\n", rc);
                return rc;
        }
        chg->early_usb_attach = val.intval;

        if (chg->bms_psy) {
                smblib_suspend_on_debug_battery(chg);
        }

        usb_plugin_irq_handler(0, &irq_data);
        typec_attach_detach_irq_handler(0, &irq_data);
        typec_state_change_irq_handler(0, &irq_data);
        usb_source_change_irq_handler(0, &irq_data);
        chg_state_change_irq_handler(0, &irq_data);
        icl_change_irq_handler(0, &irq_data);
        batt_temp_changed_irq_handler(0, &irq_data);
        wdog_bark_irq_handler(0, &irq_data);
        typec_or_rid_detection_change_irq_handler(0, &irq_data);

        return 0;
}

/**************************
 * INTERRUPT REGISTRATION *
 **************************/

static struct smb_irq_info smb5_irqs[] = {
        /* CHARGER IRQs */
        [CHGR_ERROR_IRQ] = {
                .name           = "chgr-error",
                .handler        = default_irq_handler,
        },
        [CHG_STATE_CHANGE_IRQ] = {
                .name           = "chg-state-change",
                .handler        = chg_state_change_irq_handler,
                .wake           = true,
        },
        [STEP_CHG_STATE_CHANGE_IRQ] = {
                .name           = "step-chg-state-change",
        },
        [STEP_CHG_SOC_UPDATE_FAIL_IRQ] = {
                .name           = "step-chg-soc-update-fail",
        },
        [STEP_CHG_SOC_UPDATE_REQ_IRQ] = {
                .name           = "step-chg-soc-update-req",
        },
        [FG_FVCAL_QUALIFIED_IRQ] = {
                .name           = "fg-fvcal-qualified",
        },
        [VPH_ALARM_IRQ] = {
                .name           = "vph-alarm",
        },
        [VPH_DROP_PRECHG_IRQ] = {
                .name           = "vph-drop-prechg",
        },
        /* DCDC IRQs */
        [OTG_FAIL_IRQ] = {
                .name           = "otg-fail",
                .handler        = default_irq_handler,
        },
        [OTG_OC_DISABLE_SW_IRQ] = {
                .name           = "otg-oc-disable-sw",
        },
        [OTG_OC_HICCUP_IRQ] = {
                .name           = "otg-oc-hiccup",
        },
        [BSM_ACTIVE_IRQ] = {
                .name           = "bsm-active",
        },
        [HIGH_DUTY_CYCLE_IRQ] = {
                .name           = "high-duty-cycle",
                .handler        = high_duty_cycle_irq_handler,
                .wake           = true,
        },
        [INPUT_CURRENT_LIMITING_IRQ] = {
                .name           = "input-current-limiting",
                .handler        = default_irq_handler,
        },
        [CONCURRENT_MODE_DISABLE_IRQ] = {
                .name           = "concurrent-mode-disable",
        },
        [SWITCHER_POWER_OK_IRQ] = {
                .name           = "switcher-power-ok",
                .handler        = switcher_power_ok_irq_handler,
        },
        /* BATTERY IRQs */
        [BAT_TEMP_IRQ] = {
                .name           = "bat-temp",
                .handler        = batt_temp_changed_irq_handler,
                .wake           = true,
        },
        [ALL_CHNL_CONV_DONE_IRQ] = {
                .name           = "all-chnl-conv-done",
        },
        [BAT_OV_IRQ] = {
                .name           = "bat-ov",
                .handler        = batt_psy_changed_irq_handler,
        },
        [BAT_LOW_IRQ] = {
                .name           = "bat-low",
                .handler        = batt_psy_changed_irq_handler,
        },
        [BAT_THERM_OR_ID_MISSING_IRQ] = {
                .name           = "bat-therm-or-id-missing",
                .handler        = batt_psy_changed_irq_handler,
        },
        [BAT_TERMINAL_MISSING_IRQ] = {
                .name           = "bat-terminal-missing",
                .handler        = batt_psy_changed_irq_handler,
        },
        [BUCK_OC_IRQ] = {
                .name           = "buck-oc",
        },
        [VPH_OV_IRQ] = {
                .name           = "vph-ov",
        },
        /* USB INPUT IRQs */
        [USBIN_COLLAPSE_IRQ] = {
                .name           = "usbin-collapse",
                .handler        = default_irq_handler,
        },
        [USBIN_VASHDN_IRQ] = {
                .name           = "usbin-vashdn",
                .handler        = default_irq_handler,
        },
        [USBIN_UV_IRQ] = {
                .name           = "usbin-uv",
                .handler        = usbin_uv_irq_handler,
        },
        [USBIN_OV_IRQ] = {
                .name           = "usbin-ov",
                .handler        = default_irq_handler,
        },
        [USBIN_PLUGIN_IRQ] = {
                .name           = "usbin-plugin",
                .handler        = usb_plugin_irq_handler,
                .wake           = true,
        },
        [USBIN_REVI_CHANGE_IRQ] = {
                .name           = "usbin-revi-change",
        },
        [USBIN_SRC_CHANGE_IRQ] = {
                .name           = "usbin-src-change",
                .handler        = usb_source_change_irq_handler,
                .wake           = true,
        },
        [USBIN_ICL_CHANGE_IRQ] = {
                .name           = "usbin-icl-change",
                .handler        = icl_change_irq_handler,
                .wake           = true,
        },
        /* DC INPUT IRQs */
        [DCIN_VASHDN_IRQ] = {
                .name           = "dcin-vashdn",
        },
        [DCIN_UV_IRQ] = {
                .name           = "dcin-uv",
                .handler        = default_irq_handler,
        },
        [DCIN_OV_IRQ] = {
                .name           = "dcin-ov",
                .handler        = default_irq_handler,
        },
        [DCIN_PLUGIN_IRQ] = {
                .name           = "dcin-plugin",
                .handler        = dc_plugin_irq_handler,
                .wake           = true,
        },
        [DCIN_REVI_IRQ] = {
                .name           = "dcin-revi",
        },
        [DCIN_PON_IRQ] = {
                .name           = "dcin-pon",
                .handler        = default_irq_handler,
        },
        [DCIN_EN_IRQ] = {
                .name           = "dcin-en",
                .handler        = default_irq_handler,
        },
        /* TYPEC IRQs */
        [TYPEC_OR_RID_DETECTION_CHANGE_IRQ] = {
                .name           = "typec-or-rid-detect-change",
                .handler        = typec_or_rid_detection_change_irq_handler,
                .wake           = true,
        },
        [TYPEC_VPD_DETECT_IRQ] = {
                .name           = "typec-vpd-detect",
        },
        [TYPEC_CC_STATE_CHANGE_IRQ] = {
                .name           = "typec-cc-state-change",
                .handler        = typec_state_change_irq_handler,
                .wake           = true,
        },
        [TYPEC_VCONN_OC_IRQ] = {
                .name           = "typec-vconn-oc",
                .handler        = default_irq_handler,
        },
        [TYPEC_VBUS_CHANGE_IRQ] = {
                .name           = "typec-vbus-change",
        },
        [TYPEC_ATTACH_DETACH_IRQ] = {
                .name           = "typec-attach-detach",
                .handler        = typec_attach_detach_irq_handler,
        },
        [TYPEC_LEGACY_CABLE_DETECT_IRQ] = {
                .name           = "typec-legacy-cable-detect",
                .handler        = default_irq_handler,
        },
        [TYPEC_TRY_SNK_SRC_DETECT_IRQ] = {
                .name           = "typec-try-snk-src-detect",
        },
        /* MISCELLANEOUS IRQs */
        [WDOG_SNARL_IRQ] = {
                .name           = "wdog-snarl",
        },
        [WDOG_BARK_IRQ] = {
                .name           = "wdog-bark",
                .handler        = wdog_bark_irq_handler,
                .wake           = true,
        },
        [AICL_FAIL_IRQ] = {
                .name           = "aicl-fail",
        },
        [AICL_DONE_IRQ] = {
                .name           = "aicl-done",
                .handler        = default_irq_handler,
        },
        [SMB_EN_IRQ] = {
                .name           = "smb-en",
        },
        [IMP_TRIGGER_IRQ] = {
                .name           = "imp-trigger",
        },
        [TEMP_CHANGE_IRQ] = {
                .name           = "temp-change",
        },
        [TEMP_CHANGE_SMB_IRQ] = {
                .name           = "temp-change-smb",
        },
        /* FLASH */
        [VREG_OK_IRQ] = {
                .name           = "vreg-ok",
        },
        [ILIM_S2_IRQ] = {
                .name           = "ilim2-s2",
                .handler        = schgm_flash_ilim2_irq_handler,
        },
        [ILIM_S1_IRQ] = {
                .name           = "ilim1-s1",
        },
        [VOUT_DOWN_IRQ] = {
                .name           = "vout-down",
        },
        [VOUT_UP_IRQ] = {
                .name           = "vout-up",
        },
        [FLASH_STATE_CHANGE_IRQ] = {
                .name           = "flash-state-change",
                .handler        = schgm_flash_state_change_irq_handler,
        },
        [TORCH_REQ_IRQ] = {
                .name           = "torch-req",
        },
        [FLASH_EN_IRQ] = {
                .name           = "flash-en",
        },
};

static int smb5_get_irq_index_byname(const char *irq_name)
{
        int i;

        for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
                if (strcmp(smb5_irqs[i].name, irq_name) == 0) {
                        return i;
                }
        }

        return -ENOENT;
}

static int smb5_request_interrupt(struct smb5 *chip,
                                struct device_node *node, const char *irq_name)
{
        struct smb_charger *chg = &chip->chg;
        int rc, irq, irq_index;
        struct smb_irq_data *irq_data;

        irq = of_irq_get_byname(node, irq_name);
        if (irq < 0) {
                pr_err("Couldn't get irq %s byname\n", irq_name);
                return irq;
        }

        irq_index = smb5_get_irq_index_byname(irq_name);
        if (irq_index < 0) {
                pr_err("%s is not a defined irq\n", irq_name);
                return irq_index;
        }

        if (!smb5_irqs[irq_index].handler) {
                return 0;
        }

        irq_data = devm_kzalloc(chg->dev, sizeof(*irq_data), GFP_KERNEL);
        if (!irq_data) {
                return -ENOMEM;
        }

        irq_data->parent_data = chip;
        irq_data->name = irq_name;
        irq_data->storm_data = smb5_irqs[irq_index].storm_data;
        mutex_init(&irq_data->storm_data.storm_lock);

        rc = devm_request_threaded_irq(chg->dev, irq, NULL,
                                        smb5_irqs[irq_index].handler,
                                        IRQF_ONESHOT, irq_name, irq_data);
        if (rc < 0) {
                pr_err("Couldn't request irq %d\n", irq);
                return rc;
        }

        smb5_irqs[irq_index].irq = irq;
        smb5_irqs[irq_index].irq_data = irq_data;
        if (smb5_irqs[irq_index].wake) {
                enable_irq_wake(irq);
        }

        return rc;
}

static int smb5_request_interrupts(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        struct device_node *node = chg->dev->of_node;
        struct device_node *child;
        int rc = 0;
        const char *name;
        struct property *prop;

        for_each_available_child_of_node(node, child) {
                of_property_for_each_string(child, "interrupt-names",
                                            prop, name) {
                        rc = smb5_request_interrupt(chip, child, name);
                        if (rc < 0) {
                                return rc;
                        }
                }
        }
        if (chg->irq_info[USBIN_ICL_CHANGE_IRQ].irq) {
                chg->usb_icl_change_irq_enabled = true;
        }

        return rc;
}

static void smb5_free_interrupts(struct smb_charger *chg)
{
        int i;

        for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
                if (smb5_irqs[i].irq > 0) {
                        if (smb5_irqs[i].wake) {
                                disable_irq_wake(smb5_irqs[i].irq);
                        }

                        devm_free_irq(chg->dev, smb5_irqs[i].irq,
                                                smb5_irqs[i].irq_data);
                }
        }
}

static void smb5_disable_interrupts(struct smb_charger *chg)
{
        int i;

        for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
                if (smb5_irqs[i].irq > 0) {
                        disable_irq(smb5_irqs[i].irq);
                }
        }
}

#if defined(CONFIG_DEBUG_FS)

static int force_batt_psy_update_write(void *data, u64 val)
{
        struct smb_charger *chg = data;

        power_supply_changed(chg->batt_psy);
        return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_batt_psy_update_ops, NULL,
                        force_batt_psy_update_write, "0x%02llx\n");

static int force_usb_psy_update_write(void *data, u64 val)
{
        struct smb_charger *chg = data;

        power_supply_changed(chg->usb_psy);
        return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_usb_psy_update_ops, NULL,
                        force_usb_psy_update_write, "0x%02llx\n");

static int force_dc_psy_update_write(void *data, u64 val)
{
        struct smb_charger *chg = data;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/09, sjc Add for charging */
        if (chg->dc_psy)
#endif
        power_supply_changed(chg->dc_psy);
        return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_dc_psy_update_ops, NULL,
                        force_dc_psy_update_write, "0x%02llx\n");

static void smb5_create_debugfs(struct smb5 *chip)
{
        struct dentry *file;

        chip->dfs_root = debugfs_create_dir("charger", NULL);
        if (IS_ERR_OR_NULL(chip->dfs_root)) {
                pr_err("Couldn't create charger debugfs rc=%ld\n",
                        (long)chip->dfs_root);
                return;
        }

        file = debugfs_create_file("force_batt_psy_update", 0600,
                            chip->dfs_root, chip, &force_batt_psy_update_ops);
        if (IS_ERR_OR_NULL(file)) {
                pr_err("Couldn't create force_batt_psy_update file rc=%ld\n",
                        (long)file);
        }

        file = debugfs_create_file("force_usb_psy_update", 0600,
                            chip->dfs_root, chip, &force_usb_psy_update_ops);
        if (IS_ERR_OR_NULL(file)) {
                pr_err("Couldn't create force_usb_psy_update file rc=%ld\n",
                        (long)file);
        }

        file = debugfs_create_file("force_dc_psy_update", 0600,
                            chip->dfs_root, chip, &force_dc_psy_update_ops);
        if (IS_ERR_OR_NULL(file)) {
                pr_err("Couldn't create force_dc_psy_update file rc=%ld\n",
                        (long)file);
        }
}

#else

static void smb5_create_debugfs(struct smb5 *chip)
{
        /* do nothing */
}

#endif

static int smb5_show_charger_status(struct smb5 *chip)
{
        struct smb_charger *chg = &chip->chg;
        union power_supply_propval val;
        int usb_present, batt_present, batt_health, batt_charge_type;
        int rc;

        rc = smblib_get_prop_usb_present(chg, &val);
        if (rc < 0) {
                pr_err("Couldn't get usb present rc=%d\n", rc);
                return rc;
        }
        usb_present = val.intval;

        rc = smblib_get_prop_batt_present(chg, &val);
        if (rc < 0) {
                pr_err("Couldn't get batt present rc=%d\n", rc);
                return rc;
        }
        batt_present = val.intval;

        rc = smblib_get_prop_batt_health(chg, &val);
        if (rc < 0) {
                pr_err("Couldn't get batt health rc=%d\n", rc);
                val.intval = POWER_SUPPLY_HEALTH_UNKNOWN;
        }
        batt_health = val.intval;

        rc = smblib_get_prop_batt_charge_type(chg, &val);
        if (rc < 0) {
                pr_err("Couldn't get batt charge type rc=%d\n", rc);
                return rc;
        }
        batt_charge_type = val.intval;

        pr_info("SMB5 status - usb:present=%d type=%d batt:present = %d health = %d charge = %d\n",
                usb_present, chg->real_charger_type,
                batt_present, batt_health, batt_charge_type);
        return rc;
}

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/22, sjc Add for dump registers */
static bool show_regs_mask;
static ssize_t show_regs_mask_write(struct file *file, const char __user *buff, size_t count, loff_t *ppos)
{
        char mask[16];

        if (copy_from_user(&mask, buff, count)) {
                chg_err("show_regs_mask_write error.\n");
                return -EFAULT;
        }

        if (strncmp(mask, "1", 1) == 0) {
                show_regs_mask = true;
                chg_debug("Show regs mask enable.\n");
        } else if (strncmp(mask, "0", 1) == 0) {
                show_regs_mask = false;
                chg_debug("Show regs mask disable.\n");
        } else {
                show_regs_mask = false;
                return -EFAULT;
        }

        return count;
}

static const struct file_operations show_regs_mask_fops = {
        .write = show_regs_mask_write,
        .llseek = noop_llseek,
};

static void init_proc_show_regs_mask(void)
{
        if (!proc_create("show_regs_mask", S_IWUSR | S_IWGRP | S_IWOTH, NULL, &show_regs_mask_fops)) {
                printk(KERN_ERR "proc_create show_regs_mask_fops fail\n");
        }
}
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen PSW.BSP.CHG  2018-05-25 Add for show voter */
static bool show_voter_mask;
static ssize_t show_voter_mask_write(struct file *file, const char __user *buff, size_t count, loff_t *ppos)
{
        char mask[16];

        if (copy_from_user(&mask, buff, count)) {
                chg_err("show_voter_mask_write error.\n");
                return -EFAULT;
        }

        if (strncmp(mask, "1", 1) == 0) {
                show_voter_mask = true;
                chg_debug("show voter mask enable.\n");
        } else if (strncmp(mask, "0", 1) == 0) {
                show_voter_mask = false;
                chg_debug("show voter mask disable.\n");
        } else {
                show_voter_mask = false;
                return -EFAULT;
        }

        return count;
}

static const struct file_operations show_voter_mask_fops = {
        .write = show_voter_mask_write,
        .llseek = noop_llseek,
};

static void init_proc_show_voter_mask(void)
{
        if (!proc_create("show_voter_mask", S_IWUSR | S_IWGRP | S_IWOTH, NULL, &show_voter_mask_fops)) {
                printk(KERN_ERR "proc_create show_voter_mask_fops fail\n");
        }
}
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018/04/25  OPPO_CHARGE*/
static int smbchg_usb_suspend_disable(struct oppo_chg_chip *chip);
static int smbchg_usb_suspend_enable(struct oppo_chg_chip *chip);
static int smbchg_charging_enble(struct oppo_chg_chip *chip);
static bool oppo_chg_is_usb_present(void);
static int qpnp_get_prop_charger_voltage_now(void);

#define NUM_MAX_CLIENTS         16
struct client_vote {
        bool    enabled;
        int     value;
};

struct votable {
        const char              *name;
        struct list_head        list;
        struct client_vote      votes[NUM_MAX_CLIENTS];
        int                     num_clients;
        int                     type;
        int                     effective_client_id;
        int                     effective_result;
        struct mutex            vote_lock;
        void                    *data;
        int                     (*callback)(struct votable *votable,
                                                void *data,
                                                int effective_result,
                                                const char *effective_client);
        char                    *client_strs[NUM_MAX_CLIENTS];
        bool                    voted_on;
        struct dentry           *root;
        struct dentry           *status_ent;
        u32                     force_val;
        struct dentry           *force_val_ent;
        bool                    force_active;
        struct dentry           *force_active_ent;
};

static void oppo_show_voter(void)
{
        int i = 0;
        struct votable *votable;

        votable = g_smb_chip->usb_icl_votable;
        for (i = 0; i < votable->num_clients; i++) {
                if (votable->client_strs[i]) {
                        chg_debug("%s: %s:\t\t\ten=%d v=%d\n", votable->name, votable->client_strs[i],
                                votable->votes[i].enabled, votable->votes[i].value);
                }
        }

        votable = g_smb_chip->fcc_votable;
        for (i = 0; i < votable->num_clients; i++) {
                if (votable->client_strs[i]) {
                        chg_debug("%s: %s:\t\t\ten=%d v=%d\n", votable->name, votable->client_strs[i],
                                votable->votes[i].enabled, votable->votes[i].value);
                }
        }

        votable = g_smb_chip->fv_votable;
        for (i = 0; i < votable->num_clients; i++) {
                if (votable->client_strs[i]) {
                        chg_debug("%s: %s:\t\t\ten=%d v=%d\n", votable->name, votable->client_strs[i],
                                votable->votes[i].enabled, votable->votes[i].value);
                }
        }

        return;
}

static void oppo_show_regs(void)
{
        int i, j, k, rc;
        u8 stat[16];
        int base[] = {0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1600, 0xA600};

        for (j = 0; j < 8; j ++) {
                for (i = 0; i < 16; i ++) {
                        for (k = 0; k < 16; k ++) {
                                rc = smblib_read(g_smb_chip, base[j] + (16*i + k), &stat[k]);
                        }
                        chg_debug("0x%04x # [ 0x%02x / 0x%02x / 0x%02x / 0x%02x ], "
                                "[ 0x%02x / 0x%02x / 0x%02x / 0x%02x ], [ 0x%02x / 0x%02x / 0x%02x / 0x%02x ], "
                                "[ 0x%02x / 0x%02x / 0x%02x / 0x%02x ]\n", base[j] + (16*i), stat[0], stat[1],
                                stat[2], stat[3], stat[4], stat[5], stat[6], stat[7], stat[8], stat[9], stat[10], 
                                stat[11], stat[12], stat[13], stat[14], stat[15]);
                }
        }

        return;
}

static void dump_regs(struct oppo_chg_chip *chip)
{
        if (!g_oppo_chip || !g_smb_chip) {
                return;
        }

        /* adjust vfloat */
        if (fv_adjust_enable == true && g_oppo_chip->ui_soc >= 85) {
                if (g_oppo_chip->batt_volt < 4380) {
                        fv_adjust_count ++;
                        if (fv_adjust_count > 3) {
				chg_debug("adjust fv now\n");
                                fv_adjust_count = 0;
                                g_oppo_chip->limits.vfloat_sw_set += 10;
                                if (g_oppo_chip->limits.vfloat_sw_set > 4370) {
                                        g_oppo_chip->limits.vfloat_sw_set = 4370;
                                        goto skip_adjust_fv;
                                }
                                chg_debug("adjust_fv, setting %dmV\n", g_oppo_chip->limits.vfloat_sw_set);
                                vote(g_smb_chip->fv_votable, BATT_PROFILE_VOTER, true, g_oppo_chip->limits.vfloat_sw_set * 1000);
                        }
                } else {
                        fv_adjust_count = 0;
                }
        }

skip_adjust_fv:
        if (show_voter_mask) {
                oppo_show_voter();
        }

        if (show_regs_mask) {
                oppo_show_regs();
        }

        return;
}

static void smbchg_disable_adjust_fv(void)
{
        fv_adjust_enable = false;

        return;
}

static int smbchg_kick_wdt(struct oppo_chg_chip *chip)
{
        return 0;
}

static int oppo_chg_hw_init(struct oppo_chg_chip *chip)
{
        int boot_mode = get_boot_mode();

        if (boot_mode != MSM_BOOT_MODE__RF && boot_mode != MSM_BOOT_MODE__WLAN) {
                smbchg_usb_suspend_disable(chip);
        } else {
                smbchg_usb_suspend_enable(chip);
        }
        smbchg_charging_enble(chip);

        return 0;
}

static int smbchg_set_fastchg_current_raw(struct oppo_chg_chip *chip, int current_ma)
{
        int rc = 0;

        rc = vote(g_smb_chip->fcc_votable, DEFAULT_VOTER,
                        true, current_ma * 1000);
        if (rc < 0) {
                chg_err("Couldn't vote fcc_votable[%d], rc=%d\n", current_ma, rc);
        } else {
                chg_debug("vote fcc_votable[%d], rc = %d\n", current_ma, rc);
        }

        return rc;
}

static void smbchg_set_aicl_point(struct oppo_chg_chip *chip, int vol)
{
        return;
}

static void smbchg_aicl_enable(bool enable)
{
        int rc = 0;

        rc = smblib_masked_write(g_smb_chip, USBIN_AICL_OPTIONS_CFG_REG,
                        USBIN_AICL_EN_BIT, enable ? USBIN_AICL_EN_BIT : 0);
        if (rc < 0) {
                chg_err("Couldn't write USBIN_AICL_OPTIONS_CFG_REG rc=%d\n", rc);
        }
}

static void smbchg_rerun_aicl(struct oppo_chg_chip *chip)
{
        smbchg_aicl_enable(false);

        /* Add a delay so that AICL successfully clears */
        msleep(50);

        smbchg_aicl_enable(true);
}

static bool oppo_chg_is_normal_mode(void)
{
        int boot_mode = get_boot_mode();

        if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN) {
                return false;
        }

        return true;
}


static bool oppo_chg_is_suspend_status(void)
{
        int rc = 0;
        u8 stat;

        if (!g_oppo_chip) {
                return false;
        }

        rc = smblib_read(g_smb_chip, POWER_PATH_STATUS_REG, &stat);
        if (rc < 0) {
                chg_err("oppo_chg_is_suspend_status: Couldn't read POWER_PATH_STATUS rc=%d\n", rc);
                return false;
        }

        return (bool)(stat & USBIN_SUSPEND_STS_BIT);
}

static void oppo_chg_clear_suspend(void)
{
        int rc;

        if (!g_oppo_chip) {
                return;
        }       

        rc = smblib_masked_write(g_smb_chip, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 1);
        if (rc < 0) {
                chg_err("oppo_chg_monitor_work: Couldn't set USBIN_SUSPEND_BIT rc=%d\n", rc);
        }

        msleep(50);

        rc = smblib_masked_write(g_smb_chip, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 0);
        if (rc < 0) {
                chg_err("oppo_chg_monitor_work: Couldn't clear USBIN_SUSPEND_BIT rc=%d\n", rc);
        }
}


static void oppo_chg_check_clear_suspend(void)
{
        use_present_status = true;
        oppo_chg_clear_suspend();
        use_present_status = false;
}


static int usb_icl[] = {
        300, 500, 900, 1200, 1500, 1750, 2000, 3000,
};

#define USBIN_25MA      25000
static int oppo_chg_set_input_current(struct oppo_chg_chip *chip, int current_ma)
{
        int rc = 0, i = 0;
        int chg_vol = 0;
        int aicl_point = 0;

        chg_debug( "AICL setting_value = %d, pre_value = %d\n", current_ma, g_smb_chip->pre_current_ma);

        if (g_smb_chip->pre_current_ma == current_ma) {
                return rc;
        } else {
                g_smb_chip->pre_current_ma = current_ma;
        }

        if (fv_adjust_enable == true && g_oppo_chip->limits.vfloat_sw_set > 4350 && g_oppo_chip->ui_soc >= 85) {
                chg_debug("adjust_fv setting 4350mV\n");
                vote(g_smb_chip->fv_votable, BATT_PROFILE_VOTER, true, 4350 * 1000);
                chip->limits.vfloat_sw_set = 4350;
        }

        if (chip->batt_volt > 4100) {
                aicl_point = 4550;
        } else {
                aicl_point = 4500;
        }

        smbchg_aicl_enable(false);

        if (current_ma < 500) {
                i = 0;
                goto aicl_end;
        }

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
        }

        i = 1;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                chg_debug( "use 500 here\n");
                goto aicl_boost_back;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                chg_debug( "use 500 here\n");
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                chg_debug( "use 500 here\n");
                goto aicl_end;
        } else if (current_ma < 900) {
                goto aicl_end;
        }

        i = 2;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 1;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        } else if (current_ma < 1200) {
                goto aicl_end;
        }

        i = 3;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 1;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        }

        i = 4;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(120);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 2;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 2;
                goto aicl_pre_step;
        } else if (current_ma < 1500) {
                i = i - 1;
                goto aicl_end;
        } else if (current_ma < 2000) {
                goto aicl_end;
        }

        i = 5;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(120);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 2;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 2;
                goto aicl_pre_step;
        }

        i = 6;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 2;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i =  i - 2;
                goto aicl_pre_step;
        } else if (current_ma < 3000) {
                goto aicl_end;
        }

        i = 7;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 1;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        } else if (current_ma >= 3000) {
                goto aicl_end;
        }

aicl_pre_step:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
        smbchg_rerun_aicl(chip);
        return rc;
aicl_end:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
        smbchg_rerun_aicl(chip);
        return rc;
aicl_boost_back:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_boost_back\n", chg_vol, i, usb_icl[i], aicl_point);
        if (g_smb_chip->wa_flags & BOOST_BACK_WA) {
                vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
        }
        smbchg_rerun_aicl(chip);
        return rc;
aicl_suspend:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_suspend\n", chg_vol, i, usb_icl[i], aicl_point);
        oppo_chg_check_clear_suspend();
        smbchg_rerun_aicl(chip);
        return rc;
}


static int smbchg_float_voltage_set(struct oppo_chg_chip *chip, int vfloat_mv)
{
        int rc = 0;

        chg_debug("smbchg_float_voltage_set: vfloat_mv = %d\n", vfloat_mv);
        rc = vote(g_smb_chip->fv_votable, BATT_PROFILE_VOTER, true, vfloat_mv * 1000);
        if (rc < 0) {
                chg_err("Couldn't vote fv_votable[%d], rc=%d\n", vfloat_mv, rc);
        }

        return rc;
}

static int smbchg_term_current_set(struct oppo_chg_chip *chip, int term_current)
{
        int rc = 0;
        u8 val_raw = 0;

        if (term_current < 0 || term_current > 750) {
                term_current = 150;
        }

        val_raw = term_current / 50;
        rc = smblib_masked_write(g_smb_chip, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
                        TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
        if (rc < 0) {
                chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);
        }

        return rc;
}

static int smbchg_charging_enble(struct oppo_chg_chip *chip)
{
        int rc = 0;

        rc = vote(g_smb_chip->chg_disable_votable, DEFAULT_VOTER, false, 0);
        if (rc < 0) {
                chg_err("Couldn't enable charging, rc=%d\n", rc);
        }

        g_smb_chip->pre_current_ma = -1;

        fv_adjust_enable = true;
        fv_adjust_count = 0;

        return rc;
}

static int smbchg_charging_disble(struct oppo_chg_chip *chip)
{
        int rc = 0;

        rc = vote(g_smb_chip->chg_disable_votable, DEFAULT_VOTER,
                        true, 0);
        if (rc < 0) {
                chg_err("Couldn't disable charging, rc=%d\n", rc);
        }

        fv_adjust_enable = false;
        fv_adjust_count = 0;
        
        return rc;
}

static int smbchg_get_charge_enable(struct oppo_chg_chip *chip)
{
        int rc = 0;
        u8 temp = 0;

        rc = smblib_read(g_smb_chip, CHARGING_ENABLE_CMD_REG, &temp);
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

        rc = smblib_set_usb_suspend(g_smb_chip, true);
        if (rc < 0) {
                chg_err("Couldn't write enable to USBIN_SUSPEND_BIT rc=%d\n", rc);
        }

        g_smb_chip->pre_current_ma = -1;

        return rc;
}

static int smbchg_usb_suspend_disable(struct oppo_chg_chip *chip)
{
        int rc = 0;
        int boot_mode = get_boot_mode();

        if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN) {
                chg_debug("RF/WLAN, suspending...\n");
                rc = smblib_set_usb_suspend(g_smb_chip, true);
                if (rc < 0) {
                        chg_err("Couldn't write enable to USBIN_SUSPEND_BIT rc=%d\n", rc);
                }
                return rc;
        }

        rc = smblib_set_usb_suspend(g_smb_chip, false);
        if (rc < 0) {
                chg_err("Couldn't write disable to USBIN_SUSPEND_BIT rc=%d\n", rc);
        }

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

        if (!oppo_chg_is_usb_present()) {
                return 0;
        }

        rc = smblib_read(g_smb_chip, BATTERY_CHARGER_STATUS_1_REG, &stat);
        if (rc < 0) {
                chg_err("Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n", rc);
                return 0;
        }
        stat = stat & BATTERY_CHARGER_STATUS_MASK;

        if (stat == TERMINATE_CHARGE || stat == INHIBIT_CHARGE) {
                return 1;
        }

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

static int oppo_set_chging_term_disable(struct oppo_chg_chip *chip)
{
        return 0;
}

static bool qcom_check_charger_resume(struct oppo_chg_chip *chip)
{
        return true;
}

static int smbchg_get_chargerid_volt(struct oppo_chg_chip *chip)
{
        int rc = 0;
        int mv_chargerid = 0;
        struct qpnp_vadc_result results;
        if(!g_oppo_chip) {
                return 0;
        }
        if(!g_oppo_chip->pmic_spmi.pm8953_vadc_dev) {
                pr_err("%s vadc_dev NULL\n", __func__);
                return 0;
        }
        
        rc = qpnp_vadc_read(g_oppo_chip->pmic_spmi.pm8953_vadc_dev, P_MUX4_1_3, &results);
        if (rc) {
                pr_err("Unable to read mv_chargerid (P_MUX4_1_3)rc=%d\n", rc);
                return 0;
        }
        mv_chargerid = (int)results.physical/1000;

        return mv_chargerid;
}

static int smbchg_chargerid_switch_gpio_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
                chg_err("get normalchg_gpio.pinctrl fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.chargerid_switch_active = 
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_active");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)) {
                chg_err("get chargerid_switch_active fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.chargerid_switch_sleep = 
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_sleep");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
                chg_err("get chargerid_switch_sleep fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.chargerid_switch_default = 
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_default");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
                chg_err("get chargerid_switch_default fail\n");
                return -EINVAL;
        }

        if (chip->normalchg_gpio.chargerid_switch_gpio > 0) {
                gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
        }
        pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.chargerid_switch_default);

        return 0;
}

void smbchg_set_chargerid_switch_val(struct oppo_chg_chip *chip, int value)
{
        if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
                chg_err("chargerid_switch_gpio not exist, return\n");
                return;
        }

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
                chg_err("pinctrl null, return\n");
                return;
        }

        if (oppo_vooc_get_adapter_update_real_status() == ADAPTER_FW_NEED_UPDATE
                || oppo_vooc_get_btb_temp_over() == true) {
                chg_debug("adapter update or btb_temp_over, return\n");
                return;
        }

        if (value) {
                gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
                pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                                chip->normalchg_gpio.chargerid_switch_active);
        } else {
                gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
                pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                                chip->normalchg_gpio.chargerid_switch_sleep);
        }
        chg_debug("set value:%d, gpio_val:%d\n", 
                value, gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio));
}

static int smbchg_get_chargerid_switch_val(struct oppo_chg_chip *chip)
{
        if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
                chg_err("chargerid_switch_gpio not exist, return\n");
                return -1;
        }

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
                chg_err("pinctrl null, return\n");
                return -1;
        }

        return gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio);
}

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Add for using gpio as OTG ID*/
static int oppo_usbid_gpio_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
                chg_err("get normalchg_gpio.pinctrl fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.usbid_active =
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
                                "usbid_active");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.usbid_active)) {
                chg_err("get usbid_active fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.usbid_sleep =
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
                                "usbid_sleep");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.usbid_sleep)) {
                chg_err("get usbid_sleep fail\n");
                return -EINVAL;
        }

        if (chip->normalchg_gpio.usbid_gpio > 0) {
                gpio_direction_output(chip->normalchg_gpio.usbid_gpio, 0);
        }

        pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                chip->normalchg_gpio.usbid_sleep);

        return 0;
}

static void oppo_set_usbid_active(struct oppo_chg_chip *chip)
{
        gpio_direction_input(chip->normalchg_gpio.usbid_gpio);
        pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.usbid_active);
}

static void oppo_set_usbid_sleep(struct oppo_chg_chip *chip)
{
        gpio_direction_input(chip->normalchg_gpio.usbid_gpio);
        pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.usbid_sleep);
}

static void oppo_usbid_irq_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.usbid_irq = gpio_to_irq(chip->normalchg_gpio.usbid_gpio);
}

static void oppo_usbid_irq_register(struct oppo_chg_chip *chip)
{
        int retval = 0;
        union power_supply_propval ret = {0,};
        oppo_set_usbid_active(chip);

        retval = devm_request_threaded_irq(chip->dev, chip->normalchg_gpio.usbid_irq, NULL,
                        usbid_change_handler, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                        "usbid-change", chip);
        if (retval < 0) {
                chg_err("Unable to request usbid-change irq: %d\n", retval);
        }

        power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_OTG_SWITCH, &ret);
        if (ret.intval == false) {
                oppo_set_usbid_sleep(chip);
        }
}

bool oppo_usbid_check_is_gpio(struct oppo_chg_chip *chip)
{
        return true;
}
#endif

static bool oppo_shortc_check_is_gpio(struct oppo_chg_chip *chip)
{
        if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio)) {
                return true;
        }

        return false;
}

static int oppo_shortc_gpio_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
        chip->normalchg_gpio.shortc_active = 
                pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
                        "shortc_active");

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
                chg_err("get shortc_active fail\n");
                return -EINVAL;
        }       

        pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                chip->normalchg_gpio.shortc_active);
        return 0;
}
#ifdef CONFIG_OPPO_SHORT_HW_CHECK       
static bool oppo_chg_get_shortc_hw_gpio_status(struct oppo_chg_chip *chip)
{
        bool shortc_hw_status = 1;

        if(oppo_shortc_check_is_gpio(chip) == true) {
                shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
        }
        return shortc_hw_status;
}
#else
static bool oppo_chg_get_shortc_hw_gpio_status(struct oppo_chg_chip *chip)
{
        bool shortc_hw_status = 1;

        return shortc_hw_status;
}
#endif

static bool smbchg_need_to_check_ibatt(struct oppo_chg_chip *chip)
{
        return true;
}

static int smbchg_get_chg_current_step(struct oppo_chg_chip *chip)
{
        return 25;
}

static int opchg_get_charger_type(void)
{
        u8 apsd_stat;
        int rc;

        if (!g_oppo_chip) {
                return POWER_SUPPLY_TYPE_UNKNOWN;
        }

        /* reset for fastchg to normal */
        if (g_oppo_chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
                g_smb_chip->pre_current_ma = -1;
        }

        rc = smblib_read(g_smb_chip, APSD_STATUS_REG, &apsd_stat);
        if (rc < 0) {
                chg_err("Couldn't read APSD_STATUS rc=%d\n", rc);
                return POWER_SUPPLY_TYPE_UNKNOWN;
        }
        chg_debug("APSD_STATUS = 0x%02x, Chg_Type = %d\n", apsd_stat, g_smb_chip->real_charger_type);

        if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
                return POWER_SUPPLY_TYPE_UNKNOWN;
        }

        if (g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB
                        || g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP
                        || g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
                oppo_chg_soc_update();
        }
        /* wenbin.liu add for avoid sometime charger exist but type not get */
        if (POWER_SUPPLY_TYPE_UNKNOWN == g_smb_chip->real_charger_type) {
                smblib_update_usb_type(g_smb_chip);
                chg_debug("Type Recovey Call, Type = %d\n", g_smb_chip->real_charger_type);
        }

        if (g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
                return POWER_SUPPLY_TYPE_USB;
        }
        
        return g_smb_chip->real_charger_type;
}

static int qpnp_get_prop_charger_voltage_now(void)
{
        int rc = 0;
        int mv_charger = 0;
	union power_supply_propval val;

        if(!g_oppo_chip) {
                return 0;
        }

	rc = smb5_get_adc_data(g_smb_chip, USBIN_VOLTAGE, &val);

        mv_charger = val.intval / 1000;
        g_oppo_chip->charger_volt_pre = mv_charger;

        if (val.intval < 2000 * 1000) {
                 g_smb_chip->pre_current_ma = -1;
         }

        return mv_charger;
}

static bool oppo_chg_is_usb_present(void)
{
        int rc = 0;
        u8 stat = 0;
        bool vbus_rising = false;

        if (!g_oppo_chip || !g_smb_chip) {
                chg_err("Chip not ready\n");
                return false;
        }

        rc = smblib_read(g_smb_chip, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
        if (rc < 0) {
                chg_err("Couldn't read USB_INT_RT_STS, rc=%d\n", rc);
                return false;
        }
        vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);

        if (vbus_rising == false) {
                g_smb_chip->pre_current_ma = -1;
        }

        return vbus_rising;
}

static int qpnp_get_battery_voltage(void)
{
        return 3800;
}

static int oppo_get_boot_mode(void)
{
        return 0;
}

static int smbchg_get_boot_reason(void)
{
        return 0;
}

static int oppo_chg_get_shutdown_soc(void)
{
        int rc, shutdown_soc;
        union power_supply_propval ret = {0, };
        
        if (!g_oppo_chip || !g_smb_chip) {
                chg_err("chip not ready\n");
                return 0;
        }

        rc = g_smb_chip->bms_psy->desc->get_property(g_smb_chip->bms_psy, POWER_SUPPLY_PROP_RESTORE_SOC, &ret);
        if (rc) {
                chg_err("bms psy doesn't support soc restore rc = %d\n", rc);
                goto restore_soc_err;
        }

        shutdown_soc = ret.intval;
        if (shutdown_soc >= 0 && shutdown_soc <= 100) {
                chg_debug("get restored soc = %d\n", shutdown_soc);
                return shutdown_soc;
        } else {
                chg_err("get restored soc = %d\n", shutdown_soc);
                goto restore_soc_err;
        }

restore_soc_err:
        rc = g_smb_chip->bms_psy->desc->get_property(g_smb_chip->bms_psy, POWER_SUPPLY_PROP_CAPACITY, &ret);
        if (rc) {
                chg_err("get soc error, return default 50, rc = %d\n",rc);
                return 50;
        }
        chg_debug("use QG soc = %d\n", ret.intval);

        return ret.intval;
}

static int oppo_chg_backup_soc(int backup_soc)
{
        return 0;
}

static int smbchg_get_aicl_level_ma(struct oppo_chg_chip *chip)
{
        return 0;
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

static int oppo_chg_get_fv(struct oppo_chg_chip *chip)
{
        int flv = chip->limits.temp_normal_vfloat_mv_normalchg;
        int batt_temp = chip->temperature;

        if (batt_temp > chip->limits.hot_bat_decidegc) {//53C
                //default
        } else if (batt_temp >= chip->limits.warm_bat_decidegc) {//45C
                flv = chip->limits.temp_warm_vfloat_mv;
        } else if (batt_temp >= chip->limits.normal_bat_decidegc) {//16C
                flv = chip->limits.temp_normal_vfloat_mv_normalchg;
        } else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {//12C
                flv = chip->limits.temp_little_cool_vfloat_mv;
        } else if (batt_temp >= chip->limits.cool_bat_decidegc) {//5C
                flv = chip->limits.temp_cool_vfloat_mv;
        } else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {//0C
                flv = chip->limits.temp_little_cold_vfloat_mv;
        } else if (batt_temp >= chip->limits.cold_bat_decidegc) {//-3C
                flv = chip->limits.temp_cold_vfloat_mv;
        } else {
                //default
        }

        return flv;
}

static int oppo_chg_get_charging_current(struct oppo_chg_chip *chip)
{
        int charging_current = 0;
        int batt_temp = chip->temperature;

        if (batt_temp > chip->limits.hot_bat_decidegc) {//53C
                charging_current = 0;
        } else if (batt_temp >= chip->limits.warm_bat_decidegc) {//45C
                charging_current = chip->limits.temp_warm_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.normal_bat_decidegc) {//16C
                charging_current = chip->limits.temp_normal_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {//12C
                charging_current = chip->limits.temp_little_cool_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.cool_bat_decidegc) {//5C
                if (chip->batt_volt > 4180) {
                        charging_current = chip->limits.temp_cool_fastchg_current_ma_low;
                }
                else {
                        charging_current = chip->limits.temp_cool_fastchg_current_ma_high;
                }
        } else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {//0C
                charging_current = chip->limits.temp_little_cold_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.cold_bat_decidegc) {//-3C
                charging_current = chip->limits.temp_cold_fastchg_current_ma;
        } else {
                charging_current = 0;
        }

        return charging_current;
}

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oppo_chg_get_dyna_aicl_result(struct oppo_chg_chip *chip)
{
        struct power_supply *usb_psy = NULL;
        union power_supply_propval pval = {0, };

        usb_psy = power_supply_get_by_name("usb");
        if (usb_psy) {
                power_supply_get_property(usb_psy,
                                POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
                                &pval);
                return pval.intval / 1000;
        }

        return 1000;
}
#endif

static int get_current_time(unsigned long *now_tm_sec)
{
        struct rtc_time tm;
        struct rtc_device *rtc;
        int rc;

        rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
        if (rtc == NULL) {
                pr_err("%s: unable to open rtc device (%s)\n",
                        __FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
                return -EINVAL;
        }

        rc = rtc_read_time(rtc, &tm);
        if (rc) {
                pr_err("Error reading rtc device (%s) : %d\n",
                        CONFIG_RTC_HCTOSYS_DEVICE, rc);
                goto close_time;
        }

        rc = rtc_valid_tm(&tm);
        if (rc) {
                pr_err("Invalid RTC time (%s): %d\n",
                        CONFIG_RTC_HCTOSYS_DEVICE, rc);
                goto close_time;
        }
        rtc_tm_to_time(&tm, now_tm_sec);

close_time:
        rtc_class_close(rtc);
        return rc;
}

static unsigned long suspend_tm_sec = 0;
static int smb5_pm_resume(struct device *dev)
{
        int rc = 0;
        signed long resume_tm_sec = 0;
        signed long sleep_time = 0;

        if (!g_oppo_chip || !g_smb_chip) {
                return 0;
                chg_err("Chip not ready\n");
        }

        rc = get_current_time(&resume_tm_sec);
        if (rc || suspend_tm_sec == -1) {
                chg_err("RTC read failed\n");
                sleep_time = 0;
        } else {
                sleep_time = resume_tm_sec - suspend_tm_sec;
        }

        if (sleep_time < 0) {
                sleep_time = 0;
        }

        oppo_chg_soc_update_when_resume(sleep_time);

        return 0;
}

static int smb5_pm_suspend(struct device *dev)
{
        if (!g_oppo_chip || !g_smb_chip) {
                return 0;
                chg_err("Chip not ready\n");
        }

        if (get_current_time(&suspend_tm_sec)) {
                chg_err("RTC read failed\n");
                suspend_tm_sec = -1;
        }

        return 0;
}

static const struct dev_pm_ops smb5_pm_ops = {
        .resume         = smb5_pm_resume,
        .suspend        = smb5_pm_suspend,
};

struct oppo_chg_operations  smb5_chg_ops = {
        .disable_adjust_fv           = smbchg_disable_adjust_fv,
        .dump_registers              = dump_regs,
        .kick_wdt                    = smbchg_kick_wdt,
        .hardware_init               = oppo_chg_hw_init,
        .charging_current_write_fast = smbchg_set_fastchg_current_raw,
        .set_aicl_point              = smbchg_set_aicl_point,
        .input_current_write         = oppo_chg_set_input_current,
        .float_voltage_write         = smbchg_float_voltage_set,
        .term_current_set            = smbchg_term_current_set,
        .charging_enable             = smbchg_charging_enble,
        .charging_disable            = smbchg_charging_disble,
        .get_charging_enable         = smbchg_get_charge_enable,
        .charger_suspend             = smbchg_usb_suspend_enable,
        .charger_unsuspend           = smbchg_usb_suspend_disable,
        .set_rechg_vol               = smbchg_set_rechg_vol,
        .reset_charger               = smbchg_reset_charger,
        .read_full                   = smbchg_read_full,
        .otg_enable                  = smbchg_otg_enable,
        .otg_disable                 = smbchg_otg_disable,
        .set_charging_term_disable   = oppo_set_chging_term_disable,
        .check_charger_resume        = qcom_check_charger_resume,
        .get_chargerid_volt          = smbchg_get_chargerid_volt,
        .set_chargerid_switch_val    = smbchg_set_chargerid_switch_val,
        .get_chargerid_switch_val    = smbchg_get_chargerid_switch_val,
        .need_to_check_ibatt         = smbchg_need_to_check_ibatt,
        .get_chg_current_step        = smbchg_get_chg_current_step,
#ifdef CONFIG_OPPO_CHARGER_MTK
        .get_charger_type            = mt_power_supply_type_check,
        .get_charger_volt            = battery_meter_get_charger_voltage,
        .check_chrdet_status         = pmic_chrdet_status,
        .get_instant_vbatt           = battery_meter_get_battery_voltage,
        .get_boot_mode               = oppo_get_boot_mode,
        .get_boot_reason             = get_boot_reason,
#ifdef CONFIG_MTK_HAFG_20
        .get_rtc_soc                 = get_rtc_spare_oppo_fg_value,
        .set_rtc_soc                 = set_rtc_spare_oppo_fg_value,
#else
        .get_rtc_soc                 = get_rtc_spare_fg_value,
        .set_rtc_soc                 = set_rtc_spare_fg_value,
#endif
        .set_power_off               = mt_power_off,
        .usb_connect                 = mt_usb_connect,
        .usb_disconnect              = mt_usb_disconnect,
#else
        .get_charger_type            = opchg_get_charger_type,
        .get_charger_volt            = qpnp_get_prop_charger_voltage_now,
        .check_chrdet_status         = oppo_chg_is_usb_present,
        .get_instant_vbatt           = qpnp_get_battery_voltage,
        .get_boot_mode               = oppo_get_boot_mode,
        .get_boot_reason             = smbchg_get_boot_reason,
        .get_rtc_soc                 = oppo_chg_get_shutdown_soc,
        .set_rtc_soc                 = oppo_chg_backup_soc,
        .get_aicl_ma                 = smbchg_get_aicl_level_ma,
        .rerun_aicl                  = smbchg_rerun_aicl,
        .tlim_en                     = smbchg_force_tlim_en,
        .set_system_temp_level       = smbchg_system_temp_level_set,
        .otg_pulse_skip_disable      = smbchg_set_prop_flash_active,
        .set_dp_dm                   = smbchg_dp_dm,
        .calc_flash_current          = smbchg_calc_max_flash_current,
#endif
#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
        .check_rtc_reset             = rtc_reset_check,
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        .get_dyna_aicl_result        = oppo_chg_get_dyna_aicl_result,
#endif
        .get_shortc_hw_gpio_status = oppo_chg_get_shortc_hw_gpio_status,
};
#endif

static int smb5_probe(struct platform_device *pdev)
{
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        struct oppo_chg_chip *oppo_chip;
        struct power_supply *main_psy = NULL;
        union power_supply_propval pval = {0, };
#endif
        struct smb5 *chip;
        struct smb_charger *chg;
        int rc = 0;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        if (oppo_gauge_check_chip_is_null()) {
                chg_err("gauge chip null, will do after bettery init.\n");
                return -EPROBE_DEFER;
        }
#endif
        chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
        if (!chip) {
                return -ENOMEM;
        }

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        oppo_chip = devm_kzalloc(&pdev->dev, sizeof(*oppo_chip), GFP_KERNEL);
        if (!oppo_chip) {
                return -ENOMEM;
        }

        oppo_chip->pmic_spmi.smb5_chip = chip;
        oppo_chip->chg_ops = &smb5_chg_ops;
        oppo_chip->dev = &pdev->dev;
        g_oppo_chip = oppo_chip;
        g_smb_chip = &oppo_chip->pmic_spmi.smb5_chip->chg;
#endif

        chg = &chip->chg;
        chg->dev = &pdev->dev;
        chg->debug_mask = &__debug_mask;
        chg->pd_disabled = &__pd_disabled;
        chg->weak_chg_icl_ua = &__weak_chg_icl_ua;
        chg->mode = PARALLEL_MASTER;
        chg->irq_info = smb5_irqs;
        chg->die_health = -EINVAL;
        chg->otg_present = false;
	mutex_init(&chg->vadc_lock);

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/08/10, sjc Add for charging */
        chg->pre_current_ma = -1;
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
        if (of_find_property(oppo_chip->dev->of_node, "qcom,pmi632chg-vadc", NULL)) {
                oppo_chip->pmic_spmi.pmi632_vadc_dev = qpnp_get_vadc(oppo_chip->dev, "pmi632chg");
                if (IS_ERR(oppo_chip->pmic_spmi.pmi632_vadc_dev)) {
                        rc = PTR_ERR(oppo_chip->pmic_spmi.pmi632_vadc_dev);
                        oppo_chip->pmic_spmi.pmi632_vadc_dev = NULL;
                        if (rc != -EPROBE_DEFER) {
                                chg_err("Couldn't get pmi632 vadc rc=%d\n", rc);
                        } else {
                                chg_err("Couldn't get pmi632 vadc, try again...\n");
                                return -EPROBE_DEFER;
                        }
                }
        }

        if (of_find_property(oppo_chip->dev->of_node, "qcom,pm8953chg-vadc", NULL)) {
                oppo_chip->pmic_spmi.pm8953_vadc_dev = qpnp_get_vadc(oppo_chip->dev, "pm8953chg");
                if (IS_ERR(oppo_chip->pmic_spmi.pm8953_vadc_dev)) {
                        rc = PTR_ERR(oppo_chip->pmic_spmi.pm8953_vadc_dev);
                        oppo_chip->pmic_spmi.pm8953_vadc_dev = NULL;
                        if (rc != -EPROBE_DEFER) {
                                chg_err("Couldn't get pm8953 vadc rc=%d\n", rc);
                        } else {
                                chg_err("Couldn't get pm8953 vadc, try again...\n");
                                return -EPROBE_DEFER;
                        }
                }
        }
#endif
        chg->regmap = dev_get_regmap(chg->dev->parent, NULL);
        if (!chg->regmap) {
                pr_err("parent regmap is missing\n");
                return -EINVAL;
        }

        rc = smb5_parse_dt(chip);
        if (rc < 0) {
                pr_err("Couldn't parse device tree rc=%d\n", rc);
                return rc;
        }

        rc = smb5_chg_config_init(chip);
        if (rc < 0) {
                if (rc != -EPROBE_DEFER)
                        pr_err("Couldn't setup chg_config rc=%d\n", rc);
                return rc;
        }

        rc = smblib_init(chg);
        if (rc < 0) {
                pr_err("Smblib_init failed rc=%d\n", rc);
                return rc;
        }

        /* set driver data before resources request it */
        platform_set_drvdata(pdev, chip);

        rc = smb5_init_vbus_regulator(chip);
        if (rc < 0) {
                pr_err("Couldn't initialize vbus regulator rc=%d\n",
                        rc);
                goto cleanup;
        }

        rc = smb5_init_vconn_regulator(chip);
        if (rc < 0) {
                pr_err("Couldn't initialize vconn regulator rc=%d\n",
                                rc);
                goto cleanup;
        }

        /* extcon registration */
        chg->extcon = devm_extcon_dev_allocate(chg->dev, smblib_extcon_cable);
        if (IS_ERR(chg->extcon)) {
                rc = PTR_ERR(chg->extcon);
                dev_err(chg->dev, "failed to allocate extcon device rc=%d\n",
                                rc);
                goto cleanup;
        }

        rc = devm_extcon_dev_register(chg->dev, chg->extcon);
        if (rc < 0) {
                dev_err(chg->dev, "failed to register extcon device rc=%d\n",
                                rc);
                goto cleanup;
        }

        rc = smb5_init_hw(chip);
        if (rc < 0) {
                pr_err("Couldn't initialize hardware rc=%d\n", rc);
                goto cleanup;
        }

#ifndef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Delete for charging*/
        if (chg->smb_version == PM855B_SUBTYPE) {
                rc = smb5_init_dc_psy(chip);
                if (rc < 0) {
                        pr_err("Couldn't initialize dc psy rc=%d\n", rc);
                        goto cleanup;
                }
        }
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/07, sjc Add for charging*/
        rc = smb5_init_ac_psy(chip);
        if (rc < 0) {
                chg_debug("initialize ac psy rc=%d\n", rc);
                goto cleanup;
        } else {
                chg_debug("initialize ac psy rc=%d\n", rc);
        }
#endif
        chg_debug("initialize ac psy rc=%d\n", rc);
        rc = smb5_init_usb_psy(chip);
        if (rc < 0) {
                pr_err("Couldn't initialize usb psy rc=%d\n", rc);
                goto cleanup;
        }

        rc = smb5_init_usb_main_psy(chip);
        if (rc < 0) {
                pr_err("Couldn't initialize usb main psy rc=%d\n", rc);
                goto cleanup;
        }

        rc = smb5_init_usb_port_psy(chip);
        if (rc < 0) {
                pr_err("Couldn't initialize usb pc_port psy rc=%d\n", rc);
                goto cleanup;
        }

        rc = smb5_init_batt_psy(chip);
        if (rc < 0) {
                pr_err("Couldn't initialize batt psy rc=%d\n", rc);
                goto cleanup;
        }

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/11, sjc Add for charging*/
        if (oppo_chg_is_usb_present()) {
                rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
                                CHARGING_ENABLE_CMD_BIT, 0);
                if (rc < 0) {
                        pr_err("Couldn't disable at bootup rc=%d\n", rc);
                }
                msleep(100);
                rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
                                CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
                if (rc < 0) {
                        pr_err("Couldn't enable at bootup rc=%d\n", rc);
                }
        }
#endif

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        oppo_chg_parse_dt(oppo_chip);
        oppo_chg_init(oppo_chip);
        main_psy = power_supply_get_by_name("main");
        if (main_psy) {
                pval.intval = 1000 * oppo_chg_get_fv(oppo_chip);
                chg_debug("init fv = %d\n", pval.intval);
                power_supply_set_property(main_psy,
                                POWER_SUPPLY_PROP_VOLTAGE_MAX,
                                &pval);
                pval.intval = 1000 * oppo_chg_get_charging_current(oppo_chip);
                chg_debug("init current = %d\n", pval.intval);
                power_supply_set_property(main_psy,
                                POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
                                &pval);
        }
#endif

        rc = smb5_determine_initial_status(chip);
        if (rc < 0) {
                pr_err("Couldn't determine initial status rc=%d\n",
                        rc);
                goto cleanup;
        }

        rc = smb5_request_interrupts(chip);
        if (rc < 0) {
                pr_err("Couldn't request interrupts rc=%d\n", rc);
                goto cleanup;
        }

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Add for using gpio as OTG ID*/
        if (oppo_usbid_check_is_gpio(oppo_chip) == true) {
                oppo_usbid_irq_register(oppo_chip);
        }
#endif
        rc = smb5_post_init(chip);
        if (rc < 0) {
                pr_err("Failed in post init rc=%d\n", rc);
                goto free_irq;
        }

        smb5_create_debugfs(chip);

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        g_oppo_chip->authenticate = oppo_gauge_get_batt_authenticate();
        if(!g_oppo_chip->authenticate) {
                smbchg_charging_disble(g_oppo_chip);
        }
        oppo_chg_wake_update_work();
	oppo_tbatt_power_off_task_init(oppo_chip);
#endif
        rc = smb5_show_charger_status(chip);
        if (rc < 0) {
                pr_err("Failed in getting charger status rc=%d\n", rc);
                goto free_irq;
        }

        device_init_wakeup(chg->dev, true);
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/22, sjc Add for dump register */
        init_proc_show_regs_mask();
#endif

#ifdef VENDOR_EDIT
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25 show voter */
        init_proc_show_voter_mask();
#endif

        pr_info("QPNP SMB5 probed successfully\n");

        return rc;

free_irq:
        smb5_free_interrupts(chg);
cleanup:
        smblib_deinit(chg);
        platform_set_drvdata(pdev, NULL);

        return rc;
}

static int smb5_remove(struct platform_device *pdev)
{
        struct smb5 *chip = platform_get_drvdata(pdev);
        struct smb_charger *chg = &chip->chg;

        /* force enable APSD */
        smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
                                BC1P2_SRC_DETECT_BIT, BC1P2_SRC_DETECT_BIT);

        smb5_free_interrupts(chg);
        smblib_deinit(chg);
        platform_set_drvdata(pdev, NULL);
        return 0;
}

static void smb5_shutdown(struct platform_device *pdev)
{
        struct smb5 *chip = platform_get_drvdata(pdev);
        struct smb_charger *chg = &chip->chg;

#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
        if (g_oppo_chip) {
                smbchg_set_chargerid_switch_val(g_oppo_chip, 0);
                msleep(30);
        }
#endif
        /* disable all interrupts */
        smb5_disable_interrupts(chg);

        /* configure power role for UFP */
        if (chg->connector_type == POWER_SUPPLY_CONNECTOR_TYPEC)
                smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
                                TYPEC_POWER_ROLE_CMD_MASK, EN_SNK_ONLY_BIT);

        /* force HVDCP to 5V */
        smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
                                HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT, 0);
        smblib_write(chg, CMD_HVDCP_2_REG, FORCE_5V_BIT);

        /* force enable APSD */
        smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
                                BC1P2_SRC_DETECT_BIT, BC1P2_SRC_DETECT_BIT);
}

static const struct of_device_id match_table[] = {
        { .compatible = "qcom,qpnp-smb5", },
        { },
};

static struct platform_driver smb5_driver = {
        .driver = {
                .name           = "qcom,qpnp-smb5",
                .owner          = THIS_MODULE,
                .of_match_table = match_table,
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/25, sjc Add for charging */
                .pm             = &smb5_pm_ops,
#endif
        },
        .probe          = smb5_probe,
        .remove         = smb5_remove,
        .shutdown       = smb5_shutdown,
};
module_platform_driver(smb5_driver);

MODULE_DESCRIPTION("QPNP SMB5 Charger Driver");
MODULE_LICENSE("GPL v2");
