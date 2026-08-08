/*
 * ZephCore - Notification mode (buzzer + vibration)
 * Copyright (c) 2026 ZephCore
 * SPDX-License-Identifier: MIT
 */

#include "buzzer_gate.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/* Written from the main thread (boot / CLI) and the UI task, read by the
 * render path — same split as led_gate. */
static atomic_t s_mode = ATOMIC_INIT(ZEPHCORE_BUZZER_ON);

uint8_t zephcore_buzzer_mode(void)
{
	return (uint8_t)atomic_get(&s_mode);
}

void zephcore_buzzer_set_mode(uint8_t mode, bool deferred)
{
	if (mode > ZEPHCORE_BUZZER_MODE_MAX) {
		mode = ZEPHCORE_BUZZER_ON;
	}
	atomic_set(&s_mode, (atomic_val_t)mode);
	zephcore_buzzer_apply(mode, deferred);
}

uint8_t zephcore_buzzer_mode_from_prefs(uint8_t prefs_byte)
{
	switch (prefs_byte) {
	case 0:  return ZEPHCORE_BUZZER_ON;
	case 2:  return ZEPHCORE_BUZZER_VIBRATE;
	case 3:  return ZEPHCORE_BUZZER_SOUND;
	default: return ZEPHCORE_BUZZER_OFF;
	}
}

uint8_t zephcore_buzzer_prefs_from_mode(uint8_t mode)
{
	switch (mode) {
	case ZEPHCORE_BUZZER_ON:      return 0;
	case ZEPHCORE_BUZZER_VIBRATE: return 2;
	case ZEPHCORE_BUZZER_SOUND:   return 3;
	default:                      return 1;
	}
}

uint8_t zephcore_buzzer_next_mode(uint8_t mode)
{
	if (!zephcore_buzzer_has_vibrate()) {
		/* VIBRATE is dead and SOUND is just ON, so it is a plain toggle */
		return (mode == ZEPHCORE_BUZZER_ON) ? ZEPHCORE_BUZZER_OFF
						    : ZEPHCORE_BUZZER_ON;
	}

	switch (mode) {
	case ZEPHCORE_BUZZER_ON:      return ZEPHCORE_BUZZER_VIBRATE;
	case ZEPHCORE_BUZZER_VIBRATE: return ZEPHCORE_BUZZER_OFF;
	case ZEPHCORE_BUZZER_OFF:     return ZEPHCORE_BUZZER_SOUND;
	default:                      return ZEPHCORE_BUZZER_ON;
	}
}

bool zephcore_buzzer_mode_audible(uint8_t mode)
{
	return mode == ZEPHCORE_BUZZER_ON || mode == ZEPHCORE_BUZZER_SOUND;
}

const char *zephcore_buzzer_mode_name(uint8_t mode)
{
	switch (mode) {
	case ZEPHCORE_BUZZER_ON:      return "sound+vib";
	case ZEPHCORE_BUZZER_VIBRATE: return "vibrate";
	case ZEPHCORE_BUZZER_SOUND:   return "sound";
	default:                      return "silent";
	}
}

/* Overridden in helpers/ui/buzzer.c on boards that have a buzzer. */
__weak void zephcore_buzzer_apply(uint8_t mode, bool deferred)
{
	ARG_UNUSED(mode);
	ARG_UNUSED(deferred);
}

__weak bool zephcore_buzzer_has_vibrate(void)
{
	return false;
}
