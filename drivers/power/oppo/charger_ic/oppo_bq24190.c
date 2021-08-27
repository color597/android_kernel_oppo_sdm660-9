/************************************************************************************
** File:  \\192.168.144.3\Linux_Share\12015\ics2\development\mediatek\custom\oppo77_12015\kernel\battery\battery
** VENDOR_EDIT
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
**
** Description:
**      for dc-dc sn111008 charg
**
** Version: 1.0
** Date created: 21:03:46,05/04/2012
** Author: Fanhong.Kong@ProDrv.CHG
**
** --------------------------- Revision History: ------------------------------------------------------------
* <version>       <date>        <author>              			<desc>
* Revision 1.0    2015-06-22    Fanhong.Kong@ProDrv.CHG   		Created for new architecture
************************************************************************************************************/

#include <linux/interrupt.h>
#include <linux/i2c.h>

#ifdef CONFIG_OPPO_CHARGER_MTK
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <linux/xlog.h>
#ifdef CONFIG_OPPO_CHARGER_6750T
#include <linux/module.h>
#include <upmu_common.h>
#include <mt-plat/mt_gpio.h>
#include <mt_boot_common.h>
#include <mt-plat/mtk_rtc.h>
#elif defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
#include <linux/module.h>
#include <upmu_common.h>
#include <mt-plat/mtk_gpio.h>
#include <mtk_boot_common.h>
#include <mt-plat/mtk_rtc.h>
#include <mt-plat/charging.h>
#else
#include <mach/mt_typedefs.h>
#include <mach/mt_gpio.h>
#include <mach/mt_pm_ldo.h>
#include <mach/mt_boot.h>
#include <mach/mtk_rtc.h>
#endif

#if defined CONFIG_OPPO_CHARGER_MTK6763 || defined(CONFIG_OPPO_CHARGER_MTK6771)
#include <soc/oppo/device_info.h>
#else
#include <soc/oppo/device_info.h>
#endif


extern void mt_power_off(void);
#else

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>

#include <mach/oppo_boot_mode.h>
#include <soc/oppo/device_info.h>

void (*enable_aggressive_segmentation_fn)(bool);

#endif

#include "../oppo_vooc.h"
#include "../oppo_gauge.h"
#include <oppo_bq24190.h>
#include "oppo_bq2202a.h"
/* Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2017/11/27, add for mt6771 charger */
extern int oppo_get_rtc_ui_soc(void);
extern int oppo_set_rtc_ui_soc(int value);
static struct oppo_chg_chip *bq24190_chip = NULL;
static int aicl_result = 500;
/* Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2017/12/12, Add for charger modefy */
static struct delayed_work charger_modefy_work;

static DEFINE_MUTEX(bq24190_i2c_access);

static int __bq24190_read_reg(struct oppo_chg_chip *chip, int reg, int *returnData)
{
    #ifdef 	CONFIG_OPPO_CHARGER_MTK
    #if defined CONFIG_OPPO_CHARGER_MTK6763  || defined(CONFIG_OPPO_CHARGER_MTK6771)
	int ret = 0;

    ret = i2c_smbus_read_byte_data(chip->client, reg);
    if (ret < 0) {
        chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
        return ret;
    } else {
        *returnData = ret;
    }
	#else
    char cmd_buf[1] = {0x00};
    char readData = 0;
    int ret = 0;

    chip->client->ext_flag = ((chip->client->ext_flag ) & I2C_MASK_FLAG ) | I2C_WR_FLAG | I2C_DIRECTION_FLAG;
    chip->client->timing = 300;
    cmd_buf[0] = reg;
    ret = i2c_master_send(chip->client, &cmd_buf[0], (1<<8 | 1));
    if (ret < 0) {
        chip->client->ext_flag=0;
        return ret;
    }

    readData = cmd_buf[0];
    *returnData = readData;

    chip->client->ext_flag = 0;
	#endif
    #else
    int ret = 0;

    ret = i2c_smbus_read_byte_data(chip->client, reg);
    if (ret < 0) {
        chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
        return ret;
    } else {
        *returnData = ret;
    }
    #endif /* CONFIG_OPPO_CHARGER_MTK */

    return 0;
}

static int bq24190_read_reg(struct oppo_chg_chip *chip, int reg, int *returnData)
{
	int ret = 0;

	mutex_lock(&bq24190_i2c_access);
	ret = __bq24190_read_reg(chip, reg, returnData);
	mutex_unlock(&bq24190_i2c_access);
	return ret;
}

static int __bq24190_write_reg(struct oppo_chg_chip *chip, int reg, int val)
{
    #ifdef CONFIG_OPPO_CHARGER_MTK
    #if defined CONFIG_OPPO_CHARGER_MTK6763 || defined(CONFIG_OPPO_CHARGER_MTK6771)
	int ret = 0;

    ret = i2c_smbus_write_byte_data(chip->client, reg, val);
    if (ret < 0) {
        chg_err("i2c write fail: can't write %02x to %02x: %d\n",
        val, reg, ret);
        return ret;
    }
	#else
    char write_data[2] = {0};
    int ret = 0;

    write_data[0] = reg;
    write_data[1] = val;

    chip->client->ext_flag = ((chip->client->ext_flag ) & I2C_MASK_FLAG ) | I2C_DIRECTION_FLAG;
    chip->client->timing = 300;
    ret = i2c_master_send(chip->client, write_data, 2);
    if (ret < 0) {

        chip->client->ext_flag = 0;
        return ret;
    }

    chip->client->ext_flag = 0;
	#endif
	#else /* CONFIG_OPPO_CHARGER_MTK */
    int ret = 0;

    ret = i2c_smbus_write_byte_data(chip->client, reg, val);
    if (ret < 0) {
        chg_err("i2c write fail: can't write %02x to %02x: %d\n",
        val, reg, ret);
        return ret;
    }
	#endif /* CONFIG_OPPO_CHARGER_MTK */

	return 0;
}


static int bq24190_config_interface (struct oppo_chg_chip *chip, int RegNum, int val, int MASK)
{
    int bq24190_reg = 0;
    int ret = 0;

	mutex_lock(&bq24190_i2c_access);
    ret = __bq24190_read_reg(chip, RegNum, &bq24190_reg);

    //chg_err(" Reg[%x]=0x%x\n", RegNum, bq24190_reg);

    bq24190_reg &= ~MASK;
    bq24190_reg |= val;

    ret = __bq24190_write_reg(chip, RegNum, bq24190_reg);

    //chg_err(" write Reg[%x]=0x%x\n", RegNum, bq24190_reg);

    __bq24190_read_reg(chip, RegNum, &bq24190_reg);

    //chg_err(" Check Reg[%x]=0x%x\n", RegNum, bq24190_reg);
	mutex_unlock(&bq24190_i2c_access);

    return ret;
}

