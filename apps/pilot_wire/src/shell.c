/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * "pw" shell command, meant for the mains test over the BLE NUS shell
 * (overlay-test.conf): status and diagnostics, mode, polarity swap.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/bluetooth/conn.h>

#include "pilot_wire.h"

#define MIN_SAMPLES 10
#define FLAT_MV     50 /* below this TP1 difference, no polarity verdict */

static const char *const names[] = {"Confort", "Eco", "Hors-gel", "Arret", "Confort-1",
				    "Confort-2"};
static uint32_t window_start;

static void check_sec(struct bt_conn *conn, void *data)
{
	if (bt_conn_get_security(conn) >= BT_SECURITY_L2) {
		*(bool *)data = true;
	} else {
		/* Bonded hosts encrypt on request: the next try passes */
		bt_conn_set_security(conn, BT_SECURITY_L2);
	}
}

/* The output is on mains: only a bonded, encrypted host may drive it */
static bool encrypted(const struct shell *sh)
{
	bool ok = false;

	bt_conn_foreach(BT_CONN_TYPE_LE, check_sec, &ok);
	if (!ok) {
		shell_error(sh, "link not encrypted: encryption requested, retry"
				" (bonded host only)");
	}
	return ok;
}

static uint32_t avg(uint32_t sum, uint32_t n)
{
	return n ? sum / n : 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct pw_stats s;
	uint32_t now = k_uptime_get_32();
	uint32_t ms = now - window_start;

	pilot_wire_stats(&s, true);
	window_start = now;

	shell_print(sh, "mode %s, invert %d, LED %s, window %u.%01u s",
		    s.mode < ARRAY_SIZE(names) ? names[s.mode] : "?",
		    s.invert, s.led ? "on" : "off", ms / 1000, ms % 1000 / 100);
	shell_print(sh, "zero-cross %s: %u edges (%u.%01u Hz), %u glitches",
		    s.zc_ok ? "ok" : "LOST", s.edges, ms ? s.edges * 500 / ms : 0,
		    ms ? s.edges * 5000 / ms % 10 : 0,
		    s.glitches);
	if (s.high_us + s.low_us) {
		shell_print(sh, "half-wave %u..%u us, PC7 high %u%% of the time",
			    s.min_us, s.max_us,
			    (uint32_t)((uint64_t)s.high_us * 100 / (s.high_us + s.low_us)));
	}
	shell_print(sh, "half-waves enabled: +%u -%u (PC7 high %u, low %u)", s.on[PW_HALF_POS],
		    s.on[PW_HALF_NEG], s.on_level[1], s.on_level[0]);

	if (!s.adc) {
		shell_print(sh, "ADC: not available");
		return 0;
	}
	shell_print(sh, "PC7  LED  samples  TP1 mains mV  TP3 output mV");
	for (int l = 0; l < 2; l++) {
		for (int a = 0; a < 2; a++) {
			shell_print(sh, "%-4s %-4s %7u  %12u  %13u", l ? "high" : "low",
				    a ? "on" : "off", s.n[l][a], avg(s.mains_mv[l][a], s.n[l][a]),
				    avg(s.fb_mv[l][a], s.n[l][a]));
		}
	}

	uint32_t n_hi = s.n[1][0] + s.n[1][1];
	uint32_t n_lo = s.n[0][0] + s.n[0][1];
	uint32_t hi = avg(s.mains_mv[1][0] + s.mains_mv[1][1], n_hi);
	uint32_t lo = avg(s.mains_mv[0][0] + s.mains_mv[0][1], n_lo);
	int vis; /* PC7 level where neutral is above GND: TP3 can see the output there */

	if (n_hi < MIN_SAMPLES || n_lo < MIN_SAMPLES) {
		shell_print(sh, "verdict: not enough samples, wait a few seconds");
		return 0;
	}
	if (hi > lo + FLAT_MV) {
		vis = 1;
		shell_print(sh, "verdict: PC7 high = neutral above phase = NEGATIVE half on P2"
				" -> want invert 1%s", s.invert ? " (ok)" : ": run 'pw invert 1'");
	} else if (lo > hi + FLAT_MV) {
		vis = 0;
		shell_print(sh, "verdict: PC7 high = phase above neutral = POSITIVE half on P2"
				" -> want invert 0%s", s.invert ? ": run 'pw invert 0'" : " (ok)");
	} else {
		shell_print(sh, "verdict: TP1 flat (no mains, or simulated zero-cross)");
		return 0;
	}
	for (int a = 0; a < 2; a++) {
		if (s.n[vis][a] >= MIN_SAMPLES) {
			shell_print(sh, "output, LED %s: TP3 %u mV (about 0 = triac conducts)",
				    a ? "on" : "off", avg(s.fb_mv[vis][a], s.n[vis][a]));
		}
	}
	return 0;
}

static int cmd_mode(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long m = strtoul(argv[1], &end, 10);

	if (*end || m >= PW_MODE_COUNT) {
		shell_error(sh, "mode: 0 Confort, 1 Eco, 2 Hors-gel, 3 Arret, 4 Confort-1, "
				"5 Confort-2");
		return -EINVAL;
	}
	if (!encrypted(sh)) {
		return -EACCES;
	}
	app_set_mode(m);
	/* Fresh stats for the new mode */
	struct pw_stats s;

	pilot_wire_stats(&s, true);
	window_start = k_uptime_get_32();
	shell_print(sh, "mode %s", names[m]);
	return 0;
}

static int cmd_invert(const struct shell *sh, size_t argc, char **argv)
{
	if (strcmp(argv[1], "0") && strcmp(argv[1], "1")) {
		shell_error(sh, "invert: 0 or 1");
		return -EINVAL;
	}
	if (!encrypted(sh)) {
		return -EACCES;
	}
	bool inv = argv[1][0] == '1';
	int err = app_set_invert(inv);

	shell_print(sh, "invert %d: PC7 high is the %s half-wave", inv,
		    inv ? "negative" : "positive");
	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(pw_cmds,
	SHELL_CMD_ARG(status, NULL, "Diagnostics since the last call, then reset", cmd_status,
		      1, 0),
	SHELL_CMD_ARG(mode, NULL, "<0-5> Set the pilot-wire order", cmd_mode, 2, 0),
	SHELL_CMD_ARG(invert, NULL, "<0|1> Swap the half-wave polarity", cmd_invert, 2, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(pw, &pw_cmds, "Pilot wire", NULL);
