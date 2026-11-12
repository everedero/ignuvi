/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pilot-wire output. The MOC3063 fires at the next zero crossing after its
 * LED turns on, and the triac stops at the next crossing after the LED turns
 * off. So the LED is set mid half-wave to "is the next half-wave wanted".
 *
 * Diagnostics for the mains test (no probe possible, GND is the phase): the
 * zero-cross timing, and at mid half-wave the TP1 (neutral against phase) and
 * TP3 (output) ADC levels, sorted by PC7 level and by the LED state.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "pilot_wire.h"

LOG_MODULE_REGISTER(pilot_wire, LOG_LEVEL_INF);

#define ZC_DELAY_MS   5  /* mid half-wave at 50 Hz */
#define ZC_LOST_MS    40 /* two full mains periods */
#define ZC_GLITCH_US  8000
#define ZC_MAX_US     20000 /* longer: a gap, not a half-wave */
#define TICK_MS       100
#define STATS_TICKS   100 /* 10 s */

#define HAS_ADC (IS_ENABLED(CONFIG_ADC) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels))

static const struct gpio_dt_spec opto = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), opto_gpios);

/* Uptimes in 32-bit ms, so the ISRs read them whole */
static atomic_t cur_mode;
static atomic_t mode_start;
static atomic_t last_zc;

static enum pw_half cur_half;
static int cur_level;  /* raw PC7 level of the running half-wave */
static bool cur_armed; /* LED state set for the running half-wave */
static bool led;
static uint32_t last_cyc;

static atomic_t count[2]; /* for the 10 s log */
static struct k_spinlock lock;
static struct pw_stats st;

static uint32_t cycle_ms(void)
{
	return (k_uptime_get_32() - (uint32_t)atomic_get(&mode_start)) % PW_CYCLE_MS;
}

static void set_led(bool on)
{
	led = on;
	gpio_pin_set_dt(&opto, on);
}

#if HAS_ADC
static const struct adc_dt_spec adc_mains = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), mains);
static const struct adc_dt_spec adc_fb = ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), feedback);
static K_SEM_DEFINE(sample_sem, 0, 1);
static bool adc_ok;

static int read_mv(const struct adc_dt_spec *spec, int32_t *mv)
{
	int16_t raw;
	struct adc_sequence seq = {.buffer = &raw, .buffer_size = sizeof(raw)};
	int err = adc_sequence_init_dt(spec, &seq);

	if (!err) {
		err = adc_read_dt(spec, &seq);
	}
	if (!err) {
		*mv = raw;
		err = adc_raw_to_millivolts_dt(spec, mv);
	}
	return err;
}

/* adc_read() blocks: sample from a thread, woken at mid half-wave */
static void sampler(void *a, void *b, void *c)
{
	while (true) {
		int32_t mains, fb;

		k_sem_take(&sample_sem, K_FOREVER);
		k_spinlock_key_t key = k_spin_lock(&lock);
		int level = cur_level;
		bool armed = cur_armed;

		k_spin_unlock(&lock, key);

		if (read_mv(&adc_mains, &mains) || read_mv(&adc_fb, &fb)) {
			adc_ok = false;
			continue;
		}
		key = k_spin_lock(&lock);
		st.n[level][armed]++;
		st.mains_mv[level][armed] += MAX(mains, 0);
		st.fb_mv[level][armed] += MAX(fb, 0);
		k_spin_unlock(&lock, key);
	}
}
K_THREAD_DEFINE(pw_sampler, 1024, sampler, NULL, NULL, NULL, K_PRIO_COOP(5), 0, 0);

static int adc_init(void)
{
	int err = adc_channel_setup_dt(&adc_mains);

	if (!err) {
		err = adc_channel_setup_dt(&adc_fb);
	}
	adc_ok = !err;
	return err;
}
#else
static const bool adc_ok;

static int adc_init(void)
{
	return 0;
}
#endif

/* Mid half-wave: arm (or not) the optotriac for the next one */
static void mid_half(struct k_timer *t)
{
	enum pw_half next = cur_half == PW_HALF_POS ? PW_HALF_NEG : PW_HALF_POS;
	bool on = pw_conduct(atomic_get(&cur_mode), next, cycle_ms());
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Still the running half-wave: sample it before re-arming */
	cur_armed = led;
	if (on) {
		st.on[next]++;
		st.on_level[!cur_level]++;
		atomic_inc(&count[next]);
	}
	k_spin_unlock(&lock, key);

	set_led(on);
#if HAS_ADC
	k_sem_give(&sample_sem);
#endif
}
static K_TIMER_DEFINE(mid_timer, mid_half, NULL);

