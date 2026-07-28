#include "mode_car_fused_navigation.h"
#include "core_config.h"

#include <math.h>
#include <stddef.h>

#define MODE_CAR_DEG_PER_RAD 57.29577951308232F
#define MODE_CAR_RAD_PER_DEG 0.017453292519943295F

static bool finite_value(float value)
{
    return isfinite(value) != 0;
}

static void clear_output(Mode_Car_FusedNavigationOutput *output)
{
    if (output != NULL) {
        *output = (Mode_Car_FusedNavigationOutput){0};
    }
}

static bool valid_config(const Mode_Car_FusedNavigationConfig *config)
{
    return (config != NULL) && finite_value(config->track_width_mm) &&
        (config->track_width_mm > 0.0F) &&
        finite_value(config->heading_correction_time_constant_seconds) &&
        (config->heading_correction_time_constant_seconds >= 0.0F) &&
        finite_value(
            config->slip_gate_enter_threshold_deg_per_second) &&
        finite_value(
            config->slip_gate_exit_threshold_deg_per_second) &&
        (config->slip_gate_enter_threshold_deg_per_second >
            config->slip_gate_exit_threshold_deg_per_second) &&
        (config->slip_gate_exit_threshold_deg_per_second >= 0.0F) &&
        (config->slip_gate_recovery_sample_count > 0U) &&
        finite_value(config->pure_pursuit_curvature_gain) &&
        (config->pure_pursuit_curvature_gain > 0.0F);
}

static void update_slip_gate(
    Mode_Car_FusedNavigation *mode, float slip_residual_deg_per_second)
{
    if (!mode->config.encoder_heading_correction_enabled ||
        !mode->config.slip_gate_enabled) {
        mode->slip_gate_active = false;
        mode->slip_gate_recovery_sample_count = 0U;
        return;
    }

    if (!mode->slip_gate_active) {
        if (slip_residual_deg_per_second >
            mode->config.slip_gate_enter_threshold_deg_per_second) {
            mode->slip_gate_active = true;
            mode->slip_gate_recovery_sample_count = 0U;
        }
        return;
    }

    if (slip_residual_deg_per_second <=
        mode->config.slip_gate_exit_threshold_deg_per_second) {
        if (mode->slip_gate_recovery_sample_count <
            mode->config.slip_gate_recovery_sample_count) {
            ++mode->slip_gate_recovery_sample_count;
        }
        if (mode->slip_gate_recovery_sample_count >=
            mode->config.slip_gate_recovery_sample_count) {
            mode->slip_gate_active = false;
            mode->slip_gate_recovery_sample_count = 0U;
        }
    }
    else {
        mode->slip_gate_recovery_sample_count = 0U;
    }
}

static bool valid_pose(const Mode_Car_FusedNavigationPose *pose)
{
    return (pose != NULL) && finite_value(pose->x_mm) &&
        finite_value(pose->y_mm) && finite_value(pose->yaw_deg);
}

static bool valid_input(const Mode_Car_FusedNavigationInput *input)
{
    return (input != NULL) && finite_value(input->dt_seconds) &&
        (input->dt_seconds > 0.0F) &&
        finite_value(input->left_distance_delta_mm) &&
        finite_value(input->right_distance_delta_mm) &&
        finite_value(input->measured_left_speed_mm_per_second) &&
        finite_value(input->measured_right_speed_mm_per_second) &&
        finite_value(input->measured_yaw_rate_deg_per_second) &&
        finite_value(input->tracking_point_offset_mm) &&
        (input->tracking_point_offset_mm >= 0.0F) &&
        finite_value(input->lookahead_x_mm) &&
        finite_value(input->lookahead_y_mm) &&
        finite_value(input->forward_speed_mm_per_second);
}

