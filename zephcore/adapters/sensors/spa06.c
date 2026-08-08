/*
 * Goertek SPA06-003 pressure + temperature sensor
 * Copyright (c) 2026 ZephCore
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT goertek_spa06

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

LOG_MODULE_REGISTER(spa06, CONFIG_SENSOR_LOG_LEVEL);

#define SPA06_REG_PSR_B2  0x00
#define SPA06_REG_PRS_CFG 0x06
#define SPA06_REG_TMP_CFG 0x07
#define SPA06_REG_MEAS_CFG 0x08
#define SPA06_REG_CFG_REG 0x09
#define SPA06_REG_RESET   0x0C
#define SPA06_REG_ID      0x0D
#define SPA06_REG_COEF    0x10

#define SPA06_CHIP_ID     0x11
#define SPA06_SOFT_RESET  0x09

#define SPA06_MEAS_COEF_RDY   BIT(7)
#define SPA06_MEAS_SENSOR_RDY BIT(6)
#define SPA06_MEAS_CONT_BOTH  0x07

/* 8x oversampling for both channels: no result shift needed and the
 * scaling factor below is the datasheet's kP/kT for that rate. */
#define SPA06_OVERSAMPLE_8    0x03
#define SPA06_RATE_1HZ        0x00
#define SPA06_SCALE_FACTOR    7864320.0f

#define SPA06_COEF_LEN 21

struct spa06_config {
	struct i2c_dt_spec bus;
};

struct spa06_data {
	int16_t c0, c1;
	int32_t c00, c10;
	int16_t c01, c11, c20, c21, c30;
	int16_t c31, c40;

	int32_t raw_press;
	int32_t raw_temp;
};

/* 24-bit two's complement, MSB first */
static int32_t spa06_raw24(const uint8_t *buf)
{
	int32_t v = ((int32_t)buf[0] << 16) | ((int32_t)buf[1] << 8) | buf[2];

	return (v & 0x800000) ? (v | ~0xFFFFFF) : v;
}

static int16_t sign_extend_12(uint16_t v)
{
	return (v & 0x800) ? (int16_t)(v | 0xF000) : (int16_t)v;
}

static int32_t sign_extend_20(uint32_t v)
{
	return (v & 0x80000) ? (int32_t)(v | 0xFFF00000) : (int32_t)v;
}

static int spa06_read_coefficients(const struct device *dev)
{
	const struct spa06_config *cfg = dev->config;
	struct spa06_data *data = dev->data;
	uint8_t c[SPA06_COEF_LEN];
	int rc;

	rc = i2c_burst_read_dt(&cfg->bus, SPA06_REG_COEF, c, sizeof(c));
	if (rc < 0) {
		return rc;
	}

	data->c0  = sign_extend_12(((uint16_t)c[0] << 4) | (c[1] >> 4));
	data->c1  = sign_extend_12((((uint16_t)c[1] & 0x0F) << 8) | c[2]);
	data->c00 = sign_extend_20(((uint32_t)c[3] << 12) |
				   ((uint32_t)c[4] << 4) | (c[5] >> 4));
	data->c10 = sign_extend_20((((uint32_t)c[5] & 0x0F) << 16) |
				   ((uint32_t)c[6] << 8) | c[7]);
	data->c01 = (int16_t)(((uint16_t)c[8] << 8) | c[9]);
	data->c11 = (int16_t)(((uint16_t)c[10] << 8) | c[11]);
	data->c20 = (int16_t)(((uint16_t)c[12] << 8) | c[13]);
	data->c21 = (int16_t)(((uint16_t)c[14] << 8) | c[15]);
	data->c30 = (int16_t)(((uint16_t)c[16] << 8) | c[17]);
	data->c31 = sign_extend_12(((uint16_t)c[18] << 4) | (c[19] >> 4));
	data->c40 = sign_extend_12((((uint16_t)c[19] & 0x0F) << 8) | c[20]);

	return 0;
}

static int spa06_sample_fetch(const struct device *dev,
			      enum sensor_channel chan)
{
	const struct spa06_config *cfg = dev->config;
	struct spa06_data *data = dev->data;
	uint8_t buf[6];
	int rc;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_PRESS &&
	    chan != SENSOR_CHAN_AMBIENT_TEMP) {
		return -ENOTSUP;
	}

	rc = i2c_burst_read_dt(&cfg->bus, SPA06_REG_PSR_B2, buf, sizeof(buf));
	if (rc < 0) {
		return rc;
	}

	data->raw_press = spa06_raw24(&buf[0]);
	data->raw_temp = spa06_raw24(&buf[3]);

	return 0;
}

