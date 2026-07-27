#ifndef DEVICE_SENSOR_IMU_BMI088_DEVICE_BMI088_SPI_H
#define DEVICE_SENSOR_IMU_BMI088_DEVICE_BMI088_SPI_H

#include "device_bmi088.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t transport_index;
    size_t accelerometer_chip_select;
    size_t gyroscope_chip_select;
    uint32_t expected_bit_rate_hz;
    uint32_t timeout_ms;
    uint32_t startup_delay_ms;
} Device_BMI088_SPI_Config;

typedef struct {
    Device_BMI088 sensor;
    const void *backend;
    size_t accelerometer_chip_select;
    size_t gyroscope_chip_select;
    uint32_t timeout_ms;
    bool initialized;
} Device_BMI088_SPI;

/* Bind the register-level BMI088 Device to one logical BSP SPI transport.
 * Call before the scheduler starts: initialization contains the finite reset
 * and power-up delays required by both BMI088 dies. */
Device_BMI088_Status Device_BMI088_SPI_Init(
    Device_BMI088_SPI *device,
    const Device_BMI088_SPI_Config *config);

Device_BMI088_Status Device_BMI088_SPI_UpdateIMU(
    Device_BMI088_SPI *device,
    Device_IMU *imu,
    uint32_t sampled_at_ms,
    float dt_seconds,
    Device_BMI088_Sample *sample);

#endif
