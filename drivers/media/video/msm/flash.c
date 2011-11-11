
/* Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
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
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pwm.h>
#include <linux/pmic8058-pwm.h>
#include <linux/hrtimer.h>
#include <mach/pmic.h>
#include <mach/camera.h>
#include <mach/gpio.h>

struct timer_list timer_flash;
#ifdef CONFIG_HUAWEI_KERNEL

#include <linux/mfd/pmic8058.h>
#include <linux/gpio.h>

#ifdef CONFIG_HUAWEI_EVALUATE_POWER_CONSUMPTION 
#include <mach/msm_battery.h>
#define CAMERA_FLASH_CUR_DIV 10
#endif


#define PM8058_GPIO_PM_TO_SYS(pm_gpio)     (pm_gpio + NR_GPIO_IRQS)*/
static struct  pm8058_gpio camera_flash = {
		.direction      = PM_GPIO_DIR_OUT,
		.output_buffer  = PM_GPIO_OUT_BUF_CMOS,
		.output_value   = 0,
		.pull           = PM_GPIO_PULL_NO,
		.vin_sel        = 0,
		.out_strength   = PM_GPIO_STRENGTH_HIGH,
		.function       = PM_GPIO_FUNC_2,
		.inv_int_pol 	= 1,
	};
#endif

static int msm_camera_flash_pwm(
	struct msm_camera_sensor_flash_pwm *pwm,
	unsigned led_state)
{
	int rc = 0;
	int PWM_PERIOD = USEC_PER_SEC / pwm->freq;

	/*description:pwm camera flash*/
	#ifdef CONFIG_HUAWEI_KERNEL
	static struct pwm_device *flash_pwm = NULL;
    #else 
    static struct pwm_device *flash_pwm;
    #endif
	/*If it is the first time to enter the function*/
  	if (!flash_pwm) {
        #ifdef CONFIG_HUAWEI_KERNEL
		 rc = pm8058_gpio_config( 23, &camera_flash);
  	 	 if (rc)  {
        	pr_err("%s PMIC GPIO 24 write failed\n", __func__);
     	 	return rc;
      	 }
        #endif
		flash_pwm = pwm_request(pwm->channel, "camera-flash");
		if (flash_pwm == NULL || IS_ERR(flash_pwm)) {
			pr_err("%s: FAIL pwm_request(): flash_pwm=%p\n",
			       __func__, flash_pwm);
			flash_pwm = NULL;
			return -ENXIO;
		}
	}

	switch (led_state) {
	case MSM_CAMERA_LED_LOW:
		rc = pwm_config(flash_pwm,
			(PWM_PERIOD/pwm->max_load)*pwm->low_load,
			PWM_PERIOD);
		if (rc >= 0)
			rc = pwm_enable(flash_pwm);
		break;

	case MSM_CAMERA_LED_HIGH:
		rc = pwm_config(flash_pwm,
			(PWM_PERIOD/pwm->max_load)*pwm->high_load,
			PWM_PERIOD);
		if (rc >= 0)
			rc = pwm_enable(flash_pwm);
		break;

	case MSM_CAMERA_LED_OFF:
		pwm_disable(flash_pwm);
		break;

	default:
		rc = -EFAULT;
		break;
	}

	return rc;
}

int msm_camera_flash_pmic(
	struct msm_camera_sensor_flash_pmic *pmic,
	unsigned led_state)
{
	int rc = 0;

	switch (led_state) {
	case MSM_CAMERA_LED_OFF:
		rc = pmic->pmic_set_current(pmic->led_src_1, 0);
		break;

	case MSM_CAMERA_LED_LOW:
		rc = pmic->pmic_set_current(pmic->led_src_1,
				pmic->low_current);
		break;

	case MSM_CAMERA_LED_HIGH:
		if (pmic->num_of_src == 2) {
			rc = pmic->pmic_set_current(pmic->led_src_1,
				pmic->high_current);
			rc = pmic->pmic_set_current(pmic->led_src_2,
				pmic->high_current);
		} else
			rc = pmic->pmic_set_current(pmic->led_src_1,
				pmic->high_current);
		break;

	default:
		rc = -EFAULT;
		break;
	}
	CDBG("flash_set_led_state: return %d\n", rc);

	return rc;
}

int32_t msm_camera_flash_set_led_state(
	struct msm_camera_sensor_flash_data *fdata, unsigned led_state)
{
	int32_t rc;

	CDBG("flash_set_led_state: %d flash_sr_type=%d\n", led_state,
	    fdata->flash_src->flash_sr_type);

	if (fdata->flash_type != MSM_CAMERA_FLASH_LED)
		return -ENODEV;