static int bq24190_usbin_input_current_limit[] = {
    100,    150,    500,    900,
    1200,   1500,   2000,   3000,
};
#if defined(CONFIG_OPPO_CHARGER_MTK6763)  || defined(CONFIG_OPPO_CHARGER_MTK6771)
#define AICL_COUNT 10
static int chg_vol_mini(int *chg_vol)
{
	int chg_vol_temp = 0;
	int i = 0;
	chg_vol_temp = chg_vol[0];
	for(i = 0; i < AICL_COUNT; i++) {
		if(chg_vol_temp > chg_vol[i]){
			chg_vol_temp = chg_vol[i];
		}
	}
	return chg_vol_temp;
}
#endif
#if defined(CONFIG_MTK_PMIC_CHIP_MT6353) || defined(CONFIG_OPPO_CHARGER_MTK6763)  || defined(CONFIG_OPPO_CHARGER_MTK6771)
static int bq24190_input_current_limit_write(struct oppo_chg_chip *chip, int value)
{
	int rc = 0;
	int i = 0;
	int j = 0;
	int chg_vol = 0;
	int aicl_point_temp = 0;
	int chg_vol_all[10] = {0};

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	chg_debug( "usb input max current limit=%d setting %02x\n", value, i);

	aicl_point_temp = chip->sw_aicl_point;
/*
	for (j = 2; j < ARRAY_SIZE(bq24190_usbin_input_current_limit); j++) {
		if (j == 4) //We DO NOT use 1.2A here
			continue;
		else if (j == 5)
			aicl_point_temp = chip->sw_aicl_point - 30;
		else if (j == 6)
			aicl_point_temp = chip->sw_aicl_point - 50;
		bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
		msleep(90);
		chg_vol = battery_meter_get_charger_voltage();
		if (chg_vol < aicl_point_temp) {
			if (j > 2)
				j = j - 1;
		}
		if (bq24190_usbin_input_current_limit[j] >= 3000 || value < bq24190_usbin_input_current_limit[j + 1]) {
			goto aicl_end;
		}
	}
*/
	if (value < 150) {
		j = 0;
		goto aicl_end;
	} else if (value < 500) {
		j = 1;
		goto aicl_end;
	}

	j = 2; /* 500 */
	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point_temp) {
		j = 2;
		goto aicl_pre_step;
	} else if (value < 900)
		goto aicl_end;

	j = 3; /* 900 */
	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point_temp) {
		j = j - 1;
		goto aicl_pre_step;
	} else if (value < 1200)
		goto aicl_end;

	j = 5; /* 1500 */
	#if defined(CONFIG_OPPO_CHARGER_MTK6763)  || defined(CONFIG_OPPO_CHARGER_MTK6771)
	aicl_point_temp = chip->sw_aicl_point;
	#else
	aicl_point_temp = chip->sw_aicl_point;
	#endif
	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point_temp) {
		j = j - 2; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (value < 1500) {
		j = j - 1; //We use 1.2A here
		goto aicl_end;
	} else if (value < 2000)
		goto aicl_end;

	j = 6; /* 2000 */
	#if defined(CONFIG_OPPO_CHARGER_MTK6763)  || defined(CONFIG_OPPO_CHARGER_MTK6771)
	aicl_point_temp = chip->sw_aicl_point;
	#else
	aicl_point_temp = chip->sw_aicl_point;
	#endif
	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	#if defined(CONFIG_OPPO_CHARGER_MTK6763)  || defined(CONFIG_OPPO_CHARGER_MTK6771)
	for(i = 0; i < AICL_COUNT; i++){
		chg_vol = battery_meter_get_charger_voltage();
		chg_vol_all[i] = chg_vol;
		usleep_range(5000, 5000);
	}
	chg_vol = chg_vol_mini(chg_vol_all);
	#else
	chg_vol = battery_meter_get_charger_voltage();
	#endif
	if (chg_vol < aicl_point_temp) {
		j = j - 1;
		goto aicl_pre_step;
	} else if (value < 3000)
		goto aicl_end;

	j = 7; /* 3000 */
	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point_temp) {
		j = j - 1;
		goto aicl_pre_step;
	} else if (value >= 3000)
		goto aicl_end;

aicl_pre_step:
	if((j >= 2) && (j <= ARRAY_SIZE(bq24190_usbin_input_current_limit) - 1))
		aicl_result = bq24190_usbin_input_current_limit[j];		
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, j, bq24190_usbin_input_current_limit[j], aicl_point_temp);
	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
	return rc;
aicl_end:
if((j >= 2) && (j <= ARRAY_SIZE(bq24190_usbin_input_current_limit) - 1))
		aicl_result = bq24190_usbin_input_current_limit[j];		
	chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, j, bq24190_usbin_input_current_limit[j], aicl_point_temp);
	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
	return rc;
}
#else /*CONFIG_MTK_PMIC_CHIP_MT6353 || CONFIG_OPPO_CHARGER_MTK6763*/
static int bq24190_input_current_limit_write(struct oppo_chg_chip *chip, int value)
{
	int rc = 0;
	int i = 0;
	int j = 0;
	int chg_vol = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	for (i = ARRAY_SIZE(bq24190_usbin_input_current_limit) - 1; i >= 0; i--) {
		if (bq24190_usbin_input_current_limit[i] <= value) {
			break;
		}
		else if (i == 0) {
		    break;
		}
	}
    chg_debug( "usb input max current limit=%d setting %02x\n", value, i);
    for (j = 2; j <= i; j++) {
        bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j<<REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
		#ifdef CONFIG_OPPO_CHARGER_MTK
		msleep(90);
		chg_vol = battery_meter_get_charger_voltage();
		#else /* CONFIG_OPPO_CHARGER_MTK */
		msleep(90);
		chg_vol = qpnp_get_prop_charger_voltage_now();
		#endif /* CONFIG_OPPO_CHARGER_MTK */
        if(chg_vol < chip->sw_aicl_point) {
            if (j > 2) {
                j = j-1;
            }
			if((j >= 2) && (j <= ARRAY_SIZE(bq24190_usbin_input_current_limit) - 1))
				aicl_result = bq24190_usbin_input_current_limit[j];		
            chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d\n", chg_vol, j, bq24190_usbin_input_current_limit[j], chip->sw_aicl_point);
            bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
            return 0;
        }
    }
    j = i;

	if((j >= 2) && (j <= ARRAY_SIZE(bq24190_usbin_input_current_limit) - 1))
		aicl_result = bq24190_usbin_input_current_limit[j];		
    chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d\n", chg_vol, j, bq24190_usbin_input_current_limit[j]);
    rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, j << REG00_BQ24190_INPUT_CURRENT_LIMIT_SHIFT, REG00_BQ24190_INPUT_CURRENT_LIMIT_MASK);
    return rc;
}
#endif /*CONFIG_MTK_PMIC_CHIP_MT6353*/


