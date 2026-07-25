#ifndef ALGORITHM_FILTER_LOW_PASS_H
#define ALGORITHM_FILTER_LOW_PASS_H

#include <stdbool.h>

typedef struct {
    float time_constant_seconds;
    float value;
    bool initialized;
} Algorithm_Filter_LowPass;

bool Algorithm_Filter_LowPass_Init(
    Algorithm_Filter_LowPass *filter,
    float time_constant_seconds,
    float initial_value);
bool Algorithm_Filter_LowPass_Update(
    Algorithm_Filter_LowPass *filter,
    float input,
    float dt_seconds,
    float *output);
void Algorithm_Filter_LowPass_Reset(
    Algorithm_Filter_LowPass *filter, float value);

#endif
