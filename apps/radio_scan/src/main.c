/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Radio bring-up: everything happens in the ieee802154 shell.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

LOG_MODULE_REGISTER(radio_scan, LOG_LEVEL_INF);

int main(void)
{
	struct net_if *iface = net_if_get_ieee802154();

	if (iface == NULL) {
		LOG_ERR("no IEEE 802.15.4 interface");
		return -ENODEV;
	}

	LOG_INF("rf2xx up, try: ieee802154 scan active all 300");
	return 0;
}
