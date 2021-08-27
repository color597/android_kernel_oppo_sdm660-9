#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include "oppo_touchscreen/Synaptics/S3508/synaptics_s3508.h"
#include "oppo_touchscreen/tp_devices.h"
#include "oppo_touchscreen/touchpanel_common.h" 
#include "touch.h"

#define MAX_LIMIT_DATA_LENGTH         100
#define GTP_I2C_NAME                    "Goodix-TS"
#define GT5688_FW_NAME                  "tp/16061/GT5688_Firmware.BIN"
#define S3508_FW_NAME "tp/16051/16051_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME "tp/16051/16051_Limit_data.img"
#define S3320_FW_NAME "tp/16103/16103_FW_S3320_JDI.img"
#define S3320_BASELINE_TEST_LIMIT_NAME "tp/16103/16103_Limit_data.img"
#define S3508_FW_NAME_16118 "tp/16118/16118_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME_16118 "tp/16118/16118_Limit_data.img"
#define S3508_FW_NAME_17011 "tp/17011/17011_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME_17011 "tp/17011/17011_Limit_data.img"
#define S3508_FW_NAME_17021 "tp/17021/17021_FW_S3508_SYNAPTICS.img"
#define S3508_BASELINE_TEST_LIMIT_NAME_17021 "tp/17021/17021_Limit_data.img"
#define S3706_FW_NAME_17081 "tp/17081/17081_FW_S3706_SYNAPTICS.img"
#define S3706_BASELINE_TEST_LIMIT_NAME_17081 "tp/17081/17081_Limit_data.img"
#define S3706_FW_NAME_18001 "tp/18001/18001_FW_S3706_SYNAPTICS.img"
#define S3706_BASELINE_TEST_LIMIT_NAME_18001 "tp/18001/18001_Limit_data.img"
#define S3706_FW_NAME_18005 "tp/18005/18005_FW_S3706_SYNAPTICS.img"
#define S3706_BASELINE_TEST_LIMIT_NAME_18005 "tp/18005/18005_Limit_data.img"
#define S3706_FW_NAME_18323 "tp/18323/18323_FW_S3706_SYNAPTICS.img"
#define S3706_BASELINE_TEST_LIMIT_NAME_18323 "tp/18323/18323_Limit_data.img"


#define NT36672_NF_CHIP_NAME "NT_NF36672"
#define NT36672_NF_FW_NAME_18315 "tp/18315/18315_FW_NT36672_NF_Nova.img"
#define NT36672_NF_BASELINE_TEST_LIMIT_NAME_18315 "tp/18315/18315_Limit_data.img"

#define HX83112A_NF_CHIP_NAME "HX_NF83112A"
#define HX83112A_NF_FW_NAME_18315 "tp/18315/18315_FW_HX83112A_NF_Himax.img"
#define HX83112A_NF_BASELINE_TEST_LIMIT_NAME_18315 "tp/18315/18315_Limit_data.img"

struct tp_dev_name tp_dev_names[] = {
     {TP_OFILM, "OFILM"},
     {TP_BIEL, "BIEL"},
     {TP_TRULY, "TRULY"},
     {TP_BOE, "BOE"},
     {TP_G2Y, "G2Y"},
     {TP_TPK, "TPK"},
     {TP_JDI, "JDI"},
     {TP_TIANMA, "TIANMA"},
     {TP_SAMSUNG, "SAMSUNG"},
     {TP_DSJM, "DSJM"},
     {TP_BOE_B8, "BOEB8"},
     {TP_INNOLUX, "INNOLUX"},
     {TP_HIMAX_DPT, "DPT"},
     {TP_AUO, "AUO"},
     {TP_DEPUTE, "DEPUTE"},
     {TP_UNKNOWN, "UNKNOWN"},
};

#define GET_TP_DEV_NAME(tp_type) ((tp_dev_names[tp_type].type == (tp_type))?tp_dev_names[tp_type].name:"UNMATCH")

int g_tp_dev_vendor = TP_UNKNOWN;
char *g_tp_chip_name;
static bool is_tp_type_got_in_match = false;    /*indicate whether the tp type is got in the process of ic match*/

