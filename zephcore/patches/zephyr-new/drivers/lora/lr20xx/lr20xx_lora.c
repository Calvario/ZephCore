/*
 * SPDX-License-Identifier: MIT
 * LR20xx Zephyr LoRa driver
 *
 * Implements the standard Zephyr lora_driver_api using the Semtech lr20xx_driver
 * SDK. All SPI access, DIO1 IRQ handling, and radio state management is internal.
 */

#define DT_DRV_COMPAT semtech_lr2021

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

#include "lr20xx_lora.h"
#include "lr20xx_hal_zephyr.h"
#include "lr20xx_radio_common.h"
#include "lr20xx_radio_common_types.h"
#include "lr20xx_radio_lora.h"
#include "lr20xx_radio_lora_types.h"
#include "lr20xx_radio_fifo.h"
#include "lr20xx_system.h"
#include "lr20xx_system_types.h"
#include "lr20xx_workarounds.h"
#include "lr20xx_regmem.h"

LOG_MODULE_REGISTER(lr20xx_lora, CONFIG_LORA_LOG_LEVEL);

/* Dedicated DIO1 work queue — keeps LoRa interrupt processing off the
 * system work queue so USB/BLE/timer work items cannot delay packet RX. */
#define LR20XX_DIO1_WQ_STACK_SIZE 2560
K_THREAD_STACK_DEFINE(lr20xx_dio1_wq_stack, LR20XX_DIO1_WQ_STACK_SIZE);

/* ── Driver data structures ─────────────────────────────────────────── */

struct lr20xx_config {
	struct spi_dt_spec bus;
	struct gpio_dt_spec reset;
	struct gpio_dt_spec busy;
	struct gpio_dt_spec dio1;
	uint16_t tcxo_voltage_mv;
	uint32_t tcxo_startup_delay_ms;
	bool rx_boosted;
	/* Chip-side DIO wired to the MCU IRQ line (5..11) */
	uint8_t irq_dio;
	/* RF switch DIO bitmasks (bit 0 = DIO5, bit 1 = DIO6, ...) */
	uint8_t rfswitch_enable;
	uint8_t rfswitch_standby;
	uint8_t rfswitch_rx;
	uint8_t rfswitch_tx;
	uint8_t rfswitch_tx_hp;
	/* PA config */
	uint8_t pa_hp_sel;    /* maps to pa_lf_slices in LR20xx */
	uint8_t pa_duty_cycle; /* maps to pa_lf_duty_cycle in LR20xx */
};

struct lr20xx_data {
	const struct device *dev;
	struct lr20xx_hal_context hal_ctx;
	struct k_mutex spi_mutex;

	/* Cached modem config from lora_config() */
	struct lora_modem_config modem_cfg;
	bool configured;

	/* Async RX state */
	lora_recv_cb async_rx_cb;
	void *async_rx_user_data;

	/* Async TX state */
	struct k_poll_signal *tx_signal;

	/* DIO1 work — runs on dedicated queue, not system work queue */
	struct k_work dio1_work;
	struct k_work_q dio1_wq;

	/* Radio state */
	volatile bool tx_active;
	volatile bool in_rx_mode;

	/* Extension features */
	bool rx_duty_cycle_enabled;
	bool rx_boost_enabled;
	bool rx_boost_applied;

	/* Stored duty-cycle timing from recv_duty_cycle() — re-arm paths
	 * must reuse these exact values, never recompute: the window sizing
	 * (detection budget, datasheet completion rule, wake transition) is
	 * owned by the adapter layer. */
	uint32_t dc_rx_ms;
	uint32_t dc_sleep_ms;

	/* CAD state */
	lora_cad_cb cad_cb;
	void *cad_user_data;
	struct k_sem cad_sem;
	int cad_result;	/* 0=free, 1=busy, <0=error */
	bool cad_active;
	/* Adaptive-CAD: signed offset applied to the per-SF base detPeak on
	 * every LBT CAD; cad_probe_peak overrides for one calibration probe. */
	int8_t cad_peak_offset;
	uint8_t cad_probe_peak;

	/* Deferred hardware init */
	bool hw_initialized;

	/* DIO1 stuck-HIGH detection */
	int dio1_stuck_count;
	bool tcxo_disabled;   /* set when the TCXO fallback has already fired */

	/* RX data buffer */
	uint8_t rx_buf[256];
};

/* ── Debug: dump full chip state (log builds only) ──────────────────── */

#if IS_ENABLED(CONFIG_LOG)
static void dump_chip_state(void *ctx, struct lr20xx_hal_context *hal,
			    const char *label)
{
	lr20xx_system_stat1_t s1 = {0};
	lr20xx_system_stat2_t s2 = {0};
	lr20xx_system_irq_mask_t irq = 0;
	lr20xx_system_errors_t err = 0;

	lr20xx_system_get_status(ctx, &s1, &s2, &irq);
	lr20xx_system_get_errors(ctx, &err);

	int busy = gpio_pin_get_dt(&hal->busy);
	int dio9 = gpio_pin_get_dt(&hal->dio1);

	LOG_INF("[%s] cmd=%d mode=%d err=0x%04x irq=0x%08x BUSY=%d DIO9=%d",
		label, s1.command_status, s2.chip_mode, err, irq, busy, dio9);
}
#define DUMP_CHIP_STATE(ctx, hal, label) dump_chip_state(ctx, hal, label)
#else
#define DUMP_CHIP_STATE(ctx, hal, label) do { } while (0)
#endif /* IS_ENABLED(CONFIG_LOG) */

/* ── Helpers ────────────────────────────────────────────────────────── */

static lr20xx_radio_lora_bw_t bw_enum_to_lr20xx(enum lora_signal_bandwidth bw)
{
	switch (bw) {
	case BW_31_KHZ:  return LR20XX_RADIO_LORA_BW_31;
	case BW_41_KHZ:  return LR20XX_RADIO_LORA_BW_41;
	case BW_62_KHZ:  return LR20XX_RADIO_LORA_BW_62;
	case BW_125_KHZ: return LR20XX_RADIO_LORA_BW_125;
	case BW_250_KHZ: return LR20XX_RADIO_LORA_BW_250;
	case BW_500_KHZ: return LR20XX_RADIO_LORA_BW_500;
	default:         return LR20XX_RADIO_LORA_BW_125;
	}
}

static lr20xx_radio_lora_cr_t cr_enum_to_lr20xx(enum lora_coding_rate cr)
{
	switch (cr) {
	case CR_4_5: return LR20XX_RADIO_LORA_CR_4_5;
	case CR_4_6: return LR20XX_RADIO_LORA_CR_4_6;
	case CR_4_7: return LR20XX_RADIO_LORA_CR_4_7;
	case CR_4_8: return LR20XX_RADIO_LORA_CR_4_8;
	default:     return LR20XX_RADIO_LORA_CR_4_8;
	}
}

/* SetTcxoMode's start_time is the deadline by which the 32 MHz oscillator must
 * be detected, counted in 32 MHz clock periods (datasheet S6.11.3) — NOT in
 * 32.768 kHz RTC ticks, which is what every other timeout on this chip uses.
 * Getting that wrong turns a 5 ms allowance into 5 us, no TCXO starts that
 * fast, and the chip raises HF_XOSC_START_ERR exactly as documented. */
static inline uint32_t tcxo_start_time_periods(uint32_t delay_ms)
{
	return delay_ms * 32000U;   /* 32000 periods per ms at 32 MHz */
}

static lr20xx_system_tcxo_supply_voltage_t get_tcxo_voltage(uint16_t mv)
{
	if (mv >= 3300) return LR20XX_SYSTEM_TCXO_CTRL_3_3V;
	if (mv >= 3000) return LR20XX_SYSTEM_TCXO_CTRL_3_0V;
	if (mv >= 2700) return LR20XX_SYSTEM_TCXO_CTRL_2_7V;
	if (mv >= 2400) return LR20XX_SYSTEM_TCXO_CTRL_2_4V;
	if (mv >= 2200) return LR20XX_SYSTEM_TCXO_CTRL_2_2V;
	if (mv >= 1800) return LR20XX_SYSTEM_TCXO_CTRL_1_8V;
	return LR20XX_SYSTEM_TCXO_CTRL_1_6V;
}

/* Get kHz value from Zephyr BW enum — used for LDRO/PPM calculation */
static float bw_enum_to_khz(enum lora_signal_bandwidth bw)
{
	switch (bw) {
	case BW_7_KHZ:   return 7.81f;
	case BW_10_KHZ:  return 10.42f;
	case BW_15_KHZ:  return 15.63f;
	case BW_20_KHZ:  return 20.83f;
	case BW_31_KHZ:  return 31.25f;
	case BW_41_KHZ:  return 41.67f;
	case BW_62_KHZ:  return 62.5f;
	case BW_125_KHZ: return 125.0f;
	case BW_250_KHZ: return 250.0f;
	case BW_500_KHZ: return 500.0f;
	default:         return 125.0f;
	}
}

/* ── IRQ DIO ────────────────────────────────────────────────────────── */

static inline lr20xx_system_dio_t lr20xx_irq_dio(const struct lr20xx_config *cfg)
{
	return (lr20xx_system_dio_t)cfg->irq_dio;
}

/* DIO5 can only be pulled up; every other DIO gets a pull-down. */
static inline lr20xx_system_dio_drive_t
lr20xx_irq_dio_pull(const struct lr20xx_config *cfg)
{
	return cfg->irq_dio == LR20XX_SYSTEM_DIO_5
		       ? LR20XX_SYSTEM_DIO_DRIVE_PULL_UP
		       : LR20XX_SYSTEM_DIO_DRIVE_PULL_DOWN;
}

/* ── Configure RF switch DIOs ───────────────────────────────────────── */

