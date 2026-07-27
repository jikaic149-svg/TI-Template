#include "mode_car_fused_navigation.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int close_enough(float actual, float expected, float tolerance)
{
    return fabsf(actual - expected) <= tolerance;
}

static Mode_Car_FusedNavigation make_mode(float correction_tau)
{
    Mode_Car_FusedNavigation mode;
    Mode_Car_FusedNavigationConfig config =
        Mode_Car_FusedNavigation_DefaultConfig();

    config.track_width_mm = 100.0F;
    config.heading_correction_time_constant_seconds = correction_tau;
    assert(Mode_Car_FusedNavigation_Init(&mode, &config));
    return mode;
}

static Mode_Car_FusedNavigation make_slip_gate_mode(
    float correction_tau, uint32_t recovery_sample_count)
{
    Mode_Car_FusedNavigation mode;
    Mode_Car_FusedNavigationConfig config =
        Mode_Car_FusedNavigation_DefaultConfig();

    config.track_width_mm = 100.0F;
    config.heading_correction_time_constant_seconds = correction_tau;
    config.slip_gate_enabled = true;
    config.slip_gate_enter_threshold_deg_per_second = 25.0F;
    config.slip_gate_exit_threshold_deg_per_second = 12.0F;
    config.slip_gate_recovery_sample_count = recovery_sample_count;
    assert(Mode_Car_FusedNavigation_Init(&mode, &config));
    return mode;
}

static Mode_Car_FusedNavigationInput input_zero(void)
{
    const Mode_Car_FusedNavigationInput input = {
        .dt_seconds = 0.01F,
        .lookahead_x_mm = 100.0F,
    };
    return input;
}

static void test_straight_odometry(void)
{
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.left_distance_delta_mm = 10.0F;
    input.right_distance_delta_mm = 10.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(output.pose.x_mm, 10.0F, 0.001F));
    assert(close_enough(output.pose.y_mm, 0.0F, 0.001F));
    assert(close_enough(output.pose.yaw_deg, 0.0F, 0.001F));
}

static void test_encoder_heading_reference(void)
{
    Mode_Car_FusedNavigation mode = make_mode(0.0F);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.right_distance_delta_mm = 10.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(
        output.encoder_heading_delta_deg, 5.729578F, 0.001F));
    assert(close_enough(output.pose.yaw_deg, 5.729578F, 0.001F));
    assert(output.pose.y_mm > 0.0F);
}

static void test_gyro_heading_without_encoder_correction(void)
{
    Mode_Car_FusedNavigation mode;
    Mode_Car_FusedNavigationConfig config =
        Mode_Car_FusedNavigation_DefaultConfig();
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    config.track_width_mm = 100.0F;
    config.encoder_heading_correction_enabled = false;
    config.slip_gate_enabled = true;
    assert(Mode_Car_FusedNavigation_Init(&mode, &config));
    input.right_distance_delta_mm = 10.0F;
    input.measured_yaw_rate_deg_per_second = 20.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(
        output.encoder_heading_delta_deg, 5.729578F, 0.001F));
    assert(close_enough(output.pose.yaw_deg, 0.2F, 0.001F));
    assert(!output.slip_gate_active);
    assert(output.slip_residual_deg_per_second > 500.0F);
}

static void test_encoder_slip_is_rejected_and_raw_heading_is_retained(void)
{
    Mode_Car_FusedNavigation mode = make_slip_gate_mode(0.0F, 3U);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.right_distance_delta_mm = 1.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(output.slip_gate_active);
    assert(close_enough(
        output.encoder_yaw_rate_deg_per_second, 57.29578F, 0.001F));
    assert(close_enough(
        output.slip_residual_deg_per_second, 57.29578F, 0.001F));
    assert(close_enough(output.pose.yaw_deg, 0.0F, 0.001F));
    assert(close_enough(mode.encoder_heading_deg, 0.572958F, 0.001F));
    assert(close_enough(mode.heading_reference_deg, 0.0F, 0.001F));
}

static void test_external_rotation_uses_gyro_while_gate_is_active(void)
{
    Mode_Car_FusedNavigation mode = make_slip_gate_mode(0.0F, 3U);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.measured_yaw_rate_deg_per_second = 30.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(output.slip_gate_active);
    assert(close_enough(output.pose.yaw_deg, 0.3F, 0.001F));
    assert(close_enough(mode.encoder_heading_deg, 0.0F, 0.001F));
    assert(close_enough(mode.heading_reference_deg, 0.3F, 0.001F));
}

