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


#ifdef CONFIG_OPPO_CHARGER_MTK

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
#include <linux/earlysuspend.h>
#endif
#include <linux/platform_device.h>
#include <asm/atomic.h>

#include <linux/xlog.h>
#if defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
#include <upmu_common.h>
#include <linux/gpio.h>
//#include <mt-plat/mtk_gpio.h>
#include <linux/irqchip/mtk-eic.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <mt-plat/mtk_boot_common.h>
#else
#include <linux/earlysuspend.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_gpio.h>
#include <mach/mt_pm_ldo.h>
#include <mach/eint.h>

#include <linux/of.h>
#include <linux/of_irq.h>
#include <mach/mt_boot_common.h>
#endif
#else
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>


#endif

#include "../oppo_charger.h"
#include "../oppo_gauge.h"
#include "../oppo_vooc.h"
#include "oppo_vooc_fw.h"
extern int mt_set_gpio_mode(unsigned long pin, unsigned long mode);

int g_hw_version = 0;
int vooc_low_temp = 0;
void init_hw_version(void)
{
}

#if defined(CONFIG_OPPO_CHARGER_MTK6771)
extern int main_hwid5_val;
#endif
int get_vooc_mcu_type(void)
{
#if defined(CONFIG_OPPO_CHARGER_MTK6771)
		return main_hwid5_val;
#else
		return 0;
#endif
}
#if defined(CONFIG_OPPO_CHARGER_MTK6763)  || defined(CONFIG_OPPO_CHARGER_MTK6771)
static int opchg_bq27541_gpio_pinctrl_init(struct oppo_vooc_chip *chip)
{

    chip->vooc_gpio.pinctrl = devm_pinctrl_get(chip->dev);
    if (IS_ERR_OR_NULL(chip->vooc_gpio.pinctrl)) {
            pr_err("%s:%d Getting pinctrl handle failed\n",
            __func__, __LINE__);
         return -EINVAL;
	}

	// set switch1 is active and switch2 is active
	if (1) {
	    chip->vooc_gpio.gpio_switch1_act_switch2_act =
	        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_act_switch3_act");
	    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_act_switch2_act)) {
	            pr_err("%s:%d Failed to get the active state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }

		// set switch1 is sleep and switch2 is sleep
	    chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep =
	        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_sleep_switch3_sleep");
	    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep)) {
	            pr_err("%s:%d Failed to get the suspend state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }
	} else {
		chip->vooc_gpio.gpio_switch1_act_switch2_act =
	        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_act_switch2_act");
	    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_act_switch2_act)) {
	            pr_err("%s:%d Failed to get the active state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }

		// set switch1 is sleep and switch2 is sleep
	    chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep =
	        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_sleep_switch2_sleep");
	    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep)) {
	            pr_err("%s:%d Failed to get the suspend state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }
	}
	// set switch1 is active and switch2 is sleep
    chip->vooc_gpio.gpio_switch1_act_switch2_sleep =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_act_switch2_sleep");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_act_switch2_sleep)) {
            pr_err("%s:%d Failed to get the state 2 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set switch1 is sleep and switch2 is active
    chip->vooc_gpio.gpio_switch1_sleep_switch2_act =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_sleep_switch2_act");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_sleep_switch2_act)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set clock is active
	chip->vooc_gpio.gpio_clock_active =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "clock_active");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_clock_active)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set clock is sleep
	chip->vooc_gpio.gpio_clock_sleep =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "clock_sleep");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_clock_sleep)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set clock is active
	chip->vooc_gpio.gpio_data_active =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "data_active");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_data_active)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set clock is sleep
	chip->vooc_gpio.gpio_data_sleep =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "data_sleep");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_data_sleep)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
	// set reset is atcive
	chip->vooc_gpio.gpio_reset_active =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "reset_active");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_reset_active)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
	// set reset is sleep
	chip->vooc_gpio.gpio_reset_sleep =
        pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "reset_sleep");
    if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_reset_sleep)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
    return 0;
}
#endif

void oppo_vooc_data_irq_init(struct oppo_vooc_chip *chip);