static void lr20xx_configure_rfswitch(void *ctx, const struct lr20xx_config *cfg)
{
	/* LR20xx RF switch uses per-DIO configuration.
	 * DIO5..DIO8 map to enable bitmask bits 0..3.
	 * For each enabled DIO, compute which operational modes
	 * should drive it HIGH by looking at the per-mode bitmasks. */
	for (int i = 0; i < 4; i++) {
		if (!(cfg->rfswitch_enable & BIT(i))) {
			continue;
		}

		lr20xx_system_dio_t dio = (lr20xx_system_dio_t)(LR20XX_SYSTEM_DIO_5 + i);

		/* Set this DIO function to RF switch control */
		lr20xx_system_set_dio_function(ctx, dio,
					       LR20XX_SYSTEM_DIO_FUNC_RF_SWITCH,
					       LR20XX_SYSTEM_DIO_DRIVE_NONE);

		/* Build the per-DIO mode bitmask:
		 * which operational modes drive this DIO HIGH */
		lr20xx_system_dio_rf_switch_cfg_t sw_cfg = 0;

		if (cfg->rfswitch_standby & BIT(i)) {
			sw_cfg |= LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_STANDBY;
		}
		if (cfg->rfswitch_rx & BIT(i)) {
			sw_cfg |= LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_RX_LF |
				  LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_RX_HF;
		}
		if (cfg->rfswitch_tx & BIT(i)) {
			sw_cfg |= LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_LF;
		}
		if (cfg->rfswitch_tx_hp & BIT(i)) {
			sw_cfg |= LR20XX_SYSTEM_DIO_RF_SWITCH_WHEN_TX_HF;
		}

		lr20xx_system_set_dio_rf_switch_cfg(ctx, dio, sw_cfg);
	}
}

/* ── PA power lookup table (LF / sub-GHz) ─────────────────────────────
 *
 * Mirrors RadioLib's paOptTableLf (known-good on real LR2021 silicon).
 * Each row is { pa_lf_duty_cycle, pa_lf_slices, pa_val } where pa_val is the
 * value passed to SetTxParams.
 *
 * IMPORTANT: pa_val is a PA *calibration* value, NOT power in dBm or half-dBm.
 * Our previous table mis-modeled it as 0.5dB steps (e.g. 44 for +22dBm); the
 * chip rejected those out-of-range values (PERR / CMD_ERROR) and refused TX.
 * RadioLib's LF range is -9..+22 dBm, indexed as (power_dbm + 9).
 */
struct lr20xx_pa_pwr_entry {
	uint8_t pa_duty_cycle;
	uint8_t pa_lf_slices;
	int8_t  pa_val;
};

#define LR20XX_LF_MIN_PWR (-9)
#define LR20XX_LF_MAX_PWR 22

static const struct lr20xx_pa_pwr_entry pa_lf_table[] = {
	{ 1, 1,  8 }, /*  -9 dBm */
	{ 2, 2,  1 }, /*  -8 dBm */
	{ 2, 2,  3 }, /*  -7 dBm */
	{ 2, 2,  5 }, /*  -6 dBm */
	{ 1, 2, 13 }, /*  -5 dBm */
	{ 2, 1, 13 }, /*  -4 dBm */
	{ 2, 2, 11 }, /*  -3 dBm */
	{ 2, 2, 13 }, /*  -2 dBm */
	{ 3, 1, 12 }, /*  -1 dBm */
	{ 1, 1, 18 }, /*   0 dBm */
	{ 1, 1, 20 }, /*   1 dBm */
	{ 1, 1, 23 }, /*   2 dBm */
	{ 1, 1, 27 }, /*   3 dBm */
	{ 1, 1, 33 }, /*   4 dBm */
	{ 1, 2, 26 }, /*   5 dBm */
	{ 1, 2, 31 }, /*   6 dBm */
	{ 1, 3, 27 }, /*   7 dBm */
	{ 1, 1, 37 }, /*   8 dBm */
	{ 1, 2, 40 }, /*   9 dBm */
	{ 2, 1, 38 }, /*  10 dBm */
	{ 2, 2, 39 }, /*  11 dBm */
	{ 2, 4, 40 }, /*  12 dBm */
	{ 2, 7, 41 }, /*  13 dBm */
	{ 3, 2, 39 }, /*  14 dBm */
	{ 3, 3, 39 }, /*  15 dBm */
	{ 3, 6, 38 }, /*  16 dBm */
	{ 4, 3, 37 }, /*  17 dBm */
	{ 4, 5, 37 }, /*  18 dBm */
	{ 4, 7, 38 }, /*  19 dBm */
	{ 5, 3, 37 }, /*  20 dBm */
	{ 5, 6, 37 }, /*  21 dBm */
	{ 6, 7, 35 }, /*  22 dBm */
};

/* PA_HF_DUTY_CYCLE "unused" marker (RADIOLIB_LR2021_PA_HF_DUTY_CYCLE_UNUSED) */
#define LR20XX_PA_HF_DUTY_CYCLE_UNUSED 16

static void lr20xx_get_pa_cfg_for_power(int8_t power_dbm,
					lr20xx_radio_common_pa_cfg_t *pa,
					int8_t *pa_val_out)
{
	if (power_dbm < LR20XX_LF_MIN_PWR) {
		power_dbm = LR20XX_LF_MIN_PWR;
	}
	if (power_dbm > LR20XX_LF_MAX_PWR) {
		power_dbm = LR20XX_LF_MAX_PWR;
	}

	int idx = power_dbm - LR20XX_LF_MIN_PWR;
	const struct lr20xx_pa_pwr_entry *e = &pa_lf_table[idx];

	pa->pa_sel           = LR20XX_RADIO_COMMON_PA_SEL_LF;
	pa->pa_lf_mode       = LR20XX_RADIO_COMMON_PA_LF_MODE_FSM;
	pa->pa_lf_duty_cycle = e->pa_duty_cycle;
	pa->pa_lf_slices     = e->pa_lf_slices;
	pa->pa_hf_duty_cycle = LR20XX_PA_HF_DUTY_CYCLE_UNUSED;

	*pa_val_out = e->pa_val;
}

static lr20xx_status_t lr20xx_calibrate_front_end(void *ctx, uint32_t freq_hz);

/* ── Hardware reset (BUSY stuck recovery) ───────────────────────────── */

static void lr20xx_hardware_reset(struct lr20xx_data *data,
				  const struct lr20xx_config *cfg)
{
	void *ctx = &data->hal_ctx;

	LOG_WRN("LR2021 hardware reset (BUSY stuck recovery)");

	lr20xx_hal_reset(ctx);

	/* SIMO workaround skipped — LDO mode (see DS §22.6) */

	if (cfg->tcxo_voltage_mv > 0) {
		/* Timeout in RTC ticks (30.52 µs/tick) */
		lr20xx_system_set_tcxo_mode(ctx,
					    get_tcxo_voltage(cfg->tcxo_voltage_mv),
					    tcxo_start_time_periods(cfg->tcxo_startup_delay_ms));
	}

	/* LDO mode — no cfg_lfclk, no set_reg_mode, no DCDC workarounds */

	lr20xx_configure_rfswitch(ctx, cfg);

	lr20xx_system_set_dio_function(ctx, lr20xx_irq_dio(cfg),
				       LR20XX_SYSTEM_DIO_FUNC_IRQ,
				       lr20xx_irq_dio_pull(cfg));

	lr20xx_radio_common_set_rx_tx_fallback_mode(ctx,
						    LR20XX_RADIO_FALLBACK_STDBY_RC);

	lr20xx_radio_common_set_pkt_type(ctx, LR20XX_RADIO_COMMON_PKT_TYPE_LORA);

	lr20xx_system_clear_errors(ctx);
	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);

	lr20xx_system_calibrate(ctx, 0x6F);
	k_msleep(5);

	/* Front-end calibration — single LF frequency (RadioLib approach) */
	lr20xx_calibrate_front_end(ctx, 868000000);

	data->rx_boost_applied = false;

	lr20xx_hal_enable_dio1_irq(&data->hal_ctx);

	LOG_WRN("LR2021 recovered from hardware reset");
}

/* ── Front-end calibration ──────────────────────────────────────────── */

/* Front-end calibration can legitimately fail on RSSI saturation from a nearby
 * interferer, and repeating it is the documented cure — RadioLib retries up to
 * 10 times for exactly this, and gives up immediately on any other error since
 * repeating those cannot help. Getting this wrong is expensive: an uncalibrated
 * front end makes the chip reject every set_rx with RXFREQ_NO_FRONT_END_CALIB,
 * which then looks like a broken RX path rather than a failed calibration.
 */
#define LR20XX_MAX_CAL_ATTEMPTS 10

static lr20xx_status_t lr20xx_calibrate_front_end(void *ctx, uint32_t freq_hz)
{
	/* All three frequency slots are sent, the unused two as 0, so CalibFE
	 * gets its full 8-byte form. The datasheet allows both "not providing"
	 * the extra slots and "setting other calibration frequencies to 0", but
	 * a short 4-byte command is the one thing that lines up with the
	 * CMD_PERR observed across exactly this call, with a clean error word
	 * and the chip in STBY_RC (so not a mode violation). Zeroed slots are
	 * documented as no-ops. */
	lr20xx_radio_common_front_end_calibration_value_t fe_cal[3] = {
		{ .rx_path = LR20XX_RADIO_COMMON_RX_PATH_LF,
		  .frequency_in_hertz = freq_hz },
		{ .rx_path = LR20XX_RADIO_COMMON_RX_PATH_LF,
		  .frequency_in_hertz = 0 },
		{ .rx_path = LR20XX_RADIO_COMMON_RX_PATH_LF,
		  .frequency_in_hertz = 0 },
	};

	for (int i = 0; i < LR20XX_MAX_CAL_ATTEMPTS; i++) {
		lr20xx_system_errors_t errs = 0;
		lr20xx_status_t rc;

		lr20xx_system_clear_errors(ctx);

		rc = lr20xx_radio_common_calibrate_front_end_helper(ctx, fe_cal, 3);
		lr20xx_system_get_errors(ctx, &errs);

		if (rc == LR20XX_STATUS_OK &&
		    !(errs & LR20XX_SYSTEM_ERRORS_SRC_SATURATION_CALIB_MASK)) {
			LOG_DBG("FE cal(%u Hz) ok on attempt %d (errors=0x%04x)",
				freq_hz, i + 1, errs);
			return LR20XX_STATUS_OK;
		}

		if (!(errs & LR20XX_SYSTEM_ERRORS_SRC_SATURATION_CALIB_MASK)) {
			LOG_ERR("FE cal(%u Hz) failed: rc=%d errors=0x%04x "
				"(not saturation — retrying will not help)",
				freq_hz, rc, errs);
			return (rc != LR20XX_STATUS_OK) ? rc : LR20XX_STATUS_ERROR;
		}

		LOG_WRN("FE cal(%u Hz) RSSI saturation (errors=0x%04x), "
			"attempt %d/%d", freq_hz, errs, i + 1,
			LR20XX_MAX_CAL_ATTEMPTS);
		k_msleep(5);
	}

	LOG_ERR("FE cal(%u Hz) gave up after %d attempts — RX will be refused",
		freq_hz, LR20XX_MAX_CAL_ATTEMPTS);
	return LR20XX_STATUS_ERROR;
}

