#include "algorithm_filter_wheel_acceleration.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static Algorithm_Filter_WheelAccelerationConfig make_config(void)
{
    return (Algorithm_Filter_WheelAccelerationConfig){
        .window_sample_count = 13U,
        .maximum_sample_interval_ms = 30U,
        .low_pass_time_constant_s = 0.08F,
        .maximum_absolute_acceleration_mm_s2 = 1000.0F,
    };
}

static float run_ramp(float acceleration_mm_s2)
{
    Algorithm_Filter_WheelAcceleration estimator;
    const Algorithm_Filter_WheelAccelerationConfig config = make_config();
    float output = 0.0F;
    bool valid = false;

    assert(Algorithm_Filter_WheelAcceleration_Init(&estimator, &config));
    for (uint32_t index = 0U; index < 250U; ++index) {
        const uint32_t sample_ms = index * 10U;
        const float ideal_speed = acceleration_mm_s2 *
            (float)sample_ms * 0.001F;
        const float quantized_speed = floorf(ideal_speed + 0.5F);

        assert(Algorithm_Filter_WheelAcceleration_Update(
            &estimator, sample_ms, quantized_speed, &output, &valid));
    }
    assert(valid);
    return output;
}

static void test_constant_speed_and_quantization_have_no_bias(void)
{
    Algorithm_Filter_WheelAcceleration estimator;
    const Algorithm_Filter_WheelAccelerationConfig config = make_config();
    static const float noise[] = { 0.0F, 1.0F, 0.0F, -1.0F };
    float output = 0.0F;
    bool valid = false;

    assert(Algorithm_Filter_WheelAcceleration_Init(&estimator, &config));
    for (uint32_t index = 0U; index < 300U; ++index) {
        assert(Algorithm_Filter_WheelAcceleration_Update(
            &estimator,
            index * 10U,
            234.0F + noise[index % 4U],
            &output,
            &valid));
    }
    assert(valid);
    assert(fabsf(output) < 2.0F);
}

static void test_positive_and_negative_ramps(void)
{
    assert(fabsf(run_ramp(100.0F) - 100.0F) < 3.0F);
    assert(fabsf(run_ramp(-80.0F) + 80.0F) < 3.0F);
}

static void test_gap_duplicate_and_wrap_restart_window(void)
{
    Algorithm_Filter_WheelAcceleration estimator;
    const Algorithm_Filter_WheelAccelerationConfig config = make_config();
    float output = 0.0F;
    bool valid = false;

    assert(Algorithm_Filter_WheelAcceleration_Init(&estimator, &config));
    for (uint32_t index = 0U; index < 13U; ++index) {
        assert(Algorithm_Filter_WheelAcceleration_Update(
            &estimator, UINT32_MAX - 50U + index * 10U,
            (float)index, &output, &valid));
    }
    assert(valid);

    assert(Algorithm_Filter_WheelAcceleration_Update(
        &estimator, estimator.last_sample_ms, 20.0F, &output, &valid));
    assert(!valid);
    assert(estimator.sample_count == 1U);
    assert(Algorithm_Filter_WheelAcceleration_Update(
        &estimator, estimator.last_sample_ms + 31U,
        21.0F, &output, &valid));
    assert(!valid);
    assert(estimator.sample_count == 1U);
}

static void test_limit_and_explicit_reset(void)
{
    Algorithm_Filter_WheelAcceleration estimator;
    const Algorithm_Filter_WheelAccelerationConfig config = make_config();
    float output = 0.0F;
    bool valid = false;

    assert(Algorithm_Filter_WheelAcceleration_Init(&estimator, &config));
    for (uint32_t index = 0U; index < 40U; ++index) {
        assert(Algorithm_Filter_WheelAcceleration_Update(
            &estimator, index * 10U, (float)index * 100.0F,
            &output, &valid));
    }
    assert(valid);
    assert(output <= config.maximum_absolute_acceleration_mm_s2);
    assert(output > 990.0F);
    Algorithm_Filter_WheelAcceleration_Reset(&estimator);
    assert(estimator.sample_count == 0U);
    assert(!estimator.output_initialized);
}

int main(void)
{
    test_constant_speed_and_quantization_have_no_bias();
    test_positive_and_negative_ramps();
    test_gap_duplicate_and_wrap_restart_window();
    test_limit_and_explicit_reset();
    puts("wheel acceleration estimator tests: PASS");
    return 0;
}
