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
#include <linux/platform_device.h>
#include <asm/atomic.h>

#include <linux/xlog.h>

#ifdef CONFIG_OPPO_CHARGER_6750T
#include <upmu_common.h>
#include <mt-plat/mt_gpio.h>
#elif defined(CONFIG_OPPO_CHARGER_MTK6763) || defined(CONFIG_OPPO_CHARGER_MTK6771)
#include <upmu_common.h>
#include <mt-plat/mtk_gpio.h>
#include "oppo_bq24196.h"
#else /* CONFIG_OPPO_CHARGER_6750T */
#include <mach/mt_typedefs.h>
#include <mach/mt_gpio.h>
#include <mach/mt_pm_ldo.h>
#endif /* CONFIG_OPPO_CHARGER_6750T */
#include <linux/of.h>
#include <linux/of_gpio.h>
#else /* CONFIG_OPPO_CHARGER_MTK */
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
#endif /* CONFIG_OPPO_CHARGER_MTK */
#include "oppo_bq2202a.h"
#include "../oppo_charger.h"

bool oppo_high_battery_status = 1;
int  oppo_check_ID_status = 0;
int  oppo_high_battery_check_counts = 0;
bool oppo_battery_status_init_flag = 0;

#define DEBUG_BQ2202A           
#define READ_PAGE_BQ2202A


#define READ_ID_CMD					0x33            // read ROM
#define SKIP_ROM_CMD               	0xCC           // skip ROM
#define WRITE_EPROM_CMD        		0x0F           // write EPROM 
#define READ_PAGE_ID_CMD        	0xF0          // read EPROM  PAGE
#define READ_FIELD_ID_CMD       	0xC3          // read EPROM  FIELD

#ifdef READ_PAGE_BQ2202A
#define AddressLow                   		0x20        // EPROM start address LOW 
#define AddressHigh                  		0x00        // EPROM start address  HIGH
#define BQ2022_MANUFACTURE_ADDR_LOW			0x40
#define BQ2022_MANUFACTURE_ADDR_HIGH		0x00
#else
#define AddressLow                   	0x00        // EPROM start address LOW 
#define AddressHigh                  	0x00        // EPROM start address  HIGH
#define BQ2022_MANUFACTURE_ADDR_LOW		0x00
#define BQ2022_MANUFACTURE_ADDR_HIGH		0x00
#endif

static  unsigned char ReadIDDataByte[8];     //8*8=64bit            ID ROM 
#ifdef READ_PAGE_BQ2202A
static  unsigned char CheckIDDataByte[32];    // 32*8=256bit   EPROM  PAGE1
#else
static  unsigned char CheckIDDataByte[128];    // 128*8=1024bit   EPROM  PAGE1
#endif



static DEFINE_MUTEX(bq2202a_access);
/**********************************************************************/
/* 		void wait_us(int usec)										  */
/*																      */
/*	Description :   Creates a delay of approximately (usec + 5us) 	  */
/*				  		when usec is greater.						  */
/* 	Arguments : 		None										  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/

#define wait_us(n) udelay(n)
#define wait_ms(n) mdelay(n)

//#define BQ2202A_GPIO 				GPIO56
static int bq2202a_gpio = 0;
#define GPIO_DIR_OUT_1				1
#define GPIO_DIR_OUT_0				0
#define GPIO_DIR_IN_0				0

void oppo_set_gpio_out(int val)
{
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
    mt_set_gpio_dir(bq2202a_gpio, GPIO_DIR_OUT_1);
    mt_set_gpio_out(bq2202a_gpio, val);
#endif /* CONFIG_OPPO_CHARGER_MTK6763 */
#else
	gpio_direction_output(bq2202a_gpio, val);
#endif
}

void oppo_set_gpio_in(void)
{
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
    mt_set_gpio_dir(bq2202a_gpio, GPIO_DIR_IN_0);
#endif /* CONFIG_OPPO_CHARGER_MTK6763 */
#else
	 gpio_direction_input(bq2202a_gpio);
#endif
}

