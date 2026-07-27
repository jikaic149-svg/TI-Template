#include "device_line_tracking.h"

#include <math.h>
#include <stddef.h>

static bool finite_value(float value)
{
    return isfinite(value) != 0;
}

static bool valid_config(const Device_LineTracking_Config *config)
{
    BSP_LineSensor_Info info;
    size_t index;

    if ((config == NULL) || (config->channel_count == 0U) ||
        (config->channel_count > BSP_LINE_SENSOR_MAX_CHANNELS) ||
        !finite_value(config->target_position) ||
        !finite_value(config->minimum_strength_sum) ||
        (config->minimum_strength_sum < 0.0F) ||
        (config->minimum_strength_sum > (float)config->channel_count) ||
        (config->intersection_minimum_active_channels >
            config->channel_count)) {
        return false;
    }
    if ((config->backend != NULL) &&
        (!BSP_LineSensor_GetInfo(config->backend, &info) ||
            (config->channel_count != info.channel_count))) {
        return false;
    }

    for (index = 0U; index < config->channel_count; ++index) {
        const Device_LineTracking_ChannelConfig *channel =
            &config->channel[index];
        if ((channel->calibration_min >= channel->calibration_max) ||
            (channel->threshold >= BSP_LINE_SENSOR_VALUE_MAX) ||
            !finite_value(channel->position_weight)) {
            return false;
        }
    }
    return true;
}

static void populate_default_config(
    size_t channel_count,
    uint16_t calibration_min,
    uint16_t calibration_max,
    Device_LineTracking_Config *config)
{
    *config = (Device_LineTracking_Config){
        .channel_count = channel_count,
        .target_position = 0.0F,
        .minimum_strength_sum = 0.05F,
        .intersection_minimum_active_channels =
            (channel_count >= 3U) ? 3U : channel_count,
        .estimator = Algorithm_LineTracking_DefaultConfig(),
    };
    for (size_t index = 0U; index < channel_count; ++index) {
        config->channel[index].calibration_min = calibration_min;
        config->channel[index].calibration_max = calibration_max;
        config->channel[index].threshold = 5000U;
        config->channel[index].line_is_high = true;
        config->channel[index].position_weight =
            (channel_count == 1U) ? 0.0F :
            -1.0F + (2.0F * (float)index /
                (float)(channel_count - 1U));
    }
}

bool Device_LineTracking_DefaultConfig(
    BSP_LineSensor_Handle backend, Device_LineTracking_Config *config)
{
    BSP_LineSensor_Info info;
    Device_LineTracking_Config next = {0};

    if ((config == NULL) || (backend == NULL) ||
        !BSP_LineSensor_GetInfo(backend, &info) ||
        (info.channel_count == 0U) ||
        (info.channel_count > BSP_LINE_SENSOR_MAX_CHANNELS)) {
        return false;
    }

    populate_default_config(info.channel_count,
        BSP_LINE_SENSOR_VALUE_MIN, BSP_LINE_SENSOR_VALUE_MAX, &next);
    next.backend = backend;
    *config = next;
    return true;
}

bool Device_LineTracking_DefaultConfigByIndex(
    size_t input_index, Device_LineTracking_Config *config)
{
    return Device_LineTracking_DefaultConfig(
        BSP_LineSensor_GetHandle(input_index), config);
}

bool Device_LineTracking_DefaultRawConfig(
    size_t channel_count,
    uint16_t calibration_min,
    uint16_t calibration_max,
    Device_LineTracking_Config *config)
{
    Device_LineTracking_Config next;

    if ((config == NULL) || (channel_count == 0U) ||
        (channel_count > BSP_LINE_SENSOR_MAX_CHANNELS) ||
        (calibration_min >= calibration_max)) {
        return false;
    }
    populate_default_config(
        channel_count, calibration_min, calibration_max, &next);
    *config = next;
    return true;
}

