#include "task_analog_tracking.h"
#include "task_analog_tracking_config.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "device_encoder.h"
#include "device_icm45686_spi.h"
#include "device_key_input.h"
#include "device_line_tracking.h"
#include "device_motor.h"
#include "device_rgb.h"
#include "device_serial.h"
#include "device_xinliu_t8.h"
#include "mode_car.h"
#include "mode_car_control.h"
#include "mode_communicate.h"

typedef enum {
    TASK_ANALOG_TRACKING_CALIBRATING = 0,
    TASK_ANALOG_TRACKING_READY = 5,
    TASK_ANALOG_TRACKING_ACTIVE = 6,
    TASK_ANALOG_TRACKING_FAULT = 7,
} Task_AnalogTracking_Status;

typedef enum {
    TASK_ANALOG_TRACKING_FAULT_NONE = 0,
    TASK_ANALOG_TRACKING_FAULT_KEY = 1,
    TASK_ANALOG_TRACKING_FAULT_IMU = 2,
    TASK_ANALOG_TRACKING_FAULT_ENCODER = 3,
    TASK_ANALOG_TRACKING_FAULT_MOTOR = 4,
    TASK_ANALOG_TRACKING_FAULT_CONTROLLER = 5,
    TASK_ANALOG_TRACKING_FAULT_SENSOR_UART = 6,
    TASK_ANALOG_TRACKING_FAULT_TIMING = 7,
} Task_AnalogTracking_Fault;

typedef struct {
    Device_ICM45686_SPI icm45686;
    Device_ICM45686_Sample icm45686_sample;
    Device_IMU_Snapshot imu;
    Device_Encoder encoder_a;
    Device_Encoder encoder_b;
    Device_Encoder_Sample encoder_a_sample;
    Device_Encoder_Sample encoder_b_sample;
    Device_KeyInput key;
    Device_Motor motor_a;
    Device_Motor motor_b;
    Device_RGB rgb;
    Device_Serial sensor_serial;
    Device_Serial bluetooth;
    Device_XinliuT8 xinliu;
    Device_XinliuT8_Snapshot snapshot;
    Device_XinliuT8_Stats protocol_stats;
    Device_LineTracking line_tracking;
    Device_LineTracking_Sample line_sample;
    Mode_Car_AnalogTrackingConfig analog_config;
    Mode_Car_AnalogTrackingContext analog_mode;
    Mode_Car_Output analog_output;
    Mode_Car_AngleController angle_controller;
    Mode_Car_AngleControllerOutput angle_output;
    Mode_Car_SpeedController speed_controller;
    Mode_Car_SpeedControllerOutput speed_output;
    Mode_Communicate_BluetoothPCContext bluetooth_mode;
    Mode_Communicate_BluetoothOutput bluetooth_output;
    Device_ICM45686_Status imu_status;
    Task_AnalogTracking_Status status;
    Task_AnalogTracking_Fault fault;
    uint8_t query[DEVICE_XINLIU_T8_READ_ANALOG_QUERY_SIZE];
    size_t query_length;
    uint32_t query_sent_at_ms;
    uint32_t last_sampled_at_ms;
    uint32_t query_timeouts;
    uint32_t uart_error_events;
    uint32_t dropped_telemetry_frames;
    float target_yaw_deg;
    float steering_error_deg;
    float target_speed_a_mm_per_second;
    float target_speed_b_mm_per_second;
    float measured_speed_a_mm_per_second;
    float measured_speed_b_mm_per_second;
    float effort_a_percent;
    float effort_b_percent;
    uint8_t telemetry_divider;
    char telemetry_buffer[TASK_TELEMETRY_BUFFER_SIZE];
    bool query_pending;
    bool has_snapshot;
    bool has_sample_timestamp;
    bool track_usable;
    bool motors_unlocked;
    bool initialized;
} Task_AnalogTracking_State;

static Task_AnalogTracking_State g_tracking;

static const uint16_t g_channel_calibration_min[
    TASK_ANALOG_TRACKING_CHANNEL_COUNT] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
};
static const uint16_t g_channel_calibration_max[
    TASK_ANALOG_TRACKING_CHANNEL_COUNT] = {
    4095U, 4095U, 4095U, 4095U,
    4095U, 4095U, 4095U, 4095U,
};
static const float g_channel_position_weight[
    TASK_ANALOG_TRACKING_CHANNEL_COUNT] = {
    -1.0F, -0.60F, -0.25F, 0.0F,
     0.0F, 0.25F, 0.60F, 1.0F,
};

static const Device_RGB_Color g_rgb_calibrating = {16U, 8U, 0U};
static const Device_RGB_Color g_rgb_stopped = {0U, 0U, 16U};
static const Device_RGB_Color g_rgb_enabled = {0U, 16U, 0U};
static const Device_RGB_Color g_rgb_fault = {16U, 0U, 0U};