/* ── Apply modem configuration ──────────────────────────────────────── */

/* Bring-up only: read the chip's own command status and name the command that
 * failed. Return codes from the SDK reflect the SPI write, not whether the chip
 * accepted the command, so a rejection is otherwise invisible until it shows up
 * as a CmdError IRQ several commands later. */
#if IS_ENABLED(CONFIG_LOG)
static void lr20xx_check_cmd(void *ctx, const char *what)
{
	lr20xx_system_stat1_t s1 = {0};

	if (lr20xx_system_get_status(ctx, &s1, NULL, NULL) != LR20XX_STATUS_OK) {
		return;
	}
	/* 2 = CMD_OK, 3 = CMD_DAT (successful read) */
	if (s1.command_status != 2 && s1.command_status != 3) {
		LOG_ERR("command REJECTED after %s: cmd=%d", what,
			s1.command_status);
	}
}
#define CHECK_CMD(ctx, what) lr20xx_check_cmd((ctx), (what))
#else
#define CHECK_CMD(ctx, what) do { } while (0)
#endif

static void lr20xx_apply_modem_config(struct lr20xx_data *data,
				      const struct lr20xx_config *cfg,
				      bool tx_mode)
{
	void *ctx = &data->hal_ctx;
	struct lora_modem_config *mc = &data->modem_cfg;
	lr20xx_status_t rc;

	/* Half of what follows is mode-restricted, and this runs on the TX path
	 * while the chip may still be in RX:
	 *   SetPacketType — "only works when the chip is in Standby RC, Standby
	 *                    Xosc, or Fs mode", and must come first in a setup
	 *   CalibFE       — "does not work if device is in Rx or Tx mode"
	 * Both answer CMD_FAIL from the wrong mode, which surfaces only as a
	 * CmdError IRQ several commands later. Force standby so the sequence is
	 * always issued from a legal mode, as RadioLib does before its own
	 * mode-restricted calls. */
	lr20xx_system_set_standby_mode(ctx, LR20XX_SYSTEM_STANDBY_MODE_RC);
	CHECK_CMD(ctx, "set_standby");

	rc = lr20xx_radio_common_set_pkt_type(ctx, LR20XX_RADIO_COMMON_PKT_TYPE_LORA);
	LOG_DBG("modem_cfg: set_pkt_type=%d", rc);
	CHECK_CMD(ctx, "set_pkt_type");

	/* Front-end calibration paired with set_rf_freq, exactly like RadioLib's
	 * setFrequency() (cal THEN set, together).  One cal covers ±50MHz so the
	 * bin is not the issue — the cal just has to be (re)applied here, right
	 * before the frequency is set and RX/TX starts.  Doing it only once at
	 * config time left the chip reporting RXFREQ_NO_FE_CAL (0x0200) at RX and
	 * refusing TX (PERR). */
	lr20xx_calibrate_front_end(ctx, mc->frequency);

	rc = lr20xx_radio_common_set_rf_freq(ctx, mc->frequency);
	LOG_DBG("modem_cfg: set_rf_freq(%u)=%d", mc->frequency, rc);
	CHECK_CMD(ctx, "set_rf_freq");

	/* Always configure the RX path after setting frequency
	 * (reference does this on every set_rf_freq call). */
	rc = lr20xx_radio_common_set_rx_path(
		ctx, LR20XX_RADIO_COMMON_RX_PATH_LF,
		data->rx_boost_enabled
			? LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_7
			: LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_NONE);
	data->rx_boost_applied = data->rx_boost_enabled;
	CHECK_CMD(ctx, "set_rx_path");

	/* PPM offset is this chip's name for LDRO and occupies the same wire
	 * field. The Semtech "recommended offset" helper only ever enables it
	 * for SF11/SF12, which disagrees with the rest of the mesh below
	 * 125 kHz — every other stack switches on symbol time > 16.38 ms. */
	uint32_t bw_hz = (uint32_t)(bw_enum_to_khz(mc->bandwidth) * 1000.0f);
	uint32_t symbol_time_us = ((1U << (uint8_t)mc->datarate) * 1000000U) / bw_hz;

	lr20xx_radio_lora_mod_params_t mod = {
		.sf  = (lr20xx_radio_lora_sf_t)mc->datarate,
		.bw  = bw_enum_to_lr20xx(mc->bandwidth),
		.cr  = cr_enum_to_lr20xx(mc->coding_rate),
		.ppm = (symbol_time_us > 16380) ? LR20XX_RADIO_LORA_PPM_1_4
					        : LR20XX_RADIO_LORA_NO_PPM,
	};
	rc = lr20xx_radio_lora_set_modulation_params(ctx, &mod);
	LOG_DBG("modem_cfg: set_mod(SF%d BW%d CR%d PPM%d)=%d",
		mod.sf, mod.bw, mod.cr, mod.ppm, rc);
	CHECK_CMD(ctx, "set_mod_params");

	/* DCDC workaround removed — LDO mode, RadioLib doesn't do it */

	lr20xx_radio_lora_pkt_params_t pkt = {
		.preamble_len_in_symb = mc->preamble_len,
		.pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
		.pld_len_in_bytes = 255,
		.crc = mc->packet_crc_disable ? LR20XX_RADIO_LORA_CRC_DISABLED
					      : LR20XX_RADIO_LORA_CRC_ENABLED,
		.iq = mc->iq_inverted ? LR20XX_RADIO_LORA_IQ_INVERTED
				      : LR20XX_RADIO_LORA_IQ_STANDARD,
	};
	rc = lr20xx_radio_lora_set_packet_params(ctx, &pkt);
	LOG_DBG("modem_cfg: set_pkt(pre=%d len=%d crc=%d iq=%d)=%d",
		pkt.preamble_len_in_symb, pkt.pld_len_in_bytes,
		pkt.crc, pkt.iq, rc);
	CHECK_CMD(ctx, "set_pkt_params");

	rc = lr20xx_radio_lora_set_syncword(ctx,
				       mc->public_network ? 0x34 : 0x12);
	LOG_DBG("modem_cfg: set_syncword(0x%02x)=%d",
		mc->public_network ? 0x34 : 0x12, rc);
	CHECK_CMD(ctx, "set_syncword");

	if (tx_mode) {
		/* PA config + TX params from RadioLib's known-good LF table.
		 * pa_val is a PA calibration value (NOT dBm/half-dBm). */
		lr20xx_radio_common_pa_cfg_t pa;
		int8_t pa_val;

		lr20xx_get_pa_cfg_for_power(mc->tx_power, &pa, &pa_val);
		rc = lr20xx_radio_common_set_pa_cfg(ctx, &pa);
		LOG_DBG("modem_cfg: set_pa_cfg(sel=%d mode=%d duty=%d slices=%d hf_duty=%d)=%d",
			pa.pa_sel, pa.pa_lf_mode, pa.pa_lf_duty_cycle,
			pa.pa_lf_slices, pa.pa_hf_duty_cycle, rc);

		rc = lr20xx_radio_common_set_tx_params(ctx, pa_val,
						  LR20XX_RADIO_COMMON_RAMP_48_US);
		LOG_DBG("modem_cfg: set_tx_params(pa_val=%d ramp=0x05)=%d",
			pa_val, rc);
	}

	/* Only the events the DIO1 handler actually acts on. PREAMBLE_DETECTED
	 * and SYNC_WORD_HEADER_VALID are deliberately NOT here: they fire on
	 * noise, and an unhandled DIO1 assertion drives the safety path, which
	 * restarts RX and destroys the very packet that was arriving. They stay
	 * readable in the IRQ register for lr20xx_is_receiving(), which is the
	 * same split the SX126x driver uses for its RX-busy gate. */
	rc = lr20xx_system_set_dio_irq_cfg(ctx, lr20xx_irq_dio(cfg),
		LR20XX_SYSTEM_IRQ_RX_DONE |
		LR20XX_SYSTEM_IRQ_TX_DONE |
		LR20XX_SYSTEM_IRQ_CAD_DONE |
		LR20XX_SYSTEM_IRQ_CAD_DETECTED |
		LR20XX_SYSTEM_IRQ_TIMEOUT |
		LR20XX_SYSTEM_IRQ_CRC_ERROR |
		LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR |
		LR20XX_SYSTEM_IRQ_ERROR |
		LR20XX_SYSTEM_IRQ_CMD_ERROR);
	LOG_DBG("modem_cfg: set_dio_irq=%d", rc);
	CHECK_CMD(ctx, "set_dio_irq");

	DUMP_CHIP_STATE(ctx, &data->hal_ctx, tx_mode ? "modem-TX" : "modem-RX");
}

/* ── RX duty cycle ──────────────────────────────────────────────────── */

/**
 * Re-issue the SetRxDutyCycle command with the timing stored by
 * lr20xx_lora_recv_duty_cycle().  Returns true on success, false if no
 * timing has been provided yet (falls back to continuous RX).
 */
static bool lr20xx_apply_rx_duty_cycle(struct lr20xx_data *data)
{
	void *ctx = &data->hal_ctx;

	if (data->dc_rx_ms == 0 || data->dc_sleep_ms == 0) {
		LOG_WRN("No duty-cycle timing stored — continuous RX");
		data->rx_duty_cycle_enabled = false;
		data->hal_ctx.auto_sleeps = false;
		lr20xx_radio_common_set_rx_with_timeout_in_rtc_step(
			ctx, 0xFFFFFF);
		return false;
	}

	lr20xx_radio_common_set_rx_duty_cycle(ctx, data->dc_rx_ms,
		data->dc_sleep_ms, LR20XX_RADIO_COMMON_RX_DUTY_CYCLE_MODE_RX);

	LOG_DBG("RX duty cycle re-armed: rx=%ums sleep=%ums",
		data->dc_rx_ms, data->dc_sleep_ms);
	return true;
}

/* ── Start RX (internal) ────────────────────────────────────────────── */

