/************************************************************************************
** File:  \\192.168.144.3\Linux_Share\12015\ics2\development\mechipatek\custom\oppo77_12015\kernel\battery\battery
** VENDOR_EDIT
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
**
** Description:
**          for dc-dc sn111008 charg
**
** Version: 1.0
** Date created: 21:03:46, 05/04/2012
** Author: Fanhong.Kong@ProDrv.CHG
**
** --------------------------- Revision History: ------------------------------------------------------------
* <version>           <date>                <author>                            <desc>
* Revision 1.0     2015-06-22        Fanhong.Kong@ProDrv.CHG         Created for new architecture
************************************************************************************************************/

#ifdef CONFIG_OPPO_CHARGER_MTK
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/xlog.h>
#ifdef CONFIG_OPPO_CHARGER_6750T
#include <mt-plat/mt_gpio.h>
#elif defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
#include <mt-plat/mtk_gpio.h>
#include <linux/gpio.h>
#else
#include <mach/mt_gpio.h>
#endif
#else
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#endif

#include "oppo_adapter.h"
#include "../oppo_charger.h"

static struct oppo_adapter_chip *the_chip = NULL;

static void vooc_uart_gpio_set_value(unsigned long pin, bool value)
{
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
        vooc_uart_mt_set_gpio_out(pin, value);
#else
        gpio_set_value(pin, value);
#endif
#else
	gpio_set_value(pin, value);
#endif
}

static int vooc_uart_gpio_get_value(unsigned long pin)
{
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
	return mt_get_gpio_in(pin);
#else
	return gpio_get_value(pin);
#endif
#else
        return gpio_get_value(pin);
#endif
}

static void vooc_uart_tx_bit(struct oppo_adapter_chip *chip, unsigned char tx_data)
{
        static unsigned char tx_bit = BIT_START;

        switch (tx_bit) {
        case BIT_START:
                chip->tx_byte_over = false;
                vooc_uart_gpio_set_value(chip->uart_tx_gpio, 0);
                tx_bit = BIT_0;
                break;
        case BIT_0:
        case BIT_1:
        case BIT_2:
        case BIT_3:
        case BIT_4:
        case BIT_5:
        case BIT_6:
        case BIT_7:
                if (tx_data & (1 << tx_bit)) {
                        vooc_uart_gpio_set_value(chip->uart_tx_gpio, 1);
                } else {
                        vooc_uart_gpio_set_value(chip->uart_tx_gpio, 0);
                }
                tx_bit++;
                break;
        case BIT_STOP:
        case BIT_IDLE:
                vooc_uart_gpio_set_value(chip->uart_tx_gpio, 1);
                tx_bit = BIT_START;
                chip->tx_byte_over = true;
                break;
        default:
                break;
        }
}

static int vooc_uart_rx_bit(struct oppo_adapter_chip *chip)
{
        static unsigned char rx_bit = BIT_IDLE, rx_val = 0;

        switch (rx_bit) {
        case BIT_IDLE:
                chip->rx_byte_over = false;
                if (!vooc_uart_gpio_get_value(chip->uart_rx_gpio)) {
                        rx_bit = BIT_0;
                        chip->timer_delay = 75;        /*1.5 cycle*/
                } else {
                        chip->timer_delay = 2;        /*0.02 cycle*/
                }
                break;
        case BIT_0:
        case BIT_1:
        case BIT_2:
        case BIT_3:
        case BIT_4:
        case BIT_5:
        case BIT_6:
        case BIT_7:
                chip->timer_delay = 50;        /* 1 cycle*/
                if (vooc_uart_gpio_get_value(chip->uart_rx_gpio)) {
                        rx_val |= (1 << rx_bit);
                } else {
                        rx_val &= ~(1 << rx_bit);
                }
                rx_bit++;
                break;
        case BIT_STOP:
                rx_bit = BIT_IDLE;
                chip->rx_byte_over = true;
                break;
        default:
                break;
        }
        return rx_val;
}

static void vooc_uart_tx_byte(struct oppo_adapter_chip *chip, unsigned char tx_data)
{
        chip->timer_delay = 51;
        while (1) {
                vooc_uart_tx_bit(chip, tx_data);
                udelay(chip->timer_delay);
                if (chip->tx_byte_over) {
                        chip->timer_delay = 25;
                        break;
                }
        }
}

