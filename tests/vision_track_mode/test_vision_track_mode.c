#include "vision_track_mode.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool near_value(float actual, float expected)
{
    return fabsf(actual - expected) < 0.001F;
}

static Mode_Car_VisionTrackConfig test_config(void)
{
    Mode_Car_VisionTrackConfig config =
        Mode_Car_VisionTrack_DefaultConfig();

    config.yaw_controller.kp = 8.0F;
    config.yaw_controller.ki = 0.0F;
    config.yaw_controller.kd = 0.0F;
    config.yaw_controller.integral_speed_limit = 0.0F;
    config.yaw_controller.maximum_wheel_speed = 80.0F;
    config.yaw_controller.angle_tolerance_deg = 0.0F;
    config.yaw_controller.rate_tolerance_deg_per_second = 0.0F;
    config.speed_controller.low_speed = (Mode_Car_SpeedBandConfig){0};
    config.speed_controller.high_speed = config.speed_controller.low_speed;
    config.speed_controller.high_speed_threshold = 400.0F;
    config.speed_controller.feedforward_percent_per_speed = 0.125F;
    config.speed_controller.target_smoothing_time_constant_seconds = 0.0F;
    config.speed_controller.maximum_target_speed = 480.0F;
    config.speed_controller.maximum_effort_percent = 80.0F;
    return config;
}

static void test_visual_yaw_speed_cascade(void)
{
    Mode_Car_VisionTrack mode;
    Mode_Car_VisionTrackOutput output;
    const Mode_Car_VisionTrackConfig config = test_config();
    const Mode_Car_VisionTrackInput input = {
        .dt_seconds = 0.01F,
        .track_valid = true,
        .control_error_x = 0.2F,
        .control_heading_deg = 2.0F,
        .measured_yaw_deg = 179.0F,
        .measured_yaw_rate_deg_per_second = 0.0F,
        .measured_speed_a_mm_per_second = 0.0F,
        .measured_speed_b_mm_per_second = 0.0F,
    };

    assert(Mode_Car_VisionTrack_Init(&mode, &config));
    assert(Mode_Car_VisionTrack_Update(&mode, &input, &output));
    assert(output.track_active);
    assert(near_value(output.steering_error_deg, 5.0F));
    assert(near_value(output.target_yaw_deg, 184.0F));
    assert(near_value(output.turn_speed_mm_per_second, 40.0F));
    assert(near_value(output.target_speed_a_mm_per_second, 440.0F));
    assert(near_value(output.target_speed_b_mm_per_second, 360.0F));
    assert(near_value(output.effort_a_percent, 55.0F));
    assert(near_value(output.effort_b_percent, 45.0F));
}

static void test_limit_and_lost_stop(void)
{
    Mode_Car_VisionTrack mode;
    Mode_Car_VisionTrackOutput output;
    const Mode_Car_VisionTrackConfig config = test_config();
    Mode_Car_VisionTrackInput input = {
        .dt_seconds = 0.01F,
        .track_valid = true,
        .control_error_x = 1.0F,
        .control_heading_deg = 30.0F,
        .measured_yaw_deg = -179.0F,
        .measured_yaw_rate_deg_per_second = 0.0F,
        .measured_speed_a_mm_per_second = 0.0F,
        .measured_speed_b_mm_per_second = 0.0F,
    };

    assert(Mode_Car_VisionTrack_Init(&mode, &config));
    assert(Mode_Car_VisionTrack_Update(&mode, &input, &output));
    assert(near_value(output.steering_error_deg, 10.0F));
    assert(near_value(output.target_speed_a_mm_per_second, 480.0F));
    assert(near_value(output.target_speed_b_mm_per_second, 320.0F));

    input.track_valid = false;
    assert(Mode_Car_VisionTrack_Update(&mode, &input, &output));
    assert(!output.track_active);
    assert(near_value(output.target_speed_a_mm_per_second, 0.0F));
    assert(near_value(output.target_speed_b_mm_per_second, 0.0F));
    assert(near_value(output.effort_a_percent, 0.0F));
    assert(near_value(output.effort_b_percent, 0.0F));
}

int main(void)
{
    test_visual_yaw_speed_cascade();
    test_limit_and_lost_stop();
    puts("Vision track mode tests: PASS");
    return 0;
}
