#ifndef DEVICE_LINE_TRACKING_H
#define DEVICE_LINE_TRACKING_H

#include "algorithm_line_tracking.h"
#include "bsp_line_sensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    /* Raw electrical/ADC calibration endpoints before line_is_high polarity. */
    uint16_t calibration_min;
    uint16_t calibration_max;
    /* Digital/threshold path only; analog difference-over-sum ignores it. */
    uint16_t threshold;
    bool line_is_high;
    float position_weight;
} Device_LineTracking_ChannelConfig;

typedef struct {
    BSP_LineSensor_Handle backend;
    size_t channel_count;
    Device_LineTracking_ChannelConfig
        channel[BSP_LINE_SENSOR_MAX_CHANNELS];
    float target_position;
    float minimum_strength_sum;
    size_t intersection_minimum_active_channels;
    Algorithm_LineTracking_Config estimator;
} Device_LineTracking_Config;

typedef struct {
    size_t channel_count;
    uint16_t value[BSP_LINE_SENSOR_MAX_CHANNELS];
    uint32_t valid_mask;
} Device_LineTracking_RawSample;

typedef struct {
    size_t channel_count;
    /* Per-channel calibration and polarity result in the range 0..1. */
    float normalized[BSP_LINE_SENSOR_MAX_CHANNELS];
    /* 0..1 signal passed to the estimator; equals normalized[] for analog. */
    float line_strength[BSP_LINE_SENSOR_MAX_CHANNELS];
    /* Nonzero raw samples before line polarity is applied, bit 0 first. */
    uint32_t raw_high_mask;
    uint32_t active_mask;
    size_t active_count;
    float strength_sum;
    float centroid_position;
    float difference_ratio;
    float difference_position;
    float raw_position;
    float position;
    float confidence;
    /* Positive means the detected line is to the right of target_position. */
    float error;
    bool line_present;
    bool line_lost;
    bool intersection;
    uint32_t sampled_at_ms;
} Device_LineTracking_Sample;

typedef struct {
    Device_LineTracking_Config config;
    Device_LineTracking_Sample sample;
    Algorithm_LineTracking estimator;
    float last_valid_position;
    uint32_t previous_sample_ms;
    bool has_previous_sample_time;
    bool has_last_valid_position;
    bool initialized;
} Device_LineTracking;

/* Produces a safe starting configuration: left-to-right weights -1..1,
 * active-high channels and 50% thresholds. Adjust it before Init when the
 * board uses different polarity, thresholds or geometry. */
bool Device_LineTracking_DefaultConfig(
    BSP_LineSensor_Handle backend, Device_LineTracking_Config *config);
bool Device_LineTracking_DefaultConfigByIndex(
    size_t input_index, Device_LineTracking_Config *config);
/* Builds a transport-independent configuration for externally submitted ADC
 * samples. Each channel initially uses the same raw calibration endpoints;
 * callers may replace them with per-channel measured white/black values. */
bool Device_LineTracking_DefaultRawConfig(
    size_t channel_count,
    uint16_t calibration_min,
    uint16_t calibration_max,
    Device_LineTracking_Config *config);
bool Device_LineTracking_Init(
    Device_LineTracking *device,
    const Device_LineTracking_Config *config);
bool Device_LineTracking_Update(
    Device_LineTracking *device, uint32_t now_ms);
/* Processes one already-framed raw sample without accessing the BSP. */
bool Device_LineTracking_UpdateRaw(
    Device_LineTracking *device,
    const Device_LineTracking_RawSample *raw,
    uint32_t now_ms);
/* Analog path: normalized values first have the current frame's common
 * minimum removed, then go to the weighted difference-over-sum estimator.
 * Direction therefore comes from relative channel differences without
 * stored white/black calibration. Per-channel thresholds are ignored; only
 * the total eight-channel contrast is used to reject uniform background.
 * Position weights affect direction only, not analog line presence or
 * confidence; analog confidence also comes from total frame contrast. */
bool Device_LineTracking_UpdateAnalogRaw(
    Device_LineTracking *device,
    const Device_LineTracking_RawSample *raw,
    uint32_t now_ms);
bool Device_LineTracking_GetSample(
    const Device_LineTracking *device,
    Device_LineTracking_Sample *sample);

#endif