unsigned char oppo_get_gpio_in(void)
{
#ifdef CONFIG_OPPO_CHARGER_MTK
#if !defined CONFIG_OPPO_CHARGER_MTK6763 && !defined CONFIG_OPPO_CHARGER_MTK6771
     return mt_get_gpio_in(bq2202a_gpio);
#else /* CONFIG_OPPO_CHARGER_MTK */
    return 1;
#endif /* CONFIG_OPPO_CHARGER_MTK6763 */
    #else /* CONFIG_OPPO_CHARGER_MTK */
	 return gpio_get_value(bq2202a_gpio);
#endif /* CONFIG_OPPO_CHARGER_MTK */
	
}


/**********************************************************************/
/* 	static void SendReset(void)										  */
/*																      */
/*	Description : 		Creates the Reset signal to initiate SDQ 	  */
/*				  		communication.								  */
/* 	Arguments : 		None										  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/
static void SendReset(void)
{
   	oppo_set_gpio_out(GPIO_DIR_OUT_1);
    wait_us(20);								//Allow PWR cap to charge and power IC	~ 25us
   	oppo_set_gpio_out(GPIO_DIR_OUT_0);            	//Set Low
    //wait_us(500);								//Reset time greater then 480us
    wait_us(650);
    oppo_set_gpio_in();							//Set GPIO P9.3 as Input
    
}

/**********************************************************************/
/* 	static unsigned char TestPresence(void)							  */
/*																      */
/*	Description : 		Detects if a device responds to Reset signal  */
/* 	Arguments : 		PresenceTimer - Sets timeout if no device	  */
/*							present									  */
/*						InputData - Actual state of GPIO			  */
/*						GotPulse - States if a pulse was detected	  */
/*	Global Variables:	None   										  */
/*  Returns: 			GotPulse         							  */
/**********************************************************************/
static unsigned char TestPresence(void)
{
    unsigned int PresenceTimer = 0;
    static volatile unsigned char InputData = 0;
    static volatile unsigned char GotPulse = 0;

    oppo_set_gpio_in();	        										//Set GPIO P9.3 as Input
    PresenceTimer = 300;                                                //Set timeout, enough time to allow presence pulse
    GotPulse = 0;                                                           //Initialize as no pulse detected
	wait_us(60);	
    while ((PresenceTimer > 0) && (GotPulse == 0)) 
    {
        InputData = oppo_get_gpio_in();       							//Monitor logic state of GPIO
		/*int j = 0;
		while(j < 10) 
		{
			chg_err("mt_get_gpio_in---------------InputData = %d\r\n", InputData);
			j++;
			wait_us(100);
			
		}*/
        if (InputData == 0)                                             //If GPIO is Low,
        {                           
            GotPulse = 1;                                               //it means that device responded
        } else {                                                              //If GPIO is high
            GotPulse = 0;			                            //it means that device has not responded
            --PresenceTimer;		                            //Decrease timeout until enough time has been allowed for response
        }
    }
    wait_us(200);					                    //Wait some time to continue SDQ communication
#ifdef DEBUG_BQ2202A
    //chg_err("PresenceTimer=%d\n",PresenceTimer);
#endif
    return GotPulse;				                    //Return if device detected or not
}

/**********************************************************************/
/* 	static void WriteOneBit(unsigned char OneZero)					  */
/*																      */
/*	Description : 		This procedure outputs a bit in SDQ to the 	  */
/*				  		slave.								  		  */
/* 	Arguments : 		OneZero - value of bit to be written		  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/
static void WriteOneBit(unsigned char OneZero)
{
	//wait_us(300);								//Set GPIO P9.3 as Output
	oppo_set_gpio_out(GPIO_DIR_OUT_1);			//Set High
    oppo_set_gpio_out(GPIO_DIR_OUT_0);	        //Set Low

    if (OneZero != 0x00) {
        wait_us(7);									//approximately 7us	for a Bit 1
        oppo_set_gpio_out(GPIO_DIR_OUT_1);	        //Set High
        wait_us(65);								//approximately 65us
    } else {
        wait_us(65);								//approximately 65us for a Bit 0
        oppo_set_gpio_out(GPIO_DIR_OUT_1);	        //Set High
        wait_us(7);					   				//approximately 7us
    }
    wait_us(5);	  									//approximately 5us
}


/**********************************************************************/
/* 	static unsigned char ReadOneBit(void)							  */
/*																      */
/*	Description : 		This procedure receives the bit value returned*/
/*				  		by the SDQ slave.							  */
/* 	Arguments : 		InBit - Bit value returned by slave			  */
/*	Global Variables:	None   										  */
/*  Returns: 			InBit								          */
/**********************************************************************/
static unsigned char ReadOneBit(void)
{
    static unsigned char InBit;
	
    													//Set GPIO P9.3 as Output
    oppo_set_gpio_out(GPIO_DIR_OUT_1);		            //Set High
    oppo_set_gpio_out(GPIO_DIR_OUT_0);	                //Set Low
    oppo_set_gpio_in();									//Set GPIO P9.3 as Input
    wait_us(15);		   								//Strobe window	~ 12us
    InBit = oppo_get_gpio_in();		        //This function takes about 3us
													//Between the wait_us and GPIO_ReadBit functions
													//approximately 15us should occur to monitor the 
													//GPIO line and determine if bit read is one or zero
    wait_us(65);									//End of Bit
    oppo_set_gpio_out(GPIO_DIR_OUT_1);				//Set GPIO P9.3 as Output
		            								//Set High
    return InBit;									//Return bit value
}