bool Device_LineTracking_Init(
    Device_LineTracking *device,
    const Device_LineTracking_Config *config)
{
    Device_LineTracking next = {0};

    if (device != NULL) {
        *device = (Device_LineTracking){0};
    }
    if ((device == NULL) || !valid_config(config) ||
        !Algorithm_LineTracking_Init(&next.estimator, &config->estimator)) {
        return false;
    }

    next.config = *config;
    next.sample.channel_count = config->channel_count;
    next.sample.centroid_position = config->target_position;
    next.sample.difference_position = config->target_position;
    next.sample.raw_position = config->target_position;
    next.sample.position = config->target_position;
    next.sample.error = 0.0F;
    next.sample.line_lost = true;
    next.initialized = true;
    *device = next;
    return true;
}

bool Device_LineTracking_Update(
    Device_LineTracking *device, uint32_t now_ms)
{
    BSP_LineSensor_RawSample source;
    Device_LineTracking_RawSample raw = {0};

    if ((device == NULL) || !device->initialized ||
        (device->config.backend == NULL) ||
        !valid_config(&device->config) ||
        !BSP_LineSensor_Read(device->config.backend, &source)) {
        return false;
    }
    raw.channel_count = device->config.channel_count;
    raw.valid_mask = source.valid_mask;
    for (size_t index = 0U; index < raw.channel_count; ++index) {
        raw.value[index] = source.value[index];
    }
    return Device_LineTracking_UpdateRaw(device, &raw, now_ms);
}

