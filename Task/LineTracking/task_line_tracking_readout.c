#include "task_line_tracking_readout.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "device_line_tracking.h"
#include "device_serial.h"
#include "device_xinliu_t8.h"
#include "mode_car.h"

#define TASK_SENSOR_UART_INDEX 3U
#define TASK_BLUETOOTH_UART_INDEX 2U
#define TASK_UART_BAUD UINT32_C(115200)
#define TASK_QUERY_TIMEOUT_MS UINT32_C(50)
#define TASK_SAMPLE_FRESH_TIMEOUT_MS UINT32_C(100)
#define TASK_CONTROL_DT_SECONDS 0.010F
#define TASK_MAXIMUM_DT_SECONDS 0.050F
#define TASK_ANALOG_CALIBRATION_MIN 0U
#define TASK_ANALOG_CALIBRATION_MAX 4095U
#define TASK_MODE_BASE_DRIVE 0.35F
#define TASK_RX_CHUNK_SIZE 64U
#define TASK_MAXIMUM_RX_PASSES 4U
#define TASK_TELEMETRY_DIVIDER 2U
#define TASK_TELEMETRY_BUFFER_SIZE 512U

typedef enum {
    TASK_LINE_TRACKING_WAITING = 0,
    TASK_LINE_TRACKING_ONLINE = 1,
    TASK_LINE_TRACKING_STALE = 2,
    TASK_LINE_TRACKING_FAULT = 3,
} Task_LineTrackingReadout_Status;

typedef struct {
    Device_XinliuT8 xinliu;
    Device_XinliuT8_Snapshot snapshot;
    Device_XinliuT8_Stats protocol_stats;
    Device_LineTracking line_tracking;
    Device_LineTracking_Sample sample;
    Device_Serial sensor_serial;
    Device_Serial bluetooth;
    Mode_Car_AnalogTrackingContext analog_mode;
    Mode_Car_Output mode_output;
    uint8_t query[DEVICE_XINLIU_T8_READ_ANALOG_QUERY_SIZE];
    size_t query_length;
    uint32_t query_sent_at_ms;
    uint32_t last_mode_sample_ms;
    uint32_t query_timeouts;
    uint32_t uart_error_events;
    uint32_t dropped_telemetry_frames;
    uint8_t telemetry_divider;
    char telemetry_buffer[TASK_TELEMETRY_BUFFER_SIZE];
    Task_LineTrackingReadout_Status status;
    bool query_pending;
    bool has_snapshot;
    bool has_mode_sample_time;
    bool runtime_fault;
    bool initialized;
} Task_LineTrackingReadout_State;

static Task_LineTrackingReadout_State g_readout;

/* Commissioning defaults. Replace each endpoint with the measured white and
 * black values for that physical channel after collecting a stationary log. */
static const uint16_t g_channel_calibration_min[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
};
static const uint16_t g_channel_calibration_max[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT] = {
    4095U, 4095U, 4095U, 4095U,
    4095U, 4095U, 4095U, 4095U,
};
static const float g_channel_position_weight[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT] = {
    -1.0F, -0.60F, -0.25F, 0.0F,
     0.0F, 0.25F, 0.60F, 1.0F,
};

volatile uint16_t g_TASK_LineTrackingRaw[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT];
volatile float g_TASK_LineTrackingNormalized[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT];
volatile float g_TASK_LineTrackingStrength[
    TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT];
volatile float g_TASK_LineTrackingDifferenceRatio;
volatile float g_TASK_LineTrackingPosition;
volatile float g_TASK_LineTrackingError;
volatile float g_TASK_LineTrackingConfidence;
volatile float g_TASK_LineTrackingLeftIntent;
volatile float g_TASK_LineTrackingRightIntent;
volatile uint32_t g_TASK_LineTrackingStatus;
volatile uint32_t g_TASK_LineTrackingAcceptedFrames;
volatile uint32_t g_TASK_LineTrackingRejectedFrames;
volatile uint32_t g_TASK_LineTrackingQueryTimeouts;

static void increment_saturating(uint32_t *value)
{
    if ((value != NULL) && (*value != UINT32_MAX)) {
        ++(*value);
    }
}

