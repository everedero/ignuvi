/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pilot-wire heater control over Bluetooth LE: one custom GATT service with
 * a Mode characteristic (enum pw_mode as uint8), read / write / notify.
 * Writes need an encrypted (bonded) link.
 *
 * Pairing follows GAP: a 3 s press on the middle button (sw0) enters limited
 * discoverable mode for 180 s, the blue LED blinks and Just Works pairing is
 * accepted. Outside it, the board is non-discoverable and only bonded peers
 * may connect (filter accept list). A press on the hidden button (sw1) erases
 * every bond. With no bond, the board stays in pairing mode until a host bonds.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "pilot_wire.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LONG_PRESS   K_SECONDS(3)
#define PAIRING_TIME K_SECONDS(CONFIG_BT_LIM_ADV_TIMEOUT)
#define BLINK_MS     250

/* 6f1e0001-5b2c-4d8a-9c3e-2a7b41c0d5e1, Mode is ...0002 */
#define PW_SVC_UUID_VAL  BT_UUID_128_ENCODE(0x6f1e0001, 0x5b2c, 0x4d8a, 0x9c3e, 0x2a7b41c0d5e1)
#define PW_MODE_UUID_VAL BT_UUID_128_ENCODE(0x6f1e0002, 0x5b2c, 0x4d8a, 0x9c3e, 0x2a7b41c0d5e1)

static const struct bt_uuid_128 pw_svc_uuid = BT_UUID_INIT_128(PW_SVC_UUID_VAL);
static const struct bt_uuid_128 pw_mode_uuid = BT_UUID_INIT_128(PW_MODE_UUID_VAL);

static uint8_t mode = PW_CONFORT;
static atomic_t pairing;

static const struct gpio_dt_spec pair_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec erase_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static struct gpio_callback pair_cb, erase_cb;

static int mode_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint8_t val;

	if (len != 1 || read_cb(cb_arg, &val, 1) != 1) {
		return -EINVAL;
	}
	if (!strcmp(name, "inv") && val <= 1) {
		pilot_wire_set_invert(val);
		return 0;
	}
	if (strcmp(name, "mode") || val >= PW_MODE_COUNT) {
		return -EINVAL;
	}
	mode = val;
	return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(pw, "pw", NULL, mode_set, NULL, NULL);

static ssize_t read_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &mode, sizeof(mode));
}

static ssize_t write_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags);

/* clang-format off */
BT_GATT_SERVICE_DEFINE(pw_svc,
	BT_GATT_PRIMARY_SERVICE(&pw_svc_uuid),
	BT_GATT_CHARACTERISTIC(&pw_mode_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT,
			       read_mode, write_mode, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
);
/* clang-format on */

static ssize_t write_mode(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			  uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (app_set_mode(*(const uint8_t *)buf)) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	return len;
}

int app_set_mode(uint8_t val)
{
	int err;

	if (val >= PW_MODE_COUNT) {
		return -EINVAL;
	}
	mode = val;
	pilot_wire_set(val);
	LOG_INF("mode %u", val);

	err = settings_save_one("pw/mode", &val, 1);
	if (err) {
		LOG_ERR("mode save failed (%d)", err);
	}
	bt_gatt_notify(NULL, &pw_svc.attrs[1], &val, 1);
	return 0;
}

int app_set_invert(bool inv)
{
	uint8_t val = inv;
	int err;

	pilot_wire_set_invert(inv);
	LOG_INF("polarity invert %u", val);
	err = settings_save_one("pw/inv", &val, 1);
	if (err) {
		LOG_ERR("invert save failed (%d)", err);
	}
	return err;
}

static const struct bt_data ad_pairing[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_LIMITED | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, PW_SVC_UUID_VAL),
};

/* Non-discoverable: no UUID either, so no host offers to set up the board */
static const struct bt_data ad_normal[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void accept_bond(const struct bt_bond_info *info, void *user_data)
{
	if (bt_le_filter_accept_list_add(&info->addr)) {
		LOG_ERR("accept list full");
	}
}

static void count_conn(struct bt_conn *conn, void *data)
{
	(*(int *)data)++;
}

static void drop_conn(struct bt_conn *conn, void *data)
{
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

/*
 * The accept list cannot change while advertising: rebuild it on every start.
 * The BCM4343A0 stops taking connections after it advertised during a link,
 * so with a link up, wait: .recycled restarts advertising after it.
 */
static void adv_restart(struct k_work *work)
{
	int links = 0;
	int err;

	bt_conn_foreach(BT_CONN_TYPE_LE, count_conn, &links);
	if (links) {
		return;
	}
	bt_le_adv_stop();
	bt_le_filter_accept_list_clear();
	bt_foreach_bond(BT_ID_DEFAULT, accept_bond, NULL);

	if (atomic_get(&pairing)) {
		err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad_pairing, ARRAY_SIZE(ad_pairing), sd,
				      ARRAY_SIZE(sd));
	} else {
		err = bt_le_adv_start(
			BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_FILTER_CONN,
					BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL),
			ad_normal, ARRAY_SIZE(ad_normal), NULL, 0);
	}
	if (err) {
		LOG_ERR("advertising failed (%d)", err);
	}
}
static K_WORK_DEFINE(adv_work, adv_restart);

