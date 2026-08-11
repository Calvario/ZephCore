/*
 * SPDX-License-Identifier: MIT
 * LR20xx Zephyr LoRa driver — extension API
 *
 * Functions extending the standard Zephyr lora_driver_api with
 * LR20xx-specific features (RX boost, RSSI readout, preamble detection,
 * duty cycle, AGC reset).
 */

#ifndef LR20XX_LORA_H
#define LR20XX_LORA_H

#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get instantaneous RSSI (for noise floor calibration)
 *
 * @param dev LoRa device
 * @return RSSI in dBm, or -128 on error
 */
int16_t lr20xx_get_rssi_inst(const struct device *dev);

/**
 * @brief Check if radio is actively receiving a packet
 *
 * Checks IRQ status for preamble/sync word detection.
 * Uses non-blocking mutex — returns false if SPI is busy.
 *
 * @param dev LoRa device
 * @return true if preamble or sync word detected
 */
bool lr20xx_is_receiving(const struct device *dev);

/**
 * @brief Enable/disable RX boosted mode
 *
 * @param dev LoRa device
 * @param enable true to enable boost
 */
void lr20xx_set_rx_boost(const struct device *dev, bool enable);

/**
 * @brief GPIO-only check for "chip is in a duty-cycle sleep window" (no SPI)
 *
 * True only while an RX duty cycle is armed and BUSY is high.  Deliberately
 * not raw BUSY: this chip holds BUSY high during continuous RX too, so a raw
 * reading would report the radio permanently unavailable.
 *
 * @param dev LoRa device
 * @return true if the host should not issue commands right now
 */
bool lr20xx_is_chip_busy(const struct device *dev);

/**
 * @brief Configure LoRa side detectors (multi-SF receive)
 *
 * Programs up to 3 extra spreading factors to be demodulated concurrently
 * with the configured one, on the same bandwidth.  Applies immediately and
 * is re-applied on every RX entry (the chip drops the set on each
 * SetModulationParams, and LBT CAD has to switch it off — the datasheet's
 * SF constraints for CAD are the inverse of those for RX).
 *
 * Datasheet constraints, enforced here: every side SF must be greater than
 * the configured SF, all SFs distinct, highest - lowest <= 4, and at
 * BW >= 500 kHz at most 2 side detectors (1 when the main SF is >= 10).
 *
 * @param dev LoRa device
 * @param sfs Array of side spreading factors (5..12)
 * @param num Number of side detectors, 0..3 (0 disables the feature)
 * @return 0 on success, -EINVAL if the set violates a chip constraint
 */
int lr20xx_configure_side_detectors(const struct device *dev,
				    const uint8_t *sfs, uint8_t num);

/**
 * @brief Get a random number from the radio hardware RNG
 *
 * @param dev LoRa device
 * @return Random 32-bit value
 */
uint32_t lr20xx_get_random(const struct device *dev);

/**
 * @brief Reset AGC by performing warm sleep + full recalibration
 *
 * @param dev LoRa device
 */
void lr20xx_reset_agc(const struct device *dev);

/**
 * @brief Set the adaptive-CAD operating detPeak offset
 *
 * Signed delta applied to the per-SF base cadDetPeak on every LBT CAD.
 * Takes effect on the next CAD — no reconfigure needed.  Clamped
 * in-driver to the LR20xx scale (48-90).
 *
 * @param dev    LoRa device
 * @param offset Signed offset from the base table value
 */
void lr20xx_cad_set_peak_offset(const struct device *dev, int8_t offset);

/**
 * @brief Per-SF base cadDetPeak for the currently configured SF
 *
 * @param dev LoRa device
 * @return Base detPeak (56-68 on this family)
 */
uint8_t lr20xx_cad_base_peak(const struct device *dev);

/**
 * @brief Run one blocking calibration CAD at base detPeak + peak_offset
 *
 * Uses the operating modem config (SF/BW/symbol count).  Leaves the chip
 * in STANDBY — the caller must restart RX afterwards.  Mesh loop thread
 * only.
 *
 * @param dev         LoRa device
 * @param peak_offset Signed offset from the base table value
 * @return 1 = activity detected, 0 = channel free, <0 = error
 */
int lr20xx_cad_probe(const struct device *dev, int8_t peak_offset);

#ifdef __cplusplus
}
#endif

#endif /* LR20XX_LORA_H */
