/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

#include "msm_fb.h"
#include "tvenc.h"
#ifdef CONFIG_HUAWEI_KERNEL
#define TVOUT_NTSC 0
#define TVOUT_PAL 1
#endif
//#define TVOUT_DEBUG
#ifdef  TVOUT_DEBUG
#define TV_OUT_DEBUG(fmt, args...) printk(KERN_ERR fmt, ##args)
#else
#define TV_OUT_DEBUG(fmt, args...)
#endif
#define TV_DIMENSION_MAX_WIDTH      720
#define TV_DIMENSION_MAX_HEIGHT     576
boolean tv_cable_connected = FALSE;

struct tvout_msm_state_type {
	struct platform_device *pdev;
	struct timer_list hpd_state_timer;
	struct timer_list hpd_work_timer;
	struct work_struct hpd_work;
	uint32 hpd_int_status;
	uint32 prev_hpd_int_status;
	uint32 five_retry;
	int irq;
	int video_mode;
	uint16 y_res;
	boolean hpd_initialized;
	boolean disp_powered_up;
	struct kobject *uevent_kobj;
};

static struct tvout_msm_state_type *tvout_msm_state;

static int tvout_off(struct platform_device *pdev);
static int tvout_on(struct platform_device *pdev);
static void tvout_check_status(void);

static void tvout_check_status()
{
	tvout_msm_state->hpd_int_status &= 0x05;
	/* hpd_int_status could either be 0x05 or 0x04 for a cable
		plug-out event when cable detect is driven by polling. */
	if ((((tvout_msm_state->hpd_int_status == 0x05) ||
		(tvout_msm_state->hpd_int_status == 0x04)) &&
		(tvout_msm_state->prev_hpd_int_status == BIT(2))) ||
		((tvout_msm_state->hpd_int_status == 0x01) &&
		(tvout_msm_state->prev_hpd_int_status == BIT(0)))) {
		TV_OUT_DEBUG("%s: cable event sent already!", __func__);
		return;
	}

	if (tvout_msm_state->hpd_int_status & BIT(2)) {
		tv_cable_connected = FALSE;
		TV_OUT_DEBUG("%s: cable plug-out\n", __func__);
		tvout_msm_state->prev_hpd_int_status = BIT(2);
	} else if (tvout_msm_state->hpd_int_status & BIT(0)) {
		tv_cable_connected = TRUE;
		TV_OUT_DEBUG("%s: cable plug-in\n", __func__);
		tvout_msm_state->prev_hpd_int_status = BIT(0);
	}
}

/* ISR for TV out cable detect */
static irqreturn_t tvout_msm_isr(int irq, void *dev_id)
{
	tvout_msm_state->hpd_int_status = TV_IN(TV_INTR_STATUS);
	TV_OUT(TV_INTR_CLEAR, tvout_msm_state->hpd_int_status);
	TV_OUT_DEBUG("%s: ISR: 0x%02x\n", __func__,
		tvout_msm_state->hpd_int_status & 0x05);

	if (!tvout_msm_state || !tvout_msm_state->disp_powered_up) {
		TV_OUT_DEBUG("%s: ISR ignored, display not yet powered on\n",
			__func__);
		return IRQ_HANDLED;
	}
	if (tvout_msm_state->hpd_int_status & BIT(0) ||
		tvout_msm_state->hpd_int_status & BIT(2)) {
		/* Use .75sec to debounce the interrupt */
		mod_timer(&tvout_msm_state->hpd_state_timer, jiffies
			+ msecs_to_jiffies(750));
	}

	return IRQ_HANDLED;
}

/* Interrupt debounce timer */
static void tvout_msm_hpd_state_timer(unsigned long data)
{
	if (!tvout_msm_state || !tvout_msm_state->disp_powered_up) {
		TV_OUT_DEBUG("%s: ignored, display powered off\n", __func__);
		return;
	}

	/* TV_INTR_STATUS[0x204]
		When a TV_ENC interrupt occurs, then reading this register will
		indicate what caused the interrupt since that each bit indicates
		the source of the interrupt that had happened. If multiple
		interrupt sources had happened, then multiple bits of this
		register will be set
		Bit 0 : Load present on Video1
		Bit 1 : Load present on Video2
		Bit 2 : Load removed on Video1
		Bit 3 : Load removed on Video2
	*/

	/* Locking interrupt status is not required because
	last status read after debouncing is used */
	if ((tvout_msm_state->hpd_int_status & 0x05) == 0x05) {
		/* SW-workaround :If the status read after debouncing is
		0x05(indicating both load present & load removed- which can't
		happen in reality), force an update. If status remains 0x05
		after retry, it's a cable unplug event */
		if (++tvout_msm_state->five_retry < 2) {
			uint32 reg;
			TV_OUT_DEBUG("tvout: Timer: 0x05\n");
			TV_OUT(TV_INTR_CLEAR, 0xf);
			reg = TV_IN(TV_DAC_INTF);
			TV_OUT(TV_DAC_INTF, reg & ~TVENC_LOAD_DETECT_EN);
			TV_OUT(TV_INTR_CLEAR, 0xf);
			reg = TV_IN(TV_DAC_INTF);
			TV_OUT(TV_DAC_INTF, reg | TVENC_LOAD_DETECT_EN);
			return;
		}
	}
	tvout_msm_state->five_retry = 0;
	tvout_check_status();
}

