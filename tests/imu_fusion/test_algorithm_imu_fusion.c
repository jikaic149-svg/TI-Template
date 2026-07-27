#include "algorithm_imu_fusion.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_GRAVITY 9.80665F
#define TEST_PI 3.14159265358979323846F

static unsigned int g_failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        ++g_failures; \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
            #condition); \
    } \
} while (0)

static void check_near(float actual, float expected, float tolerance)
{
    if (!isfinite(actual) || (fabsf(actual - expected) > tolerance)) {
        ++g_failures;
        (void)fprintf(stderr,
            "FAIL near: actual=%.9g expected=%.9g tolerance=%.9g\n",
            (double)actual, (double)expected, (double)tolerance);
    }
}

static Algorithm_IMUFusion_Input make_input(
    const float primary_acceleration[3],
    const float primary_angular_rate[3],
    const float secondary_acceleration[3],
    const float secondary_angular_rate[3],
    bool primary_valid,
    bool secondary_valid,
    float dt_seconds)
{
    Algorithm_IMUFusion_Input input = {0};

    if (primary_acceleration != NULL) {
        memcpy(input.primary_acceleration_mps2, primary_acceleration,
            sizeof(input.primary_acceleration_mps2));
    }
    if (primary_angular_rate != NULL) {
        memcpy(input.primary_angular_rate_dps, primary_angular_rate,
            sizeof(input.primary_angular_rate_dps));
    }
    if (secondary_acceleration != NULL) {
        memcpy(input.secondary_acceleration_mps2, secondary_acceleration,
            sizeof(input.secondary_acceleration_mps2));
    }
    if (secondary_angular_rate != NULL) {
        memcpy(input.secondary_angular_rate_dps, secondary_angular_rate,
            sizeof(input.secondary_angular_rate_dps));
    }
    input.dt_seconds = dt_seconds;
    input.primary_valid = primary_valid;
    input.secondary_valid = secondary_valid;
    return input;
}

static float wrap_degrees(float value)
{
    while (value > 180.0F) {
        value -= 360.0F;
    }
    while (value < -180.0F) {
        value += 360.0F;
    }
    return value;
}

static float quaternion_error_degrees(
    const Algorithm_Mahony_Quaternion *actual,
    const Algorithm_Mahony_Quaternion *expected)
{
    float cosine = fabsf((actual->w * expected->w) +
        (actual->x * expected->x) + (actual->y * expected->y) +
        (actual->z * expected->z));

    if (cosine > 1.0F) {
        cosine = 1.0F;
    }
    return 2.0F * acosf(cosine) * 180.0F / TEST_PI;
}

static void acceleration_for_euler(
    float roll_deg,
    float pitch_deg,
    float acceleration[3])
{
    const float roll = roll_deg * TEST_PI / 180.0F;
    const float pitch = pitch_deg * TEST_PI / 180.0F;

    acceleration[0] = -TEST_GRAVITY * sinf(pitch);
    acceleration[1] = TEST_GRAVITY * sinf(roll) * cosf(pitch);
    acceleration[2] = TEST_GRAVITY * cosf(roll) * cosf(pitch);
}

static Algorithm_Mahony_Quaternion quaternion_for_euler(
    float roll_deg, float pitch_deg, float yaw_deg)
{
    const float half_roll = 0.5F * roll_deg * TEST_PI / 180.0F;
    const float half_pitch = 0.5F * pitch_deg * TEST_PI / 180.0F;
    const float half_yaw = 0.5F * yaw_deg * TEST_PI / 180.0F;
    const float cr = cosf(half_roll);
    const float sr = sinf(half_roll);
    const float cp = cosf(half_pitch);
    const float sp = sinf(half_pitch);
    const float cy = cosf(half_yaw);
    const float sy = sinf(half_yaw);
    Algorithm_Mahony_Quaternion quaternion;

    quaternion.w = (cr * cp * cy) + (sr * sp * sy);
    quaternion.x = (sr * cp * cy) - (cr * sp * sy);
    quaternion.y = (cr * sp * cy) + (sr * cp * sy);
    quaternion.z = (cr * cp * sy) - (sr * sp * cy);
    return quaternion;
}

static Algorithm_IMUFusion_Config startup_calibration_config(
    uint32_t settle_samples, uint32_t calibration_samples)
{
    Algorithm_IMUFusion_Config config = Algorithm_IMUFusion_DefaultConfig();

    config.enable_startup_gyro_bias_calibration = true;
    config.startup_calibration_settle_samples = settle_samples;
    config.startup_calibration_required_samples = calibration_samples;
    return config;
}

