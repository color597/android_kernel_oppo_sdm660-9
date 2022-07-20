/**********************************************************************************
* Copyright (c)  2008-2015  Guangdong OPPO Mobile Comm Corp., Ltd
* VENDOR_EDIT
* Description: Charger IC management module for charger system framework.
*              Manage all charger IC and define abstarct function flow.
* Version   : 1.0
* Date      : 2015-06-22
* Author    : fanhui@PhoneSW.BSP
* 			: Fanhong.Kong@ProDrv.CHG
* ------------------------------ Revision History: --------------------------------
* <version>       <date>        <author>              			<desc>
* Revision 1.0    2015-06-22    fanhui@PhoneSW.BSP    			Created for new architecture
* Revision 1.0    2015-06-22    Fanhong.Kong@ProDrv.CHG   		Created for new architecture
***********************************************************************************/

#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/wait.h>		/* For wait queue*/
#include <linux/sched.h>	/* For wait queue*/
#include <linux/kthread.h>	/* For Kthread_run */
#include <linux/platform_device.h>	/* platform device */
#include <linux/time.h>
#include <linux/wakelock.h>

#include <linux/netlink.h>	/* netlink */
#include <linux/kernel.h>
#include <linux/socket.h>	/* netlink */
#include <linux/skbuff.h>	/* netlink */
#include <net/sock.h>		/* netlink */
#include <linux/cdev.h>		/* cdev */
#include <linux/gpio.h>
#include <mt-plat/mtk_gpio.h>
#include <linux/of_gpio.h>


#include <linux/err.h>	/* IS_ERR, PTR_ERR */
#include <linux/reboot.h>	/*kernel_power_off*/
#include <linux/proc_fs.h>

#include <linux/vmalloc.h>
#include <linux/power_supply.h>
#include <mach/mtk_charging.h>
#include <mt-plat/charging.h>
#ifdef CONFIG_OPPO_CHARGER_MTK6771
#include <oppo_bq24190.h>
#else
#include <oppo_bq24196.h>
#endif
#include "../oppo_gauge.h"
extern CHARGER_TYPE MTK_CHR_Type_num;
extern CHARGER_TYPE mt_charger_type_detection(void);
extern int mt_charger_type_detection_bq25890h(void);
extern int smb1351_get_charger_type(void);
extern int charger_ic_flag;
extern bool upmu_is_chr_det(void);
extern CHARGER_TYPE g_chr_type;
extern int otg_is_exist;
#ifdef VENDOR_EDIT//Qiao.Hu@BSP.BaseDrv.CHG.Basic,add 2017/12/09 for shipmode  stm6620
static bool oppo_ship_check_is_gpio(struct oppo_chg_chip *chip);
int oppo_ship_gpio_init(struct oppo_chg_chip *chip);
void smbchg_enter_shipmode(struct oppo_chg_chip *chip);

static bool oppo_ship_check_is_gpio(struct oppo_chg_chip *chip)
{
	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

int oppo_ship_gpio_init(struct oppo_chg_chip *chip)
{
	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	chip->normalchg_gpio.ship_active = 
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
			"ship_active");

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}
	chip->normalchg_gpio.ship_sleep = 
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
				"ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
		chip->normalchg_gpio.ship_sleep);
	return 0;
}

#define SHIP_MODE_CONFIG		0x40
#define SHIP_MODE_MASK			BIT(0)
#define SHIP_MODE_ENABLE		0
#define PWM_COUNT				5
void smbchg_enter_shipmode(struct oppo_chg_chip *chip)
{
	int i = 0;
	chg_err("enter smbchg_enter_shipmode\n");

	if (oppo_ship_check_is_gpio(chip) == true) {
		chg_err("select gpio control\n");
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active) && !IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
			for (i = 0; i < PWM_COUNT; i++) {
				//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 1);
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
				mdelay(3);
				//gpio_direction_output(chip->normalchg_gpio.ship_gpio, 0);
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
				mdelay(3);
			}
		}
		chg_err("power off after 15s\n");
	}
}
void enter_ship_mode_function(struct oppo_chg_chip *chip)
{
	if(chip != NULL){
		if (chip->enable_shipmode) {
			printk("kernel_power_off\n");
			smbchg_enter_shipmode(chip);
		}
	}
}

