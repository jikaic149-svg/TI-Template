#include "device_bmi088.h"
#include "device_icm45686.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int16_t acceleration_z_raw;
    int16_t gyroscope_z_raw;
} MockImu;

static void encode_i16_le(int16_t value, uint8_t bytes[2])
{
    const uint16_t raw = (uint16_t)value;

    bytes[0] = (uint8_t)(raw & UINT16_C(0x00FF));
    bytes[1] = (uint8_t)(raw >> 8U);
}

static bool icm_read(
    void *context, uint8_t first_register, uint8_t *bytes, size_t length)
{
    const MockImu *mock = context;

    if ((mock == NULL) || (bytes == NULL) ||
        (first_register != 0x00U) || (length != 14U)) {
        return false;
    }
    (void)memset(bytes, 0, length);
    encode_i16_le(mock->acceleration_z_raw, &bytes[4]);
    encode_i16_le(mock->gyroscope_z_raw, &bytes[10]);
    return true;
}

static bool bmi_read(
    void *context,
    Device_BMI088_Die die,
    uint8_t first_register,
    uint8_t *bytes,
    size_t length)
{
    const MockImu *mock = context;

    if ((mock == NULL) || (bytes == NULL)) {
        return false;
    }
    (void)memset(bytes, 0, length);
    if ((die == DEVICE_BMI088_DIE_ACCELEROMETER) &&
        (first_register == 0x12U) && (length == 6U)) {
        encode_i16_le(mock->acceleration_z_raw, &bytes[4]);
        return true;
    }
    if ((die == DEVICE_BMI088_DIE_GYROSCOPE) &&
        (first_register == 0x02U) && (length == 6U)) {
        encode_i16_le(mock->gyroscope_z_raw, &bytes[4]);
        return true;
    }
    return (die == DEVICE_BMI088_DIE_ACCELEROMETER) &&
        (first_register == 0x22U) && (length == 2U);
}

static Device_IMU_Config calibrated_config(void)
{
    Device_IMU_Config config = Device_IMU_DefaultConfig();

    config.start_calibration_on_init = true;
    config.calibration_required_samples = 2U;
    config.stationary_acceleration_tolerance_mps2 = 1.0F;
    config.stationary_gyro_maximum_dps = 10.0F;
    return config;
}

static void assert_calibrated_snapshot(
    Device_IMU *imu, uint32_t now_ms)
{
    Device_IMU_Snapshot snapshot;

    assert(Device_IMU_GetSnapshot(imu, now_ms, &snapshot));
    assert(snapshot.feedback_valid);
    assert(snapshot.gyro_bias_valid);
    assert(snapshot.calibration_status ==
        DEVICE_IMU_CALIBRATION_COMPLETE);
    assert(snapshot.calibration_samples_collected == 2U);
    assert(snapshot.sample_count == 2U);
}

static void test_single_icm45686(void)
{
    MockImu mock = {INT16_C(8192), INT16_C(50)};
    Device_ICM45686 sensor = {0};
    Device_ICM45686_Sample sample;
    Device_IMU imu;
    const Device_IMU_Config imu_config = calibrated_config();

    sensor.config = Device_ICM45686_DefaultConfig();
    sensor.config.read_registers = icm_read;
    sensor.config.transport_context = &mock;
    sensor.initialized = true;
    assert(Device_IMU_Init(&imu, &imu_config));

    assert(Device_ICM45686_UpdateIMU(
        &sensor, &imu, 10U, 0.01F, &sample) ==
        DEVICE_ICM45686_STATUS_OK);
    assert(Device_ICM45686_UpdateIMU(
        &sensor, &imu, 20U, 0.01F, &sample) ==
        DEVICE_ICM45686_STATUS_OK);
    assert_calibrated_snapshot(&imu, 20U);

    mock.gyroscope_z_raw = INT16_C(32000);
    assert(Device_ICM45686_UpdateIMU(
        &sensor, &imu, 30U, 0.01F, &sample) ==
        DEVICE_ICM45686_STATUS_IMU_REJECTED_SAMPLE);
    assert(!sample.valid);
    assert_calibrated_snapshot(&imu, 30U);
}

static void test_single_bmi088(void)
{
    MockImu mock = {INT16_C(10923), INT16_C(50)};
    Device_BMI088 sensor = {0};
    Device_BMI088_Sample sample;
    Device_IMU imu;
    const Device_IMU_Config imu_config = calibrated_config();

    sensor.config = Device_BMI088_DefaultConfig();
    sensor.config.read_registers = bmi_read;
    sensor.config.transport_context = &mock;
    sensor.initialized = true;
    assert(Device_IMU_Init(&imu, &imu_config));

    assert(Device_BMI088_UpdateIMU(
        &sensor, &imu, 10U, 0.01F, &sample) ==
        DEVICE_BMI088_STATUS_OK);
    assert(Device_BMI088_UpdateIMU(
        &sensor, &imu, 20U, 0.01F, &sample) ==
        DEVICE_BMI088_STATUS_OK);
    assert_calibrated_snapshot(&imu, 20U);

    mock.gyroscope_z_raw = -INT16_C(32000);
    assert(Device_BMI088_UpdateIMU(
        &sensor, &imu, 30U, 0.01F, &sample) ==
        DEVICE_BMI088_STATUS_IMU_REJECTED_SAMPLE);
    assert(!sample.valid);
    assert_calibrated_snapshot(&imu, 30U);
}

int main(void)
{
    test_single_icm45686();
    test_single_bmi088();
    puts("Single ICM45686/BMI088 attitude tests: PASS");
    return 0;
}