/**********************************************************************/
/* 	static void WriteOneByte(unsigned char Data2Send)				  */
/*																      */
/*	Description : 		This procedure calls the WriteOneBit() 		  */
/*				  		function 8 times to send a byte in SDQ.		  */
/* 	Arguments : 		Data2Send - Value of byte to be sent in SDQ	  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/
static void WriteOneByte(unsigned char Data2Send)
{
    unsigned char i = 0;
    unsigned char MaskByte = 0;
    unsigned char Bit2Send = 0;

    MaskByte = 0x01;

    for (i = 0; i < 8; i++) {
        Bit2Send = Data2Send & MaskByte;		//Selects the bit to be sent
        WriteOneBit(Bit2Send);					//Writes the selected bit
        MaskByte <<= 1;							//Moves the bit mask to the next most significant position
    }
}

/**********************************************************************/
/* 	static unsigned char ReadOneByte(void)							  */
/*																      */
/*	Description : 		This procedure reads 8 bits on the SDQ line   */
/*				  		and returns the byte value.					  */
/* 	Arguments : 		Databyte - Byte value returned by SDQ slave	  */
/*						MaskByte - Used to seperate each bit	      */
/*						i - Used for 8 time loop					  */
/*	Global Variables:	None   										  */
/*  Returns: 			DataByte							          */
/**********************************************************************/
static unsigned char ReadOneByte(void)
{
    unsigned char i = 0;
    unsigned char DataByte = 0;
    unsigned char MaskByte = 0;

    DataByte = 0x00;			 								//Initialize return value

    for (i = 0; i < 8; i++) {                                     //Select one bit at a time
        MaskByte = ReadOneBit();				    		//Read One Bit
        MaskByte <<= i;										//Determine Bit position within the byte
        DataByte = DataByte | MaskByte;					//Keep adding bits to form the byte
    }
    return DataByte;											//Return byte value read
}

