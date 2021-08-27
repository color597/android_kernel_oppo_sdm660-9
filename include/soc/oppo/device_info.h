/**
 * Copyright 2008-2013 OPPO Mobile Comm Corp., Ltd, All rights reserved.
 * VENDOR_EDIT:
 * FileName:devinfo.h
 * ModuleName:devinfo
 * Author: wangjc
 * Create Date: 2013-10-23
 * Description:add interface to get device information.
 * History:
   <version >  <time>  <author>  <desc>
   1.0		2013-10-23	wangjc	init
*/

#ifndef _DEVICE_INFO_H
#define _DEVICE_INFO_H

#if 0
//dram type
enum{
	DRAM_TYPE0 = 0,
	DRAM_TYPE1,	
	DRAM_TYPE2,
	DRAM_TYPE3,
	DRAM_UNKNOWN,
};
#endif

enum{
	MAINBOARD_RESOURCE0 = 0,
	MAINBOARD_RESOURCE1 = 1,
	MAINBOARD_RESOURCE2 = 2,
};

struct manufacture_info
{
	char *version;
	char *manufacture;
};

int register_device_proc(char *name, char *version, char *manufacture);


#endif /*_DEVICE_INFO_H*/
