/*
 * SPDX-License-Identifier: MIT
 *
 * Wio Tracker L1 Pro 1W battery curve — single-cell LiPo, 21 points at 5% steps.
 *
 * Inherited verbatim from boards/nrf52840/wio_tracker_l1. The divider and
 * multiplier are identical on the Pro 1W (BAT_CTL P0.04, VBAT P0.31,
 * ADC_MULTIPLIER 2.0 in both MeshCore's and Meshtastic's variants, matching
 * vbat-mv-multiplier = 7236 here), so the same curve applies -- but the
 * 4190 mV ceiling was OBSERVED on the stock L1, not on this board. The Pro 1W
 * charges through a BQ25616 rather than the stock part, so re-measure the
 * ceiling before treating the top of this table as this board's.
 *
 * The discharge shape below 4190 mV follows the generic LiPo profile.
 */

#include "battery_curve.h"

static const uint16_t ocv_wio_tracker_l1_1w[21] = {
	4190, 4120, 4050, 4020, 3990, /* 100 .. 80% */
	3940, 3890, 3845, 3800, 3760, /*  75 .. 55% */
	3720, 3675, 3630, 3580, 3530, /*  50 .. 30% */
	3475, 3420, 3360, 3300, 3200, /*  25 ..  5% */
	3100,                          /*   0%       */
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_wio_tracker_l1_1w,
	.num_points = 21,
	.num_cells  = 1,
};
