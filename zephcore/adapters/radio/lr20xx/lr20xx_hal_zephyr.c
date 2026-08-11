/*
 * SPDX-License-Identifier: MIT
 * LR20xx HAL implementation for Zephyr - ZephCore
 *
 * Based on Semtech SWDR001 lr20xx_driver and the ZephCore lr11xx HAL.
 */

#include "lr20xx_hal_zephyr.h"
#include "lr20xx_hal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lr20xx_hal, CONFIG_LORA_LOG_LEVEL);

/* BUSY timeout; covers worst-case post-reset firmware boot (~300ms) with margin */
#define LR20XX_BUSY_TIMEOUT_MS  3000

/* Static state for DIO1 interrupt handling */
static struct gpio_callback dio1_gpio_cb;
static lr20xx_dio1_callback_t dio1_user_cb = NULL;
static void *dio1_user_data = NULL;

/* BUSY pin interrupt — wakes wait_on_busy() via semaphore instead of polling */
static struct gpio_callback busy_gpio_cb;
static K_SEM_DEFINE(busy_sem, 0, 1);

static void busy_isr_callback(const struct device *dev, struct gpio_callback *cb,
			       uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	k_sem_give(&busy_sem);
}

/* Track the last SPI opcode for debugging BUSY stuck */
static uint16_t last_opcode;
static int64_t last_cmd_time;

/**
 * @brief Wait until BUSY deasserts, using GPIO IRQ + semaphore (not polling).
 * Fast-path returns immediately if already idle.
 */
static lr20xx_hal_status_t wait_on_busy(struct lr20xx_hal_context *ctx)
{
	if (!gpio_pin_get_dt(&ctx->busy)) {
		return LR20XX_HAL_STATUS_OK;
	}

	k_sem_reset(&busy_sem);
	gpio_pin_interrupt_configure_dt(&ctx->busy, GPIO_INT_EDGE_TO_INACTIVE);

	/* Re-check after arming IRQ to close the race: BUSY may have dropped
	 * between the first read and the interrupt enable. */
	if (!gpio_pin_get_dt(&ctx->busy)) {
		gpio_pin_interrupt_configure_dt(&ctx->busy, GPIO_INT_DISABLE);
		return LR20XX_HAL_STATUS_OK;
	}

	int ret = k_sem_take(&busy_sem, K_MSEC(LR20XX_BUSY_TIMEOUT_MS));
	gpio_pin_interrupt_configure_dt(&ctx->busy, GPIO_INT_DISABLE);

	if (ret == -EAGAIN) {
		LOG_ERR("BUSY timeout! last_op=0x%04x sent_at=%lld (%lld ms ago) DIO1=%d",
			last_opcode, last_cmd_time,
			k_uptime_get() - last_cmd_time,
			gpio_pin_get_dt(&ctx->dio1));
		return LR20XX_HAL_STATUS_ERROR;
	}

	return LR20XX_HAL_STATUS_OK;
}

/**
 * @brief Assert ready; if sleeping, issue NSS wake pulse first.
 */
static lr20xx_hal_status_t check_device_ready(struct lr20xx_hal_context *ctx)
{
	if (!ctx->radio_is_sleeping) {
		/* Always wait — never second-guess a high BUSY.
		 *
		 * There used to be an "if a duty cycle is armed and BUSY is high
		 * the chip must have self-slept, so wake it" short-circuit here.
		 * It could not tell self-sleep apart from a command still running,
		 * and BUSY is high for both: CAD holds it for the CAD duration,
		 * SetTx until the PA has ramped, SetRxDutyCycle and the FE
		 * calibration for their own spans.  So after every slow command
		 * the next one skipped the wait and fired an NSS pulse instead —
		 * a frame with no clock cycles, which the chip reads as a
		 * malformed command (CMD_PERR, DS Table 6-38) and which latches
		 * CmdError.  That is the rejection storm and reset loop seen on
		 * the MeshTracker X1 whenever rxduty was on, and why rxduty off
		 * always worked.
		 *
		 * Waiting costs nothing worth saving: a duty-cycle sleep window is
		 * bounded by cycle_time - rx_max_time (25 ms at the presets we
		 * use) and the chip drops BUSY by itself on waking into its RX
		 * window.  There is no timeout to burn.  The LR11xx driver has
		 * never had such a branch, for the same reason. */
		return wait_on_busy(ctx);
	}

	/* Wake from sleep: the chip leaves Sleep when NSS is held low for 100us
	 * (datasheet, Sleep mode). Semtech's reference HAL allows 1 ms; keep that
	 * margin — a short pulse leaves the radio asleep and every following
	 * command is answered by a chip that is not listening. */
	gpio_pin_set_dt(&ctx->nss, 1);
	k_busy_wait(1000);
	gpio_pin_set_dt(&ctx->nss, 0);

	ctx->radio_is_sleeping = false;
	return wait_on_busy(ctx);
}

