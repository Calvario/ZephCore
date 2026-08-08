/*
 * ZephCore - Haptic feedback
 * Copyright (c) 2026 ZephCore
 * SPDX-License-Identifier: MIT
 *
 * Vibration alerts on boards carrying a DRV2605 haptic driver. Every call
 * is a no-op on boards without one.
 */

#ifndef ZEPHCORE_HAPTIC_H
#define ZEPHCORE_HAPTIC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the haptic driver from devicetree (ti,drv2605).
 * @return 0 on success, -ENODEV if the board has no haptic driver
 */
int haptic_init(void);

/** Play a short notification buzz. No-op if disabled or uninitialized. */
void haptic_pulse(void);

/** Enable or disable vibration. Driven by the notification mode. */
void haptic_set_enabled(bool enabled);

/** @return true if the board has a working vibration motor */
bool haptic_available(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHCORE_HAPTIC_H */