static ssize_t tvout_msm_rda_video_mode(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t ret = snprintf(buf, PAGE_SIZE, "%d\n",
		tvout_msm_state->video_mode);
	pr_info("%s: '%d'\n", __func__, tvout_msm_state->video_mode);
	return ret;
}

static ssize_t tvout_msm_wta_video_mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	kobject_uevent(tvout_msm_state->uevent_kobj,
		KOBJ_OFFLINE);
	strict_strtoul(buf, 10,
		(unsigned long *)&tvout_msm_state->video_mode);
	kobject_uevent(tvout_msm_state->uevent_kobj,
		KOBJ_ONLINE);
	pr_info("%s: '%d'\n", __func__, tvout_msm_state->video_mode);
	return ret;
}

/* sysfs attribute for TVOut video mode */
static DEVICE_ATTR(video_mode, S_IRUGO | S_IWUGO, tvout_msm_rda_video_mode,
	tvout_msm_wta_video_mode);

static struct attribute *tvout_msm_fs_attrs[] = {
	&dev_attr_video_mode.attr,
	NULL,
};
static struct attribute_group tvout_msm_fs_attr_group = {
	.attrs = tvout_msm_fs_attrs,
};

/* create TVOut kobject and initialize */
static int tvout_msm_state_create(struct platform_device *pdev)
{
	int rc;
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);

	rc = sysfs_create_group(&mfd->fbi->dev->kobj,
			&tvout_msm_fs_attr_group);
	if (rc) {
		pr_err("%s: sysfs group creation failed, rc=%d\n", __func__,
			rc);
		return rc;
	}

	tvout_msm_state->uevent_kobj = &mfd->fbi->dev->kobj;
	return 0;
}

static int tvout_on(struct platform_device *pdev)
{
	uint32 reg = 0;
	int ret = 0, rc;
	struct fb_var_screeninfo *var;
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);;

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	var = &mfd->fbi->var;

#ifdef CONFIG_HUAWEI_KERNEL
	if(TVOUT_PAL == var->reserved[4])
	{
		tvout_msm_state->video_mode = PAL_M;
	}
	else
	{
		tvout_msm_state->video_mode = NTSC_M;
	}