static int bq24190_charging_current_write_fast(struct oppo_chg_chip *chip, int chg_cur)
{
	int ret = 0;
	int value = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	chg_debug( " chg_cur = %d\r\n", chg_cur);
	if (chg_cur < BQ24190_MIN_FAST_CURRENT_MA_ALLOWED) {
		if (chg_cur > BQ24190_MIN_FAST_CURRENT_MA_20_PERCENT)
			chg_cur = BQ24190_MIN_FAST_CURRENT_MA_20_PERCENT;
	    chg_cur = chg_cur * 5;
	    value = (chg_cur - BQ24190_MIN_FAST_CURRENT_MA)/BQ24190_FAST_CURRENT_STEP_MA;
	    value <<= REG02_BQ24190_FAST_CHARGING_CURRENT_LIMIT_SHIFT;
	    value = value | REG02_BQ24190_FAST_CHARGING_CURRENT_FORCE20PCT_ENABLE;
	} else {
	    value = (chg_cur - BQ24190_MIN_FAST_CURRENT_MA)/BQ24190_FAST_CURRENT_STEP_MA;
	    value <<= REG02_BQ24190_FAST_CHARGING_CURRENT_LIMIT_SHIFT;
	}
	ret = bq24190_config_interface(chip, REG02_BQ24190_ADDRESS, value,
		REG02_BQ24190_FAST_CHARGING_CURRENT_LIMIT_MASK | REG02_BQ24190_FAST_CHARGING_CURRENT_FORCE20PCT_MASK);
	return ret;
}

static int bq24190_set_vindpm_vol(struct oppo_chg_chip *chip, int vol)
{
	int rc = 0;
	int value = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	value = (vol - REG00_BQ24190_VINDPM_OFFSET) / REG00_BQ24190_VINDPM_STEP_MV;
	value <<= REG00_BQ24190_VINDPM_SHIFT;

    rc = bq24190_config_interface(chip,
		REG00_BQ24190_ADDRESS, value, REG00_BQ24190_VINDPM_MASK);
	return rc;
}

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oppo_chg_get_dyna_aicl_result(struct oppo_chg_chip *chip)
{
	return aicl_result;
	
}
#endif /* CONFIG_OPPO_SHORT_C_BATT_CHECK */
static void bq24190_set_aicl_point(struct oppo_chg_chip *chip, int vbatt)
{
	if (chip->hw_aicl_point == 4440 && vbatt > 4100) {
		chip->hw_aicl_point = 4500;
		chip->sw_aicl_point = 4550;
		bq24190_set_vindpm_vol(chip, chip->hw_aicl_point);
	} else if (chip->hw_aicl_point == 4520 && vbatt < 4100) {
		chip->hw_aicl_point = 4400;
		chip->sw_aicl_point = 4500;
		bq24190_set_vindpm_vol(chip, chip->hw_aicl_point);
	}
}

static int bq24190_set_enable_volatile_writes(struct oppo_chg_chip *chip)
{
    int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

    return rc;
}

static int bq24190_set_complete_charge_timeout(struct oppo_chg_chip * chip, int val)
{
    int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    if (val == OVERTIME_AC) {
        val = REG05_BQ24190_CHARGING_SAFETY_TIME_ENABLE | REG05_BQ24190_FAST_CHARGING_TIMEOUT_8H;
    } else if (val == OVERTIME_USB) {
        val = REG05_BQ24190_CHARGING_SAFETY_TIME_ENABLE | REG05_BQ24190_FAST_CHARGING_TIMEOUT_12H;
    } else {
        val = REG05_BQ24190_CHARGING_SAFETY_TIME_DISABLE | REG05_BQ24190_FAST_CHARGING_TIMEOUT_8H;
    }

    rc = bq24190_config_interface(chip, REG05_BQ24190_ADDRESS,
		val, REG05_BQ24190_CHARGING_SAFETY_TIME_MASK | REG05_BQ24190_FAST_CHARGING_TIMEOUT_MASK);
    if (rc < 0) {
        chg_err("Couldn't complete charge timeout rc = %d\n", rc);
    }

    return rc;
}

int bq24190_float_voltage_write(struct oppo_chg_chip *chip, int vfloat_mv)
{
	int rc = 0;
    int value = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	if (vfloat_mv < BQ24190_MIN_FLOAT_MV) {
		chg_err(" bad vfloat_mv:%d,return\n", vfloat_mv);
		return 0;
	}
	value = (vfloat_mv - BQ24190_MIN_FLOAT_MV)/BQ24190_VFLOAT_STEP_MV;
	value <<= REG04_BQ24190_CHARGING_VOL_LIMIT_SHIFT;
	chg_debug( "bq24190_set_float_voltage vfloat_mv = %d value=%d\n", vfloat_mv, value);

	rc = bq24190_config_interface(chip, REG04_BQ24190_ADDRESS, value, REG04_BQ24190_CHARGING_VOL_LIMIT_MASK);
	return rc;
}

int bq24190_set_prechg_current(struct oppo_chg_chip *chip, int ipre_mA)
{
    int value = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	value = (ipre_mA - BQ24190_MIN_PRE_CURRENT_MA)/BQ24190_PRE_CURRENT_STEP_MA;
	value <<= REG03_BQ24190_PRE_CHARGING_CURRENT_LIMIT_SHIFT;

	return bq24190_config_interface(chip, REG03_BQ24190_ADDRESS,
		value, REG03_BQ24190_PRE_CHARGING_CURRENT_LIMIT_MASK);
}

static int bq24190_set_termchg_current(struct oppo_chg_chip *chip, int term_curr)
{
	int value = 0;
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	if (term_curr < BQ24190_MIN_TERM_CURRENT_MA) {
		term_curr = BQ24190_MIN_TERM_CURRENT_MA;
	}
	value = (term_curr - BQ24190_MIN_TERM_CURRENT_MA)/BQ24190_TERM_CURRENT_STEP_MA;
	value <<= REG03_BQ24190_TERM_CHARGING_CURRENT_LIMIT_SHIFT;
	chg_debug( " value=%d\n", value);

	bq24190_config_interface(chip, REG03_BQ24190_ADDRESS,
		value, REG03_BQ24190_TERM_CHARGING_CURRENT_LIMIT_MASK);
	return rc;
}

int bq24190_set_rechg_voltage(struct oppo_chg_chip *chip, int recharge_mv)
{
	int reg = 0;
	int rc = 0;
	
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	/* set recharge voltage */
    if (recharge_mv >= 300) {
        reg = REG04_BQ24190_RECHARGING_THRESHOLD_VOL_300MV;
    } else {
        reg = REG04_BQ24190_RECHARGING_THRESHOLD_VOL_100MV;
    }
    rc = bq24190_config_interface(chip, REG04_BQ24190_ADDRESS, reg, REG04_BQ24190_RECHARGING_THRESHOLD_VOL_MASK);
    if (rc) {
        chg_err("Couldn't set recharging threshold rc = %d\n", rc);
    }
	 return rc;
}

