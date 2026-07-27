#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_line_sensor.h"
#include "device_line_tracking.h"
#include "device_xinliu_t8.h"
#include "mode_car.h"
#include "mode_car_control.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n",                           \
                __FILE__, __LINE__, #condition);                             \
            return false;                                                    \
        }                                                                    \
    } while (0)

static const uint8_t g_manual_response[
    DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE] = {
    0xAAU, 0x55U, 0x19U, 0x10U,
    0x85U, 0x0BU, 0xB4U, 0x0BU,
    0xFFU, 0x0AU, 0x81U, 0x08U,
    0xB8U, 0x05U, 0x43U, 0x01U,
    0xA8U, 0x08U, 0x53U, 0x06U,
    0x14U, 0x66U,
};

size_t BSP_LineSensor_Count(void)
{
    return 0U;
}

BSP_LineSensor_Handle BSP_LineSensor_GetHandle(size_t index)
{
    (void)index;
    return NULL;
}

bool BSP_LineSensor_GetInfo(
    BSP_LineSensor_Handle sensor, BSP_LineSensor_Info *info)
{
    (void)sensor;
    (void)info;
    return false;
}

bool BSP_LineSensor_Read(
    BSP_LineSensor_Handle sensor, BSP_LineSensor_RawSample *sample)
{
    (void)sensor;
    (void)sample;
    return false;
}

static Device_XinliuT8_FeedResult feed_frame(
    Device_XinliuT8 *device,
    const uint8_t *frame,
    size_t length,
    uint32_t now_ms)
{
    Device_XinliuT8_FeedResult result =
        DEVICE_XINLIU_T8_FEED_IN_PROGRESS;

    for (size_t index = 0U; index < length; ++index) {
        result = Device_XinliuT8_FeedByte(device, now_ms, frame[index]);
    }
    return result;
}

static bool nearly_equal(float actual, float expected, float tolerance)
{
    return fabsf(actual - expected) <= tolerance;
}

static bool test_query_and_manual_response(void)
{
    static const uint8_t expected_query[
        DEVICE_XINLIU_T8_READ_ANALOG_QUERY_SIZE] = {
        0xAAU, 0x55U, 0x00U, 0x01U, 0x19U, 0x1AU, 0x66U,
    };
    static const uint16_t expected_adc[
        DEVICE_XINLIU_T8_CHANNEL_COUNT] = {
        2949U, 2996U, 2815U, 2177U,
        1464U, 323U, 2216U, 1619U,
    };
    Device_XinliuT8 device;
    Device_XinliuT8_Snapshot snapshot;
    Device_XinliuT8_Stats stats;
    uint8_t query[DEVICE_XINLIU_T8_READ_ANALOG_QUERY_SIZE];
    size_t query_length = 99U;

    CHECK(Device_XinliuT8_Init(&device));
    CHECK(Device_XinliuT8_BuildReadAnalogQuery(
        query, sizeof(query), &query_length));
    CHECK(query_length == sizeof(expected_query));
    CHECK(memcmp(query, expected_query, sizeof(expected_query)) == 0);
    CHECK(feed_frame(&device, g_manual_response,
        sizeof(g_manual_response), 123U) ==
        DEVICE_XINLIU_T8_FEED_SAMPLE_READY);
    CHECK(Device_XinliuT8_GetLatest(&device, &snapshot));
    CHECK(snapshot.received_at_ms == 123U);
    CHECK(snapshot.sequence == 1U);
    for (size_t index = 0U;
         index < DEVICE_XINLIU_T8_CHANNEL_COUNT;
         ++index) {
        CHECK(snapshot.adc[index] == expected_adc[index]);
    }
    CHECK(Device_XinliuT8_GetStats(&device, &stats));
    CHECK(stats.accepted_frames == 1U);
    CHECK(stats.rejected_frames == 0U);
    CHECK(Device_XinliuT8_SnapshotFresh(&snapshot, 173U, 50U));
    CHECK(!Device_XinliuT8_SnapshotFresh(&snapshot, 174U, 50U));
    return true;
}

