/* drivers\video\msm\hw_mddi_lcd.h
 * LCD driver for 7x30 platform
 *
 * Copyright (C) 2010 HUAWEI Technology Co., ltd.
 * 
 * Date: 2010/12/07
 * 
 */
#ifndef HW_MDDI_LCD_H
#define HW_MDDI_LCD_H

#include <linux/hardware_self_adapt.h>
#include "lcd_hw_debug.h"

#define LCD_DEBUG
#ifdef  LCD_DEBUG
#define MDDI_LCD_DEBUG(fmt, args...) printk(KERN_ERR fmt, ##args)
#else
#define MDDI_LCD_DEBUG(fmt, args...)
#endif
/* MDDI interface type 1 or 2 */
typedef enum
{
    MDDI_TYPE1 = 0,
    MDDI_TYPE2,
    MDDI_MAX_TYPE
}mddi_type;
/* MDDI output bpp type */
typedef enum
{
    MDDI_OUT_16BPP = 16,
    MDDI_OUT_24BPP = 24,
    MDDI_OUT_MAX_BPP = 0xFF
}bpp_type;

/* LCD_MDELAY will select mdelay or msleep according value */
#define LCD_MDELAY(time_ms)   	\
	do							\
	{ 							\
		if (time_ms>10)			\
			msleep(time_ms);	\
		else					\
			mdelay(time_ms);	\
	}while(0)

mddi_type mddi_port_type_probe(void);
int process_lcd_table(struct sequence *table, size_t count, lcd_panel_type lcd_panel);
/*delete int lcd_reset(void) */

#endif