/* `level`: raw PC7 level after the edge */
static void zero_cross(int level)
{
	uint32_t now = k_cycle_get_32();
	uint32_t us = k_cyc_to_us_floor32(now - last_cyc);
	k_spinlock_key_t key = k_spin_lock(&lock);

	last_cyc = now;
	st.edges++;
	if (us < ZC_GLITCH_US) {
		st.glitches++;
	} else if (us < ZC_MAX_US) {
		st.min_us = MIN(st.min_us, us);
		st.max_us = MAX(st.max_us, us);
		/* The half-wave that just ended had the other level */
		if (level) {
			st.low_us += us;
		} else {
			st.high_us += us;
		}
	}
	cur_level = level;
	k_spin_unlock(&lock, key);

	/* Verified on mains: PC7 high is the negative half-wave on P2 */
	cur_half = (level != 0) != IS_ENABLED(CONFIG_PILOT_WIRE_INVERT_POLARITY) ? PW_HALF_NEG
										 : PW_HALF_POS;
	atomic_set(&last_zc, k_uptime_get_32());
	k_timer_start(&mid_timer, K_MSEC(ZC_DELAY_MS), K_NO_WAIT);
}

#ifdef CONFIG_PILOT_WIRE_SIM_ZC
static void sim_zc(struct k_timer *t)
{
	static int level;

	level = !level;
	zero_cross(level);
}
static K_TIMER_DEFINE(sim_timer, sim_zc, NULL);
#else
static const struct gpio_dt_spec zc = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), zero_cross_gpios);
static struct gpio_callback zc_cb;

static void zc_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	zero_cross(gpio_pin_get_dt(&zc));
}
#endif

static bool zc_present(void)
{
	return k_uptime_get_32() - (uint32_t)atomic_get(&last_zc) <= ZC_LOST_MS;
}

/* No mains (bench): only full-wave orders can be expressed, as LED on */
static void tick(struct k_work *work)
{
	static int ticks;
	static int was_present = -1;
	bool present = zc_present();

	if (present != was_present) {
		was_present = present;
		if (present) {
			LOG_INF("zero-cross present");
		} else {
			LOG_WRN("no zero-cross: half-wave orders fall back to Confort");
		}
	}
	if (!present) {
		enum pw_mode mode = atomic_get(&cur_mode);
		uint32_t t = cycle_ms();

		set_led(pw_conduct(mode, PW_HALF_POS, t) && pw_conduct(mode, PW_HALF_NEG, t));
	}

	if (++ticks == STATS_TICKS) {
		ticks = 0;
		LOG_INF("mode %ld, half-waves on: +%ld -%ld", atomic_get(&cur_mode),
			atomic_set(&count[PW_HALF_POS], 0), atomic_set(&count[PW_HALF_NEG], 0));
	}
	k_work_schedule(k_work_delayable_from_work(work), K_MSEC(TICK_MS));
}
static K_WORK_DELAYABLE_DEFINE(tick_work, tick);

void pilot_wire_set(enum pw_mode mode)
{
	atomic_set(&mode_start, k_uptime_get_32());
	atomic_set(&cur_mode, mode);
}

void pilot_wire_stats(struct pw_stats *out, bool reset)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*out = st;
	if (reset) {
		st = (struct pw_stats){.min_us = UINT32_MAX};
	}
	k_spin_unlock(&lock, key);

	out->mode = atomic_get(&cur_mode);
	out->adc = adc_ok;
	out->zc_ok = zc_present();
	out->led = led;
	out->invert = IS_ENABLED(CONFIG_PILOT_WIRE_INVERT_POLARITY);
}

int pilot_wire_init(enum pw_mode mode)
{
	int err;

	st.min_us = UINT32_MAX;
	pilot_wire_set(mode);
	atomic_set(&last_zc, k_uptime_get_32() - 1000U);

	err = gpio_pin_configure_dt(&opto, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
	if (adc_init()) {
		LOG_WRN("ADC unavailable: no mains levels in the stats");
	}

#ifdef CONFIG_PILOT_WIRE_SIM_ZC
	LOG_WRN("simulated zero-cross");
	k_timer_start(&sim_timer, K_MSEC(10), K_MSEC(10));
#else
	err = gpio_pin_configure_dt(&zc, GPIO_INPUT);
	if (err) {
		return err;
	}
	gpio_init_callback(&zc_cb, zc_isr, BIT(zc.pin));
	err = gpio_add_callback_dt(&zc, &zc_cb);
	if (err) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&zc, GPIO_INT_EDGE_BOTH);
	if (err) {
		return err;
	}
#endif

	/* Give the zero-cross time to show up before calling it lost */
	k_work_schedule(&tick_work, K_MSEC(2 * ZC_LOST_MS));
	return 0;
}
