/*
 * ZephCore - QSPI flash bring-up probe
 * Copyright (c) 2026 ZephCore
 * SPDX-License-Identifier: MIT
 *
 * Zephyr's nrf_qspi_nor driver throws away the reason it failed: qspi_init()
 * does `rc = nrfx_qspi_init() / exit_dpd() / qspi_rdid(); if (rc < 0) return
 * rc;` and only a JEDEC *mismatch* is logged. A flash that never answers looks
 * exactly like one that was never probed, and /ext just fails with -ENODEV.
 *
 * Two things are recovered here. The init return code is not actually lost —
 * a failed init is recorded in the device state as a positive errno, which
 * says which of those three steps broke. And RDID (0x9F) is re-sent by
 * bit-banging the QSPI pins as ordinary single-lane SPI, which needs nothing
 * from the QSPI peripheral and so still answers when the driver has given up:
 *   c8 40 17  - the expected GD25Q64E, so the fault is driver-side
 *   00 00 00  - pins reach the part but it is not driving (unpowered? early?)
 *   ff ff ff  - nothing on the bus at all (wrong pins / no part)
 *
 * Enable with CONFIG_ZEPHCORE_QSPI_RDID_PROBE.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>

#if IS_ENABLED(CONFIG_ZEPHCORE_QSPI_RDID_PROBE) && \
	DT_HAS_COMPAT_STATUS_OKAY(nordic_qspi_nor)

LOG_MODULE_REGISTER(qspi_probe, CONFIG_FLASH_LOG_LEVEL);

/* Pin numbers, not gpio-specs: these live in the board's qspi pinctrl group,
 * which cannot be referenced as a phandle. Defaults match the MeshTracker X1. */
#define P_SCK 19
#define P_CSN 20
#define P_IO0 21
#define P_IO1 22

static const struct device *p0;

static uint8_t spi_byte(uint8_t out)
{
	uint8_t in = 0;

	for (int b = 7; b >= 0; b--) {
		gpio_pin_set_raw(p0, P_IO0, (out >> b) & 1);
		k_busy_wait(1);
		gpio_pin_set_raw(p0, P_SCK, 1);   /* mode 0: sample on rising */
		in = (in << 1) | (gpio_pin_get_raw(p0, P_IO1) & 1);
		k_busy_wait(1);
		gpio_pin_set_raw(p0, P_SCK, 0);
	}
	return in;
}

static int qspi_rdid_probe(void)
{
	const struct device *flash =
		DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(nordic_qspi_nor));
	uint8_t id[3];

	p0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	if (!device_is_ready(p0)) {
		return 0;
	}

	if (device_is_ready(flash)) {
		/* Bit-banging steals the QSPI pins as GPIOs and leaves them
		 * that way, which would break a flash that just came up fine.
		 * There is nothing to diagnose in that case, so stay off the
		 * bus and let this probe live in a build that works. */
		LOG_INF("qspi flash '%s' ready - /ext usable, RDID probe skipped",
			flash->name);
		return 0;
	}

	LOG_INF("qspi flash '%s' failed init (init_res=-%d) - bit-banging RDID on "
		"SCK=P0.%02d CSN=P0.%02d IO0=P0.%02d IO1=P0.%02d",
		flash->name, (int)flash->state->init_res,
		P_SCK, P_CSN, P_IO0, P_IO1);

#if DT_NODE_EXISTS(DT_NODELABEL(flash_power))
	/* The driver reading 00 00 00 says the part was not driving the bus,
	 * which is either "the rail never came up" or "it came up too late".
	 * Those need opposite fixes, and the supply state here separates them:
	 * enabled now but the JEDEC read failed means the rail is fine and the
	 * startup delay is short. */
	{
		const struct device *supply =
			DEVICE_DT_GET(DT_NODELABEL(flash_power));

		LOG_INF("flash supply '%s': ready=%d enabled=%d",
			supply->name, (int)device_is_ready(supply),
			device_is_ready(supply)
				? (int)regulator_is_enabled(supply) : -1);
	}
#endif

	gpio_pin_configure(p0, P_CSN, GPIO_OUTPUT_HIGH);
	gpio_pin_configure(p0, P_SCK, GPIO_OUTPUT_LOW);
	gpio_pin_configure(p0, P_IO0, GPIO_OUTPUT_LOW);
	gpio_pin_configure(p0, P_IO1, GPIO_INPUT);
	k_busy_wait(100);

	gpio_pin_set_raw(p0, P_CSN, 0);
	(void)spi_byte(0x9F);
	id[0] = spi_byte(0x00);
	id[1] = spi_byte(0x00);
	id[2] = spi_byte(0x00);
	gpio_pin_set_raw(p0, P_CSN, 1);

	LOG_INF("RDID raw: %02x %02x %02x  (expect c8 40 17 for GD25Q64E)",
		id[0], id[1], id[2]);
	return 0;
}

/* Deferred rather than run straight from SYS_INIT: at APPLICATION level this
 * fires before USB CDC enumerates, so the result went to a console nobody was
 * attached to yet — the same truncation that hides the driver's own JEDEC
 * error. Five seconds is comfortably past enumeration and long after the flash
 * driver and its supply regulator have had their turn. */
static void qspi_probe_work(struct k_work *work)
{
	ARG_UNUSED(work);
	qspi_rdid_probe();
}

static K_WORK_DELAYABLE_DEFINE(qspi_probe_dwork, qspi_probe_work);

static int qspi_probe_schedule(void)
{
	k_work_schedule(&qspi_probe_dwork, K_SECONDS(5));
	return 0;
}

SYS_INIT(qspi_probe_schedule, APPLICATION, 99);

#endif
