#ifndef DEVICE_SENSOR_IMU_DUAL_IMU_DEVICE_DUAL_IMU_H
#define DEVICE_SENSOR_IMU_DUAL_IMU_DEVICE_DUAL_IMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_bmi088.h"
#include "device_icm45686.h"

typedef struct {
    size_t bmi088_transport_index;
    size_t icm45686_transport_index;
    size_t bmi088_accelerometer_chip_select;
    size_t bmi088_gyroscope_chip_select;
    size_t icm45686_chip_select;
    uint32_t expected_spi_bit_rate_hz;
    uint32_t spi_timeout_ms;
    uint32_t startup_delay_ms;
} Device_DualIMU_Config;

typedef struct {
    Device_BMI088_Sample bmi088;
    Device_ICM45686_Sample icm45686;
    Device_BMI088_Status bmi088_status;
    Device_ICM45686_Status icm45686_status;
} Device_DualIMU_Snapshot;

typedef struct {
    const void *backend;
    size_t primary_chip_select;
    size_t secondary_chip_select;
    uint32_t timeout_ms;
} Device_DualIMU_Transport;

typedef struct {
    Device_DualIMU_Transport bmi088_transport;
    Device_DualIMU_Transport icm45686_transport;
    Device_BMI088 bmi088;
    Device_ICM45686 icm45686;
    Device_DualIMU_Snapshot latest;
    bool initialized;
} Device_DualIMU;

bool Device_DualIMU_Init(
    Device_DualIMU *device, const Device_DualIMU_Config *config);

/* Read both devices once. Individual status fields report independent
 * success/failure and the last valid physical sample is retained on failure. */
bool Device_DualIMU_Read(
    Device_DualIMU *device,
    uint32_t sampled_at_ms,
    float dt_seconds,
    Device_DualIMU_Snapshot *snapshot);

#endif
