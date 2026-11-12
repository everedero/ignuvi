/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/init.h>

/* pwm-clock only runs once turned on, and the Bluetooth driver never does it */
static int ignuvi_lpo_init(void)
{
	const struct device *lpo = DEVICE_DT_GET(DT_NODELABEL(ap6212_lpo));

	if (!device_is_ready(lpo)) {
		return -ENODEV;
	}

	return clock_control_on(lpo, 0);
}

SYS_INIT(ignuvi_lpo_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