Mode_Car_FusedNavigationConfig
Mode_Car_FusedNavigation_DefaultConfig(void)
{
    Mode_Car_FusedNavigationConfig config = {0};

    config.track_width_mm = TEMPLATE_CAR_NAV_TRACK_WIDTH_MM;
    config.heading_correction_time_constant_seconds =
        TEMPLATE_CAR_NAV_HEADING_CORRECTION_TIME_CONSTANT_SECONDS;
    config.encoder_heading_correction_enabled =
        TEMPLATE_CAR_NAV_ENCODER_HEADING_CORRECTION_ENABLED;
    config.slip_gate_enabled = TEMPLATE_CAR_NAV_SLIP_GATE_ENABLED;
    config.slip_gate_enter_threshold_deg_per_second =
        TEMPLATE_CAR_NAV_SLIP_GATE_ENTER_THRESHOLD_DPS;
    config.slip_gate_exit_threshold_deg_per_second =
        TEMPLATE_CAR_NAV_SLIP_GATE_EXIT_THRESHOLD_DPS;
    config.slip_gate_recovery_sample_count =
        TEMPLATE_CAR_NAV_SLIP_GATE_RECOVERY_SAMPLE_COUNT;
    config.pure_pursuit_curvature_gain =
        TEMPLATE_CAR_NAV_PURE_PURSUIT_CURVATURE_GAIN;
    config.yaw_rate_controller = (Mode_Car_SpinRateConfig){
            .kp = TEMPLATE_CAR_NAV_YAW_RATE_KP,
            .ki = TEMPLATE_CAR_NAV_YAW_RATE_KI,
            .integral_output_limit =
                TEMPLATE_CAR_NAV_YAW_RATE_INTEGRAL_OUTPUT_LIMIT,
            .maximum_correction_drive =
                TEMPLATE_CAR_NAV_YAW_RATE_MAXIMUM_CORRECTION_MM_PER_SECOND,
            /* Init replaces this with track_width*pi/360. */
            .feedforward_drive_per_dps =
                TEMPLATE_CAR_NAV_YAW_RATE_INITIAL_FEEDFORWARD,
            .maximum_drive =
                TEMPLATE_CAR_NAV_YAW_RATE_MAXIMUM_DRIVE_MM_PER_SECOND,
    };
    config.speed_controller = Mode_Car_SpeedController_DefaultConfig();
    return config;
}

bool Mode_Car_FusedNavigation_Init(
    Mode_Car_FusedNavigation *mode,
    const Mode_Car_FusedNavigationConfig *config)
{
    Mode_Car_FusedNavigation next = {0};
    Mode_Car_SpinRateConfig yaw_rate_config;

    if (mode != NULL) {
        *mode = (Mode_Car_FusedNavigation){0};
    }
    if ((mode == NULL) || !valid_config(config)) {
        return false;
    }

    next.config = *config;
    yaw_rate_config = config->yaw_rate_controller;
    yaw_rate_config.feedforward_drive_per_dps =
        config->track_width_mm * MODE_CAR_RAD_PER_DEG * 0.5F;
    next.config.yaw_rate_controller = yaw_rate_config;
    if (!Mode_Car_SpinRate_Init(
            &next.yaw_rate_controller, &yaw_rate_config) ||
        !Mode_Car_SpeedController_Init(
            &next.speed_controller,
            &config->speed_controller)) {
        return false;
    }
    next.initialized = true;
    *mode = next;
    return true;
}

bool Mode_Car_FusedNavigation_ResetPose(
    Mode_Car_FusedNavigation *mode,
    const Mode_Car_FusedNavigationPose *pose)
{
    Mode_Car_SpinRateContext yaw_rate;
    Mode_Car_SpeedController speed;

    if ((mode == NULL) || !mode->initialized || !valid_pose(pose)) {
        return false;
    }
    if (!Mode_Car_SpinRate_Init(
            &yaw_rate, &mode->config.yaw_rate_controller) ||
        !Mode_Car_SpeedController_Init(
            &speed, &mode->config.speed_controller)) {
        return false;
    }
    mode->pose = *pose;
    mode->encoder_heading_deg = pose->yaw_deg;
    mode->heading_reference_deg = pose->yaw_deg;
    mode->slip_gate_recovery_sample_count = 0U;
    mode->slip_gate_active = false;
    mode->yaw_rate_controller = yaw_rate;
    mode->speed_controller = speed;
    return true;
}

