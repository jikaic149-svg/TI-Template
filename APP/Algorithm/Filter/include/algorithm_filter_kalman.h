#ifndef ALGORITHM_FILTER_KALMAN_H
#define ALGORITHM_FILTER_KALMAN_H

#include <stdbool.h>

typedef struct {
    float process_noise;
    float measurement_noise;
    float value;
    float covariance;
    bool seeded;
    bool initialized;
} Algorithm_Filter_Kalman;

bool Algorithm_Filter_Kalman_Init(
    Algorithm_Filter_Kalman *filter,
    float process_noise,
    float measurement_noise);
bool Algorithm_Filter_Kalman_SetState(
    Algorithm_Filter_Kalman *filter, float value);
bool Algorithm_Filter_Kalman_Update(
    Algorithm_Filter_Kalman *filter,
    float measurement,
    float *output);

#endif
