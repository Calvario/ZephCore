/*
 * SPDX-License-Identifier: MIT
 *
 * MeshTracker X1 high-voltage LiPo OCV curve — 21 points at 5% steps.
 * 11-point base linearly interpolated to 5% resolution.
 * Index 0 = 100% (4290 mV), index 20 = 0% (3100 mV).
 *
 * 4.37 V cut-off cell, so the top of the curve sits ~100 mV above a
 * conventional 4.2 V pack.
 */

#include "battery_curve.h"

static const uint16_t ocv_meshtracker_x1[21] = {
	4290, 4204, 4118, 4060, 4002, /* 100 .. 80% */
	3946, 3889, 3844, 3798, 3763, /*  75 .. 55% */
	3728, 3703, 3677, 3660, 3643, /*  50 .. 30% */
	3619, 3594, 3547, 3500, 3300, /*  25 ..  5% */
	3100,                          /*   0%       */
};

const battery_curve_t battery_curve_default = {
	.ocv_mv     = ocv_meshtracker_x1,
	.num_points = 21,
	.num_cells  = 1,
};