int bq24190_set_wdt_timer(struct oppo_chg_chip *chip, int reg)
{
    int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    rc = bq24190_config_interface(chip,
		REG05_BQ24190_ADDRESS, reg, REG05_BQ24190_I2C_WATCHDOG_TIME_MASK);

    return rc;
}

static int bq24190_set_chging_term_disable(struct oppo_chg_chip *chip)
{
	int rc = 0;
	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = bq24190_config_interface(chip, REG05_BQ24190_ADDRESS,
		REG05_BQ24190_TERMINATION_DISABLE, REG05_BQ24190_TERMINATION_MASK);

	return rc;
}

int bq24190_kick_wdt(struct oppo_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    rc = bq24190_config_interface(chip, REG01_BQ24190_ADDRESS,
		REG01_BQ24190_WDT_TIMER_RESET, REG01_BQ24190_WDT_TIMER_RESET_MASK);

    return rc;
}

#ifdef CONFIG_MTK_PMIC_CHIP_MT6353
static bool bq24190_check_charger_suspend_enable(struct oppo_chg_chip *chip);
int bq24190_enable_charging(struct oppo_chg_chip *chip)
{
	int rc = 0;

	#ifdef CONFIG_OPPO_CHARGER_MTK
	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
		|| get_boot_mode() == ADVMETA_BOOT
		|| get_boot_mode() == ATE_FACTORY_BOOT) {
		return 0;
	}
	#endif /* CONFIG_OPPO_CHARGER_MTK */

	if (bq24190_check_charger_suspend_enable(chip)) {
		bq24190_unsuspend_charger(chip);
	}

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("Couldn't bq24190_enable_charging because charger_suspended\n");
		return 0;
	}
	rc = bq24190_config_interface(chip, REG01_BQ24190_ADDRESS,
			REG01_BQ24190_CHARGING_ENABLE, REG01_BQ24190_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn'tbq24190_enable_charging rc = %d\n", rc);
	}
	return rc;
}

int bq24190_disable_charging(struct oppo_chg_chip *chip)
{
	int rc = 0;

	/* Only for BATTERY_STATUS__HIGH_TEMP or BATTERY_STATUS__WARM_TEMP, */
	/* system power is from charger but NOT from battery to avoid temperature rise problem */
	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("Couldn't bq24190_disable_charging because charger_suspended\n");
		return 0;
	}
	rc = bq24190_config_interface(chip, REG01_BQ24190_ADDRESS,
			REG01_BQ24190_CHARGING_DISABLE, REG01_BQ24190_CHARGING_MASK);
	if (rc < 0) {
		chg_err("Couldn't bq24190_disable_charging  rc = %d\n", rc);
	} else {
		chg_err("battery HIGH_TEMP or WARM_TEMP, bq24190_disable_charging\n");
	}
	return rc;
}
#else /* CONFIG_MTK_PMIC_CHIP_MT6353 */
int bq24190_enable_charging(struct oppo_chg_chip *chip)
{
	int rc = 0;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

    rc = bq24190_config_interface(chip, REG01_BQ24190_ADDRESS,
		REG01_BQ24190_CHARGING_ENABLE, REG01_BQ24190_CHARGING_MASK);
    if (rc < 0) {
		chg_err("Couldn'tbq24190_enable_charging rc = %d\n", rc);
	}
	bq24190_set_wdt_timer(chip, REG05_BQ24190_I2C_WATCHDOG_TIME_40S);

	return rc;
}

int bq24190_disable_charging(struct oppo_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    rc = bq24190_config_interface(chip, REG01_BQ24190_ADDRESS,
		REG01_BQ24190_CHARGING_DISABLE, REG01_BQ24190_CHARGING_MASK);
    if (rc < 0) {
		chg_err("Couldn't bq24190_disable_charging  rc = %d\n", rc);
	}
	if(chip->charging_state == CHARGING_STATUS_FULL)
	{
		bq24190_charging_current_write_fast(chip, 512);
	}
	
	bq24190_set_wdt_timer(chip, REG05_BQ24190_I2C_WATCHDOG_TIME_DISABLE);
	bq24190_set_vindpm_vol(chip, 4600);
	return rc;
}
#endif /*CONFIG_MTK_PMIC_CHIP_MT6353*/