static void lr20xx_start_rx(struct lr20xx_data *data,
			     const struct lr20xx_config *cfg)
{
	void *ctx = &data->hal_ctx;
	lr20xx_status_t rc;

	/* Standby first — wake from any state (radio_is_sleeping is managed
	 * by the HAL via sleep opcode detection; do not set it here). */
	rc = lr20xx_system_set_standby_mode(ctx,
					    LR20XX_SYSTEM_STANDBY_MODE_RC);
	if (rc != LR20XX_STATUS_OK) {
		LOG_ERR("standby failed (rc=%d) — triggering HW reset", rc);
		lr20xx_hardware_reset(data, cfg);
	}

	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);

	/* Clear RX FIFO before entering RX (Semtech reference does this) */
	lr20xx_radio_fifo_clear_rx(ctx);

	lr20xx_apply_modem_config(data, cfg, false);

	/* set_rx_path is now always called inside apply_modem_config,
	 * with boost mode set according to rx_boost_enabled. */

	if (data->rx_duty_cycle_enabled) {
		lr20xx_apply_rx_duty_cycle(data);
		/* apply may have disabled duty cycle if preamble too short */
	} else {
		lr20xx_radio_common_set_rx_with_timeout_in_rtc_step(
			ctx, 0xFFFFFF);
	}

	/* Clear any IRQ flags set during modem configuration */
	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);

	data->in_rx_mode = true;
	data->tx_active = false;

	/* DEBUG: dump state AFTER SET_RX — should show mode=4 (RX). The
	 * "modem-RX" dump inside apply_modem_config is taken before SET_RX. */
	DUMP_CHIP_STATE(ctx, &data->hal_ctx, "post-SET_RX");
}

/* ── Lightweight RX restart (no modem reconfig) ─────────────────────── */

static void lr20xx_restart_rx(struct lr20xx_data *data)
{
	void *ctx = &data->hal_ctx;

	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);

	/* TX rewrites payload_len to the length it sent, and in explicit-header
	 * RX that field is a filter: "accept 1..payload_len, reject anything
	 * longer with a header error". Left alone, a node silently stops hearing
	 * every packet bigger than its own last transmission. RadioLib restores
	 * this on each RX entry for the same reason. */
	{
		lr20xx_radio_lora_pkt_params_t pkt = {
			.preamble_len_in_symb = data->modem_cfg.preamble_len,
			.pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
			.pld_len_in_bytes = 255,
			.crc = data->modem_cfg.packet_crc_disable
				? LR20XX_RADIO_LORA_CRC_DISABLED
				: LR20XX_RADIO_LORA_CRC_ENABLED,
			.iq = data->modem_cfg.iq_inverted
				? LR20XX_RADIO_LORA_IQ_INVERTED
				: LR20XX_RADIO_LORA_IQ_STANDARD,
		};
		lr20xx_radio_lora_set_packet_params(ctx, &pkt);
		CHECK_CMD(ctx, "restart_rx set_pkt_params");
	}

	/* "If the device is already in Rx mode, the command fails as timeout
	 * cannot be updated" (datasheet, SetRx) — and in continuous mode the
	 * chip never leaves Rx, it just keeps searching. So after a packet there
	 * is nothing to re-arm: issuing SetRx only earns a CMD_FAIL, whose
	 * CmdError comes back through DIO1 as another unhandled IRQ.
	 *
	 * Skipping it also keeps the receiver on air. Forcing a mode change
	 * would cost the HF oscillator restart (702 us) plus PLL lock (32 us)
	 * on the path taken after every single packet. */
	if (!data->rx_duty_cycle_enabled) {
		lr20xx_system_stat2_t s2 = {0};

		if (lr20xx_system_get_status(ctx, NULL, &s2, NULL) == LR20XX_STATUS_OK &&
		    s2.chip_mode == LR20XX_SYSTEM_CHIP_MODE_RX) {
			data->in_rx_mode = true;
			return;
		}
	}

	if (data->rx_duty_cycle_enabled) {
		lr20xx_apply_rx_duty_cycle(data);
	} else {
		lr20xx_radio_common_set_rx_with_timeout_in_rtc_step(
			ctx, 0xFFFFFF);
	}

	CHECK_CMD(ctx, "restart_rx set_rx");
	data->in_rx_mode = true;
}

/* ── DIO1 IRQ handler (work queue, thread context) ──────────────────── */

/* IRQ bit 16 means "call get_errors" per the datasheet; bit 17 is a rejected
 * host command. Both are useless on their own — the error word names the
 * actual fault. Throttled: these can fire every RX restart. */
static void lr20xx_log_chip_errors(void *ctx, const char *what)
{
	static const struct {
		uint16_t mask;
		const char *name;
	} err_names[] = {
		{ BIT(0),  "HF_XOSC_START" },
		{ BIT(1),  "LF_XOSC_START" },
		{ BIT(2),  "PLL_LOCK" },
		{ BIT(3),  "LF_RC_CALIB" },
		{ BIT(4),  "HF_RC_CALIB" },
		{ BIT(5),  "PLL_CALIB" },
		{ BIT(6),  "AAF_CALIB" },
		{ BIT(7),  "IMG_CALIB" },
		{ BIT(8),  "CHIP_BUSY" },
		{ BIT(9),  "RXFREQ_NO_FRONT_END_CALIB" },
		{ BIT(10), "MEAS_UNIT_ADC_CALIB" },
		{ BIT(11), "PA_OFFSET_CALIB" },
		{ BIT(12), "PPF_CALIB" },
		{ BIT(13), "SRC_CALIB" },
		{ BIT(14), "SRC_SATURATION_CALIB" },
		{ BIT(15), "SRC_TOLERANCE_CALIB" },
	};
	static uint32_t seen;
	lr20xx_system_errors_t errs = 0;

	if (lr20xx_system_get_errors(ctx, &errs) != LR20XX_STATUS_OK) {
		LOG_ERR("chip %s error; get_errors() failed too", what);
		return;
	}

	/* First few in full, then one in 64 — the console cannot keep up */
	if (seen++ >= 4 && (seen & 0x3F) != 0) {
		return;
	}

	LOG_ERR("chip %s error: errors=0x%04x", what, errs);
	for (int i = 0; i < (int)ARRAY_SIZE(err_names); i++) {
		if (errs & err_names[i].mask) {
			LOG_ERR("    %s", err_names[i].name);
		}
	}
}

static void lr20xx_dio1_callback(void *user_data);

static void lr20xx_dio1_work_handler(struct k_work *work)
{
	struct lr20xx_data *data = CONTAINER_OF(work, struct lr20xx_data,
						dio1_work);
	const struct lr20xx_config *cfg = data->dev->config;
	void *ctx = &data->hal_ctx;
	bool rx_restarted = false;

	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	/* Combined get + clear IRQ status */
	lr20xx_system_irq_mask_t irq = 0;
	lr20xx_status_t rc = lr20xx_system_get_and_clear_irq_status(ctx, &irq);

	if (rc != LR20XX_STATUS_OK) {
		LOG_ERR("Failed to read IRQ status (rc=%d)", rc);
		goto safety_check;
	}

	if (irq & (LR20XX_SYSTEM_IRQ_ERROR | LR20XX_SYSTEM_IRQ_CMD_ERROR)) {
		lr20xx_log_chip_errors(ctx, (irq & LR20XX_SYSTEM_IRQ_CMD_ERROR)
					    ? "cmd rejected" : "hardware");
		/* Latched until cleared, or every later poll re-reports it */
		lr20xx_system_clear_errors(ctx);
	}

	/* Error-only IRQs are not progress. Resetting the counter on them let
	 * an error that re-fires on every RX restart spin forever, never
	 * reaching the stuck-DIO1 escape hatch below. */
	if (irq & ~(LR20XX_SYSTEM_IRQ_ERROR | LR20XX_SYSTEM_IRQ_CMD_ERROR)) {
		data->dio1_stuck_count = 0;
	}

	/* ── RX done ──
	 * Gated on no error bits: a CRC-failed packet asserts RX_DONE and
	 * CRC_ERROR together on this chip family (same as SX126x/LR11xx),
	 * so an ungated done-first read would deliver corrupted payloads
	 * as valid.  A good packet coalesced with an earlier header error
	 * in the same handler window is dropped too — the aborted packet
	 * may have left bytes in the RX FIFO, misaligning the read.  The
	 * error branch below owns the window instead. */
	if ((irq & LR20XX_SYSTEM_IRQ_RX_DONE) &&
	    !(irq & (LR20XX_SYSTEM_IRQ_CRC_ERROR |
		     LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR))) {
		uint16_t pkt_len = 0;
		lr20xx_radio_common_get_rx_packet_length(ctx, &pkt_len);

		if (pkt_len > 0 && pkt_len <= 255) {
			lr20xx_radio_lora_packet_status_t pkt_stat;
			lr20xx_radio_lora_get_packet_status(ctx, &pkt_stat);

			lr20xx_radio_fifo_read_rx(ctx, data->rx_buf,
						  (uint16_t)pkt_len);

			/* Restart RX before firing callback */
			lr20xx_restart_rx(data);
			rx_restarted = true;

			/* When SNR < 0, use signal RSSI for a more
			 * accurate reading on weak links. */
			int16_t rssi = pkt_stat.rssi_pkt_in_dbm;
			int8_t snr = ((int8_t)pkt_stat.snr_pkt_raw + 2) >> 2;

			if (snr < 0 &&
			    pkt_stat.rssi_signal_pkt_in_dbm > rssi) {
				rssi = pkt_stat.rssi_signal_pkt_in_dbm;
			}

			k_mutex_unlock(&data->spi_mutex);

			if (data->async_rx_cb) {
				data->async_rx_cb(data->dev, data->rx_buf,
						  (uint8_t)pkt_len,
						  rssi, snr,
						  data->async_rx_user_data);
			}
			return;
		}

		LOG_WRN("RX: invalid len %d", pkt_len);
		lr20xx_restart_rx(data);
		rx_restarted = true;
	}

	/* ── CAD done ── */
	if (irq & LR20XX_SYSTEM_IRQ_CAD_DONE) {
		bool detected = (irq & LR20XX_SYSTEM_IRQ_CAD_DETECTED) != 0;

		LOG_DBG("CAD done: %s", detected ? "activity" : "free");
		data->cad_active = false;

		if (data->cad_cb) {
			lora_cad_cb cb = data->cad_cb;
			void *ud = data->cad_user_data;

			data->cad_cb = NULL;
			data->cad_user_data = NULL;
			k_mutex_unlock(&data->spi_mutex);
			cb(data->dev, detected, ud);
			return;
		}

		/* Blocking CAD: signal the semaphore */
		data->cad_result = detected ? 1 : 0;
		k_sem_give(&data->cad_sem);
	}

	/* ── TX done ── */
	if (irq & LR20XX_SYSTEM_IRQ_TX_DONE) {
		LOG_DBG("TX done");
		data->tx_active = false;

		lr20xx_start_rx(data, cfg);
		rx_restarted = true;

		if (data->tx_signal) {
			k_poll_signal_raise(data->tx_signal, 0);
		}
	}

	/* ── Timeout ── */
	if (irq & LR20XX_SYSTEM_IRQ_TIMEOUT) {
		LOG_DBG("Timeout IRQ — restarting RX");
		if (!data->tx_active) {
			lr20xx_restart_rx(data);
			rx_restarted = true;
		}
	}

	/* ── CRC / Header error ──
	 * Plain `CRC || HDR` — no SYNC_WORD_HEADER_VALID gate.  IRQ status
	 * is bulk-cleared on every handler entry, so a set header error
	 * always belongs to this window; a SYNC_VALID bit latched by
	 * another packet in the same window must not suppress it. */
	if (irq & (LR20XX_SYSTEM_IRQ_CRC_ERROR |
		   LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR)) {
		LOG_WRN("RX error: CRC=%d HDR=%d RXDONE=%d",
			(irq & LR20XX_SYSTEM_IRQ_CRC_ERROR) ? 1 : 0,
			(irq & LR20XX_SYSTEM_IRQ_LORA_HEADER_ERROR) ? 1 : 0,
			(irq & LR20XX_SYSTEM_IRQ_RX_DONE) ? 1 : 0);

		/* Drop whatever the failed (or coalesced) packet left in
		 * the RX FIFO so the next packet's read starts aligned —
		 * lr20xx_restart_rx does not clear it (only full start_rx
		 * does). */
		lr20xx_radio_fifo_clear_rx(ctx);

		if (!data->tx_active) {
			lr20xx_restart_rx(data);
			rx_restarted = true;
		}

		k_mutex_unlock(&data->spi_mutex);

		if (data->async_rx_cb) {
			data->async_rx_cb(data->dev, NULL, 0, 0, 0,
					  data->async_rx_user_data);
		}
		return;
	}

safety_check:
	/* A zero IRQ word means the edge was already consumed — a duplicate
	 * re-submit, or the restart path cleared it. Nothing failed, so do not
	 * tear RX down and do not count it toward the stuck-DIO1 reset. */
	if (irq == 0) {
		goto edge_recheck;
	}

	if (!rx_restarted && data->in_rx_mode && !data->tx_active) {
		LOG_WRN("DIO1 safety: no IRQ handled (0x%08x rc=%d), "
			"restarting RX", irq, rc);
		lr20xx_restart_rx(data);
	}

edge_recheck:
	/* Edge-triggered DIO1: if still HIGH, re-submit for pending flags.
	 * Guard against stuck DIO1: after 5 empty cycles, hardware reset. */
	if (gpio_pin_get_dt(&data->hal_ctx.dio1)) {
		data->dio1_stuck_count++;
		if (data->dio1_stuck_count >= 5) {
			LOG_ERR("DIO1 stuck HIGH for %d cycles — "
				"hardware reset", data->dio1_stuck_count);
			data->dio1_stuck_count = 0;
			lr20xx_hardware_reset(data, cfg);
			lr20xx_start_rx(data, cfg);
		} else {
			k_work_submit_to_queue(&data->dio1_wq,
					       &data->dio1_work);
		}
	} else {
		data->dio1_stuck_count = 0;
	}

	k_mutex_unlock(&data->spi_mutex);
}