static void dio1_isr_callback(const struct device *dev, struct gpio_callback *cb,
			       uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (dio1_user_cb) {
		dio1_user_cb(dio1_user_data);
	}
}

int lr20xx_hal_init(struct lr20xx_hal_context *ctx)
{
	int ret;

	ctx->radio_is_sleeping = false;

	ret = gpio_pin_configure_dt(&ctx->nss, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure NSS: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&ctx->reset, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure RESET: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&ctx->busy, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure BUSY: %d", ret);
		return ret;
	}

	/* BUSY IRQ enabled on-demand by wait_on_busy() */
	gpio_init_callback(&busy_gpio_cb, busy_isr_callback, BIT(ctx->busy.pin));
	ret = gpio_add_callback(ctx->busy.port, &busy_gpio_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add BUSY callback: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&ctx->dio1, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure DIO1: %d", ret);
		return ret;
	}

	gpio_init_callback(&dio1_gpio_cb, dio1_isr_callback, BIT(ctx->dio1.pin));
	ret = gpio_add_callback(ctx->dio1.port, &dio1_gpio_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add DIO1 callback: %d", ret);
		return ret;
	}

	LOG_INF("LR20xx HAL initialized");
	return 0;
}

void lr20xx_hal_set_dio1_callback(struct lr20xx_hal_context *ctx,
				   lr20xx_dio1_callback_t cb, void *user_data)
{
	ARG_UNUSED(ctx);
	dio1_user_cb = cb;
	dio1_user_data = user_data;
}

void lr20xx_hal_enable_dio1_irq(struct lr20xx_hal_context *ctx)
{
	gpio_pin_interrupt_configure_dt(&ctx->dio1, GPIO_INT_EDGE_RISING);
}

void lr20xx_hal_disable_dio1_irq(struct lr20xx_hal_context *ctx)
{
	gpio_pin_interrupt_configure_dt(&ctx->dio1, GPIO_INT_DISABLE);
}

lr20xx_hal_status_t lr20xx_hal_write(const void *context, const uint8_t *command,
				      const uint16_t command_length,
				      const uint8_t *data, const uint16_t data_length)
{
	struct lr20xx_hal_context *ctx = (struct lr20xx_hal_context *)context;
	int ret;

	/* Track opcode for BUSY timeout diagnostics */
	if (command_length >= 2) {
		last_opcode = ((uint16_t)command[0] << 8) | command[1];
	}
	last_cmd_time = k_uptime_get();

	if (check_device_ready(ctx) != LR20XX_HAL_STATUS_OK) {
		LOG_ERR("hal_write: device not ready, op=0x%04x", last_opcode);
		return LR20XX_HAL_STATUS_ERROR;
	}

	const struct spi_buf tx_bufs[] = {
		{ .buf = (uint8_t *)command, .len = command_length },
		{ .buf = (uint8_t *)data, .len = data_length },
	};
	const struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = (data_length > 0) ? 2 : 1,
	};

	gpio_pin_set_dt(&ctx->nss, 1);
	ret = spi_write(ctx->spi_dev, &ctx->spi_cfg, &tx);
	gpio_pin_set_dt(&ctx->nss, 0);

	if (ret < 0) {
		LOG_ERR("SPI write failed: %d", ret);
		return LR20XX_HAL_STATUS_ERROR;
	}

	/* Opcode 0x0127 = SetSleep (LR2021 datasheet §5.4.2) */
	if (command_length >= 2 && command[0] == 0x01 && command[1] == 0x27) {
		ctx->radio_is_sleeping = true;
		k_busy_wait(1000);  /* ≥500us sleep entry per datasheet §5.4.2 */
		return LR20XX_HAL_STATUS_OK;
	}

	return wait_on_busy(ctx);
}

/* LR2021 prepends a 2-byte stat header before response data (datasheet §5.4.1.2) */
#define LR20XX_STAT_LEN 2

/**
 * @brief Clock one command-and-response frame inside a single NSS assertion.
 *
 * The LR2021 streams the whole answer within the NSS window that carried the
 * command:
 *
 *     MOSI  [ command_length ][      LR20XX_STAT_LEN + data_length      ]
 *     MISO  [  undefined     ][ stat header ][        payload           ]
 *
 * Releasing NSS between the command and the payload ends the frame, so the
 * following read starts a fresh one and the chip answers it with the status /
 * IRQ word rather than the payload the caller asked for. That misread is not
 * obviously wrong at the call site — it is a plausible-looking short integer —
 * so it surfaces as nonsense lengths and all-zero status reads rather than as
 * an SPI error. Keep every read in one transaction.
 *
 * NULL TX buffers clock the SPI controller's over-read character, NOT zero --
 * Nordic defaults it to 0xff, which the LR2021 reads as a bogus opcode and
 * rejects. Boards using this driver must set overrun-character = <0x00> (the
 * LR2021 NOP) on the SPI node.
 */