static void test_slip_gate_hysteresis_and_confirmed_recovery(void)
{
    Mode_Car_FusedNavigation mode = make_slip_gate_mode(2.0F, 3U);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;
    unsigned int sample;

    input.right_distance_delta_mm = 1.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(output.slip_gate_active);

    input = input_zero();
    input.measured_yaw_rate_deg_per_second = 20.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(output.slip_gate_active);
    assert(mode.slip_gate_recovery_sample_count == 0U);

    input.measured_yaw_rate_deg_per_second = 0.0F;
    for (sample = 1U; sample < 3U; ++sample) {
        assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
        assert(output.slip_gate_active);
        assert(mode.slip_gate_recovery_sample_count == sample);
    }
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(!output.slip_gate_active);
    assert(mode.slip_gate_recovery_sample_count == 0U);
    assert(close_enough(mode.encoder_heading_deg, 0.572958F, 0.001F));
    assert(close_enough(mode.heading_reference_deg, 0.2F, 0.001F));
    assert(close_enough(output.heading_innovation_deg, 0.0F, 0.001F));
}

static void test_invalid_slip_gate_config_is_rejected(void)
{
    Mode_Car_FusedNavigation mode;
    Mode_Car_FusedNavigationConfig config =
        Mode_Car_FusedNavigation_DefaultConfig();

    config.slip_gate_recovery_sample_count = 0U;
    assert(!Mode_Car_FusedNavigation_Init(&mode, &config));
    config = Mode_Car_FusedNavigation_DefaultConfig();
    config.slip_gate_exit_threshold_deg_per_second =
        config.slip_gate_enter_threshold_deg_per_second;
    assert(!Mode_Car_FusedNavigation_Init(&mode, &config));
}

static void test_straight_lookahead(void)
{
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.forward_speed_mm_per_second = 400.0F;
    input.measured_left_speed_mm_per_second = 400.0F;
    input.measured_right_speed_mm_per_second = 400.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(
        output.target_yaw_rate_deg_per_second, 0.0F, 0.001F));
    assert(close_enough(
        output.target_left_speed_mm_per_second, 400.0F, 0.001F));
    assert(close_enough(
        output.target_right_speed_mm_per_second, 400.0F, 0.001F));
}

static void test_left_radius_400_lookahead(void)
{
    const float radius_mm = 400.0F;
    const float arc_angle_rad = 0.5F;
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.lookahead_x_mm = radius_mm * sinf(arc_angle_rad);
    input.lookahead_y_mm =
        radius_mm * (1.0F - cosf(arc_angle_rad));
    input.forward_speed_mm_per_second = 400.0F;
    input.measured_left_speed_mm_per_second = 400.0F;
    input.measured_right_speed_mm_per_second = 400.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(
        output.target_yaw_rate_deg_per_second, 57.29578F, 0.01F));
    assert(output.target_right_speed_mm_per_second >
        output.target_left_speed_mm_per_second);
}

static void test_front_offset_does_not_shrink_radius(void)
{
    const float front_radius_mm = 400.0F;
    const float tracking_offset_mm = 210.0F;
    const float drive_axle_radius_mm = sqrtf(
        (front_radius_mm * front_radius_mm) -
        (tracking_offset_mm * tracking_offset_mm));
    const float arc_angle_rad = 0.5F;
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.tracking_point_offset_mm = tracking_offset_mm;
    input.lookahead_x_mm =
        (tracking_offset_mm * cosf(arc_angle_rad)) +
        (drive_axle_radius_mm * sinf(arc_angle_rad));
    input.lookahead_y_mm =
        (drive_axle_radius_mm * (1.0F - cosf(arc_angle_rad))) +
        (tracking_offset_mm * sinf(arc_angle_rad));
    input.forward_speed_mm_per_second = 450.0F;
    input.measured_left_speed_mm_per_second = 450.0F;
    input.measured_right_speed_mm_per_second = 450.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(
        output.target_yaw_rate_deg_per_second,
        (450.0F / drive_axle_radius_mm) * 57.2957795F,
        0.01F));
}

static void test_target_yaw_rate_uses_current_speed(void)
{
    const float radius_mm = 400.0F;
    const float arc_angle_rad = 0.5F;
    Mode_Car_FusedNavigation mode;
    Mode_Car_FusedNavigationConfig config =
        Mode_Car_FusedNavigation_DefaultConfig();
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    config.track_width_mm = 140.0F;
    config.pure_pursuit_curvature_gain = 1.0F;
    assert(Mode_Car_FusedNavigation_Init(&mode, &config));
    input.lookahead_x_mm = radius_mm * sinf(arc_angle_rad);
    input.lookahead_y_mm =
        radius_mm * (1.0F - cosf(arc_angle_rad));
    input.forward_speed_mm_per_second = 450.0F;
    input.measured_left_speed_mm_per_second = 500.0F;
    input.measured_right_speed_mm_per_second = 500.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(
        output.target_yaw_rate_deg_per_second, 71.61972F, 0.01F));
}