volatile uint16_t g_TASK_AnalogTrackingRaw[
    TASK_ANALOG_TRACKING_CHANNEL_COUNT];
volatile float g_TASK_AnalogTrackingDifferenceRatio;
volatile float g_TASK_AnalogTrackingPosition;
volatile float g_TASK_AnalogTrackingError;
volatile float g_TASK_AnalogTrackingConfidence;
volatile float g_TASK_AnalogTrackingSteeringErrorDeg;
volatile float g_TASK_AnalogTrackingTargetYawDeg;
volatile float g_TASK_AnalogTrackingMeasuredYawDeg;
volatile float g_TASK_AnalogTrackingTargetSpeedA;
volatile float g_TASK_AnalogTrackingMeasuredSpeedA;
volatile float g_TASK_AnalogTrackingEffortA;
volatile float g_TASK_AnalogTrackingTargetSpeedB;
volatile float g_TASK_AnalogTrackingMeasuredSpeedB;
volatile float g_TASK_AnalogTrackingEffortB;
volatile uint32_t g_TASK_AnalogTrackingStatus;
volatile uint32_t g_TASK_AnalogTrackingFault;
volatile uint32_t g_TASK_AnalogTrackingUsable;
volatile uint32_t g_TASK_AnalogTrackingAcceptedFrames;
volatile uint32_t g_TASK_AnalogTrackingRejectedFrames;
volatile uint32_t g_TASK_AnalogTrackingQueryTimeouts;

static void increment_saturating(uint32_t *value)
{
    if ((value != NULL) && (*value != UINT32_MAX)) {
        ++(*value);
    }
}

static void reset_control_output(void)
{
    g_tracking.analog_output = (Mode_Car_Output){0};
    g_tracking.angle_output = (Mode_Car_AngleControllerOutput){0};
    g_tracking.speed_output = (Mode_Car_SpeedControllerOutput){0};
    g_tracking.target_yaw_deg = g_tracking.imu.yaw_deg;
    g_tracking.steering_error_deg = 0.0F;
    g_tracking.target_speed_a_mm_per_second = 0.0F;
    g_tracking.target_speed_b_mm_per_second = 0.0F;
    g_tracking.effort_a_percent = 0.0F;
    g_tracking.effort_b_percent = 0.0F;
}

static bool reset_controllers(void)
{
    Mode_Car_AngleController_Reset(&g_tracking.angle_controller);
    Mode_Car_SpeedController_Reset(&g_tracking.speed_controller);
    reset_control_output();
    return Mode_Car_AnalogTracking_Init(
        &g_tracking.analog_mode, &g_tracking.analog_config);
}

static bool motor_pair_lock(void)
{
    const Device_Motor_Result result_a =
        Device_Motor_SetUnlocked(&g_tracking.motor_a, false);
    const Device_Motor_Result result_b =
        Device_Motor_SetUnlocked(&g_tracking.motor_b, false);

    g_tracking.motors_unlocked = false;
    reset_control_output();
    return (result_a == DEVICE_MOTOR_RESULT_OK) &&
        (result_b == DEVICE_MOTOR_RESULT_OK);
}

static bool motor_pair_unlock(void)
{
    if ((Device_Motor_SetUnlocked(&g_tracking.motor_a, true) !=
            DEVICE_MOTOR_RESULT_OK) ||
        (Device_Motor_SetUnlocked(&g_tracking.motor_b, true) !=
            DEVICE_MOTOR_RESULT_OK)) {
        (void)motor_pair_lock();
        return false;
    }
    g_tracking.motors_unlocked = true;
    return true;
}

static bool apply_motor_commands(int32_t command_a, int32_t command_b)
{
    return (Device_Motor_SetCommand(&g_tracking.motor_a, command_a) ==
            DEVICE_MOTOR_RESULT_OK) &&
        (Device_Motor_SetCommand(&g_tracking.motor_b, command_b) ==
            DEVICE_MOTOR_RESULT_OK);
}

static void set_indicator(Device_RGB_Color color)
{
    /* The indicator is diagnostic and cannot create a motor-control fault. */
    (void)Device_RGB_StartColor(&g_tracking.rgb, color);
}

static void enter_fault(Task_AnalogTracking_Fault fault)
{
    (void)motor_pair_lock();
    (void)reset_controllers();
    g_tracking.fault = fault;
    g_tracking.status = TASK_ANALOG_TRACKING_FAULT;
    set_indicator(g_rgb_fault);
}

static bool imu_calibration_ready(void)
{
    return g_tracking.imu.feedback_valid &&
        g_tracking.imu.gyro_bias_valid &&
        (g_tracking.imu.calibration_status ==
            DEVICE_IMU_CALIBRATION_COMPLETE);
}