	switch (fdata->flash_src->flash_sr_type) {
	case MSM_CAMERA_FLASH_SRC_PMIC:
		rc = msm_camera_flash_pmic(&fdata->flash_src->_fsrc.pmic_src,
			led_state);
		break;

	case MSM_CAMERA_FLASH_SRC_PWM:
		rc = msm_camera_flash_pwm(&fdata->flash_src->_fsrc.pwm_src,
			led_state);
		break;

	default:
		rc = -ENODEV;
		break;
	}

#ifdef CONFIG_HUAWEI_EVALUATE_POWER_CONSUMPTION 
    /* start calculate flash consume */
	switch (led_state) {
	case MSM_CAMERA_LED_OFF:
        huawei_rpc_current_consuem_notify(EVENT_CAMERA_FLASH_STATE, 0);
		break;

	case MSM_CAMERA_LED_LOW:
        /* the consume depend on low_current */
        huawei_rpc_current_consuem_notify(EVENT_CAMERA_FLASH_STATE, fdata->flash_src->_fsrc.pmic_src.low_current);
		break;

	case MSM_CAMERA_LED_HIGH:
        /* the consume depend on high_current */
        huawei_rpc_current_consuem_notify(EVENT_CAMERA_FLASH_STATE, fdata->flash_src->_fsrc.pmic_src.high_current);
		break;

	default:
		break;
	}
#endif


	return rc;
}

static int msm_strobe_flash_xenon_charge(
		int32_t flash_charge, int32_t charge_enable)
{
	gpio_direction_output(flash_charge, charge_enable);
	/* add timer for the recharge */
	add_timer(&timer_flash);

	return 0;
}

static void strobe_flash_xenon_recharge_handler(unsigned long data)
{
	unsigned long flags;
	struct msm_camera_sensor_strobe_flash_data *sfdata =
		(struct msm_camera_sensor_strobe_flash_data *)data;

	spin_lock_irqsave(&sfdata->timer_lock, flags);
	msm_strobe_flash_xenon_charge(sfdata->flash_charge, 1);
	spin_unlock_irqrestore(&sfdata->timer_lock, flags);

	return;
}

static irqreturn_t strobe_flash_charge_ready_irq(int irq_num, void *data)
{
	struct msm_camera_sensor_strobe_flash_data *sfdata =
		(struct msm_camera_sensor_strobe_flash_data *)data;

	/* put the charge signal to low */
	gpio_direction_output(sfdata->flash_charge, 0);

	return IRQ_HANDLED;
}

static int msm_strobe_flash_xenon_init(
	struct msm_camera_sensor_strobe_flash_data *sfdata)
{
	int rc = 0;

	rc = request_irq(sfdata->irq, strobe_flash_charge_ready_irq,
			IRQF_TRIGGER_FALLING, "charge_ready", sfdata);
	if (rc < 0) {
		pr_err("%s: request_irq failed %d\n", __func__, rc);
		return rc;
	}
	rc = gpio_request(sfdata->flash_charge, "charge");
	if (rc < 0) {
		pr_err("%s: gpio_request failed\n", __func__);
		free_irq(sfdata->irq, sfdata);
		return rc;
	}
	spin_lock_init(&sfdata->timer_lock);
	/* setup timer */
	init_timer(&timer_flash);
	timer_flash.function = strobe_flash_xenon_recharge_handler;
	timer_flash.data = (unsigned long)sfdata;
	timer_flash.expires = jiffies +
		msecs_to_jiffies(sfdata->flash_recharge_duration);

	return rc;
}

static int msm_strobe_flash_xenon_release
	(struct msm_camera_sensor_strobe_flash_data *sfdata)
{
	free_irq(sfdata->irq, sfdata);
	gpio_free(sfdata->flash_charge);
	del_timer_sync(&timer_flash);
	return 0;
}

static void msm_strobe_flash_xenon_fn_init
	(struct msm_strobe_flash_ctrl *strobe_flash_ptr)
{
	strobe_flash_ptr->strobe_flash_init =
				msm_strobe_flash_xenon_init;
	strobe_flash_ptr->strobe_flash_charge =
				msm_strobe_flash_xenon_charge;
	strobe_flash_ptr->strobe_flash_release =
				msm_strobe_flash_xenon_release;
}

int msm_strobe_flash_init(struct msm_sync *sync, uint32_t sftype)
{
	int rc = 0;
	switch (sftype) {
	case MSM_CAMERA_STROBE_FLASH_XENON:
		msm_strobe_flash_xenon_fn_init(&sync->sfctrl);
		rc = sync->sfctrl.strobe_flash_init(
			sync->sdata->strobe_flash_data);
		break;
	default:
		rc = -ENODEV;
	}
	return rc;
}
