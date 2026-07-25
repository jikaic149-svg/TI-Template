#ifndef ALGORITHM_FILTER_BIQUAD_H
#define ALGORITHM_FILTER_BIQUAD_H

#include <stdbool.h>

typedef enum {
    ALGORITHM_FILTER_BIQUAD_LOWPASS = 0,
    ALGORITHM_FILTER_BIQUAD_HIGHPASS,
    ALGORITHM_FILTER_BIQUAD_BANDPASS_PEAK,
    ALGORITHM_FILTER_BIQUAD_NOTCH,
} Algorithm_Filter_BiquadType;

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float x1;
    float x2;
    float y1;
    float y2;
    bool initialized;
} Algorithm_Filter_Biquad;

bool Algorithm_Filter_Biquad_Init(
    Algorithm_Filter_Biquad *filter,
    Algorithm_Filter_BiquadType type,
    float sample_rate_hz,
    float cutoff_hz,
    float q);
bool Algorithm_Filter_Biquad_Update(
    Algorithm_Filter_Biquad *filter,
    float input,
    float *output);
void Algorithm_Filter_Biquad_Reset(Algorithm_Filter_Biquad *filter);

#endif