static void publish_debug_snapshot(void)
{
    for (size_t index = 0U;
         index < TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT;
         ++index) {
        g_TASK_LineTrackingRaw[index] = g_readout.snapshot.adc[index];
        g_TASK_LineTrackingNormalized[index] =
            g_readout.sample.normalized[index];
        g_TASK_LineTrackingStrength[index] =
            g_readout.sample.line_strength[index];
    }
    g_TASK_LineTrackingDifferenceRatio =
        g_readout.sample.difference_ratio;
    g_TASK_LineTrackingPosition = g_readout.sample.position;
    g_TASK_LineTrackingError = g_readout.sample.error;
    g_TASK_LineTrackingConfidence = g_readout.sample.confidence;
    g_TASK_LineTrackingLeftIntent = g_readout.mode_output.left_drive;
    g_TASK_LineTrackingRightIntent = g_readout.mode_output.right_drive;
    g_TASK_LineTrackingStatus = (uint32_t)g_readout.status;
    g_TASK_LineTrackingAcceptedFrames =
        g_readout.protocol_stats.accepted_frames;
    g_TASK_LineTrackingRejectedFrames =
        g_readout.protocol_stats.rejected_frames;
    g_TASK_LineTrackingQueryTimeouts = g_readout.query_timeouts;
}

static float sample_dt_seconds(uint32_t sampled_at_ms)
{
    float dt_seconds = TASK_CONTROL_DT_SECONDS;

    if (g_readout.has_mode_sample_time) {
        dt_seconds = (float)((uint32_t)(sampled_at_ms -
            g_readout.last_mode_sample_ms)) / 1000.0F;
        if (dt_seconds <= 0.0F) {
            dt_seconds = TASK_CONTROL_DT_SECONDS;
        }
        else if (dt_seconds > TASK_MAXIMUM_DT_SECONDS) {
            dt_seconds = TASK_MAXIMUM_DT_SECONDS;
        }
    }
    g_readout.last_mode_sample_ms = sampled_at_ms;
    g_readout.has_mode_sample_time = true;
    return dt_seconds;
}

static bool process_snapshot(
    const Device_XinliuT8_Snapshot *snapshot, uint32_t now_ms)
{
    Device_LineTracking_RawSample raw = {
        .channel_count = TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT,
        .valid_mask = UINT32_C(0xFF),
    };
    Mode_Car_AnalogTrackingInput mode_input;

    if (snapshot == NULL) {
        return false;
    }
    for (size_t index = 0U;
         index < TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT;
         ++index) {
        raw.value[index] = snapshot->adc[index];
    }
    if (!Device_LineTracking_UpdateAnalogRaw(
            &g_readout.line_tracking, &raw, snapshot->received_at_ms) ||
        !Device_LineTracking_GetSample(
            &g_readout.line_tracking, &g_readout.sample)) {
        return false;
    }
    mode_input = (Mode_Car_AnalogTrackingInput){
        .time = {
            .now_ms = now_ms,
            .dt_seconds = sample_dt_seconds(snapshot->received_at_ms),
        },
        .base_drive = TASK_MODE_BASE_DRIVE,
        .analog_error = g_readout.sample.error,
        .confidence = g_readout.sample.confidence,
        .line_valid = g_readout.sample.line_present &&
            Device_XinliuT8_SnapshotFresh(snapshot, now_ms,
                TASK_SAMPLE_FRESH_TIMEOUT_MS),
        .line_lost = g_readout.sample.line_lost,
        .intersection = g_readout.sample.intersection,
    };
    if (!Mode_Car_AnalogTracking_Update(
            &g_readout.analog_mode, &mode_input,
            &g_readout.mode_output)) {
        return false;
    }
    g_readout.snapshot = *snapshot;
    g_readout.has_snapshot = true;
    g_readout.status = TASK_LINE_TRACKING_ONLINE;
    publish_debug_snapshot();
    return true;
}

