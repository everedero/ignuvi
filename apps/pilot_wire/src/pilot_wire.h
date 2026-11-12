/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILOT_WIRE_H_
#define PILOT_WIRE_H_

#include "pilot_wire_logic.h"

/* Diagnostics since the last pilot_wire_stats(reset = true) */
struct pw_stats {
	uint32_t edges;    /* zero-cross edges */
	uint32_t glitches; /* edges less than 8 ms after the previous one */
	uint32_t min_us;   /* shortest and longest half-wave, glitches excluded */
	uint32_t max_us;
	uint32_t high_us; /* total time PC7 spent high / low */
	uint32_t low_us;
	uint32_t on[2];       /* half-waves enabled, indexed by enum pw_half */
	uint32_t on_level[2]; /* the same, indexed by raw PC7 level */
	/*
	 * Mid half-wave ADC samples, indexed [raw PC7 level][LED armed for
	 * that half-wave]. Sums in mV.
	 */
	uint32_t n[2][2];
	uint32_t mains_mv[2][2];
	uint32_t fb_mv[2][2];
	uint8_t mode;
	bool adc;    /* ADC present and read without error */
	bool zc_ok;  /* zero-cross seen in the last 40 ms */
	bool led;    /* current optotriac LED state */
	bool invert; /* CONFIG_PILOT_WIRE_INVERT_POLARITY */
};

int pilot_wire_init(enum pw_mode mode);
void pilot_wire_set(enum pw_mode mode);
void pilot_wire_stats(struct pw_stats *out, bool reset);

/* In main.c: set, save and notify, as a Bluetooth write would */
int app_set_mode(uint8_t mode);

#endif /* PILOT_WIRE_H_ */
