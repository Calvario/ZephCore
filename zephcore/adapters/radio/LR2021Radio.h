/*
 * SPDX-License-Identifier: MIT
 * ZephCore Radio adapter for LR2021 using Zephyr LoRa driver
 *
 * Thin wrapper around LoRaRadioBase — only hardware-specific hooks.
 */

#pragma once

#include "LoRaRadioBase.h"

namespace mesh {

class LR2021Radio : public LoRaRadioBase {
public:
	LR2021Radio(const struct device *lora_dev, MainBoard &board,
		    NodePrefs *prefs = nullptr);

	void begin() override;

	/* LoRa side detectors — LR2021-only multi-SF receive. */
	bool configSideDetectors(const uint8_t *sfs, uint8_t num) override;

	/* Carrier frequency error accumulated from received packets.  LR2021
	 * only: driver v2.0.2 decodes it per packet, no other radio here
	 * reports it. */
	int formatFreqErrorStatus(char *buf, int cap) override;

	/* Also clears the frequency-error accumulator, so `clear stats` gives a
	 * clean baseline after changing frequency or swapping a module. */
	void resetStats() override;

protected:
	/* Hardware primitives */
	bool hwConfigure(const struct lora_modem_config &cfg) override;
	void hwCancelReceive() override;
	int hwSendAsync(uint8_t *buf, uint32_t len,
			struct k_poll_signal *sig) override;
	int16_t hwGetCurrentRSSI() override;
	bool hwIsReceiving() override;
	void hwSetRxBoost(bool enable) override;
	int hwCadProbe(int8_t level) override;
	void hwCadSetPeakOffset(int8_t offset) override;
	uint8_t hwCadBasePeak() override;
	uint8_t hwCadPeakMin() override;
	uint8_t hwCadPeakMax() override;
	uint32_t hwWakeupTimeUs() override;
};

} /* namespace mesh */