static unsigned char vooc_uart_rx_byte(struct oppo_adapter_chip *chip, unsigned int cmd)
{
        unsigned char rx_val = 0;
        unsigned int count = 0;
        unsigned int max_count = 0;

        if (cmd == Read_Addr_Line_Cmd) {
                max_count = Read_Addr_Line_Cmd_Count;
        } else if (cmd == Write_Addr_Line_Cmd) {
                max_count = Write_Addr_Line_Cmd_Count;
        } else if (cmd == Erase_Addr_Line_Cmd) {
                max_count = Erase_Addr_Line_Cmd_Count;
        } else if (cmd == Read_All_Cmd) {
                max_count = Read_All_Cmd_Count;
        } else if (cmd == Erase_All_Cmd) {
                max_count = Erase_All_Cmd_Count;
        } else if (cmd == Boot_Over_Cmd) {
                max_count = Boot_Over_Cmd_Count;
        } else {
                max_count = Other_Cmd_count;
        }
        chip->rx_timeout = false;
        chip->timer_delay = 25;
        while (1) {
                rx_val = vooc_uart_rx_bit(chip);
                udelay(chip->timer_delay);
                if (chip->rx_byte_over) {
                        return rx_val;
                }
                if (count > max_count) {
                        chip->rx_timeout = true;
                        return 0x00;
                }
                count++;
        }
}

static void vooc_uart_irq_fiq_enable(bool enable)
{
        if (enable) {
                preempt_enable();
                local_fiq_enable();
                local_irq_enable();
        } else {
                local_irq_disable();
                local_fiq_disable();
                preempt_disable();
        }
}

static int vooc_uart_write_some_addr(struct oppo_adapter_chip *chip, u8 *fw_buf, int length)
{
        unsigned int write_addr = 0, i = 0, fw_count = 0;
        unsigned char rx_val = 0;

        while (1) {
                /*cmd(2 bytes) + count(1 byte) + addr(2 bytes) + data(16 bytes)*/
                /*tx: 0xF5*/
                vooc_uart_irq_fiq_enable(false);
                vooc_uart_tx_byte(chip, (Write_Addr_Line_Cmd >> 8) & 0xff);
                vooc_uart_irq_fiq_enable(true);

                /*tx: 0x02*/
                vooc_uart_irq_fiq_enable(false);
                vooc_uart_tx_byte(chip, Write_Addr_Line_Cmd & 0xff);
                vooc_uart_irq_fiq_enable(true);

                /*count:16 bytes*/
                vooc_uart_irq_fiq_enable(false);
                vooc_uart_tx_byte(chip, 16);
                vooc_uart_irq_fiq_enable(true);

                /*addr: 2 byte*/
                if (write_addr == 0) {
                        write_addr = (fw_buf[fw_count + 1] << 8) | fw_buf[fw_count];
                }
                vooc_uart_irq_fiq_enable(false);
                vooc_uart_tx_byte(chip, (write_addr >> 8) & 0xff);
                vooc_uart_irq_fiq_enable(true);

                vooc_uart_irq_fiq_enable(false);
                vooc_uart_tx_byte(chip, write_addr & 0xff);
                vooc_uart_irq_fiq_enable(true);

                if (!(write_addr % 0x20)) {
                        fw_count += 2;
                }
                /*data: 16 bytes*/
                for (i = 0;i < 16;i++) {
                        vooc_uart_irq_fiq_enable(false);
                        vooc_uart_tx_byte(chip, fw_buf[fw_count]);
                        fw_count++;
                        if (i == 15) {
                                rx_val = vooc_uart_rx_byte(chip, Write_Addr_Line_Cmd);
                        }
                        vooc_uart_irq_fiq_enable(true);
                }
                write_addr += 16;
                if (rx_val != UART_ACK || chip->rx_timeout) {
                        chg_err(" err, write_addr:0x%x, chip->rx_timeout:%d\n", write_addr, chip->rx_timeout);
                        return -1;
                }
                if (fw_count >= length) {
                        return 0;
                }
        }
}

#define STM8S_ADAPTER_FIRST_ADDR                0x8C00
#define STM8S_ADAPTER_LAST_ADDR                 0x9FEF
#define HALF_ONE_LINE                           16