static void test_curvature_gain(void)
{
    Mode_Car_FusedNavigation mode;
    Mode_Car_FusedNavigationConfig config =
        Mode_Car_FusedNavigation_DefaultConfig();
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    config.track_width_mm = 120.0F;
    config.pure_pursuit_curvature_gain = 0.5F;
    assert(Mode_Car_FusedNavigation_Init(&mode, &config));
    input.lookahead_x_mm = 100.0F;
    input.lookahead_y_mm = 100.0F;
    input.forward_speed_mm_per_second = 400.0F;
    input.measured_left_speed_mm_per_second = 400.0F;
    input.measured_right_speed_mm_per_second = 400.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(
        output.target_yaw_rate_deg_per_second, 114.59156F, 0.01F));
}

static void test_right_turn_direction(void)
{
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.lookahead_y_mm = -30.0F;
    input.forward_speed_mm_per_second = 400.0F;
    input.measured_left_speed_mm_per_second = 400.0F;
    input.measured_right_speed_mm_per_second = 400.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(output.target_yaw_rate_deg_per_second < 0.0F);
    assert(output.target_left_speed_mm_per_second >
        output.target_right_speed_mm_per_second);
}

static void test_h_problem_ab_heading_self_steers_to_ac(void)
{
    const float start_to_ac_heading_rad =
        atan2f(-800.0F, 1000.0F);
    const float lookahead_distance_mm = 300.0F;
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    const Mode_Car_FusedNavigationPose drive_axle_start_pose = {
        .x_mm = -210.0F,
    };
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    assert(Mode_Car_FusedNavigation_ResetPose(
        &mode, &drive_axle_start_pose));
    input.tracking_point_offset_mm = 210.0F;
    input.lookahead_x_mm =
        lookahead_distance_mm * cosf(start_to_ac_heading_rad);
    input.lookahead_y_mm =
        lookahead_distance_mm * sinf(start_to_ac_heading_rad);
    input.forward_speed_mm_per_second = 450.0F;
    input.measured_left_speed_mm_per_second = 450.0F;
    input.measured_right_speed_mm_per_second = 450.0F;

    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(output.tracking_x_mm, 0.0F, 0.001F));
    assert(close_enough(output.tracking_y_mm, 0.0F, 0.001F));
    assert(close_enough(output.target_heading_deg, -38.65981F, 0.01F));
    assert(output.target_yaw_rate_deg_per_second < 0.0F);
    assert(output.target_left_speed_mm_per_second >
        output.target_right_speed_mm_per_second);
}

static void test_shortest_heading_across_wrap(void)
{
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    Mode_Car_FusedNavigationPose pose = {
        .yaw_deg = 179.0F,
    };
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    assert(Mode_Car_FusedNavigation_ResetPose(&mode, &pose));
    input.lookahead_x_mm = 100.0F * cosf(-179.0F * 0.01745329252F);
    input.lookahead_y_mm = 100.0F * sinf(-179.0F * 0.01745329252F);
    input.forward_speed_mm_per_second = 400.0F;
    input.measured_left_speed_mm_per_second = 400.0F;
    input.measured_right_speed_mm_per_second = 400.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(output.heading_error_deg, 2.0F, 0.001F));
    assert(output.target_yaw_rate_deg_per_second > 0.0F);
    assert(output.target_right_speed_mm_per_second >
        output.target_left_speed_mm_per_second);
}

static void test_invalid_input_does_not_advance(void)
{
    Mode_Car_FusedNavigation mode = make_mode(2.0F);
    Mode_Car_FusedNavigationInput input = input_zero();
    Mode_Car_FusedNavigationOutput output;

    input.left_distance_delta_mm = 4.0F;
    input.right_distance_delta_mm = 4.0F;
    assert(Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    input.dt_seconds = NAN;
    assert(!Mode_Car_FusedNavigation_Update(&mode, &input, &output));
    assert(close_enough(mode.pose.x_mm, 4.0F, 0.001F));
    assert(close_enough(output.pose.x_mm, 0.0F, 0.001F));
}

int main(void)
{
    test_straight_odometry();
    test_encoder_heading_reference();
    test_gyro_heading_without_encoder_correction();
    test_encoder_slip_is_rejected_and_raw_heading_is_retained();
    test_external_rotation_uses_gyro_while_gate_is_active();
    test_slip_gate_hysteresis_and_confirmed_recovery();
    test_invalid_slip_gate_config_is_rejected();
    test_straight_lookahead();
    test_left_radius_400_lookahead();
    test_front_offset_does_not_shrink_radius();
    test_target_yaw_rate_uses_current_speed();
    test_curvature_gain();
    test_right_turn_direction();
    test_h_problem_ab_heading_self_steers_to_ac();
    test_shortest_heading_across_wrap();
    test_invalid_input_does_not_advance();
    puts("Fused navigation Mode tests: PASS");
    return 0;
}