#ifdef CONFIG_MTK_PMIC_CHIP_MT6353
static bool bq24190_check_charger_suspend_enable(struct oppo_chg_chip *chip)
{
	int rc = 0;
	int reg_val = 0;
	bool charger_suspend_enable = false;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = bq24190_read_reg(chip, REG00_BQ24190_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG00_BQ24190_ADDRESS rc = %d\n", rc);
		return 0;
	}

	charger_suspend_enable = ((reg_val & REG00_BQ24190_SUSPEND_MODE_MASK) == REG00_BQ24190_SUSPEND_MODE_ENABLE) ? true : false;

	return charger_suspend_enable;
}
static int bq24190_check_charging_enable(struct oppo_chg_chip *chip)
{
	bool rc = 0;

	rc = bq24190_check_charger_suspend_enable(chip);
	if(rc)
		return 0;
	else
		return 1;
}
#else /* CONFIG_MTK_PMIC_CHIP_MT6353 */
static int bq24190_check_charging_enable(struct oppo_chg_chip *chip)
{
	int rc = 0;
	int reg_val = 0;
	bool charging_enable = false;

	if(atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
	rc = bq24190_read_reg(chip, REG01_BQ24190_ADDRESS, &reg_val);
    if (rc) {
        chg_err("Couldn't read REG01_BQ24190_ADDRESS rc = %d\n", rc);
        return 0;
    }

    charging_enable = ((reg_val & REG01_BQ24190_CHARGING_MASK) == REG01_BQ24190_CHARGING_ENABLE) ? 1 : 0;

	return charging_enable;
}
#endif /*CONFIG_MTK_PMIC_CHIP_MT6353*/

int bq24190_registers_read_full(struct oppo_chg_chip *chip)
{
	int rc = 0;
    int reg_full = 0;
	int reg_ovp = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    rc = bq24190_read_reg(chip, REG08_BQ24190_ADDRESS, &reg_full);
    if (rc) {
        chg_err("Couldn't read STAT_C rc = %d\n", rc);
        return 0;
    }

    reg_full = ((reg_full & REG08_BQ24190_CHARGING_STATUS_CHARGING_MASK) == REG08_BQ24190_CHARGING_STATUS_TERM_CHARGING) ? 1 : 0;

	rc = bq24190_read_reg(chip, REG09_BQ24190_ADDRESS, &reg_ovp);
	if (rc) {
        chg_err("Couldn't read STAT_D rc = %d\n", rc);
        return 0;
    }

	reg_ovp = ((reg_ovp & REG09_BQ24190_BATTERY_VOLATGE_MASK) == REG09_BQ24190_BATTERY_VOLATGE_HIGH_ERROR) ? 1 : 0;
//	chg_err("bq24190_registers_read_full, reg_full = %d, reg_ovp = %d\r\n", reg_full, reg_ovp);
	return (reg_full || reg_ovp);
}

static int oppo_otg_online = 0;

int bq24190_otg_enable(void)
{
	int rc = 0;

	if (!bq24190_chip) {
		return 0;
	}
	#ifndef CONFIG_OPPO_CHARGER_MTK
	if (atomic_read(&bq24190_chip->charger_suspended) == 1) {
		return 0;
	}
	#endif /* CONFIG_OPPO_CHARGER_MTK */
	bq24190_config_interface(bq24190_chip, REG00_BQ24190_ADDRESS,
		REG00_BQ24190_SUSPEND_MODE_DISABLE, REG00_BQ24190_SUSPEND_MODE_MASK);

	rc = bq24190_config_interface(bq24190_chip, REG01_BQ24190_ADDRESS,
		REG01_BQ24190_OTG_ENABLE, REG01_BQ24190_OTG_MASK);
	if (rc) {
		chg_err("Couldn't enable  OTG mode rc=%d\n", rc);
	} else {
		chg_debug( "bq24190_otg_enable rc=%d\n", rc);
	}
	
	bq24190_set_wdt_timer(bq24190_chip, REG05_BQ24190_I2C_WATCHDOG_TIME_40S);
	oppo_otg_online = 1;
	oppo_chg_set_otg_online(true);
	return rc;
}

int bq24190_otg_disable(void)
{
	int rc = 0;
	int val_buf;

	if (!bq24190_chip) {
		return 0;
	}
	#ifndef CONFIG_OPPO_CHARGER_MTK
	if (atomic_read(&bq24190_chip->charger_suspended) == 1) {
		return 0;
	}
	#endif /* CONFIG_OPPO_CHARGER_MTK */
	rc = bq24190_config_interface(bq24190_chip, REG01_BQ24190_ADDRESS,
		REG01_BQ24190_OTG_DISABLE, REG01_BQ24190_OTG_MASK);
	if (rc)
		chg_err("Couldn't disable OTG mode rc=%d\n", rc);
	else
		chg_debug( "bq24190_otg_disable rc=%d\n", rc);
	
	bq24190_set_wdt_timer(bq24190_chip, REG05_BQ24190_I2C_WATCHDOG_TIME_DISABLE);
	oppo_otg_online = 0;
	oppo_chg_set_otg_online(false);
	
	bq24190_read_reg(bq24190_chip, REG09_BQ24190_ADDRESS, &val_buf);
	bq24190_read_reg(bq24190_chip, REG09_BQ24190_ADDRESS, &val_buf);
	return rc;
}

int bq24190_suspend_charger(struct oppo_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	if (oppo_chg_get_otg_online() == true)
		return 0;

	if (oppo_otg_online == 1)
		return 0;

	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS,
		REG00_BQ24190_SUSPEND_MODE_ENABLE, REG00_BQ24190_SUSPEND_MODE_MASK);
	chg_debug( " rc = %d\n", rc);
	if (rc < 0) {
		chg_err("Couldn't bq24190_suspend_charger rc = %d\n", rc);
	}

	return rc;
}
int bq24190_unsuspend_charger(struct oppo_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

    rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS,
		REG00_BQ24190_SUSPEND_MODE_DISABLE, REG00_BQ24190_SUSPEND_MODE_MASK);
	chg_debug( " rc = %d\n", rc);
    if (rc < 0) {
		chg_err("Couldn't bq24190_unsuspend_charger rc = %d\n", rc);
	}
	return rc;
}

#if defined CONFIG_MTK_PMIC_CHIP_MT6353 || defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
int bq24190_reset_charger(struct oppo_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	rc = bq24190_config_interface(chip, REG00_BQ24190_ADDRESS, 0x32, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG00 rc = %d\n", rc);
	}
	rc = bq24190_config_interface(chip, REG01_BQ24190_ADDRESS, 0x1B, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG01 rc = %d\n", rc);
	}
	rc = bq24190_config_interface(chip, REG02_BQ24190_ADDRESS, 0x60, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG02 rc = %d\n", rc);
	}
	rc = bq24190_config_interface(chip, REG03_BQ24190_ADDRESS, 0x11, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG03 rc = %d\n", rc);
	}
	rc = bq24190_config_interface(chip, REG04_BQ24190_ADDRESS, 0xCA, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG04 rc = %d\n", rc);
	}
	rc = bq24190_config_interface(chip, REG05_BQ24190_ADDRESS, 0x1A, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG05 rc = %d\n", rc);
	}
	rc = bq24190_config_interface(chip, REG06_BQ24190_ADDRESS, 0x03, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG06 rc = %d\n", rc);
	}
	rc = bq24190_config_interface(chip, REG07_BQ24190_ADDRESS, 0x4B, 0xFF);
	if (rc < 0) {
		chg_err("Couldn't reset REG07 rc = %d\n", rc);
	}

	return rc;
}
#else /*defined CONFIG_MTK_PMIC_CHIP_MT6353 || defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)*/
int bq24190_reset_charger(struct oppo_chg_chip *chip)
{
	int rc = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}
    rc = bq24190_config_interface(chip, REG01_BQ24190_ADDRESS,
		REG01_BQ24190_REGISTER_RESET, REG01_BQ24190_REGISTER_RESET_MASK);
	chg_debug( " rc = %d\n", rc);
    if (rc < 0) {
		chg_err("Couldn't bq24190_reset_charger rc = %d\n", rc);
	}

	return rc;
}
#endif /*defined CONFIG_MTK_PMIC_CHIP_MT6353 || defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)*/

static bool bq24190_check_charger_resume(struct oppo_chg_chip *chip)
{
	if (atomic_read(&chip->charger_suspended) == 1) {
		return false;
	} else {
		return true;
	}
}

#define DUMP_REG_LOG_CNT_30S  6