#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
//tongfeng.Huang@BSP.BaseDrv.CHG.Basic,add 2018/02/09 for short c hw check;
bool oppo_shortc_check_is_gpio(struct oppo_chg_chip *chip)
{
	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio))
	{
		return true;
	}
	return false;
}

int oppo_shortc_gpio_init(struct oppo_chg_chip *chip)
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
bool oppo_chg_get_shortc_hw_gpio_status(struct oppo_chg_chip *chip)
{
	bool shortc_hw_status = 1;

	if(oppo_shortc_check_is_gpio(chip) == true) {	
		shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else
bool oppo_chg_get_shortc_hw_gpio_status(struct oppo_chg_chip *chip)
{
	bool shortc_hw_status = 1;

	return shortc_hw_status;
}
#endif	
int oppo_chg_shortc_hw_parse_dt(struct oppo_chg_chip *chip)
{
	int rc;	
    struct device_node *node = chip->dev->of_node;
	
    if (!node) {
        dev_err(chip->dev, "device tree info. missing\n");
        return -EINVAL;
    }

	if (chip) {
		chip->normalchg_gpio.shortc_gpio = of_get_named_gpio(node, "qcom,shortc_gpio", 0);
		if (chip->normalchg_gpio.shortc_gpio <= 0) {
			chg_err("Couldn't read qcom,shortc_gpio rc = %d, qcom,shortc_gpio:%d\n", 
			rc, chip->normalchg_gpio.shortc_gpio);
		} else {
			if(oppo_shortc_check_is_gpio(chip) == true) {
				chg_debug("This project use gpio for shortc hw check\n");
				rc = gpio_request(chip->normalchg_gpio.shortc_gpio, "shortc_gpio");
				if(rc){
					chg_err("unable to request shortc_gpio:%d\n", 
					chip->normalchg_gpio.shortc_gpio);
				} else {
					oppo_shortc_gpio_init(chip);
				}
			} else {
				chg_err("chip->normalchg_gpio.shortc_gpio is not valid or get_PCB_Version() < V0.3:%d\n", 
				get_PCB_Version());
			}
			chg_err("shortc_gpio:%d\n", chip->normalchg_gpio.shortc_gpio);
		}
	}
	return rc;
}
#endif

int mt_power_supply_type_check(void)
{
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	int charger_type_final = 0;
	if(otg_is_exist == 1)
		return g_chr_type;
//	pr_err("mt_power_supply_type_check-----1---------charger_type = %d,charger_type_first = %d\r\n",charger_type,charger_type_first);
	if(true == upmu_is_chr_det()) {
		if(charger_ic_flag == 0) {
			charger_type_final = mt_charger_type_detection();
		} else if(charger_ic_flag == 1){
			charger_type_final = mt_charger_type_detection_bq25890h();
		} else {
			charger_type_final = smb1351_get_charger_type();
		}
		g_chr_type = charger_type_final;
	}
	else {
		chg_debug(" call first type\n");
		charger_type_final = g_chr_type;
	}

	switch(charger_type_final) {
	case CHARGER_UNKNOWN:
		break;
	case STANDARD_HOST:
	case CHARGING_HOST:
		charger_type = POWER_SUPPLY_TYPE_USB;
		break;
	case NONSTANDARD_CHARGER:
	case APPLE_0_5A_CHARGER:
	case STANDARD_CHARGER:
	case APPLE_2_1A_CHARGER:
	case APPLE_1_0A_CHARGER:
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	default:
		break;
	}
	pr_err("mt_power_supply_type_check-----2---------charger_type = %d,charger_type_final = %d\r\n",charger_type,charger_type_final);
	return charger_type;

}


enum {
    Channel_12 = 2,
    Channel_13,
    Channel_14,
};
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int* rawdata);
extern int IMM_IsAdcInitReady(void);
int mt_vadc_read(int times, int Channel)
{
    int ret = 0, data[4], i, ret_value = 0, ret_temp = 0;
    if( IMM_IsAdcInitReady() == 0 )
    {
        return 0;
    }
    i = times ;
    while (i--)
    {
	ret_value = IMM_GetOneChannelValue(Channel, data, &ret_temp);
	if(ret_value != 0)
	{
		i++;
		continue;
	}
	ret += ret_temp;
    }
	ret = ret*1500/4096;
    ret = ret/times;
	chg_debug("[mt_vadc_read] Channel %d: vol_ret=%d\n",Channel,ret);
	return ret;
}
static void set_usbswitch_to_rxtx(struct oppo_chg_chip *chip)
{

	int gpio_status = 0;
	int ret = 0;
	gpio_direction_output(chip->usb_switch_gpio, 1);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output2);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return ;
	}
}
static void set_usbswitch_to_dpdm(struct oppo_chg_chip *chip)
{
	int gpio_status = 0;
	int ret = 0;
	gpio_direction_output(chip->usb_switch_gpio, 0);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output1);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return ;
	}
	chg_err("set_usbswitch_to_dpdm \n");
}
static bool is_support_chargerid_check(void)
{

#ifdef CONFIG_OPPO_CHECK_CHARGERID_VOLT
	return true;
#else
	return false;
#endif

}
int mt_get_chargerid_volt (struct oppo_chg_chip *chip)
{
	int chargerid_volt = 0;
	if(is_support_chargerid_check() == true)
	{
		chargerid_volt = mt_vadc_read(10,Channel_14);//get the charger id volt
		chg_debug("chargerid_volt = %d \n",
					   chargerid_volt);
	}
		else
		{
		chg_debug("is_support_chargerid_check = false !\n");
		return 0;
	}
	return chargerid_volt;
		}


