#include "device_bmi088_spi.h"

#include "bsp_spi.h"
#include "bsp_time.h"

#include <stddef.h>
#include <string.h>

#define DEVICE_BMI088_SPI_BUFFER_SIZE 16U

static size_t chip_select_for_die(
    const Device_BMI088_SPI *device, Device_BMI088_Die die)
{
    return (die == DEVICE_BMI088_DIE_ACCELEROMETER) ?
        device->accelerometer_chip_select : device->gyroscope_chip_select;
}

static bool transfer(
    const Device_BMI088_SPI *device,
    Device_BMI088_Die die,
    const uint8_t *tx,
    uint8_t *rx,
    size_t length)
{
    return (device != NULL) &&
        (BSP_SPI_Transfer(
            (BSP_SPI_Handle)device->backend,
            chip_select_for_die(device, die),
            tx, rx, length, device->timeout_ms) == BSP_SPI_RESULT_OK);
}

static bool read_registers(
    void *context,
    Device_BMI088_Die die,
    uint8_t first_register,
    uint8_t *bytes,
    size_t length)
{
    uint8_t tx[DEVICE_BMI088_SPI_BUFFER_SIZE] = {0U};
    uint8_t rx[DEVICE_BMI088_SPI_BUFFER_SIZE];
    const Device_BMI088_SPI *device = context;
    const size_t dummy_bytes =
        (die == DEVICE_BMI088_DIE_ACCELEROMETER) ? 1U : 0U;
    const size_t payload_offset = 1U + dummy_bytes;
    const size_t transfer_length = payload_offset + length;

    if ((device == NULL) || (bytes == NULL) || (length == 0U) ||
        (transfer_length > sizeof(tx)) ||
        ((die != DEVICE_BMI088_DIE_ACCELEROMETER) &&
         (die != DEVICE_BMI088_DIE_GYROSCOPE))) {
        return false;
    }
    tx[0] = (uint8_t)(first_register | UINT8_C(0x80));
    if (!transfer(device, die, tx, rx, transfer_length)) {
        return false;
    }
    memcpy(bytes, &rx[payload_offset], length);
    return true;
}

static bool write_register(
    void *context,
    Device_BMI088_Die die,
    uint8_t register_address,
    uint8_t value)
{
    const Device_BMI088_SPI *device = context;
    const uint8_t tx[2] = {
        (uint8_t)(register_address & UINT8_C(0x7F)), value};

    if ((device == NULL) ||
        ((die != DEVICE_BMI088_DIE_ACCELEROMETER) &&
         (die != DEVICE_BMI088_DIE_GYROSCOPE))) {
        return false;
    }
    return transfer(device, die, tx, NULL, sizeof(tx));
}

static void delay_ms(void *context, uint32_t milliseconds)
{
    (void)context;
    BSP_Time_DelayMs(milliseconds);
}

Device_BMI088_Status Device_BMI088_SPI_Init(
    Device_BMI088_SPI *device,
    const Device_BMI088_SPI_Config *config)
{
    BSP_SPI_Handle handle;
    BSP_SPI_Info info;
    Device_BMI088_Config sensor_config;
    Device_BMI088_Status status;
    uint8_t discarded;

    if (device != NULL) {
        *device = (Device_BMI088_SPI){0};
    }
    if ((device == NULL) || (config == NULL) ||
        (config->expected_bit_rate_hz == 0U) ||
        (config->timeout_ms == 0U) ||
        (config->accelerometer_chip_select ==
            config->gyroscope_chip_select)) {
        return DEVICE_BMI088_STATUS_INVALID_ARGUMENT;
    }
    handle = BSP_SPI_GetHandle(config->transport_index);
    if ((handle == NULL) || !BSP_SPI_GetInfo(handle, &info) ||
        (info.bit_rate_hz != config->expected_bit_rate_hz) ||
        (config->accelerometer_chip_select >= info.chip_select_count) ||
        (config->gyroscope_chip_select >= info.chip_select_count)) {
        return DEVICE_BMI088_STATUS_INVALID_ARGUMENT;
    }

    device->backend = handle;
    device->accelerometer_chip_select =
        config->accelerometer_chip_select;
    device->gyroscope_chip_select = config->gyroscope_chip_select;
    device->timeout_ms = config->timeout_ms;
    BSP_Time_DelayMs(config->startup_delay_ms);

    /* The accelerometer powers up in I2C mode. The first active-low CS
     * transaction selects SPI; discard it before the real chip-ID check. */
    (void)read_registers(device, DEVICE_BMI088_DIE_ACCELEROMETER,
        UINT8_C(0x00), &discarded, 1U);
    BSP_Time_DelayMs(1U);

    sensor_config = Device_BMI088_DefaultConfig();
    sensor_config.read_registers = read_registers;
    sensor_config.write_register = write_register;
    sensor_config.delay_ms = delay_ms;
    sensor_config.transport_context = device;
    status = Device_BMI088_Init(&device->sensor, &sensor_config);
    if (status != DEVICE_BMI088_STATUS_OK) {
        return status;
    }
    device->initialized = true;
    return DEVICE_BMI088_STATUS_OK;
}

Device_BMI088_Status Device_BMI088_SPI_UpdateIMU(
    Device_BMI088_SPI *device,
    Device_IMU *imu,
    uint32_t sampled_at_ms,
    float dt_seconds,
    Device_BMI088_Sample *sample)
{
    if ((device == NULL) || !device->initialized) {
        return DEVICE_BMI088_STATUS_NOT_INITIALIZED;
    }
    return Device_BMI088_UpdateIMU(
        &device->sensor, imu, sampled_at_ms, dt_seconds, sample);
}