static bool update_raw(
    Device_LineTracking *device,
    const Device_LineTracking_RawSample *raw,
    uint32_t now_ms,
    bool apply_channel_threshold)
{
    Device_LineTracking_Sample next = {0};
    Algorithm_LineTracking_Input estimator_input;
    Algorithm_LineTracking_Output estimator_output;
    float weights[BSP_LINE_SENSOR_MAX_CHANNELS];
    float left_strength_sum = 0.0F;
    float right_strength_sum = 0.0F;
    float left_weight_sum = 0.0F;
    float right_weight_sum = 0.0F;
    float analog_common_level = 1.0F;
    size_t index;

    if ((device == NULL) || !device->initialized || (raw == NULL) ||
        !valid_config(&device->config) ||
        (raw->channel_count != device->config.channel_count)) {
        return false;
    }

    next.channel_count = device->config.channel_count;
    next.sampled_at_ms = now_ms;
    for (index = 0U; index < next.channel_count; ++index) {
        const uint32_t bit = UINT32_C(1) << index;
        const Device_LineTracking_ChannelConfig *channel =
            &device->config.channel[index];
        float response;

        if ((raw->valid_mask & bit) == 0U) {
            return false;
        }
        if (raw->value[index] != 0U) {
            next.raw_high_mask |= bit;
        }
        response = ((float)raw->value[index] -
            (float)channel->calibration_min) /
            ((float)channel->calibration_max -
                (float)channel->calibration_min);
        if (response < 0.0F) {
            response = 0.0F;
        }
        else if (response > 1.0F) {
            response = 1.0F;
        }
        if (!channel->line_is_high) {
            response = 1.0F - response;
        }
        if (!apply_channel_threshold && (response < analog_common_level)) {
            analog_common_level = response;
        }
        next.normalized[index] = response;
        weights[index] = channel->position_weight;
    }

    for (index = 0U; index < next.channel_count; ++index) {
        const uint32_t bit = UINT32_C(1) << index;
        const Device_LineTracking_ChannelConfig *channel =
            &device->config.channel[index];
        float strength;

        if (apply_channel_threshold) {
            const float threshold = (float)channel->threshold /
                (float)BSP_LINE_SENSOR_VALUE_MAX;
            strength = (next.normalized[index] > threshold) ?
                (next.normalized[index] - threshold) /
                    (1.0F - threshold) : 0.0F;
        }
        else {
            /* Analog direction uses only channel-to-channel contrast in the
             * current frame. This removes the common optical/ADC baseline
             * without stored white/black calibration or channel thresholds. */
            strength = next.normalized[index] - analog_common_level;
        }
        if (!finite_value(strength)) {
            return false;
        }

        next.line_strength[index] = strength;
        next.strength_sum += strength;
        if (strength > 0.0F) {
            next.active_mask |= bit;
            ++next.active_count;
        }
        if (channel->position_weight < device->config.target_position) {
            const float span = device->config.target_position -
                channel->position_weight;
            left_strength_sum += strength * span;
            left_weight_sum += span;
        }
        else if (channel->position_weight >
            device->config.target_position) {
            const float span = channel->position_weight -
                device->config.target_position;
            right_strength_sum += strength * span;
            right_weight_sum += span;
        }
    }

    const float estimator_denominator =
        ((left_weight_sum > 0.0F) ?
            left_strength_sum / left_weight_sum : 0.0F) +
        ((right_weight_sum > 0.0F) ?
            right_strength_sum / right_weight_sum : 0.0F);
    /* Direction weighting must not hide a valid line close to the center.
     * Analog presence uses total frame contrast; digital presence retains
     * the existing thresholded left/right level check. */
    const float presence_level = apply_channel_threshold ?
        estimator_denominator : next.strength_sum;

    next.line_present = (next.active_count > 0U) &&
        (next.strength_sum >= device->config.minimum_strength_sum) &&
        (presence_level >
            device->config.estimator.denominator_epsilon);
    next.line_lost = !next.line_present;
    next.intersection = apply_channel_threshold && next.line_present &&
        (device->config.intersection_minimum_active_channels > 0U) &&
        (next.active_count >=
            device->config.intersection_minimum_active_channels);

    if (next.line_present) {
        estimator_input.strength = next.line_strength;
        estimator_input.position_weight = weights;
        estimator_input.channel_count = next.channel_count;
        estimator_input.target_position = device->config.target_position;
        estimator_input.dt_seconds = device->has_previous_sample_time ?
            (float)((uint32_t)(now_ms - device->previous_sample_ms)) /
                1000.0F : 0.0F;
        if (!Algorithm_LineTracking_Update(
                &device->estimator, &estimator_input, &estimator_output)) {
            return false;
        }
        next.centroid_position = estimator_output.centroid_position;
        next.difference_ratio = estimator_output.difference_ratio;
        next.difference_position = estimator_output.difference_position;
        next.raw_position = estimator_output.raw_position;
        next.position = estimator_output.filtered_position;
        next.confidence = apply_channel_threshold ?
            estimator_output.confidence :
            next.strength_sum / (next.strength_sum +
                device->config.estimator.denominator_epsilon);
        device->last_valid_position = next.position;
        device->has_last_valid_position = true;
    }
    else {
        Algorithm_LineTracking_Reset(&device->estimator);
        next.centroid_position = device->config.target_position;
        next.difference_position = device->config.target_position;
        next.raw_position = device->has_last_valid_position ?
            device->last_valid_position : device->config.target_position;
        next.position = next.raw_position;
    }
    next.error = next.position - device->config.target_position;
    if (!finite_value(next.error)) {
        return false;
    }

    device->previous_sample_ms = now_ms;
    device->has_previous_sample_time = true;
    device->sample = next;
    return true;
}

bool Device_LineTracking_UpdateRaw(
    Device_LineTracking *device,
    const Device_LineTracking_RawSample *raw,
    uint32_t now_ms)
{
    return update_raw(device, raw, now_ms, true);
}

bool Device_LineTracking_UpdateAnalogRaw(
    Device_LineTracking *device,
    const Device_LineTracking_RawSample *raw,
    uint32_t now_ms)
{
    return update_raw(device, raw, now_ms, false);
}

bool Device_LineTracking_GetSample(
    const Device_LineTracking *device,
    Device_LineTracking_Sample *sample)
{
    if ((device == NULL) || !device->initialized || (sample == NULL)) {
        return false;
    }
    *sample = device->sample;
    return true;
}