void mt_set_chargerid_switch_val(struct oppo_chg_chip *chip, int value)
{
	chg_debug("set_value= %d\n",value);
	if(NULL == chip)
		return;
	if(is_support_chargerid_check() == false)
		return;
	if(chip->usb_switch_gpio <= 0) {
		chg_err("usb_shell_ctrl_gpio not exist, return\n");
		return;
	}
	if(IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output1)
		|| IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("pinctrl null, return\n");
		return;
	}
	if(1 == value){
			set_usbswitch_to_rxtx(chip);
	}else if(0 == value){
		set_usbswitch_to_dpdm(chip);
	}else{
		//do nothing
	}
	chg_debug("get_val:%d\n",gpio_get_value(chip->usb_switch_gpio));
}

int mt_get_chargerid_switch_val(struct oppo_chg_chip *chip)
{
	int gpio_status = 0;
	if(NULL == chip)
		return 0;
	if(is_support_chargerid_check() == false)
		return 0;
	gpio_status = gpio_get_value(chip->usb_switch_gpio);

	chg_debug("mt_get_chargerid_switch_val:%d\n",gpio_status);
	return gpio_status;
}




void mt_set_usb_shell_ctrl_val(struct oppo_chg_chip *chip, int value)
{
	if(chip->usb_shell_ctrl_gpio.usb_shell_gpio <= 0) {
		chg_err("usb_shell_ctrl_gpio not exist, return\n");
		return;
	}
	if(IS_ERR_OR_NULL(chip->usb_shell_ctrl_gpio.pinctrl)
		|| IS_ERR_OR_NULL(chip->usb_shell_ctrl_gpio.usb_shell_ctrl_active)
		|| IS_ERR_OR_NULL(chip->usb_shell_ctrl_gpio.usb_shell_ctrl_sleep)) {
		chg_err("pinctrl null, return\n");
		return;
	}
	if(value) {
		gpio_direction_output(chip->usb_shell_ctrl_gpio.usb_shell_gpio, 1);
		pinctrl_select_state(chip->usb_shell_ctrl_gpio.pinctrl,
				chip->usb_shell_ctrl_gpio.usb_shell_ctrl_active);
	} else {
		gpio_direction_output(chip->usb_shell_ctrl_gpio.usb_shell_gpio, 0);
		pinctrl_select_state(chip->usb_shell_ctrl_gpio.pinctrl,
				chip->usb_shell_ctrl_gpio.usb_shell_ctrl_sleep);
	}
	chg_err("set value:%d, gpio_val:%d\n",
		value, gpio_get_value(chip->usb_shell_ctrl_gpio.usb_shell_gpio));
	
}