// wenbin.liu@BSP.CHG.Vooc, 2016/10/20
// Add for vooc batt 4.40
void oppo_vooc_fw_type_dt(struct oppo_vooc_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc;
	
	chip->batt_type_4400mv = of_property_read_bool(node, "qcom,oppo_batt_4400mv");
	rc = of_property_read_u32(node, "qcom,vooc-fw-type", &chip->vooc_fw_type);
    if (rc) {
        chip->vooc_fw_type = VOOC_FW_TYPE_INVALID;
    }
		chip->vooc_fw_update_newmethod = of_property_read_bool(node, "qcom,vooc_fw_update_newmethod");
		chg_debug(" vooc_fw_upate:%d\n", chip->vooc_fw_update_newmethod);
	rc = of_property_read_u32(node, "qcom,vooc-low-temp", &chip->vooc_low_temp);
        if (rc) {
                chip->vooc_low_temp = 165;
        } else {
                chg_debug("qcom,vooc-low-temp is %d\n", chip->vooc_low_temp);
        }
		vooc_low_temp = chip->vooc_low_temp;
}

int oppo_vooc_gpio_dt_init(struct oppo_vooc_chip *chip)
{

 	int rc;
    struct device_node *node = chip->dev->of_node;

#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
    if (!node) {
        dev_err(chip->dev, "device tree info. missing\n");
        return -EINVAL;
    }

	rc = of_property_read_u32(node, "qcom,vooc_switch1_gpio", &chip->vooc_gpio.switch1_gpio);
    if (rc) {
        chip->vooc_gpio.switch1_gpio = OPPO_VOOC_SW_CTRL_EVT;
    }

	rc = of_property_read_u32(node, "qcom,vooc_switch2_gpio", &chip->vooc_gpio.switch2_gpio);
    if (rc) {
        chip->vooc_gpio.switch2_gpio = OPPO_VOOC_SW_CTRL_DVT;
    }

	rc = of_property_read_u32(node, "qcom,vooc_reset_gpio", &chip->vooc_gpio.reset_gpio);
    if (rc) {
        chip->vooc_gpio.reset_gpio = OPPO_VOOC_RESET_MCU_EN;
    }
	
	rc = of_property_read_u32(node, "qcom,vooc_clock_gpio", &chip->vooc_gpio.clock_gpio);
    if (rc) {
        chip->vooc_gpio.clock_gpio = OPPO_VOOC_MCU_AP_CLK;
    }
	
	rc = of_property_read_u32(node, "qcom,vooc_data_gpio", &chip->vooc_gpio.data_gpio);
    if (rc) {
        chip->vooc_gpio.data_gpio = OPPO_VOOC_MCU_AP_DATA;
    }
	
	oppo_vooc_data_irq_init(chip);

	chg_debug( " switch1_gpio = %d,switch2_gpio = %d,reset_gpio = %d,clock_gpio = %d,data_gpio = %d,,data_irq = %d\n", chip->vooc_gpio.switch1_gpio, chip->vooc_gpio.switch2_gpio, chip->vooc_gpio.reset_gpio, chip->vooc_gpio.clock_gpio, chip->vooc_gpio.data_gpio, chip->vooc_gpio.data_irq);
	return rc;
#else
	// Parsing gpio swutch1
	chip->vooc_gpio.switch1_gpio = of_get_named_gpio(node, "qcom,charging_switch1-gpio", 0);
	if(chip->vooc_gpio.switch1_gpio < 0 ){
		pr_err("chip->vooc_gpio.switch1_gpio not specified\n");
	}
	else
	{
		if( gpio_is_valid(chip->vooc_gpio.switch1_gpio) ){
			rc = gpio_request(chip->vooc_gpio.switch1_gpio, "charging-switch1-gpio");
			if(rc){
				pr_err("unable to request gpio [%d]\n", chip->vooc_gpio.switch1_gpio);
			}
		}
		pr_err("chip->vooc_gpio.switch1_gpio =%d\n",chip->vooc_gpio.switch1_gpio);
	}

	// Parsing gpio swutch2
	//if(get_PCB_Version()== 0)
	if (1) {
		chip->vooc_gpio.switch2_gpio = of_get_named_gpio(node, "qcom,charging_switch3-gpio", 0);
		if(chip->vooc_gpio.switch2_gpio < 0 ){
			pr_err("chip->vooc_gpio.switch2_gpio not specified\n");
		}
		else
		{
			if( gpio_is_valid(chip->vooc_gpio.switch2_gpio) ){
				rc = gpio_request(chip->vooc_gpio.switch2_gpio, "charging-switch3-gpio");
				if(rc){
					pr_err("unable to request gpio [%d]\n", chip->vooc_gpio.switch2_gpio);
				}
			}
			pr_err("chip->vooc_gpio.switch2_gpio =%d\n",chip->vooc_gpio.switch2_gpio);
		}
	} else {
		chip->vooc_gpio.switch2_gpio = of_get_named_gpio(node, "qcom,charging_switch2-gpio", 0);
		if (chip->vooc_gpio.switch2_gpio < 0 ) {
			pr_err("chip->vooc_gpio.switch2_gpio not specified\n");
		} else {
			if ( gpio_is_valid(chip->vooc_gpio.switch2_gpio) ) {
				rc = gpio_request(chip->vooc_gpio.switch2_gpio, "charging-switch2-gpio");
				if (rc) {
					pr_err("unable to request gpio [%d]\n", chip->vooc_gpio.switch2_gpio);
				}
			}
			pr_err("chip->vooc_gpio.switch2_gpio =%d\n",chip->vooc_gpio.switch2_gpio);
		}
	}
	// Parsing gpio reset
	chip->vooc_gpio.reset_gpio = of_get_named_gpio(node, "qcom,charging_reset-gpio", 0);
	if (chip->vooc_gpio.reset_gpio < 0 ){
		pr_err("chip->vooc_gpio.reset_gpio not specified\n");
	} else {
		if ( gpio_is_valid(chip->vooc_gpio.reset_gpio) ) {
			rc = gpio_request(chip->vooc_gpio.reset_gpio, "charging-reset-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->vooc_gpio.reset_gpio);
			}
		}
		pr_err("chip->vooc_gpio.reset_gpio =%d\n",chip->vooc_gpio.reset_gpio);
	}

	// Parsing gpio clock
	chip->vooc_gpio.clock_gpio = of_get_named_gpio(node, "qcom,charging_clock-gpio", 0);
	if (chip->vooc_gpio.clock_gpio < 0 ) {
		pr_err("chip->vooc_gpio.reset_gpio not specified\n");
	} else {
		if ( gpio_is_valid(chip->vooc_gpio.clock_gpio) ){
			rc = gpio_request(chip->vooc_gpio.clock_gpio, "charging-clock-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->vooc_gpio.clock_gpio);
			}
		}
		pr_err("chip->vooc_gpio.clock_gpio =%d\n",chip->vooc_gpio.clock_gpio);
	}

	// Parsing gpio data
	chip->vooc_gpio.data_gpio = of_get_named_gpio(node, "qcom,charging_data-gpio", 0);
	if (chip->vooc_gpio.data_gpio < 0 ) {
		pr_err("chip->vooc_gpio.data_gpio not specified\n");
	} else {
		if ( gpio_is_valid(chip->vooc_gpio.data_gpio) ) {
			rc = gpio_request(chip->vooc_gpio.data_gpio, "charging-data-gpio");
			if (rc) {
				pr_err("unable to request gpio [%d]\n", chip->vooc_gpio.data_gpio);
			}
		}
		pr_err("chip->vooc_gpio.data_gpio =%d\n",chip->vooc_gpio.data_gpio);
	}

	oppo_vooc_data_irq_init(chip);

	rc =opchg_bq27541_gpio_pinctrl_init(chip);
	chg_debug( " switch1_gpio = %d,switch2_gpio = %d,reset_gpio = %d,clock_gpio = %d,data_gpio = %d,,data_irq = %d\n", chip->vooc_gpio.switch1_gpio, chip->vooc_gpio.switch2_gpio, chip->vooc_gpio.reset_gpio, chip->vooc_gpio.clock_gpio, chip->vooc_gpio.data_gpio, chip->vooc_gpio.data_irq);
	return rc;
#endif
 }