#endif
	if (!tvout_msm_state->uevent_kobj) {
		rc = tvout_msm_state_create(pdev);
		if (rc) {
			pr_err("Init FAILED: tvout_msm_state_create, rc=%d\n",
				rc);
			goto error;
		}
		kobject_uevent(tvout_msm_state->uevent_kobj, KOBJ_ADD);
		pr_info("%s: kobject_uevent(KOBJ_ADD)\n", __func__);
	}

	TV_OUT(TV_ENC_CTL, 0);	/* disable TV encoder */

	switch (tvout_msm_state->video_mode) {
	case NTSC_M:
	case NTSC_J:
		TV_OUT(TV_CGMS, 0x0);
		/*  NTSC Timing */
		TV_OUT(TV_SYNC_1, 0x0020009e);
		TV_OUT(TV_SYNC_2, 0x011306B4);
		TV_OUT(TV_SYNC_3, 0x0006000C);
		TV_OUT(TV_SYNC_4, 0x0028020D);
		TV_OUT(TV_SYNC_5, 0x005E02FB);
		TV_OUT(TV_SYNC_6, 0x0006000C);
		TV_OUT(TV_SYNC_7, 0x00000012);
		TV_OUT(TV_BURST_V1, 0x0013020D);
		TV_OUT(TV_BURST_V2, 0x0014020C);
		TV_OUT(TV_BURST_V3, 0x0013020D);
		TV_OUT(TV_BURST_V4, 0x0014020C);
		TV_OUT(TV_BURST_H, 0x00AE00F2);
		TV_OUT(TV_SOL_REQ_ODD, 0x00280208);
		TV_OUT(TV_SOL_REQ_EVEN, 0x00290209);

		reg |= TVENC_CTL_TV_MODE_NTSC_M_PAL60;

		if (tvout_msm_state->video_mode == NTSC_M) {
			/* Cr gain 11, Cb gain C6, y_gain 97 */
			TV_OUT(TV_GAIN, 0x0081B697);
		} else {
			/* Cr gain 11, Cb gain C6, y_gain 97 */
			TV_OUT(TV_GAIN, 0x008bc4a3);
			reg |= TVENC_CTL_NTSCJ_MODE;
		}

		var->yres = 480;
		break;
	case PAL_BDGHIN:
	case PAL_N:
		/*  PAL Timing */
		TV_OUT(TV_SYNC_1, 0x00180097);
		TV_OUT(TV_SYNC_3, 0x0005000a);
		TV_OUT(TV_SYNC_4, 0x00320271);
		TV_OUT(TV_SYNC_5, 0x005602f9);
		TV_OUT(TV_SYNC_6, 0x0005000a);
		TV_OUT(TV_SYNC_7, 0x0000000f);
		TV_OUT(TV_BURST_V1, 0x0012026e);
		TV_OUT(TV_BURST_V2, 0x0011026d);
		TV_OUT(TV_BURST_V3, 0x00100270);
		TV_OUT(TV_BURST_V4, 0x0013026f);
		TV_OUT(TV_SOL_REQ_ODD, 0x0030026e);
		TV_OUT(TV_SOL_REQ_EVEN, 0x0031026f);

		if (tvout_msm_state->video_mode == PAL_BDGHIN) {
			/* Cr gain 11, Cb gain C6, y_gain 97 */
			TV_OUT(TV_GAIN, 0x0088c1a0);
			TV_OUT(TV_CGMS, 0x00012345);
			TV_OUT(TV_SYNC_2, 0x011f06c0);
			TV_OUT(TV_BURST_H, 0x00af00ea);
			reg |= TVENC_CTL_TV_MODE_PAL_BDGHIN;
		} else {
			/* Cr gain 11, Cb gain C6, y_gain 97 */
			TV_OUT(TV_GAIN, 0x0081b697);
			TV_OUT(TV_CGMS, 0x000af317);
			TV_OUT(TV_SYNC_2, 0x12006c0);
			TV_OUT(TV_BURST_H, 0x00af00fa);
			reg |= TVENC_CTL_TV_MODE_PAL_N;
		}
		var->yres = 576;
		break;
	case PAL_M:
		/* Cr gain 11, Cb gain C6, y_gain 97 */
		TV_OUT(TV_GAIN, 0x0081b697);
		TV_OUT(TV_CGMS, 0x000af317);
		TV_OUT(TV_TEST_MUX, 0x000001c3);
		TV_OUT(TV_TEST_MODE, 0x00000002);
		/*  PAL Timing */
		TV_OUT(TV_SYNC_1, 0x0020009e);
		TV_OUT(TV_SYNC_2, 0x011306b4);
		TV_OUT(TV_SYNC_3, 0x0006000c);
		TV_OUT(TV_SYNC_4, 0x0028020D);
		TV_OUT(TV_SYNC_5, 0x005e02fb);
		TV_OUT(TV_SYNC_6, 0x0006000c);
		TV_OUT(TV_SYNC_7, 0x00000012);
		TV_OUT(TV_BURST_V1, 0x0012020b);
		TV_OUT(TV_BURST_V2, 0x0016020c);
		TV_OUT(TV_BURST_V3, 0x00150209);
		TV_OUT(TV_BURST_V4, 0x0013020c);
		TV_OUT(TV_BURST_H, 0x00bf010b);
		TV_OUT(TV_SOL_REQ_ODD, 0x00280208);
		TV_OUT(TV_SOL_REQ_EVEN, 0x00290209);

		reg |= TVENC_CTL_TV_MODE_PAL_M;
		var->yres = 480;
		break;
	default:
		return -ENODEV;
	}

#ifdef CONFIG_FB_MSM_TVOUT_SVIDEO
	reg |= TVENC_CTL_S_VIDEO_EN;
#endif

	reg |= TVENC_CTL_Y_FILTER_EN |
	    TVENC_CTL_CR_FILTER_EN |
	    TVENC_CTL_CB_FILTER_EN | TVENC_CTL_SINX_FILTER_EN;

	/* DC offset to 0. */
	TV_OUT(TV_LEVEL, 0x00000000);
	TV_OUT(TV_OFFSET, 0x008080f0);

#if defined(CONFIG_FB_MSM_MDP31)
	TV_OUT(TV_DAC_INTF, 0x29);