int mt_get_usb_shell_ctrl_val(struct oppo_chg_chip *chip)
{
	int gpio_status = -1;
	if(chip->usb_shell_ctrl_gpio.usb_shell_gpio <= 0) {
		chg_err("usb_shell_ctrl_gpio not exist, return\n");
		return -1;
	}
	gpio_status = gpio_get_value(chip->usb_shell_ctrl_gpio.usb_shell_gpio);
	chg_debug("get_usb_shell_ctrl:%d\n",gpio_status);
	return gpio_status;
}

int oppo_usb_shell_ctrl_switch_gpio_init(struct oppo_chg_chip *chip)
{

	chg_err("---1-----");
	chip->usb_shell_ctrl_gpio.pinctrl  = devm_pinctrl_get(chip->dev);
	if(IS_ERR_OR_NULL(chip->usb_shell_ctrl_gpio.pinctrl)){
		chg_err("get usb_shell_ctrl_gpio pinctrl falil\n");
		return -EINVAL;
	}
	chg_err("---2-----");
	chip->usb_shell_ctrl_gpio.usb_shell_ctrl_active = 
			pinctrl_lookup_state(chip->usb_shell_ctrl_gpio.pinctrl,"usb_shell_ctrl_output_high");
	if (IS_ERR_OR_NULL(chip->usb_shell_ctrl_gpio.usb_shell_ctrl_active)) {
		chg_err("get chargerid_switch_active fail\n");
		return -EINVAL;
	}

	chip->usb_shell_ctrl_gpio.usb_shell_ctrl_sleep = 
			pinctrl_lookup_state(chip->usb_shell_ctrl_gpio.pinctrl,"usb_shell_ctrl_output_low");
	if (IS_ERR_OR_NULL(chip->usb_shell_ctrl_gpio.usb_shell_ctrl_sleep)) {
		chg_err("get chargerid_switch_active fail\n");
		return -EINVAL;
	}
	pinctrl_select_state(chip->usb_shell_ctrl_gpio.pinctrl,
		chip->usb_shell_ctrl_gpio.usb_shell_ctrl_sleep);

	return 0;
	
}

int oppo_usb_switch_gpio_gpio_init(struct oppo_chg_chip *chip)
{
	int rc;
	chg_err("---1-----");
	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
    if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
       chg_err("get usb_switch_gpio pinctrl falil\n");
		return -EINVAL;
    }
    chip->normalchg_gpio.charger_gpio_as_output1 = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
								"charger_gpio_as_output_low");
    if (IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output1)) {
       	chg_err("get charger_gpio_as_output_low fail\n");
			return -EINVAL;
    }
	chip->normalchg_gpio.charger_gpio_as_output2 = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
								"charger_gpio_as_output_high");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("get charger_gpio_as_output_high fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,chip->normalchg_gpio.charger_gpio_as_output1);	
	return 0;
}


int charger_pretype_get(void)
{
	int chg_type = STANDARD_HOST;
	//chg_type = hw_charging_get_charger_type();
	chg_type = 4;
	return chg_type;
}

int oppo_battery_meter_get_battery_voltage(void)
{
	//return battery_get_bat_voltage();
	return 4000;
}

bool oppo_pmic_check_chip_is_null(void)
{
	if (!oppo_fuelgauged_init_flag) {
		return true;
	} else {
		return false;
	}
}