/**********************************************************************/
/* 	void ReadBq2202aID(void)               							  */
/*																      */
/*	Description : 		This procedure reads BQ2202A'S ID on the SDQ  */
/*				  		line.                   					  */
/* 	Arguments : 		None                    					  */
/*	Global Variables:	None   										  */
/*  Returns: 			None       							          */
/**********************************************************************/
void ReadBq2202aID(void)
{
    unsigned char i = 0;
    mutex_lock(&bq2202a_access);
     
    SendReset();
    wait_us(2);
    i = TestPresence();
#ifdef DEBUG_BQ2202A
    //chg_err("TestPresence=%d\n",i);
#endif
    WriteOneByte(READ_ID_CMD);                     		 // read rom commond
    for(i = 0;i < 8;i++) {
        //ReadIDDataByte[i] = ReadOneByte();     				 // read rom Partition 64bits = 8Bits
        ReadIDDataByte[7-i] = ReadOneByte();      // read rom Partition 64bits = 8Bits
    }

    mutex_unlock(&bq2202a_access);
	chg_debug( "ReadBq2202aID[0-7]:%03d,%03d,%03d,%03d,%03d,%03d,%03d,%03d\n",ReadIDDataByte[0],ReadIDDataByte[1],ReadIDDataByte[2],ReadIDDataByte[3],ReadIDDataByte[4],ReadIDDataByte[5],ReadIDDataByte[6],ReadIDDataByte[7]);
}
/**********************************************************************/
/* 	void CheckBq2202aID(void)               							  */
/*																      */
/*	Description : 		This procedure reads BQ2202A'S ID on the SDQ  */
/*				  		line.                   					  */
/* 	Arguments : 		None                    					  */
/*	Global Variables:	None   										  */
/*  Returns: 			None       							          */
/**********************************************************************/
void CheckBq2202aID(void)
{
    unsigned char i;
    mutex_lock(&bq2202a_access);
       
    SendReset();
    wait_us(2);
    i=TestPresence();
#ifdef DEBUG_BQ2202A
    //chg_err("TestPresence=%d\n",i);
#endif

    WriteOneByte(SKIP_ROM_CMD);              // skip rom commond
    wait_us(60);

#ifdef READ_PAGE_BQ2202A
    WriteOneByte(READ_PAGE_ID_CMD);     // read eprom Partition for page mode
#else
    WriteOneByte(READ_FIELD_ID_CMD);     // read eprom Partition for field mode
#endif
    wait_us(60);
    WriteOneByte(AddressLow);               // read eprom Partition Starting address low
    wait_us(60);
    WriteOneByte(AddressHigh);               // read eprom Partition Starting address high

	#ifdef READ_PAGE_BQ2202A
    for (i = 0;i < 32;i++) {
        CheckIDDataByte[i] = ReadOneByte();   // read eprom Partition page1  256bits = 32Bits
    }

    mutex_unlock(&bq2202a_access);
	chg_debug( "CheckBq2202aID[16-23]:%03d,%03d,%03d,%03d,%03d,%03d,%03d,%03d\n",CheckIDDataByte[16],CheckIDDataByte[17],CheckIDDataByte[18],CheckIDDataByte[19],CheckIDDataByte[20],CheckIDDataByte[21],CheckIDDataByte[22],CheckIDDataByte[23]);
	//chg_err("CheckBq2202aID[24]=%03d,CheckBq2202aID[25]=%03d,CheckBq2202aID[26]=%03d,CheckBq2202aID[27]=%03d,CheckBq2202aID[28]=%03d,CheckBq2202aID[29]=%03d,CheckBq2202aID[30]=%03d,CheckBq2202aID[31]=%03d\n",CheckIDDataByte[24],CheckIDDataByte[25],CheckIDDataByte[26],CheckIDDataByte[27],CheckIDDataByte[28],CheckIDDataByte[29],CheckIDDataByte[30],CheckIDDataByte[31]);
	//chg_err("CheckBq2202aID[0] =%03d,CheckBq2202aID[1] =%03d,CheckBq2202aID[2] =%03d,CheckBq2202aID[3] =%03d,CheckBq2202aID[4] =%03d,CheckBq2202aID[5] =%03d,CheckBq2202aID[6] =%03d,CheckBq2202aID[7] =%03d\n",CheckIDDataByte[0],CheckIDDataByte[1],CheckIDDataByte[2],CheckIDDataByte[3],CheckIDDataByte[4],CheckIDDataByte[5],CheckIDDataByte[6],CheckIDDataByte[7]);
	//chg_err("CheckBq2202aID[8] =%03d,CheckBq2202aID[9] =%03d,CheckBq2202aID[10]=%03d,CheckBq2202aID[11]=%03d,CheckBq2202aID[12]=%03d,CheckBq2202aID[13]=%03d,CheckBq2202aID[14]=%03d,CheckBq2202aID[15]=%03d\n",CheckIDDataByte[8],CheckIDDataByte[9],CheckIDDataByte[10],CheckIDDataByte[11],CheckIDDataByte[12],CheckIDDataByte[13],CheckIDDataByte[14],CheckIDDataByte[15]);


	#else /* READ_PAGE_BQ2202A */
    for(i = 0;i < 128;i++) {
        CheckIDDataByte[i] = ReadOneByte();   // read eprom Partition field  1024bits = 128Bits
    }
    mutex_unlock(&bq2202a_access);

    #ifdef DEBUG_BQ2202A
    for(i = 0;i < 128;i++) {
        chg_debug( "CheckBq2202aID[%d]=%d\n",i,CheckIDDataByte[i]);
    }
    #endif
#endif
}
/**********************************************************************/
/* 	void CheckIDCompare(void)               							  */
/*																      */
/*	Description : 		This procedure reads BQ2202A'S ID on the SDQ  */
/*				  		line.                   					  */
/* 	Arguments : 		None                    					  */
/*	Global Variables:	None   										  */
/*  Returns: 			None       							          */
/**********************************************************************/
void CheckIDCompare(void) 
{
    unsigned char i = 0;
    unsigned char j = 0;
    int IDReadSign = 1;

    if (IDReadSign == 1) {
        for (i = 0; i < 1; i++) {
            ReadBq2202aID();
            CheckBq2202aID();

            oppo_check_ID_status = 0;
            if (ReadIDDataByte[7] == 0x09) {
                for (j = 1; j < 7; j++) {
                    if((ReadIDDataByte[j] == CheckIDDataByte[j + 16])
						&& (ReadIDDataByte[j] != 0xff)  && (ReadIDDataByte[j] != 0)) {
                        oppo_check_ID_status++;
                    }
                }
                if(oppo_check_ID_status > 0) {
                    IDReadSign = 0;
                    return;
                }
            } else {
                continue;
            }
        } 
        IDReadSign=0;
    }
}

