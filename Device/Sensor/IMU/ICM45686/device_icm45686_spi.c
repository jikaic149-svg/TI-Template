#include "device_icm45686_spi.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "bsp_spi.h"
#include "bsp_time.h"

#define DEVICE_ICM45686_SPI_BUFFER_SIZE 16U

static bool spi_transfer(
    const Device_ICM45686_SPI *device,
    const uint8_t *tx,
    uint8_t *rx,
    size_t length)
{
    return (device != NULL) && (device->backend != NULL) &&
        (BSP_SPI_Transfer(
            (BSP_SPI_Handle)device->backend,
            device->chip_select_index,
            tx,
            rx,
            length,
            device->transfer_timeout_ms) == BSP_SPI_RESULT_OK);
}

static bool read_registers(
    void *context,
    uint8_t first_register,
    uint8_t *bytes,
    size_t length)
{
    uint8_t tx[DEVICE_ICM45686_SPI_BUFFER_SIZE] = {0U};
    uint8_t rx[DEVICE_ICM45686_SPI_BUFFER_SIZE];
    Device_ICM45686_SPI *device = context;

    if ((device == NULL) || (bytes == NULL) || (length == 0U) ||
        (length + 1U > sizeof(tx))) {
        return false;
    }
    tx[0] = (uint8_t)(first_register | UINT8_C(0x80));
    if (!spi_transfer(device, tx, rx, length + 1U)) {
        return false;
    }
    memcpy(bytes, &rx[1], length);
    return true;
}

static bool write_register(
    void *context, uint8_t register_address, uint8_t value)
{
    Device_ICM45686_SPI *device = context;
    const uint8_t tx[2] = {
        (uint8_t)(register_address & UINT8_C(0x7F)), value};

    return spi_transfer(device, tx, NULL, sizeof(tx));
}

static void delay_ms(void *context, uint32_t milliseconds)
{
    (void)context;
    BSP_Time_DelayMs(milliseconds);
}

bool Device_ICM45686_SPI_Init(
    Device_ICM45686_SPI *device,
    const Device_ICM45686_SPI_Config *transport_config,
    const Device_IMU_Config *imu_config)
{
    BSP_SPI_Handle handle;
    BSP_SPI_Info info;
    Device_ICM45686_Config sensor_config;

    if (device != NULL) {
        *device = (Device_ICM45686_SPI){0};
    }
    if ((device == NULL) || (transport_config == NULL) ||
        (imu_config == NULL) ||
        (transport_config->expected_bit_rate_hz == 0U) ||
        (transport_config->transfer_timeout_ms == 0U) ||
        !isfinite(imu_config->acceleration_mps2_per_sensor_unit) ||
        !isfinite(imu_config->angular_rate_dps_per_sensor_unit) ||
        (imu_config->acceleration_mps2_per_sensor_unit != 1.0F) ||
        (imu_config->angular_rate_dps_per_sensor_unit != 1.0F)) {
        return false;
    }

    handle = BSP_SPI_GetHandle(transport_config->transport_index);
    if ((handle == NULL) || !BSP_SPI_GetInfo(handle, &info) ||
        (info.bit_rate_hz != transport_config->expected_bit_rate_hz) ||
        (transport_config->chip_select_index >= info.chip_select_count)) {
        return false;
    }

    device->backend = handle;
    device->chip_select_index = transport_config->chip_select_index;
    device->transfer_timeout_ms =
        transport_config->transfer_timeout_ms;
    BSP_Time_DelayMs(transport_config->startup_delay_ms);

    if (!Device_IMU_Init(&device->imu, imu_config)) {
        return false;
    }
    sensor_config = Device_ICM45686_DefaultConfig();
    sensor_config.read_registers = read_registers;
    sensor_config.write_register = write_register;
    sensor_config.delay_ms = delay_ms;
    sensor_config.transport_context = device;
    sensor_config.enable_data_ready_interrupt = false;
    device->last_status =
        Device_ICM45686_Init(&device->sensor, &sensor_config);
    if (device->last_status != DEVICE_ICM45686_STATUS_OK) {
        return false;
    }
    device->initialized = true;
    return true;
}

Device_ICM45686_Status Device_ICM45686_SPI_Update(
    Device_ICM45686_SPI *device,
    uint32_t sampled_at_ms,
    float dt_seconds,
    Device_ICM45686_Sample *sample,
    Device_IMU_Snapshot *snapshot)
{
    Device_ICM45686_Sample next_sample;
    Device_IMU_Sample imu_sample = {
        .sampled_at_ms = sampled_at_ms,
        .dt_seconds = dt_seconds,
    };

    if ((device == NULL) || !device->initialized || (snapshot == NULL)) {
        return DEVICE_ICM45686_STATUS_INVALID_ARGUMENT;
    }
    device->last_status = Device_ICM45686_ReadSample(
        &device->sensor, sampled_at_ms, dt_seconds, &next_sample);
    if (device->last_status != DEVICE_ICM45686_STATUS_OK) {
        return device->last_status;
    }
    if (!next_sample.valid) {
        device->last_status =
            DEVICE_ICM45686_STATUS_IMU_REJECTED_SAMPLE;
        return device->last_status;
    }
    for (size_t axis = 0U; axis < 3U; ++axis) {
        imu_sample.acceleration_sensor_units[axis] =
            next_sample.acceleration_mps2[axis];
        imu_sample.angular_rate_sensor_units[axis] =
            next_sample.angular_rate_dps[axis];
    }
    if (!Device_IMU_PushSample(&device->imu, &imu_sample) ||
        !Device_IMU_GetSnapshot(&device->imu, sampled_at_ms, snapshot)) {
        device->last_status =
            DEVICE_ICM45686_STATUS_IMU_REJECTED_SAMPLE;
        return device->last_status;
    }
    if (sample != NULL) {
        *sample = next_sample;
    }
    device->last_status = DEVICE_ICM45686_STATUS_OK;
    return device->last_status;
}