int bq24190_dump_registers(struct oppo_chg_chip *chip)
{
	int rc = 0;
	int addr = 0;
	unsigned int val_buf[BQ24190_REG_NUMBER] = {0x0};
	static int dump_count = 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		return 0;
	}

	for (addr = BQ24190_FIRST_REG; addr <= BQ24190_LAST_REG; addr++) {
		rc = bq24190_read_reg(chip, addr, &val_buf[addr]);
		if (rc) {
			 chg_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
			 return -1;
		}
	}

	if(dump_count == DUMP_REG_LOG_CNT_30S) {
		///if(1) {
		dump_count = 0;
		chg_debug( "bq24190_reg[0-a]:0x%02x(0),0x%02x(1),0x%02x(2),0x%02x(3),0x%02x(4),0x%02x(5),0x%02x(6),0x%02x(7),0x%02x(8),0x%02x(9),0x%02x(a)\n",
			val_buf[0],val_buf[1],val_buf[2],val_buf[3],val_buf[4],val_buf[5],val_buf[6],val_buf[7],
			val_buf[8],val_buf[9],val_buf[10]);
	}
	dump_count++;
  	return 0;

}

static int bq24190_get_chg_current_step(struct oppo_chg_chip *chip)
{
	int rc = 0;
	int reg_val = 0;

	rc = bq24190_read_reg(chip, REG02_BQ24190_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG02_BQ24190_ADDRESS rc = %d\n", rc);
		return 0;
	}

	if(reg_val & REG02_BQ24190_FAST_CHARGING_CURRENT_FORCE20PCT_MASK)
		return 13;
	else
		return 64;
}


int bq24190_hardware_init(struct oppo_chg_chip *chip)
{
	/* must be before set_vindpm_vol and set_input_current */
	chip->hw_aicl_point = 4440;
	chip->sw_aicl_point = 4500;

	bq24190_reset_charger(chip);

	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
		#ifndef CONFIG_MTK_PMIC_CHIP_MT6353
		bq24190_disable_charging(chip);
		#endif /* CONFIG_MTK_PMIC_CHIP_MT6353 */
		bq24190_float_voltage_write(chip, 4400);
		msleep(100);
	}
	#if defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
	bq24190_float_voltage_write(chip, 4370);
	#else
	bq24190_float_voltage_write(chip, 4320);
	#endif

	bq24190_set_enable_volatile_writes(chip);

	bq24190_set_complete_charge_timeout(chip, OVERTIME_DISABLED);

    bq24190_set_prechg_current(chip, 300);

	bq24190_charging_current_write_fast(chip, 512);

    bq24190_set_termchg_current(chip, 150);

    bq24190_set_rechg_voltage(chip, 100);

	bq24190_set_vindpm_vol(chip, chip->hw_aicl_point);
	if((chip->stop_chg ==0 && chip->charger_type ==4)){
		bq24190_suspend_charger(chip);
		return true;
	}

	#ifdef CONFIG_OPPO_CHARGER_MTK
	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
		|| get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
		bq24190_suspend_charger(chip);
		bq24190_disable_charging(chip);
	} else {
		bq24190_unsuspend_charger(chip);
	}
	#else /* CONFIG_OPPO_CHARGER_MTK */
	bq24190_unsuspend_charger(chip);
	#endif /* CONFIG_OPPO_CHARGER_MTK */

    bq24190_enable_charging(chip);

    bq24190_set_wdt_timer(chip, REG05_BQ24190_I2C_WATCHDOG_TIME_40S);

	return true;
}
#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
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

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPPO_RTC_DET_SUPPORT */

extern bool oppo_chg_get_shortc_hw_gpio_status(struct oppo_chg_chip *chip);
extern int oppo_chg_shortc_hw_parse_dt(struct oppo_chg_chip *chip);
struct oppo_chg_operations  bq24190_chg_ops = {
	.dump_registers = bq24190_dump_registers,
	.kick_wdt = bq24190_kick_wdt,
	.hardware_init = bq24190_hardware_init,
	.charging_current_write_fast = bq24190_charging_current_write_fast,
	.set_aicl_point = bq24190_set_aicl_point,
	.input_current_write = bq24190_input_current_limit_write,
	.float_voltage_write = bq24190_float_voltage_write,
	.term_current_set = bq24190_set_termchg_current,
	.charging_enable = bq24190_enable_charging,
	.charging_disable = bq24190_disable_charging,
	.get_charging_enable = bq24190_check_charging_enable,
	.charger_suspend = bq24190_suspend_charger,
	.charger_unsuspend = bq24190_unsuspend_charger,
	.set_rechg_vol = bq24190_set_rechg_voltage,
	.reset_charger = bq24190_reset_charger,
	.read_full = bq24190_registers_read_full,
	.otg_enable = bq24190_otg_enable,
	.otg_disable = bq24190_otg_disable,
	.set_charging_term_disable = bq24190_set_chging_term_disable,
	.check_charger_resume = bq24190_check_charger_resume,
	#ifdef CONFIG_OPPO_CHARGER_MTK
	.get_charger_type = mt_power_supply_type_check,
	.get_chg_pretype = charger_pretype_get,
	.get_charger_volt = battery_meter_get_charger_voltage,
	.check_chrdet_status = (bool (*) (void)) pmic_chrdet_status,
	.get_instant_vbatt = oppo_battery_meter_get_battery_voltage,
	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
//	#ifdef CONFIG_MTK_HAFG_20
	.get_chargerid_volt = mt_get_chargerid_volt,

	.set_chargerid_switch_val = mt_set_chargerid_switch_val ,
	.get_chargerid_switch_val  = mt_get_chargerid_switch_val,
	.set_usb_shell_ctrl_val = mt_set_usb_shell_ctrl_val,
	.get_usb_shell_ctrl_val = mt_get_usb_shell_ctrl_val,
//	#endif /* CONFIG_MTK_HAFG_20 */
	#ifdef CONFIG_MTK_HAFG_20
	.get_rtc_soc = get_rtc_spare_oppo_fg_value,
	.set_rtc_soc = set_rtc_spare_oppo_fg_value,
/*Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2017/11/29, add for mt6771 charger */
	#elif defined(CONFIG_OPPO_CHARGER_MTK6771)
    .get_rtc_soc = oppo_get_rtc_ui_soc,
	.set_rtc_soc = oppo_set_rtc_ui_soc,
	#elif defined(CONFIG_OPPO_CHARGER_MTK6763)
    .get_rtc_soc = get_rtc_spare_oppo_fg_value,
    .set_rtc_soc = set_rtc_spare_oppo_fg_value,
	#else /* CONFIG_MTK_HAFG_20 */
	.get_rtc_soc = oppo_get_rtc_ui_soc,
	.set_rtc_soc = set_rtc_spare_fg_value,
	#endif /* CONFIG_MTK_HAFG_20 */
	.set_power_off = mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
	#else /* CONFIG_OPPO_CHARGER_MTK */
	.get_charger_type = qpnp_charger_type_get,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.check_chrdet_status = qpnp_lbc_is_usb_chg_plugged_in,
	.get_instant_vbatt = qpnp_get_prop_battery_voltage_now,
	.get_boot_mode = get_boot_mode,
	.get_rtc_soc = qpnp_get_pmic_soc_memory,
	.set_rtc_soc = qpnp_set_pmic_soc_memory,
	#endif /* CONFIG_OPPO_CHARGER_MTK */
	.get_chg_current_step = bq24190_get_chg_current_step,
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oppo_chg_get_dyna_aicl_result,
#endif
	.get_shortc_hw_gpio_status = oppo_chg_get_shortc_hw_gpio_status,
	#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
	#endif
};

