#include "mode_car.h"
#include "core_config.h"

#include <math.h>
#include <stddef.h>

Mode_Car_AnalogTrackingConfig Mode_Car_AnalogTracking_DefaultConfig(void)
{
    const Mode_Car_AnalogTrackingConfig config = {
        .steering = {
            .kp = TEMPLATE_LINE_STEERING_KP,
            .ki = TEMPLATE_LINE_STEERING_KI,
            .kd = TEMPLATE_LINE_STEERING_KD,
            .integral_output_limit =
                TEMPLATE_LINE_STEERING_INTEGRAL_OUTPUT_LIMIT,
            .maximum_correction =
                TEMPLATE_LINE_STEERING_MAXIMUM_CORRECTION,
            .maximum_drive = TEMPLATE_LINE_STEERING_MAXIMUM_DRIVE,
            .error_deadband = TEMPLATE_LINE_STEERING_ERROR_DEADBAND,
            .stop_on_intersection =
                TEMPLATE_LINE_STEERING_STOP_ON_INTERSECTION,
        },
        .minimum_confidence = TEMPLATE_LINE_ANALOG_MINIMUM_CONFIDENCE,
    };
    return config;
}

bool Mode_Car_AnalogTracking_Init(
    Mode_Car_AnalogTrackingContext *context,
    const Mode_Car_AnalogTrackingConfig *config)
{
    if (context != NULL) {
        *context = (Mode_Car_AnalogTrackingContext){0};
    }
    if ((context == NULL) || (config == NULL) ||
        !isfinite(config->minimum_confidence) ||
        (config->minimum_confidence < 0.0F) ||
        (config->minimum_confidence > 1.0F) ||
        !Mode_Car_Tracking_Init(&context->core, &config->steering)) {
        return false;
    }
    context->minimum_confidence = config->minimum_confidence;
    context->initialized = true;
    return true;
}

bool Mode_Car_AnalogTracking_Update(
    Mode_Car_AnalogTrackingContext *context,
    const Mode_Car_AnalogTrackingInput *input,
    Mode_Car_Output *output)
{
    Mode_Car_TrackingInput core_input;

    if (output != NULL) {
        *output = (Mode_Car_Output){0};
    }
    if ((context == NULL) || !context->initialized || (input == NULL) ||
        !isfinite(input->confidence) || (input->confidence < 0.0F) ||
        (input->confidence > 1.0F)) {
        return false;
    }
    core_input = (Mode_Car_TrackingInput){
        .time = input->time,
        .base_drive = input->base_drive,
        .line_error = input->analog_error,
        .line_valid = input->line_valid &&
            (input->confidence >= context->minimum_confidence),
        .line_lost = input->line_lost,
        .intersection = input->intersection,
    };
    return Mode_Car_Tracking_Update(&context->core, &core_input, output);
}
