/*
 * SPDX-License-Identifier: MIT
 * Zephyr Environment & Power Sensors
 *
 * Auto-detects available sensors via Zephyr devicetree nodelabels.
 *
 * Environment sensors (temp/humidity/pressure/light):
 *   SHTC3, AHT20/DHT20/AM2301B, SHT4x, SHT3xD, BME280, BME680, BMP280, BMP388, LPS22HB, SPA06
 *   MCU die temperature as fallback (nordic,nrf-temp)
 *   Board-local analog sensors (seeed,t1000e-analog: NTC thermistor + photocell)
 *
 * Power monitors (voltage/current/power):
 *   INA219, INA3221, INA226, INA228, INA230, INA232, INA236, INA237
 */

#include "ZephyrEnvSensors.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zephcore_sensors, CONFIG_ZEPHCORE_SENSORS_LOG_LEVEL);

/* ========== Sensor Support ========== */
#if IS_ENABLED(CONFIG_SENSOR)
#define HAS_ENV_SENSORS 1
#include <zephyr/drivers/sensor.h>
#else
#define HAS_ENV_SENSORS 0
#endif

/* INA3221 channel selection attribute — defined in driver's private header
 * (zephyr/drivers/sensor/ti/ina3221/ina3221.h), replicated here to avoid
 * including private driver internals */
#define SENSOR_ATTR_INA3221_SELECTED_CHANNEL (SENSOR_ATTR_PRIV_START + 1)

/* ================================================================
 *  DEVICE_DT_GET_OR_NULL helper
 *
 *  Returns a const struct device* for a DT nodelabel if it exists
 *  in the board's devicetree, or NULL at compile time if it doesn't.
 *  This is the proper Zephyr pattern — device_get_binding() is deprecated.
 * ================================================================ */

/* ================================================================
 *  Environment Sensors
 * ================================================================ */

#if HAS_ENV_SENSORS
static const struct device *temp_humidity_dev = NULL;
static const struct device *pressure_dev = NULL;
static const struct device *light_dev = NULL;
static bool temp_dev_has_pressure = false;  /* BME280/BME680 also have pressure */
static bool env_available = false;

/* Is this sensor usable — bringing it up first if its node deferred init?
 *
 * A part behind a switched rail cannot be probed at POST_KERNEL. Regulators
 * come up at priority 75 and sensors at 90, typically microseconds later, and a
 * regulator-boot-on rail never applies its startup-delay-us (regulator_common_init
 * takes the refcount-only branch, so regulator_delay() never runs). Such a node
 * is marked zephyr,deferred-init and initialised from here instead, where the
 * rail has had the whole boot to settle. See the i2c0 comment in the
 * MeshTracker X1 DTS for the failure this prevents.
 *
 * Safe to call for every candidate on every board: do_device_init() marks a
 * device initialized even when its init function failed, so device_init()
 * answers -EALREADY for anything that already ran at POST_KERNEL and this
 * reduces to a plain device_is_ready() check. */
static bool sensor_ready(const struct device *dev)
{
	if (dev == NULL) {
		return false;
	}
	if (!device_is_ready(dev)) {
		(void)device_init(dev);
	}
	return device_is_ready(dev);
}
#endif

