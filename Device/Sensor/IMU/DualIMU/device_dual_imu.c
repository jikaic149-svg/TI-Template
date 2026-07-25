#include "device_dual_imu.h"

#include <stddef.h>
#include <string.h>

#include "bsp_spi.h"
#include "bsp_time.h"

#define DEVICE_DUAL_IMU_TRANSPORT_BUFFER_SIZE 16U

static bool spi_transfer(
    const Device_DualIMU_Transport *transport,
    size_t chip_select,
    const uint8_t *tx,
    uint8_t *rx,
    size_t length)
{
    return (transport != NULL) &&
        (BSP_SPI_Transfer(
            (BSP_SPI_Handle)transport->backend,
            chip_select,
            tx,
            rx,
            length,
            transport->timeout_ms) == BSP_SPI_RESULT_OK);
}

static bool icm45686_read(
    void *context,
    uint8_t first_register,
    uint8_t *bytes,
    size_t length)
{
    uint8_t tx[DEVICE_DUAL_IMU_TRANSPORT_BUFFER_SIZE] = {0U};
    uint8_t rx[DEVICE_DUAL_IMU_TRANSPORT_BUFFER_SIZE];
    const Device_DualIMU_Transport *transport = context;

    if ((transport == NULL) || (bytes == NULL) || (length == 0U) ||
        (length + 1U > sizeof(tx))) {
        return false;
    }
    tx[0] = (uint8_t)(first_register | UINT8_C(0x80));
    if (!spi_transfer(transport, transport->primary_chip_select,
            tx, rx, length + 1U)) {
        return false;
    }
    memcpy(bytes, &rx[1], length);
    return true;
}

static bool icm45686_write(
    void *context, uint8_t register_address, uint8_t value)
{
    const Device_DualIMU_Transport *transport = context;
    const uint8_t tx[2] = {
        (uint8_t)(register_address & UINT8_C(0x7F)), value};

    return (transport != NULL) &&
        spi_transfer(transport, transport->primary_chip_select,
            tx, NULL, sizeof(tx));
}

static size_t bmi088_chip_select(
    const Device_DualIMU_Transport *transport, Device_BMI088_Die die)
{
    return (die == DEVICE_BMI088_DIE_ACCELEROMETER) ?
        transport->primary_chip_select : transport->secondary_chip_select;
}

static bool bmi088_read(
    void *context,
    Device_BMI088_Die die,
    uint8_t first_register,
    uint8_t *bytes,
    size_t length)
{
    uint8_t tx[DEVICE_DUAL_IMU_TRANSPORT_BUFFER_SIZE] = {0U};
    uint8_t rx[DEVICE_DUAL_IMU_TRANSPORT_BUFFER_SIZE];
    const Device_DualIMU_Transport *transport = context;
    const size_t dummy_bytes =
        (die == DEVICE_BMI088_DIE_ACCELEROMETER) ? 1U : 0U;
    const size_t payload_offset = 1U + dummy_bytes;
    const size_t transfer_length = payload_offset + length;

    if ((transport == NULL) || (bytes == NULL) || (length == 0U) ||
        (transfer_length > sizeof(tx)) ||
        ((die != DEVICE_BMI088_DIE_ACCELEROMETER) &&
         (die != DEVICE_BMI088_DIE_GYROSCOPE))) {
        return false;
    }
    tx[0] = (uint8_t)(first_register | UINT8_C(0x80));
    if (!spi_transfer(transport, bmi088_chip_select(transport, die),
            tx, rx, transfer_length)) {
        return false;
    }
    memcpy(bytes, &rx[payload_offset], length);
    return true;
}

static bool bmi088_write(
    void *context,
    Device_BMI088_Die die,
    uint8_t register_address,
    uint8_t value)
{
    const Device_DualIMU_Transport *transport = context;
    const uint8_t tx[2] = {
        (uint8_t)(register_address & UINT8_C(0x7F)), value};

    if ((transport == NULL) ||
        ((die != DEVICE_BMI088_DIE_ACCELEROMETER) &&
         (die != DEVICE_BMI088_DIE_GYROSCOPE))) {
        return false;
    }
    return spi_transfer(transport, bmi088_chip_select(transport, die),
        tx, NULL, sizeof(tx));
}

static void sensor_delay_ms(void *context, uint32_t milliseconds)
{
    (void)context;
    BSP_Time_DelayMs(milliseconds);
}