static bool test_rejection_and_resynchronization(void)
{
    Device_XinliuT8 device;
    Device_XinliuT8_Stats stats;
    uint8_t damaged[DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE];
    uint8_t wrong_command[DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE];
    const uint8_t noise[] = {0x01U, 0x02U, 0xAAU, 0x03U, 0xAAU};

    CHECK(Device_XinliuT8_Init(&device));
    memcpy(damaged, g_manual_response, sizeof(damaged));
    damaged[20] ^= 0x01U;
    CHECK(feed_frame(&device, damaged, sizeof(damaged), 10U) ==
        DEVICE_XINLIU_T8_FEED_FRAME_REJECTED);

    memcpy(wrong_command, g_manual_response, sizeof(wrong_command));
    wrong_command[2] = 0x18U;
    wrong_command[20] = 0x13U;
    CHECK(feed_frame(&device, wrong_command, sizeof(wrong_command), 20U) ==
        DEVICE_XINLIU_T8_FEED_FRAME_REJECTED);

    CHECK(feed_frame(&device, noise, sizeof(noise), 30U) ==
        DEVICE_XINLIU_T8_FEED_IN_PROGRESS);
    Device_XinliuT8_ResetStream(&device);
    CHECK(feed_frame(&device, g_manual_response,
        sizeof(g_manual_response), 40U) ==
        DEVICE_XINLIU_T8_FEED_SAMPLE_READY);
    CHECK(Device_XinliuT8_GetStats(&device, &stats));
    CHECK(stats.accepted_frames == 1U);
    CHECK(stats.rejected_frames == 2U);
    CHECK(stats.checksum_errors == 1U);
    CHECK(stats.format_errors == 1U);
    CHECK(stats.discarded_bytes >= 2U);
    return true;
}