static void lr20xx_dio1_callback(void *user_data)
{
	struct lr20xx_data *data = (struct lr20xx_data *)user_data;
	k_work_submit_to_queue(&data->dio1_wq, &data->dio1_work);
}

/* Forward declaration */
static int lr20xx_hw_init(struct lr20xx_data *data,
			  const struct lr20xx_config *cfg);

/* ── Driver API: config ─────────────────────────────────────────────── */

static int lr20xx_lora_config(const struct device *dev,
			      struct lora_modem_config *config)
{
	struct lr20xx_data *data = dev->data;

	if (!data->hw_initialized) {
		int ret = lr20xx_hw_init(data, dev->config);
		if (ret != 0) {
			LOG_ERR("Hardware init failed: %d", ret);
			return ret;
		}
	}

	memcpy(&data->modem_cfg, config, sizeof(*config));
	data->configured = true;

	/* Image calibration at operating frequency */
	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	/* FE cal raw value: ceil(freq / 4MHz), bit 15 = HF flag */
	uint16_t fe_raw = (uint16_t)((config->frequency + 3999999U) / 4000000U);
	LOG_DBG("config: FE cal freq=%uHz raw=0x%04x", config->frequency, fe_raw);

	lr20xx_calibrate_front_end(&data->hal_ctx, config->frequency);

	DUMP_CHIP_STATE(&data->hal_ctx, &data->hal_ctx, "config-FEcal");
	k_mutex_unlock(&data->spi_mutex);

	LOG_DBG("config: %uHz SF%d BW%d CR%d pwr=%d tx=%d",
		config->frequency, config->datarate, config->bandwidth,
		config->coding_rate, config->tx_power, config->tx);

	return 0;
}

/* ── Driver API: airtime ────────────────────────────────────────────── */

static uint32_t lr20xx_lora_airtime(const struct device *dev,
				    uint32_t data_len)
{
	struct lr20xx_data *data = dev->data;
	struct lora_modem_config *mc = &data->modem_cfg;

	uint8_t sf = (uint8_t)mc->datarate;
	float bw = bw_enum_to_khz(mc->bandwidth) * 1000.0f;
	uint8_t cr = (uint8_t)mc->coding_rate + 4;

	float ts = (float)(1 << sf) / bw;
	int de = (sf >= 11 && bw <= 125000.0f) ? 1 : 0;
	float n_payload = 8.0f + fmaxf(
		ceilf((8.0f * data_len - 4.0f * sf + 28.0f + 16.0f) /
		      (4.0f * (sf - 2.0f * de))) * cr,
		0.0f);
	float t_preamble = (mc->preamble_len + 4.25f) * ts;
	float t_payload = n_payload * ts;

	return (uint32_t)((t_preamble + t_payload) * 1000.0f);
}

/* ── Driver API: send_async ─────────────────────────────────────────── */

static int lr20xx_lora_cad(const struct device *dev, k_timeout_t timeout);

/* Blocking-CAD wait budget scaled to the actual CAD duration:
 * nSym * Tsym + startup radio-side, plus IRQ latency margin.  A fixed
 * 200 ms fits 2-symbol CAD everywhere but is exceeded by 4-symbol CAD
 * on slow presets (SF12 @ 62.5 kHz = ~262 ms). */
static uint32_t lr20xx_cad_timeout_ms(struct lr20xx_data *data)
{
	struct lora_modem_config *mc = &data->modem_cfg;
	uint8_t sf = (uint8_t)mc->datarate;
	uint8_t symb_nb = mc->cad.symbol_num ?
			  (uint8_t)mc->cad.symbol_num : 2;
	uint32_t bw_hz = (uint32_t)(bw_enum_to_khz(mc->bandwidth) * 1000.0f);

	if (bw_hz == 0 || sf < 5 || sf > 12) {
		return 200;
	}

	uint32_t tsym_us = ((1UL << sf) * 1000000UL) / bw_hz;
	/* +1 symbol covers radio startup + internal processing tail */
	uint32_t ms = ((symb_nb + 1U) * tsym_us) / 1000U + 100U;

	return MAX(ms, 200U);
}

static int lr20xx_lora_send_async(const struct device *dev,
				  uint8_t *buf, uint32_t data_len,
				  struct k_poll_signal *async)
{
	struct lr20xx_data *data = dev->data;
	const struct lr20xx_config *cfg = dev->config;
	void *ctx = &data->hal_ctx;

	if (!data->configured) return -EINVAL;
	if (data->tx_active) return -EBUSY;
	if (data_len > 255 || data_len == 0) return -EINVAL;

	/* LBT: perform blocking CAD before transmitting.  On CAD-busy, restore
	 * RX in-driver before returning -EBUSY so the C++ layer doesn't have
	 * to do a full cancel-then-restart round-trip.  lr20xx_lora_cad
	 * transitions the chip to STANDBY and clears data->in_rx_mode as
	 * part of running CAD; capture the pre-CAD state to know whether
	 * to re-arm. */
	if (data->modem_cfg.cad.mode == LORA_CAD_MODE_LBT) {
		bool was_in_rx = data->in_rx_mode;
		int cad_ret = lr20xx_lora_cad(dev,
					      K_MSEC(lr20xx_cad_timeout_ms(data)));
		if (cad_ret > 0) {
			LOG_DBG("LBT: channel busy");
			if (was_in_rx && data->async_rx_cb != NULL) {
				k_mutex_lock(&data->spi_mutex, K_FOREVER);
				lr20xx_start_rx(data, cfg);
				k_mutex_unlock(&data->spi_mutex);
			}
			return -EBUSY;
		}
		if (cad_ret < 0 && cad_ret != -ENOSYS) {
			LOG_WRN("LBT: CAD failed (%d), proceeding with TX",
				cad_ret);
		}
	}

	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	data->async_rx_cb = NULL;
	data->in_rx_mode = false;

	lr20xx_hal_disable_dio1_irq(&data->hal_ctx);

	/* Standby */
	lr20xx_status_t rc = lr20xx_system_set_standby_mode(ctx,
							    LR20XX_SYSTEM_STANDBY_MODE_RC);
	if (rc != LR20XX_STATUS_OK) {
		LOG_ERR("TX standby failed — HW reset");
		lr20xx_hardware_reset(data, cfg);
	}