static bool drain_sensor_uart(uint32_t now_ms)
{
    uint8_t bytes[TASK_RX_CHUNK_SIZE];

    for (size_t pass = 0U; pass < TASK_MAXIMUM_RX_PASSES; ++pass) {
        size_t transferred = 0U;
        const Device_Serial_Result read_result =
            Device_Serial_ReadAvailable(&g_readout.sensor_serial,
                bytes, sizeof(bytes), &transferred);

        if (read_result != DEVICE_SERIAL_RESULT_OK) {
            increment_saturating(&g_readout.uart_error_events);
            (void)Device_Serial_Recover(&g_readout.sensor_serial);
            Device_XinliuT8_ResetStream(&g_readout.xinliu);
            g_readout.query_pending = false;
            return true;
        }
        for (size_t index = 0U; index < transferred; ++index) {
            const Device_XinliuT8_FeedResult feed_result =
                Device_XinliuT8_FeedByte(
                    &g_readout.xinliu, now_ms, bytes[index]);

            if (feed_result == DEVICE_XINLIU_T8_FEED_SAMPLE_READY) {
                Device_XinliuT8_Snapshot snapshot;

                g_readout.query_pending = false;
                if (!Device_XinliuT8_GetLatest(
                        &g_readout.xinliu, &snapshot) ||
                    !process_snapshot(&snapshot, now_ms)) {
                    return false;
                }
            }
            else if (feed_result ==
                DEVICE_XINLIU_T8_FEED_FRAME_REJECTED) {
                g_readout.query_pending = false;
            }
            else if (feed_result ==
                DEVICE_XINLIU_T8_FEED_INVALID_ARGUMENT) {
                return false;
            }
        }
        if (transferred < sizeof(bytes)) {
            break;
        }
    }
    return Device_XinliuT8_GetStats(
        &g_readout.xinliu, &g_readout.protocol_stats);
}

static void service_uart_errors(void)
{
    Device_Serial_RxStatus status;

    if (!Device_Serial_GetRxStatus(&g_readout.sensor_serial, &status)) {
        increment_saturating(&g_readout.uart_error_events);
        return;
    }
    if (status.sticky_errors != DEVICE_SERIAL_RX_ERROR_NONE) {
        increment_saturating(&g_readout.uart_error_events);
        (void)Device_Serial_ClearRxStatus(&g_readout.sensor_serial);
    }
}

static void service_query(uint32_t now_ms)
{
    if (g_readout.query_pending &&
        ((uint32_t)(now_ms - g_readout.query_sent_at_ms) >
            TASK_QUERY_TIMEOUT_MS)) {
        increment_saturating(&g_readout.query_timeouts);
        Device_XinliuT8_ResetStream(&g_readout.xinliu);
        g_readout.query_pending = false;
    }
    if (!g_readout.query_pending) {
        const Device_Serial_Result result = Device_Serial_WriteBuffered(
            &g_readout.sensor_serial,
            g_readout.query, g_readout.query_length);

        if (result == DEVICE_SERIAL_RESULT_OK) {
            g_readout.query_sent_at_ms = now_ms;
            g_readout.query_pending = true;
        }
        else if (result != DEVICE_SERIAL_RESULT_BUSY) {
            increment_saturating(&g_readout.uart_error_events);
            (void)Device_Serial_Recover(&g_readout.sensor_serial);
            Device_XinliuT8_ResetStream(&g_readout.xinliu);
        }
    }
}

static bool stop_stale_mode(uint32_t now_ms)
{
    Mode_Car_AnalogTrackingInput input;

    if (!g_readout.has_snapshot ||
        Device_XinliuT8_SnapshotFresh(&g_readout.snapshot, now_ms,
            TASK_SAMPLE_FRESH_TIMEOUT_MS)) {
        return true;
    }
    input = (Mode_Car_AnalogTrackingInput){
        .time = {
            .now_ms = now_ms,
            .dt_seconds = TASK_CONTROL_DT_SECONDS,
        },
        .base_drive = TASK_MODE_BASE_DRIVE,
        .analog_error = g_readout.sample.error,
        .confidence = 0.0F,
        .line_valid = false,
        .line_lost = true,
        .intersection = false,
    };
    if (!Mode_Car_AnalogTracking_Update(
            &g_readout.analog_mode, &input,
            &g_readout.mode_output)) {
        return false;
    }
    g_readout.status = TASK_LINE_TRACKING_STALE;
    publish_debug_snapshot();
    return true;
}