static void register_charger_devinfo(void)
{
	int ret = 0;
	char *version = "bq24190";
	char *manufacture = "TI";
	//ret = register_device_proc("charger", version, manufacture);
	if (ret)
		chg_err("register_charger_devinfo fail\n");
}
extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);
extern bool is_usb_rdy(void);
static void hw_bc12_init(void)
{
	int timeout = 40;
	static bool first_connect = true;
	msleep(400);

	if (first_connect == true) {
		/* add make sure USB Ready */
		if (is_usb_rdy() == false) {
			pr_err("CDP, block\n");
			while (is_usb_rdy() == false && timeout > 0) {
				msleep(100);
				timeout--;
			}
			if (timeout == 0)
				pr_err("CDP, timeout\n");
			else {
				pr_err("CDP, free, timeout:%d\n", timeout);
			}
		} else
			pr_err("CDP, PASS\n");
		first_connect = false;
	}
	Charger_Detect_Init();
}

//static DEVICE_ATTR(bq24190_access, 0664, show_bq24190_access, store_bq24190_access);
#ifdef VENDOR_EDIT
CHARGER_TYPE MTK_CHR_Type_num;
extern unsigned int upmu_get_rgs_chrdet(void);

CHARGER_TYPE mt_charger_type_detection(void)
{
	int rc =0;
	int addr = 0;
	int val_buf;
	int count = 0;
	int i;
	if (!bq24190_chip) {
		pr_err("%s bq24190_chip null,return\n", __func__);
		return CHARGER_UNKNOWN;
	}
	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
		|| get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
		return STANDARD_HOST;
	}
	addr = BQ24190_FIRST_REG + 8;
	printk("mt_charger_type_detection0\n");
	hw_bc12_init();
	usleep_range(40000, 40200);
	printk("mt_charger_type_detection1\n");
	bq24190_config_interface(bq24190_chip, REG00_BQ24190_ADDRESS, 0, 0x80);
	usleep_range(5000, 5200);
	bq24190_config_interface(bq24190_chip, REG07_BQ24190_ADDRESS, 0x80, 0);
	bq24190_dump_registers(bq24190_chip);
	rc = bq24190_read_reg(bq24190_chip, REG07_BQ24190_ADDRESS, &val_buf);
	while (val_buf == 0xcb && count <20) {
		count++;
		rc = bq24190_read_reg(bq24190_chip, REG07_BQ24190_ADDRESS, &val_buf);
		usleep_range(50000, 50200);
		if (!upmu_get_rgs_chrdet()) {
			MTK_CHR_Type_num = CHARGER_UNKNOWN;
			return MTK_CHR_Type_num;
		}
	}
	if (count == 20) {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
		chg_debug("count time out,return apple adapter\n");
		return MTK_CHR_Type_num;
	}
	printk("mt_charger_type_detection2\n");
	rc = bq24190_read_reg(bq24190_chip, addr, &val_buf);
	printk("mt_charger_type_detection\n");
	bq24190_dump_registers(bq24190_chip);
	val_buf = val_buf & 0xc0;
	printk("val_buf =0x%x\n",val_buf);
	if (val_buf == 0) {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
	}
	else if(val_buf == 0x40) {
		MTK_CHR_Type_num = STANDARD_HOST;
	}
	else if(val_buf == 0x80){
		MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
	}
	else {
		MTK_CHR_Type_num = CHARGER_UNKNOWN;
	}
	if((MTK_CHR_Type_num == CHARGER_UNKNOWN || MTK_CHR_Type_num == STANDARD_HOST)  && upmu_get_rgs_chrdet()) {
		if (MTK_CHR_Type_num == STANDARD_HOST) {
			for (i = 0;i < 4;i++) {
				usleep_range(100000, 100020);
			}
		}
		bq24190_config_interface(bq24190_chip, REG00_BQ24190_ADDRESS, 0, 0x80);
		usleep_range(5000, 5020);
		bq24190_config_interface(bq24190_chip, REG07_BQ24190_ADDRESS, 0x80, 0);
		bq24190_dump_registers(bq24190_chip);
		rc = bq24190_read_reg(bq24190_chip, REG07_BQ24190_ADDRESS, &val_buf);
		while (val_buf == 0xcb && count <20) {
			count++;
			rc = bq24190_read_reg(bq24190_chip, REG07_BQ24190_ADDRESS, &val_buf);
			usleep_range(50000, 50200);
			if (!upmu_get_rgs_chrdet()) {
				MTK_CHR_Type_num = CHARGER_UNKNOWN;
				return MTK_CHR_Type_num;
			}
		}
		if (count == 20) {
			MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
			chg_debug("count time out,return apple adapter\n");
			return MTK_CHR_Type_num;
		}
		rc = bq24190_read_reg(bq24190_chip, addr, &val_buf);
		bq24190_dump_registers(bq24190_chip);
		val_buf = val_buf & 0xc0;
		printk("val_buf =0x%x\n",val_buf);
		if (val_buf == 0) {
			MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
		}
		else if(val_buf == 0x40) {
			MTK_CHR_Type_num = STANDARD_HOST;
		}
		else if(val_buf == 0x80){
			MTK_CHR_Type_num = APPLE_2_1A_CHARGER;
		}
		else {
			MTK_CHR_Type_num = CHARGER_UNKNOWN;
		}
	}
	chg_debug("chr_type:%d\n",MTK_CHR_Type_num);
	Charger_Detect_Release();
	//MTK_CHR_Type_num = APPLE_2_1A_CHARGER;

	return MTK_CHR_Type_num;
}
#endif
/* Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2017/12/12, Add for charger modefy */
static void do_charger_modefy_work(struct work_struct *data)
{
	/*
	if(bq24190_chip != NULL) {
		mt_charger_type_detection_bq25890h();
	}
	*/
}

	
static struct delayed_work bq24190_irq_delay_work;
bool fg_bq24190_irq_delay_work_running = false;
static void do_bq24190_irq_delay_work(struct work_struct *data)
{
	int val_buf;
	int i;
	int otg_overcurrent_flag = 0;
	
	if (!bq24190_chip) {
		pr_err("%s bq24190_chip null,return\n", __func__);
		return;
	}

	for (i = 0;i < 4;i++) {
		bq24190_read_reg(bq24190_chip, REG09_BQ24190_ADDRESS, &val_buf);
		val_buf = val_buf & 0x40;
		if (val_buf == 0x40) {
			otg_overcurrent_flag++;
		}
		
		usleep_range(10000, 10200);
	}
	printk("do_bq24190_irq_delay_work disable  vbus out flag =%d\n",otg_overcurrent_flag);
	if (otg_overcurrent_flag >= 3) {
		bq24190_otg_disable();
	}
	fg_bq24190_irq_delay_work_running = false;
	return; 
}


