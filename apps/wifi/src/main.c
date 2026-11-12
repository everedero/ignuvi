/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wi-Fi bring-up on the AP6212: scan and log the networks, then join
 * CONFIG_IGNUVI_WIFI_SSID (if set), wait for a DHCP address and ping 8.8.8.8.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/icmp.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(scan_done, 0, 1);
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(bound, 0, 1);
static K_SEM_DEFINE(pong, 0, 1);
static int found;

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_SCAN_RESULT) {
		const struct wifi_scan_result *r = cb->info;

		LOG_INF("%-32.*s ch %2u %4d dBm %s", r->ssid_length, r->ssid, r->channel, r->rssi,
			wifi_security_txt(r->security));
		found++;
	} else if (event == NET_EVENT_WIFI_SCAN_DONE) {
		k_sem_give(&scan_done);
	} else if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *s = cb->info;

		LOG_INF("connect result %d", s->status);
		if (!s->status) {
			k_sem_give(&connected);
		}
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		struct net_if_addr *a = &iface->config.ip.ipv4->unicast[i].ipv4;

		if (a->is_used && a->addr_type == NET_ADDR_DHCP) {
			LOG_INF("DHCP address %s",
				net_addr_ntop(AF_INET, &a->address.in_addr, buf, sizeof(buf)));
			k_sem_give(&bound);
		}
	}
}

static enum net_verdict ping_reply(struct net_icmp_ctx *ctx, struct net_pkt *pkt,
				   struct net_icmp_ip_hdr *ip_hdr, struct net_icmp_hdr *icmp_hdr,
				   void *user_data)
{
	k_sem_give(&pong);
	return NET_OK;
}

static void ping(struct net_if *iface, const char *host, int count)
{
	struct net_sockaddr_in dst = {.sin_family = NET_AF_INET};
	struct net_icmp_ctx ctx;
	int ok = 0;

	net_addr_pton(NET_AF_INET, host, &dst.sin_addr);
	if (net_icmp_init_ctx(&ctx, NET_AF_INET, NET_ICMPV4_ECHO_REPLY, 0, ping_reply)) {
		LOG_ERR("no ICMP context");
		return;
	}
	for (int i = 0; i < count; i++) {
		struct net_icmp_ping_params params = {.sequence = i, .data_size = 32};
		int64_t t = k_uptime_get();
		int err = net_icmp_send_echo_request(&ctx, iface, (struct net_sockaddr *)&dst,
						     &params, NULL);

		if (err) {
			LOG_ERR("ping %s seq %d: send failed (%d)", host, i, err);
		} else if (k_sem_take(&pong, K_SECONDS(2))) {
			LOG_WRN("ping %s seq %d: timeout", host, i);
		} else {
			LOG_INF("ping %s seq %d: %lld ms", host, i, k_uptime_get() - t);
			ok++;
		}
		k_sleep(K_SECONDS(1));
	}
	LOG_INF("ping %s: %d/%d replies", host, ok, count);
	net_icmp_cleanup_ctx(&ctx);
}

int main(void)
{
	struct net_if *iface = net_if_get_wifi_sta();
	int err;

	if (!iface) {
		LOG_ERR("no Wi-Fi interface");
		return 0;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE |
					     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	err = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (err) {
		LOG_ERR("scan request failed (%d)", err);
		return 0;
	}
	k_sem_take(&scan_done, K_SECONDS(30));
	LOG_INF("scan done, %d networks", found);

	if (sizeof(CONFIG_IGNUVI_WIFI_SSID) == 1) {
		return 0;
	}

	struct wifi_connect_req_params p = {
		.ssid = CONFIG_IGNUVI_WIFI_SSID,
		.ssid_length = sizeof(CONFIG_IGNUVI_WIFI_SSID) - 1,
		.psk = CONFIG_IGNUVI_WIFI_PSK,
		.psk_length = sizeof(CONFIG_IGNUVI_WIFI_PSK) - 1,
		.security = sizeof(CONFIG_IGNUVI_WIFI_PSK) > 1 ? WIFI_SECURITY_TYPE_PSK
							       : WIFI_SECURITY_TYPE_NONE,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.channel = WIFI_CHANNEL_ANY,
		.mfp = WIFI_MFP_OPTIONAL,
	};

	LOG_INF("joining %s", CONFIG_IGNUVI_WIFI_SSID);
	err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
	if (err) {
		LOG_ERR("connect request failed (%d)", err);
		return 0;
	}
	if (k_sem_take(&connected, K_SECONDS(30))) {
		LOG_ERR("not connected after 30 s");
		return 0;
	}
	net_dhcpv4_start(iface);
	if (k_sem_take(&bound, K_SECONDS(30))) {
		LOG_ERR("no DHCP address after 30 s");
		return 0;
	}
	ping(iface, "8.8.8.8", 4);
	return 0;
}