	/* Clear errors before modem config */
	lr20xx_system_clear_errors(ctx);
	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);

	lr20xx_apply_modem_config(data, cfg, true);

	/* Set TX-specific packet length */
	lr20xx_radio_lora_pkt_params_t pkt = {
		.preamble_len_in_symb = data->modem_cfg.preamble_len,
		.pkt_mode = LR20XX_RADIO_LORA_PKT_EXPLICIT,
		.pld_len_in_bytes = (uint8_t)data_len,
		.crc = data->modem_cfg.packet_crc_disable
			? LR20XX_RADIO_LORA_CRC_DISABLED
			: LR20XX_RADIO_LORA_CRC_ENABLED,
		.iq = data->modem_cfg.iq_inverted
			? LR20XX_RADIO_LORA_IQ_INVERTED
			: LR20XX_RADIO_LORA_IQ_STANDARD,
	};
	lr20xx_radio_lora_set_packet_params(ctx, &pkt);

	/* Write to TX FIFO */
	lr20xx_radio_fifo_write_tx(ctx, buf, (uint16_t)data_len);

	/* Clear ALL errors + IRQs right before set_tx */
	lr20xx_system_clear_errors(ctx);
	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);

	lr20xx_hal_enable_dio1_irq(&data->hal_ctx);

	data->tx_signal = async;
	data->tx_active = true;

	lr20xx_radio_common_set_tx(ctx, 5000);

	/* Command status here is SET_TX's own (2=accepted, 1=rejected,
	 * 0=not executed) and the mode should have left standby. */
	DUMP_CHIP_STATE(ctx, &data->hal_ctx, "post-SET_TX");

	k_mutex_unlock(&data->spi_mutex);

	return 0;
}

/* ── Driver API: send (sync) ────────────────────────────────────────── */

static int lr20xx_lora_send(const struct device *dev,
			    uint8_t *buf, uint32_t data_len)
{
	struct k_poll_signal done = K_POLL_SIGNAL_INITIALIZER(done);
	struct k_poll_event evt = K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &done);

	int ret = lr20xx_lora_send_async(dev, buf, data_len, &done);
	if (ret < 0) return ret;

	uint32_t air_time = lr20xx_lora_airtime(dev, data_len);
	ret = k_poll(&evt, 1, K_MSEC(2 * air_time + 1000));
	if (ret < 0) {
		LOG_ERR("TX sync timeout");
		return ret;
	}

	return 0;
}

/* ── Driver API: recv_async ─────────────────────────────────────────── */

static int lr20xx_lora_recv_async(const struct device *dev,
				  lora_recv_cb cb, void *user_data)
{
	struct lr20xx_data *data = dev->data;
	const struct lr20xx_config *cfg = dev->config;

	if (cb == NULL) {
		k_mutex_lock(&data->spi_mutex, K_FOREVER);
		data->async_rx_cb = NULL;
		data->async_rx_user_data = NULL;
		data->in_rx_mode = false;
		k_mutex_unlock(&data->spi_mutex);
		return 0;
	}

	if (!data->configured) return -EINVAL;

	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	data->async_rx_cb = cb;
	data->async_rx_user_data = user_data;
	data->rx_duty_cycle_enabled = false;

	lr20xx_start_rx(data, cfg);

	k_mutex_unlock(&data->spi_mutex);

	return 0;
}

/* ── Driver API: recv (sync) ────────────────────────────────────────── */

static int lr20xx_lora_recv(const struct device *dev, uint8_t *buf,
			    uint8_t size, k_timeout_t timeout,
			    int16_t *rssi, int8_t *snr)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(buf);
	ARG_UNUSED(size);
	ARG_UNUSED(timeout);
	ARG_UNUSED(rssi);
	ARG_UNUSED(snr);
	return -ENOTSUP;
}

/* ── LR20xx extension API ───────────────────────────────────────────── */

int16_t lr20xx_get_rssi_inst(const struct device *dev)
{
	struct lr20xx_data *data = dev->data;
	int16_t rssi = 0;
	uint8_t half_dbm = 0;

	k_mutex_lock(&data->spi_mutex, K_FOREVER);
	lr20xx_radio_common_get_rssi_inst(&data->hal_ctx, &rssi, &half_dbm);
	k_mutex_unlock(&data->spi_mutex);

	return rssi;
}

bool lr20xx_is_receiving(const struct device *dev)
{
	struct lr20xx_data *data = dev->data;

	if (!data->in_rx_mode || data->tx_active) {
		return false;
	}

	/* Use non-destructive get_status to check for preamble/header
	 * without racing the DIO1 work handler. */
	if (k_mutex_lock(&data->spi_mutex, K_NO_WAIT) != 0) {
		return false;
	}

	lr20xx_system_irq_mask_t irq = 0;
	lr20xx_system_get_status(&data->hal_ctx, NULL, NULL, &irq);
	k_mutex_unlock(&data->spi_mutex);

	return (irq & (LR20XX_SYSTEM_IRQ_PREAMBLE_DETECTED |
		       LR20XX_SYSTEM_IRQ_SYNC_WORD_HEADER_VALID)) != 0;
}

void lr20xx_set_rx_boost(const struct device *dev, bool enable)
{
	struct lr20xx_data *data = dev->data;

	if (data->rx_boost_enabled == enable) {
		return;
	}

	data->rx_boost_enabled = enable;
	LOG_DBG("RX boost %s", enable ? "enabled" : "disabled");

	if (data->in_rx_mode && data->configured) {
		k_mutex_lock(&data->spi_mutex, K_FOREVER);
		lr20xx_radio_common_set_rx_path(
			&data->hal_ctx, LR20XX_RADIO_COMMON_RX_PATH_LF,
			enable ? LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_7
			       : LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_NONE);
		data->rx_boost_applied = enable;
		k_mutex_unlock(&data->spi_mutex);
	} else {
		data->rx_boost_applied = false;
	}
}

uint32_t lr20xx_get_random(const struct device *dev)
{
	struct lr20xx_data *data = dev->data;
	uint32_t random = 0;

	k_mutex_lock(&data->spi_mutex, K_FOREVER);
	lr20xx_system_get_random_number(
		&data->hal_ctx,
		LR20XX_SYSTEM_RANDOM_ENTROPY_SOURCE_PLL |
		LR20XX_SYSTEM_RANDOM_ENTROPY_SOURCE_ADC,
		&random);
	k_mutex_unlock(&data->spi_mutex);

	return random;
}

void lr20xx_reset_agc(const struct device *dev)
{
	struct lr20xx_data *data = dev->data;
	void *ctx = &data->hal_ctx;

	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	/* Warm sleep — powers down analog frontend (resets AGC gain state).
	 * is_ram_retention_enabled=true = warm sleep (equivalent to LR11xx warm_start). */
	lr20xx_system_sleep_cfg_t sleep_cfg = {
		.is_clk_32k_enabled       = false,
		.is_ram_retention_enabled = true,
	};
	lr20xx_system_set_sleep_mode(ctx, &sleep_cfg, 0);
	k_sleep(K_USEC(500));

	lr20xx_system_set_standby_mode(ctx, LR20XX_SYSTEM_STANDBY_MODE_RC);

	lr20xx_system_calibrate(ctx, 0x6F);

	if (data->configured) {
		lr20xx_calibrate_front_end(ctx, data->modem_cfg.frequency);
	}

	if (data->rx_boost_enabled) {
		lr20xx_radio_common_set_rx_path(
			ctx, LR20XX_RADIO_COMMON_RX_PATH_LF,
			LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_7);
		data->rx_boost_applied = true;
	}

	k_mutex_unlock(&data->spi_mutex);
}

/* ── Driver API: CAD ────────────────────────────────────────────────── */

/* Datasheet Table 6-19, the 4-symbol row — LoRaRadioBase asks for
 * LORA_CAD_SYMB_4, so these are the values that go with the window we
 * actually use. The old table here was RadioLib's 2-symbol set, which is
 * systematically higher: a higher det_peak is a *less* sensitive CAD, so
 * pairing it with a 4-symbol window made LBT more willing to talk over
 * faint traffic. Adaptive CAD offsets from this base, so getting the base
 * right shifts the whole operating range. */
static uint8_t lr20xx_cad_detect_peak(uint8_t sf)
{
	switch (sf) {
	case 5:  return 51;
	case 6:  return 51;
	case 7:  return 51;
	case 8:  return 54;
	case 9:  return 56;
	case 10: return 60;
	case 11: return 60;
	case 12: return 64;
	default: return 56;
	}
}

static int lr20xx_do_cad(struct lr20xx_data *data)
{
	void *ctx = &data->hal_ctx;
	struct lora_modem_config *mc = &data->modem_cfg;

	uint8_t sf = (uint8_t)mc->datarate;
	lr20xx_radio_lora_cad_params_t cad = {
		.cad_symb_nb = 4,   /* overridden below from mc->cad.symbol_num */
		.pnr_delta = 0,	/* exact symbol count, no best-effort */
		.cad_exit_mode = LR20XX_RADIO_LORA_CAD_EXIT_MODE_STANDBYRC,
		.cad_timeout_in_pll_step = 0,
		.cad_detect_peak = lr20xx_cad_detect_peak(sf),
	};

	/* Override from modem config if caller set non-zero values */
	if (mc->cad.symbol_num != 0) {
		cad.cad_symb_nb = (uint8_t)mc->cad.symbol_num;
	}
	if (mc->cad.detection_peak != 0) {
		cad.cad_detect_peak = mc->cad.detection_peak;
	} else if (data->cad_peak_offset != 0) {
		/* Adaptive-CAD operating offset (base +/- learned delta).
		 * LR20xx detPeak scale matches LR11xx (~48-90). */
		int peak = (int)cad.cad_detect_peak + data->cad_peak_offset;

		if (peak < 48) {
			peak = 48;
		} else if (peak > 90) {
			peak = 90;
		}
		cad.cad_detect_peak = (uint8_t)peak;
	}
	if (data->cad_probe_peak != 0) {
		/* One-shot calibration probe: absolute peak wins over all. */
		cad.cad_detect_peak = data->cad_probe_peak;
	}

	lr20xx_radio_lora_configure_cad_params(ctx, &cad);
	CHECK_CMD(ctx, "configure_cad_params");

	/* Clear any pending IRQ flags, then start CAD */
	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);
	data->cad_active = true;
	lr20xx_radio_lora_set_cad(ctx);
	CHECK_CMD(ctx, "set_cad");

	return 0;
}