bool Mode_Car_FusedNavigation_Update(
    Mode_Car_FusedNavigation *mode,
    const Mode_Car_FusedNavigationInput *input,
    Mode_Car_FusedNavigationOutput *output)
{
    Mode_Car_FusedNavigation next;
    Mode_Car_Output yaw_rate_output;
    Mode_Car_SpeedControllerInput speed_input;
    Mode_Car_SpeedControllerOutput speed_output;
    Mode_Car_SpinRateInput yaw_rate_input;
    float forward_delta;
    float encoder_delta_deg;
    float gyro_delta_deg;
    float encoder_yaw_rate_deg_per_second;
    float slip_residual_deg_per_second;
    float gyro_predicted_deg;
    float innovation_deg;
    float correction_fraction;
    float next_yaw_deg;
    float fused_delta_deg;
    float midpoint_yaw_rad;
    float tracking_x_mm;
    float tracking_y_mm;
    float lookahead_dx;
    float lookahead_dy;
    float lookahead_distance;
    float lookahead_forward_mm;
    float lookahead_left_mm;
    float curvature_denominator;
    float target_curvature_per_mm;
    float current_forward_speed_mm_per_second;
    float target_heading_deg;
    float heading_error_deg;
    float target_yaw_rate_deg_per_second;
    float target_left_speed;
    float target_right_speed;

    clear_output(output);
    if ((mode == NULL) || !mode->initialized ||
        !valid_config(&mode->config) || !valid_input(input) ||
        (output == NULL)) {
        return false;
    }

    next = *mode;
    forward_delta = 0.5F *
        (input->left_distance_delta_mm +
            input->right_distance_delta_mm);
    encoder_delta_deg =
        ((input->right_distance_delta_mm -
             input->left_distance_delta_mm) /
            next.config.track_width_mm) * MODE_CAR_DEG_PER_RAD;
    gyro_delta_deg =
        input->measured_yaw_rate_deg_per_second * input->dt_seconds;
    encoder_yaw_rate_deg_per_second =
        encoder_delta_deg / input->dt_seconds;
    slip_residual_deg_per_second = fabsf(
        encoder_yaw_rate_deg_per_second -
            input->measured_yaw_rate_deg_per_second);
    gyro_predicted_deg = next.pose.yaw_deg + gyro_delta_deg;
    next.encoder_heading_deg += encoder_delta_deg;
    update_slip_gate(&next, slip_residual_deg_per_second);
    next.heading_reference_deg += next.slip_gate_active ?
        gyro_delta_deg : encoder_delta_deg;
    if (!finite_value(forward_delta) ||
        !finite_value(encoder_delta_deg) ||
        !finite_value(gyro_delta_deg) ||
        !finite_value(encoder_yaw_rate_deg_per_second) ||
        !finite_value(slip_residual_deg_per_second) ||
        !finite_value(gyro_predicted_deg) ||
        !finite_value(next.encoder_heading_deg) ||
        !finite_value(next.heading_reference_deg) ||
        !Algorithm_PID_AngleErrorDeg(
            next.heading_reference_deg,
            gyro_predicted_deg,
            &innovation_deg)) {
        return false;
    }

    if (!next.config.encoder_heading_correction_enabled ||
        next.slip_gate_active) {
        correction_fraction = 0.0F;
    }
    else if (next.config.heading_correction_time_constant_seconds == 0.0F) {
        correction_fraction = 1.0F;
    }
    else {
        correction_fraction = 1.0F - expf(
            -input->dt_seconds /
            next.config.heading_correction_time_constant_seconds);
    }
    if (!finite_value(correction_fraction) ||
        !Algorithm_PID_Clamp(
            correction_fraction, 0.0F, 1.0F,
            &correction_fraction)) {
        return false;
    }
    next_yaw_deg = gyro_predicted_deg +
        (correction_fraction * innovation_deg);
    fused_delta_deg = next_yaw_deg - next.pose.yaw_deg;
    midpoint_yaw_rad =
        (next.pose.yaw_deg + (0.5F * fused_delta_deg)) *
        MODE_CAR_RAD_PER_DEG;
    next.pose.x_mm += forward_delta * cosf(midpoint_yaw_rad);
    next.pose.y_mm += forward_delta * sinf(midpoint_yaw_rad);
    next.pose.yaw_deg = next_yaw_deg;
    if (!valid_pose(&next.pose)) {
        return false;
    }

    tracking_x_mm = next.pose.x_mm +
        input->tracking_point_offset_mm * cosf(next_yaw_deg * MODE_CAR_RAD_PER_DEG);
    tracking_y_mm = next.pose.y_mm +
        input->tracking_point_offset_mm * sinf(next_yaw_deg * MODE_CAR_RAD_PER_DEG);
    lookahead_dx = input->lookahead_x_mm - tracking_x_mm;
    lookahead_dy = input->lookahead_y_mm - tracking_y_mm;
    lookahead_distance = sqrtf(
        (lookahead_dx * lookahead_dx) + (lookahead_dy * lookahead_dy));
    if (!finite_value(tracking_x_mm) || !finite_value(tracking_y_mm) ||
        !finite_value(lookahead_distance) || (lookahead_distance <= 1.0F)) {
        return false;
    }
    target_heading_deg = atan2f(lookahead_dy, lookahead_dx) * MODE_CAR_DEG_PER_RAD;
    if (!Algorithm_PID_AngleErrorDeg(
            target_heading_deg,
            next_yaw_deg,
            &heading_error_deg)) {
        return false;
    }

    lookahead_forward_mm =
        (lookahead_dx * cosf(next_yaw_deg * MODE_CAR_RAD_PER_DEG)) +
        (lookahead_dy * sinf(next_yaw_deg * MODE_CAR_RAD_PER_DEG));
    lookahead_left_mm =
        (-lookahead_dx * sinf(next_yaw_deg * MODE_CAR_RAD_PER_DEG)) +
        (lookahead_dy * cosf(next_yaw_deg * MODE_CAR_RAD_PER_DEG));
    curvature_denominator =
        (lookahead_dx * lookahead_dx) +
        (lookahead_dy * lookahead_dy) +
        (2.0F * input->tracking_point_offset_mm *
            lookahead_forward_mm);
    if (!finite_value(lookahead_forward_mm) ||
        !finite_value(lookahead_left_mm) ||
        !finite_value(curvature_denominator) ||
        (fabsf(curvature_denominator) <= 1.0F)) {
        return false;
    }
    target_curvature_per_mm =
        next.config.pure_pursuit_curvature_gain *
        (2.0F * lookahead_left_mm / curvature_denominator);
    current_forward_speed_mm_per_second = 0.5F *
        (input->measured_left_speed_mm_per_second +
            input->measured_right_speed_mm_per_second);
    target_yaw_rate_deg_per_second =
        current_forward_speed_mm_per_second *
        target_curvature_per_mm * MODE_CAR_DEG_PER_RAD;
    if (!finite_value(target_curvature_per_mm) ||
        !finite_value(current_forward_speed_mm_per_second) ||
        !finite_value(target_yaw_rate_deg_per_second)) {
        return false;
    }

    yaw_rate_input = (Mode_Car_SpinRateInput){
        .time = {
            .now_ms = 0U,
            .dt_seconds = input->dt_seconds,
        },
        .target_yaw_rate_deg_per_second = target_yaw_rate_deg_per_second,
        .measured_yaw_rate_deg_per_second =
            input->measured_yaw_rate_deg_per_second,
    };
    if (!Mode_Car_SpinRate_Update(
            &next.yaw_rate_controller,
            &yaw_rate_input,
            &yaw_rate_output)) {
        return false;
    }

    target_left_speed =
        input->forward_speed_mm_per_second + yaw_rate_output.left_drive;
    target_right_speed =
        input->forward_speed_mm_per_second + yaw_rate_output.right_drive;

    speed_input = (Mode_Car_SpeedControllerInput){
        .dt_seconds = input->dt_seconds,
        .target_left_speed = target_left_speed,
        .target_right_speed = target_right_speed,
        .measured_left_speed =
            input->measured_left_speed_mm_per_second,
        .measured_right_speed =
            input->measured_right_speed_mm_per_second,
    };
    if (!Mode_Car_SpeedController_Update(
            &next.speed_controller, &speed_input, &speed_output)) {
        return false;
    }

    *output = (Mode_Car_FusedNavigationOutput){
        .pose = next.pose,
        .forward_distance_delta_mm = forward_delta,
        .encoder_heading_delta_deg = encoder_delta_deg,
        .gyro_heading_delta_deg = gyro_delta_deg,
        .heading_innovation_deg = innovation_deg,
        .encoder_yaw_rate_deg_per_second =
            encoder_yaw_rate_deg_per_second,
        .slip_residual_deg_per_second =
            slip_residual_deg_per_second,
        .tracking_x_mm = tracking_x_mm,
        .tracking_y_mm = tracking_y_mm,
        .target_heading_deg = target_heading_deg,
        .heading_error_deg = heading_error_deg,
        .target_yaw_rate_deg_per_second = target_yaw_rate_deg_per_second,
        .turn_speed_mm_per_second = yaw_rate_output.right_drive,
        .target_left_speed_mm_per_second = target_left_speed,
        .target_right_speed_mm_per_second = target_right_speed,
        .effort_left_percent = speed_output.left_effort_percent,
        .effort_right_percent = speed_output.right_effort_percent,
        .slip_gate_active = next.slip_gate_active,
    };
    *mode = next;
    return true;
}