int opchg_get_bq2022_manufacture_id(void)
{
    unsigned char i = 0;
	unsigned char manufac_id_buf[7] = {0x0};
	int batt_manufac_id = 0;

	if (!oppo_battery_status_init_flag) {
		return -1;
	}

    mutex_lock(&bq2202a_access);
    SendReset();
    wait_us(2);
    TestPresence();

    WriteOneByte(SKIP_ROM_CMD);              // skip rom commond
    wait_us(60);

#ifdef READ_PAGE_BQ2202A
    WriteOneByte(READ_PAGE_ID_CMD);     // read eprom Partition for page mode
#else
    WriteOneByte(READ_FIELD_ID_CMD);     // read eprom Partition for field mode
#endif
    wait_us(60);
    WriteOneByte(BQ2022_MANUFACTURE_ADDR_LOW);               // read eprom Partition Starting address low
    wait_us(60);
    WriteOneByte(BQ2022_MANUFACTURE_ADDR_HIGH);               // read eprom Partition Starting address high

	#ifdef READ_PAGE_BQ2202A
    for (i = 0;i < 7;i++) {
        manufac_id_buf[i] = ReadOneByte();   // read eprom Partition page1  256bits = 32Bits
        //chg_err("manufac_id[0x%x]:0x%x\n",i,manufac_id_buf[i]);
    }
    mutex_unlock(&bq2202a_access);
#if 0
	if(is_project(OPPO_14043)){
		if(manufac_id_buf[1] == 0x3)		//manufac_id_buf[0] must be discarded
			batt_manufac_id = BATTERY_1800MAH_XWD;
		else if(manufac_id_buf[1] == 0x1)
			batt_manufac_id = BATTERY_1800MAH_MM;
		else 
			batt_manufac_id = BATTERY_1800MAH_MM;	//default to BATTERY_1800MAH_MM
	} else if(is_project(OPPO_14037)){
		if(manufac_id_buf[6] == 0x2)
			batt_manufac_id = BATTERY_2020MAH_SONY;
		else if(manufac_id_buf[6] == 0x4)
			batt_manufac_id = BATTERY_2020MAH_ATL;
		else
			batt_manufac_id = BATTERY_2020MAH_SONY;
	}
#endif	
	chg_debug( "manufac_id[0-6]:0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",manufac_id_buf[0],manufac_id_buf[1],
		manufac_id_buf[2],manufac_id_buf[3],manufac_id_buf[4],manufac_id_buf[5],manufac_id_buf[6]);
#else 
    for(i = 0;i < 128;i++)
    {
        CheckIDDataByte[i] = ReadOneByte();   // read eprom Partition field  1024bits = 128Bits
    }
    mutex_unlock(&bq2202a_access);

    #ifdef DEBUG_BQ2202A
    for(i = 0;i < 128;i++)
    {
        chg_debug( "CheckBq2202aID[%d]=%d\n",i,CheckIDDataByte[i]);
    }
    #endif
#endif

	return batt_manufac_id;
}