int env_sensors_init(void)
{
#if HAS_ENV_SENSORS
	const struct device *dev;

	/* === Temperature/Humidity sensors ===
	 * Priority order: dedicated temp/humidity first, then combo sensors.
	 * BME280/BME680 also provide pressure — tracked via temp_dev_has_pressure. */

	/* SHTC3 (e.g., RAK1901) */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(shtc3));
	if (sensor_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (SHTC3)", dev->name);
		goto check_pressure;
	}

	/* Aosong AHT20/DHT20/AM2301B — same chip family, three compatible strings */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(aht20));
	if (!sensor_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(dht20));
	}
	if (!sensor_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(am2301b));
	}
	if (sensor_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (AHT20/DHT20)", dev->name);
		goto check_pressure;
	}

	/* SHT4x */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sht4x));
	if (sensor_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (SHT4x)", dev->name);
		goto check_pressure;
	}

	/* SHT3xD */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sht3xd));
	if (sensor_ready(dev)) {
		temp_humidity_dev = dev;
		LOG_INF("Found temp/humidity sensor: %s (SHT3xD)", dev->name);
		goto check_pressure;
	}

	/* BME280 — temperature + humidity + pressure */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bme280));
	if (sensor_ready(dev)) {
		temp_humidity_dev = dev;
		temp_dev_has_pressure = true;
		LOG_INF("Found env sensor: %s (BME280 — temp/humidity/pressure)", dev->name);
		goto check_pressure;
	}

	/* BME680 — temperature + humidity + pressure (+ gas) */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bme680));
	if (sensor_ready(dev)) {
		temp_humidity_dev = dev;
		temp_dev_has_pressure = true;
		LOG_INF("Found env sensor: %s (BME680 — temp/humidity/pressure)", dev->name);
		goto check_pressure;
	}

check_pressure:
	/* === Pressure-only sensors ===
	 * Only needed if the temp/humidity sensor doesn't provide pressure. */
	if (!temp_dev_has_pressure) {
		/* LPS22HB (e.g., RAK1902) */
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(lps22hb));
		if (sensor_ready(dev)) {
			pressure_dev = dev;
			LOG_INF("Found pressure sensor: %s (LPS22HB)", dev->name);
			goto done;
		}

		/* BMP280 — pressure + temperature (lower priority as temp source) */
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bmp280));
		if (sensor_ready(dev)) {
			pressure_dev = dev;
			LOG_INF("Found pressure sensor: %s (BMP280)", dev->name);
			goto done;
		}

		/* BMP388 */
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(bmp388));
		if (sensor_ready(dev)) {
			pressure_dev = dev;
			LOG_INF("Found pressure sensor: %s (BMP388)", dev->name);
			goto done;
		}

		/* SPA06 — the two nodes are the same part at its two possible
		 * addresses; the one that isn't there fails its ID check. */
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spa06));
		if (!sensor_ready(dev)) {
			dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spa06_alt));
		}
		if (sensor_ready(dev)) {
			pressure_dev = dev;
			LOG_INF("Found pressure sensor: %s (SPA06)", dev->name);
			goto done;
		}
	}

done:
	/* === Board-local analog sensors ===
	 * Not on any bus — a thermistor and a photocell wired straight to the
	 * SoC's ADC, so there is nothing to probe and the node's presence in DT
	 * is the whole detection. Its thermistor is read last in
	 * env_sensors_read() and only fills in a temperature nothing else
	 * supplied — a dedicated part beats a thermistor inside the case. */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(t1000e_sensors));
	if (sensor_ready(dev)) {
		light_dev = dev;
		LOG_INF("Found analog sensors: %s (T1000-E NTC + photocell)", dev->name);
	}

	env_available = (temp_humidity_dev != NULL) || (pressure_dev != NULL) ||
			(light_dev != NULL);
	if (!env_available) {
		LOG_INF("No environment sensors found");
	}
#endif

	return 0;
}

bool env_sensors_available(void)
{
#if HAS_ENV_SENSORS
	return env_available;
#else
	return false;
#endif
}