static void test_default_and_mapping(void)
{
    Algorithm_IMUFusion_Config config = Algorithm_IMUFusion_DefaultConfig();
    Algorithm_IMUFusion fusion;

    CHECK(!config.estimate_gyro_bias);
    CHECK(!config.enable_startup_gyro_bias_calibration);
    CHECK(!config.enable_startup_bias_trend_fit);
    CHECK(config.secondary_position_m[0] == 0.035F);
    CHECK(config.gyro_rate_process_noise_dps2_per_second >= 0.0F);
    CHECK(config.initial_gyro_rate_variance_dps2 > 0.0F);
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    CHECK(fusion.initialized);
    CHECK(fusion.snapshot.yaw_absolute_observable == false);
    CHECK(fusion.snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_DISABLED);
    check_near(fusion.snapshot.quaternion.w, 1.0F, 1.0e-6F);

    config.secondary_body_axis[2].direction = -1;
    CHECK(!Algorithm_IMUFusion_Init(&fusion, &config));

    config = Algorithm_IMUFusion_DefaultConfig();
    config.estimate_gyro_bias = true;
    CHECK(!Algorithm_IMUFusion_Init(&fusion, &config));

    config = Algorithm_IMUFusion_DefaultConfig();
    config.primary_body_axis[1].source = config.primary_body_axis[0].source;
    CHECK(!Algorithm_IMUFusion_Init(&fusion, &config));

    config = Algorithm_IMUFusion_DefaultConfig();
    config.maximum_dt_seconds = 0.051F;
    CHECK(!Algorithm_IMUFusion_Init(&fusion, &config));

    config = Algorithm_IMUFusion_DefaultConfig();
    config.maximum_gyro_rate_prediction_variance_dps2 =
        0.5F * config.initial_gyro_rate_variance_dps2;
    CHECK(!Algorithm_IMUFusion_Init(&fusion, &config));

    config = Algorithm_IMUFusion_DefaultConfig();
    config.calibration_filter_time_constant_seconds = -0.1F;
    CHECK(!Algorithm_IMUFusion_Init(&fusion, &config));

    config = Algorithm_IMUFusion_DefaultConfig();
    config.estimate_gyro_bias = true;
    CHECK(!Algorithm_IMUFusion_Init(&fusion, &config));
}