static bool vooc_uart_read_addr_line_and_check(struct oppo_adapter_chip *chip, unsigned int addr)
{
        unsigned char fw_check_buf[20] = {0x00};
        int i = 0;
        static int fw_line = 0;
        bool check_result = false;
        int addr_check_err = 0;

        if (addr == STM8S_ADAPTER_FIRST_ADDR) {
                fw_line = 0;
        }
        /*Tx_Read_Addr_Line */
        /*tx:0xF5*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (Read_Addr_Line_Cmd >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x01*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, Read_Addr_Line_Cmd & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x9F*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (addr >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0xF0*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, addr & 0xff);

        /*addr(2 bytes) + data(16 bytes)*/
        fw_check_buf[0] = vooc_uart_rx_byte(chip, Read_Addr_Line_Cmd);
        if (chip->rx_timeout) {
                goto  read_addr_line_err;
        }
        fw_check_buf[1] = vooc_uart_rx_byte(chip, Read_Addr_Line_Cmd);
        if (chip->rx_timeout) {
                goto  read_addr_line_err;
        }
        if (addr != ((fw_check_buf[0] << 8) | fw_check_buf[1])) {
                addr_check_err = 1;
                goto read_addr_line_err;
        }
        for (i = 0; i < 16;i++) {
                fw_check_buf[i + 2] = vooc_uart_rx_byte(chip, Read_Addr_Line_Cmd);
                if (chip->rx_timeout) {
                        goto  read_addr_line_err;
                }
        }
        if (!(addr % 0x20)) {
                if (addr == ((adapter_stm8s_firmware_data[fw_line * 34 + 1] << 8) | (adapter_stm8s_firmware_data[fw_line * 34]))) {
                        for (i = 0;i < 16;i++) {
                                if (fw_check_buf[i + 2] != adapter_stm8s_firmware_data[fw_line * 34 + 2 + i]) {
                                        goto read_addr_line_err;
                                }
                        }
                }
        } else {
                if ((addr - 16) ==
                        ((adapter_stm8s_firmware_data[fw_line * 34 + 1] << 8) | (adapter_stm8s_firmware_data[fw_line * 34]))) {
                        for (i = 0;i < 16;i++) {
                                if (fw_check_buf[i + 2] != adapter_stm8s_firmware_data[fw_line * 34 + 2 + HALF_ONE_LINE + i]) {
                                        goto read_addr_line_err;
                                }
                        }
                }
                fw_line++;
        }
        check_result = true;
read_addr_line_err:
        vooc_uart_irq_fiq_enable(true);
        if (addr_check_err) {
                chg_debug(" addr:0x%x, buf[0]:0x%x, buf[1]:0x%x\n", addr, fw_check_buf[0], fw_check_buf[1]);
        }
        if (!check_result) {
                chg_err(" fw_check err, addr:0x%x, check_buf[%d]:0x%x != fw_data[%d]:0x%x\n",
                         addr, i + 2, fw_check_buf[i + 2], (fw_line * 34 + 2 + i),
                        adapter_stm8s_firmware_data[fw_line * 34 + 2 + i]);
        }
        return check_result;
}

static int vooc_uart_read_front_addr_and_check(struct oppo_adapter_chip *chip)
{
        unsigned int read_addr = STM8S_ADAPTER_FIRST_ADDR;
        bool result = false;

        while (read_addr < STM8S_ADAPTER_LAST_ADDR) {
                result = vooc_uart_read_addr_line_and_check(chip, read_addr);
                read_addr = read_addr + 16;
                if ((!result) || chip->rx_timeout) {
                        chg_err(" result:%d, chip->rx_timeout:%d\n", result, chip->rx_timeout);
                        return -1;
                }
        }
        return 0;
}