static bool test_normalization_difference_sum_and_modes(void)
{
    static const float product_weight[
        DEVICE_XINLIU_T8_CHANNEL_COUNT] = {
        -1.0F, -0.60F, -0.25F, 0.0F,
         0.0F, 0.25F, 0.60F, 1.0F,
    };
    Device_LineTracking_Config config;
    Device_LineTracking device;
    Device_LineTracking inner_device;
    Device_LineTracking edge_device;
    Device_LineTracking relative_device;
    Device_LineTracking_RawSample raw = {
        .channel_count = DEVICE_XINLIU_T8_CHANNEL_COUNT,
        .valid_mask = UINT32_C(0xFF),
    };
    Device_LineTracking_Sample sample;
    Device_LineTracking_Sample inner_sample;
    Device_LineTracking_Sample edge_sample;
    Device_LineTracking_Sample relative_sample;
    Mode_Car_AnalogTrackingConfig analog_config =
        Mode_Car_AnalogTracking_DefaultConfig();
    Mode_Car_AnalogTrackingContext analog_mode;
    Mode_Car_AnalogTrackingInput analog_input;
    Mode_Car_DigitalTrackingConfig digital_config =
        Mode_Car_DigitalTracking_DefaultConfig();
    Mode_Car_DigitalTrackingContext digital_mode;
    Mode_Car_DigitalTrackingInput digital_input;
    Mode_Car_Output output;
    Mode_Car_AngleControllerConfig angle_config =
        Mode_Car_AngleController_DefaultConfig();
    Mode_Car_AngleController angle_controller;
    Mode_Car_AngleControllerInput angle_input;
    Mode_Car_AngleControllerOutput angle_output;
    Mode_Car_SpeedControllerConfig speed_config =
        Mode_Car_SpeedController_DefaultConfig();
    Mode_Car_SpeedController speed_controller;
    Mode_Car_SpeedControllerInput speed_input;
    Mode_Car_SpeedControllerOutput speed_output;
    float steering_drive;

    CHECK(Device_LineTracking_DefaultRawConfig(
        DEVICE_XINLIU_T8_CHANNEL_COUNT, 0U, 4095U, &config));
    config.intersection_minimum_active_channels = 6U;
    config.estimator.difference_weight = 1.0F;
    for (size_t index = 0U;
         index < DEVICE_XINLIU_T8_CHANNEL_COUNT;
         ++index) {
        config.channel[index].position_weight = product_weight[index];
    }
    CHECK(Device_LineTracking_Init(&device, &config));
    raw.value[7] = 4095U;
    CHECK(Device_LineTracking_UpdateAnalogRaw(&device, &raw, 100U));
    CHECK(Device_LineTracking_GetSample(&device, &sample));
    CHECK(nearly_equal(sample.normalized[7], 1.0F, 0.0001F));
    CHECK(nearly_equal(sample.line_strength[7], 1.0F, 0.0001F));
    CHECK(sample.line_present);
    CHECK(sample.difference_ratio > 0.9F);
    CHECK(sample.centroid_position > 0.99F);
    CHECK(sample.error > 0.0F);
    CHECK(nearly_equal(sample.error, sample.difference_ratio, 0.0001F));

    CHECK(Device_LineTracking_Init(&inner_device, &config));
    raw.value[7] = 0U;
    raw.value[4] = 4095U;
    CHECK(Device_LineTracking_UpdateAnalogRaw(&inner_device, &raw, 100U));
    CHECK(Device_LineTracking_GetSample(&inner_device, &inner_sample));
    CHECK(nearly_equal(inner_sample.difference_ratio, 0.0F, 0.0001F));
    CHECK(nearly_equal(inner_sample.error, 0.0F, 0.0001F));
    CHECK(inner_sample.confidence > 0.9F);

    CHECK(Device_LineTracking_Init(&edge_device, &config));
    raw.value[4] = 0U;
    raw.value[5] = 4095U;
    CHECK(Device_LineTracking_UpdateAnalogRaw(&edge_device, &raw, 100U));
    CHECK(Device_LineTracking_GetSample(&edge_device, &edge_sample));
    CHECK(edge_sample.difference_ratio > 0.80F);
    CHECK(edge_sample.difference_ratio < sample.difference_ratio);

    CHECK(Device_LineTracking_Init(&relative_device, &config));
    raw.value[0] = 160U;
    raw.value[1] = 159U;
    raw.value[2] = 159U;
    raw.value[3] = 162U;
    raw.value[4] = 157U;
    raw.value[5] = 152U;
    raw.value[6] = 162U;
    raw.value[7] = 157U;
    CHECK(Device_LineTracking_UpdateAnalogRaw(
        &relative_device, &raw, 100U));
    CHECK(Device_LineTracking_GetSample(&relative_device, &relative_sample));
    CHECK(!relative_sample.line_present);
    CHECK(relative_sample.line_lost);

    raw.value[0] = 171U;
    raw.value[1] = 178U;
    raw.value[2] = 202U;
    raw.value[3] = 474U;
    raw.value[4] = 188U;
    raw.value[5] = 156U;
    raw.value[6] = 176U;
    raw.value[7] = 182U;
    CHECK(Device_LineTracking_UpdateAnalogRaw(
        &relative_device, &raw, 110U));
    CHECK(Device_LineTracking_GetSample(&relative_device, &relative_sample));
    CHECK(relative_sample.line_present);
    CHECK(!relative_sample.line_lost);
    CHECK(relative_sample.strength_sum > 0.10F);
    CHECK(relative_sample.confidence > 0.80F);
    CHECK(relative_sample.difference_ratio < 0.0F);

    analog_config.steering.kp = 1.5F;
    analog_config.steering.ki = 0.0F;
    analog_config.steering.kd = 0.03F;
    analog_config.steering.maximum_correction = 0.8F;
    CHECK(Mode_Car_AnalogTracking_Init(&analog_mode, &analog_config));
    analog_input = (Mode_Car_AnalogTrackingInput){
        .time = {.now_ms = 100U, .dt_seconds = 0.01F},
        .base_drive = 0.35F,
        .analog_error = sample.error,
        .confidence = sample.confidence,
        .line_valid = sample.line_present,
        .line_lost = sample.line_lost,
        .intersection = sample.intersection,
    };
    CHECK(Mode_Car_AnalogTracking_Update(
        &analog_mode, &analog_input, &output));
    CHECK(output.motion_requested);
    CHECK(output.left_drive > output.right_drive);

    steering_drive = 0.5F *
        (output.left_drive - output.right_drive);
    CHECK(fabsf(steering_drive * 31.25F) <= 25.0F);
    angle_config.kp = 10.0F;
    angle_config.ki = 0.0F;
    angle_config.kd = 0.8F;
    angle_config.integral_speed_limit = 0.0F;
    angle_config.maximum_wheel_speed = 1050.0F;
    CHECK(Mode_Car_AngleController_Init(
        &angle_controller, &angle_config));
    angle_input = (Mode_Car_AngleControllerInput){
        .dt_seconds = 0.01F,
        /* Product mounting sign: line right requires negative yaw. */
        .target_yaw_deg = -steering_drive * 31.25F,
        .measured_yaw_deg = 0.0F,
        .measured_yaw_rate_deg_per_second = 0.0F,
    };
    CHECK(Mode_Car_AngleController_Update(
        &angle_controller, &angle_input, &angle_output));
    CHECK(angle_output.turn_wheel_speed < 0.0F);

    speed_config.low_speed = (Mode_Car_SpeedBandConfig){
        .kp = 0.10F,
        .ki = 0.80F,
        .kd = 0.0F,
    };
    speed_config.high_speed = speed_config.low_speed;
    speed_config.feedforward_percent_per_speed = 0.1250F;
    speed_config.target_smoothing_time_constant_seconds = 0.050F;
    speed_config.high_speed_threshold = 250.0F;
    speed_config.maximum_target_speed = 800.0F;
    speed_config.maximum_effort_percent = 80.0F;
    CHECK(Mode_Car_SpeedController_Init(
        &speed_controller, &speed_config));
    speed_input = (Mode_Car_SpeedControllerInput){
        .dt_seconds = 0.01F,
        .target_left_speed = 250.0F + angle_output.turn_wheel_speed,
        .target_right_speed = 250.0F - angle_output.turn_wheel_speed,
        .measured_left_speed = 0.0F,
        .measured_right_speed = 0.0F,
    };
    CHECK(Mode_Car_SpeedController_Update(
        &speed_controller, &speed_input, &speed_output));
    CHECK(speed_input.target_left_speed < speed_input.target_right_speed);
    CHECK(speed_output.left_effort_percent <
        speed_output.right_effort_percent);

    analog_input.confidence = 0.0F;
    CHECK(Mode_Car_AnalogTracking_Update(
        &analog_mode, &analog_input, &output));
    CHECK(!output.motion_requested);
    CHECK(nearly_equal(output.left_drive, 0.0F, 0.0001F));
    CHECK(nearly_equal(output.right_drive, 0.0F, 0.0001F));

    memset(raw.value, 0, sizeof(raw.value));
    CHECK(Device_LineTracking_UpdateAnalogRaw(&device, &raw, 110U));
    CHECK(Device_LineTracking_GetSample(&device, &sample));
    CHECK(sample.line_lost);

    CHECK(Mode_Car_DigitalTracking_Init(&digital_mode, &digital_config));
    digital_input = (Mode_Car_DigitalTrackingInput){
        .time = {.now_ms = 120U, .dt_seconds = 0.01F},
        .base_drive = 0.35F,
        .digital_error = 0.5F,
        .line_valid = true,
        .line_lost = false,
        .intersection = false,
    };
    CHECK(Mode_Car_DigitalTracking_Update(
        &digital_mode, &digital_input, &output));
    CHECK(output.left_drive > output.right_drive);
    return true;
}