static int lr20xx_spi_read_frame(struct lr20xx_hal_context *ctx, const uint8_t *command,
				  uint16_t command_length, uint8_t *data, uint16_t data_length)
{
	int ret;

	/* Phase 1: the command, in its own NSS window. */
	{
		const struct spi_buf tx_buf = {
			.buf = (uint8_t *)command,
			.len = command_length,
		};
		const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

		gpio_pin_set_dt(&ctx->nss, 1);
		ret = spi_write(ctx->spi_dev, &ctx->spi_cfg, &tx);
		gpio_pin_set_dt(&ctx->nss, 0);

		if (ret < 0) {
			return ret;
		}
	}

	/* The answer is not ready until BUSY drops — without this the second
	 * window clocks out whatever the chip had, two bytes early. */
	if (wait_on_busy(ctx) != LR20XX_HAL_STATUS_OK) {
		return -ETIMEDOUT;
	}

	/* Phase 2: two dummy bytes absorb the stat header, then the payload.
	 * The stat header lands in a real buffer rather than being discarded
	 * with a NULL one: spi_nrfx_spim rejects a transfer whose first TX and
	 * RX buffers are both NULL (-EINVAL), and TX is legitimately NULL here. */
	{
		uint8_t stat[LR20XX_STAT_LEN];
		const struct spi_buf tx_buf = {
			.buf = NULL,
			.len = LR20XX_STAT_LEN + data_length,
		};
		const struct spi_buf rx_bufs[] = {
			{ .buf = stat, .len = LR20XX_STAT_LEN },
			{ .buf = data, .len = data_length },
		};
		const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
		const struct spi_buf_set rx = { .buffers = rx_bufs, .count = 2 };

		gpio_pin_set_dt(&ctx->nss, 1);
		ret = spi_transceive(ctx->spi_dev, &ctx->spi_cfg, &tx, &rx);
		gpio_pin_set_dt(&ctx->nss, 0);
	}

	return ret;
}

#if IS_ENABLED(CONFIG_LOG)
/*
 * Dump one command frame from byte 0 — command echo, stat header and payload
 * all in one line, nothing discarded. The single-NSS read layout above has
 * never been checked against real silicon; this shows where the payload
 * actually starts instead of assuming it.
 */
void lr20xx_hal_debug_raw_frame(const void *context, const uint8_t *command,
				uint16_t command_length, uint16_t extra)
{
	struct lr20xx_hal_context *ctx = (struct lr20xx_hal_context *)context;
	uint8_t rx[24] = { 0 };
	uint16_t total = command_length + extra;

	if (total > sizeof(rx)) {
		total = sizeof(rx);
	}

	const struct spi_buf tx_bufs[] = {
		{ .buf = (uint8_t *)command, .len = command_length },
		{ .buf = NULL, .len = (size_t)(total - command_length) },
	};
	const struct spi_buf rx_bufs[] = {
		{ .buf = rx, .len = total },
	};
	const struct spi_buf_set tx = { .buffers = tx_bufs, .count = 2 };
	const struct spi_buf_set rxs = { .buffers = rx_bufs, .count = 1 };

	if (check_device_ready(ctx) != LR20XX_HAL_STATUS_OK) {
		LOG_WRN("raw frame: device not ready");
		return;
	}

	gpio_pin_set_dt(&ctx->nss, 1);
	int ret = spi_transceive(ctx->spi_dev, &ctx->spi_cfg, &tx, &rxs);
	gpio_pin_set_dt(&ctx->nss, 0);

	if (ret < 0) {
		LOG_ERR("raw frame: spi err %d", ret);
		return;
	}

	LOG_HEXDUMP_INF(rx, total, "frame A: one NSS window, from byte 0");
}
#endif /* CONFIG_LOG */