static bool vooc_adapter_update_handle(struct oppo_adapter_chip *chip,
        unsigned long tx_pin, unsigned long rx_pin)
{
        unsigned char rx_val = 0;
        int rx_last_line_count = 0;
        unsigned char rx_last_line[18] = {0x0};
        int rc = 0;

        chg_debug(" begin\n");
        chip->uart_tx_gpio = tx_pin;
        chip->uart_rx_gpio = rx_pin;
        chip->adapter_update_ing = true;
        chip->rx_timeout = false;
/*step1: Tx_Erase_Addr_Line*/
        /*tx:0xF5*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (Erase_Addr_Line_Cmd >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x03*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, Erase_Addr_Line_Cmd & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x9F*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (Last_Line_Addr >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0xF0*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, Last_Line_Addr & 0xff);
        rx_val = vooc_uart_rx_byte(chip, Erase_Addr_Line_Cmd);
        vooc_uart_irq_fiq_enable(true);
        if (rx_val != UART_ACK || chip->rx_timeout) {
                chg_err(" Tx_Erase_Addr_Line err, chip->rx_timeout:%d\n", chip->rx_timeout);
                goto update_err;
        }

/*Step2: Tx_Read_Addr_Line */
        /*tx:0xF5*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (Read_Addr_Line_Cmd >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x01*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, Read_Addr_Line_Cmd & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x9F*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (Last_Line_Addr >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0xF0*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, Last_Line_Addr & 0xff);
        for (rx_last_line_count = 0; rx_last_line_count < 18;rx_last_line_count++) {
                rx_last_line[rx_last_line_count] = vooc_uart_rx_byte(chip, Read_Addr_Line_Cmd);
                if (chip->rx_timeout) {
                        break;
                }
        }
        vooc_uart_irq_fiq_enable(true);
        if ((rx_last_line[FW_EXIST_LOW] == 0x55 && rx_last_line[FW_EXIST_HIGH] == 0x34) || chip->rx_timeout) {
                chg_err(" Tx_Read_Addr_Line err, chip->rx_timeout:%d\n",  chip->rx_timeout);
                goto update_err;
        }

/*Step3: Tx_Erase_All */
        /*tx:0xF5*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (Erase_All_Cmd >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x05*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, Erase_All_Cmd & 0xff);
        rx_val = vooc_uart_rx_byte(chip, Erase_All_Cmd);
        vooc_uart_irq_fiq_enable(true);
        if (rx_val != UART_ACK || chip->rx_timeout) {
                chg_err(" Tx_Erase_All err, chip->rx_timeout:%d\n", chip->rx_timeout);
                goto update_err;
        }

/* Step4: Tx_Write_Addr_Line */
        rc = vooc_uart_write_some_addr(chip, &adapter_stm8s_firmware_data[0],
                (sizeof(adapter_stm8s_firmware_data) - 34));
        if (rc) {
                chg_err(" Tx_Write_Addr_Line err\n");
                goto update_err;
        }

/* Step5: Tx_Read_All */
        rc = vooc_uart_read_front_addr_and_check(chip);
        if (rc) {
                chg_err(" Tx_Read_All err\n");
                goto update_err;
        }

/* Step6: write the last line */
        rc = vooc_uart_write_some_addr(chip,
                &adapter_stm8s_firmware_data[sizeof(adapter_stm8s_firmware_data) - 34], 34);
        if (rc) {
                chg_err(" write the last line err\n");
                goto update_err;
        }

/* Step7: Tx_Boot_Over */
        /*tx:0xF5*/
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, (Boot_Over_Cmd >> 8) & 0xff);
        vooc_uart_irq_fiq_enable(true);

        /*tx:0x06 */
        vooc_uart_irq_fiq_enable(false);
        vooc_uart_tx_byte(chip, Boot_Over_Cmd & 0xff);
        rx_val = vooc_uart_rx_byte(chip, Boot_Over_Cmd);
        vooc_uart_irq_fiq_enable(true);
        if (rx_val != UART_ACK || chip->rx_timeout) {
                chg_err("  Tx_Boot_Over err, chip->rx_timeout:%d\n" , chip->rx_timeout);
                goto update_err;
        }
        chip->rx_timeout = false;
        chip->adapter_update_ing = false;
        chg_debug(" success\n");
        return true;

update_err:
        chip->rx_timeout = false;
        chip->adapter_update_ing = false;
        chg_err(" err\n");
        return false;
}

#ifndef CONFIG_OPPO_CHARGER_MTK
bool oppo_vooc_adapter_update_is_tx_gpio(unsigned long gpio_num)
{
        if (!the_chip) {
                return false;
        }
        if (the_chip->adapter_update_ing && gpio_num == the_chip->uart_tx_gpio) {
                return true;
        } else {
                return false;
        }
}

bool oppo_vooc_adapter_update_is_rx_gpio(unsigned long gpio_num)
{
        if (!the_chip) {
                return false;
        }
        if (the_chip->adapter_update_ing && gpio_num == the_chip->uart_rx_gpio) {
                return true;
        } else {
                return false;
        }
}
#endif

struct oppo_adapter_operations oppo_adapter_ops = {
        .adapter_update = vooc_adapter_update_handle,
};

static int __init vooc_adapter_init(void)
{
        struct oppo_adapter_chip *chip = NULL;

        chip = kzalloc(sizeof(struct oppo_adapter_chip), GFP_KERNEL);
        if (!chip) {
                chg_err(" vooc_adapter alloc fail\n");
                return -1;
        }
        chip->timer_delay = 0;
        chip->tx_byte_over = false;
        chip->rx_byte_over = false;
        chip->rx_timeout = false;
        chip->uart_tx_gpio = 0;
        chip->uart_rx_gpio = 0;
        chip->adapter_update_ing = false;
        chip->adapter_firmware_data = adapter_stm8s_firmware_data;
        chip->adapter_fw_data_count = sizeof(adapter_stm8s_firmware_data);
        chip->vops = &oppo_adapter_ops;
        oppo_adapter_init(chip);
        the_chip = chip;
        chg_debug(" success\n");
        return 0;
}

static void __init vooc_adapter_exit(void)
{
        return;
}

module_init(vooc_adapter_init);
module_exit(vooc_adapter_exit);

