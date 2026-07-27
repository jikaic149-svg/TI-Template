#include "mode_car.h"

#include <stddef.h>

Mode_Car_DigitalTrackingConfig
Mode_Car_DigitalTracking_DefaultConfig(void)
{
    return Mode_Car_Tracking_DefaultConfig();
}

bool Mode_Car_DigitalTracking_Init(
    Mode_Car_DigitalTrackingContext *context,
    const Mode_Car_DigitalTrackingConfig *config)
{
    if (context != NULL) {
        *context = (Mode_Car_DigitalTrackingContext){0};
    }
    if ((context == NULL) ||
        !Mode_Car_Tracking_Init(&context->core, config)) {
        return false;
    }
    context->initialized = true;
    return true;
}

bool Mode_Car_DigitalTracking_Update(
    Mode_Car_DigitalTrackingContext *context,
    const Mode_Car_DigitalTrackingInput *input,
    Mode_Car_Output *output)
{
    Mode_Car_TrackingInput core_input;

    if (output != NULL) {
        *output = (Mode_Car_Output){0};
    }
    if ((context == NULL) || !context->initialized || (input == NULL)) {
        return false;
    }
    core_input = (Mode_Car_TrackingInput){
        .time = input->time,
        .base_drive = input->base_drive,
        .line_error = input->digital_error,
        .line_valid = input->line_valid,
        .line_lost = input->line_lost,
        .intersection = input->intersection,
    };
    return Mode_Car_Tracking_Update(&context->core, &core_input, output);
}