/*
* this function is used to judge whether the ic driver should be loaded
* For incell module, tp is defined by lcd module, so if we judge the tp ic
* by the boot command line of containing lcd string, we can also get tp type.
*/
bool __init tp_judge_ic_match(char * tp_ic_name)
{
    pr_err("[TP] tp_ic_name = %s \n", tp_ic_name);
    pr_err("[TP] boot_command_line = %s \n", boot_command_line);

    switch(get_project()) {
    case OPPO_18316:
        is_tp_type_got_in_match = true;
        if (strstr(tp_ic_name, "nt36672") && strstr(boot_command_line, "tianma_nt36672")) {
            g_tp_dev_vendor = TP_TIANMA;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(NT36672_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = NT36672_NF_CHIP_NAME;
            #endif
            return true;
        }
        if (strstr(tp_ic_name, "nt36672") && strstr(boot_command_line, "dpt_jdi_nt36672")) {
            g_tp_dev_vendor = TP_DEPUTE;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(NT36672_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = NT36672_NF_CHIP_NAME;
            #endif
            return true;
        }
        if (strstr(tp_ic_name, "hx83112a_nf") && strstr(boot_command_line, "himax_hx83112")) {
            g_tp_dev_vendor = TP_DSJM;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(HX83112A_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = HX83112A_NF_CHIP_NAME;
            #endif
            return true;
        }
        pr_err("[TP] Driver does not match the project\n");
        break;
    case OPPO_18321:
        is_tp_type_got_in_match = true;
        if (strstr(tp_ic_name, "nt36672") && strstr(boot_command_line, "tianma_nt36672")) {
            g_tp_dev_vendor = TP_TIANMA;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(NT36672_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = NT36672_NF_CHIP_NAME;
            #endif
            return true;
        }
        if (strstr(tp_ic_name, "hx83112a_nf") && strstr(boot_command_line, "himax_hx83112")) {
            g_tp_dev_vendor = TP_DSJM;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(HX83112A_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = HX83112A_NF_CHIP_NAME;
            #endif
            return true;
        }
        if (strstr(tp_ic_name, "nt36672") && strstr(boot_command_line, "dpt_jdi_nt36672")) {
            g_tp_dev_vendor = TP_DEPUTE;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(NT36672_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = NT36672_NF_CHIP_NAME;
            #endif
            return true;
        }
        pr_err("[TP] Driver does not match the project\n");
        break;
		/*case OPPO_18325:
        is_tp_type_got_in_match = true;
        if (strstr(tp_ic_name, "nt36672") && strstr(boot_command_line, "tianma_nt36672")) {
            g_tp_dev_vendor = TP_TIANMA;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(NT36672_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = NT36672_NF_CHIP_NAME;
            #endif
            return true;
        }
        if (strstr(tp_ic_name, "hx83112a_nf") && strstr(boot_command_line, "himax_hx83112")) {
            g_tp_dev_vendor = TP_DSJM;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(HX83112A_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = HX83112A_NF_CHIP_NAME;
            #endif
            return true;
        }
		if (strstr(tp_ic_name, "nt36672") && strstr(boot_command_line, "dpt_jdi_nt36672")) {
            g_tp_dev_vendor = TP_DSJM;
            #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
            g_tp_chip_name = kzalloc(sizeof(HX83112A_NF_CHIP_NAME), GFP_KERNEL);
            g_tp_chip_name = HX83112A_NF_CHIP_NAME;
            #endif
            return true;
        }*/
    default:
        pr_err("Invalid project\n");
        break;
    }
    pr_err("Lcd module not found\n");
    return false;
}
int tp_util_get_vendor(struct hw_resource *hw_res, struct panel_info *panel_data)
{
    int id1 = -1, id2 = -1, id3 = -1;
    char* vendor;

    if (gpio_is_valid(hw_res->id1_gpio)) {
        id1 = gpio_get_value(hw_res->id1_gpio);
    }
    if (gpio_is_valid(hw_res->id2_gpio)) {
        id2 = gpio_get_value(hw_res->id2_gpio);
    }
    if (gpio_is_valid(hw_res->id3_gpio)) {
        id3 = gpio_get_value(hw_res->id3_gpio);
    }

    pr_err("[TP]%s: id1 = %d, id2 = %d, id3 = %d\n", __func__, id1, id2, id3);
    if ((id1 == 1) && (id2 == 1) && (id3 == 0)) {
        pr_err("[TP]%s::OFILM\n", __func__);
        panel_data->tp_type = TP_OFILM;
    } else if ((id1 == 0) && (id2 == 0) && (id3 == 0)) {
        pr_err("[TP]%s::TP_TPK\n", __func__);
        panel_data->tp_type = TP_TPK;
    } else if ((id1 == 0) && (id2 == 0) && (id3 == 0)) {
        pr_err("[TP]%s::TP_TRULY\n", __func__);
        panel_data->tp_type = TP_TRULY;
    } else {
        pr_err("[TP]%s::TP_UNKNOWN\n", __func__);
        panel_data->tp_type = TP_TRULY;
    }

    #ifdef CONFIG_TOUCHPANEL_MULTI_NOFLASH
    if (g_tp_chip_name != NULL) {
        panel_data->chip_name = g_tp_chip_name;
    }
    #endif

    if (is_project(OPPO_16051)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME);
        strcpy(panel_data->fw_name, S3508_FW_NAME);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_16103)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3320_BASELINE_TEST_LIMIT_NAME), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3320_BASELINE_TEST_LIMIT_NAME);
        strcpy(panel_data->fw_name, S3320_FW_NAME);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_16118)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME_16118), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME_16118);
        strcpy(panel_data->fw_name, S3508_FW_NAME_16118);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_17011)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME_17011), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME_17011);
        strcpy(panel_data->fw_name, S3508_FW_NAME_17011);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_17021)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3508_BASELINE_TEST_LIMIT_NAME_17021), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3508_BASELINE_TEST_LIMIT_NAME_17021);
        strcpy(panel_data->fw_name, S3508_FW_NAME_17021);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_17081) || is_project(OPPO_17085)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3706_BASELINE_TEST_LIMIT_NAME_17081), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3706_BASELINE_TEST_LIMIT_NAME_17081);
        strcpy(panel_data->fw_name, S3706_FW_NAME_17081);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_18005)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3706_BASELINE_TEST_LIMIT_NAME_18005), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3706_BASELINE_TEST_LIMIT_NAME_18005);
        strcpy(panel_data->fw_name, S3706_FW_NAME_18005);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    } else if (is_project(OPPO_18323)) {
        panel_data->test_limit_name = kzalloc(sizeof(S3706_BASELINE_TEST_LIMIT_NAME_18323), GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
            return -1;
        }
        strcpy(panel_data->test_limit_name, S3706_BASELINE_TEST_LIMIT_NAME_18323);
        strcpy(panel_data->fw_name, S3706_FW_NAME_18323);
        pr_err("[TP]%s: fw_name = %s \n",__func__, panel_data->fw_name);
    }else if (is_project(OPPO_18316)) {

        panel_data->test_limit_name = kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
        }

        panel_data->extra= kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
        if (panel_data->extra == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
        }

        panel_data->tp_type = g_tp_dev_vendor;
        if (panel_data->tp_type == TP_UNKNOWN) {
            pr_err("[TP]%s type is unknown\n", __func__);
            return 0;
        }
        vendor = GET_TP_DEV_NAME(panel_data->tp_type);
        strcpy(panel_data->manufacture_info.manufacture, vendor);
        snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
                "tp/%d/FW_%s_%s.img",
                get_project(), panel_data->chip_name, vendor);

        if (panel_data->test_limit_name) {
            snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
                "tp/%d/LIMIT_%s_%s.img",
                get_project(), panel_data->chip_name, vendor);
        }

        if (panel_data->extra) {
            snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
                "tp/%d/BOOT_FW_%s_%s.ihex",
                get_project(), panel_data->chip_name, vendor);
        }
        //panel_data->manufacture_info.fw_path = panel_data->fw_name;

        if(is_project(OPPO_18316)) {
            if (strstr(boot_command_line, "tianma_nt36672")) {
                memcpy(panel_data->manufacture_info.version, "0xBD1671", 8);
            }else if (strstr(boot_command_line, "dpt_jdi_nt36672")) {
                memcpy(panel_data->manufacture_info.version, "0xBD1672", 8);
            }else if (strstr(boot_command_line, "himax_hx83112")) {
                memcpy(panel_data->manufacture_info.version, "0xBD1673", 8);
            }
        }
        //memcpy(panel_data->manufacture_info.version, "0xBD1670", 8);


        if (strstr(boot_command_line, "dpt_jdi_nt36672")) {    //noflash
            panel_data->firmware_headfile.firmware_data = FW_18316_NT36672A_NF_DEPUTE;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_NT36672A_NF_DEPUTE);
        }else if (strstr(boot_command_line, "tianma_nt36672")) {
            panel_data->firmware_headfile.firmware_data = FW_18316_NT36672A_NF_TIANMA;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_NT36672A_NF_TIANMA);
        }else if (strstr(boot_command_line, "himax_hx83112")) {
            panel_data->firmware_headfile.firmware_data = FW_18316_HX83112A_NF_DSJM;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_HX83112A_NF_DSJM);
        }else {
            panel_data->firmware_headfile.firmware_data = NULL;
            panel_data->firmware_headfile.firmware_size = 0;
        }

        pr_info("Vendor:%s\n", vendor);
        pr_info("Fw:%s\n", panel_data->fw_name);
        pr_info("Limit:%s\n", panel_data->test_limit_name==NULL?"NO Limit":panel_data->test_limit_name);
        pr_info("Extra:%s\n", panel_data->extra==NULL?"NO Extra":panel_data->extra);
        pr_info("is matched %d, type %d\n", is_tp_type_got_in_match, panel_data->tp_type);
        return 0;
    }else if (is_project(OPPO_18321)) {

        panel_data->test_limit_name = kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
        }

        panel_data->extra= kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
        if (panel_data->extra == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
        }

        panel_data->tp_type = g_tp_dev_vendor;
        if (panel_data->tp_type == TP_UNKNOWN) {
            pr_err("[TP]%s type is unknown\n", __func__);
            return 0;
        }
        vendor = GET_TP_DEV_NAME(panel_data->tp_type);
        strcpy(panel_data->manufacture_info.manufacture, vendor);
        snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
                "tp/%d/FW_%s_%s.img",
                get_project(), panel_data->chip_name, vendor);

        if (panel_data->test_limit_name) {
            snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
                "tp/%d/LIMIT_%s_%s.img",
                get_project(), panel_data->chip_name, vendor);

        }

        if (panel_data->extra) {
            snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
                "tp/%d/BOOT_FW_%s_%s.ihex",
                get_project(), panel_data->chip_name, vendor);
        }
        //panel_data->manufacture_info.fw_path = panel_data->fw_name;
        if(is_project(OPPO_18321)) {
            if (strstr(boot_command_line, "tianma_nt36672")) {
                memcpy(panel_data->manufacture_info.version, "0xBD1671", 8);
            }else if (strstr(boot_command_line, "dpt_jdi_nt36672")) {
                memcpy(panel_data->manufacture_info.version, "0xBD1672", 8);
            }else if (strstr(boot_command_line, "himax_hx83112")) {
                memcpy(panel_data->manufacture_info.version, "0xBD1673", 8);
            }
        }
        //memcpy(panel_data->manufacture_info.version, "0xBD1670", 8);


        if (strstr(boot_command_line, "dpt_jdi_nt36672")) {    //noflash
            panel_data->firmware_headfile.firmware_data = FW_18316_NT36672A_NF_DEPUTE;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_NT36672A_NF_DEPUTE);
        }else if (strstr(boot_command_line, "tianma_nt36672")) {
            panel_data->firmware_headfile.firmware_data = FW_18316_NT36672A_NF_TIANMA;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_NT36672A_NF_TIANMA);
        }else if (strstr(boot_command_line, "himax_hx83112")) {
            panel_data->firmware_headfile.firmware_data = FW_18316_HX83112A_NF_DSJM;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_HX83112A_NF_DSJM);
        }else {
            panel_data->firmware_headfile.firmware_data = NULL;
            panel_data->firmware_headfile.firmware_size = 0;
        }

        pr_info("Vendor:%s\n", vendor);
        pr_info("Fw:%s\n", panel_data->fw_name);
        pr_info("Limit:%s\n", panel_data->test_limit_name==NULL?"NO Limit":panel_data->test_limit_name);
        pr_info("Extra:%s\n", panel_data->extra==NULL?"NO Extra":panel_data->extra);
        pr_info("is matched %d, type %d\n", is_tp_type_got_in_match, panel_data->tp_type);
        return 0;
    }/*else if (is_project(OPPO_18325)) {

        panel_data->test_limit_name = kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
        if (panel_data->test_limit_name == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
        }

        panel_data->extra= kzalloc(MAX_LIMIT_DATA_LENGTH, GFP_KERNEL);
        if (panel_data->extra == NULL) {
            pr_err("[TP]panel_data.test_limit_name kzalloc error\n");
        }

        panel_data->tp_type = g_tp_dev_vendor;
        if (panel_data->tp_type == TP_UNKNOWN) {
            pr_err("[TP]%s type is unknown\n", __func__);
            return 0;
        }
        vendor = GET_TP_DEV_NAME(panel_data->tp_type);
        strcpy(panel_data->manufacture_info.manufacture, vendor);
        snprintf(panel_data->fw_name, MAX_FW_NAME_LENGTH,
                "tp/%d/FW_%s_%s.img",
                get_project(), panel_data->chip_name, vendor);

        if (panel_data->test_limit_name) {
            snprintf(panel_data->test_limit_name, MAX_LIMIT_DATA_LENGTH,
                "tp/%d/LIMIT_%s_%s.img",
                get_project(), panel_data->chip_name, vendor);
        }

        if (panel_data->extra) {
            snprintf(panel_data->extra, MAX_LIMIT_DATA_LENGTH,
                "tp/%d/BOOT_FW_%s_%s.ihex",
                get_project(), panel_data->chip_name, vendor);
        }
        //panel_data->manufacture_info.fw_path = panel_data->fw_name;
        memcpy(panel_data->manufacture_info.version, "0xBD1670", 8);


        if (strstr(boot_command_line, "dpt_jdi_nt36672")) {    //noflash
            panel_data->firmware_headfile.firmware_data = FW_18316_NT36672A_NF_DEPUTE;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_NT36672A_NF_DEPUTE);
        }else if (strstr(boot_command_line, "tianma_nt36672")) {
            panel_data->firmware_headfile.firmware_data = FW_18316_NT36672A_NF_TIANMA;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_NT36672A_NF_TIANMA);
        }else if (strstr(boot_command_line, "himax_hx83112")) {
            panel_data->firmware_headfile.firmware_data = FW_18316_HX83112A_NF_DSJM;
            panel_data->firmware_headfile.firmware_size = sizeof(FW_18316_HX83112A_NF_DSJM);
        }else {
            panel_data->firmware_headfile.firmware_data = NULL;
            panel_data->firmware_headfile.firmware_size = 0;
        }

        pr_info("Vendor:%s\n", vendor);
        pr_info("Fw:%s\n", panel_data->fw_name);
        pr_info("Limit:%s\n", panel_data->test_limit_name==NULL?"NO Limit":panel_data->test_limit_name);
        pr_info("Extra:%s\n", panel_data->extra==NULL?"NO Extra":panel_data->extra);
        pr_info("is matched %d, type %d\n", is_tp_type_got_in_match, panel_data->tp_type);
        return 0;
    }*/
    strcpy(panel_data->manufacture_info.manufacture, "SAMSUNG");
    //panel_data->manufacture_info.fw_path = panel_data->fw_name;

    return 0;
}

