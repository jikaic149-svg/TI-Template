#include "algorithm_filter_biquad.h"

#include <math.h>
#include <stddef.h>

bool Algorithm_Filter_Biquad_Init(
    Algorithm_Filter_Biquad *filter,
    Algorithm_Filter_BiquadType type,
    float sample_rate_hz,
    float cutoff_hz,
    float q)
{
    const float pi = 3.14159265358979323846F;
    float omega;
    float sine;
    float cosine;
    float alpha;
    float numerator0;
    float numerator1;
    float numerator2;
    float denominator0;
    float denominator1;
    float denominator2;
    Algorithm_Filter_Biquad next = {0};

    if (filter != NULL) {
        *filter = (Algorithm_Filter_Biquad){0};
    }
    if ((filter == NULL) || !isfinite(sample_rate_hz) ||
        !isfinite(cutoff_hz) || !isfinite(q) ||
        (sample_rate_hz <= 0.0F) || (cutoff_hz <= 0.0F) ||
        (cutoff_hz >= (sample_rate_hz * 0.5F)) || (q <= 0.0F) ||
        ((type != ALGORITHM_FILTER_BIQUAD_LOWPASS) &&
         (type != ALGORITHM_FILTER_BIQUAD_HIGHPASS) &&
         (type != ALGORITHM_FILTER_BIQUAD_BANDPASS_PEAK) &&
         (type != ALGORITHM_FILTER_BIQUAD_NOTCH))) {
        return false;
    }
    omega = 2.0F * pi * cutoff_hz / sample_rate_hz;
    sine = sinf(omega);
    cosine = cosf(omega);
    alpha = sine / (2.0F * q);
    denominator0 = 1.0F + alpha;
    denominator1 = -2.0F * cosine;
    denominator2 = 1.0F - alpha;
    switch (type) {
    case ALGORITHM_FILTER_BIQUAD_LOWPASS:
        numerator0 = (1.0F - cosine) * 0.5F;
        numerator1 = 2.0F * numerator0;
        numerator2 = numerator0;
        break;
    case ALGORITHM_FILTER_BIQUAD_HIGHPASS:
        numerator0 = (1.0F + cosine) * 0.5F;
        numerator1 = -2.0F * numerator0;
        numerator2 = numerator0;
        break;
    case ALGORITHM_FILTER_BIQUAD_BANDPASS_PEAK:
        numerator0 = alpha;
        numerator1 = 0.0F;
        numerator2 = -alpha;
        break;
    case ALGORITHM_FILTER_BIQUAD_NOTCH:
        numerator0 = 1.0F;
        numerator1 = -2.0F * cosine;
        numerator2 = 1.0F;
        break;
    default:
        return false;
    }
    next.b0 = numerator0 / denominator0;
    next.b1 = numerator1 / denominator0;
    next.b2 = numerator2 / denominator0;
    next.a1 = denominator1 / denominator0;
    next.a2 = denominator2 / denominator0;
    if (!isfinite(next.b0) || !isfinite(next.b1) ||
        !isfinite(next.b2) || !isfinite(next.a1) ||
        !isfinite(next.a2)) {
        return false;
    }
    next.initialized = true;
    *filter = next;
    return true;
}

bool Algorithm_Filter_Biquad_Update(
    Algorithm_Filter_Biquad *filter,
    float input,
    float *output)
{
    float next_output;

    if ((filter == NULL) || !filter->initialized || (output == NULL) ||
        !isfinite(input) || !isfinite(filter->x1) ||
        !isfinite(filter->x2) || !isfinite(filter->y1) ||
        !isfinite(filter->y2)) {
        return false;
    }
    next_output = (filter->b0 * input) + (filter->b1 * filter->x1) +
        (filter->b2 * filter->x2) - (filter->a1 * filter->y1) -
        (filter->a2 * filter->y2);
    if (!isfinite(next_output)) {
        return false;
    }
    filter->x2 = filter->x1;
    filter->x1 = input;
    filter->y2 = filter->y1;
    filter->y1 = next_output;
    *output = next_output;
    return true;
}

void Algorithm_Filter_Biquad_Reset(Algorithm_Filter_Biquad *filter)
{
    if ((filter == NULL) || !filter->initialized) {
        return;
    }
    filter->x1 = 0.0F;
    filter->x2 = 0.0F;
    filter->y1 = 0.0F;
    filter->y2 = 0.0F;
}
