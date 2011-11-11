/* drivers\video\msm\hw_mddi_lcd.c
 * LCD driver for 7x30 platform
 *
 * Copyright (C) 2010 HUAWEI Technology Co., ltd.
 * 
 * Date: 2010/12/07
 * 
 */
#include "msm_fb.h"
#include "mddihost.h"
#include "mddihosti.h"
#include <linux/mfd/pmic8058.h>
#include <mach/gpio.h>
#include <mach/vreg.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/hardware_self_adapt.h>
#include <asm/mach-types.h>
#include "hw_mddi_lcd.h"

/*delete int lcd_reset(void) */
mddi_type mddi_port_type_probe(void)
{
	mddi_type mddi_port_type = MDDI_MAX_TYPE;
	lcd_panel_type lcd_panel = lcd_panel_probe();
	/* U8800 board is MMDI type1, so config it type1 */	
	if (machine_is_msm7x30_u8800())
	{
		mddi_port_type = MDDI_TYPE1;
	}	
	/* U8820 board version A is MMDI type1, so config it type1 
	 * Version B and other is MDDI type2, so config it according to LCD
	 */
	else if(machine_is_msm7x30_u8820())
	{
		if(HW_VER_SUB_VA == get_hw_sub_board_id())
		{
			mddi_port_type = MDDI_TYPE1;
		}
		else
		{
			switch(lcd_panel)
			{
				case LCD_NT35582_BYD_WVGA:
				case LCD_NT35582_TRULY_WVGA:
					mddi_port_type = MDDI_TYPE1;
					break;
				case LCD_NT35510_ALPHA_SI_WVGA:
/* Modify to MDDI type1 to elude the freezing screen */
					mddi_port_type = MDDI_TYPE1;
					break;
				case LCD_NT35510_ALPHA_SI_WVGA_TYPE2:
					mddi_port_type = MDDI_TYPE2;
					break;
				default:
					mddi_port_type = MDDI_TYPE1;
					break;
			}
		}
	}
	/* U8800-51 board is MMDI type2, so config it according to LCD */
	else if (machine_is_msm7x30_u8800_51() || machine_is_msm8255_u8800_pro())
	{
		switch(lcd_panel)
		{
			case LCD_NT35582_BYD_WVGA:
			case LCD_NT35582_TRULY_WVGA:
				mddi_port_type = MDDI_TYPE1;
				break;
			case LCD_NT35510_ALPHA_SI_WVGA:
/* Modify to MDDI type1 to elude the freezing screen */
				mddi_port_type = MDDI_TYPE1;
				break;
			case LCD_NT35510_ALPHA_SI_WVGA_TYPE2:
				mddi_port_type = MDDI_TYPE2;
				break;
			default:
				mddi_port_type = MDDI_TYPE1;
				break;
		}
	}
	else
	{
		mddi_port_type = MDDI_TYPE1;
	}
	return mddi_port_type;
}
int process_lcd_table(struct sequence *table, size_t count, lcd_panel_type lcd_panel)
{
	unsigned int i;
    uint32 reg = 0;
    uint32 value = 0;
    uint32 time = 0;
	int ret = 0;   
   
    for (i = 0; i < count; i++) 
    {
        reg = table[i].reg;
        value = table[i].value;
        time = table[i].time;
		switch(lcd_panel)
		{
			case LCD_NT35582_BYD_WVGA:
			case LCD_NT35582_TRULY_WVGA:
			case LCD_NT35510_ALPHA_SI_WVGA:
			case LCD_NT35510_ALPHA_SI_WVGA_TYPE2:
				/* MDDI port to write the reg and value */
				ret = mddi_queue_register_write(reg,value,TRUE,0);
				break;
		
			default:
				break;
		}		
        if (time != 0)
        {
            LCD_MDELAY(time);
        }
	}
	return ret;
}