static bool append_char(char value, size_t *length)
{
    if ((length == NULL) || (*length >= TASK_TELEMETRY_BUFFER_SIZE)) {
        return false;
    }
    g_readout.telemetry_buffer[*length] = value;
    ++(*length);
    return true;
}

static bool append_unsigned(uint32_t value, size_t *length)
{
    char reverse[10];
    size_t count = 0U;

    do {
        reverse[count] = (char)('0' + (value % UINT32_C(10)));
        value /= UINT32_C(10);
        ++count;
    } while ((value != 0U) && (count < sizeof(reverse)));
    while (count > 0U) {
        --count;
        if (!append_char(reverse[count], length)) {
            return false;
        }
    }
    return true;
}

static bool append_float3(float value, size_t *length)
{
    const bool negative = value < 0.0F;
    const float magnitude = negative ? -value : value;
    uint32_t scaled;
    uint32_t fraction;

    if (!isfinite(value) || (magnitude > 99999.999F)) {
        return false;
    }
    scaled = (uint32_t)(magnitude * 1000.0F + 0.5F);
    if (negative && (scaled != 0U) && !append_char('-', length)) {
        return false;
    }
    if (!append_unsigned(scaled / 1000U, length) ||
        !append_char('.', length)) {
        return false;
    }
    fraction = scaled % 1000U;
    return append_char((char)('0' + (fraction / 100U)), length) &&
        append_char((char)('0' + ((fraction / 10U) % 10U)), length) &&
        append_char((char)('0' + (fraction % 10U)), length);
}

static bool append_separator(size_t *length)
{
    return (*length == 0U) || append_char(',', length);
}

static bool append_u32_field(uint32_t value, size_t *length)
{
    return append_separator(length) && append_unsigned(value, length);
}

static bool append_float_field(float value, size_t *length)
{
    return append_separator(length) && append_float3(value, length);
}

static bool build_telemetry_frame(size_t *frame_length)
{
    size_t length = 0U;

    if (frame_length == NULL) {
        return false;
    }
    for (size_t index = 0U;
         index < TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT;
         ++index) {
        if (!append_u32_field(g_readout.snapshot.adc[index], &length)) {
            return false;
        }
    }
    for (size_t index = 0U;
         index < TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT;
         ++index) {
        if (!append_float_field(
                g_readout.sample.normalized[index], &length)) {
            return false;
        }
    }
    if (!append_float_field(g_readout.sample.difference_ratio, &length) ||
        !append_float_field(g_readout.sample.centroid_position, &length) ||
        !append_float_field(g_readout.sample.position, &length) ||
        !append_float_field(g_readout.sample.error, &length) ||
        !append_float_field(g_readout.sample.confidence, &length) ||
        !append_float_field(g_readout.mode_output.left_drive, &length) ||
        !append_float_field(g_readout.mode_output.right_drive, &length) ||
        !append_u32_field(g_readout.sample.line_present ? 1U : 0U,
            &length) ||
        !append_u32_field(g_readout.sample.line_lost ? 1U : 0U,
            &length) ||
        !append_u32_field(g_readout.sample.intersection ? 1U : 0U,
            &length) ||
        !append_u32_field(g_readout.protocol_stats.accepted_frames,
            &length) ||
        !append_u32_field(g_readout.protocol_stats.rejected_frames,
            &length) ||
        !append_u32_field(g_readout.query_timeouts, &length) ||
        !append_u32_field(g_readout.uart_error_events, &length) ||
        !append_u32_field(g_readout.dropped_telemetry_frames, &length) ||
        !append_u32_field((uint32_t)g_readout.status, &length) ||
        !append_char('\r', &length) || !append_char('\n', &length)) {
        return false;
    }
    *frame_length = length;
    return true;
}