static int spa06_channel_get(const struct device *dev,
			     enum sensor_channel chan,
			     struct sensor_value *val)
{
	struct spa06_data *data = dev->data;
	float t_sc = (float)data->raw_temp / SPA06_SCALE_FACTOR;
	float p_sc, p2, p3, p4, pa;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		return sensor_value_from_float(val,
					       (float)data->c0 * 0.5f +
					       (float)data->c1 * t_sc);

	case SENSOR_CHAN_PRESS:
		p_sc = (float)data->raw_press / SPA06_SCALE_FACTOR;
		p2 = p_sc * p_sc;
		p3 = p2 * p_sc;
		p4 = p3 * p_sc;

		pa = (float)data->c00 + (float)data->c10 * p_sc +
		     (float)data->c20 * p2 + (float)data->c30 * p3 +
		     (float)data->c40 * p4 +
		     t_sc * ((float)data->c01 + (float)data->c11 * p_sc +
			     (float)data->c21 * p2 + (float)data->c31 * p3);

		/* Sensor API expects kPa */
		return sensor_value_from_float(val, pa / 1000.0f);

	default:
		return -ENOTSUP;
	}
}

static DEVICE_API(sensor, spa06_api) = {
	.sample_fetch = spa06_sample_fetch,
	.channel_get = spa06_channel_get,
};

static int spa06_init(const struct device *dev)
{
	const struct spa06_config *cfg = dev->config;
	uint8_t id, meas;
	int rc;

	if (!i2c_is_ready_dt(&cfg->bus)) {
		return -ENODEV;
	}

	rc = i2c_reg_read_byte_dt(&cfg->bus, SPA06_REG_ID, &id);
	if (rc < 0) {
		return -ENODEV;
	}
	if (id != SPA06_CHIP_ID) {
		LOG_DBG("no SPA06 at 0x%02x (id 0x%02x)", cfg->bus.addr, id);
		return -ENODEV;
	}

	rc = i2c_reg_write_byte_dt(&cfg->bus, SPA06_REG_RESET, SPA06_SOFT_RESET);
	if (rc < 0) {
		return rc;
	}
	k_msleep(15);

	/* Calibration data is only valid once the part reports both ready */
	for (int i = 0; i < 20; i++) {
		rc = i2c_reg_read_byte_dt(&cfg->bus, SPA06_REG_MEAS_CFG, &meas);
		if (rc == 0 && (meas & SPA06_MEAS_COEF_RDY) &&
		    (meas & SPA06_MEAS_SENSOR_RDY)) {
			goto ready;
		}
		k_msleep(10);
	}
	LOG_ERR("SPA06 not ready after reset");
	return -EIO;

ready:
	rc = spa06_read_coefficients(dev);
	if (rc < 0) {
		return rc;
	}

	rc = i2c_reg_write_byte_dt(&cfg->bus, SPA06_REG_PRS_CFG,
				   (SPA06_RATE_1HZ << 4) | SPA06_OVERSAMPLE_8);
	if (rc == 0) {
		rc = i2c_reg_write_byte_dt(&cfg->bus, SPA06_REG_TMP_CFG,
					   (SPA06_RATE_1HZ << 4) | SPA06_OVERSAMPLE_8);
	}
	if (rc == 0) {
		/* No result shift: only needed above 8x oversampling */
		rc = i2c_reg_write_byte_dt(&cfg->bus, SPA06_REG_CFG_REG, 0x00);
	}
	if (rc == 0) {
		rc = i2c_reg_write_byte_dt(&cfg->bus, SPA06_REG_MEAS_CFG,
					   SPA06_MEAS_CONT_BOTH);
	}
	if (rc < 0) {
		return rc;
	}

	LOG_INF("SPA06 at 0x%02x ready", cfg->bus.addr);
	return 0;
}

#define SPA06_DEFINE(inst)                                                   \
	static struct spa06_data spa06_data_##inst;                          \
	static const struct spa06_config spa06_config_##inst = {             \
		.bus = I2C_DT_SPEC_INST_GET(inst),                           \
	};                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, spa06_init, NULL,                 \
				     &spa06_data_##inst, &spa06_config_##inst,\
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,\
				     &spa06_api);

DT_INST_FOREACH_STATUS_OKAY(SPA06_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
