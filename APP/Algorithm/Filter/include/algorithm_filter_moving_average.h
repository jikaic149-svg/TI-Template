#ifndef ALGORITHM_FILTER_MOVING_AVERAGE_H
#define ALGORITHM_FILTER_MOVING_AVERAGE_H

#include <stdbool.h>
#include <stddef.h>

/* Caller supplies storage so window size and RAM use remain explicit. */
typedef struct {
    float *storage;
    size_t capacity;
    size_t count;
    size_t next;
    float sum;
    bool initialized;
} Algorithm_Filter_MovingAverage;

bool Algorithm_Filter_MovingAverage_Init(
    Algorithm_Filter_MovingAverage *filter,
    float *storage,
    size_t capacity);
bool Algorithm_Filter_MovingAverage_Update(
    Algorithm_Filter_MovingAverage *filter,
    float input,
    float *output);
void Algorithm_Filter_MovingAverage_Reset(
    Algorithm_Filter_MovingAverage *filter);

#endif