#endif
	TV_OUT(TV_ENC_CTL, reg);

	/* Enable TV Out */
	reg |= TVENC_CTL_ENC_EN;
	TV_OUT(TV_ENC_CTL, reg);
	reg = TV_IN(TV_DAC_INTF);
	reg |= TVENC_LOAD_DETECT_EN;
	TV_OUT(TV_DAC_INTF, reg);
	/* Enable Load present & removal interrupts for Video1 */
	TV_OUT(TV_INTR_ENABLE, 0x5);
	tvout_msm_state->disp_powered_up = TRUE;
	/* Enable interrupts when display is on */
	enable_irq(tvout_msm_state->irq);

error:
	return ret;
}

static int tvout_off(struct platform_device *pdev)
{
	TV_OUT(TV_ENC_CTL, 0);	/* disable TV encoder */
	disable_irq(tvout_msm_state->irq);
	tvout_msm_state->disp_powered_up = FALSE;
	return 0;
}

static int __init tvout_probe(struct platform_device *pdev)
{
	int rc = 0;

#ifdef CONFIG_FB_MSM_TVOUT_NTSC_M
	tvout_msm_state->video_mode = NTSC_M;
#elif defined CONFIG_FB_MSM_TVOUT_NTSC_J
	tvout_msm_state->video_mode = NTSC_J;
#elif defined CONFIG_FB_MSM_TVOUT_PAL_M
	tvout_msm_state->video_mode = PAL_M;
#elif defined CONFIG_FB_MSM_TVOUT_PAL_N
	tvout_msm_state->video_mode = PAL_N;
#elif defined CONFIG_FB_MSM_TVOUT_PAL_BDGHIN
	tvout_msm_state->video_mode = PAL_BDGHIN;
#endif
	msm_fb_add_device(pdev);
	tvout_msm_state->irq = INT_TV_ENC;
	TV_OUT_DEBUG("%s: tvout_msm_state->irq : %d",
			__func__, tvout_msm_state->irq);
	
	rc = request_irq(tvout_msm_state->irq, &tvout_msm_isr,
		IRQF_TRIGGER_HIGH, "tvout_msm_isr", NULL);

	if (rc) {
		TV_OUT_DEBUG("Init FAILED: IRQ request, rc=%d\n", rc);
		goto error;
	}
	disable_irq(tvout_msm_state->irq);

	init_timer(&tvout_msm_state->hpd_state_timer);
	tvout_msm_state->hpd_state_timer.function =
		tvout_msm_hpd_state_timer;
	tvout_msm_state->hpd_state_timer.data = (uint32)NULL;
	tvout_msm_state->hpd_state_timer.expires = jiffies
						+ msecs_to_jiffies(1000);
error:
	return 0;
}

static int __devexit tvout_remove(struct platform_device *pdev)
{
	if (tvout_msm_state->uevent_kobj) {
		sysfs_remove_group(tvout_msm_state->uevent_kobj,
			&tvout_msm_fs_attr_group);
		tvout_msm_state->uevent_kobj = NULL;
	}
	kfree(tvout_msm_state);
	tvout_msm_state = NULL;
	return 0;
}

static struct platform_driver this_driver = {
	.probe  = tvout_probe,
	.remove = tvout_remove,
	.driver = {
		.name   = "tvout_device",
	},
};

static struct msm_fb_panel_data tvout_panel_data = {
	.panel_info.xres = TV_DIMENSION_MAX_WIDTH,
	.panel_info.yres = TV_DIMENSION_MAX_HEIGHT,
	.panel_info.type = TV_PANEL,
	.panel_info.pdest = DISPLAY_2,
	.panel_info.wait_cycle = 0,
#ifdef CONFIG_FB_MSM_MDP40
	.panel_info.bpp = 24,
#else
	.panel_info.bpp = 16,
#endif
	.panel_info.fb_num = 2,
	.on = tvout_on,
	.off = tvout_off,
};

static struct platform_device this_device = {
	.name   = "tvout_device",
	.dev	= {
		.platform_data = &tvout_panel_data,
	}
};

static int __init tvout_init(void)
{
	int ret;
	tvout_msm_state = kzalloc(sizeof(*tvout_msm_state), GFP_KERNEL);
	if (!tvout_msm_state) {
		pr_err("tvout_msm_init FAILED: out of memory\n");
		ret = -ENOMEM;
		goto init_exit;
	}

	ret = platform_driver_register(&this_driver);
	if (!ret) {
		ret = platform_device_register(&this_device);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return 0;
init_exit:
	kfree(tvout_msm_state);
	tvout_msm_state = NULL;
	return ret;
}

static void __exit tvout_exit(void)
{
	platform_device_unregister(&this_device);
	platform_driver_unregister(&this_driver);
}

module_init(tvout_init);
module_exit(tvout_exit);

MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Qualcomm Innovation Center, Inc.");
MODULE_DESCRIPTION("TV out driver");
