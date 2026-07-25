#include "algorithm_filter_low_pass.h"

#include <math.h>
#include <stddef.h>

bool Algorithm_Filter_LowPass_Init(
    Algorithm_Filter_LowPass *filter,
    float time_constant_seconds,
    float initial_value)
{
    if ((filter == NULL) || !isfinite(time_constant_seconds) ||
        !isfinite(initial_value) || (time_constant_seconds < 0.0F)) {
        return false;
    }
    filter->time_constant_seconds = time_constant_seconds;
    filter->value = initial_value;
    filter->initialized = true;
    return true;
}

bool Algorithm_Filter_LowPass_Update(
    Algorithm_Filter_LowPass *filter,
    float input,
    float dt_seconds,
    float *output)
{
    float alpha;
    float next;

    if ((filter == NULL) || !filter->initialized || (output == NULL) ||
        !isfinite(filter->time_constant_seconds) ||
        (filter->time_constant_seconds < 0.0F) ||
        !isfinite(filter->value) || !isfinite(input) ||
        !isfinite(dt_seconds) || (dt_seconds <= 0.0F)) {
        return false;
    }
    alpha = (filter->time_constant_seconds == 0.0F)
        ? 1.0F
        : dt_seconds / (filter->time_constant_seconds + dt_seconds);
    next = filter->value + ((input - filter->value) * alpha);
    if (!isfinite(next)) {
        return false;
    }
    filter->value = next;
    *output = next;
    return true;
}

void Algorithm_Filter_LowPass_Reset(
    Algorithm_Filter_LowPass *filter, float value)
{
    if ((filter == NULL) || !filter->initialized || !isfinite(value)) {
        return;
    }
    filter->value = value;
}