static void publish_telemetry(void)
{
    size_t frame_length;

    ++g_readout.telemetry_divider;
    if (g_readout.telemetry_divider < TASK_TELEMETRY_DIVIDER) {
        return;
    }
    g_readout.telemetry_divider = 0U;
    if (!build_telemetry_frame(&frame_length) ||
        (Device_Serial_WriteBuffered(&g_readout.bluetooth,
            (const uint8_t *)g_readout.telemetry_buffer,
            frame_length) != DEVICE_SERIAL_RESULT_OK)) {
        increment_saturating(&g_readout.dropped_telemetry_frames);
    }
}

bool Task_LineTrackingReadout_Init(void)
{
    Device_LineTracking_Config line_config;
    Mode_Car_AnalogTrackingConfig mode_config =
        Mode_Car_AnalogTracking_DefaultConfig();
    const Device_Serial_Config sensor_serial_config = {
        .transport_index = TASK_SENSOR_UART_INDEX,
        .expected_baud_rate = TASK_UART_BAUD,
        .minimum_tx_capacity = DEVICE_XINLIU_T8_READ_ANALOG_QUERY_SIZE,
        .minimum_rx_capacity = DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE,
        .require_buffered_tx = true,
        .require_buffered_rx = true,
    };
    const Device_Serial_Config bluetooth_config = {
        .transport_index = TASK_BLUETOOTH_UART_INDEX,
        .expected_baud_rate = TASK_UART_BAUD,
        .minimum_tx_capacity = TASK_TELEMETRY_BUFFER_SIZE,
        .minimum_rx_capacity = 0U,
        .require_buffered_tx = true,
        .require_buffered_rx = false,
    };

    g_readout = (Task_LineTrackingReadout_State){0};
    g_readout.status = TASK_LINE_TRACKING_WAITING;
    if (!Device_XinliuT8_Init(&g_readout.xinliu) ||
        !Device_XinliuT8_BuildReadAnalogQuery(g_readout.query,
            sizeof(g_readout.query), &g_readout.query_length) ||
        !Device_LineTracking_DefaultRawConfig(
            TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT,
            TASK_ANALOG_CALIBRATION_MIN,
            TASK_ANALOG_CALIBRATION_MAX,
            &line_config)) {
        g_readout.status = TASK_LINE_TRACKING_FAULT;
        return false;
    }
    line_config.minimum_strength_sum = 0.0F;
    for (size_t index = 0U;
         index < TASK_LINE_TRACKING_READOUT_CHANNEL_COUNT;
         ++index) {
        line_config.channel[index].calibration_min =
            g_channel_calibration_min[index];
        line_config.channel[index].calibration_max =
            g_channel_calibration_max[index];
        line_config.channel[index].line_is_high = true;
        line_config.channel[index].position_weight =
            g_channel_position_weight[index];
    }
    if (!Device_LineTracking_Init(
            &g_readout.line_tracking, &line_config) ||
        !Mode_Car_AnalogTracking_Init(
            &g_readout.analog_mode, &mode_config) ||
        !Device_Serial_Init(
            &g_readout.sensor_serial, &sensor_serial_config) ||
        !Device_Serial_Init(&g_readout.bluetooth, &bluetooth_config)) {
        g_readout.status = TASK_LINE_TRACKING_FAULT;
        return false;
    }
    g_readout.initialized = true;
    publish_debug_snapshot();
    return true;
}

void Task_LineTrackingReadout_Update(uint32_t now_ms)
{
    if (!g_readout.initialized || g_readout.runtime_fault) {
        return;
    }
    if (!drain_sensor_uart(now_ms) || !stop_stale_mode(now_ms)) {
        g_readout.runtime_fault = true;
        g_readout.status = TASK_LINE_TRACKING_FAULT;
        g_readout.mode_output = (Mode_Car_Output){0};
        publish_debug_snapshot();
        return;
    }
    service_uart_errors();
    service_query(now_ms);
    publish_telemetry();
    publish_debug_snapshot();
}

bool Task_LineTrackingReadout_OutputEnabled(void)
{
    return false;
}

bool Task_LineTrackingReadout_ControlFaulted(void)
{
    return g_readout.runtime_fault;
}