int env_sensors_read(struct env_data *data)
{
	memset(data, 0, sizeof(*data));

#if HAS_ENV_SENSORS
	struct sensor_value val;
	int rc;

	/* === Read temperature/humidity sensor === */
	if (temp_humidity_dev) {
		rc = sensor_sample_fetch(temp_humidity_dev);
		if (rc == 0) {
			if (sensor_channel_get(temp_humidity_dev, SENSOR_CHAN_AMBIENT_TEMP, &val) == 0) {
				data->temperature_c = sensor_value_to_float(&val);
				data->has_temperature = true;
			}
			if (sensor_channel_get(temp_humidity_dev, SENSOR_CHAN_HUMIDITY, &val) == 0) {
				data->humidity_pct = sensor_value_to_float(&val);
				data->has_humidity = true;
			}
			/* BME280/BME680 also have pressure — read from same device */
			if (temp_dev_has_pressure) {
				if (sensor_channel_get(temp_humidity_dev, SENSOR_CHAN_PRESS, &val) == 0) {
					data->pressure_hpa = sensor_value_to_float(&val) * 10.0f;
					data->has_pressure = true;
				}
			}
		}
	}

	/* === Read dedicated pressure sensor (if not already from combo sensor) === */
	if (pressure_dev && !data->has_pressure) {
		if (pressure_dev != temp_humidity_dev) {
			sensor_sample_fetch(pressure_dev);
		}
		if (sensor_channel_get(pressure_dev, SENSOR_CHAN_PRESS, &val) == 0) {
			data->pressure_hpa = sensor_value_to_float(&val) * 10.0f;
			data->has_pressure = true;
		}
		/* Barometers carry a die temperature. It beats the MCU's own
		 * sensor as a fallback on boards with no dedicated temp part. */
		if (!data->has_temperature &&
		    sensor_channel_get(pressure_dev, SENSOR_CHAN_AMBIENT_TEMP, &val) == 0) {
			data->temperature_c = sensor_value_to_float(&val);
			data->has_temperature = true;
		}
	}

	/* === Board-local analog sensors (light, and thermistor as fallback) ===
	 * One fetch covers both channels — it switches the sensor rail, so
	 * splitting it would pay that cost twice. */
	if (light_dev) {
		if (sensor_sample_fetch(light_dev) == 0) {
			if (sensor_channel_get(light_dev, SENSOR_CHAN_LIGHT, &val) == 0) {
				data->luminosity = sensor_value_to_float(&val);
				data->has_luminosity = true;
			}
			/* Only where no bus sensor — nor a barometer's die
			 * channel above — already produced a temperature. */
			if (!data->has_temperature &&
			    sensor_channel_get(light_dev, SENSOR_CHAN_AMBIENT_TEMP, &val) == 0) {
				data->temperature_c = sensor_value_to_float(&val);
				data->has_temperature = true;
			}
		}
	}

	/* === MCU die temperature — always read when available ===
	 * Used as fallback when no external temp sensor, and always
	 * available via has_mcu_temperature for telemetry decisions. */
	const struct device *mcu_temp = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(temp));
	if (mcu_temp && device_is_ready(mcu_temp)) {
		if (sensor_sample_fetch(mcu_temp) == 0 &&
		    sensor_channel_get(mcu_temp, SENSOR_CHAN_DIE_TEMP, &val) == 0) {
			data->mcu_temperature_c = sensor_value_to_float(&val);
			data->has_mcu_temperature = true;
		}
	}

	return (data->has_temperature || data->has_humidity || data->has_pressure ||
	        data->has_luminosity || data->has_mcu_temperature) ? 0 : -ENODATA;
#else
	return -ENOTSUP;
#endif
}

/* ================================================================
 *  Power Monitor Sensors (INA family)
 *
 *  Three Zephyr driver families:
 *  - INA219  (ti,ina219)  — standalone, 1 channel
 *  - INA3221 (ti,ina3221) — standalone, 3 channels with channel selection
 *  - ina2xx  (ti,ina226/228/230/232/236/237) — unified driver, 1 channel
 *
 *  All use: SENSOR_CHAN_VOLTAGE, SENSOR_CHAN_CURRENT, SENSOR_CHAN_POWER
 * ================================================================ */

#if HAS_ENV_SENSORS

enum ina_type { INA_NONE, INA_219, INA_3221, INA_2XX };

static const struct device *ina_dev = NULL;
static enum ina_type ina_found = INA_NONE;
static uint8_t ina_num_channels = 0;
static bool ina_available = false;

#endif /* HAS_ENV_SENSORS */

