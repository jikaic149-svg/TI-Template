#include "algorithm_filter_kalman.h"

#include <math.h>
#include <stddef.h>

bool Algorithm_Filter_Kalman_Init(
    Algorithm_Filter_Kalman *filter,
    float process_noise,
    float measurement_noise)
{
    if (filter != NULL) {
        *filter = (Algorithm_Filter_Kalman){0};
    }
    if ((filter == NULL) || !isfinite(process_noise) ||
        !isfinite(measurement_noise) || (process_noise < 0.0F) ||
        (measurement_noise <= 0.0F)) {
        return false;
    }
    *filter = (Algorithm_Filter_Kalman){
        .process_noise = process_noise,
        .measurement_noise = measurement_noise,
        .covariance = 1.0F,
        .initialized = true,
    };
    return true;
}

bool Algorithm_Filter_Kalman_SetState(
    Algorithm_Filter_Kalman *filter, float value)
{
    if ((filter == NULL) || !filter->initialized || !isfinite(value)) {
        return false;
    }
    filter->value = value;
    filter->covariance = 1.0F;
    filter->seeded = true;
    return true;
}

bool Algorithm_Filter_Kalman_Update(
    Algorithm_Filter_Kalman *filter,
    float measurement,
    float *output)
{
    float covariance;
    float gain;
    float value;

    if ((filter == NULL) || !filter->initialized || (output == NULL) ||
        !isfinite(measurement)) {
        return false;
    }
    if (!filter->seeded) {
        if (!Algorithm_Filter_Kalman_SetState(filter, measurement)) {
            return false;
        }
        *output = measurement;
        return true;
    }
    covariance = filter->covariance + filter->process_noise;
    gain = covariance / (covariance + filter->measurement_noise);
    value = filter->value + gain * (measurement - filter->value);
    covariance *= 1.0F - gain;
    if (!isfinite(gain) || !isfinite(value) || !isfinite(covariance)) {
        return false;
    }
    filter->value = value;
    filter->covariance = covariance;
    *output = value;
    return true;
}