void opchg_set_clock_active(struct oppo_vooc_chip *chip)
{
	if(chip->mcu_boot_by_gpio) {
		chg_debug( " mcu_boot_by_gpio,return\n" );
		return ;
	}
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	mt_set_gpio_mode(chip->vooc_gpio.clock_gpio, GPIO_MODE_00);
	mt_set_gpio_dir(chip->vooc_gpio.clock_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(chip->vooc_gpio.clock_gpio, 0);
#else
	mutex_lock(&chip->pinctrl_mutex);
	pinctrl_select_state(chip->vooc_gpio.pinctrl,chip->vooc_gpio.gpio_clock_sleep);	// PULL_down

	gpio_direction_output(chip->vooc_gpio.clock_gpio,0);	// out 0
	mutex_unlock(&chip->pinctrl_mutex);
#endif
}

void opchg_set_clock_sleep(struct oppo_vooc_chip *chip)
{
	if(chip->mcu_boot_by_gpio) {
		chg_debug( " mcu_boot_by_gpio,return\n" );
		return ;
	}	
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	mt_set_gpio_mode(chip->vooc_gpio.clock_gpio, GPIO_MODE_00);
	mt_set_gpio_dir(chip->vooc_gpio.clock_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(chip->vooc_gpio.clock_gpio, 1);
#else
	mutex_lock(&chip->pinctrl_mutex);
	pinctrl_select_state(chip->vooc_gpio.pinctrl,chip->vooc_gpio.gpio_clock_active);// PULL_up
	gpio_direction_output(chip->vooc_gpio.clock_gpio,1);	// out 1
	mutex_unlock(&chip->pinctrl_mutex);
#endif
}

void opchg_set_data_active(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	mt_set_gpio_mode(chip->vooc_gpio.data_gpio, GPIO_MODE_00);
	mt_set_gpio_dir(chip->vooc_gpio.data_gpio, GPIO_DIR_IN);
#else
	mutex_lock(&chip->pinctrl_mutex);
	gpio_direction_input(chip->vooc_gpio.data_gpio);	// in
	pinctrl_select_state(chip->vooc_gpio.pinctrl,chip->vooc_gpio.gpio_data_active);	// no_PULL
	mutex_unlock(&chip->pinctrl_mutex);
#endif
}

void opchg_set_data_sleep(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	mt_set_gpio_mode(chip->vooc_gpio.data_gpio, GPIO_MODE_00);
	mt_set_gpio_dir(chip->vooc_gpio.data_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(chip->vooc_gpio.data_gpio, 1);
#else
	mutex_lock(&chip->pinctrl_mutex);
	pinctrl_select_state(chip->vooc_gpio.pinctrl,chip->vooc_gpio.gpio_data_sleep);// PULL_down
	gpio_direction_output(chip->vooc_gpio.data_gpio,0);	// out 1
	mutex_unlock(&chip->pinctrl_mutex);
#endif
}

void opchg_set_reset_active(struct oppo_vooc_chip *chip)
{
	if(chip->adapter_update_real == ADAPTER_FW_NEED_UPDATE || chip->btb_temp_over
		|| chip->mcu_update_ing) {
		chg_debug( " adapter_fw_need_update:%d,btb_temp_over:%d,mcu_update_ing:%d,return\n",
			  chip->adapter_update_real, chip->btb_temp_over, chip->mcu_update_ing);
		return ;
	}
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	mt_set_gpio_mode(chip->vooc_gpio.reset_gpio, GPIO_MODE_00);	//Set GPIO P9.3 as Output
	mt_set_gpio_dir(chip->vooc_gpio.reset_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(chip->vooc_gpio.reset_gpio, 0);
	msleep(5);
	mt_set_gpio_out(chip->vooc_gpio.reset_gpio, 1);
	msleep(10);
	mt_set_gpio_out(chip->vooc_gpio.reset_gpio, 0);	
	msleep(5);
	chg_debug( " call !\n" );
#else
	mutex_lock(&chip->pinctrl_mutex);
	gpio_direction_output(chip->vooc_gpio.reset_gpio,0);	// out 1
	#ifdef CONFIG_OPPO_CHARGER_MTK6771
	pinctrl_select_state(chip->vooc_gpio.pinctrl,chip->vooc_gpio.gpio_reset_sleep);	// PULL_down
	#else
	pinctrl_select_state(chip->vooc_gpio.pinctrl,chip->vooc_gpio.gpio_reset_active);	// PULL_up
	#endif
	gpio_set_value(chip->vooc_gpio.reset_gpio,0);
	usleep_range(5000,5000);
	gpio_set_value(chip->vooc_gpio.reset_gpio,1);
	usleep_range(10000,10000);
	gpio_set_value(chip->vooc_gpio.reset_gpio,0);
	usleep_range(5000,5000);
	mutex_unlock(&chip->pinctrl_mutex);
	pr_err("%s\n", __func__);
#endif
}

int oppo_vooc_get_reset_gpio_val(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	return mt_get_gpio_out(chip->vooc_gpio.reset_gpio);
#else
	return gpio_get_value(chip->vooc_gpio.reset_gpio);
#endif
}

bool oppo_is_power_off_charging(struct oppo_vooc_chip *chip)
{
	if(get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT)
		return true;
	else
		return false;
}
bool oppo_is_charger_reboot(struct oppo_vooc_chip *chip)
{
		int charger_type;
		
		charger_type = oppo_chg_get_chg_type();
		if(charger_type == 5) {
			chg_debug("dont need check fw_update\n");
			return true;
		} else {
			return false;
		}
}
static void delay_reset_mcu_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oppo_vooc_chip *chip = container_of(dwork,struct oppo_vooc_chip, delay_reset_mcu_work);

	opchg_set_clock_sleep(chip);
	opchg_set_reset_active(chip);
}

void oppo_vooc_delay_reset_mcu_init(struct oppo_vooc_chip *chip)
{
	INIT_DELAYED_WORK(&chip->delay_reset_mcu_work, delay_reset_mcu_work_func);
}

static void oppo_vooc_delay_reset_mcu(struct oppo_vooc_chip *chip)
{
	schedule_delayed_work(&chip->delay_reset_mcu_work, 
				round_jiffies_relative(msecs_to_jiffies(1500)));
}

static bool is_allow_fast_chg_real(void)
{
	//bool auth = false,low_temp_full = false;
	int temp = 0,cap = 0,chg_type = 0;
	
	temp = oppo_chg_get_chg_temperature();
	cap = oppo_chg_get_ui_soc();
	chg_type = oppo_chg_get_chg_type();
	//low_temp_full = oppo_vooc_get_fastchg_low_temp_full();
	//chg_err("temp:%d,cap:%d,chg_type:%d\n",temp,cap,chg_type);

	if(chg_type != POWER_SUPPLY_TYPE_USB_DCP) {
		return false;
    }

    if(temp < vooc_low_temp){
		return false;
	}


	if(temp > 430){
		return false;
	}
	if(cap < 1) {
		return false;
    }
	if(cap > 85){
		return false;
	}

	if(oppo_vooc_get_fastchg_to_normal() == true){
		chg_debug( "  oppo_vooc_get_fastchg_to_normal is true\n" );
		return false;
	}
	return true;
}

static bool is_allow_fast_chg_dummy(void)
{
	int chg_type = 0;
	bool allow_real = false;

	chg_type = oppo_chg_get_chg_type();
	//chg_err(" chg_type:%d\n",  chg_type);
	if(chg_type != POWER_SUPPLY_TYPE_USB_DCP) {
		return false;
    }
	if(oppo_vooc_get_fastchg_to_normal() == true){
		chg_debug( " fast_switch_to_noraml is true\n" );
		return false;
	}

	allow_real = is_allow_fast_chg_real();
	if(oppo_vooc_get_fastchg_dummy_started() == true && (!allow_real)) {
		chg_debug( " dummy_started true,allow_real false\n" );
		return false;
	}
	oppo_vooc_set_fastchg_allow(allow_real);
	return true;
}

void switch_fast_chg(struct oppo_vooc_chip *chip)
{
	bool allow_real = false;
	
	//if(mt_get_gpio_out(chip->vooc_gpio.switch1_gpio) == 1)
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	if(chip->dpdm_switch_mode == VOOC_CHARGER_MODE
			&& mt_get_gpio_out(chip->vooc_gpio.switch1_gpio) == 1) {
		if (oppo_vooc_get_fastchg_started() == false) {
			allow_real = is_allow_fast_chg_real();
			oppo_vooc_set_fastchg_allow(allow_real);
		}
		return;
	}
#else
	if (chip->dpdm_switch_mode == VOOC_CHARGER_MODE
		&& gpio_get_value(chip->vooc_gpio.switch1_gpio) == 1) {
		if(oppo_vooc_get_fastchg_started() == false) {
			allow_real = is_allow_fast_chg_real();
			oppo_vooc_set_fastchg_allow(allow_real);
		}
		return;
	}
#endif

	if(is_allow_fast_chg_dummy() == true) {
		if(oppo_vooc_get_adapter_update_status() == ADAPTER_FW_UPDATE_FAIL) {
			opchg_set_switch_mode(chip, VOOC_CHARGER_MODE);
			oppo_vooc_delay_reset_mcu(chip);
		} else {
			if(oppo_vooc_get_fastchg_allow() == false && oppo_vooc_get_fastchg_to_warm() == true) {
				chg_debug( "fastchg_allow false, to_warm true, don't switch to vooc mode\n");
			} else {
				opchg_set_switch_mode(chip, VOOC_CHARGER_MODE);
				opchg_set_clock_sleep(chip);
				opchg_set_reset_active(chip);
			}
		}
	}
	chg_debug( " end,allow_real:%d\n", oppo_vooc_get_fastchg_allow());
}

int oppo_vooc_get_ap_clk_gpio_val(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	return mt_get_gpio_out(chip->vooc_gpio.clock_gpio);
#else
	return gpio_get_value(chip->vooc_gpio.clock_gpio);
#endif
}

int opchg_get_gpio_ap_data(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	return mt_get_gpio_in(chip->vooc_gpio.data_gpio);
#else
	return gpio_get_value(chip->vooc_gpio.data_gpio);
#endif
}

int opchg_read_ap_data(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	int bit = 0;
	mt_set_gpio_mode(chip->vooc_gpio.clock_gpio, GPIO_MODE_00);	//Set GPIO P9.3 as Output
	mt_set_gpio_mode(chip->vooc_gpio.data_gpio, GPIO_MODE_00);	//Set GPIO P9.3 as Output
	mt_set_gpio_dir(chip->vooc_gpio.clock_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(chip->vooc_gpio.clock_gpio, 0);
	usleep_range(1000,1000);
	mt_set_gpio_out(chip->vooc_gpio.clock_gpio, 1);
	usleep_range(19000,19000);
	mt_set_gpio_dir(chip->vooc_gpio.data_gpio, GPIO_DIR_IN);
	bit = mt_get_gpio_in(chip->vooc_gpio.data_gpio); 
	return bit;
#else
	int bit = 0;
	opchg_set_clock_active(chip);
	usleep_range(1000,1000);
	opchg_set_clock_sleep(chip);
	usleep_range(19000,19000);
	bit = gpio_get_value(chip->vooc_gpio.data_gpio);

	return bit;
#endif
}

void opchg_reply_mcu_data(struct oppo_vooc_chip *chip, int ret_info,int device_type)
{
	int i = 0;
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	for(i = 0; i < 3; i++) {
		if(i == 0){	//tell mcu1503 device_type
			mt_set_gpio_out(chip->vooc_gpio.data_gpio, ret_info >> 1);
		} else if(i == 1){
			mt_set_gpio_out(chip->vooc_gpio.data_gpio, ret_info & 0x1);
		} else {
			mt_set_gpio_out(chip->vooc_gpio.data_gpio,device_type);
			chg_debug( "device_type = %d\n",device_type);
		}
		opchg_set_clock_active(chip);
		usleep_range(1000,1000);
		opchg_set_clock_sleep(chip);
		usleep_range(19000,19000);
	}
#else
	for (i = 0; i < 3; i++) {
		if(i == 0){	//tell mcu1503 device_type
			gpio_set_value(chip->vooc_gpio.data_gpio, ret_info >> 1);
		} else if(i == 1){
			gpio_set_value(chip->vooc_gpio.data_gpio, ret_info & 0x1);
		} else {
			gpio_set_value(chip->vooc_gpio.data_gpio,device_type);
			printk("device_type = %d\n",device_type);
		}
		opchg_set_clock_active(chip);
		usleep_range(1000,1000);
		opchg_set_clock_sleep(chip);
		usleep_range(19000,19000);
	}
#endif
}

static void opchg_set_switch_fast_charger(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	mt_set_gpio_mode(chip->vooc_gpio.switch1_gpio, GPIO_MODE_00);	//Set GPIO P9.3 as Output
	mt_set_gpio_dir(chip->vooc_gpio.switch1_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(chip->vooc_gpio.switch1_gpio, 1);
#else
	gpio_direction_output(chip->vooc_gpio.switch1_gpio,1);	// out 1
#endif
}

static void opchg_set_switch_normal_charger(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	mt_set_gpio_mode(chip->vooc_gpio.switch1_gpio, GPIO_MODE_00);	//Set GPIO P9.3 as Output
	mt_set_gpio_dir(chip->vooc_gpio.switch1_gpio, GPIO_DIR_OUT);
	mt_set_gpio_out(chip->vooc_gpio.switch1_gpio, 0);
#else
	if (chip->vooc_gpio.switch1_gpio > 0) {
		 gpio_direction_output(chip->vooc_gpio.switch1_gpio,0);	// in 0
	}
#endif
}

static void opchg_set_switch_earphone(struct oppo_vooc_chip *chip)
{

}


void opchg_set_switch_mode(struct oppo_vooc_chip *chip, int mode)
{
	//chg_err("-------------------------mode = %d\r\n",mode);
	if(chip->adapter_update_real == ADAPTER_FW_NEED_UPDATE || chip->btb_temp_over) {
		chg_debug( " adapter_fw_need_update:%d,btb_temp_over:%d\n",
			  chip->adapter_update_real, chip->btb_temp_over);
		return ;
	}
	if(mode == VOOC_CHARGER_MODE && chip->mcu_update_ing) {
		chg_debug( " mcu_update_ing,don't switch to vooc mode\n" );
		return ;
	}
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
    switch(mode) {
        case VOOC_CHARGER_MODE:	//11
			opchg_set_switch_fast_charger(chip);
			chg_debug( " vooc mode,switch1_gpio:%d\n" ,mt_get_gpio_out(chip->vooc_gpio.switch1_gpio));
			break;
            
        case HEADPHONE_MODE:		//10
			opchg_set_switch_earphone(chip);
			chg_debug( " headphone mode,switch1_gpio:%d\n" ,mt_get_gpio_out(chip->vooc_gpio.switch1_gpio));
			break;
            
        case NORMAL_CHARGER_MODE:	//01
        default:
			opchg_set_switch_normal_charger(chip);
			chg_debug( " normal mode,switch1_gpio:%d\n" ,mt_get_gpio_out(chip->vooc_gpio.switch1_gpio));
			break;
    }
#else
	switch(mode) {
	case VOOC_CHARGER_MODE:	//11
		opchg_set_switch_fast_charger(chip);
		pr_err("%s vooc mode,switch1_gpio:%d\n",__func__,gpio_get_value(chip->vooc_gpio.switch1_gpio));
		break;

	case HEADPHONE_MODE:		//10
		opchg_set_switch_earphone(chip);
		pr_err("%s headphone mode,switch1_gpio:%d\n",__func__,gpio_get_value(chip->vooc_gpio.switch1_gpio));
		break;

	case NORMAL_CHARGER_MODE:	//01
	default:
		opchg_set_switch_normal_charger(chip);
		pr_err("%s normal mode,switch1_gpio:%d\n",__func__,gpio_get_value(chip->vooc_gpio.switch1_gpio));
		break;
	}
#endif
	chip->dpdm_switch_mode = mode;
}

int oppo_vooc_get_switch_gpio_val(struct oppo_vooc_chip *chip)
{
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	return mt_get_gpio_out(chip->vooc_gpio.switch1_gpio);
#else
	return gpio_get_value(chip->vooc_gpio.switch1_gpio);
#endif
}

void reset_fastchg_after_usbout(struct oppo_vooc_chip *chip)
{
	if(oppo_vooc_get_fastchg_started() == false) {
		chg_debug( " switch off fastchg\n" );
		opchg_set_switch_mode(chip, NORMAL_CHARGER_MODE);
	}

	oppo_vooc_set_fastchg_to_normal_false();
	oppo_vooc_set_fastchg_to_warm_false();
	oppo_vooc_set_fastchg_low_temp_full_false();
	oppo_vooc_set_fastchg_dummy_started_false();
}

static irqreturn_t irq_rx_handler(int irq, void *dev_id)
{
	oppo_vooc_shedule_fastchg_work();
	return IRQ_HANDLED;
}

void oppo_vooc_data_irq_init(struct oppo_vooc_chip *chip)
{
	struct device_node *node = NULL;
	struct device_node *node_new = NULL;
	u32 intr[2] = {0,0};
	
	node = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_AP_DATA-eint");
	node_new = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_EINT_NEW_FUNCTION");
	if (node) {
		if (node_new) {
#if defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
			chip->vooc_gpio.data_irq = gpio_to_irq(chip->vooc_gpio.data_gpio);
#endif
			chg_err("vooc_gpio.data_irq:%d\n", chip->vooc_gpio.data_irq);
		} else {
			of_property_read_u32_array(node , "interrupts", intr, ARRAY_SIZE(intr));
			chg_debug( " intr[0]  = %d, intr[1]  = %d\r\n",intr[0] ,intr[1] );
			chip->vooc_gpio.data_irq = irq_of_parse_and_map(node, 0);
		}
	} else {
		chg_err(" node not exist!\r\n");
		chip->vooc_gpio.data_irq = CUST_EINT_MCU_AP_DATA;
	}	
}

void oppo_vooc_eint_register(struct oppo_vooc_chip *chip)
{
	static int register_status = 0;
	int ret = 0;
	struct device_node *node = NULL;
	node = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_EINT_NEW_FUNCTION");
	if(node) {
		opchg_set_data_active(chip);
		ret = request_irq(chip->vooc_gpio.data_irq, (irq_handler_t)irq_rx_handler, IRQF_TRIGGER_RISING, "VOOC_AP_DATA-eint", chip);	//0X01:rising edge,0x02:falling edge
		if (ret < 0) {
			chg_err("ret = %d, oppo_vooc_eint_register failed to request_irq \n", ret);
		}
	} else {
		if (!register_status) {
			opchg_set_data_active(chip);
			ret = request_irq(chip->vooc_gpio.data_irq, (irq_handler_t)irq_rx_handler, IRQF_TRIGGER_RISING, "VOOC_AP_DATA-eint",  NULL);
			if (ret) {
				chg_err("ret = %d, oppo_vooc_eint_register failed to request_irq \n", ret);
			}
			register_status = 1;
		} else {
			chg_debug( " enable_irq!\r\n");
			enable_irq(chip->vooc_gpio.data_irq);
		}	
	}
}

void oppo_vooc_eint_unregister(struct oppo_vooc_chip *chip)
{
	struct device_node *node = NULL;
	node = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_EINT_NEW_FUNCTION");
	chg_debug(" disable_irq_mtk!\r\n");
	if(node) {
		free_irq(chip->vooc_gpio.data_irq, chip);
	} else {
		disable_irq(chip->vooc_gpio.data_irq);
	}
}