static bool update_encoders(float dt_seconds)
{
    const bool encoder_a_ok =
        Device_Encoder_Update(&g_tracking.encoder_a, dt_seconds) &&
        Device_Encoder_GetSample(
            &g_tracking.encoder_a, &g_tracking.encoder_a_sample);
    const bool encoder_b_ok =
        Device_Encoder_Update(&g_tracking.encoder_b, dt_seconds) &&
        Device_Encoder_GetSample(
            &g_tracking.encoder_b, &g_tracking.encoder_b_sample);

    if (!encoder_a_ok || !encoder_b_ok || !isfinite(dt_seconds) ||
        (dt_seconds <= 0.0F)) {
        return false;
    }
    g_tracking.measured_speed_a_mm_per_second =
        g_tracking.encoder_a_sample.speed_distance_per_second;
    g_tracking.measured_speed_b_mm_per_second =
        g_tracking.encoder_b_sample.speed_distance_per_second;
    return isfinite(g_tracking.measured_speed_a_mm_per_second) &&
        isfinite(g_tracking.measured_speed_b_mm_per_second);
}

static void refresh_track_usable(uint32_t now_ms)
{
    g_tracking.track_usable = g_tracking.has_snapshot &&
        Device_XinliuT8_SnapshotFresh(
            &g_tracking.snapshot, now_ms,
            TASK_SAMPLE_FRESH_TIMEOUT_MS) &&
        g_tracking.line_sample.line_present &&
        !g_tracking.line_sample.line_lost &&
        (g_tracking.line_sample.confidence >=
            TASK_ANALOG_MINIMUM_CONFIDENCE);
}

static bool process_snapshot(
    const Device_XinliuT8_Snapshot *snapshot, uint32_t now_ms)
{
    Device_LineTracking_RawSample raw = {
        .channel_count = TASK_ANALOG_TRACKING_CHANNEL_COUNT,
        .valid_mask = UINT32_C(0xFF),
    };

    if (snapshot == NULL) {
        return false;
    }
    for (size_t index = 0U;
         index < TASK_ANALOG_TRACKING_CHANNEL_COUNT;
         ++index) {
        raw.value[index] = snapshot->adc[index];
    }
    if (!Device_LineTracking_UpdateAnalogRaw(
            &g_tracking.line_tracking, &raw,
            snapshot->received_at_ms) ||
        !Device_LineTracking_GetSample(
            &g_tracking.line_tracking, &g_tracking.line_sample)) {
        return false;
    }
    g_tracking.snapshot = *snapshot;
    g_tracking.has_snapshot = true;
    refresh_track_usable(now_ms);
    return true;
}