static irqreturn_t bq24190_irq_handler_fn(int irq, void *dev_id)
{
	int val_buf;

	pr_err("%s start\n", __func__);
	if (!bq24190_chip) {
		pr_err("%s bq24190_chip null,return\n", __func__);
		return IRQ_HANDLED;
	}
	if (oppo_otg_online == 0) {
		return IRQ_HANDLED;
	}
	if(fg_bq24190_irq_delay_work_running == false)
	{
		fg_bq24190_irq_delay_work_running = true;
	    schedule_delayed_work(&bq24190_irq_delay_work, round_jiffies_relative(msecs_to_jiffies(50)));
	}

	return IRQ_HANDLED;
}

static int bq24190_irq_registration(struct oppo_chg_chip *chip)
{
	int ret = 0;
	ret = request_threaded_irq(chip->client->irq, NULL,
		bq24190_irq_handler_fn,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"BQ24190-eint", chip);
	if (ret < 0) {
		printk("BQ24190 request_irq IRQ LINE NOT AVAILABLE!.");
		return -EFAULT;
	}
	return 0;
}

int charger_ic_flag = 0;
static int bq24190_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int reg = 0;
	struct oppo_chg_chip *chip = NULL;

	#ifndef CONFIG_OPPO_CHARGER_MTK
	struct power_supply *usb_psy;
	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
        chg_err("USB psy not found; deferring probe\n");
        return -EPROBE_DEFER;
    }
	#endif  /* CONFIG_OPPO_CHARGER_MTK */
    chg_debug( " call \n");

	chip = devm_kzalloc(&client->dev,
		sizeof(struct oppo_chg_chip), GFP_KERNEL);
	if (!chip) {
		chg_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	chip->client = client;
    chip->dev = &client->dev;
	chip->chg_ops = &bq24190_chg_ops;
	reg = bq24190_dump_registers(chip);
	if (reg < 0) {
		return 0;
	}
	charger_ic_flag = 0;
	/* Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2018/01/05, Add for fast charger */
	if (is_project(OPPO_17197)) {
		if (oppo_pmic_check_chip_is_null()) {
			chg_err("[oppo_chg_init] vooc || gauge || pmic not ready, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
	}

	oppo_chg_parse_dt(chip);	
	oppo_chg_shortc_hw_parse_dt(chip);
	
	INIT_DELAYED_WORK(&bq24190_irq_delay_work, do_bq24190_irq_delay_work);
	bq24190_irq_registration(chip);
	atomic_set(&chip->charger_suspended, 0);

	bq24190_hardware_init(chip);
	if(is_project(OPPO_17331) || is_project(OPPO_17061) || is_project(OPPO_17197) || is_project(OPPO_17175)) {
		chip->authenticate = oppo_gauge_get_batt_authenticate();
	} else {
		chip->authenticate = get_oppo_high_battery_status();
	}
	if (!chip->authenticate) {
		bq24190_disable_charging(chip);
		bq24190_suspend_charger(chip);
	}
  	oppo_chg_init(chip);
	register_charger_devinfo();
	bq24190_chip = chip;
	/* Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2017/12/12, Add for charger modefy */
	usleep_range(1000, 1200);
	INIT_DELAYED_WORK(&charger_modefy_work, do_charger_modefy_work);
	schedule_delayed_work(&charger_modefy_work, 0);
    oppo_tbatt_power_off_task_init(chip);
    return 0;

}


static struct i2c_driver bq24190_i2c_driver;
extern void enter_ship_mode_function(struct oppo_chg_chip *chip);

static int bq24190_driver_remove(struct i2c_client *client)
{
	return 0;
}

static unsigned long suspend_tm_sec = 0;
static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc = NULL;
	int rc = 0;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		chg_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		chg_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		chg_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int bq24190_resume(struct device *dev)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int rc = 0;

	if (!bq24190_chip) {
		return 0;
	}
	atomic_set(&bq24190_chip->charger_suspended, 0);
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
	chg_err(" resume_sec:%ld,sleep_time:%ld\n\n",resume_tm_sec,sleep_time);
	oppo_chg_soc_update_when_resume(sleep_time);
	return 0;
}

static int bq24190_suspend(struct device *dev)
{
	if (!bq24190_chip) {
		return 0;
	}
	atomic_set(&bq24190_chip->charger_suspended, 1);
	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	chg_err(" suspend_sec:%ld\n",suspend_tm_sec);
	return 0;
}
static const struct dev_pm_ops bq24190_pm_ops = {
	.resume		= bq24190_resume,
	.suspend		= bq24190_suspend,
};


#else //(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int bq24190_resume(struct i2c_client *client)
{
	unsigned long resume_tm_sec = 0;
	unsigned long sleep_time = 0;
	int rc = 0;

	if (!bq24190_chip) {
		return 0;
	}
	atomic_set(&bq24190_chip->charger_suspended, 0);
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

static int bq24190_suspend(struct i2c_client *client, pm_message_t mesg)
{
	if (!bq24190_chip) {
		return 0;
	}
	atomic_set(&bq24190_chip->charger_suspended, 1);
	if (get_current_time(&suspend_tm_sec)) {
		chg_err("RTC read failed\n");
		suspend_tm_sec = -1;
	}
	return 0;
}
#endif //(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))

static void bq24190_reset(struct i2c_client *client)
{
	bq24190_otg_disable();
	if(bq24190_chip != NULL){
		enter_ship_mode_function(bq24190_chip);
	}
	else {
		printk("enter_ship_mode_function fail_bq24190\n");
	return ;
	}
}

/**********************************************************
 * *
 * *   [platform_driver API]
 * *
 * *********************************************************/

static const struct of_device_id bq24190_match[] = {
	{ .compatible = "oppo,bq24190-charger"},
	{ },
};

static const struct i2c_device_id bq24190_id[] = {
	{"bq24190-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, bq24190_id);


static struct i2c_driver bq24190_i2c_driver = {
	.driver		= {
		.name = "bq24190-charger",
		.owner	= THIS_MODULE,
		.of_match_table = bq24190_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm		= &bq24190_pm_ops,
#endif /*CONFIG_OPPO_CHARGER_MTK6763*/
	},
	.probe		= bq24190_driver_probe,
	.remove		= bq24190_driver_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= bq24190_resume,
	.suspend	= bq24190_suspend,
#endif

	.shutdown	= bq24190_reset,
	.id_table	= bq24190_id,
};


module_i2c_driver(bq24190_i2c_driver);
MODULE_DESCRIPTION("Driver for bq24190 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:bq24190-charger");
