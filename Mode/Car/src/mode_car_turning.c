#include "mode_car_internal.h"
#include "core_config.h"

#include <math.h>
#include <stddef.h>

Mode_Car_TurningConfig Mode_Car_Turning_DefaultConfig(void)
{
    const Mode_Car_TurningConfig config = {
        .kp = TEMPLATE_CAR_TURNING_KP,
        .ki = TEMPLATE_CAR_TURNING_KI,
        .kd = TEMPLATE_CAR_TURNING_KD,
        .gain_reference_seconds =
            TEMPLATE_CAR_TURNING_GAIN_REFERENCE_SECONDS,
        .integral_output_limit =
            TEMPLATE_CAR_TURNING_INTEGRAL_OUTPUT_LIMIT,
        .maximum_turn_drive = TEMPLATE_CAR_TURNING_MAXIMUM_DRIVE,
        .minimum_turn_drive = TEMPLATE_CAR_TURNING_MINIMUM_DRIVE,
        .deceleration_zone_deg =
            TEMPLATE_CAR_TURNING_DECELERATION_ZONE_DEG,
        .arrival_tolerance_deg =
            TEMPLATE_CAR_TURNING_ARRIVAL_TOLERANCE_DEG,
    };
    return config;
}

bool Mode_Car_Turning_Init(
    Mode_Car_TurningContext *context,
    const Mode_Car_TurningConfig *config)
{
    Algorithm_PID pid;
    float effective_ki;
    float effective_kd;

    if (context != NULL) {
        context->initialized = false;
    }
    if ((context == NULL) || (config == NULL) ||
        !Mode_Car_Internal_IsFinite(config->gain_reference_seconds) ||
        !Mode_Car_Internal_IsFinite(config->minimum_turn_drive) ||
        !Mode_Car_Internal_IsFinite(config->deceleration_zone_deg) ||
        !Mode_Car_Internal_IsFinite(config->arrival_tolerance_deg) ||
        (config->gain_reference_seconds <= 0.0F) ||
        (config->minimum_turn_drive < 0.0F) ||
        (config->minimum_turn_drive > config->maximum_turn_drive) ||
        (config->deceleration_zone_deg <= 0.0F) ||
        (config->arrival_tolerance_deg < 0.0F) ||
        (config->arrival_tolerance_deg > config->deceleration_zone_deg) ||
        !Mode_Car_Internal_IsFinite(
            effective_ki = config->ki / config->gain_reference_seconds) ||
        !Mode_Car_Internal_IsFinite(
            effective_kd = config->kd * config->gain_reference_seconds) ||
        !Mode_Car_Internal_InitPID(&pid,
            config->kp, effective_ki, effective_kd,
            config->integral_output_limit, config->maximum_turn_drive)) {
        return false;
    }

    context->config = *config;
    context->heading_pid = pid;
    context->completion = MODE_CAR_COMPLETION_NONE;
    context->initialized = true;
    return true;
}

bool Mode_Car_Turning_Update(
    Mode_Car_TurningContext *context,
    const Mode_Car_TurningInput *input,
    Mode_Car_Output *output)
{
    Algorithm_PID next_pid;
    float heading_error;
    float turn_drive;
    float absolute_error;
    float local_limit;

    Mode_Car_Internal_StopOutput(output, MODE_CAR_COMPLETION_NONE);
    if ((output == NULL) || (context == NULL) || !context->initialized ||
        (input == NULL) ||
        !Mode_Car_Internal_ValidTime(&input->time) ||
        !Mode_Car_Internal_IsFinite(input->target_heading_deg) ||
        !Mode_Car_Internal_IsFinite(input->measured_heading_deg)) {
        return false;
    }

    if (context->completion != MODE_CAR_COMPLETION_NONE) {
        Mode_Car_Internal_StopOutput(output, context->completion);
        return true;
    }

    if (!Algorithm_PID_AngleErrorDeg(input->target_heading_deg,
            input->measured_heading_deg, &heading_error)) {
        return false;
    }
    absolute_error = fabsf(heading_error);
    if (absolute_error <= context->config.arrival_tolerance_deg) {
        context->completion = MODE_CAR_COMPLETION_TARGET_REACHED;
        Mode_Car_Internal_StopOutput(output, context->completion);
        return true;
    }

    next_pid = context->heading_pid;
    if (!Algorithm_PID_Update(&next_pid,
            heading_error, 0.0F, input->time.dt_seconds, &turn_drive)) {
        return false;
    }

    local_limit = context->config.maximum_turn_drive;
    if (absolute_error < context->config.deceleration_zone_deg) {
        local_limit = context->config.maximum_turn_drive * absolute_error /
            context->config.deceleration_zone_deg;
        if (local_limit < context->config.minimum_turn_drive) {
            local_limit = context->config.minimum_turn_drive;
        }
    }
    if (!Algorithm_PID_Clamp(
            turn_drive, -local_limit, local_limit, &turn_drive)) {
        return false;
    }

    if ((turn_drive > 0.0F) &&
        (turn_drive < context->config.minimum_turn_drive)) {
        turn_drive = context->config.minimum_turn_drive;
    }
    else if ((turn_drive < 0.0F) &&
        (turn_drive > -context->config.minimum_turn_drive)) {
        turn_drive = -context->config.minimum_turn_drive;
    }

    if (!Mode_Car_Internal_DriveOutput(
            -turn_drive, turn_drive, output)) {
        return false;
    }
    context->heading_pid = next_pid;
    return true;
}
