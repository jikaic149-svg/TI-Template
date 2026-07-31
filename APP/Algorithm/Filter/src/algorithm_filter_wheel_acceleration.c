#include "algorithm_filter_wheel_acceleration.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static bool valid_config(
    const Algorithm_Filter_WheelAccelerationConfig *config)
{
    return (config != NULL) &&
        (config->window_sample_count >= 3U) &&
        (config->window_sample_count <=
            ALGORITHM_FILTER_WHEEL_ACCELERATION_MAX_SAMPLES) &&
        (config->maximum_sample_interval_ms > 0U) &&
        (config->maximum_sample_interval_ms < UINT32_C(0x80000000)) &&
        isfinite(config->low_pass_time_constant_s) &&
        (config->low_pass_time_constant_s >= 0.0F) &&
        isfinite(config->maximum_absolute_acceleration_mm_s2) &&
        (config->maximum_absolute_acceleration_mm_s2 > 0.0F);
}

static void reset_runtime(Algorithm_Filter_WheelAcceleration *estimator)
{
    memset(estimator->sample_ms, 0, sizeof(estimator->sample_ms));
    memset(estimator->speed_mm_s, 0, sizeof(estimator->speed_mm_s));
    estimator->write_index = 0U;
    estimator->sample_count = 0U;
    estimator->last_sample_ms = 0U;
    estimator->last_output_ms = 0U;
    estimator->filtered_acceleration_mm_s2 = 0.0F;
    estimator->sample_seen = false;
    estimator->output_initialized = false;
}

static void push_sample(
    Algorithm_Filter_WheelAcceleration *estimator,
    uint32_t sample_ms,
    float speed_mm_s)
{
    estimator->sample_ms[estimator->write_index] = sample_ms;
    estimator->speed_mm_s[estimator->write_index] = speed_mm_s;
    estimator->write_index = (estimator->write_index + 1U) %
        estimator->config.window_sample_count;
    if (estimator->sample_count < estimator->config.window_sample_count) {
        ++estimator->sample_count;
    }
    estimator->last_sample_ms = sample_ms;
    estimator->sample_seen = true;
}

static bool regression_acceleration(
    const Algorithm_Filter_WheelAcceleration *estimator,
    float *acceleration_mm_s2)
{
    const size_t count = estimator->sample_count;
    const size_t capacity = estimator->config.window_sample_count;
    const size_t oldest =
        (estimator->write_index + capacity - count) % capacity;
    const uint32_t origin_ms = estimator->sample_ms[oldest];
    float mean_time_s = 0.0F;
    float mean_speed_mm_s = 0.0F;
    float numerator = 0.0F;
    float denominator = 0.0F;

    for (size_t order = 0U; order < count; ++order) {
        const size_t index = (oldest + order) % capacity;

        mean_time_s +=
            (float)((uint32_t)(estimator->sample_ms[index] - origin_ms)) *
            0.001F;
        mean_speed_mm_s += estimator->speed_mm_s[index];
    }
    mean_time_s /= (float)count;
    mean_speed_mm_s /= (float)count;
    for (size_t order = 0U; order < count; ++order) {
        const size_t index = (oldest + order) % capacity;
        const float time_s =
            (float)((uint32_t)(estimator->sample_ms[index] - origin_ms)) *
            0.001F;
        const float centered_time_s = time_s - mean_time_s;

        numerator += centered_time_s *
            (estimator->speed_mm_s[index] - mean_speed_mm_s);
        denominator += centered_time_s * centered_time_s;
    }
    if (!isfinite(numerator) || !isfinite(denominator) ||
        (denominator <= 0.0000001F)) {
        return false;
    }
    *acceleration_mm_s2 = numerator / denominator;
    return isfinite(*acceleration_mm_s2);
}

bool Algorithm_Filter_WheelAcceleration_Init(
    Algorithm_Filter_WheelAcceleration *estimator,
    const Algorithm_Filter_WheelAccelerationConfig *config)
{
    if (estimator != NULL) {
        memset(estimator, 0, sizeof(*estimator));
    }
    if ((estimator == NULL) || !valid_config(config)) {
        return false;
    }
    estimator->config = *config;
    estimator->initialized = true;
    reset_runtime(estimator);
    return true;
}

bool Algorithm_Filter_WheelAcceleration_Update(
    Algorithm_Filter_WheelAcceleration *estimator,
    uint32_t sample_ms,
    float speed_mm_s,
    float *acceleration_mm_s2,
    bool *output_valid)
{
    float raw_acceleration_mm_s2;

    if (acceleration_mm_s2 != NULL) {
        *acceleration_mm_s2 = 0.0F;
    }
    if (output_valid != NULL) {
        *output_valid = false;
    }
    if ((estimator == NULL) || !estimator->initialized ||
        (acceleration_mm_s2 == NULL) || (output_valid == NULL) ||
        !isfinite(speed_mm_s) || !valid_config(&estimator->config)) {
        return false;
    }
    if (estimator->sample_seen) {
        const uint32_t delta_ms = sample_ms - estimator->last_sample_ms;

        if ((delta_ms == 0U) ||
            (delta_ms > estimator->config.maximum_sample_interval_ms)) {
            reset_runtime(estimator);
        }
    }
    push_sample(estimator, sample_ms, speed_mm_s);
    if (estimator->sample_count < estimator->config.window_sample_count) {
        return true;
    }
    if (!regression_acceleration(estimator, &raw_acceleration_mm_s2)) {
        reset_runtime(estimator);
        push_sample(estimator, sample_ms, speed_mm_s);
        return false;
    }
    if (raw_acceleration_mm_s2 >
        estimator->config.maximum_absolute_acceleration_mm_s2) {
        raw_acceleration_mm_s2 =
            estimator->config.maximum_absolute_acceleration_mm_s2;
    }
    else if (raw_acceleration_mm_s2 <
        -estimator->config.maximum_absolute_acceleration_mm_s2) {
        raw_acceleration_mm_s2 =
            -estimator->config.maximum_absolute_acceleration_mm_s2;
    }

    if (!estimator->output_initialized) {
        estimator->filtered_acceleration_mm_s2 = raw_acceleration_mm_s2;
        estimator->output_initialized = true;
    }
    else {
        const float dt_s =
            (float)((uint32_t)(sample_ms - estimator->last_output_ms)) *
            0.001F;
        const float alpha =
            (estimator->config.low_pass_time_constant_s == 0.0F) ? 1.0F :
            dt_s / (estimator->config.low_pass_time_constant_s + dt_s);

        estimator->filtered_acceleration_mm_s2 += alpha *
            (raw_acceleration_mm_s2 -
                estimator->filtered_acceleration_mm_s2);
    }
    estimator->last_output_ms = sample_ms;
    if (!isfinite(estimator->filtered_acceleration_mm_s2)) {
        reset_runtime(estimator);
        return false;
    }
    *acceleration_mm_s2 = estimator->filtered_acceleration_mm_s2;
    *output_valid = true;
    return true;
}

void Algorithm_Filter_WheelAcceleration_Reset(
    Algorithm_Filter_WheelAcceleration *estimator)
{
    if ((estimator == NULL) || !estimator->initialized) {
        return;
    }
    reset_runtime(estimator);
}