static bool test_per_channel_calibration(void)
{
    Device_LineTracking_Config config;
    Device_LineTracking device;
    Device_LineTracking_RawSample raw = {
        .channel_count = DEVICE_XINLIU_T8_CHANNEL_COUNT,
        .valid_mask = UINT32_C(0xFF),
    };
    Device_LineTracking_Sample sample;

    CHECK(Device_LineTracking_DefaultRawConfig(
        DEVICE_XINLIU_T8_CHANNEL_COUNT, 0U, 4095U, &config));
    config.channel[0].calibration_min = 100U;
    config.channel[0].calibration_max = 1100U;
    raw.value[0] = 600U;
    CHECK(Device_LineTracking_Init(&device, &config));
    CHECK(Device_LineTracking_UpdateAnalogRaw(&device, &raw, 1U));
    CHECK(Device_LineTracking_GetSample(&device, &sample));
    CHECK(nearly_equal(sample.normalized[0], 0.5F, 0.0001F));
    CHECK(nearly_equal(sample.line_strength[0], 0.5F, 0.0001F));
    return true;
}

int main(void)
{
    CHECK(test_query_and_manual_response());
    CHECK(test_rejection_and_resynchronization());
    CHECK(test_normalization_difference_sum_and_modes());
    CHECK(test_per_channel_calibration());
    puts("Xinliu T8 analog line-tracking tests: PASS");
    return 0;
}