static int lr20xx_lora_cad(const struct device *dev, k_timeout_t timeout)
{
	struct lr20xx_data *data = dev->data;
	int ret;

	if (!data->configured) {
		return -EINVAL;
	}

	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	/* Stop async RX if active — CAD needs the radio */
	bool was_in_rx = data->in_rx_mode;

	if (was_in_rx) {
		data->in_rx_mode = false;
		lr20xx_system_set_standby_mode(&data->hal_ctx,
					       LR20XX_SYSTEM_STANDBY_MODE_RC);
	}

	k_sem_reset(&data->cad_sem);
	data->cad_result = -ETIMEDOUT;
	data->cad_cb = NULL;

	ret = lr20xx_do_cad(data);
	k_mutex_unlock(&data->spi_mutex);

	if (ret < 0) {
		return ret;
	}

	/* Wait for DIO1 handler to signal CAD_DONE */
	ret = k_sem_take(&data->cad_sem, timeout);
	if (ret == -EAGAIN) {
		data->cad_active = false;
		return -ETIMEDOUT;
	}

	return data->cad_result;
}

static int lr20xx_lora_cad_async(const struct device *dev,
				  lora_cad_cb cb, void *user_data)
{
	struct lr20xx_data *data = dev->data;

	if (cb == NULL) {
		/* Cancel pending CAD */
		data->cad_cb = NULL;
		data->cad_user_data = NULL;
		data->cad_active = false;
		return 0;
	}

	if (!data->configured) {
		return -EINVAL;
	}

	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	bool was_in_rx = data->in_rx_mode;

	if (was_in_rx) {
		data->in_rx_mode = false;
		lr20xx_system_set_standby_mode(&data->hal_ctx,
					       LR20XX_SYSTEM_STANDBY_MODE_RC);
	}

	data->cad_cb = cb;
	data->cad_user_data = user_data;

	int ret = lr20xx_do_cad(data);
	k_mutex_unlock(&data->spi_mutex);

	return ret;
}

/* ── Extension API: adaptive CAD ────────────────────────────────────── */

void lr20xx_cad_set_peak_offset(const struct device *dev, int8_t offset)
{
	struct lr20xx_data *data = dev->data;

	data->cad_peak_offset = offset;
}

uint8_t lr20xx_cad_base_peak(const struct device *dev)
{
	struct lr20xx_data *data = dev->data;

	return lr20xx_cad_detect_peak((uint8_t)data->modem_cfg.datarate);
}

int lr20xx_cad_probe(const struct device *dev, int8_t peak_offset)
{
	struct lr20xx_data *data = dev->data;
	int base = (int)lr20xx_cad_base_peak(dev);
	int peak = base + peak_offset;
	int ret;

	if (peak < 48) {
		peak = 48;
	} else if (peak > 90) {
		peak = 90;
	}

	/* One-shot absolute override consumed by lr20xx_do_cad().  Probes and
	 * LBT both run on the mesh loop thread, so no concurrent CAD exists. */
	data->cad_probe_peak = (uint8_t)peak;
	ret = lr20xx_lora_cad(dev, K_MSEC(lr20xx_cad_timeout_ms(data)));
	data->cad_probe_peak = 0;

	return ret;
}

/* ── Driver API: recv_duty_cycle ────────────────────────────────────── */

static int lr20xx_lora_recv_duty_cycle(const struct device *dev,
				       k_timeout_t rx_period,
				       k_timeout_t sleep_period,
				       lora_recv_cb cb, void *user_data)
{
	struct lr20xx_data *data = dev->data;
	const struct lr20xx_config *cfg = dev->config;

	if (cb == NULL) {
		/* Cancel — same as recv_async(NULL) */
		k_mutex_lock(&data->spi_mutex, K_FOREVER);
		data->async_rx_cb = NULL;
		data->async_rx_user_data = NULL;
		data->in_rx_mode = false;
		k_mutex_unlock(&data->spi_mutex);
		return 0;
	}

	if (!data->configured) {
		return -EINVAL;
	}

	/* Explicit timing only — the adapter owns the window sizing. */
	if (K_TIMEOUT_EQ(rx_period, K_FOREVER) ||
	    K_TIMEOUT_EQ(sleep_period, K_FOREVER)) {
		LOG_ERR("recv_duty_cycle: explicit rx/sleep periods required");
		return -EINVAL;
	}

	k_mutex_lock(&data->spi_mutex, K_FOREVER);

	data->async_rx_cb = cb;
	data->async_rx_user_data = user_data;

	void *ctx = &data->hal_ctx;

	lr20xx_system_set_standby_mode(ctx, LR20XX_SYSTEM_STANDBY_MODE_RC);
	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);
	lr20xx_radio_fifo_clear_rx(ctx);
	lr20xx_apply_modem_config(data, cfg, false);

	uint32_t rx_ms = k_ticks_to_ms_ceil32(rx_period.ticks);
	uint32_t slp_ms = k_ticks_to_ms_ceil32(sleep_period.ticks);
	if (rx_ms < 1) rx_ms = 1;
	if (slp_ms < 1) slp_ms = 1;

	/* Store for the re-arm paths (lr20xx_start_rx / lr20xx_restart_rx)
	 * so every re-entry uses exactly this timing. */
	data->dc_rx_ms = rx_ms;
	data->dc_sleep_ms = slp_ms;
	data->rx_duty_cycle_enabled = true;
	data->hal_ctx.auto_sleeps = true;
	lr20xx_radio_common_set_rx_duty_cycle(ctx, rx_ms, slp_ms,
		LR20XX_RADIO_COMMON_RX_DUTY_CYCLE_MODE_RX);
	LOG_INF("recv_duty_cycle: rx=%ums sleep=%ums", rx_ms, slp_ms);

	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);
	data->in_rx_mode = true;
	data->tx_active = false;

	k_mutex_unlock(&data->spi_mutex);
	return 0;
}

/* ── Deferred hardware init ─────────────────────────────────────────── */

static int lr20xx_hw_init(struct lr20xx_data *data,
			  const struct lr20xx_config *cfg)
{
	void *ctx = &data->hal_ctx;

	LOG_INF("LR20xx hardware init starting");

	lr20xx_system_version_t ver;
	bool found = false;

	for (int attempt = 0; attempt < 3; attempt++) {
		lr20xx_hal_status_t hal_rc = lr20xx_hal_reset(ctx);
		if (hal_rc != LR20XX_HAL_STATUS_OK) {
			LOG_WRN("LR20xx reset failed (attempt %d)", attempt);
			k_msleep(10);
			continue;
		}

		lr20xx_status_t st = lr20xx_system_get_version(ctx, &ver);
		if (st == LR20XX_STATUS_OK) {
			found = true;
			break;
		}

		LOG_WRN("LR20xx get_version failed (attempt %d)", attempt);
		k_msleep(10);
	}

	if (!found) {
		LOG_ERR("LR20xx not found after 3 attempts");
		return -EIO;
	}

	LOG_INF("LR20xx SDK get_version: major=%u minor=%u", ver.major, ver.minor);

#if IS_ENABLED(CONFIG_LOG)
	/* GET_VERSION (0x0101) must answer 0x01 0x18 for the documented FW 1.24.
	 * Two dumps, because the two references disagree on the protocol and
	 * our layout matches RadioLib on paper yet reads shifted on silicon:
	 *
	 *   frame A = command and response in ONE NSS window (RadioLib's model)
	 *   frame B = a bare read in a SECOND window (the Semtech HAL's model)
	 *
	 * Whichever one contains 01 18 is the protocol this chip actually
	 * speaks, and its offset is the skip the HAL should use.
	 */
	{
		const uint8_t ver_cmd[2] = { 0x01, 0x01 };
		uint8_t follow[8] = { 0 };

		lr20xx_hal_debug_raw_frame(ctx, ver_cmd, sizeof(ver_cmd), 10);

		if (lr20xx_hal_direct_read(ctx, follow, sizeof(follow)) ==
		    LR20XX_HAL_STATUS_OK) {
			LOG_HEXDUMP_INF(follow, sizeof(follow),
					"frame B: bare read after GET_VERSION");
		} else {
			LOG_WRN("frame B: bare read failed");
		}
	}
#endif

	/* The only base FW version the datasheet documents is 1.24 (0x01/0x18).
	 * Anything else still runs — the mismatch is worth a line in the log
	 * when a bring-up goes sideways, not a hard failure. */
	if (ver.major != 0x01 || ver.minor != 0x18) {
		LOG_WRN("Unexpected LR2021 FW %u.%u (datasheet documents 1.24)",
			ver.major, ver.minor);
	}

	DUMP_CHIP_STATE(ctx, &data->hal_ctx, "post-reset");

	/* SIMO DC-DC workaround REMOVED — datasheet §22.6 says it's only
	 * needed when SetRegMode simo_usage=0x02 (SIMO_NORMAL).
	 * We run in LDO mode (default, simo_usage=0x00). */

	/* A board that declares a TCXO but is actually fitted with a plain
	 * crystal fails to start its 32 MHz reference once TCXO mode is on, and
	 * every later command is rejected. The datasheet (S1.9.3) puts the TCXO
	 * on XTA with the VTCXO regulator, so the two builds are indistinguishable
	 * from software — the chip's own HF_XOSC_START error is the only signal.
	 * Fall back to XTAL rather than come up dead, as MeshCore's CustomLR2021
	 * does when begin() reports a rejected command. */
	if (cfg->tcxo_voltage_mv > 0 && !data->tcxo_disabled) {
		uint32_t tcxo_ticks = tcxo_start_time_periods(cfg->tcxo_startup_delay_ms);
		lr20xx_status_t tcxo_rc = lr20xx_system_set_tcxo_mode(ctx,
					    get_tcxo_voltage(cfg->tcxo_voltage_mv),
					    tcxo_ticks);
		LOG_DBG("init: set_tcxo(%dmV, %u ticks)=%d",
			cfg->tcxo_voltage_mv, tcxo_ticks, tcxo_rc);
	} else {
		LOG_DBG("init: TCXO disabled (XTAL mode)");
	}

	/* RadioLib does NOT call cfg_lfclk or set_reg_mode.
	 * Stay in LDO mode (chip default after reset).
	 * DCDC mode + wrong SET_REG_MODE encoding was likely
	 * preventing TX. */
	lr20xx_status_t st;

	lr20xx_configure_rfswitch(ctx, cfg);
	LOG_DBG("RF switch: en=0x%02x stby=0x%02x rx=0x%02x tx=0x%02x txhp=0x%02x",
		cfg->rfswitch_enable, cfg->rfswitch_standby, cfg->rfswitch_rx,
		cfg->rfswitch_tx, cfg->rfswitch_tx_hp);

	st = lr20xx_system_set_dio_function(ctx, lr20xx_irq_dio(cfg),
				       LR20XX_SYSTEM_DIO_FUNC_IRQ,
				       lr20xx_irq_dio_pull(cfg));

	st = lr20xx_radio_common_set_rx_tx_fallback_mode(ctx,
						    LR20XX_RADIO_FALLBACK_STDBY_RC);

	DUMP_CHIP_STATE(ctx, &data->hal_ctx, "pre-cal");

	lr20xx_system_clear_errors(ctx);
	lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);

	/* Calibrate all analog blocks: 0x6F = LF_RC|HF_RC|PLL|AAF|MU|PA_OFF */
	st = lr20xx_system_calibrate(ctx, 0x6F);

	/* RadioLib waits for BUSY to go LOW after calibrate.
	 * We poll the BUSY pin (max 500ms timeout). */
	{
		int64_t cal_start = k_uptime_get();
		while (gpio_pin_get_dt(&data->hal_ctx.busy)) {
			k_msleep(1);
			if ((k_uptime_get() - cal_start) > 500) {
				LOG_ERR("BUSY stuck HIGH after calibrate!");
				break;
			}
		}
	}

	DUMP_CHIP_STATE(ctx, &data->hal_ctx, "post-cal");

	/* The 32 MHz reference is what calibration needs, so this is where a
	 * wrong clock source shows up. One retry only — if XTAL fails too, the
	 * fault is not the clock config and looping would just hide it. */
	{
		lr20xx_system_errors_t clk_err = 0;

		lr20xx_system_get_errors(ctx, &clk_err);
		if ((clk_err & LR20XX_SYSTEM_ERRORS_HF_XOSC_START_MASK) &&
		    cfg->tcxo_voltage_mv > 0 && !data->tcxo_disabled) {
			LOG_WRN("HF XOSC did not start with TCXO at %d mV "
				"(errors=0x%04x) — retrying in XTAL mode",
				cfg->tcxo_voltage_mv, clk_err);
			data->tcxo_disabled = true;
			return lr20xx_hw_init(data, cfg);
		}
	}

	/* Front-end calibration at 868 MHz LF.
	 * raw_value = ceil(868000000/4000000) = 217 = 0x00D9 */
	st = lr20xx_calibrate_front_end(ctx, 868000000);

	DUMP_CHIP_STATE(ctx, &data->hal_ctx, "post-FEcal");

	/* Verify: set packet type to LoRa and read it back */
	st = lr20xx_radio_common_set_pkt_type(ctx, LR20XX_RADIO_COMMON_PKT_TYPE_LORA);

	/* dcdc_reset removed — not needed in LDO mode, RadioLib doesn't do it */

	lr20xx_radio_common_pkt_type_t pkt_readback = 0xFF;
	lr20xx_radio_common_get_pkt_type(ctx, &pkt_readback);

	DUMP_CHIP_STATE(ctx, &data->hal_ctx, "init-done");

