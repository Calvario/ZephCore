/*
 * SPDX-License-Identifier: MIT
 * Adaptive Power Control — echo-based TX power reduction
 */

#include <mesh/PowerController.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zephcore_apc, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

/* SNR demodulation thresholds per SF (x4 fixed point, SX126x DS table) */
static constexpr int8_t snr_threshold_x4[] = {
	-10, /* SF5:  -2.5 dB */
	-20, /* SF6:  -5.0 dB */
	-30, /* SF7:  -7.5 dB */
	-40, /* SF8: -10.0 dB */
	-50, /* SF9: -12.5 dB */
	-60, /* SF10: -15.0 dB */
	-70, /* SF11: -17.5 dB */
	-80, /* SF12: -20.0 dB */
};

namespace mesh {

PowerController::PowerController()
	: _next_idx(0), _margin_ema_x256(0), _finalized_count(0),
	  _last_echo_ms(0), _echo_count(0), _noecho_count(0), _noecho_streak(0),
	  _power_reduction_db(0), _enabled(true),
	  _sf(8), _last_source_count(0), _target_margin_x4(DEFAULT_TARGET_MARGIN_X4)
{
	memset(_ring, 0, sizeof(_ring));
}

void PowerController::setEnabled(bool en)
{
	if (_enabled == en) return;
	_enabled = en;
	if (!_enabled) {
		/* Drop APC runtime state while disabled so no tracking work runs. */
		memset(_ring, 0, sizeof(_ring));
		_next_idx = 0;
		_margin_ema_x256 = 0;
		_finalized_count = 0;
		_last_echo_ms = 0;
		_echo_count = 0;
		_noecho_count = 0;
		_noecho_streak = 0;
		_last_source_count = 0;
		_power_reduction_db = 0;
	}
}

int8_t PowerController::sfThresholdX4(uint8_t sf)
{
	int idx = (int)sf - 5;
	if (idx < 0) idx = 0;
	if (idx > 7) idx = 7;
	return snr_threshold_x4[idx];
}

int PowerController::findEntry(uint32_t hash32) const
{
	for (int i = 0; i < RING_SIZE; i++) {
		if (_ring[i].active && _ring[i].hash32 == hash32) {
			return i;
		}
	}
	return -1;
}

void PowerController::trackTransmit(uint32_t hash32, uint32_t now_ms, uint8_t path_pos)
{
	if (!_enabled) return;

	/* If ring slot is occupied, finalize it first */
	if (_ring[_next_idx].active) {
		finalizeEntry(_next_idx);
	}

	EchoEntry &e = _ring[_next_idx];
	e.hash32 = hash32;
	e.timestamp_ms = now_ms;
	e.source_count = 0;
	e.sf_at_track = _sf;
	e.path_pos = path_pos;
	e.reduction_at_track = _power_reduction_db;
	memset(e.sources, 0, sizeof(e.sources));
	e.active = true;

	_next_idx = (_next_idx + 1) % RING_SIZE;
}

bool PowerController::recordEcho(uint32_t hash32, int8_t snr_x4, uint32_t now_ms,
				 const uint8_t *path, uint8_t path_count,
				 uint8_t hash_size, const uint8_t *self_hash)
{
	if (!_enabled) return false;

	int idx = findEntry(hash32);
	if (idx < 0) return false;

	EchoEntry &e = _ring[idx];

	/* Check if entry has expired */
	if (now_ms - e.timestamp_ms > ECHO_WINDOW_MS) {
		finalizeEntry(idx);
		return false;
	}

	/* True-echo gating: for forwarded floods, only a dupe carrying our
	 * hash at the position we appended it proves our TX was received.
	 * Parallel retransmits of the origin's copy — including dupes heard
	 * before our own TX even airs — don't route through us and say
	 * nothing about our reach. */
	if (e.path_pos != PATH_POS_ORIGINATED) {
		if (path_count <= e.path_pos) return false;
		if (memcmp(&path[(size_t)e.path_pos * hash_size], self_hash,
			   hash_size) != 0) {
			return false;
		}
	}

	/* Echo source = the node that transmitted this copy = last path
	 * entry (every retransmitting node appends itself; nodes that
	 * can't append don't retransmit). */
	uint8_t src_hash = (path_count > 0)
			   ? path[(size_t)(path_count - 1) * hash_size] : 0;

	/* Re-contact after a stale gap: the EMA predates the gap and must
	 * be re-earned before it can drive reduction again. */
	if (_last_echo_ms != 0 && now_ms - _last_echo_ms > STALE_MS) {
		LOG_INF("APC: echo after stale gap, re-warming margin estimate");
		_margin_ema_x256 = 0;
		_finalized_count = 0;
	}

	/* Update existing source or add new one */
	for (int i = 0; i < e.source_count; i++) {
		if (e.sources[i].hash == src_hash) {
			if (snr_x4 > e.sources[i].snr_x4) {
				e.sources[i].snr_x4 = snr_x4;
			}
			_last_echo_ms = now_ms;
			return true;
		}
	}

	if (e.source_count < MAX_SOURCES) {
		e.sources[e.source_count].hash = src_hash;
		e.sources[e.source_count].snr_x4 = snr_x4;
		e.source_count++;
	}

	_last_echo_ms = now_ms;
	return true;
}

int8_t PowerController::computeRobustSNR(const EchoEntry &entry) const
{
	if (entry.source_count == 0) {
		/* Not reached in practice — finalizeEntry handles no-echo. */
		return sfThresholdX4(entry.sf_at_track);
	}

	if (entry.source_count == 1) {
		return entry.sources[0].snr_x4;
	}

	/* 2-3 sources: sort descending, then cluster + rogue filter */
	int8_t sorted[MAX_SOURCES];
	int n = entry.source_count;
	for (int i = 0; i < n; i++) {
		sorted[i] = entry.sources[i].snr_x4;
	}
	/* Simple insertion sort (max 3 elements) */
	for (int i = 1; i < n; i++) {
		int8_t key = sorted[i];
		int j = i - 1;
		while (j >= 0 && sorted[j] < key) {
			sorted[j + 1] = sorted[j];
			j--;
		}
		sorted[j + 1] = key;
	}

	/* Count how many are within CLUSTER_WIDTH of the best */
	int cluster_count = 1;
	for (int i = 1; i < n; i++) {
		if (sorted[0] - sorted[i] <= CLUSTER_WIDTH_X4) {
			cluster_count++;
		}
	}

	if (cluster_count >= 2) {
		/* 2+ in cluster: median of the cluster values */
		/* For 2 values: average. For 3 values: middle one. */
		if (cluster_count == 2) {
			return (int8_t)(((int)sorted[0] + (int)sorted[1]) / 2);
		}
		/* cluster_count == 3 (all 3 within 6 dB) */
		return sorted[1]; /* median */
	}

	/* Only 1 in top cluster → rogue. Drop it, use next. */
	if (n >= 3 && sorted[1] - sorted[2] <= CLUSTER_WIDTH_X4) {
		/* sources[1] and [2] cluster together — median them */
		return (int8_t)(((int)sorted[1] + (int)sorted[2]) / 2);
	}
	/* Fall back to second-best */
	return sorted[1];
}

void PowerController::finalizeEntry(int idx)
{
	if (!_ring[idx].active) return;

	EchoEntry &e = _ring[idx];
	_last_source_count = e.source_count;

	int32_t margin_x4;
	if (e.source_count == 0) {
		/* Nobody downstream decoded this TX.  At the reduction R in
		 * effect when it was sent, that bounds the full-power margin:
		 * margin - R <= 0  =>  margin <= R.  Encode the least
		 * pessimistic consistent value (R) so occasional misses
		 * (collision, RX duty cycle) correct gently; the no-echo
		 * streak below handles real link loss hard. */
		margin_x4 = (int32_t)e.reduction_at_track * 4;
		_noecho_count++;
		_noecho_streak++;
	} else {
		int8_t robust_snr = computeRobustSNR(e);
		margin_x4 = (int32_t)robust_snr
			  - (int32_t)sfThresholdX4(e.sf_at_track);
		_echo_count++;
		_noecho_streak = 0;
	}

	/* margin_x4 is in x4 units. Convert to x256 for EMA. */
	int32_t sample_x256 = margin_x4 << 6; /* x4 * 64 = x256 */
	int32_t diff = sample_x256 - _margin_ema_x256;

	if (_finalized_count < WARMUP_COUNT) {
		/* During warmup, seed the EMA faster */
		if (_finalized_count == 0) {
			_margin_ema_x256 = sample_x256;
		} else {
			_margin_ema_x256 += diff >> 1;
		}
	} else {
		/* Normal EMA update: ema += (sample - ema) >> shift */
		_margin_ema_x256 += diff >> EMA_SHIFT;
	}

	_finalized_count++;
	e.active = false;

	/* Fast recovery: consecutive unechoed transmissions while reduced
	 * mean the EMA no longer reflects reality — restore full power NOW
	 * and re-earn the reduction from fresh samples. */
	if (_noecho_streak >= NOECHO_TRIP_COUNT && _power_reduction_db > 0) {
		LOG_INF("APC: %u consecutive no-echo TX, restoring full power",
			(unsigned)_noecho_streak);
		_power_reduction_db = 0;
		_margin_ema_x256 = 0;
		_finalized_count = 0;
		_noecho_streak = 0;
	}

	LOG_DBG("APC finalize: sources=%d margin=%.1f ema=%.1f",
		(int)_last_source_count,
		(double)(margin_x4 / 4.0f),
		(double)getMarginEstimate());
}

void PowerController::tick(uint32_t now_ms)
{
	if (!_enabled) return;

	/* Finalize expired entries */
	for (int i = 0; i < RING_SIZE; i++) {
		if (_ring[i].active && now_ms - _ring[i].timestamp_ms > ECHO_WINDOW_MS) {
			finalizeEntry(i);
		}
	}

	if (!isWarmedUp()) return;

	int8_t old_reduction = _power_reduction_db;

	/* Staleness takes priority: ramp back to full power if no echoes.
	 * When stale, never increase reduction — old EMA data is unreliable. */
	if (isStale(now_ms)) {
		if (_power_reduction_db > 0) {
			_power_reduction_db -= STEP_UP_DB;
			if (_power_reduction_db < 0) {
				_power_reduction_db = 0;
			}
		}
	} else {
		/* Compensated control: echo SNR is measured from the
		 * neighbor's fixed-power TX and does NOT respond to our own
		 * reduction, so subtract it ourselves (path reciprocity).
		 * excess = margin the neighbor has ABOVE target at our
		 * current reduced power; regulate it to the deadband. */
		int32_t excess_x256 = _margin_ema_x256
				    - ((int32_t)_target_margin_x4 << 6)
				    - ((int32_t)_power_reduction_db << 8);
		int32_t hyst_x256 = HYSTERESIS_X4 << 6;

		if (excess_x256 > hyst_x256) {
			int step = (int)(excess_x256 >> 8);
			if (step > STEP_DOWN_DB) step = STEP_DOWN_DB;
			_power_reduction_db += step;
			if (_power_reduction_db > MAX_REDUCTION_DB) {
				_power_reduction_db = MAX_REDUCTION_DB;
			}
		} else if (excess_x256 < -hyst_x256) {
			int step = (int)((-excess_x256) >> 8);
			if (step > STEP_UP_DB) step = STEP_UP_DB;
			_power_reduction_db -= step;
			if (_power_reduction_db < 0) {
				_power_reduction_db = 0;
			}
		}
	}

	if (_power_reduction_db != old_reduction) {
		LOG_INF("APC: reduction %d -> %d dBm (margin=%.1f)",
			(int)old_reduction, (int)_power_reduction_db,
			(double)getMarginEstimate());
	}
}

float PowerController::getMarginEstimate() const
{
	return (float)_margin_ema_x256 / 256.0f;
}

bool PowerController::isStale(uint32_t now_ms) const
{
	if (_last_echo_ms == 0) return false;
	return now_ms - _last_echo_ms > STALE_MS;
}

} /* namespace mesh */