bool Device_DualIMU_Init(
    Device_DualIMU *device, const Device_DualIMU_Config *config)
{
    BSP_SPI_Handle bmi088_handle;
    BSP_SPI_Handle icm45686_handle;
    BSP_SPI_Info bmi088_info;
    BSP_SPI_Info icm45686_info;
    Device_BMI088_Config bmi088_config;
    Device_ICM45686_Config icm45686_config;
    uint8_t bmi088_spi_mode_dummy;

    if (device != NULL) {
        *device = (Device_DualIMU){0};
    }
    if ((device == NULL) || (config == NULL) ||
        (config->expected_spi_bit_rate_hz == 0U) ||
        (config->spi_timeout_ms == 0U)) {
        return false;
    }

    bmi088_handle = BSP_SPI_GetHandle(config->bmi088_transport_index);
    icm45686_handle = BSP_SPI_GetHandle(config->icm45686_transport_index);
    if ((bmi088_handle == NULL) || (icm45686_handle == NULL) ||
        !BSP_SPI_GetInfo(bmi088_handle, &bmi088_info) ||
        !BSP_SPI_GetInfo(icm45686_handle, &icm45686_info) ||
        (bmi088_info.bit_rate_hz != config->expected_spi_bit_rate_hz) ||
        (icm45686_info.bit_rate_hz != config->expected_spi_bit_rate_hz) ||
        (config->bmi088_accelerometer_chip_select >=
            bmi088_info.chip_select_count) ||
        (config->bmi088_gyroscope_chip_select >=
            bmi088_info.chip_select_count) ||
        (config->icm45686_chip_select >=
            icm45686_info.chip_select_count)) {
        return false;
    }

    device->bmi088_transport = (Device_DualIMU_Transport){
        .backend = bmi088_handle,
        .primary_chip_select = config->bmi088_accelerometer_chip_select,
        .secondary_chip_select = config->bmi088_gyroscope_chip_select,
        .timeout_ms = config->spi_timeout_ms,
    };
    device->icm45686_transport = (Device_DualIMU_Transport){
        .backend = icm45686_handle,
        .primary_chip_select = config->icm45686_chip_select,
        .timeout_ms = config->spi_timeout_ms,
    };

    BSP_Time_DelayMs(config->startup_delay_ms);

    /* BMI088 accelerometer powers up in I2C mode. Its first active-low CS
     * transaction selects SPI; discard that mode-switch read, then let the
     * register-level Device perform the real chip-ID check. */
    (void)bmi088_read(
        &device->bmi088_transport,
        DEVICE_BMI088_DIE_ACCELEROMETER,
        UINT8_C(0x00),
        &bmi088_spi_mode_dummy,
        1U);
    BSP_Time_DelayMs(1U);

    bmi088_config = Device_BMI088_DefaultConfig();
    bmi088_config.read_registers = bmi088_read;
    bmi088_config.write_register = bmi088_write;
    bmi088_config.delay_ms = sensor_delay_ms;
    bmi088_config.transport_context = &device->bmi088_transport;
    device->latest.bmi088_status =
        Device_BMI088_Init(&device->bmi088, &bmi088_config);

    icm45686_config = Device_ICM45686_DefaultConfig();
    icm45686_config.read_registers = icm45686_read;
    icm45686_config.write_register = icm45686_write;
    icm45686_config.delay_ms = sensor_delay_ms;
    icm45686_config.transport_context = &device->icm45686_transport;
    icm45686_config.enable_data_ready_interrupt = false;
    device->latest.icm45686_status =
        Device_ICM45686_Init(&device->icm45686, &icm45686_config);

    device->initialized = true;
    return true;
}

bool Device_DualIMU_Read(
    Device_DualIMU *device,
    uint32_t sampled_at_ms,
    float dt_seconds,
    Device_DualIMU_Snapshot *snapshot)
{
    Device_BMI088_Sample bmi088_sample;
    Device_ICM45686_Sample icm45686_sample;

    if ((device == NULL) || !device->initialized || (snapshot == NULL)) {
        return false;
    }

    device->latest.icm45686_status = Device_ICM45686_ReadSample(
        &device->icm45686, sampled_at_ms, dt_seconds, &icm45686_sample);
    if (device->latest.icm45686_status == DEVICE_ICM45686_STATUS_OK) {
        device->latest.icm45686 = icm45686_sample;
    }

    device->latest.bmi088_status = Device_BMI088_ReadSample(
        &device->bmi088, sampled_at_ms, dt_seconds, &bmi088_sample);
    if (device->latest.bmi088_status == DEVICE_BMI088_STATUS_OK) {
        device->latest.bmi088 = bmi088_sample;
    }

    *snapshot = device->latest;
    return true;
}
