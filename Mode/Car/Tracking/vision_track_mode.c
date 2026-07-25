#include "vision_track_mode.h"

#include <math.h>
#include <stddef.h>

static bool finite_value(float value)
{
    return isfinite(value) != 0;
}

static bool valid_config(const Mode_Car_VisionTrackConfig *config)
{
    return (config != NULL) &&
        finite_value(config->forward_speed_mm_per_second) &&
        finite_value(config->position_error_gain_deg) &&
        finite_value(config->maximum_steering_error_deg) &&
        (config->forward_speed_mm_per_second >= 0.0F) &&
        (config->maximum_steering_error_deg > 0.0F) &&
        (config->maximum_steering_error_deg < 180.0F);
}

Mode_Car_VisionTrackConfig Mode_Car_VisionTrack_DefaultConfig(void)
{
    Mode_Car_VisionTrackConfig config = {
        .forward_speed_mm_per_second = 400.0F,
        .position_error_gain_deg = 15.0F,
        .maximum_steering_error_deg = 10.0F,
        .yaw_controller = Mode_Car_AngleController_DefaultConfig(),
        .speed_controller = Mode_Car_SpeedController_DefaultConfig(),
    };

    return config;
}

bool Mode_Car_VisionTrack_Init(
    Mode_Car_VisionTrack *mode,
    const Mode_Car_VisionTrackConfig *config)
{
    Mode_Car_VisionTrack next;

    if (mode != NULL) {
        *mode = (Mode_Car_VisionTrack){0};
    }
    if ((mode == NULL) || !valid_config(config)) {
        return false;
    }
    next = (Mode_Car_VisionTrack){0};
    next.config = *config;
    if (!Mode_Car_AngleController_Init(
            &next.yaw_controller, &config->yaw_controller) ||
        !Mode_Car_SpeedController_Init(
            &next.speed_controller, &config->speed_controller)) {
        return false;
    }
    next.initialized = true;
    *mode = next;
    return true;
}

void Mode_Car_VisionTrack_Reset(Mode_Car_VisionTrack *mode)
{
    if ((mode == NULL) || !mode->initialized) {
        return;
    }
    Mode_Car_AngleController_Reset(&mode->yaw_controller);
    Mode_Car_SpeedController_Reset(&mode->speed_controller);
}

bool Mode_Car_VisionTrack_Update(
    Mode_Car_VisionTrack *mode,
    const Mode_Car_VisionTrackInput *input,
    Mode_Car_VisionTrackOutput *output)
{
    Mode_Car_AngleControllerInput yaw_input;
    Mode_Car_AngleControllerOutput yaw_output;
    Mode_Car_SpeedControllerInput speed_input;
    Mode_Car_SpeedControllerOutput speed_output;
    float steering_error;

    if (output != NULL) {
        *output = (Mode_Car_VisionTrackOutput){0};
    }
    if ((mode == NULL) || !mode->initialized ||
        !valid_config(&mode->config) || (input == NULL) ||
        (output == NULL) || !finite_value(input->dt_seconds) ||
        (input->dt_seconds <= 0.0F)) {
        return false;
    }
    if (!input->track_valid) {
        Mode_Car_VisionTrack_Reset(mode);
        return true;
    }
    if (!finite_value(input->control_error_x) ||
        !finite_value(input->control_heading_deg) ||
        !finite_value(input->measured_yaw_deg) ||
        !finite_value(input->measured_yaw_rate_deg_per_second) ||
        !finite_value(input->measured_speed_a_mm_per_second) ||
        !finite_value(input->measured_speed_b_mm_per_second)) {
        return false;
    }

    steering_error = input->control_heading_deg +
        (mode->config.position_error_gain_deg * input->control_error_x);
    if (steering_error > mode->config.maximum_steering_error_deg) {
        steering_error = mode->config.maximum_steering_error_deg;
    }
    else if (steering_error < -mode->config.maximum_steering_error_deg) {
        steering_error = -mode->config.maximum_steering_error_deg;
    }
    yaw_input = (Mode_Car_AngleControllerInput){
        .dt_seconds = input->dt_seconds,
        .target_yaw_deg = input->measured_yaw_deg + steering_error,
        .measured_yaw_deg = input->measured_yaw_deg,
        .measured_yaw_rate_deg_per_second =
            input->measured_yaw_rate_deg_per_second,
    };
    if (!Mode_Car_AngleController_Update(
            &mode->yaw_controller, &yaw_input, &yaw_output)) {
        return false;
    }

    speed_input = (Mode_Car_SpeedControllerInput){
        .dt_seconds = input->dt_seconds,
        .target_left_speed = mode->config.forward_speed_mm_per_second +
            yaw_output.turn_wheel_speed,
        .target_right_speed = mode->config.forward_speed_mm_per_second -
            yaw_output.turn_wheel_speed,
        .measured_left_speed = input->measured_speed_a_mm_per_second,
        .measured_right_speed = input->measured_speed_b_mm_per_second,
    };
    if (!Mode_Car_SpeedController_Update(
            &mode->speed_controller, &speed_input, &speed_output)) {
        return false;
    }

    *output = (Mode_Car_VisionTrackOutput){
        .track_active = true,
        .steering_error_deg = steering_error,
        .target_yaw_deg = yaw_input.target_yaw_deg,
        .turn_speed_mm_per_second = yaw_output.turn_wheel_speed,
        .target_speed_a_mm_per_second = speed_input.target_left_speed,
        .target_speed_b_mm_per_second = speed_input.target_right_speed,
        .effort_a_percent = speed_output.left_effort_percent,
        .effort_b_percent = speed_output.right_effort_percent,
    };
    return true;
}