void oppo_battery_status_init(void)
{
    static int CheckIDSign=5;
//    oppo_high_battery_status = g_lk_bat_high_status;
	if((!oppo_battery_status_init_flag) && (oppo_high_battery_status == 0))
	{
		while(CheckIDSign > 0)
	    {
	        CheckIDCompare();
	        CheckIDSign--;

			chg_debug("IDSign = %d, check_ID_status = %d: oppo_high_battery_status =%d \r\n",
					CheckIDSign, oppo_check_ID_status,oppo_high_battery_status);
	        if (oppo_check_ID_status > 0) {
	            oppo_high_battery_status = 1;
	            oppo_check_ID_status = 0;
	            CheckIDSign = 0;
				oppo_battery_status_init_flag = 1;
				break;
	        } else if(CheckIDSign <= 0) {
	            oppo_high_battery_status = 0;
	            oppo_check_ID_status = 0;
				oppo_battery_status_init_flag = 1;
	        }
			
	    }
	}
}

void oppo_battery_status_check(void)
{
	if ((oppo_high_battery_status == 0) && (oppo_battery_status_init_flag)) {
	    if (oppo_high_battery_check_counts < 10) {
		    CheckIDCompare();
			oppo_high_battery_check_counts++;
			chg_debug( " oppo_high_battery_check_counts =%d, \
					oppo_check_ID_status = %d, oppo_high_battery_status =%d\n",
					oppo_high_battery_check_counts, oppo_check_ID_status, oppo_high_battery_status);
		    if (oppo_check_ID_status == 6) {
				oppo_high_battery_status = 1;
				oppo_check_ID_status=0;
				oppo_high_battery_check_counts = 0;
		    }   
			 
			
	    }
	}
}

#ifdef CONFIG_OPPO_STANDARD_BATTERY_CHECK_ADC
extern bool meter_fg_20_get_battery_authenticate(void);
bool get_oppo_high_battery_status(void)
{
	return meter_fg_20_get_battery_authenticate();
}
#elif defined(CONFIG_OPPO_CHARGER_MTK6763)
bool get_oppo_high_battery_status(void)
{
	return meter_fg_30_get_battery_authenticate();
}
#else
bool get_oppo_high_battery_status(void)
{
	return oppo_high_battery_status;
}
#endif

void set_oppo_high_battery_status(int val)
{
	oppo_high_battery_status = val;
}


static int bq2202a_driver_remove(struct i2c_client *client)
{

	int ret=0;
    
	chg_debug( " ret = %d\n", ret);
	return 0;
}

static int bq2202a_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int batt_id_gpio = 0;
	int rc = 0;
	struct device_node *node = client->dev.of_node;
	enum of_gpio_flags gpio_flags;
	batt_id_gpio = of_get_named_gpio_flags(node, "qcom,batt-id-gpio", 0, &gpio_flags);
    if (!gpio_is_valid(batt_id_gpio)) {
        dev_err(&client->dev, "Invalid batt-id-gpio");
		return 0;
    }
	
#ifndef CONFIG_OPPO_CHARGER_MTK
	if(gpio_is_valid(batt_id_gpio)){
		rc = gpio_request(batt_id_gpio,"opcharger_batt_id");
		if (rc) {
			dev_err(&client->dev, "gpio_request for %d failed rc=%d\n", batt_id_gpio, rc);
			return 0;
		}		
	}
#endif
	bq2202a_gpio = batt_id_gpio;

	oppo_battery_status_init();
	
    chg_debug( "batt_id_gpio = %d \n",batt_id_gpio);
    return 0;                                                                                       
}


/**********************************************************
  *
  *   [platform_driver API] 
  *
  *********************************************************/

static const struct of_device_id bq2202a_match[] = {
	{ .compatible = "oppo,bq2202a-eprom"},
	{ },
};

static const struct i2c_device_id bq2202a_id[] = {
	{"bq2202a-eprom", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, bq2202a_id);


static struct i2c_driver bq2202a_i2c_driver = {
	.driver		= {
		.name = "bq2202a-eprom",
		.owner	= THIS_MODULE,
		.of_match_table = bq2202a_match,
	},
	.probe		= bq2202a_driver_probe,
	.remove		= bq2202a_driver_remove,
	.id_table	= bq2202a_id,
};


module_i2c_driver(bq2202a_i2c_driver);
MODULE_DESCRIPTION("Driver for bq2202a eprom device driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:bq2202a-eprom");