static void test_gravity_update_sign(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    float acceleration[3];
    const float rate[3] = {0.0F, 0.0F, 0.0F};
    Algorithm_IMUFusion_Input input;

    acceleration_for_euler(30.0F, 0.0F, acceleration);
    input = make_input(acceleration, rate, NULL, NULL, true, false, 0.010F);
    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    /* Bypass the startup seed so this exercises the signed Kalman innovation
     * and quaternion injection rather than the direct accelerometer seed. */
    fusion.attitude_seeded = true;
    for (unsigned int count = 0U; count < 100U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    check_near(snapshot.euler.roll_deg, 30.0F, 0.5F);
    check_near(snapshot.euler.pitch_deg, 0.0F, 0.2F);
}

static void test_level_static_and_bias_disabled(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float angular_rate[3] = {0.0F, 0.0F, 0.0F};
    const Algorithm_IMUFusion_Input input = make_input(
        acceleration, angular_rate, acceleration, angular_rate,
        true, true, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    for (unsigned int count = 0U; count < 200U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    check_near(snapshot.euler.roll_deg, 0.0F, 0.05F);
    check_near(snapshot.euler.pitch_deg, 0.0F, 0.05F);
    check_near(snapshot.euler.yaw_deg, 0.0F, 0.05F);
    CHECK(snapshot.gyro_source_mask == 0x03U);
    CHECK(snapshot.acceleration_source_mask == 0x03U);
    CHECK(snapshot.acceleration_correction_used);
    CHECK(snapshot.update_count == 200U);
    CHECK(snapshot.gyro_bias_dps[0] == 0.0F);
    CHECK(snapshot.gyro_bias_dps[1] == 0.0F);
    CHECK(snapshot.gyro_bias_dps[2] == 0.0F);
    CHECK(!snapshot.yaw_absolute_observable);
}

static void test_known_tilt_seed(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    float acceleration[3];
    const float angular_rate[3] = {0.0F, 0.0F, 0.0F};
    Algorithm_IMUFusion_Input mutable_input;

    acceleration_for_euler(30.0F, -20.0F, acceleration);
    mutable_input = make_input(acceleration, angular_rate, NULL, NULL,
        true, false, 0.010F);
    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    CHECK(Algorithm_IMUFusion_Update(&fusion, &mutable_input, &snapshot));
    check_near(snapshot.euler.roll_deg, 30.0F, 0.2F);
    check_near(snapshot.euler.pitch_deg, -20.0F, 0.2F);
}

static void test_gravity_correction_direction(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float rate[3] = {0.0F, 0.0F, 0.0F};
    const float half_roll = 5.0F * TEST_PI / 180.0F;
    const Algorithm_IMUFusion_Input input = make_input(
        acceleration, rate, NULL, NULL, true, false, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    fusion.attitude_seeded = true;
    fusion.snapshot.quaternion.w = cosf(half_roll);
    fusion.snapshot.quaternion.x = sinf(half_roll);
    fusion.snapshot.quaternion.y = 0.0F;
    fusion.snapshot.quaternion.z = 0.0F;
    for (unsigned int count = 0U; count < 100U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    check_near(snapshot.euler.roll_deg, 0.0F, 0.5F);
    check_near(snapshot.euler.pitch_deg, 0.0F, 0.1F);
}

static void test_gravity_update_does_not_observe_yaw(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float rate[3] = {0.0F, 0.0F, 0.0F};
    const Algorithm_IMUFusion_Input input = make_input(
        acceleration, rate, NULL, NULL, true, false, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    fusion.attitude_seeded = true;
    fusion.snapshot.quaternion = quaternion_for_euler(25.0F, -15.0F, 40.0F);
    for (unsigned int count = 0U; count < 500U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    check_near(snapshot.euler.roll_deg, 0.0F, 0.2F);
    check_near(snapshot.euler.pitch_deg, 0.0F, 0.2F);
    check_near(wrap_degrees(snapshot.euler.yaw_deg - 40.0F), 0.0F, 0.2F);
}

static void test_dynamic_acceleration_is_not_gravity(void)
{
    Algorithm_IMUFusion_Config config = Algorithm_IMUFusion_DefaultConfig();
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float zero_rate[3] = {0.0F, 0.0F, 0.0F};
    const float direction_angle = 20.0F * TEST_PI / 180.0F;
    const float translated_acceleration[3] = {
        TEST_GRAVITY * sinf(direction_angle),
        0.0F,
        TEST_GRAVITY * cosf(direction_angle),
    };
    const float excessive_magnitude[3] = {
        0.0F, 0.0F, TEST_GRAVITY + 1.5F};
    Algorithm_IMUFusion_Input input;

    config.acceleration_norm_tolerance_mps2 = 1.0F;
    config.maximum_gravity_direction_innovation = 0.25F;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    fusion.attitude_seeded = true;
    input = make_input(translated_acceleration, zero_rate,
        translated_acceleration, zero_rate, true, true, 0.010F);
    for (unsigned int count = 0U; count < 100U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
        CHECK(!snapshot.acceleration_correction_used);
    }
    check_near(snapshot.euler.roll_deg, 0.0F, 0.02F);
    check_near(snapshot.euler.pitch_deg, 0.0F, 0.02F);
    check_near(snapshot.euler.yaw_deg, 0.0F, 0.02F);

    input = make_input(excessive_magnitude, zero_rate,
        excessive_magnitude, zero_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(!snapshot.acceleration_correction_used);
    check_near(snapshot.euler.yaw_deg, 0.0F, 0.02F);
}

static void test_yaw_integration(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float angular_rate[3] = {0.0F, 0.0F, 90.0F};
    const Algorithm_IMUFusion_Input input = make_input(
        acceleration, angular_rate, NULL, NULL, true, false, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    for (unsigned int count = 0U; count < 100U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    check_near(wrap_degrees(snapshot.euler.yaw_deg - 90.0F), 0.0F, 0.3F);
    check_near(snapshot.euler.roll_deg, 0.0F, 0.1F);
    check_near(snapshot.euler.pitch_deg, 0.0F, 0.1F);
    CHECK(snapshot.gyro_source_mask == 0x01U);
}

static void test_coning_compensated_large_rotation(void)
{
    Algorithm_IMUFusion_Config config = Algorithm_IMUFusion_DefaultConfig();
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float zero_acceleration[3] = {0.0F, 0.0F, 0.0F};
    const Algorithm_Mahony_Quaternion expected = {
        0.031396936F, 0.260939603F, -0.000238326F, 0.964844391F};
    float rate[3];
    Algorithm_IMUFusion_Input input;

    config.primary_gyro_variance_dps2 = 1.0e-6F;
    config.secondary_gyro_variance_dps2 = 1.0e-6F;
    config.gyro_rate_process_noise_dps2_per_second = 1.0e6F;
    config.initial_gyro_rate_variance_dps2 = 1.0e4F;
    config.maximum_gyro_rate_prediction_variance_dps2 = 1.0e4F;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    fusion.previous_attitude_angular_rate_rad_per_second[0] =
        300.0F * TEST_PI / 180.0F;
    fusion.previous_attitude_angular_rate_rad_per_second[1] = 0.0F;
    fusion.previous_attitude_angular_rate_rad_per_second[2] = 0.0F;
    fusion.has_previous_attitude_angular_rate = true;
    for (unsigned int count = 1U; count <= 400U; ++count) {
        const float time_seconds = 0.010F * (float)count;
        const float phase = 2.0F * TEST_PI * 3.0F * time_seconds;

        rate[0] = 300.0F * cosf(phase);
        rate[1] = 300.0F * sinf(phase);
        rate[2] = 90.0F * sinf(0.7F * phase);
        input = make_input(zero_acceleration, rate,
            zero_acceleration, rate, true, true, 0.010F);
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(quaternion_error_degrees(&snapshot.quaternion, &expected) < 1.2F);
}

static void test_reinitialization_resets_yaw_zero(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float rotating_rate[3] = {0.0F, 0.0F, 90.0F};
    const float zero_rate[3] = {0.0F, 0.0F, 0.0F};
    Algorithm_IMUFusion_Input input = make_input(
        acceleration, rotating_rate, NULL, NULL, true, false, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    for (unsigned int count = 0U; count < 100U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(fabsf(snapshot.euler.yaw_deg) > 80.0F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    input = make_input(
        acceleration, zero_rate, NULL, NULL, true, false, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    check_near(snapshot.euler.yaw_deg, 0.0F, 1.0e-5F);
}

static void test_bias_estimation_is_deferred(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float biased_rate[3] = {0.0F, 0.0F, 0.5F};
    const Algorithm_IMUFusion_Input input = make_input(
        acceleration, biased_rate, NULL, NULL, true, false, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    for (unsigned int count = 0U; count < 200U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    /* Phase one must not learn a bias from the gravity update. */
    check_near(snapshot.gyro_bias_dps[0], 0.0F, 1.0e-7F);
    check_near(snapshot.gyro_bias_dps[1], 0.0F, 1.0e-7F);
    check_near(snapshot.gyro_bias_dps[2], 0.0F, 1.0e-7F);
    check_near(wrap_degrees(snapshot.euler.yaw_deg), 1.0F, 0.05F);
}

static void test_startup_bias_calibration(void)
{
    Algorithm_IMUFusion_Config config = startup_calibration_config(2U, 4U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float primary_sensor_bias[3] = {1.0F, 2.0F, 0.3F};
    const float secondary_bias[3] = {0.2F, -0.4F, 0.1F};
    float primary_sensor_rate[3];
    Algorithm_IMUFusion_Input input = make_input(
        acceleration, primary_sensor_bias, acceleration, secondary_bias,
        true, true, 0.010F);

    /* Body X=+sensor Y, body Y=-sensor X, body Z=+sensor Z. */
    config.primary_body_axis[0] = (Algorithm_IMUFusion_AxisMapping){
        ALGORITHM_IMU_FUSION_SENSOR_AXIS_Y, 1};
    config.primary_body_axis[1] = (Algorithm_IMUFusion_AxisMapping){
        ALGORITHM_IMU_FUSION_SENSOR_AXIS_X, -1};
    config.primary_body_axis[2] = (Algorithm_IMUFusion_AxisMapping){
        ALGORITHM_IMU_FUSION_SENSOR_AXIS_Z, 1};
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    CHECK(!fusion.snapshot.valid);
    for (unsigned int count = 0U; count < 5U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
        CHECK(snapshot.update_count == 0U);
        check_near(snapshot.euler.yaw_deg, 0.0F, 1.0e-7F);
    }
    CHECK(snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_COLLECTING);
    CHECK(!snapshot.gyro_bias_valid);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE);
    CHECK(snapshot.gyro_bias_valid);
    check_near(snapshot.primary_gyro_bias_dps[0], 2.0F, 1.0e-6F);
    check_near(snapshot.primary_gyro_bias_dps[1], -1.0F, 1.0e-6F);
    check_near(snapshot.primary_gyro_bias_dps[2], 0.3F, 1.0e-6F);
    check_near(snapshot.secondary_gyro_bias_dps[0], 0.2F, 1.0e-6F);
    check_near(snapshot.secondary_gyro_bias_dps[1], -0.4F, 1.0e-6F);
    check_near(snapshot.secondary_gyro_bias_dps[2], 0.1F, 1.0e-6F);
    check_near(snapshot.angular_rate_dps[0], 0.0F, 1.0e-6F);
    check_near(snapshot.angular_rate_dps[1], 0.0F, 1.0e-6F);
    check_near(snapshot.angular_rate_dps[2], 0.0F, 1.0e-6F);
    CHECK(snapshot.update_count == 1U);

    memcpy(primary_sensor_rate, primary_sensor_bias,
        sizeof(primary_sensor_rate));
    primary_sensor_rate[2] += 0.2F;
    input = make_input(acceleration, primary_sensor_rate,
        NULL, NULL, true, false, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.gyro_source_mask == 0x01U);
    CHECK(snapshot.angular_rate_dps[2] > 0.0F);
    CHECK(snapshot.angular_rate_dps[2] < 0.2F);
    check_near(snapshot.gyro_prediction_weight[2] +
        snapshot.primary_gyro_weight[2] +
        snapshot.secondary_gyro_weight[2], 1.0F, 1.0e-6F);
    CHECK(snapshot.gyro_prediction_weight[2] > 0.0F);
    CHECK(snapshot.primary_gyro_weight[2] > 0.0F);
    check_near(snapshot.secondary_gyro_weight[2], 0.0F, 1.0e-7F);
    CHECK(snapshot.update_count == 2U);
}

static void test_startup_calibration_restarts_on_motion_or_missing_sensor(void)
{
    const Algorithm_IMUFusion_Config config =
        startup_calibration_config(2U, 3U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float moving_acceleration[3] = {0.0F, 0.0F, 2.0F * TEST_GRAVITY};
    const float bias[3] = {1.0F, 0.0F, 0.0F};
    Algorithm_IMUFusion_Input input = make_input(
        acceleration, bias, acceleration, bias, true, true, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.calibration_settle_samples == 1U);
    input = make_input(moving_acceleration, bias, acceleration, bias,
        true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_SETTLING);
    CHECK(snapshot.calibration_settle_samples == 0U);
    CHECK(snapshot.calibration_samples_collected == 0U);
    CHECK(snapshot.calibration_restart_count == 1U);
    CHECK(snapshot.calibration_sample_rejected);
    CHECK(snapshot.update_count == 0U);

    input = make_input(acceleration, bias, acceleration, bias,
        true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    input.secondary_valid = false;
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.calibration_settle_samples == 0U);
    CHECK(snapshot.calibration_restart_count == 2U);
    CHECK(!snapshot.gyro_bias_valid);
}

static void test_startup_calibration_fits_filtered_bias_trend(void)
{
    Algorithm_IMUFusion_Config config = startup_calibration_config(1U, 10U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float primary_base[3] = {0.4F, -0.2F, 0.1F};
    const float primary_slope[3] = {0.2F, -0.1F, 0.05F};
    const float secondary_base[3] = {-0.3F, 0.25F, -0.05F};
    const float secondary_slope[3] = {-0.15F, 0.08F, 0.03F};
    float primary_rate[3];
    float secondary_rate[3];
    Algorithm_IMUFusion_Input input;

    config.calibration_filter_time_constant_seconds = 0.0F;
    config.enable_startup_bias_trend_fit = true;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    input = make_input(acceleration, primary_base,
        acceleration, secondary_base, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    for (unsigned int sample = 0U; sample < 10U; ++sample) {
        const float time = 0.010F * (float)sample;

        for (unsigned int axis = 0U; axis < 3U; ++axis) {
            primary_rate[axis] = primary_base[axis] +
                primary_slope[axis] * time;
            secondary_rate[axis] = secondary_base[axis] +
                secondary_slope[axis] * time;
        }
        input = make_input(acceleration, primary_rate,
            acceleration, secondary_rate, true, true, 0.010F);
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE);
    for (unsigned int axis = 0U; axis < 3U; ++axis) {
        check_near(snapshot.primary_gyro_bias_fit_slope_dps_per_second[axis],
            primary_slope[axis], 2.0e-5F);
        check_near(snapshot.secondary_gyro_bias_fit_slope_dps_per_second[axis],
            secondary_slope[axis], 2.0e-5F);
        check_near(snapshot.primary_gyro_bias_dps[axis],
            primary_base[axis] + 0.09F * primary_slope[axis], 2.0e-6F);
        check_near(snapshot.secondary_gyro_bias_dps[axis],
            secondary_base[axis] + 0.09F * secondary_slope[axis], 2.0e-6F);
    }
}

static void test_long_filtered_fit_recovers_window_end_bias(void)
{
    Algorithm_IMUFusion_Config config =
        startup_calibration_config(1U, 6000U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float initial_rate[3] = {0.4F, -0.3F, 0.1F};
    const float slope[3] = {0.002F, -0.001F, 0.0005F};
    float rate[3];
    Algorithm_IMUFusion_Input input;

    config.calibration_filter_time_constant_seconds = 1.0F;
    config.enable_startup_bias_trend_fit = true;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    input = make_input(acceleration, initial_rate,
        acceleration, initial_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    for (unsigned int sample = 0U; sample < 6000U; ++sample) {
        const float time = 0.010F * (float)sample;

        for (unsigned int axis = 0U; axis < 3U; ++axis) {
            rate[axis] = initial_rate[axis] + slope[axis] * time;
        }
        input = make_input(acceleration, rate,
            acceleration, rate, true, true, 0.010F);
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(snapshot.gyro_bias_valid);
    for (unsigned int axis = 0U; axis < 3U; ++axis) {
        check_near(snapshot.primary_gyro_bias_dps[axis],
            initial_rate[axis] + slope[axis] * 59.99F, 1.0e-3F);
        check_near(snapshot.primary_gyro_bias_fit_slope_dps_per_second[axis],
            slope[axis], 1.0e-4F);
    }
}

static void test_startup_calibration_rejects_excessive_noise(void)
{
    Algorithm_IMUFusion_Config config = startup_calibration_config(1U, 4U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    float rate[3] = {0.0F, 0.0F, 0.0F};
    Algorithm_IMUFusion_Input input;

    config.calibration_gyro_stddev_maximum_dps = 0.6F;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    input = make_input(acceleration, rate, acceleration, rate,
        true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    for (unsigned int count = 0U; count < 4U; ++count) {
        rate[0] = ((count & 1U) == 0U) ? -1.0F : 1.0F;
        input = make_input(acceleration, rate, acceleration, rate,
            true, true, 0.010F);
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_SETTLING);
    CHECK(snapshot.calibration_samples_collected == 0U);
    CHECK(snapshot.calibration_restart_count == 1U);
    CHECK(snapshot.calibration_sample_rejected);
    CHECK(!snapshot.gyro_bias_valid);
}

static void test_slow_rotation_is_not_zeroed_after_calibration(void)
{
    Algorithm_IMUFusion_Config config =
        startup_calibration_config(1U, 2U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float primary_bias[3] = {1.0F, 0.0F, -0.4F};
    const float secondary_bias[3] = {0.2F, 0.1F, 0.05F};
    float primary_rate[3];
    float secondary_rate[3];
    Algorithm_IMUFusion_Input input = make_input(
        acceleration, primary_bias, acceleration, secondary_bias,
        true, true, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    for (unsigned int count = 0U; count < 3U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(snapshot.gyro_bias_valid);
    memcpy(primary_rate, primary_bias, sizeof(primary_rate));
    memcpy(secondary_rate, secondary_bias, sizeof(secondary_rate));
    primary_rate[2] += 0.2F;
    secondary_rate[2] += 0.2F;
    input = make_input(acceleration, primary_rate,
        acceleration, secondary_rate, true, true, 0.010F);
    for (unsigned int count = 0U; count < 100U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    check_near(snapshot.angular_rate_dps[2], 0.2F, 5.0e-3F);
    check_near(snapshot.euler.yaw_deg, 0.2F, 0.02F);
}

static void test_fast_rotation_remains_sensitive_after_filtered_calibration(void)
{
    Algorithm_IMUFusion_Config config = startup_calibration_config(1U, 2U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float zero_rate[3] = {0.0F, 0.0F, 0.0F};
    const float fast_rate[3] = {0.0F, 0.0F, 90.0F};
    Algorithm_IMUFusion_Input input = make_input(
        acceleration, zero_rate, acceleration, zero_rate,
        true, true, 0.010F);

    config.calibration_filter_time_constant_seconds = 1.0F;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    for (unsigned int count = 0U; count < 3U; ++count) {
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(snapshot.gyro_bias_valid);
    input = make_input(acceleration, fast_rate,
        acceleration, fast_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.angular_rate_dps[2] > 89.0F);
    CHECK(snapshot.gyro_prediction_weight[2] < 0.01F);
    /* Endpoint samples use midpoint propagation.  Angular rate responds in
     * this frame while the angle has only the unavoidable half-sample delay. */
    check_near(snapshot.euler.yaw_deg, 0.45F, 0.04F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    check_near(snapshot.euler.yaw_deg, 1.35F, 0.05F);
}

static void test_secondary_only_and_lever_arm(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float primary_acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float primary_rate[3] = {0.0F, 0.0F, 572.9578F};
    const float secondary_acceleration[3] = {-3.5F, 0.0F, TEST_GRAVITY};
    Algorithm_IMUFusion_Input input;

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    input = make_input(primary_acceleration, primary_rate,
        secondary_acceleration, primary_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    check_near(snapshot.secondary_lever_acceleration_mps2[0], -3.5F, 0.08F);
    check_near(snapshot.acceleration_mps2[0], 0.0F, 0.08F);
    CHECK(snapshot.secondary_lever_compensation_used);
    CHECK(snapshot.acceleration_source_mask == 0x03U);

    input = make_input(NULL, NULL, primary_acceleration, primary_rate,
        false, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.gyro_source_mask == 0x02U);
    CHECK(snapshot.acceleration_source_mask == 0x02U);
}

static void test_transaction_and_boundaries(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion before;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    Algorithm_IMUFusion_Snapshot before_snapshot = snapshot;
    Algorithm_IMUFusion_Input input = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float rate[3] = {0.0F, 0.0F, 0.0F};

    snapshot.quaternion.w = 123.0F;
    before_snapshot = snapshot;
    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    before = fusion;
    input.dt_seconds = 0.010F;
    CHECK(!Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(memcmp(&fusion, &before, sizeof(fusion)) == 0);
    CHECK(memcmp(&snapshot, &before_snapshot, sizeof(snapshot)) == 0);

    input = make_input(acceleration, rate, NULL, NULL, true, false, 0.050F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    before = fusion;
    before_snapshot = snapshot;
    input.dt_seconds = 0.0501F;
    CHECK(!Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(memcmp(&fusion, &before, sizeof(fusion)) == 0);
    CHECK(memcmp(&snapshot, &before_snapshot, sizeof(snapshot)) == 0);

    before = fusion;
    before_snapshot = snapshot;
    input.dt_seconds = 0.010F;
    input.primary_acceleration_mps2[0] = NAN;
    CHECK(!Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(memcmp(&fusion, &before, sizeof(fusion)) == 0);
    CHECK(memcmp(&snapshot, &before_snapshot, sizeof(snapshot)) == 0);

    input = make_input(NULL, NULL, acceleration, rate, false, true, 0.010F);
    input.primary_acceleration_mps2[0] = NAN;
    input.primary_angular_rate_dps[0] = NAN;
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.gyro_source_mask == 0x02U);
}

static void test_disagreement_primary_fallback(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float primary_acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float secondary_acceleration[3] = {10.0F, 0.0F, TEST_GRAVITY};
    const float primary_rate[3] = {0.0F, 0.0F, 0.0F};
    const float secondary_rate[3] = {100.0F, 0.0F, 0.0F};
    const Algorithm_IMUFusion_Input input = make_input(
        primary_acceleration, primary_rate, secondary_acceleration,
        secondary_rate, true, true, 0.010F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.gyro_source_mask == 0x01U);
    CHECK(snapshot.acceleration_source_mask == 0x01U);
    check_near(snapshot.angular_rate_dps[0], 0.0F, 0.001F);
}

static void test_gyro_prediction_confidence_weights(void)
{
    Algorithm_IMUFusion_Config config = Algorithm_IMUFusion_DefaultConfig();
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float primary_rate[3] = {1.0F, 0.0F, 0.0F};
    const float secondary_rate[3] = {3.0F, 0.0F, 0.0F};
    Algorithm_IMUFusion_Input input;
    float settled_prediction_weight;

    config.primary_gyro_variance_dps2 = 0.01F;
    config.secondary_gyro_variance_dps2 = 0.09F;
    config.gyro_rate_process_noise_dps2_per_second = 0.001F;
    config.initial_gyro_rate_variance_dps2 = 0.1F;
    config.maximum_gyro_rate_prediction_variance_dps2 = 10000.0F;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    input = make_input(acceleration, primary_rate,
        acceleration, secondary_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    check_near(snapshot.gyro_prediction_weight[0], 0.0F, 1.0e-7F);
    check_near(snapshot.primary_gyro_weight[0], 0.9F, 1.0e-6F);
    check_near(snapshot.secondary_gyro_weight[0], 0.1F, 1.0e-6F);
    check_near(snapshot.angular_rate_dps[0], 1.2F, 1.0e-6F);

    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    settled_prediction_weight = snapshot.gyro_prediction_weight[0];
    CHECK(settled_prediction_weight > 0.1F);
    check_near(snapshot.gyro_prediction_weight[0] +
        snapshot.primary_gyro_weight[0] +
        snapshot.secondary_gyro_weight[0], 1.0F, 1.0e-6F);

    {
        const float fast_primary_rate[3] = {101.0F, 0.0F, 0.0F};
        const float fast_secondary_rate[3] = {103.0F, 0.0F, 0.0F};

        input = make_input(acceleration, fast_primary_rate,
            acceleration, fast_secondary_rate, true, true, 0.010F);
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    CHECK(snapshot.gyro_prediction_weight[0] <
        0.1F * settled_prediction_weight);
    CHECK(snapshot.angular_rate_dps[0] > 100.0F);
    check_near(snapshot.gyro_prediction_weight[0] +
        snapshot.primary_gyro_weight[0] +
        snapshot.secondary_gyro_weight[0], 1.0F, 1.0e-6F);
}

static void test_startup_noise_sets_per_axis_gyro_confidence(void)
{
    Algorithm_IMUFusion_Config config = startup_calibration_config(1U, 4U);
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float zero_rate[3] = {0.0F, 0.0F, 0.0F};
    const float primary_amplitude[3] = {0.05F, 0.10F, 0.20F};
    const float secondary_amplitude[3] = {0.20F, 0.10F, 0.05F};
    Algorithm_IMUFusion_Input input;

    config.primary_gyro_variance_dps2 = 0.01F;
    config.secondary_gyro_variance_dps2 = 0.01F;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    input = make_input(acceleration, zero_rate,
        acceleration, zero_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));

    for (unsigned int sample = 0U; sample < 4U; ++sample) {
        float primary_rate[3];
        float secondary_rate[3];
        const float sign = ((sample & 1U) == 0U) ? -1.0F : 1.0F;

        for (unsigned int axis = 0U; axis < 3U; ++axis) {
            primary_rate[axis] = sign * primary_amplitude[axis];
            secondary_rate[axis] = sign * secondary_amplitude[axis];
        }
        input = make_input(acceleration, primary_rate,
            acceleration, secondary_rate, true, true, 0.010F);
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }

    CHECK(snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE);
    check_near(snapshot.primary_gyro_weight[0], 16.0F / 17.0F, 1.0e-5F);
    check_near(snapshot.secondary_gyro_weight[0], 1.0F / 17.0F, 1.0e-5F);
    check_near(snapshot.primary_gyro_weight[1], 0.5F, 1.0e-5F);
    check_near(snapshot.secondary_gyro_weight[1], 0.5F, 1.0e-5F);
    check_near(snapshot.primary_gyro_weight[2], 1.0F / 17.0F, 1.0e-5F);
    check_near(snapshot.secondary_gyro_weight[2], 16.0F / 17.0F, 1.0e-5F);
}

static void test_gyro_disagreement_uses_prediction_innovation(void)
{
    Algorithm_IMUFusion_Config config = Algorithm_IMUFusion_DefaultConfig();
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    const float acceleration[3] = {0.0F, 0.0F, TEST_GRAVITY};
    const float zero_rate[3] = {0.0F, 0.0F, 0.0F};
    const float primary_outlier[3] = {100.0F, 0.0F, 0.0F};
    Algorithm_IMUFusion_Input input;

    config.primary_gyro_variance_dps2 = 0.01F;
    config.secondary_gyro_variance_dps2 = 0.09F;
    config.maximum_gyro_disagreement_dps = 10.0F;
    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    input = make_input(acceleration, zero_rate,
        acceleration, zero_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));

    input = make_input(acceleration, primary_outlier,
        acceleration, zero_rate, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.gyro_source_mask == 0x02U);
    check_near(snapshot.primary_gyro_weight[0], 0.0F, 1.0e-7F);
    CHECK(snapshot.secondary_gyro_weight[0] > 0.0F);
    check_near(snapshot.angular_rate_dps[0], 0.0F, 1.0e-6F);

    CHECK(Algorithm_IMUFusion_Init(&fusion, &config));
    CHECK(Algorithm_IMUFusion_Update(&fusion, &(Algorithm_IMUFusion_Input){
        .primary_acceleration_mps2 = {0.0F, 0.0F, TEST_GRAVITY},
        .secondary_acceleration_mps2 = {0.0F, 0.0F, TEST_GRAVITY},
        .primary_valid = true,
        .secondary_valid = true,
        .dt_seconds = 0.010F,
    }, &snapshot));
    input = make_input(acceleration, zero_rate,
        acceleration, primary_outlier, true, true, 0.010F);
    CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    CHECK(snapshot.gyro_source_mask == 0x01U);
    CHECK(snapshot.primary_gyro_weight[0] > 0.0F);
    check_near(snapshot.secondary_gyro_weight[0], 0.0F, 1.0e-7F);
}

static void test_long_run_covariance(void)
{
    Algorithm_IMUFusion fusion;
    Algorithm_IMUFusion_Snapshot snapshot = {0};
    Algorithm_IMUFusion_Input input;
    float acceleration[3];
    float rate[3];

    CHECK(Algorithm_IMUFusion_Init(&fusion, NULL));
    for (unsigned int count = 0U; count < 2000U; ++count) {
        const float phase = 0.01F * (float)count;
        const float roll = 12.0F * sinf(0.7F * phase);
        const float pitch = 8.0F * cosf(0.5F * phase);

        acceleration_for_euler(roll, pitch, acceleration);
        rate[0] = 30.0F * sinf(0.3F * phase);
        rate[1] = 25.0F * cosf(0.4F * phase);
        rate[2] = 45.0F * sinf(0.2F * phase);
        input = make_input(acceleration, rate, NULL, NULL,
            true, false, 0.010F);
        CHECK(Algorithm_IMUFusion_Update(&fusion, &input, &snapshot));
    }
    for (unsigned int row = 0U; row < ALGORITHM_IMU_FUSION_STATE_COUNT;
         ++row) {
        CHECK(isfinite(fusion.covariance[row][row]));
        CHECK(fusion.covariance[row][row] >= 0.0F);
        for (unsigned int column = row + 1U;
             column < ALGORITHM_IMU_FUSION_STATE_COUNT; ++column) {
            check_near(fusion.covariance[row][column],
                fusion.covariance[column][row], 1.0e-5F);
        }
    }
    CHECK(snapshot.update_count == 2000U);
}

int main(void)
{
    test_default_and_mapping();
    test_level_static_and_bias_disabled();
    test_known_tilt_seed();
    test_gravity_update_sign();
    test_gravity_correction_direction();
    test_gravity_update_does_not_observe_yaw();
    test_dynamic_acceleration_is_not_gravity();
    test_yaw_integration();
    test_coning_compensated_large_rotation();
    test_reinitialization_resets_yaw_zero();
    test_bias_estimation_is_deferred();
    test_startup_bias_calibration();
    test_startup_calibration_restarts_on_motion_or_missing_sensor();
    test_startup_calibration_fits_filtered_bias_trend();
    test_long_filtered_fit_recovers_window_end_bias();
    test_startup_calibration_rejects_excessive_noise();
    test_slow_rotation_is_not_zeroed_after_calibration();
    test_fast_rotation_remains_sensitive_after_filtered_calibration();
    test_secondary_only_and_lever_arm();
    test_transaction_and_boundaries();
    test_disagreement_primary_fallback();
    test_gyro_prediction_confidence_weights();
    test_startup_noise_sets_per_axis_gyro_confidence();
    test_gyro_disagreement_uses_prediction_innovation();
    test_long_run_covariance();
    if (g_failures != 0U) {
        (void)fprintf(stderr, "%u test assertion(s) failed\n", g_failures);
        return 1;
    }
    (void)puts("algorithm_imu_fusion tests: PASS");
    return 0;
}