lr20xx_hal_status_t lr20xx_hal_read(const void *context, const uint8_t *command,
				     const uint16_t command_length,
				     uint8_t *data, const uint16_t data_length)
{
	struct lr20xx_hal_context *ctx = (struct lr20xx_hal_context *)context;
	int ret;

	/* Track opcode for BUSY timeout diagnostics */
	if (command_length >= 2) {
		last_opcode = ((uint16_t)command[0] << 8) | command[1];
	}
	last_cmd_time = k_uptime_get();

	if (check_device_ready(ctx) != LR20XX_HAL_STATUS_OK) {
		LOG_ERR("hal_read: device not ready, op=0x%04x", last_opcode);
		return LR20XX_HAL_STATUS_ERROR;
	}

	/* No response requested — this is a plain command write. */
	if (data_length == 0) {
		const struct spi_buf tx_buf = {
			.buf = (uint8_t *)command,
			.len = command_length,
		};
		const struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };

		gpio_pin_set_dt(&ctx->nss, 1);
		ret = spi_write(ctx->spi_dev, &ctx->spi_cfg, &tx);
		gpio_pin_set_dt(&ctx->nss, 0);

		if (ret < 0) {
			LOG_ERR("SPI write (cmd) failed: %d", ret);
			return LR20XX_HAL_STATUS_ERROR;
		}

		return wait_on_busy(ctx);
	}

	ret = lr20xx_spi_read_frame(ctx, command, command_length, data, data_length);
	if (ret < 0) {
		LOG_ERR("SPI read failed: %d", ret);
		return LR20XX_HAL_STATUS_ERROR;
	}

	return LR20XX_HAL_STATUS_OK;
}

lr20xx_hal_status_t lr20xx_hal_direct_read(const void *context, uint8_t *data,
					    const uint16_t data_length)
{
	struct lr20xx_hal_context *ctx = (struct lr20xx_hal_context *)context;
	int ret;

	if (check_device_ready(ctx) != LR20XX_HAL_STATUS_OK) {
		return LR20XX_HAL_STATUS_ERROR;
	}

	const struct spi_buf rx_buf = { .buf = data, .len = data_length };
	const struct spi_buf_set rx = { .buffers = &rx_buf, .count = 1 };

	gpio_pin_set_dt(&ctx->nss, 1);
	ret = spi_read(ctx->spi_dev, &ctx->spi_cfg, &rx);
	gpio_pin_set_dt(&ctx->nss, 0);

	if (ret < 0) {
		LOG_ERR("SPI direct read failed: %d", ret);
		return LR20XX_HAL_STATUS_ERROR;
	}

	return LR20XX_HAL_STATUS_OK;
}

lr20xx_hal_status_t lr20xx_hal_direct_read_fifo(const void *context,
						  const uint8_t *command,
						  const uint16_t command_length,
						  uint8_t *data,
						  const uint16_t data_length)
{
	struct lr20xx_hal_context *ctx = (struct lr20xx_hal_context *)context;
	int ret;

	if (check_device_ready(ctx) != LR20XX_HAL_STATUS_OK) {
		return LR20XX_HAL_STATUS_ERROR;
	}

	/* Unlike every other read, the FIFO answers inside the command's own NSS
	 * window with no stat header and no BUSY wait — command bytes out, payload
	 * straight back. Semtech's reference HAL is explicit about this
	 * (lr20xx_hal_direct_read_fifo); treating it like a normal read releases
	 * NSS mid-frame and eats two payload bytes as a header. */
	{
		const struct spi_buf tx_bufs[] = {
			{ .buf = (uint8_t *)command, .len = command_length },
			{ .buf = NULL, .len = data_length },
		};
		const struct spi_buf rx_bufs[] = {
			{ .buf = NULL, .len = command_length },
			{ .buf = data, .len = data_length },
		};
		const struct spi_buf_set tx = { .buffers = tx_bufs, .count = 2 };
		const struct spi_buf_set rx = { .buffers = rx_bufs, .count = 2 };

		gpio_pin_set_dt(&ctx->nss, 1);
		ret = spi_transceive(ctx->spi_dev, &ctx->spi_cfg, &tx, &rx);
		gpio_pin_set_dt(&ctx->nss, 0);
	}

	if (ret < 0) {
		LOG_ERR("SPI FIFO read failed: %d", ret);
		return LR20XX_HAL_STATUS_ERROR;
	}

	return LR20XX_HAL_STATUS_OK;
}

lr20xx_hal_status_t lr20xx_hal_reset(const void *context)
{
	struct lr20xx_hal_context *ctx = (struct lr20xx_hal_context *)context;

	LOG_INF("LR20xx reset: assert reset, hold 10ms");

	gpio_pin_set_dt(&ctx->reset, 1);
	k_msleep(10);   /* ≥100us reset pulse per LR2021 datasheet §5.1 */

	gpio_pin_set_dt(&ctx->reset, 0);

	k_msleep(300);  /* LR2021 firmware boot time per datasheet §5.1 */

	LOG_INF("LR20xx reset complete, BUSY=%d", gpio_pin_get_dt(&ctx->busy));

	ctx->radio_is_sleeping = false;

	return wait_on_busy(ctx);
}

lr20xx_hal_status_t lr20xx_hal_wakeup(const void *context)
{
	struct lr20xx_hal_context *ctx = (struct lr20xx_hal_context *)context;
	return check_device_ready(ctx);
}
