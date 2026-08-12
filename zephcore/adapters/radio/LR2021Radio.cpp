/*
 * SPDX-License-Identifier: MIT
 * LR2021 hardware hooks for LoRaRadioBase.
 */

#include "LR2021Radio.h"
#include <zephyr/kernel.h>

/* LR20xx driver extension API */
extern "C" {
#include "lr20xx_lora.h"
}

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lr2021_radio, CONFIG_ZEPHCORE_LORA_LOG_LEVEL);

namespace mesh {

K_THREAD_STACK_DEFINE(lr20xx_tx_wait_stack, TX_WAIT_THREAD_STACK_SIZE);

LR2021Radio::LR2021Radio(const struct device *lora_dev, MainBoard &board,
			 NodePrefs *prefs)
	: LoRaRadioBase(lora_dev, board, prefs)
{
}

void LR2021Radio::begin()
{
	startTxThread(lr20xx_tx_wait_stack,
		      K_THREAD_STACK_SIZEOF(lr20xx_tx_wait_stack));
	LoRaRadioBase::begin();
}

bool LR2021Radio::configSideDetectors(const uint8_t *sfs, uint8_t num)
{
	/* The driver validates against the configured SF/BW, so this must run
	 * after the modem is configured — which it is: the CLI path only
	 * reaches here on a live radio. */
	int ret = lr20xx_configure_side_detectors(_dev, sfs, num);

	if (ret < 0) {
		LOG_WRN("side detector config rejected: %d", ret);
		return false;
	}
	return true;
}

/* ── Hardware primitives ──────────────────────────────────────────────── */

bool LR2021Radio::hwConfigure(const struct lora_modem_config &cfg)
{
	int ret = lora_config(_dev, const_cast<struct lora_modem_config *>(&cfg));
	if (ret < 0) {
		LOG_ERR("lora_config failed: %d", ret);
		return false;
	}
	return true;
}

void LR2021Radio::hwCancelReceive()
{
	lora_recv_async(_dev, NULL, NULL);
}

int LR2021Radio::hwSendAsync(uint8_t *buf, uint32_t len,
			     struct k_poll_signal *sig)
{
	return lora_send_async(_dev, buf, len, sig);
}

int16_t LR2021Radio::hwGetCurrentRSSI()
{
	return lr20xx_get_rssi_inst(_dev);
}

/* Deliberately no hwIsChipBusy() override, matching the LR11xx.
 *
 * It backs LoRaRadioBase::isRadioReady(), which gates TX in startSendRaw() as
 * well as the probes.  Wiring BUSY into the transmit path stalled sends until
 * the dispatcher's 4 s CAD timeout, because the event-driven loop has nothing
 * to re-wake a deferred send.  The duty-cycle sleep window is handled where it
 * belongs instead: skipped in the two incidental pollers
 * (lr20xx_is_receiving, lr20xx_get_rssi_inst) and stood down deliberately for
 * TX inside the driver. */

bool LR2021Radio::hwIsReceiving()
{
	/* The poll itself is non-destructive: lr20xx_is_receiving() reads the
	 * IRQ register via get_status(), which clears nothing.
	 *
	 * Foreign-preamble release is NOT hardware-driven, whatever the comment
	 * that stood here (copy-pasted from LR1110Radio.cpp) claimed.  DS §5.7:
	 * IRQ status bits are latched until ClearIrq, and continuous RX has no
	 * timeout — so the driver releases them in software, on an SF-aware
	 * grace for the preamble and a max-airtime deadline for the header,
	 * exactly as the LR11xx and SX126x drivers do. */
	return lr20xx_is_receiving(_dev);
}

void LR2021Radio::hwSetRxBoost(bool enable)
{
	lr20xx_set_rx_boost(_dev, enable);
}

int LR2021Radio::hwCadProbe(int8_t level)
{
	return lr20xx_cad_probe(_dev, level);
}

void LR2021Radio::hwCadSetPeakOffset(int8_t offset)
{
	lr20xx_cad_set_peak_offset(_dev, offset);
}

uint8_t LR2021Radio::hwCadBasePeak()
{
	return lr20xx_cad_base_peak(_dev);
}

uint32_t LR2021Radio::hwWakeupTimeUs()
{
	/* Per-device, because the TCXO term dominates and is board-specific:
	 * meshtracker_x1 declares tcxo-startup-delay-ms = <5>, promicro_lr2021
	 * is XTAL-only.  Inheriting the base class's flat 1500 us under-counted
	 * the X1's real deaf time by ~4.6 ms, which oversized the duty-cycle
	 * sleep and dropped window-edge preambles regardless of signal strength.
	 * Same shape as the SX126x and LR11xx overrides. */
	return lr20xx_get_wakeup_time_us(_dev);
}

} /* namespace mesh */
