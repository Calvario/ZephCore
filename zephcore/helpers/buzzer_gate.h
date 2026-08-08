/*
 * ZephCore - Notification mode (buzzer + vibration)
 * Copyright (c) 2026 ZephCore
 * SPDX-License-Identifier: MIT
 *
 * Always linked, like led_gate — the CLI needs these symbols on boards that
 * compile no buzzer at all.
 */

#ifndef ZEPHCORE_BUZZER_GATE_H
#define ZEPHCORE_BUZZER_GATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* User-facing mode, as accepted by "set buzzer". */
#define ZEPHCORE_BUZZER_OFF     0  /* silent */
#define ZEPHCORE_BUZZER_ON      1  /* sound + vibration */
#define ZEPHCORE_BUZZER_VIBRATE 2  /* vibration only */
#define ZEPHCORE_BUZZER_SOUND   3  /* sound only */
#define ZEPHCORE_BUZZER_MODE_MAX 3

/*
 * NodePrefs still stores the original buzzer_quiet byte: 0 = not quiet,
 * 1 = quiet. Vibration-only and sound-only are the new values 2 and 3, which
 * older firmware reads as "quiet" — so a downgrade silences a node left on
 * sound-only, which is the safe direction. Conversion lives here so the two
 * numberings never leak into the rest of the tree.
 */
uint8_t zephcore_buzzer_mode_from_prefs(uint8_t prefs_byte);
uint8_t zephcore_buzzer_prefs_from_mode(uint8_t mode);

/* ON -> VIBRATE -> OFF -> SOUND -> ON. With no motor fitted only ON and OFF
 * are reachable — the other two would be indistinguishable from them. */
uint8_t zephcore_buzzer_next_mode(uint8_t mode);

/** @return true if the mode lets the buzzer make noise */
bool zephcore_buzzer_mode_audible(uint8_t mode);

/** @return short label for a mode, for the CLI and the display page */
const char *zephcore_buzzer_mode_name(uint8_t mode);

/** @return the mode currently in effect */
uint8_t zephcore_buzzer_mode(void);

/**
 * Record a mode and apply it to the hardware.
 * @param deferred when muting, let an in-flight melody finish first
 */
void zephcore_buzzer_set_mode(uint8_t mode, bool deferred);

/* Hardware half of zephcore_buzzer_set_mode(). Weak no-op with no buzzer;
 * overridden in helpers/ui/buzzer.c. Call the setter, not this. */
void zephcore_buzzer_apply(uint8_t mode, bool deferred);

/** @return true if the board has a vibration motor. Weak false. */
bool zephcore_buzzer_has_vibrate(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHCORE_BUZZER_GATE_H */
