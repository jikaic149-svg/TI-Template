#include "mode_car_internal.h"
#include "core_config.h"

#include <stddef.h>

Mode_Car_CurveConfig Mode_Car_Curve_DefaultConfig(void)
{
    const Mode_Car_CurveConfig config = {
        .outer_kp = TEMPLATE_CAR_CURVE_OUTER_KP,
        .outer_ki = TEMPLATE_CAR_CURVE_OUTER_KI,
        .outer_integral_output_limit =
            TEMPLATE_CAR_CURVE_OUTER_INTEGRAL_OUTPUT_LIMIT,
        .maximum_heading_rate_deg_per_second =
            TEMPLATE_CAR_CURVE_MAXIMUM_HEADING_RATE_DPS,
        .outer_period_seconds = TEMPLATE_CAR_CURVE_OUTER_PERIOD_SECONDS,
        .inner_kp = TEMPLATE_CAR_CURVE_INNER_KP,
        .inner_ki = TEMPLATE_CAR_CURVE_INNER_KI,
        .inner_integral_output_limit =
            TEMPLATE_CAR_CURVE_INNER_INTEGRAL_OUTPUT_LIMIT,
        .maximum_drive_difference =
            TEMPLATE_CAR_CURVE_MAXIMUM_DRIVE_DIFFERENCE,
        .drive_difference_slew_per_second =
            TEMPLATE_CAR_CURVE_DRIVE_DIFFERENCE_SLEW_PER_SECOND,
    };
    return config;
}

bool Mode_Car_Curve_Init(
    Mode_Car_CurveContext *context,
    const Mode_Car_CurveConfig *config)
{
    Algorithm_PID heading_pid;
    Algorithm_PID rate_pid;

    if (context != NULL) {
        context->initialized = false;
    }
    if ((context == NULL) || (config == NULL) ||
        !Mode_Car_Internal_IsFinite(config->outer_period_seconds) ||
        !Mode_Car_Internal_IsFinite(
            config->drive_difference_slew_per_second) ||
        (config->outer_period_seconds <= 0.0F) ||
        (config->drive_difference_slew_per_second < 0.0F) ||
        !Mode_Car_Internal_InitPID(&heading_pid,
            config->outer_kp, config->outer_ki, 0.0F,
            config->outer_integral_output_limit,
            config->maximum_heading_rate_deg_per_second) ||
        !Mode_Car_Internal_InitPID(&rate_pid,
            config->inner_kp, config->inner_ki, 0.0F,
            config->inner_integral_output_limit,
            config->maximum_drive_difference)) {
        return false;
    }

    context->config = *config;
    context->heading_pid = heading_pid;
    context->heading_rate_pid = rate_pid;
    context->target_heading_rate_deg_per_second = 0.0F;
    context->drive_difference = 0.0F;
    context->outer_elapsed_seconds = 0.0F;
    context->outer_initialized = false;
    context->initialized = true;
    return true;
}

bool Mode_Car_Curve_Update(
    Mode_Car_CurveContext *context,
    const Mode_Car_CurveInput *input,
    Mode_Car_Output *output)
{
    Algorithm_PID next_heading_pid;
    Algorithm_PID next_rate_pid;
    float next_target_rate;
    float next_difference;
    float next_outer_elapsed;
    float heading_error;
    float raw_difference;
    float left_drive;
    float right_drive;
    bool next_outer_initialized;

    Mode_Car_Internal_StopOutput(output, MODE_CAR_COMPLETION_NONE);
    if ((output == NULL) || (context == NULL) || !context->initialized ||
        (input == NULL) ||
        !Mode_Car_Internal_ValidTime(&input->time) ||
        !Mode_Car_Internal_IsFinite(input->base_drive) ||
        !Mode_Car_Internal_IsFinite(input->target_heading_deg) ||
        !Mode_Car_Internal_IsFinite(input->measured_heading_deg) ||
        !Mode_Car_Internal_IsFinite(
            input->measured_heading_rate_deg_per_second)) {
        return false;
    }

    next_heading_pid = context->heading_pid;
    next_rate_pid = context->heading_rate_pid;
    next_target_rate = context->target_heading_rate_deg_per_second;
    next_difference = context->drive_difference;
    next_outer_elapsed =
        context->outer_elapsed_seconds + input->time.dt_seconds;
    next_outer_initialized = context->outer_initialized;
    if (!Mode_Car_Internal_IsFinite(next_outer_elapsed)) {
        return false;
    }

    if (!next_outer_initialized ||
        (next_outer_elapsed >= context->config.outer_period_seconds)) {
        if (!Algorithm_PID_AngleErrorDeg(input->target_heading_deg,
                input->measured_heading_deg, &heading_error) ||
            !Algorithm_PID_Update(&next_heading_pid,
                heading_error, 0.0F, next_outer_elapsed,
                &next_target_rate)) {
            return false;
        }
        next_outer_elapsed = 0.0F;
        next_outer_initialized = true;
    }

    if (!Algorithm_PID_Update(&next_rate_pid,
            next_target_rate,
            input->measured_heading_rate_deg_per_second,
            input->time.dt_seconds,
            &raw_difference) ||
        !Algorithm_PID_Slew(next_difference, raw_difference,
            context->config.drive_difference_slew_per_second,
            input->time.dt_seconds, &next_difference)) {
        return false;
    }

    left_drive = input->base_drive - next_difference;
    right_drive = input->base_drive + next_difference;
    if (!Mode_Car_Internal_DriveOutput(
            left_drive, right_drive, output)) {
        return false;
    }

    context->heading_pid = next_heading_pid;
    context->heading_rate_pid = next_rate_pid;
    context->target_heading_rate_deg_per_second = next_target_rate;
    context->drive_difference = next_difference;
    context->outer_elapsed_seconds = next_outer_elapsed;
    context->outer_initialized = next_outer_initialized;
    return true;
}
