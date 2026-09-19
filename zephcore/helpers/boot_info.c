/*
 * SPDX-License-Identifier: MIT
 * Boot-time reset-cause capture. See boot_info.h.
 */

#include "boot_info.h"

#include <zephyr/init.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <stdio.h>

LOG_MODULE_REGISTER(zephcore_boot, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

static uint32_t s_reset_cause;
static bool s_reset_cause_valid;

/* POST_KERNEL: before main() on every role. */
static int boot_info_init(void)
{
	uint32_t cause = 0;

	if (hwinfo_get_reset_cause(&cause) != 0) {
		/* Nothing to capture; the accessor reports false. */
		return 0;
	}

	s_reset_cause = cause;
	s_reset_cause_valid = true;

	/* Logged here, not only in the role that renders it: the clear below
	 * would otherwise lose the cause on roles that never read it. */
	LOG_INF("Boot reset cause: 0x%08x", cause);

	/* Not reported again next boot. -ENOSYS where unimplemented (ESP32). */
	(void)hwinfo_clear_reset_cause();

	return 0;
}

SYS_INIT(boot_info_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

bool zephcore_boot_reset_cause(uint32_t *out)
{
	if (!s_reset_cause_valid) {
		return false;
	}

	*out = s_reset_cause;
	return true;
}

/*
 * One row per RESET_* bit in <zephyr/drivers/hwinfo.h>. A bit with no row is
 * rendered as nothing and is invisible to zephcore_boot_reset_cause_labelled().
 */
static const struct {
	uint32_t bit;
	const char *label;
} s_labels[] = {
	{ RESET_PIN,            "PIN" },
	{ RESET_SOFTWARE,       "SOFTWARE" },
	{ RESET_BROWNOUT,       "BROWNOUT" },
	{ RESET_POR,            "POR" },
	{ RESET_WATCHDOG,       "WATCHDOG" },
	{ RESET_DEBUG,          "DEBUG" },
	{ RESET_SECURITY,       "SECURITY" },
	{ RESET_LOW_POWER_WAKE, "LOWPOWER" },
	{ RESET_CPU_LOCKUP,     "LOCKUP" },
	{ RESET_PARITY,         "PARITY" },
	{ RESET_PLL,            "PLL" },
	{ RESET_CLOCK,          "CLOCK" },
	{ RESET_HARDWARE,       "HARDWARE" },
	{ RESET_USER,           "USER" },
	{ RESET_TEMPERATURE,    "TEMPERATURE" },
	{ RESET_BOOTLOADER,     "BOOTLOADER" },
	{ RESET_FLASH,          "FLASH" },
};

int zephcore_boot_reset_cause_str(char *buf, size_t cap)
{
	if (buf == NULL || cap == 0) {
		return 0;
	}

	buf[0] = '\0';

	if (!s_reset_cause_valid) {
		return 0;
	}

	size_t n = 0;

	for (size_t i = 0; i < ARRAY_SIZE(s_labels); i++) {
		if ((s_reset_cause & s_labels[i].bit) == 0) {
			continue;
		}

		int w = snprintf(buf + n, cap - n, " %s", s_labels[i].label);

		if (w < 0 || (size_t)w >= cap - n) {
			/* Stop rather than emit half a label. */
			buf[n] = '\0';
			break;
		}

		n += (size_t)w;
	}

	return (int)n;
}

uint32_t zephcore_boot_reset_cause_labelled(void)
{
	uint32_t mask = 0;

	if (!s_reset_cause_valid) {
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(s_labels); i++) {
		mask |= s_reset_cause & s_labels[i].bit;
	}

	return mask;
}