static void start_adv(void)
{
	k_work_submit(&adv_work);
}

static void blink(struct k_timer *t)
{
	gpio_pin_toggle_dt(&led_blue);
}
static K_TIMER_DEFINE(blink_timer, blink, NULL);

static struct k_work_delayable pairing_end_work;

static void count_bond(const struct bt_bond_info *info, void *user_data)
{
	(*(int *)user_data)++;
}

static void pairing_end(struct k_work *work)
{
	int bonds = 0;

	/* No bond: the accept list would be empty and nobody could connect */
	bt_foreach_bond(BT_ID_DEFAULT, count_bond, &bonds);
	if (!bonds) {
		k_work_reschedule(&pairing_end_work, PAIRING_TIME);
		return;
	}
	if (atomic_cas(&pairing, 1, 0)) {
		k_timer_stop(&blink_timer);
		gpio_pin_set_dt(&led_blue, 0);
		LOG_INF("pairing mode off");
		start_adv();
	}
}
static K_WORK_DELAYABLE_DEFINE(pairing_end_work, pairing_end);

static void pairing_start(void)
{
	atomic_set(&pairing, 1);
	gpio_pin_set_dt(&led_red, 0);
	k_timer_start(&blink_timer, K_NO_WAIT, K_MSEC(BLINK_MS));
	k_work_reschedule(&pairing_end_work, PAIRING_TIME);
	LOG_INF("pairing mode for %d s", CONFIG_BT_LIM_ADV_TIMEOUT);
	/*
	 * One link only (see adv_restart): free it for the new host.
	 * ponytail: a bonded host that reconnects first takes the slot back,
	 * press again. Fix with a second link if the controller ever allows it.
	 */
	bt_conn_foreach(BT_CONN_TYPE_LE, drop_conn, NULL);
	start_adv();
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	LOG_INF("connected (%u)", err);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("disconnected (0x%02x)", reason);
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	LOG_INF("security level %d (err %d)", level, err);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = start_adv,
	.security_changed = security_changed,
};

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	if (!atomic_get(&pairing)) {
		LOG_WRN("pairing rejected: hold the middle button 3 s first");
		return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
	}
	return BT_SECURITY_ERR_SUCCESS;
}

static struct bt_conn_auth_cb auth_cb = {
	.pairing_accept = pairing_accept,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	LOG_INF("paired (bonded %d)", bonded);
	if (bonded) {
		k_work_reschedule(&pairing_end_work, K_NO_WAIT);
	}
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
};

static void long_press(struct k_work *work)
{
	if (gpio_pin_get_dt(&pair_button) == 1) {
		pairing_start();
	}
}
static K_WORK_DELAYABLE_DEFINE(long_press_work, long_press);

static void pair_changed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	if (gpio_pin_get_dt(&pair_button) == 1) {
		k_work_reschedule(&long_press_work, LONG_PRESS);
	} else {
		k_work_cancel_delayable(&long_press_work);
	}
}

/* Red for 1 s, then pairing mode: no bond is left */
static void erase_done(struct k_work *work)
{
	pairing_start();
}
static K_WORK_DELAYABLE_DEFINE(erase_done_work, erase_done);

static void erase(struct k_work *work)
{
	int err;

	if (gpio_pin_get_dt(&erase_button) != 1) {
		return;
	}
	/* Also disconnects the bonded peers */
	err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
	LOG_INF("bonds erased (%d)", err);
	gpio_pin_set_dt(&led_red, 1);
	k_work_reschedule(&erase_done_work, K_SECONDS(1));
}
static K_WORK_DELAYABLE_DEFINE(erase_work, erase);

static void erase_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_work_reschedule(&erase_work, K_MSEC(50));
}

static int button_init(const struct gpio_dt_spec *b, struct gpio_callback *cb,
		       gpio_callback_handler_t handler, gpio_flags_t edge)
{
	int err = gpio_pin_configure_dt(b, GPIO_INPUT);

	if (!err) {
		err = gpio_pin_interrupt_configure_dt(b, edge);
	}
	if (!err) {
		gpio_init_callback(cb, handler, BIT(b->pin));
		err = gpio_add_callback_dt(b, cb);
	}
	return err;
}

int main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (%d)", err);
		return 0;
	}
	/* Bonds, identity and the saved mode */
	settings_load();

	err = pilot_wire_init(mode);
	if (err) {
		LOG_ERR("pilot wire init failed (%d)", err);
		return 0;
	}
	LOG_INF("mode %u", mode);

	bt_conn_auth_cb_register(&auth_cb);
	bt_conn_auth_info_cb_register(&auth_info_cb);

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	if (button_init(&pair_button, &pair_cb, pair_changed, GPIO_INT_EDGE_BOTH)) {
		LOG_ERR("pairing button unavailable");
	}
	if (button_init(&erase_button, &erase_cb, erase_pressed, GPIO_INT_EDGE_TO_ACTIVE)) {
		LOG_ERR("erase button unavailable");
	}

	int bonds = 0;

	bt_foreach_bond(BT_ID_DEFAULT, count_bond, &bonds);
	LOG_INF("%d bond(s), advertising as %s", bonds, CONFIG_BT_DEVICE_NAME);
	if (bonds) {
		start_adv();
	} else {
		pairing_start();
	}
	return 0;
}
