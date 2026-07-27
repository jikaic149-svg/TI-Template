#ifndef DEVICE_SENSOR_IMU_ICM45686_DEVICE_ICM45686_SPI_H
#define DEVICE_SENSOR_IMU_ICM45686_DEVICE_ICM45686_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_icm45686.h"

typedef struct {
    size_t transport_index;
    size_t chip_select_index;
    uint32_t expected_bit_rate_hz;
    uint32_t transfer_timeout_ms;
    uint32_t startup_delay_ms;
} Device_ICM45686_SPI_Config;

typedef struct {
    const void *backend;
    size_t chip_select_index;
    uint32_t transfer_timeout_ms;
    Device_ICM45686 sensor;
    Device_IMU imu;
    Device_ICM45686_Status last_status;
    bool initialized;
} Device_ICM45686_SPI;

/* Bind one ICM-45686 to a logical BSP SPI controller and the common IMU
 * mapper/attitude estimator. The IMU configuration must use physical-unit
 * scales of 1.0 because the register Device already performs conversion. */
bool Device_ICM45686_SPI_Init(
    Device_ICM45686_SPI *device,
    const Device_ICM45686_SPI_Config *transport_config,
    const Device_IMU_Config *imu_config);

/* Read one finite, non-saturated sample and update the attitude snapshot.
 * On failure no new IMU sample is committed. */
Device_ICM45686_Status Device_ICM45686_SPI_Update(
    Device_ICM45686_SPI *device,
    uint32_t sampled_at_ms,
    float dt_seconds,
    Device_ICM45686_Sample *sample,
    Device_IMU_Snapshot *snapshot);

#endif
