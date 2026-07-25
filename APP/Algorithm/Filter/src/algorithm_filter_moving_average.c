#include "algorithm_filter_moving_average.h"

#include <math.h>
#include <stddef.h>

bool Algorithm_Filter_MovingAverage_Init(
    Algorithm_Filter_MovingAverage *filter,
    float *storage,
    size_t capacity)
{
    size_t index;

    if ((filter == NULL) || (storage == NULL) || (capacity == 0U)) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        storage[index] = 0.0F;
    }
    *filter = (Algorithm_Filter_MovingAverage){
        .storage = storage,
        .capacity = capacity,
        .initialized = true,
    };
    return true;
}

bool Algorithm_Filter_MovingAverage_Update(
    Algorithm_Filter_MovingAverage *filter,
    float input,
    float *output)
{
    float next_sum;
    float next_output;
    size_t next_count;
    size_t next_index;

    if ((filter == NULL) || !filter->initialized || (output == NULL) ||
        (filter->storage == NULL) || (filter->capacity == 0U) ||
        (filter->count > filter->capacity) ||
        (filter->next >= filter->capacity) || !isfinite(filter->sum) ||
        !isfinite(input)) {
        return false;
    }
    next_sum = filter->sum;
    next_count = filter->count;
    if (filter->count == filter->capacity) {
        next_sum -= filter->storage[filter->next];
    }
    else {
        ++next_count;
    }
    next_sum += input;
    if (!isfinite(next_sum) || (next_count == 0U)) {
        return false;
    }
    next_output = next_sum / (float)next_count;
    if (!isfinite(next_output)) {
        return false;
    }
    next_index = (filter->next + 1U) % filter->capacity;

    filter->storage[filter->next] = input;
    filter->count = next_count;
    filter->next = next_index;
    filter->sum = next_sum;
    *output = next_output;
    return true;
}

void Algorithm_Filter_MovingAverage_Reset(
    Algorithm_Filter_MovingAverage *filter)
{
    size_t index;

    if ((filter == NULL) || !filter->initialized) {
        return;
    }
    for (index = 0U; index < filter->capacity; ++index) {
        filter->storage[index] = 0.0F;
    }
    filter->count = 0U;
    filter->next = 0U;
    filter->sum = 0.0F;
}
