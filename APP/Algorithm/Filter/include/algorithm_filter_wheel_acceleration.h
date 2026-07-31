#ifndef ALGORITHM_FILTER_WHEEL_ACCELERATION_H
#define ALGORITHM_FILTER_WHEEL_ACCELERATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ALGORITHM_FILTER_WHEEL_ACCELERATION_MAX_SAMPLES 16U

typedef struct {
    size_t window_sample_count;
    uint32_t maximum_sample_interval_ms;
    float low_pass_time_constant_s;
    float maximum_absolute_acceleration_mm_s2;
} Algorithm_Filter_WheelAccelerationConfig;

typedef struct {
    Algorithm_Filter_WheelAccelerationConfig config;
    uint32_t sample_ms[ALGORITHM_FILTER_WHEEL_ACCELERATION_MAX_SAMPLES];
    float speed_mm_s[ALGORITHM_FILTER_WHEEL_ACCELERATION_MAX_SAMPLES];
    size_t write_index;
    size_t sample_count;
    uint32_t last_sample_ms;
    uint32_t last_output_ms;
    float filtered_acceleration_mm_s2;
    bool sample_seen;
    bool output_initialized;
    bool initialized;
} Algorithm_Filter_WheelAcceleration;

bool Algorithm_Filter_WheelAcceleration_Init(
    Algorithm_Filter_WheelAcceleration *estimator,
    const Algorithm_Filter_WheelAccelerationConfig *config);

/* Adds one mean-wheel speed sample. A repeated/backward timestamp or an
 * excessive sampling gap starts a new window without emitting stale data.
 * The function returns false only for invalid API/state input; output_valid
 * remains false while a complete regression window is being collected. */
bool Algorithm_Filter_WheelAcceleration_Update(
    Algorithm_Filter_WheelAcceleration *estimator,
    uint32_t sample_ms,
    float speed_mm_s,
    float *acceleration_mm_s2,
    bool *output_valid);

void Algorithm_Filter_WheelAcceleration_Reset(
    Algorithm_Filter_WheelAcceleration *estimator);

#endif