#if IS_ENABLED(CONFIG_LOG)
	/* Does merely polling status raise CmdError? get_status is a bare
	 * direct_read (no opcode) in this SDK, where RadioLib sends GET_STATUS
	 * as a real command — so if the chip parses those clocked-out zeros as
	 * a malformed command, our own polling is manufacturing the CMD_ERROR
	 * storm rather than reporting one. Chip is idle here, so nothing else
	 * can set the bit. */
	{
		lr20xx_system_irq_mask_t before = 0, after = 0;
		lr20xx_system_stat1_t s1 = {0};

		lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);
		lr20xx_system_get_status(ctx, NULL, NULL, &before);

		for (int i = 0; i < 5; i++) {
			lr20xx_system_get_status(ctx, &s1, NULL, NULL);
		}

		lr20xx_system_get_status(ctx, NULL, NULL, &after);
		LOG_INF("status-poll probe: irq before=0x%08x after=0x%08x "
			"(CMD_ERROR %s self-inflicted)", before, after,
			(after & LR20XX_SYSTEM_IRQ_CMD_ERROR) ? "IS" : "is NOT");
		lr20xx_system_clear_irq_status(ctx, LR20XX_SYSTEM_IRQ_ALL_MASK);
	}
#endif

	/* DEBUG: what voltage does the chip see on its OWN supply pin? (mV,
	 * after MU calibration).  If this reads low (≪3000mV) while the board
	 * is powered, the LR2021 VBAT/supply pin is floating or miswired — that
	 * is why it flags LOW_BATTERY and aborts TX no matter the system rail
	 * or external battery. ~3000-3300mV ⇒ supply is fine, sag is transient. */
	{
		uint16_t vbat_mv = 0;
		lr20xx_status_t vrc = lr20xx_system_get_vbat(ctx,
			LR20XX_SYSTEM_VALUE_FORMAT_UNIT,
			LR20XX_SYSTEM_MEAS_RES_12_BITS, &vbat_mv);
		LOG_INF("init: chip VBAT reads %u mV (rc=%d)", vbat_mv, vrc);
	}

	lr20xx_system_clear_errors(ctx);
	lr20xx_hal_enable_dio1_irq(&data->hal_ctx);

	data->rx_boost_enabled = cfg->rx_boosted;
	data->rx_boost_applied = false;

	data->hw_initialized = true;
	LOG_INF("LR20xx driver ready");
	return 0;
}

/* ── Driver init (lightweight — runs at POST_KERNEL) ────────────────── */

static int lr20xx_lora_init(const struct device *dev)
{
	struct lr20xx_data *data = dev->data;
	const struct lr20xx_config *cfg = dev->config;
	int ret;

	data->dev = dev;
	data->hw_initialized = false;

	k_mutex_init(&data->spi_mutex);
	k_sem_init(&data->cad_sem, 0, 1);
	k_work_init(&data->dio1_work, lr20xx_dio1_work_handler);

	k_work_queue_start(&data->dio1_wq, lr20xx_dio1_wq_stack,
			   K_THREAD_STACK_SIZEOF(lr20xx_dio1_wq_stack),
			   K_PRIO_COOP(7), NULL);
	k_thread_name_set(&data->dio1_wq.thread, "lr20xx_dio1");

	if (!spi_is_ready_dt(&cfg->bus)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	memset(&data->hal_ctx, 0, sizeof(data->hal_ctx));
	data->hal_ctx.spi_dev = cfg->bus.bus;
	data->hal_ctx.spi_cfg = cfg->bus.config;
	/* Manual NSS control — disable SPI peripheral CS */
	data->hal_ctx.spi_cfg.cs.cs_is_gpio = false;
	data->hal_ctx.spi_cfg.cs.gpio.port = NULL;
	data->hal_ctx.nss.port  = cfg->bus.config.cs.gpio.port;
	data->hal_ctx.nss.pin   = cfg->bus.config.cs.gpio.pin;
	data->hal_ctx.nss.dt_flags = cfg->bus.config.cs.gpio.dt_flags;
	data->hal_ctx.reset = cfg->reset;
	data->hal_ctx.busy  = cfg->busy;
	data->hal_ctx.dio1  = cfg->dio1;
	data->hal_ctx.radio_is_sleeping = false;

	ret = lr20xx_hal_init(&data->hal_ctx);
	if (ret != 0) {
		LOG_ERR("HAL init failed: %d", ret);
		return ret;
	}

	lr20xx_hal_set_dio1_callback(&data->hal_ctx, lr20xx_dio1_callback,
				     data);

	LOG_INF("LR20xx driver registered (hw init deferred to first config)");
	return 0;
}

/* ── Device instantiation ───────────────────────────────────────────── */

static DEVICE_API(lora, lr20xx_lora_api) = {
	.config          = lr20xx_lora_config,
	.airtime         = lr20xx_lora_airtime,
	.send            = lr20xx_lora_send,
	.send_async      = lr20xx_lora_send_async,
	.recv            = lr20xx_lora_recv,
	.recv_async      = lr20xx_lora_recv_async,
	.cad             = lr20xx_lora_cad,
	.cad_async       = lr20xx_lora_cad_async,
	.recv_duty_cycle = lr20xx_lora_recv_duty_cycle,
};

#define LR20XX_INIT(n)                                                       \
	static const struct lr20xx_config lr20xx_config_##n = {              \
		.bus = SPI_DT_SPEC_INST_GET(n,                               \
			SPI_WORD_SET(8) | SPI_OP_MODE_MASTER |               \
			SPI_TRANSFER_MSB),                                   \
		.reset = GPIO_DT_SPEC_INST_GET(n, reset_gpios),              \
		.busy  = GPIO_DT_SPEC_INST_GET(n, busy_gpios),              \
		.dio1  = GPIO_DT_SPEC_INST_GET(n, dio1_gpios),              \
		.tcxo_voltage_mv =                                           \
			DT_INST_PROP_OR(n, tcxo_voltage_mv, 0),             \
		.tcxo_startup_delay_ms =                                     \
			DT_INST_PROP_OR(n, tcxo_startup_delay_ms, 5),       \
		.rx_boosted       = DT_INST_PROP(n, rx_boosted),            \
		.irq_dio          = DT_INST_PROP_OR(n, irq_dio, 9),         \
		.rfswitch_enable  = DT_INST_PROP_OR(n, rfswitch_enable, 0), \
		.rfswitch_standby = DT_INST_PROP_OR(n, rfswitch_standby, 0),\
		.rfswitch_rx      = DT_INST_PROP_OR(n, rfswitch_rx, 0),     \
		.rfswitch_tx      = DT_INST_PROP_OR(n, rfswitch_tx, 0),     \
		.rfswitch_tx_hp   = DT_INST_PROP_OR(n, rfswitch_tx_hp, 0),  \
		.pa_hp_sel        = DT_INST_PROP_OR(n, pa_hp_sel, 7),       \
		.pa_duty_cycle    = DT_INST_PROP_OR(n, pa_duty_cycle, 4),   \
	};                                                                   \
	static struct lr20xx_data lr20xx_data_##n;                           \
	DEVICE_DT_INST_DEFINE(n, lr20xx_lora_init, NULL,                     \
			      &lr20xx_data_##n, &lr20xx_config_##n,          \
			      POST_KERNEL, CONFIG_LORA_INIT_PRIORITY,        \
			      &lr20xx_lora_api);

DT_INST_FOREACH_STATUS_OKAY(LR20XX_INIT)