int power_sensors_init(void)
{
#if HAS_ENV_SENSORS
	const struct device *dev;

	/* INA3221 — 3-channel power monitor (check first — most channels) */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina3221));
	if (sensor_ready(dev)) {
		ina_dev = dev;
		ina_found = INA_3221;
		ina_num_channels = 3;
		ina_available = true;
		LOG_INF("Found power monitor: %s (INA3221, 3 channels)", dev->name);
		return 0;
	}

	/* INA219 — standalone single-channel */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina219));
	if (sensor_ready(dev)) {
		ina_dev = dev;
		ina_found = INA_219;
		ina_num_channels = 1;
		ina_available = true;
		LOG_INF("Found power monitor: %s (INA219)", dev->name);
		return 0;
	}

	/* ina2xx unified family — try all supported variants */
	dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina226));
	if (!sensor_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina228));
	}
	if (!sensor_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina230));
	}
	if (!sensor_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina232));
	}
	if (!sensor_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina236));
	}
	if (!sensor_ready(dev)) {
		dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina237));
	}
	if (sensor_ready(dev)) {
		ina_dev = dev;
		ina_found = INA_2XX;
		ina_num_channels = 1;
		ina_available = true;
		LOG_INF("Found power monitor: %s (ina2xx)", dev->name);
		return 0;
	}

	LOG_INF("No power monitors found");
#endif

	return 0;
}

bool power_sensors_available(void)
{
#if HAS_ENV_SENSORS
	return ina_available;
#else
	return false;
#endif
}

int power_sensors_read(struct power_data *data)
{
	memset(data, 0, sizeof(*data));

#if HAS_ENV_SENSORS
	if (!ina_available || !ina_dev) {
		return -ENODEV;
	}

	struct sensor_value val;

	if (ina_found == INA_3221) {
		/* INA3221: iterate channels 1-3, select each before reading */
		data->num_channels = ina_num_channels;
		for (int ch = 0; ch < ina_num_channels; ch++) {
			struct sensor_value sel;
			sel.val1 = ch + 1;  /* INA3221 channels are 1-indexed */
			sel.val2 = 0;

			int rc = sensor_attr_set(ina_dev, SENSOR_CHAN_ALL,
			                         (enum sensor_attribute)SENSOR_ATTR_INA3221_SELECTED_CHANNEL,
			                         &sel);
			if (rc != 0) {
				continue;
			}

			rc = sensor_sample_fetch(ina_dev);
			if (rc != 0) {
				continue;
			}

			if (sensor_channel_get(ina_dev, SENSOR_CHAN_VOLTAGE, &val) == 0) {
				data->channels[ch].voltage_v = sensor_value_to_float(&val);
			}
			if (sensor_channel_get(ina_dev, SENSOR_CHAN_CURRENT, &val) == 0) {
				data->channels[ch].current_a = sensor_value_to_float(&val);
			}
			if (sensor_channel_get(ina_dev, SENSOR_CHAN_POWER, &val) == 0) {
				data->channels[ch].power_w = sensor_value_to_float(&val);
			}
			data->channels[ch].valid = true;
		}
	} else {
		/* INA219 / ina2xx unified: single-channel, straightforward read */
		data->num_channels = 1;
		int rc = sensor_sample_fetch(ina_dev);
		if (rc != 0) {
			return -EIO;
		}

		if (sensor_channel_get(ina_dev, SENSOR_CHAN_VOLTAGE, &val) == 0) {
			data->channels[0].voltage_v = sensor_value_to_float(&val);
		}
		if (sensor_channel_get(ina_dev, SENSOR_CHAN_CURRENT, &val) == 0) {
			data->channels[0].current_a = sensor_value_to_float(&val);
		}
		if (sensor_channel_get(ina_dev, SENSOR_CHAN_POWER, &val) == 0) {
			data->channels[0].power_w = sensor_value_to_float(&val);
		}
		data->channels[0].valid = true;
	}

	return 0;
#else
	return -ENOTSUP;
#endif
}