static bool drain_sensor_uart(uint32_t now_ms)
{
    uint8_t bytes[TASK_RX_CHUNK_SIZE];

    for (size_t pass = 0U; pass < TASK_MAXIMUM_RX_PASSES; ++pass) {
        size_t transferred = 0U;
        const Device_Serial_Result read_result =
            Device_Serial_ReadAvailable(&g_tracking.sensor_serial,
                bytes, sizeof(bytes), &transferred);

        if (read_result != DEVICE_SERIAL_RESULT_OK) {
            increment_saturating(&g_tracking.uart_error_events);
            (void)Device_Serial_Recover(&g_tracking.sensor_serial);
            Device_XinliuT8_ResetStream(&g_tracking.xinliu);
            g_tracking.query_pending = false;
            return false;
        }
        for (size_t index = 0U; index < transferred; ++index) {
            const Device_XinliuT8_FeedResult feed_result =
                Device_XinliuT8_FeedByte(
                    &g_tracking.xinliu, now_ms, bytes[index]);

            if (feed_result == DEVICE_XINLIU_T8_FEED_SAMPLE_READY) {
                Device_XinliuT8_Snapshot snapshot;

                g_tracking.query_pending = false;
                if (!Device_XinliuT8_GetLatest(
                        &g_tracking.xinliu, &snapshot) ||
                    !process_snapshot(&snapshot, now_ms)) {
                    return false;
                }
            }
            else if (feed_result ==
                DEVICE_XINLIU_T8_FEED_FRAME_REJECTED) {
                g_tracking.query_pending = false;
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
        &g_tracking.xinliu, &g_tracking.protocol_stats);
}

static void service_sensor_uart_errors(void)
{
    Device_Serial_RxStatus status;

    if (!Device_Serial_GetRxStatus(&g_tracking.sensor_serial, &status)) {
        increment_saturating(&g_tracking.uart_error_events);
        return;
    }
    if (status.sticky_errors != DEVICE_SERIAL_RX_ERROR_NONE) {
        increment_saturating(&g_tracking.uart_error_events);
        (void)Device_Serial_ClearRxStatus(&g_tracking.sensor_serial);
    }
}

static void service_sensor_query(uint32_t now_ms)
{
    if (g_tracking.query_pending &&
        ((uint32_t)(now_ms - g_tracking.query_sent_at_ms) >
            TASK_QUERY_TIMEOUT_MS)) {
        increment_saturating(&g_tracking.query_timeouts);
        Device_XinliuT8_ResetStream(&g_tracking.xinliu);
        g_tracking.query_pending = false;
    }
    if (!g_tracking.query_pending) {
        const Device_Serial_Result result = Device_Serial_WriteBuffered(
            &g_tracking.sensor_serial,
            g_tracking.query, g_tracking.query_length);

        if (result == DEVICE_SERIAL_RESULT_OK) {
            g_tracking.query_sent_at_ms = now_ms;
            g_tracking.query_pending = true;
        }
        else if (result != DEVICE_SERIAL_RESULT_BUSY) {
            increment_saturating(&g_tracking.uart_error_events);
            (void)Device_Serial_Recover(&g_tracking.sensor_serial);
            Device_XinliuT8_ResetStream(&g_tracking.xinliu);
        }
    }
}

static bool update_line_tracking(uint32_t now_ms)
{
    if (!drain_sensor_uart(now_ms)) {
        g_tracking.track_usable = false;
        return false;
    }
    service_sensor_uart_errors();
    service_sensor_query(now_ms);
    refresh_track_usable(now_ms);
    return true;
}

static bool start_tracking(void)
{
    if (!reset_controllers()) {
        enter_fault(TASK_ANALOG_TRACKING_FAULT_CONTROLLER);
        return false;
    }
    /* Start from the current heading. The tracking PID updates this target
     * from the normalized difference-over-sum error while running. */
    g_tracking.target_yaw_deg = g_tracking.imu.yaw_deg;
    if (!motor_pair_unlock()) {
        enter_fault(TASK_ANALOG_TRACKING_FAULT_MOTOR);
        return false;
    }
    g_tracking.status = TASK_ANALOG_TRACKING_ACTIVE;
    set_indicator(g_rgb_enabled);
    return true;
}

static bool stop_tracking(void)
{
    if (!motor_pair_lock()) {
        enter_fault(TASK_ANALOG_TRACKING_FAULT_MOTOR);
        return false;
    }
    if (!reset_controllers()) {
        enter_fault(TASK_ANALOG_TRACKING_FAULT_CONTROLLER);
        return false;
    }
    g_tracking.status = TASK_ANALOG_TRACKING_READY;
    set_indicator(g_rgb_stopped);
    return true;
}

static int32_t effort_to_command(float effort_percent)
{
    float scaled;

    if (!isfinite(effort_percent)) {
        return 0;
    }
    scaled = effort_percent * 100.0F;
    if (scaled > (float)DEVICE_MOTOR_COMMAND_MAX) {
        scaled = (float)DEVICE_MOTOR_COMMAND_MAX;
    }
    if (scaled < (float)DEVICE_MOTOR_COMMAND_MIN) {
        scaled = (float)DEVICE_MOTOR_COMMAND_MIN;
    }
    return (scaled >= 0.0F) ?
        (int32_t)(scaled + 0.5F) : (int32_t)(scaled - 0.5F);
}

static float clamp_wheel_target(float target_mm_per_second)
{
    if (target_mm_per_second >
        TASK_MAXIMUM_WHEEL_TARGET_MM_PER_SECOND) {
        return TASK_MAXIMUM_WHEEL_TARGET_MM_PER_SECOND;
    }
    if (target_mm_per_second <
        -TASK_MAXIMUM_WHEEL_TARGET_MM_PER_SECOND) {
        return -TASK_MAXIMUM_WHEEL_TARGET_MM_PER_SECOND;
    }
    return target_mm_per_second;
}

static bool stop_closed_loop_output(void)
{
    Mode_Car_AngleController_Reset(&g_tracking.angle_controller);
    Mode_Car_SpeedController_Reset(&g_tracking.speed_controller);
    g_tracking.angle_output = (Mode_Car_AngleControllerOutput){0};
    g_tracking.speed_output = (Mode_Car_SpeedControllerOutput){0};
    g_tracking.target_yaw_deg = g_tracking.imu.yaw_deg;
    g_tracking.steering_error_deg = 0.0F;
    g_tracking.target_speed_a_mm_per_second = 0.0F;
    g_tracking.target_speed_b_mm_per_second = 0.0F;
    g_tracking.effort_a_percent = 0.0F;
    g_tracking.effort_b_percent = 0.0F;
    return apply_motor_commands(0, 0);
}

static bool update_closed_loop(uint32_t now_ms, float dt_seconds)
{
    Mode_Car_AnalogTrackingInput tracking_input;
    Mode_Car_AngleControllerInput angle_input;
    Mode_Car_SpeedControllerInput speed_input;
    float tracking_correction;

    tracking_input = (Mode_Car_AnalogTrackingInput){
        .time = {
            .now_ms = now_ms,
            .dt_seconds = dt_seconds,
        },
        .base_drive = 0.0F,
        .analog_error = g_tracking.line_sample.error,
        .confidence = g_tracking.line_sample.confidence,
        .line_valid = g_tracking.track_usable,
        .line_lost = g_tracking.line_sample.line_lost,
        .intersection = g_tracking.line_sample.intersection,
    };
    if (!Mode_Car_AnalogTracking_Update(
            &g_tracking.analog_mode,
            &tracking_input,
            &g_tracking.analog_output)) {
        return false;
    }
    if (!g_tracking.track_usable) {
        return stop_closed_loop_output();
    }
    tracking_correction = 0.5F *
        (g_tracking.analog_output.left_drive -
            g_tracking.analog_output.right_drive);
    /* Vehicle test confirmed that positive sensor error needs negative yaw:
     * line right -> A slower, B faster. Keep Device/Mode sign conventions
     * unchanged and apply the product mounting sign only at this boundary. */
    g_tracking.steering_error_deg = -tracking_correction *
        TASK_TRACKING_YAW_DEG_PER_CORRECTION;
    g_tracking.target_yaw_deg = g_tracking.imu.yaw_deg +
        g_tracking.steering_error_deg;

    angle_input = (Mode_Car_AngleControllerInput){
        .dt_seconds = dt_seconds,
        .target_yaw_deg = g_tracking.target_yaw_deg,
        .measured_yaw_deg = g_tracking.imu.yaw_deg,
        .measured_yaw_rate_deg_per_second =
            g_tracking.imu.yaw_rate_dps,
    };
    if (!Mode_Car_AngleController_Update(
            &g_tracking.angle_controller,
            &angle_input,
            &g_tracking.angle_output)) {
        return false;
    }
    /* The angle and wheel polarities are independently verified. */
    g_tracking.target_speed_a_mm_per_second = clamp_wheel_target(
        TASK_FORWARD_SPEED_MM_PER_SECOND +
            g_tracking.angle_output.turn_wheel_speed);
    g_tracking.target_speed_b_mm_per_second = clamp_wheel_target(
        TASK_FORWARD_SPEED_MM_PER_SECOND -
            g_tracking.angle_output.turn_wheel_speed);
    speed_input = (Mode_Car_SpeedControllerInput){
        .dt_seconds = dt_seconds,
        .target_left_speed =
            g_tracking.target_speed_a_mm_per_second,
        .target_right_speed =
            g_tracking.target_speed_b_mm_per_second,
        .measured_left_speed =
            g_tracking.measured_speed_a_mm_per_second,
        .measured_right_speed =
            g_tracking.measured_speed_b_mm_per_second,
    };
    if (!Mode_Car_SpeedController_Update(
            &g_tracking.speed_controller,
            &speed_input,
            &g_tracking.speed_output)) {
        return false;
    }
    g_tracking.effort_a_percent =
        g_tracking.speed_output.left_effort_percent;
    g_tracking.effort_b_percent =
        g_tracking.speed_output.right_effort_percent;
    return apply_motor_commands(
        effort_to_command(g_tracking.effort_a_percent),
        effort_to_command(g_tracking.effort_b_percent));
}

static bool append_char(char value, size_t *length)
{
    if ((length == NULL) || (*length >= TASK_TELEMETRY_BUFFER_SIZE)) {
        return false;
    }
    g_tracking.telemetry_buffer[*length] = value;
    ++(*length);
    return true;
}

static bool append_unsigned(uint64_t value, size_t *length)
{
    char reverse[20];
    size_t count = 0U;

    do {
        reverse[count] = (char)('0' + (value % UINT64_C(10)));
        value /= UINT64_C(10);
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

    if ((frame_length == NULL) ||
        !append_u32_field((uint32_t)g_tracking.status, &length) ||
        !append_float_field(
            g_tracking.target_speed_a_mm_per_second, &length) ||
        !append_float_field(
            g_tracking.measured_speed_a_mm_per_second, &length) ||
        !append_float_field(
            g_tracking.target_speed_b_mm_per_second, &length) ||
        !append_float_field(
            g_tracking.measured_speed_b_mm_per_second, &length)) {
        return false;
    }
    for (size_t index = 0U;
         index < TASK_ANALOG_TRACKING_CHANNEL_COUNT;
         ++index) {
        if (!append_u32_field(g_tracking.snapshot.adc[index], &length)) {
            return false;
        }
    }
    if (!append_float_field(g_tracking.imu.yaw_deg, &length) ||
        !append_char('\r', &length) || !append_char('\n', &length)) {
        return false;
    }
    *frame_length = length;
    return true;
}

static void publish_telemetry(void)
{
    size_t frame_length;

    ++g_tracking.telemetry_divider;
    if (g_tracking.telemetry_divider < TASK_TELEMETRY_DIVIDER) {
        return;
    }
    g_tracking.telemetry_divider = 0U;
    if (!build_telemetry_frame(&frame_length) ||
        (Device_Serial_WriteBuffered(
            &g_tracking.bluetooth,
            (const uint8_t *)g_tracking.telemetry_buffer,
            frame_length) != DEVICE_SERIAL_RESULT_OK)) {
        increment_saturating(&g_tracking.dropped_telemetry_frames);
    }
}

static void publish_debug_snapshot(void)
{
    for (size_t index = 0U;
         index < TASK_ANALOG_TRACKING_CHANNEL_COUNT;
         ++index) {
        g_TASK_AnalogTrackingRaw[index] = g_tracking.snapshot.adc[index];
    }
    g_TASK_AnalogTrackingDifferenceRatio =
        g_tracking.line_sample.difference_ratio;
    g_TASK_AnalogTrackingPosition = g_tracking.line_sample.position;
    g_TASK_AnalogTrackingError = g_tracking.line_sample.error;
    g_TASK_AnalogTrackingConfidence = g_tracking.line_sample.confidence;
    g_TASK_AnalogTrackingSteeringErrorDeg =
        g_tracking.steering_error_deg;
    g_TASK_AnalogTrackingTargetYawDeg = g_tracking.target_yaw_deg;
    g_TASK_AnalogTrackingMeasuredYawDeg = g_tracking.imu.yaw_deg;
    g_TASK_AnalogTrackingTargetSpeedA =
        g_tracking.target_speed_a_mm_per_second;
    g_TASK_AnalogTrackingMeasuredSpeedA =
        g_tracking.measured_speed_a_mm_per_second;
    g_TASK_AnalogTrackingEffortA = g_tracking.effort_a_percent;
    g_TASK_AnalogTrackingTargetSpeedB =
        g_tracking.target_speed_b_mm_per_second;
    g_TASK_AnalogTrackingMeasuredSpeedB =
        g_tracking.measured_speed_b_mm_per_second;
    g_TASK_AnalogTrackingEffortB = g_tracking.effort_b_percent;
    g_TASK_AnalogTrackingStatus = (uint32_t)g_tracking.status;
    g_TASK_AnalogTrackingFault = (uint32_t)g_tracking.fault;
    g_TASK_AnalogTrackingUsable = g_tracking.track_usable ? 1U : 0U;
    g_TASK_AnalogTrackingAcceptedFrames =
        g_tracking.protocol_stats.accepted_frames;
    g_TASK_AnalogTrackingRejectedFrames =
        g_tracking.protocol_stats.rejected_frames;
    g_TASK_AnalogTrackingQueryTimeouts = g_tracking.query_timeouts;
}

bool Task_AnalogTracking_Init(void)
{
    Device_IMU_Config imu_config = Device_IMU_DefaultConfig();
    Device_Motor_Config motor_a_config;
    Device_Motor_Config motor_b_config;
    Device_Encoder_Config encoder_a_config;
    Device_Encoder_Config encoder_b_config;
    Device_RGB_Config rgb_config;
    Device_LineTracking_Config line_config;
    Mode_Car_AngleControllerConfig angle_config =
        Mode_Car_AngleController_DefaultConfig();
    Mode_Car_SpeedControllerConfig speed_config =
        Mode_Car_SpeedController_DefaultConfig();
    const Device_ICM45686_SPI_Config icm45686_config = {
        .transport_index = TASK_ICM45686_SPI_INDEX,
        .chip_select_index = TASK_ICM45686_CS_INDEX,
        .expected_bit_rate_hz = TASK_SPI_BIT_RATE_HZ,
        .transfer_timeout_ms = TASK_SPI_TIMEOUT_MS,
        .startup_delay_ms = TASK_IMU_STARTUP_DELAY_MS,
    };
    const Device_KeyInput_Config key_config = {
        .key = {
            .debounce_ms = TASK_KEY_DEBOUNCE_MS,
            .long_press_ms = TASK_KEY_LONG_PRESS_MS,
            .double_click_ms = TASK_KEY_DOUBLE_CLICK_MS,
        },
        .input_index = TASK_KEY_INPUT_INDEX,
    };
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
    const bool motor_configs_valid =
        Device_Motor_DefaultConfig(TASK_MOTOR_A_INDEX, &motor_a_config) &&
        Device_Motor_DefaultConfig(TASK_MOTOR_B_INDEX, &motor_b_config);
    const bool encoder_configs_valid =
        Device_Encoder_MakeConfig(
            TASK_ENCODER_A_INDEX,
            TASK_ENCODER_COUNTS_PER_WHEEL_REVOLUTION,
            TASK_WHEEL_CIRCUMFERENCE_MM,
            TASK_ENCODER_A_DIRECTION_SIGN,
            &encoder_a_config) &&
        Device_Encoder_MakeConfig(
            TASK_ENCODER_B_INDEX,
            TASK_ENCODER_COUNTS_PER_WHEEL_REVOLUTION,
            TASK_WHEEL_CIRCUMFERENCE_MM,
            TASK_ENCODER_B_DIRECTION_SIGN,
            &encoder_b_config);

    if (motor_configs_valid) {
        motor_a_config.zero_mode = BSP_MOTOR_MODE_COAST;
        motor_b_config.zero_mode = BSP_MOTOR_MODE_COAST;
        motor_a_config.positive_is_forward =
            TASK_MOTOR_A_POSITIVE_IS_FORWARD;
        motor_b_config.positive_is_forward =
            TASK_MOTOR_B_POSITIVE_IS_FORWARD;
    }

    imu_config.start_calibration_on_init = true;
    imu_config.calibration_required_samples =
        TASK_IMU_CALIBRATION_REQUIRED_SAMPLES;
    imu_config.offline_timeout_ms = TASK_IMU_OFFLINE_TIMEOUT_MS;

    angle_config.kp = TASK_YAW_KP;
    angle_config.ki = TASK_YAW_KI;
    angle_config.kd = TASK_YAW_KD;
    angle_config.integral_speed_limit =
        TASK_YAW_INTEGRAL_SPEED_LIMIT;
    angle_config.maximum_wheel_speed =
        TASK_YAW_CONTROLLER_OUTPUT_LIMIT_MM_PER_SECOND;

    speed_config.low_speed = (Mode_Car_SpeedBandConfig){
        .kp = TASK_SPEED_P_KP,
        .ki = TASK_SPEED_I_KI,
        .kd = 0.0F,
    };
    speed_config.high_speed = speed_config.low_speed;
    speed_config.feedforward_percent_per_speed =
        TASK_SPEED_FEEDFORWARD_PERCENT_PER_SPEED;
    speed_config.target_smoothing_time_constant_seconds =
        TASK_SPEED_TARGET_SMOOTHING_SECONDS;
    speed_config.high_speed_threshold =
        TASK_FORWARD_SPEED_MM_PER_SECOND;
    speed_config.maximum_target_speed =
        TASK_MAXIMUM_WHEEL_TARGET_MM_PER_SECOND;
    speed_config.maximum_effort_percent =
        TASK_SPEED_MAXIMUM_EFFORT_PERCENT;

    g_tracking = (Task_AnalogTracking_State){0};
    g_tracking.status = TASK_ANALOG_TRACKING_CALIBRATING;
    g_tracking.imu_status = DEVICE_ICM45686_STATUS_NOT_INITIALIZED;
    g_tracking.analog_config = Mode_Car_AnalogTracking_DefaultConfig();
    g_tracking.analog_config.minimum_confidence =
        TASK_ANALOG_MINIMUM_CONFIDENCE;
    g_tracking.analog_config.steering.kp = TASK_TRACKING_KP;
    g_tracking.analog_config.steering.ki = TASK_TRACKING_KI;
    g_tracking.analog_config.steering.kd = TASK_TRACKING_KD;
    g_tracking.analog_config.steering.integral_output_limit =
        TASK_TRACKING_INTEGRAL_OUTPUT_LIMIT;
    g_tracking.analog_config.steering.maximum_correction =
        TASK_TRACKING_MAXIMUM_CORRECTION;
    if (!Device_XinliuT8_Init(&g_tracking.xinliu) ||
        !Device_XinliuT8_BuildReadAnalogQuery(g_tracking.query,
            sizeof(g_tracking.query), &g_tracking.query_length) ||
        !Device_LineTracking_DefaultRawConfig(
            TASK_ANALOG_TRACKING_CHANNEL_COUNT,
            TASK_ANALOG_CALIBRATION_MIN,
            TASK_ANALOG_CALIBRATION_MAX,
            &line_config)) {
        return false;
    }
    line_config.minimum_strength_sum = 0.0F;
    line_config.estimator.difference_weight = 1.0F;
    for (size_t index = 0U;
         index < TASK_ANALOG_TRACKING_CHANNEL_COUNT;
         ++index) {
        line_config.channel[index].calibration_min =
            g_channel_calibration_min[index];
        line_config.channel[index].calibration_max =
            g_channel_calibration_max[index];
        line_config.channel[index].line_is_high = true;
        line_config.channel[index].position_weight =
            g_channel_position_weight[index];
    }

    if (!motor_configs_valid || !encoder_configs_valid ||
        !Device_RGB_DefaultConfig(TASK_RGB_INDEX, &rgb_config) ||
        !Device_RGB_Init(&g_tracking.rgb, &rgb_config) ||
        !Device_Motor_Init(&g_tracking.motor_a, &motor_a_config) ||
        !Device_Motor_Init(&g_tracking.motor_b, &motor_b_config) ||
        !Device_KeyInput_Init(&g_tracking.key, &key_config, 0U) ||
        !Device_ICM45686_SPI_Init(
            &g_tracking.icm45686, &icm45686_config, &imu_config) ||
        !Device_Encoder_Init(
            &g_tracking.encoder_a, &encoder_a_config) ||
        !Device_Encoder_Init(
            &g_tracking.encoder_b, &encoder_b_config) ||
        !Device_Serial_Init(
            &g_tracking.sensor_serial, &sensor_serial_config) ||
        !Device_Serial_Init(&g_tracking.bluetooth, &bluetooth_config) ||
        !Device_LineTracking_Init(
            &g_tracking.line_tracking, &line_config) ||
        !Mode_Communicate_BluetoothPC_Init(
            &g_tracking.bluetooth_mode) ||
        !Mode_Communicate_BluetoothPC_Update(
            &g_tracking.bluetooth_mode, &g_tracking.bluetooth_output) ||
        (g_tracking.bluetooth_output.mode != BLUETOOTH_MODE_PC) ||
        (g_tracking.bluetooth_output.format !=
            MODE_COMMUNICATE_BLUETOOTH_FORMAT_RAW_STREAM) ||
        !Mode_Car_AnalogTracking_Init(
            &g_tracking.analog_mode, &g_tracking.analog_config) ||
        !Mode_Car_AngleController_Init(
            &g_tracking.angle_controller, &angle_config) ||
        !Mode_Car_SpeedController_Init(
            &g_tracking.speed_controller, &speed_config) ||
        !motor_pair_lock() ||
        (Device_RGB_SetColor(
            &g_tracking.rgb, g_rgb_calibrating) !=
            DEVICE_RGB_RESULT_OK)) {
        (void)motor_pair_lock();
        (void)Device_RGB_SetColor(&g_tracking.rgb, g_rgb_fault);
        return false;
    }
    g_tracking.imu = g_tracking.icm45686.imu.snapshot;
    g_tracking.initialized = true;
    publish_debug_snapshot();
    return true;
}

void Task_AnalogTracking_Update(uint32_t now_ms)
{
    float dt_seconds = TASK_SAMPLE_DT_SECONDS;
    bool pressed_edge = false;
    bool imu_ok;
    bool encoder_ok;

    if (!g_tracking.initialized) {
        return;
    }
    if (g_tracking.has_sample_timestamp) {
        const uint32_t elapsed_ms =
            now_ms - g_tracking.last_sampled_at_ms;

        if (elapsed_ms != 0U) {
            dt_seconds = (float)elapsed_ms * 0.001F;
        }
    }
    g_tracking.last_sampled_at_ms = now_ms;
    g_tracking.has_sample_timestamp = true;

    /* T8 data drives the outer tracking PID after the key enables control. */
    (void)update_line_tracking(now_ms);
    g_tracking.imu_status = Device_ICM45686_SPI_Update(
        &g_tracking.icm45686,
        now_ms,
        dt_seconds,
        &g_tracking.icm45686_sample,
        &g_tracking.imu);
    imu_ok = (g_tracking.imu_status == DEVICE_ICM45686_STATUS_OK) &&
        g_tracking.imu.feedback_valid;
    encoder_ok = update_encoders(dt_seconds);
    if (!Device_KeyInput_UpdatePressedEdge(
            &g_tracking.key, now_ms, &pressed_edge)) {
        enter_fault(TASK_ANALOG_TRACKING_FAULT_KEY);
    }

    if (!isfinite(dt_seconds) || (dt_seconds <= 0.0F) ||
        (dt_seconds > TASK_MAXIMUM_DT_SECONDS)) {
        if (g_tracking.motors_unlocked) {
            enter_fault(TASK_ANALOG_TRACKING_FAULT_TIMING);
        }
    }
    else if (g_tracking.motors_unlocked && !imu_ok) {
        enter_fault(TASK_ANALOG_TRACKING_FAULT_IMU);
    }
    else if (g_tracking.motors_unlocked && !encoder_ok) {
        enter_fault(TASK_ANALOG_TRACKING_FAULT_ENCODER);
    }
    if ((g_tracking.status == TASK_ANALOG_TRACKING_CALIBRATING) &&
        imu_calibration_ready()) {
        g_tracking.status = TASK_ANALOG_TRACKING_READY;
        set_indicator(g_rgb_stopped);
    }

    switch (g_tracking.status) {
    case TASK_ANALOG_TRACKING_READY:
        if (pressed_edge && imu_ok && encoder_ok &&
            g_tracking.track_usable) {
            (void)start_tracking();
        }
        break;
    case TASK_ANALOG_TRACKING_ACTIVE:
        if (pressed_edge) {
            (void)stop_tracking();
        }
        else if (imu_ok && encoder_ok &&
            !update_closed_loop(now_ms, dt_seconds)) {
                enter_fault(TASK_ANALOG_TRACKING_FAULT_CONTROLLER);
        }
        break;
    case TASK_ANALOG_TRACKING_CALIBRATING:
    case TASK_ANALOG_TRACKING_FAULT:
    default:
        break;
    }

    publish_telemetry();
    publish_debug_snapshot();
}

bool Task_AnalogTracking_OutputEnabled(void)
{
    return g_tracking.initialized && g_tracking.motors_unlocked &&
        (g_tracking.status == TASK_ANALOG_TRACKING_ACTIVE);
}

bool Task_AnalogTracking_ControlFaulted(void)
{
    return g_tracking.status == TASK_ANALOG_TRACKING_FAULT;
}
