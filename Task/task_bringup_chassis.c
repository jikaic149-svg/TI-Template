#include "task_bringup_chassis.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "core_config.h"
#include "device_encoder.h"
#include "device_icm45686_spi.h"
#include "device_jy61p.h"
#include "device_key_input.h"
#include "device_line_tracking.h"
#include "device_motor.h"
#include "MG513X/device_motor_mg513x_gmr.h"
#include "device_peer_link.h"
#include "device_rgb.h"
#include "device_serial.h"
#include "device_st7735s.h"
#include "device_yahboom_track.h"
#include "mode_car.h"
#include "mode_car_control.h"
#include "mode_car_fused_navigation.h"
#include "mode_communicate.h"

#define BRINGUP_TASK_NAME "Bringup"
#define BRINGUP_TASK_PERIOD_MS UINT32_C(10)
#define BRINGUP_TASK_STACK_DEPTH 512U
#define BRINGUP_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)

/* M2 continues pure grayscale tracking for this fixed distance after the
 * end-marker is confirmed, then completes the existing linear planned stop. */
#define BRINGUP_ITEM2_AFTER_MARKER_DISTANCE_MM 180.0F

/* BSP UART resource order on G3519: 0 debug, 1 aux (PB0/PB1),
 * 2 Bluetooth (PB10/PB11), 3 line-tracking (PC6 TX/PC7 RX). */
#define BRINGUP_BLUETOOTH_TRANSPORT_INDEX 2U
#define BRINGUP_GRAYSCALE_TRANSPORT_INDEX 3U
#define BRINGUP_PEER_LINK_TRANSPORT_INDEX 0U
#define BRINGUP_PENDULUM_IMU_TRANSPORT_INDEX 1U
#define BRINGUP_UART_BAUD UINT32_C(115200)

/* BSP SPI resource order on G3519: 0 TFT, 1 ICM-45686 (CS index 0). */
#define BRINGUP_TFT_DISPLAY_INDEX 0U
#define BRINGUP_ICM45686_SPI_INDEX 1U
#define BRINGUP_ICM45686_SPI_BAUD UINT32_C(10000000)

#define BRINGUP_MOTOR_COUNT 2U

/* MG513X drivetrain: 500-line GMR, x4 QEI decode, 28:1 gearbox. The wheel
 * circumference is the user-confirmed nominal value; the effective rolling
 * circumference still needs the push-1-m calibration. */
#define BRINGUP_COUNTS_PER_WHEEL_REV \
    ((float)DEVICE_MOTOR_MG513X_GMR_ENCODER_LINES * 4.0F * \
        DEVICE_MOTOR_MG513X_GEAR_RATIO)
#define BRINGUP_WHEEL_CIRCUMFERENCE_MM 204.20F

/* Product-confirmed Yahboom channel order is x1..x8 from left to right. The two
 * center channels share zero weight because they straddle the car center. */
static const float g_line_position_weight[
    DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT] = {
    -TEMPLATE_BRINGUP_LINE_OUTER_WEIGHT,
    -TEMPLATE_BRINGUP_LINE_MIDDLE_WEIGHT,
    -TEMPLATE_BRINGUP_LINE_INNER_WEIGHT,
    0.0F,
    0.0F,
    TEMPLATE_BRINGUP_LINE_INNER_WEIGHT,
    TEMPLATE_BRINGUP_LINE_MIDDLE_WEIGHT,
    TEMPLATE_BRINGUP_LINE_OUTER_WEIGHT,
};

/* VOFA+ FireWater: one ASCII line per frame, comma-separated values ending
 * with \n. The 32-channel debug frame uses integer-only formatting except for
 * fixed-point angles and line error, keeping newlib-nano printf small. */
#define BRINGUP_TELEMETRY_LINE_CAPACITY 320U
#define BRINGUP_F407_DEBUG_MAX_AGE_MS UINT32_C(100)
#define BRINGUP_F407_DEBUG_FRESH_FLAG UINT16_C(0x0200)
#define BRINGUP_PENDULUM_IMU_READ_CAPACITY 128U
#define BRINGUP_PENDULUM_IMU_INVALID_AGE_MS UINT32_C(9999)

#define BRINGUP_DISPLAY_WIDTH 128U
#define BRINGUP_DISPLAY_LINE_HEIGHT 16U
#define BRINGUP_DISPLAY_CHARACTER_WIDTH 8U
#define BRINGUP_DISPLAY_CHARACTER_COUNT \
    (BRINGUP_DISPLAY_WIDTH / BRINGUP_DISPLAY_CHARACTER_WIDTH)
#define BRINGUP_DISPLAY_STATUS_LINE_COUNT 4U

#define BRINGUP_DISPLAY_COLOR_BLACK UINT16_C(0x0000)
#define BRINGUP_DISPLAY_COLOR_RED UINT16_C(0xF800)
#define BRINGUP_DISPLAY_COLOR_GREEN UINT16_C(0x07E0)
#define BRINGUP_DISPLAY_COLOR_YELLOW UINT16_C(0xFFE0)
#define BRINGUP_DISPLAY_COLOR_CYAN UINT16_C(0x07FF)

typedef enum {
    BRINGUP_COMPETITION_MODE_ITEM_2 = 2,
    BRINGUP_COMPETITION_MODE_ITEM_3,
    BRINGUP_COMPETITION_MODE_ITEM_4,
    BRINGUP_COMPETITION_MODE_ITEM_5,
    BRINGUP_COMPETITION_MODE_ITEM_6,
} Bringup_CompetitionMode;

/* Compact Task-owned 5x7 uppercase font. Each row uses bits 4..0 from left
 * to right; the renderer doubles rows into an 8x16 status cell. */
static const uint8_t g_bringup_font_5x7['Z' - ' ' + 1U][7] = {
    ['+' - ' '] = {0x00U, 0x04U, 0x04U, 0x1FU, 0x04U, 0x04U, 0x00U},
    ['-' - ' '] = {0x00U, 0x00U, 0x00U, 0x1FU, 0x00U, 0x00U, 0x00U},
    ['.' - ' '] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x0CU},
    ['/' - ' '] = {0x01U, 0x02U, 0x02U, 0x04U, 0x08U, 0x08U, 0x10U},
    ['0' - ' '] = {0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU},
    ['1' - ' '] = {0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU},
    ['2' - ' '] = {0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU},
    ['3' - ' '] = {0x1EU, 0x01U, 0x01U, 0x0EU, 0x01U, 0x01U, 0x1EU},
    ['4' - ' '] = {0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U},
    ['5' - ' '] = {0x1FU, 0x10U, 0x10U, 0x1EU, 0x01U, 0x01U, 0x1EU},
    ['6' - ' '] = {0x0EU, 0x10U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU},
    ['7' - ' '] = {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U},
    ['8' - ' '] = {0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU},
    ['9' - ' '] = {0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x01U, 0x0EU},
    [':' - ' '] = {0x00U, 0x0CU, 0x0CU, 0x00U, 0x0CU, 0x0CU, 0x00U},
    ['A' - ' '] = {0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U},
    ['B' - ' '] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU},
    ['C' - ' '] = {0x0EU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0EU},
    ['D' - ' '] = {0x1EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1EU},
    ['E' - ' '] = {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x1FU},
    ['F' - ' '] = {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U},
    ['G' - ' '] = {0x0EU, 0x11U, 0x10U, 0x17U, 0x11U, 0x11U, 0x0FU},
    ['H' - ' '] = {0x11U, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U},
    ['I' - ' '] = {0x0EU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU},
    ['J' - ' '] = {0x07U, 0x02U, 0x02U, 0x02U, 0x12U, 0x12U, 0x0CU},
    ['K' - ' '] = {0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U},
    ['L' - ' '] = {0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1FU},
    ['M' - ' '] = {0x11U, 0x1BU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U},
    ['N' - ' '] = {0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U, 0x11U},
    ['O' - ' '] = {0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU},
    ['P' - ' '] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U},
    ['Q' - ' '] = {0x0EU, 0x11U, 0x11U, 0x11U, 0x15U, 0x12U, 0x0DU},
    ['R' - ' '] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U},
    ['S' - ' '] = {0x0FU, 0x10U, 0x10U, 0x0EU, 0x01U, 0x01U, 0x1EU},
    ['T' - ' '] = {0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U},
    ['U' - ' '] = {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU},
    ['V' - ' '] = {0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x0AU, 0x04U},
    ['W' - ' '] = {0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x1BU, 0x11U},
    ['X' - ' '] = {0x11U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U, 0x11U},
    ['Y' - ' '] = {0x11U, 0x11U, 0x0AU, 0x04U, 0x04U, 0x04U, 0x04U},
    ['Z' - ' '] = {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x1FU},
};

volatile uint32_t g_BRINGUP_Fault = 0U;
volatile uint32_t g_BRINGUP_DisplayFault = 0U;

static StaticTask_t g_bringup_tcb;
static StackType_t g_bringup_stack[BRINGUP_TASK_STACK_DEPTH];

static Device_Motor g_motor[BRINGUP_MOTOR_COUNT];
static Device_Encoder g_encoder[BRINGUP_MOTOR_COUNT];
static Device_KeyInput g_start_key;
static Device_RGB g_rgb;
static Device_Serial g_bluetooth;
static Device_Serial g_grayscale_serial;
static Device_Serial g_pendulum_imu_serial;
static Device_YahboomTrack g_grayscale;
static Device_JY61P g_pendulum_imu;
static Device_LineTracking g_line_tracking;
static Device_ST7735S g_display;
static Device_ICM45686_SPI g_imu;
static Mode_Communicate_BluetoothPCContext g_bluetooth_mode;
static Mode_Car_SpeedController g_speed_controller;
static Mode_Car_AngleController g_angle_controller;
static Mode_Car_LinearSpeedProfile g_speed_profile;
static Mode_Car_AnalogTrackingContext g_tracking_controller;
static Mode_Car_FusedNavigation g_navigation;
static Mode_Car_FusedNavigationOutput g_navigation_output;
static Device_PeerLink g_peer_link;
static uint16_t g_display_line_pixels[
    BRINGUP_DISPLAY_WIDTH * BRINGUP_DISPLAY_LINE_HEIGHT];
static char g_display_last_text[BRINGUP_DISPLAY_STATUS_LINE_COUNT]
    [BRINGUP_DISPLAY_CHARACTER_COUNT + 1U];
static uint16_t
    g_display_last_foreground[BRINGUP_DISPLAY_STATUS_LINE_COUNT];

static bool g_devices_ready = false;
static bool g_peer_link_ready = false;
/* 0 none, 1 init, 2 periodic step, 3 CAR send. */
static volatile uint32_t g_peer_link_fault = 0U;
static bool g_tracking_active = false;
static bool g_pendulum_imu_ready = false;
static bool g_grayscale_start_command_attempted = false;
static bool g_grayscale_reporting_requested = false;
static uint32_t g_grayscale_initialized_ms = 0U;
static bool g_stop_requested = false;
static bool g_navigation_pose_valid = false;
static bool g_navigation_update_valid = false;
static bool g_profile_zero_candidate = false;
static bool g_finish_marker_latched = false;
static bool g_finish_marker_window_armed = false;
static uint32_t g_finish_marker_confirm_count = 0U;
static float g_finish_marker_distance_mm = 0.0F;
static uint32_t g_profile_zero_since_ms = 0U;
static float g_navigation_start_x_mm = 0.0F;
static float g_navigation_start_y_mm = 0.0F;
static float g_navigation_start_yaw_deg = 0.0F;
static size_t g_display_status_line = 0U;
static Device_LineTracking_Sample g_line_sample;
static bool g_line_sample_available = false;
static uint32_t g_line_sample_sequence = 0U;
static uint32_t g_processed_grayscale_sequence = 0U;
static uint32_t g_previous_line_sample_ms = 0U;
static bool g_has_previous_line_sample_time = false;
static uint32_t g_last_valid_line_sample_ms = 0U;
static bool g_has_last_valid_line_sample = false;
static float g_target_left_speed = 0.0F;
static float g_target_right_speed = 0.0F;
static float g_line_left_speed_correction = 0.0F;
static float g_line_right_speed_correction = 0.0F;
static bool g_line_speed_correction_valid = false;
static float g_item56_line_half_difference_mm_per_second = 0.0F;
static uint32_t g_tracking_started_ms = 0U;
static uint32_t g_inspection_elapsed_ms = 0U;
static uint32_t g_score_elapsed_ms = 0U;
static bool g_score_time_latched = false;
static float g_route_start_distance_mm = 0.0F;
static Bringup_CompetitionMode g_competition_mode =
    BRINGUP_COMPETITION_MODE_ITEM_2;
static bool g_competition_mode_confirmed = false;

static uint32_t bringup_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static Mode_Car_AnalogTrackingConfig bringup_tracking_config(void)
{
    Mode_Car_AnalogTrackingConfig config =
        Mode_Car_AnalogTracking_DefaultConfig();

    config.steering.kp = TEMPLATE_BRINGUP_LINE_STEERING_KP;
    if ((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6)) {
        config.steering.kd = TEMPLATE_BRINGUP_ITEM56_LINE_STEERING_KD;
    }
    return config;
}

static Mode_Car_SpeedControllerConfig bringup_speed_controller_config(void)
{
    Mode_Car_SpeedControllerConfig config =
        Mode_Car_SpeedController_DefaultConfig();

    /* The outer profile is the only target slew limiter. */
    config.target_smoothing_time_constant_seconds = 0.0F;
    return config;
}

static Mode_Car_LinearProfileConfig bringup_linear_profile_config(void)
{
    Mode_Car_LinearProfileConfig config =
        Mode_Car_LinearProfile_DefaultConfig();

    config.maximum_speed_mm_per_second =
        TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND;
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_4) {
        config.acceleration_up_mm_per_second2 =
            TEMPLATE_BRINGUP_ITEM4_AB_ACCELERATION_MM_PER_SECOND2;
        config.acceleration_down_mm_per_second2 =
            TEMPLATE_BRINGUP_ITEM4_AB_DECELERATION_MM_PER_SECOND2;
    } else if ((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6)) {
        config.acceleration_up_mm_per_second2 =
            TEMPLATE_BRINGUP_ITEM56_LAP_ACCELERATION_MM_PER_SECOND2;
        config.acceleration_down_mm_per_second2 =
            TEMPLATE_BRINGUP_ITEM56_LAP_DECELERATION_MM_PER_SECOND2;
    }
    return config;
}

static float bringup_launch_cruise_speed_mm_per_second(void)
{
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_4) {
        return TEMPLATE_BRINGUP_ITEM4_AB_CRUISE_SPEED_MM_PER_SECOND;
    }
    if ((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6)) {
        return TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND;
    }
    return TEMPLATE_BRINGUP_LINE_BASE_SPEED_MM_PER_SECOND;
}

static Mode_Car_FusedNavigationConfig bringup_navigation_config(void)
{
    Mode_Car_FusedNavigationConfig config =
        Mode_Car_FusedNavigation_DefaultConfig();

    config.speed_controller.target_smoothing_time_constant_seconds = 0.0F;
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) {
        config.yaw_rate_controller.maximum_drive =
            TEMPLATE_BRINGUP_ITEM2_MAXIMUM_HALF_DIFFERENCE_MM_PER_SECOND;
    }
    return config;
}

static Bringup_CompetitionMode bringup_next_competition_mode(
    Bringup_CompetitionMode mode)
{
    if ((mode < BRINGUP_COMPETITION_MODE_ITEM_2) ||
        (mode >= BRINGUP_COMPETITION_MODE_ITEM_6)) {
        return BRINGUP_COMPETITION_MODE_ITEM_2;
    }
    return (Bringup_CompetitionMode)((unsigned int)mode + 1U);
}

static float bringup_competition_goal_distance_mm(void)
{
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_4) {
        return TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM +
            TEMPLATE_BRINGUP_ITEM4_PASS_B_RUNOUT_MM;
    }
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) {
        /* M2 completion is based only on one lap of mean wheel odometry. */
        return TEMPLATE_BRINGUP_ROUTE_GOAL_DISTANCE_MM;
    }
    if ((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6)) {
        return TEMPLATE_BRINGUP_ROUTE_GOAL_DISTANCE_MM +
            TEMPLATE_BRINGUP_CAR_LENGTH_MM;
    }
    return 0.0F;
}

/* Keep the steering target ahead through A. M2 stops at one-lap odometry but
 * needs one lookahead length of route so the final arc is not pinned to A. */
static float bringup_route_terminal_distance_mm(void)
{
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_4) {
        return TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM +
            TEMPLATE_BRINGUP_ITEM4_PASS_B_RUNOUT_MM;
    }
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) {
        return TEMPLATE_BRINGUP_ROUTE_GOAL_DISTANCE_MM +
            TEMPLATE_BRINGUP_INS_LOOKAHEAD_MM;
    }
    if ((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6)) {
        return TEMPLATE_BRINGUP_ROUTE_GOAL_DISTANCE_MM +
            TEMPLATE_BRINGUP_CAR_LENGTH_MM;
    }
    return 0.0F;
}

static float bringup_mean_distance(
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b)
{
    return 0.5F * (speed_a->cumulative_distance +
        speed_b->cumulative_distance);
}

static float bringup_mean_speed(
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b)
{
    return 0.5F * (speed_a->speed_distance_per_second +
        speed_b->speed_distance_per_second);
}

static float bringup_item56_slew_line_half_difference(
    float desired_half_difference_mm_per_second,
    float dt_seconds)
{
    const float maximum_half_difference =
        TEMPLATE_BRINGUP_ITEM56_MAXIMUM_HALF_DIFFERENCE_MM_PER_SECOND;
    float maximum_change;
    float desired;
    float change;

    if (!isfinite(desired_half_difference_mm_per_second) ||
        !isfinite(dt_seconds) || (dt_seconds <= 0.0F)) {
        return g_item56_line_half_difference_mm_per_second;
    }
    desired = fmaxf(-maximum_half_difference,
        fminf(maximum_half_difference,
            desired_half_difference_mm_per_second));
    maximum_change =
        TEMPLATE_BRINGUP_ITEM56_HALF_DIFFERENCE_SLEW_MM_PER_SECOND2 *
            dt_seconds;
    change = fmaxf(-maximum_change,
        fminf(maximum_change,
            desired - g_item56_line_half_difference_mm_per_second));
    g_item56_line_half_difference_mm_per_second += change;
    return g_item56_line_half_difference_mm_per_second;
}

static bool bringup_scale_i32(
    float value, float scale, int32_t maximum, int32_t *output)
{
    float scaled;

    if ((output == NULL) || !isfinite(value) || !isfinite(scale) ||
        (scale == 0.0F) || (maximum <= 0)) {
        return false;
    }
    scaled = value * scale;
    if (!isfinite(scaled) || (scaled < (float)-maximum) ||
        (scaled > (float)maximum)) {
        return false;
    }
    /* C integer conversion truncates toward zero. Offset by half a unit for
     * symmetric nearest-integer quantization on both signs. */
    *output = (int32_t)(scaled + ((scaled >= 0.0F) ? 0.5F : -0.5F));
    return true;
}

static Device_PeerLink_CarState bringup_make_car_state(
    uint32_t now_ms,
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b,
    bool encoder_samples_valid,
    const Device_IMU_Snapshot *imu_snapshot,
    bool imu_navigation_valid)
{
    Device_PeerLink_CarState state = {
        .sample_ms = now_ms,
    };

    if ((speed_a != NULL) && (speed_b != NULL) &&
        (imu_snapshot != NULL) && encoder_samples_valid &&
        imu_navigation_valid &&
        bringup_scale_i32(
            bringup_mean_speed(speed_a, speed_b), 1.0F,
            DEVICE_PEER_LINK_CAR_SPEED_ABS_MAX_MM_PER_SECOND,
            &state.speed_x_mm_per_second) &&
        bringup_scale_i32(
            imu_snapshot->acceleration_mps2[0], 1000.0F,
            DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2,
            &state.acceleration_x_mm_per_second2) &&
        bringup_scale_i32(
            imu_snapshot->acceleration_mps2[2], 1000.0F,
            DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2,
            &state.acceleration_z_mm_per_second2) &&
        bringup_scale_i32(
            imu_snapshot->pitch_rate_dps, 100.0F,
            DEVICE_PEER_LINK_CAR_PITCH_RATE_ABS_MAX_CENTI_DPS,
            &state.pitch_rate_centi_dps)) {
        state.valid = true;
    }
    return state;
}

/* The ordinary analog line estimator deliberately has no per-channel
 * threshold. This separate product-level detector uses the raw common-mode
 * rise of a transverse black marker and does not change lateral line error. */
static bool bringup_finish_marker_candidate(
    const Device_YahboomTrack_Snapshot *snapshot)
{
    size_t black_channel_count = 0U;
    uint32_t adc_sum = 0U;

    if (snapshot == NULL) {
        return false;
    }
    for (size_t index = 0U;
         index < DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT;
         ++index) {
        adc_sum += snapshot->adc[index];
        if (snapshot->adc[index] >=
            TEMPLATE_BRINGUP_FINISH_MARKER_ADC_THRESHOLD) {
            ++black_channel_count;
        }
    }
    return (black_channel_count >=
            TEMPLATE_BRINGUP_FINISH_MARKER_MINIMUM_CHANNELS) &&
        (adc_sum >=
            (uint32_t)TEMPLATE_BRINGUP_FINISH_MARKER_ADC_AVERAGE_THRESHOLD *
                DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT);
}

static bool bringup_update_finish_marker(
    const Device_YahboomTrack_Snapshot *snapshot,
    float current_distance_mm)
{
    const float route_distance_mm =
        current_distance_mm - g_route_start_distance_mm;
    const float expected_marker_distance_mm =
        TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_CENTER_MM;
    const bool candidate = bringup_finish_marker_candidate(snapshot);
    const float marker_runout_mm =
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) ?
        BRINGUP_ITEM2_AFTER_MARKER_DISTANCE_MM :
        (((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
            (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6)) ?
        TEMPLATE_BRINGUP_ITEM56_AFTER_MARKER_GOAL_RUNOUT_MM :
        TEMPLATE_BRINGUP_FINISH_MARKER_STOP_RUNOUT_MM);

    if (g_finish_marker_latched) {
        return true;
    }
    if (!isfinite(route_distance_mm) ||
        !isfinite(expected_marker_distance_mm) ||
        (expected_marker_distance_mm < 0.0F) ||
        (fabsf(route_distance_mm - expected_marker_distance_mm) >
            TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_HALF_WIDTH_MM)) {
        g_finish_marker_window_armed = false;
        g_finish_marker_confirm_count = 0U;
        return true;
    }
    if (!candidate) {
        /* The map has an ordinary curved line before the A-point transverse
         * marker. Seeing that normal pattern inside the bounded window keeps a
         * candidate already present at window entry from latching immediately. */
        g_finish_marker_window_armed = true;
        g_finish_marker_confirm_count = 0U;
        return true;
    }
    if (!g_finish_marker_window_armed) {
        g_finish_marker_confirm_count = 0U;
        return true;
    }
    if (g_finish_marker_confirm_count <
        TEMPLATE_BRINGUP_FINISH_MARKER_CONFIRM_SAMPLES) {
        ++g_finish_marker_confirm_count;
    }
    if (g_finish_marker_confirm_count <
        TEMPLATE_BRINGUP_FINISH_MARKER_CONFIRM_SAMPLES) {
        return true;
    }
    if (!Mode_Car_LinearSpeedProfile_SetGoal(
            &g_speed_profile,
            current_distance_mm + marker_runout_mm)) {
        return false;
    }
    g_finish_marker_distance_mm = current_distance_mm;
    g_finish_marker_latched = true;
    return true;
}

static bool bringup_line_auxiliary_hold_active(uint32_t now_ms)
{
    if (g_has_last_valid_line_sample) {
        return (uint32_t)(now_ms - g_last_valid_line_sample_ms) <=
            TEMPLATE_BRINGUP_GRAYSCALE_AUXILIARY_HOLD_MS;
    }
    /* Give the sensor link one bounded startup window.  The INS is already
     * valid during this interval, so a delayed first UART frame does not
     * turn a commanded launch into a zero-speed step. */
    return (uint32_t)(now_ms - g_tracking_started_ms) <=
        TEMPLATE_BRINGUP_GRAYSCALE_AUXILIARY_HOLD_MS;
}

/* Return the route speed cap for the current encoder distance.  The cap is
 * changed before the profile update, but the profile itself slews toward it;
 * this keeps the B/C/D corner transitions linear as well. */
static float bringup_route_cruise_speed(float distance_from_start_mm)
{
    const float lap_mm = TEMPLATE_BRINGUP_H_ONE_LAP_DISTANCE_MM;
    const float straight_mm = TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM;
    const float arc_mm = TEMPLATE_BRINGUP_H_SEMICIRCLE_LENGTH_MM;
    const float straight_speed =
        TEMPLATE_BRINGUP_LINE_BASE_SPEED_MM_PER_SECOND;
    const float curve_speed =
        TEMPLATE_BRINGUP_LINE_CURVE_SPEED_MM_PER_SECOND;
    const float finish_approach_speed =
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) ?
        TEMPLATE_BRINGUP_ITEM2_FINISH_APPROACH_SPEED_MM_PER_SECOND :
        TEMPLATE_BRINGUP_FINISH_APPROACH_SPEED_MM_PER_SECOND;
    const float finish_window_lower_mm =
        TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_CENTER_MM -
            TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_HALF_WIDTH_MM;
    const float finish_transition_distance_mm =
        ((curve_speed * curve_speed) -
            (finish_approach_speed * finish_approach_speed)) /
        (2.0F * TEMPLATE_CAR_PROFILE_ACCELERATION_DOWN_MM_PER_SECOND2);
    const float finish_approach_start_mm =
        finish_window_lower_mm - finish_transition_distance_mm;
    float phase_mm;
    float distance_to_curve_mm;
    float preview_speed;

    if (!isfinite(distance_from_start_mm) ||
        (distance_from_start_mm < 0.0F) || (lap_mm <= 0.0F)) {
        return straight_speed;
    }
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_4) {
        return TEMPLATE_BRINGUP_ITEM4_AB_CRUISE_SPEED_MM_PER_SECOND;
    }
    if ((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6)) {
        const float item56_approach_speed =
            TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND;
        const float item56_transition_distance_mm =
            ((TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND *
                TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND) -
                (item56_approach_speed * item56_approach_speed)) /
            (2.0F *
                TEMPLATE_BRINGUP_ITEM56_LAP_DECELERATION_MM_PER_SECOND2);
        const float item56_approach_start_mm =
            finish_window_lower_mm - item56_transition_distance_mm;

        /* Keep the full-lap cap through all tangent points, then use the
         * VOFA-calibrated final approach so the axle can stop at A. */
        if (distance_from_start_mm >= item56_approach_start_mm) {
            return item56_approach_speed;
        }
        return TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND;
    }
    phase_mm = fmodf(distance_from_start_mm, lap_mm);
    if (phase_mm < straight_mm) {
        distance_to_curve_mm = straight_mm - phase_mm;
    }
    else if (phase_mm < (straight_mm + arc_mm)) {
        return curve_speed;
    }
    else if (phase_mm < ((2.0F * straight_mm) + arc_mm)) {
        distance_to_curve_mm =
            ((2.0F * straight_mm) + arc_mm) - phase_mm;
    }
    else {
        /* Reach the finish-approach speed at the lower edge of the A-point marker window.
         * SetCruiseSpeed only changes the cap; the existing linear profile
         * applies the configured 185 mm/s^2 deceleration. */
        if (phase_mm >= finish_approach_start_mm) {
            return finish_approach_speed;
        }
        return curve_speed;
    }
    /* Reach curve speed at B/D instead of starting to decelerate after the
     * tangent point. This is the same constant-deceleration envelope used by
     * the longitudinal stop planner. */
    preview_speed = sqrtf((curve_speed * curve_speed) +
        (2.0F * TEMPLATE_CAR_PROFILE_ACCELERATION_DOWN_MM_PER_SECOND2 *
            distance_to_curve_mm));
    return fminf(straight_speed, preview_speed);
}

/* H 题赛道的几何前视点，坐标原点为启动点 A，x 轴沿 A->B，y 轴向
 * 赛道内侧。INS 路径是主要转向来源，灰度只提供小幅辅助修正。 */
/* On the H drawing the track is on the vehicle-right while travelling A->B;
 * with +Y defined as vehicle-left, the clockwise arcs therefore use -Y. */
static bool bringup_route_point(
    float distance_from_start_mm, float *x_mm, float *y_mm)
{
    const float straight_mm = TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM;
    const float radius_mm = TEMPLATE_BRINGUP_H_SEMICIRCLE_RADIUS_MM;
    const float arc_mm = TEMPLATE_BRINGUP_H_SEMICIRCLE_LENGTH_MM;
    const float lap_mm = TEMPLATE_BRINGUP_H_ONE_LAP_DISTANCE_MM;
    const float pi = 3.14159265359F;
    float distance;
    float angle;

    if ((x_mm == NULL) || (y_mm == NULL) ||
        !isfinite(distance_from_start_mm) || (distance_from_start_mm < 0.0F) ||
        (straight_mm <= 0.0F) || (radius_mm <= 0.0F) ||
        (arc_mm <= 0.0F) || (lap_mm <= 0.0F)) {
        return false;
    }
    /* +Y is vehicle-left. The required A->B->C->D->A clockwise path lies
     * at negative Y and wraps cleanly for repeated-lap diagnostics. */
    distance = fmodf(distance_from_start_mm, lap_mm);
    if (distance < straight_mm) {
        *x_mm = distance;
        *y_mm = 0.0F;
    }
    else if (distance < (straight_mm + arc_mm)) {
        angle = -0.5F * pi +
            ((distance - straight_mm) / radius_mm);
        *x_mm = straight_mm + radius_mm * cosf(angle);
        *y_mm = -radius_mm - radius_mm * sinf(angle);
    }
    else if (distance < (2.0F * straight_mm + arc_mm)) {
        *x_mm = straight_mm -
            (distance - straight_mm - arc_mm);
        *y_mm = -2.0F * radius_mm;
    }
    else if (distance < lap_mm) {
        angle = 0.5F * pi +
            ((distance - 2.0F * straight_mm - arc_mm) / radius_mm);
        *x_mm = radius_mm * cosf(angle);
        *y_mm = -radius_mm - radius_mm * sinf(angle);
    }
    else {
        *x_mm = 0.0F;
        *y_mm = 0.0F;
    }
    return isfinite(*x_mm) && isfinite(*y_mm);
}

/* The 26H product keeps encoder average increments only for forward distance.
 * Heading is propagated exclusively by the bias-corrected ICM gyro; the Mode
 * rejects an invalid gyro instead of falling back to encoder yaw. Grayscale is
 * deliberately not consumed here; it is an independent lateral correction. */
static bool bringup_update_navigation(
    float dt_seconds,
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b,
    const Device_IMU_Snapshot *imu_snapshot,
    bool imu_navigation_valid)
{
    const float yaw_rate =
        (imu_navigation_valid && imu_snapshot != NULL) ?
        imu_snapshot->yaw_rate_dps : 0.0F;
    const float forward_speed = bringup_mean_speed(speed_a, speed_b);
    const float current_distance = bringup_mean_distance(speed_a, speed_b);
    float lookahead_distance;
    float lookahead_route_x;
    float lookahead_route_y;
    float lookahead_x;
    float lookahead_y;
    const float route_terminal_distance =
        bringup_route_terminal_distance_mm();
    const float yaw_rad = g_navigation_start_yaw_deg *
        0.01745329252F;
    Mode_Car_FusedNavigationInput input;

    if (!g_navigation_pose_valid || (dt_seconds <= 0.0F) ||
        !isfinite(forward_speed) || !isfinite(yaw_rate) ||
        !isfinite(current_distance)) {
        return false;
    }
    lookahead_distance = fmaxf(0.0F,
        current_distance - g_route_start_distance_mm) +
        TEMPLATE_BRINGUP_INS_LOOKAHEAD_MM;
    /* Aim at each mode's own terminal path point near the stop. M2/M5/M6
     * continue through A along AB for their configured one-car-length runout. */
    if ((route_terminal_distance > 0.0F) &&
        (lookahead_distance > route_terminal_distance)) {
        lookahead_distance = route_terminal_distance;
    }
    if (!bringup_route_point(
            lookahead_distance, &lookahead_route_x, &lookahead_route_y)) {
        return false;
    }
    lookahead_x = g_navigation_start_x_mm +
        (lookahead_route_x * cosf(yaw_rad)) -
        (lookahead_route_y * sinf(yaw_rad));
    lookahead_y = g_navigation_start_y_mm +
        (lookahead_route_x * sinf(yaw_rad)) +
        (lookahead_route_y * cosf(yaw_rad));
    input = (Mode_Car_FusedNavigationInput){
        .dt_seconds = dt_seconds,
        .left_distance_delta_mm = speed_a->delta_distance,
        .right_distance_delta_mm = speed_b->delta_distance,
        .measured_left_speed_mm_per_second =
            speed_a->speed_distance_per_second,
        .measured_right_speed_mm_per_second =
            speed_b->speed_distance_per_second,
        .measured_yaw_rate_deg_per_second = yaw_rate,
        .measured_yaw_rate_valid = imu_navigation_valid,
        .tracking_point_offset_mm = 0.0F,
        .lookahead_x_mm = lookahead_x,
        .lookahead_y_mm = lookahead_y,
        .forward_speed_mm_per_second = forward_speed,
    };
    g_navigation_update_valid = Mode_Car_FusedNavigation_Update(
        &g_navigation, &input, &g_navigation_output);
    return g_navigation_update_valid;
}

static void bringup_set_planned_targets(
    float forward_speed,
    float measured_speed,
    float line_left_correction,
    float line_right_correction,
    bool use_navigation)
{
    float left_offset = 0.0F;
    float right_offset = 0.0F;
    float left_target;
    float right_target;

    if (use_navigation && g_navigation_update_valid &&
        isfinite(measured_speed)) {
        left_offset = g_navigation_output.target_left_speed_mm_per_second -
            measured_speed;
        right_offset = g_navigation_output.target_right_speed_mm_per_second -
            measured_speed;
    }
    left_target = forward_speed + left_offset + line_left_correction;
    right_target = forward_speed + right_offset + line_right_correction;
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) {
        const float mean_target = 0.5F * (left_target + right_target);
        const float raw_half_difference =
            0.5F * (right_target - left_target);
        const float half_difference = fmaxf(
            -TEMPLATE_BRINGUP_ITEM2_MAXIMUM_HALF_DIFFERENCE_MM_PER_SECOND,
            fminf(
                TEMPLATE_BRINGUP_ITEM2_MAXIMUM_HALF_DIFFERENCE_MM_PER_SECOND,
                raw_half_difference));
        const float maximum_mean_target =
            TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND -
                fabsf(half_difference);
        const float bounded_mean_target = fmaxf(
            -maximum_mean_target,
            fminf(maximum_mean_target, mean_target));

        /* Preserve the requested steering differential under the per-wheel
         * clamp. If necessary, reduce only the common longitudinal target. */
        left_target = bounded_mean_target - half_difference;
        right_target = bounded_mean_target + half_difference;
    }
    g_target_left_speed = fmaxf(-TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
        fminf(TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
            left_target));
    g_target_right_speed = fmaxf(-TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
        fminf(TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
            right_target));
}

static void bringup_show_status(void)
{
    Device_RGB_Color color;

    if (g_BRINGUP_Fault != 0U) {
        color = (Device_RGB_Color){
            TEMPLATE_BRINGUP_RGB_BRIGHTNESS, 0U, 0U};
    } else if (g_tracking_active) {
        color = (Device_RGB_Color){
            0U, 0U, TEMPLATE_BRINGUP_RGB_BRIGHTNESS};
    } else {
        color = (Device_RGB_Color){
            0U, TEMPLATE_BRINGUP_RGB_BRIGHTNESS, 0U};
    }
    /* StartColor is non-blocking; a busy frame is retried next cycle. */
    (void)Device_RGB_StartColor(&g_rgb, color);
}

static void bringup_init_peer_link(uint32_t now_ms)
{
    const Device_PeerLink_Config config = {
        .transport_index = BRINGUP_PEER_LINK_TRANSPORT_INDEX,
        .local_node_id = TEMPLATE_PEER_LINK_LOCAL_NODE_ID,
        .remote_node_id = TEMPLATE_PEER_LINK_REMOTE_NODE_ID,
        .heartbeat_period_ms = TEMPLATE_PEER_LINK_HEARTBEAT_PERIOD_MS,
        .offline_timeout_ms = TEMPLATE_PEER_LINK_OFFLINE_TIMEOUT_MS,
        .maximum_rx_bytes_per_step =
            TEMPLATE_PEER_LINK_RX_BYTES_PER_STEP,
    };

    g_peer_link_fault = 0U;
    g_peer_link_ready = Device_PeerLink_Init(
        &g_peer_link, &config, now_ms);
    if (!g_peer_link_ready) {
        g_peer_link_fault = 1U;
    }
}

static void bringup_init_display(void)
{
    Device_ST7735S_Config config;
    uint16_t width;
    uint16_t height;
    const uint16_t used_height =
        BRINGUP_DISPLAY_STATUS_LINE_COUNT * BRINGUP_DISPLAY_LINE_HEIGHT;

    g_BRINGUP_DisplayFault = 0U;
    if (!Device_ST7735S_DefaultConfig(
            BRINGUP_TFT_DISPLAY_INDEX, &config)) {
        g_BRINGUP_DisplayFault = 1U;
        return;
    }
    if (Device_ST7735S_Init(&g_display, &config) !=
        DEVICE_ST7735S_RESULT_OK) {
        g_BRINGUP_DisplayFault = 2U;
        return;
    }
    if (!Device_ST7735S_GetSize(&g_display, &width, &height)) {
        g_BRINGUP_DisplayFault = 3U;
        return;
    }
    if (Device_ST7735S_Fill(&g_display,
            0U, 0U, (uint16_t)(width - 1U),
            (uint16_t)(height - 1U),
            Device_ST7735S_Color565(255U, 0U, 0U)) !=
        DEVICE_ST7735S_RESULT_OK) {
        g_BRINGUP_DisplayFault = 4U;
        return;
    }
    /* Preserve the red startup self-test, then clear the unused lower area
     * once. Runtime refreshes only touch the four compact status rows. */
    if ((height > used_height) &&
        (Device_ST7735S_Fill(&g_display,
            0U, used_height, (uint16_t)(width - 1U),
            (uint16_t)(height - 1U), BRINGUP_DISPLAY_COLOR_BLACK) !=
            DEVICE_ST7735S_RESULT_OK)) {
        g_BRINGUP_DisplayFault = 4U;
    }
}

static const uint8_t *bringup_display_glyph(char character)
{
    unsigned int value = (unsigned char)character;

    if ((value < (unsigned int)' ') || (value > (unsigned int)'Z')) {
        value = (unsigned int)' ';
    }
    return g_bringup_font_5x7[value - (unsigned int)' '];
}

static bool bringup_display_write_span(
    size_t line_index,
    const char *text,
    uint16_t foreground,
    size_t first_character,
    size_t character_count)
{
    const size_t text_length = (text != NULL) ? strlen(text) : 0U;
    const size_t pixel_width =
        character_count * BRINGUP_DISPLAY_CHARACTER_WIDTH;
    const size_t pixel_count =
        pixel_width * BRINGUP_DISPLAY_LINE_HEIGHT;

    if ((line_index >= BRINGUP_DISPLAY_STATUS_LINE_COUNT) ||
        (text == NULL) || (character_count == 0U) ||
        (first_character >= BRINGUP_DISPLAY_CHARACTER_COUNT) ||
        (character_count >
            (BRINGUP_DISPLAY_CHARACTER_COUNT - first_character))) {
        return false;
    }
    for (size_t index = 0U; index < pixel_count; ++index) {
        g_display_line_pixels[index] = BRINGUP_DISPLAY_COLOR_BLACK;
    }
    for (size_t relative_character = 0U;
         relative_character < character_count;
         ++relative_character) {
        const size_t character_index =
            first_character + relative_character;
        const uint8_t *glyph =
            bringup_display_glyph(
                (character_index < text_length) ?
                    text[character_index] : ' ');

        for (size_t glyph_y = 0U; glyph_y < 7U; ++glyph_y) {
            const size_t pixel_y = 1U + (2U * glyph_y);

            for (size_t glyph_x = 0U; glyph_x < 5U; ++glyph_x) {
                if ((glyph[glyph_y] &
                        (uint8_t)(UINT8_C(1) << (4U - glyph_x))) != 0U) {
                    const size_t pixel_x =
                        (relative_character *
                            BRINGUP_DISPLAY_CHARACTER_WIDTH) +
                        1U + glyph_x;

                    g_display_line_pixels[
                        pixel_y * pixel_width + pixel_x] =
                        foreground;
                    g_display_line_pixels[
                        (pixel_y + 1U) * pixel_width + pixel_x] =
                        foreground;
                }
            }
        }
    }
    return Device_ST7735S_WriteRGB565(&g_display,
        (uint16_t)(first_character * BRINGUP_DISPLAY_CHARACTER_WIDTH),
        (uint16_t)(line_index * BRINGUP_DISPLAY_LINE_HEIGHT),
        (uint16_t)(((first_character + character_count) *
            BRINGUP_DISPLAY_CHARACTER_WIDTH) - 1U),
        (uint16_t)(((line_index + 1U) *
            BRINGUP_DISPLAY_LINE_HEIGHT) - 1U),
        g_display_line_pixels,
        pixel_count) == DEVICE_ST7735S_RESULT_OK;
}

static bool bringup_line_sample_fresh(uint32_t now_ms)
{
    return g_line_sample_available &&
        ((uint32_t)(now_ms - g_line_sample.sampled_at_ms) <=
            TEMPLATE_BRINGUP_GRAYSCALE_TIMEOUT_MS);
}

static bool bringup_launch_hardware_ready(
    uint32_t now_ms, bool imu_valid)
{
    return g_devices_ready && (g_BRINGUP_Fault == 0U) &&
        g_grayscale_reporting_requested &&
        bringup_line_sample_fresh(now_ms) &&
        g_line_sample.line_present &&
        ((g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) ||
            imu_valid) &&
        g_peer_link_ready && g_peer_link.status.online;
}

static void bringup_update_display(uint32_t now_ms, bool imu_valid)
{
    char text[BRINGUP_DISPLAY_CHARACTER_COUNT + 1U];
    uint16_t foreground;

    if (g_BRINGUP_DisplayFault != 0U) {
        return;
    }
    switch (g_display_status_line) {
        case 0U: {
            const bool sample_fresh = bringup_line_sample_fresh(now_ms);

            if (!sample_fresh) {
                (void)snprintf(text, sizeof(text), "LINE:STALE");
                foreground = BRINGUP_DISPLAY_COLOR_RED;
            } else if (!g_line_sample.line_present) {
                (void)snprintf(text, sizeof(text), "LINE:LOST");
                foreground = BRINGUP_DISPLAY_COLOR_RED;
            } else {
                (void)snprintf(text, sizeof(text), "LINE:OK");
                foreground = BRINGUP_DISPLAY_COLOR_CYAN;
            }
            break;
        }
        case 1U: {
            uint32_t elapsed_deciseconds =
                (g_score_time_latched ?
                    g_score_elapsed_ms : g_inspection_elapsed_ms) /
                UINT32_C(100);
            uint32_t minutes;
            uint32_t seconds;
            uint32_t tenths;
            size_t minute_index;

            /* The 16-character row fits through 99:59.9. Keep the internal
             * millisecond counter intact and clamp only this presentation. */
            if (elapsed_deciseconds > UINT32_C(59999)) {
                elapsed_deciseconds = UINT32_C(59999);
            }
            minutes = elapsed_deciseconds / UINT32_C(600);
            seconds = (elapsed_deciseconds / UINT32_C(10)) % UINT32_C(60);
            tenths = elapsed_deciseconds % UINT32_C(10);
            if (g_tracking_active) {
                (void)memcpy(text, "CAR:RUN 00:00.0", sizeof("CAR:RUN 00:00.0"));
                minute_index = 8U;
            } else {
                (void)memcpy(text, "CAR:STOP 00:00.0", sizeof("CAR:STOP 00:00.0"));
                minute_index = 9U;
            }
            text[minute_index] = (char)('0' + (minutes / UINT32_C(10)));
            text[minute_index + 1U] =
                (char)('0' + (minutes % UINT32_C(10)));
            text[minute_index + 3U] =
                (char)('0' + (seconds / UINT32_C(10)));
            text[minute_index + 4U] =
                (char)('0' + (seconds % UINT32_C(10)));
            text[minute_index + 6U] = (char)('0' + tenths);
            foreground = g_tracking_active ? BRINGUP_DISPLAY_COLOR_GREEN :
                BRINGUP_DISPLAY_COLOR_YELLOW;
            break;
        }
        case 2U: {
            const bool link_online =
                g_peer_link_ready && g_peer_link.status.online;

            (void)snprintf(text, sizeof(text), "COM:I%u L%u",
                imu_valid ? 1U : 0U,
                link_online ? 1U : 0U);
            foreground = (imu_valid && link_online) ?
                BRINGUP_DISPLAY_COLOR_GREEN : BRINGUP_DISPLAY_COLOR_RED;
            break;
        }
        default: {
            switch (g_competition_mode) {
                case BRINGUP_COMPETITION_MODE_ITEM_3:
                    (void)snprintf(text, sizeof(text), "M3:STATIC BALL");
                    break;
                case BRINGUP_COMPETITION_MODE_ITEM_4:
                    (void)snprintf(text, sizeof(text), "M4:A-B YAW HOLD");
                    break;
                case BRINGUP_COMPETITION_MODE_ITEM_5:
                    (void)snprintf(text, sizeof(text), "M5:LAP CENTER");
                    break;
                case BRINGUP_COMPETITION_MODE_ITEM_6:
                    (void)snprintf(text, sizeof(text), "M6:LAP TARGET");
                    break;
                default:
                    (void)snprintf(text, sizeof(text), "M2:LAP STOP");
                    break;
            }
            foreground = g_competition_mode_confirmed ?
                BRINGUP_DISPLAY_COLOR_GREEN : BRINGUP_DISPLAY_COLOR_YELLOW;
            break;
        }
    }
    {
        const size_t new_length = strlen(text);
        const size_t previous_length =
            strlen(g_display_last_text[g_display_status_line]);
        size_t first_changed = BRINGUP_DISPLAY_CHARACTER_COUNT;
        size_t last_changed = 0U;

        if (foreground !=
            g_display_last_foreground[g_display_status_line]) {
            /* The first update must erase the red startup fill. Rare color
             * changes redraw the row so every existing glyph is recolored. */
            first_changed = 0U;
            last_changed = BRINGUP_DISPLAY_CHARACTER_COUNT - 1U;
        } else {
            for (size_t index = 0U;
                 index < BRINGUP_DISPLAY_CHARACTER_COUNT;
                 ++index) {
                const char current =
                    (index < new_length) ? text[index] : ' ';
                const char previous =
                    (index < previous_length) ?
                        g_display_last_text[g_display_status_line][index] : ' ';

                if (current != previous) {
                    if (first_changed ==
                        BRINGUP_DISPLAY_CHARACTER_COUNT) {
                        first_changed = index;
                    }
                    last_changed = index;
                }
            }
        }
        if ((first_changed < BRINGUP_DISPLAY_CHARACTER_COUNT) &&
            !bringup_display_write_span(
                g_display_status_line,
                text,
                foreground,
                first_changed,
                last_changed - first_changed + 1U)) {
            g_BRINGUP_DisplayFault = 5U;
            return;
        }
        if (first_changed < BRINGUP_DISPLAY_CHARACTER_COUNT) {
            (void)memcpy(g_display_last_text[g_display_status_line],
                text, new_length + 1U);
            g_display_last_foreground[g_display_status_line] = foreground;
        }
    }
    g_display_status_line =
        (g_display_status_line + 1U) % BRINGUP_DISPLAY_STATUS_LINE_COUNT;
}

/* Every motor command passes through this clamp so no code path can exceed
 * the TB6612 protection limit. */
static Device_Motor_Result bringup_command_motor(
    Device_Motor *motor, int32_t command)
{
    if (command > TEMPLATE_BRINGUP_MOTOR_COMMAND_LIMIT) {
        command = TEMPLATE_BRINGUP_MOTOR_COMMAND_LIMIT;
    } else if (command < -TEMPLATE_BRINGUP_MOTOR_COMMAND_LIMIT) {
        command = -TEMPLATE_BRINGUP_MOTOR_COMMAND_LIMIT;
    }
    return Device_Motor_SetCommand(motor, command);
}

static bool bringup_init_devices(void)
{
    Device_RGB_Config rgb_config;
    Device_KeyInput_Config key_config = {
        .key = {
            .debounce_ms = TEMPLATE_BRINGUP_KEY_DEBOUNCE_MS,
            .long_press_ms = TEMPLATE_BRINGUP_KEY_LONG_PRESS_MS,
            .double_click_ms = TEMPLATE_BRINGUP_KEY_DOUBLE_CLICK_MS,
        },
        .input_index = 0U,
    };
    const Device_Serial_Config bluetooth_config = {
        .transport_index = BRINGUP_BLUETOOTH_TRANSPORT_INDEX,
        .expected_baud_rate = BRINGUP_UART_BAUD,
        .minimum_tx_capacity = BRINGUP_TELEMETRY_LINE_CAPACITY,
        .minimum_rx_capacity = 0U,
        .require_buffered_tx = true,
        .require_buffered_rx = false,
    };
    const Device_Serial_Config grayscale_config = {
        .transport_index = BRINGUP_GRAYSCALE_TRANSPORT_INDEX,
        .expected_baud_rate = BRINGUP_UART_BAUD,
        .minimum_tx_capacity = DEVICE_YAHBOOM_TRACK_START_COMMAND_SIZE,
        .minimum_rx_capacity = DEVICE_YAHBOOM_TRACK_FRAME_CAPACITY,
        .require_buffered_tx = true,
        .require_buffered_rx = true,
    };
    const Device_Serial_Config pendulum_imu_serial_config = {
        .transport_index = BRINGUP_PENDULUM_IMU_TRANSPORT_INDEX,
        .expected_baud_rate = BRINGUP_UART_BAUD,
        .minimum_tx_capacity = 0U,
        .minimum_rx_capacity = 3U * 11U,
        .require_buffered_tx = false,
        .require_buffered_rx = true,
    };
    const Device_ICM45686_SPI_Config imu_transport = {
        .transport_index = BRINGUP_ICM45686_SPI_INDEX,
        .chip_select_index = 0U,
        .expected_bit_rate_hz = BRINGUP_ICM45686_SPI_BAUD,
        .transfer_timeout_ms = 5U,
        .startup_delay_ms = 10U,
    };
    Device_IMU_Config imu_config = Device_IMU_DefaultConfig();
    Device_LineTracking_Config line_config;

    if (!Device_RGB_DefaultConfig(0U, &rgb_config) ||
        !Device_RGB_Init(&g_rgb, &rgb_config)) {
        g_BRINGUP_Fault = 1U;
        return false;
    }
    if (!Device_KeyInput_Init(&g_start_key, &key_config, bringup_now_ms())) {
        g_BRINGUP_Fault = 2U;
        return false;
    }
    for (size_t index = 0U; index < BRINGUP_MOTOR_COUNT; ++index) {
        static const int8_t encoder_direction[BRINGUP_MOTOR_COUNT] = {
            TEMPLATE_BRINGUP_ENCODER_A_DIRECTION,
            TEMPLATE_BRINGUP_ENCODER_B_DIRECTION,
        };
        Device_Motor_Config motor_config;
        Device_Encoder_Config encoder_config;

        if (!Device_Motor_DefaultConfig(index, &motor_config) ||
            !Device_Motor_Init(&g_motor[index], &motor_config)) {
            g_BRINGUP_Fault = 3U;
            return false;
        }
        if (!Device_Encoder_MakeConfig(index,
                BRINGUP_COUNTS_PER_WHEEL_REV,
                BRINGUP_WHEEL_CIRCUMFERENCE_MM,
                encoder_direction[index],
                &encoder_config) ||
            !Device_Encoder_Init(&g_encoder[index], &encoder_config)) {
            g_BRINGUP_Fault = 4U;
            return false;
        }
    }
    if (!Device_Serial_Init(&g_bluetooth, &bluetooth_config)) {
        g_BRINGUP_Fault = 5U;
        return false;
    }
    /* Template policy layer: PC Bluetooth is a raw byte stream whose upper
     * format is decided by this product Task (FireWater text lines). */
    {
        Mode_Communicate_BluetoothOutput bluetooth_policy;

        if (!Mode_Communicate_BluetoothPC_Init(&g_bluetooth_mode) ||
            !Mode_Communicate_BluetoothPC_Update(
                &g_bluetooth_mode, &bluetooth_policy) ||
            (bluetooth_policy.format !=
                MODE_COMMUNICATE_BLUETOOTH_FORMAT_RAW_STREAM)) {
            g_BRINGUP_Fault = 5U;
            return false;
        }
    }
    /* Stationary gyro-bias calibration runs right after boot; keep the car
     * still for the first ~2 s so yaw drift stays low. */
    imu_config.start_calibration_on_init = true;
    imu_config.calibration_required_samples = 200U;
    if (!Device_ICM45686_SPI_Init(&g_imu, &imu_transport, &imu_config)) {
        g_BRINGUP_Fault = 6U;
        return false;
    }
    if (!Device_Serial_Init(&g_grayscale_serial, &grayscale_config) ||
        !Device_YahboomTrack_Init(&g_grayscale)) {
        g_BRINGUP_Fault = 8U;
        return false;
    }
    /* The official UART guide requires at least 20 s of power-on stability
     * before communication. The Task submits the one-shot start command later
     * without blocking this initialization path. */
    g_grayscale_initialized_ms = bringup_now_ms();
    /* The moving-link WIT IMU is diagnostic-only. A missing or misconfigured
     * module must not inhibit chassis start, navigation, or planned stopping. */
    g_pendulum_imu_ready =
        Device_Serial_Init(
            &g_pendulum_imu_serial, &pendulum_imu_serial_config) &&
        Device_JY61P_Init(&g_pendulum_imu, NULL);
    if (!Device_LineTracking_DefaultRawConfig(
            DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT, 0U,
            DEVICE_YAHBOOM_TRACK_ADC_FULL_SCALE, &line_config)) {
        g_BRINGUP_Fault = 10U;
        return false;
    }
    for (size_t index = 0U;
         index < DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT;
         ++index) {
        line_config.channel[index].position_weight =
            g_line_position_weight[index];
    }
    if (!Device_LineTracking_Init(&g_line_tracking, &line_config)) {
        g_BRINGUP_Fault = 10U;
        return false;
    }
    {
        const Mode_Car_SpeedControllerConfig speed_config =
            bringup_speed_controller_config();

        if (!Mode_Car_SpeedController_Init(
                &g_speed_controller, &speed_config)) {
            g_BRINGUP_Fault = 9U;
            return false;
        }
    }
    {
        const Mode_Car_AngleControllerConfig angle_config =
            Mode_Car_AngleController_DefaultConfig();

        if (!Mode_Car_AngleController_Init(
                &g_angle_controller, &angle_config)) {
            g_BRINGUP_Fault = 9U;
            return false;
        }
    }
    {
        const Mode_Car_LinearProfileConfig profile_config =
            bringup_linear_profile_config();

        if (!Mode_Car_LinearSpeedProfile_Init(
                &g_speed_profile, &profile_config)) {
            g_BRINGUP_Fault = 11U;
            return false;
        }
    }
    {
        Mode_Car_FusedNavigationConfig navigation_config =
            bringup_navigation_config();
        if (!Mode_Car_FusedNavigation_Init(
                &g_navigation, &navigation_config)) {
            g_BRINGUP_Fault = 12U;
            return false;
        }
    }
    {
        const Mode_Car_AnalogTrackingConfig tracking_config =
            bringup_tracking_config();

        if ((TEMPLATE_BRINGUP_LINE_BASE_SPEED_MM_PER_SECOND <= 0.0F) ||
            (TEMPLATE_BRINGUP_LINE_BASE_SPEED_MM_PER_SECOND >
                TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND) ||
            (TEMPLATE_BRINGUP_ITEM2_FINISH_APPROACH_SPEED_MM_PER_SECOND <=
                0.0F) ||
            (TEMPLATE_BRINGUP_ITEM2_FINISH_APPROACH_SPEED_MM_PER_SECOND >
                TEMPLATE_BRINGUP_LINE_CURVE_SPEED_MM_PER_SECOND) ||
            (TEMPLATE_BRINGUP_FINISH_APPROACH_SPEED_MM_PER_SECOND <= 0.0F) ||
            (TEMPLATE_BRINGUP_FINISH_APPROACH_SPEED_MM_PER_SECOND >
                TEMPLATE_BRINGUP_LINE_CURVE_SPEED_MM_PER_SECOND) ||
            (TEMPLATE_BRINGUP_ITEM4_AB_CRUISE_SPEED_MM_PER_SECOND <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM4_AB_CRUISE_SPEED_MM_PER_SECOND >
                TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND) ||
            (TEMPLATE_BRINGUP_ITEM4_AB_ACCELERATION_MM_PER_SECOND2 <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM4_AB_DECELERATION_MM_PER_SECOND2 <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM4_PASS_B_RUNOUT_MM <=
                TEMPLATE_CAR_PROFILE_GUARD_DISTANCE_MM) ||
            (TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND >
                TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND) ||
            (TEMPLATE_BRINGUP_ITEM56_LAP_ACCELERATION_MM_PER_SECOND2 <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_LAP_DECELERATION_MM_PER_SECOND2 <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND <=
                0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND >
                TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND) ||
            (TEMPLATE_BRINGUP_ITEM56_LINE_SENSOR_OFFSET_MM <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_AFTER_MARKER_GOAL_RUNOUT_MM <= 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_LINE_STEERING_KD < 0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_MAXIMUM_HALF_DIFFERENCE_MM_PER_SECOND <=
                0.0F) ||
            (TEMPLATE_BRINGUP_ITEM56_HALF_DIFFERENCE_SLEW_MM_PER_SECOND2 <=
                0.0F) ||
            ((TEMPLATE_CAR_PROFILE_GUARD_DISTANCE_MM +
                ((TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND *
                    TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND) /
                    (2.0F *
                        TEMPLATE_BRINGUP_ITEM56_LAP_DECELERATION_MM_PER_SECOND2))) >=
                TEMPLATE_BRINGUP_ITEM56_AFTER_MARKER_GOAL_RUNOUT_MM) ||
            !Mode_Car_AnalogTracking_Init(
                &g_tracking_controller, &tracking_config)) {
            g_BRINGUP_Fault = 10U;
            return false;
        }
    }
    return true;
}

/* Wait out the Yahboom power-on stabilization interval, then submit the
 * analog-report command exactly once. A failed buffered submission is a
 * terminal grayscale fault for this boot; there is no hidden retry loop. */
static bool bringup_start_grayscale_reporting(uint32_t now_ms)
{
    uint8_t command[DEVICE_YAHBOOM_TRACK_START_COMMAND_SIZE];
    size_t command_length = 0U;

    if (g_grayscale_reporting_requested) {
        return true;
    }
    if (g_grayscale_start_command_attempted) {
        return false;
    }
    if ((uint32_t)(now_ms - g_grayscale_initialized_ms) <
        DEVICE_YAHBOOM_TRACK_STARTUP_DELAY_MS) {
        return true;
    }

    g_grayscale_start_command_attempted = true;
    if (!Device_YahboomTrack_BuildStartCommand(
            command, sizeof(command), &command_length) ||
        (Device_Serial_WriteBuffered(&g_grayscale_serial,
             command, command_length) != DEVICE_SERIAL_RESULT_OK)) {
        g_BRINGUP_Fault = 8U;
        return false;
    }
    g_grayscale_reporting_requested = true;
    return true;
}

static void bringup_stop_motors(void)
{
    for (size_t index = 0U; index < BRINGUP_MOTOR_COUNT; ++index) {
        (void)Device_Motor_Stop(&g_motor[index]);
        (void)Device_Motor_SetUnlocked(&g_motor[index], false);
    }
    Mode_Car_SpeedController_Reset(&g_speed_controller);
    Mode_Car_AngleController_Reset(&g_angle_controller);
    Mode_Car_LinearSpeedProfile_Reset(&g_speed_profile);
    {
        const Mode_Car_AnalogTrackingConfig tracking_config =
            bringup_tracking_config();

        (void)Mode_Car_AnalogTracking_Init(
            &g_tracking_controller, &tracking_config);
    }
    g_target_left_speed = 0.0F;
    g_target_right_speed = 0.0F;
    g_line_left_speed_correction = 0.0F;
    g_line_right_speed_correction = 0.0F;
    g_line_speed_correction_valid = false;
    g_item56_line_half_difference_mm_per_second = 0.0F;
    g_has_previous_line_sample_time = false;
    g_tracking_active = false;
    g_stop_requested = false;
    g_navigation_update_valid = false;
    g_profile_zero_candidate = false;
    g_has_last_valid_line_sample = false;
}

static void bringup_finish_planned_stop(void)
{
    bringup_stop_motors();
}

static bool bringup_request_stop(
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b)
{
    if (!Mode_Car_LinearSpeedProfile_RequestStop(
            &g_speed_profile,
            bringup_mean_distance(speed_a, speed_b),
            bringup_mean_speed(speed_a, speed_b))) {
        g_BRINGUP_Fault = 11U;
        return false;
    }
    g_stop_requested = true;
    return true;
}

static void bringup_start_tracking(
    uint32_t now_ms,
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b,
    float start_yaw_deg)
{
    const float goal_distance_mm =
        bringup_competition_goal_distance_mm();

    if (!(goal_distance_mm > 0.0F) || !isfinite(goal_distance_mm)) {
        g_BRINGUP_Fault = 11U;
        bringup_stop_motors();
        return;
    }
    {
        const Mode_Car_FusedNavigationConfig navigation_config =
            bringup_navigation_config();

        if (!Mode_Car_FusedNavigation_Init(
                &g_navigation, &navigation_config)) {
            g_BRINGUP_Fault = 12U;
            bringup_stop_motors();
            return;
        }
    }
    {
        const Mode_Car_SpeedControllerConfig speed_config =
            bringup_speed_controller_config();

        if (!Mode_Car_SpeedController_Init(
                &g_speed_controller, &speed_config)) {
            g_BRINGUP_Fault = 9U;
            bringup_stop_motors();
            return;
        }
    }
    {
        const Mode_Car_LinearProfileConfig profile_config =
            bringup_linear_profile_config();

        if (!Mode_Car_LinearSpeedProfile_Init(
                &g_speed_profile, &profile_config)) {
            g_BRINGUP_Fault = 11U;
            bringup_stop_motors();
            return;
        }
    }
    for (size_t index = 0U; index < BRINGUP_MOTOR_COUNT; ++index) {
        if (Device_Motor_SetUnlocked(&g_motor[index], true) !=
                DEVICE_MOTOR_RESULT_OK) {
            g_BRINGUP_Fault = 3U;
            bringup_stop_motors();
            return;
        }
    }
    Mode_Car_AngleController_Reset(&g_angle_controller);
    if (!Mode_Car_LinearSpeedProfile_Start(
            &g_speed_profile,
            bringup_mean_distance(speed_a, speed_b),
            bringup_launch_cruise_speed_mm_per_second())) {
        g_BRINGUP_Fault = 11U;
        bringup_stop_motors();
        return;
    }
    {
        const Mode_Car_AnalogTrackingConfig tracking_config =
            bringup_tracking_config();

        if (!Mode_Car_AnalogTracking_Init(
                &g_tracking_controller, &tracking_config)) {
            g_BRINGUP_Fault = 10U;
            bringup_stop_motors();
            return;
        }
    }
    g_processed_grayscale_sequence = 0U;
    g_has_previous_line_sample_time = false;
    g_has_last_valid_line_sample = false;
    g_item56_line_half_difference_mm_per_second = 0.0F;
    g_tracking_started_ms = now_ms;
    g_inspection_elapsed_ms = 0U;
    g_score_elapsed_ms = 0U;
    g_score_time_latched = false;
    g_route_start_distance_mm = bringup_mean_distance(speed_a, speed_b);
    g_target_left_speed = 0.0F;
    g_target_right_speed = 0.0F;
    g_line_left_speed_correction = 0.0F;
    g_line_right_speed_correction = 0.0F;
    g_line_speed_correction_valid = false;
    g_tracking_active = true;
    g_stop_requested = false;
    g_navigation_start_x_mm = 0.0F;
    g_navigation_start_y_mm = 0.0F;
    g_navigation_start_yaw_deg = isfinite(start_yaw_deg) ? start_yaw_deg : 0.0F;
    g_navigation_pose_valid = Mode_Car_FusedNavigation_ResetPose(
        &g_navigation,
        &(Mode_Car_FusedNavigationPose){
            .x_mm = g_navigation_start_x_mm,
            .y_mm = g_navigation_start_y_mm,
            .yaw_deg = g_navigation_start_yaw_deg,
        });
    g_navigation_update_valid = false;
    g_profile_zero_candidate = false;
    g_finish_marker_latched = false;
    g_finish_marker_window_armed = false;
    g_finish_marker_confirm_count = 0U;
    g_finish_marker_distance_mm = 0.0F;
    if (!Mode_Car_LinearSpeedProfile_SetGoal(
            &g_speed_profile,
            g_route_start_distance_mm + goal_distance_mm)) {
        g_BRINGUP_Fault = 11U;
        bringup_stop_motors();
        return;
    }
}

static void bringup_latch_competition_time(
    uint32_t now_ms, float current_distance_mm)
{
    float pass_distance_mm;

    if (g_score_time_latched || !isfinite(current_distance_mm)) {
        return;
    }
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_4) {
        pass_distance_mm = g_route_start_distance_mm +
            TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM;
    } else if ((g_competition_mode ==
                    BRINGUP_COMPETITION_MODE_ITEM_5) ||
            (g_competition_mode ==
                    BRINGUP_COMPETITION_MODE_ITEM_6)) {
        pass_distance_mm = g_finish_marker_latched ?
            g_finish_marker_distance_mm +
                TEMPLATE_BRINGUP_ITEM56_LINE_SENSOR_OFFSET_MM :
            g_route_start_distance_mm +
                TEMPLATE_BRINGUP_H_ONE_LAP_DISTANCE_MM;
    } else {
        return;
    }
    if (current_distance_mm >= pass_distance_mm) {
        g_score_elapsed_ms =
            (uint32_t)(now_ms - g_tracking_started_ms);
        g_score_time_latched = true;
    }
}

static bool bringup_pause_tracking(uint32_t now_ms, float dt_seconds)
{
    const Mode_Car_AnalogTrackingInput tracking_input = {
        .time = {
            .now_ms = now_ms,
            .dt_seconds = (dt_seconds > 0.0F) ? dt_seconds :
                (float)BRINGUP_TASK_PERIOD_MS * 0.001F,
        },
        .base_drive = 0.0F,
        .analog_error = 0.0F,
        .confidence = 0.0F,
        .line_valid = false,
        .line_lost = true,
        .intersection = false,
    };
    Mode_Car_Output stop_output;

    g_target_left_speed = 0.0F;
    g_target_right_speed = 0.0F;
    g_line_left_speed_correction = 0.0F;
    g_line_right_speed_correction = 0.0F;
    g_line_speed_correction_valid = false;
    Mode_Car_SpeedController_Reset(&g_speed_controller);
    if (!Mode_Car_AnalogTracking_Update(
            &g_tracking_controller, &tracking_input, &stop_output)) {
        g_BRINGUP_Fault = 10U;
        return false;
    }
    if ((bringup_command_motor(&g_motor[0], 0) !=
            DEVICE_MOTOR_RESULT_OK) ||
        (bringup_command_motor(&g_motor[1], 0) !=
            DEVICE_MOTOR_RESULT_OK)) {
        g_BRINGUP_Fault = 3U;
        return false;
    }
    return true;
}

/* The INS/profile pair is the longitudinal backbone.  Each new T8 frame only
 * changes the differential (left/right) correction.  During a short UART
 * gap, the last pose/heading estimate keeps the car moving straight; after
 * the bounded auxiliary window the same profile performs a planned stop. */
static void bringup_update_tracking(
    uint32_t now_ms,
    float dt_seconds,
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b,
    const Device_IMU_Snapshot *imu_snapshot,
    bool imu_navigation_valid)
{
    Device_YahboomTrack_Snapshot snapshot;
    Mode_Car_LinearProfileOutput profile_output;
    Mode_Car_SpeedControllerInput speed_input;
    Mode_Car_SpeedControllerOutput speed_output;
    Mode_Car_AngleControllerOutput item56_finish_heading_output = {0};
    const float current_distance = bringup_mean_distance(speed_a, speed_b);
    const float measured_speed = bringup_mean_speed(speed_a, speed_b);
    const bool item56_mode =
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_5) ||
        (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_6);
    bool snapshot_available;
    bool grayscale_fresh = false;
    bool auxiliary_hold = false;

    if ((dt_seconds <= 0.0F) || !isfinite(current_distance) ||
        !isfinite(measured_speed)) {
        return;
    }
    if (g_speed_profile.running &&
        !Mode_Car_LinearSpeedProfile_SetCruiseSpeed(
            &g_speed_profile,
            bringup_route_cruise_speed(
                fmaxf(0.0F, current_distance - g_route_start_distance_mm)))) {
        g_BRINGUP_Fault = 11U;
        bringup_stop_motors();
        return;
    }
    if (!Mode_Car_LinearSpeedProfile_Update(
            &g_speed_profile,
            &(Mode_Car_LinearProfileInput){
                .dt_seconds = dt_seconds,
                .odometry_distance_mm = current_distance,
                .measured_speed_mm_per_second = measured_speed,
            }, &profile_output)) {
        g_BRINGUP_Fault = 11U;
        bringup_stop_motors();
        return;
    }
    if (profile_output.stopped) {
        /* A distance-goal completion is a normal run completion too. */
        if (profile_output.goal_valid) {
            g_stop_requested = true;
        }
        (void)bringup_pause_tracking(now_ms, dt_seconds);
        if (!g_profile_zero_candidate) {
            g_profile_zero_candidate = true;
            g_profile_zero_since_ms = now_ms;
        } else if ((uint32_t)(now_ms - g_profile_zero_since_ms) >=
            (uint32_t)(TEMPLATE_CAR_BRAKE_STABLE_TIME_SECONDS * 1000.0F)) {
            bringup_finish_planned_stop();
        }
        return;
    }
    g_profile_zero_candidate = false;

    snapshot_available = Device_YahboomTrack_GetLatest(
        &g_grayscale, &snapshot);
    if (snapshot_available) {
        grayscale_fresh = Device_YahboomTrack_SnapshotFresh(
            &snapshot, now_ms, TEMPLATE_BRINGUP_GRAYSCALE_TIMEOUT_MS);
    }
    auxiliary_hold = bringup_line_auxiliary_hold_active(now_ms);
    if (grayscale_fresh && g_line_sample_available &&
        (snapshot.sequence == g_line_sample_sequence) &&
        (snapshot.sequence != g_processed_grayscale_sequence)) {
        Mode_Car_AnalogTrackingInput tracking_input;
        Mode_Car_Output tracking_output;
        float tracking_dt_seconds =
            (float)BRINGUP_TASK_PERIOD_MS * 0.001F;

        if (g_has_previous_line_sample_time) {
            tracking_dt_seconds =
                (float)((uint32_t)(snapshot.received_at_ms -
                    g_previous_line_sample_ms)) * 0.001F;
        }
        tracking_input = (Mode_Car_AnalogTrackingInput){
            .time = {
                .now_ms = snapshot.received_at_ms,
                .dt_seconds = tracking_dt_seconds,
            },
            .base_drive =
                profile_output.reference_speed_mm_per_second /
                TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
            .analog_error = g_line_sample.error,
            .confidence = g_line_sample.confidence,
            .line_valid = g_line_sample.line_present,
            .line_lost = g_line_sample.line_lost,
            .intersection = g_line_sample.intersection,
        };
        if (!Mode_Car_AnalogTracking_Update(
                &g_tracking_controller, &tracking_input,
                &tracking_output)) {
            g_BRINGUP_Fault = 10U;
            bringup_stop_motors();
            return;
        }
        if (!bringup_update_finish_marker(&snapshot, current_distance)) {
            g_BRINGUP_Fault = 11U;
            bringup_stop_motors();
            return;
        }
        g_processed_grayscale_sequence = snapshot.sequence;
        g_previous_line_sample_ms = snapshot.received_at_ms;
        g_has_previous_line_sample_time = true;
        if (tracking_output.motion_requested && g_line_sample.line_present) {
            const float base_drive =
                profile_output.reference_speed_mm_per_second /
                TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND;
            const float left_line_correction =
                (tracking_output.left_drive - base_drive) *
                TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND;
            const float right_line_correction =
                (tracking_output.right_drive - base_drive) *
                TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND;

            g_line_left_speed_correction = left_line_correction;
            g_line_right_speed_correction = right_line_correction;
            g_line_speed_correction_valid = true;
            g_last_valid_line_sample_ms = snapshot.received_at_ms;
            g_has_last_valid_line_sample = true;
        } else {
            /* A fresh but invalid frame is treated like a short UART gap if
             * a recent valid line exists.  Only a prolonged loss requests
             * the profile's planned stop. */
            g_line_speed_correction_valid = false;
            if (!auxiliary_hold &&
                !bringup_request_stop(speed_a, speed_b)) {
                bringup_stop_motors();
                return;
            }
        }
    }
    if (!auxiliary_hold && !grayscale_fresh) {
        if (!bringup_request_stop(speed_a, speed_b)) {
            bringup_stop_motors();
            return;
        }
    }
    if (item56_mode && g_finish_marker_latched && imu_navigation_valid &&
        (imu_snapshot != NULL)) {
        if (!Mode_Car_AngleController_Update(
                &g_angle_controller,
                &(Mode_Car_AngleControllerInput){
                    .dt_seconds = dt_seconds,
                    .target_yaw_deg = g_navigation_start_yaw_deg,
                    .measured_yaw_deg = imu_snapshot->yaw_deg,
                    .measured_yaw_rate_deg_per_second =
                        imu_snapshot->yaw_rate_dps,
                }, &item56_finish_heading_output)) {
            g_BRINGUP_Fault = 9U;
            bringup_stop_motors();
            return;
        }
    }
    /* Item 4 holds launch yaw throughout. M2 and M5/M6 keep grayscale
     * steering, then add launch-yaw correction only after the finish marker. */
    if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_2) {
        Mode_Car_AngleControllerOutput finish_heading_output = {0};
        const float line_left_guidance =
            (grayscale_fresh && g_line_sample_available &&
                g_line_sample.line_present &&
                g_line_speed_correction_valid) ?
            g_line_left_speed_correction *
                TEMPLATE_BRINGUP_ITEM2_LINE_GUIDANCE_RATIO : 0.0F;
        const float line_right_guidance =
            (grayscale_fresh && g_line_sample_available &&
                g_line_sample.line_present &&
                g_line_speed_correction_valid) ?
            g_line_right_speed_correction *
                TEMPLATE_BRINGUP_ITEM2_LINE_GUIDANCE_RATIO : 0.0F;

        if (g_finish_marker_latched && imu_navigation_valid &&
            (imu_snapshot != NULL)) {
            if (!Mode_Car_AngleController_Update(
                    &g_angle_controller,
                    &(Mode_Car_AngleControllerInput){
                        .dt_seconds = dt_seconds,
                        .target_yaw_deg = g_navigation_start_yaw_deg,
                        .measured_yaw_deg = imu_snapshot->yaw_deg,
                        .measured_yaw_rate_deg_per_second =
                            imu_snapshot->yaw_rate_dps,
                    }, &finish_heading_output)) {
                g_BRINGUP_Fault = 9U;
                bringup_stop_motors();
                return;
            }
        }
        bringup_set_planned_targets(
            profile_output.reference_speed_mm_per_second,
            measured_speed,
            line_left_guidance + finish_heading_output.target_left_speed,
            line_right_guidance + finish_heading_output.target_right_speed,
            false);
    } else if (g_competition_mode == BRINGUP_COMPETITION_MODE_ITEM_4) {
        Mode_Car_AngleControllerOutput angle_output = {0};

        if (imu_navigation_valid && (imu_snapshot != NULL)) {
            if (!Mode_Car_AngleController_Update(
                    &g_angle_controller,
                    &(Mode_Car_AngleControllerInput){
                        .dt_seconds = dt_seconds,
                        .target_yaw_deg = g_navigation_start_yaw_deg,
                        .measured_yaw_deg = imu_snapshot->yaw_deg,
                        .measured_yaw_rate_deg_per_second =
                            imu_snapshot->yaw_rate_dps,
                    }, &angle_output)) {
                g_BRINGUP_Fault = 9U;
                bringup_stop_motors();
                return;
            }
        }
        g_target_left_speed = fmaxf(
            -TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
            fminf(TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
                profile_output.reference_speed_mm_per_second +
                    angle_output.target_left_speed));
        g_target_right_speed = fmaxf(
            -TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
            fminf(TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND,
                profile_output.reference_speed_mm_per_second +
                    angle_output.target_right_speed));
    } else if (grayscale_fresh && g_line_sample_available &&
        g_line_sample.line_present && g_line_speed_correction_valid) {
        if (item56_mode) {
            const float raw_half_difference = 0.5F *
                (g_line_right_speed_correction -
                    g_line_left_speed_correction);
            const float half_difference =
                bringup_item56_slew_line_half_difference(
                    raw_half_difference, dt_seconds);

            /* M5/M6 retain bounded grayscale guidance. After the marker, the
             * launch-yaw PID is added without changing the longitudinal mean. */
            bringup_set_planned_targets(
                profile_output.reference_speed_mm_per_second,
                measured_speed,
                -half_difference +
                    item56_finish_heading_output.target_left_speed,
                half_difference +
                    item56_finish_heading_output.target_right_speed,
                false);
        } else {
            bringup_set_planned_targets(
                profile_output.reference_speed_mm_per_second,
                measured_speed,
                g_line_left_speed_correction,
                g_line_right_speed_correction,
                false);
        }
    } else {
        if (item56_mode) {
            const float half_difference =
                bringup_item56_slew_line_half_difference(0.0F, dt_seconds);

            bringup_set_planned_targets(
                profile_output.reference_speed_mm_per_second,
                measured_speed,
                -half_difference +
                    item56_finish_heading_output.target_left_speed,
                half_difference +
                    item56_finish_heading_output.target_right_speed,
                false);
        } else {
            bringup_set_planned_targets(
                profile_output.reference_speed_mm_per_second,
                measured_speed, 0.0F, 0.0F, true);
        }
    }
    speed_input = (Mode_Car_SpeedControllerInput){
        .dt_seconds = dt_seconds,
        .target_left_speed = g_target_left_speed,
        .target_right_speed = g_target_right_speed,
        .measured_left_speed = speed_a->speed_distance_per_second,
        .measured_right_speed = speed_b->speed_distance_per_second,
    };
    if (!Mode_Car_SpeedController_Update(
            &g_speed_controller, &speed_input, &speed_output)) {
        g_BRINGUP_Fault = 9U;
        bringup_stop_motors();
        return;
    }
    if ((bringup_command_motor(&g_motor[0],
            (int32_t)(speed_output.left_effort_percent * 100.0F)) !=
            DEVICE_MOTOR_RESULT_OK) ||
        (bringup_command_motor(&g_motor[1],
            (int32_t)(speed_output.right_effort_percent * 100.0F)) !=
            DEVICE_MOTOR_RESULT_OK)) {
        g_BRINGUP_Fault = 3U;
        bringup_stop_motors();
    }
}

/* Drain the Yahboom active-report stream and feed every buffered byte into
 * the strict ASCII frame parser. */
static void bringup_update_grayscale(uint32_t now_ms)
{
    uint8_t buffer[DEVICE_YAHBOOM_TRACK_FRAME_CAPACITY];
    size_t received = 0U;

    while ((Device_Serial_ReadAvailable(&g_grayscale_serial,
                buffer, sizeof(buffer), &received) ==
            DEVICE_SERIAL_RESULT_OK) &&
        (received > 0U)) {
        for (size_t index = 0U; index < received; ++index) {
            (void)Device_YahboomTrack_FeedByte(
                &g_grayscale, now_ms, buffer[index]);
        }
    }
}

/* Convert each accepted Yahboom frame exactly once, including while parked. This
 * keeps LINE readiness current before a launch attempt; the running control
 * path only consumes this already-validated sample. */
static void bringup_update_line_sample(void)
{
    Device_YahboomTrack_Snapshot snapshot;
    Device_LineTracking_RawSample raw = {
        .channel_count = DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT,
        .valid_mask = UINT32_C(0xFF),
    };

    if (!Device_YahboomTrack_GetLatest(&g_grayscale, &snapshot) ||
        (g_line_sample_available &&
            (snapshot.sequence == g_line_sample_sequence))) {
        return;
    }
    for (size_t index = 0U;
         index < DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT;
         ++index) {
        /* Keep the user's existing raw-analog grayscale algorithm. The new
         * Device changes only the UART protocol and x1..x8 acquisition. */
        raw.value[index] = snapshot.adc[index];
    }
    if (!Device_LineTracking_UpdateAnalogRaw(
            &g_line_tracking, &raw, snapshot.received_at_ms) ||
        !Device_LineTracking_GetSample(
            &g_line_tracking, &g_line_sample)) {
        g_line_sample_available = false;
        g_BRINGUP_Fault = 10U;
        bringup_stop_motors();
        return;
    }
    g_line_sample_sequence = snapshot.sequence;
    g_line_sample_available = true;
}

/* Drain a bounded amount from the moving-link WIT IMU. The Device parser owns
 * frame synchronization/checksum; this Task only owns the UART and scheduling.
 * The sensor is diagnostic-only and never enters the chassis control path. */
static void bringup_update_pendulum_imu(uint32_t now_ms)
{
    uint8_t buffer[BRINGUP_PENDULUM_IMU_READ_CAPACITY];
    size_t received = 0U;

    if (!g_pendulum_imu_ready) {
        return;
    }
    if (Device_Serial_ReadAvailable(
            &g_pendulum_imu_serial, buffer, sizeof(buffer), &received) ==
        DEVICE_SERIAL_RESULT_OK) {
        for (size_t index = 0U; index < received; ++index) {
            (void)Device_JY61P_FeedByte(
                &g_pendulum_imu, buffer[index], now_ms);
        }
    }
    Device_JY61P_UpdateOnline(&g_pendulum_imu, now_ms);
}

static void bringup_send_firewater(
    uint32_t now_ms,
    const Device_Encoder_Sample *speed_a,
    const Device_Encoder_Sample *speed_b,
    float yaw_deg,
    float chassis_pitch_deg,
    bool chassis_pitch_valid)
{
    char line[BRINGUP_TELEMETRY_LINE_CAPACITY];
    Device_JY61P_Snapshot pendulum_imu = {0};
    Device_YahboomTrack_Snapshot grayscale = {0};
    Device_PeerLink_DebugTelemetry f407_debug = {0};
    const Mode_Car_LinearProfileOutput *profile = &g_speed_profile.output;
    const float route_distance_mm = g_tracking_active ?
        fmaxf(0.0F,
            bringup_mean_distance(speed_a, speed_b) -
                g_route_start_distance_mm) : 0.0F;
    int32_t line_error_milli = bringup_line_sample_fresh(now_ms)
        ? (int32_t)(g_line_sample.error * 1000.0F) : 0;
    int32_t yaw_centi = (int32_t)(yaw_deg * 100.0F);
    uint32_t pendulum_angle_age_ms =
        BRINGUP_PENDULUM_IMU_INVALID_AGE_MS;
    const bool pendulum_snapshot_valid = g_pendulum_imu_ready &&
        Device_JY61P_GetSnapshot(&g_pendulum_imu, &pendulum_imu) &&
        pendulum_imu.has_euler && (pendulum_imu.euler_frame_count > 0U);
    const bool grayscale_snapshot_fresh =
        Device_YahboomTrack_GetLatest(&g_grayscale, &grayscale) &&
            Device_YahboomTrack_SnapshotFresh(
                &grayscale, now_ms, TEMPLATE_BRINGUP_GRAYSCALE_TIMEOUT_MS);
    const bool f407_debug_fresh = g_peer_link_ready &&
        Device_PeerLink_GetRemoteDebugTelemetry(
            &g_peer_link, now_ms, BRINGUP_F407_DEBUG_MAX_AGE_MS,
            &f407_debug);
    bool absolute_pitch_valid = false;
    int32_t absolute_pitch_centi = 0;
    const char *line_error_sign = "";
    const char *yaw_sign = "";
    const char *absolute_pitch_sign = "";
    int length;

    if (pendulum_snapshot_valid) {
        const uint32_t age_ms =
            (uint32_t)(now_ms - pendulum_imu.last_euler_frame_ms);

        const float absolute_pitch_deg =
            chassis_pitch_deg + pendulum_imu.euler_deg[1];

        absolute_pitch_valid = chassis_pitch_valid &&
            (age_ms <= TEMPLATE_BRINGUP_JY61P_ANGLE_TIMEOUT_MS) &&
            isfinite(chassis_pitch_deg) &&
            isfinite(pendulum_imu.euler_deg[1]) &&
            isfinite(absolute_pitch_deg) &&
            (fabsf(absolute_pitch_deg) <= 360.0F);
        if (absolute_pitch_valid) {
            /* User-defined calibration convention: the JY61P Pitch is the
             * rod's relative term, so CH12 is the sum with chassis Pitch. */
            absolute_pitch_centi =
                (int32_t)(absolute_pitch_deg * 100.0F);
            pendulum_angle_age_ms = age_ms;
        }
    }

    if (line_error_milli < 0) {
        line_error_sign = "-";
        line_error_milli = -line_error_milli;
    }
    if (yaw_centi < 0) {
        yaw_sign = "-";
        yaw_centi = -yaw_centi;
    }
    if (absolute_pitch_centi < 0) {
        absolute_pitch_sign = "-";
        absolute_pitch_centi = -absolute_pitch_centi;
    }
    if (!grayscale_snapshot_fresh) {
        grayscale = (Device_YahboomTrack_Snapshot){0};
    }
    if (!f407_debug_fresh) {
        f407_debug = (Device_PeerLink_DebugTelemetry){0};
    }

    /* Channel order: route distance, measured wheel A/B speed, target wheel
     * A/B speed, profile reference speed/acceleration, remaining distance,
     * braking trigger distance, chassis yaw, line error, profile state,
     * absolute rod Pitch (ICM45686 + JY61P), JY61P angle age in ms, then the
     * fresh Yahboom raw ADC values x1..x8, then F407 DBG CH22..CH31. */
    length = snprintf(line, sizeof(line),
        "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%s%ld.%02ld,"
        "%s%ld.%03ld,%u,%s%ld.%02ld,%lu,"
        "%u,%u,%u,%u,%u,%u,%u,%u,"
        "%ld,%ld,%ld,%u,%u,%ld,%ld,%ld,%u,%u\n",
        (long)route_distance_mm,
        (long)speed_a->speed_distance_per_second,
        (long)speed_b->speed_distance_per_second,
        (long)g_target_left_speed,
        (long)g_target_right_speed,
        (long)profile->reference_speed_mm_per_second,
        (long)profile->reference_acceleration_mm_per_second2,
        (long)profile->remaining_distance_mm,
        (long)profile->braking_trigger_distance_mm,
        yaw_sign,
        (long)(yaw_centi / 100),
        (long)(yaw_centi % 100),
        line_error_sign,
        (long)(line_error_milli / 1000),
        (long)(line_error_milli % 1000),
        (unsigned)profile->state,
        absolute_pitch_sign,
        (long)(absolute_pitch_centi / 100),
        (long)(absolute_pitch_centi % 100),
        (unsigned long)pendulum_angle_age_ms,
        (unsigned)grayscale.adc[0],
        (unsigned)grayscale.adc[1],
        (unsigned)grayscale.adc[2],
        (unsigned)grayscale.adc[3],
        (unsigned)grayscale.adc[4],
        (unsigned)grayscale.adc[5],
        (unsigned)grayscale.adc[6],
        (unsigned)grayscale.adc[7],
        (long)f407_debug.chassis_acceleration_mm_s2,
        (long)f407_debug.chassis_compensation_mdeg,
        (long)f407_debug.rod_command_mdeg,
        (unsigned)f407_debug.motor_target_raw,
        (unsigned)f407_debug.motor_feedback_raw,
        (long)f407_debug.motor_command_current_ma,
        (long)f407_debug.motor_feedback_current_ma,
        (long)f407_debug.motor_speed_centi_rpm,
        (unsigned)f407_debug.control_phase,
        (unsigned)(f407_debug.status_flags |
            (f407_debug_fresh ? BRINGUP_F407_DEBUG_FRESH_FLAG : 0U)));
    if ((length > 0) && ((size_t)length < sizeof(line))) {
        /* Dropping a frame on BUSY is acceptable for bring-up telemetry. */
        (void)Device_Serial_WriteBuffered(
            &g_bluetooth, (const uint8_t *)line, (size_t)length);
    }
}

static void Task_BringupChassis(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t previous_ms;
    uint32_t last_car_state_ms;
    uint32_t last_telemetry_ms = 0U;
    uint32_t last_display_ms = 0U;
    bool display_tracking_previous = false;

    (void)argument;
    g_devices_ready = bringup_init_devices();
    bringup_init_peer_link(bringup_now_ms());
    /* The panel reset/init is a bounded blocking startup operation. Running
     * it here, after the scheduler starts, lets the higher-priority Control
     * task continue feeding WWDT1. Display failure stays independent from
    * chassis and communication readiness. */
    bringup_init_display();
    last_wake = xTaskGetTickCount();
    previous_ms = bringup_now_ms();
    last_car_state_ms =
        previous_ms - TEMPLATE_PEER_LINK_CAR_STATE_PERIOD_MS;

    for (;;) {
        const uint32_t now_ms = bringup_now_ms();
        const float dt_seconds = (float)(now_ms - previous_ms) * 0.001F;
        bool display_imu_valid = false;

        previous_ms = now_ms;
        if (g_peer_link_ready &&
            !Device_PeerLink_Step(&g_peer_link, now_ms)) {
            g_peer_link_fault = 2U;
        }
        if (g_devices_ready) {
            Device_Encoder_Sample sample_a = {0};
            Device_Encoder_Sample sample_b = {0};
            Device_IMU_Snapshot imu_snapshot = {0};
            float yaw_deg = 0.0F;
            bool encoder_samples_valid;
            bool imu_valid = false;
            bool imu_navigation_valid = false;
            bool telemetry_due = false;

            (void)Device_Encoder_Update(&g_encoder[0], dt_seconds);
            (void)Device_Encoder_Update(&g_encoder[1], dt_seconds);
            {
                const bool sample_a_valid =
                    Device_Encoder_GetSample(&g_encoder[0], &sample_a);
                const bool sample_b_valid =
                    Device_Encoder_GetSample(&g_encoder[1], &sample_b);

                encoder_samples_valid = sample_a_valid && sample_b_valid;
            }

            if ((Device_ICM45686_SPI_Update(&g_imu, now_ms, dt_seconds,
                    NULL, &imu_snapshot) == DEVICE_ICM45686_STATUS_OK) &&
                imu_snapshot.has_sample) {
                yaw_deg = imu_snapshot.yaw_deg;
                imu_valid = imu_snapshot.feedback_valid;
                imu_navigation_valid = imu_valid &&
                    imu_snapshot.gyro_bias_valid &&
                    (imu_snapshot.calibration_status ==
                        DEVICE_IMU_CALIBRATION_COMPLETE);
            }

            telemetry_due = (now_ms - last_telemetry_ms) >=
                TEMPLATE_BRINGUP_TELEMETRY_PERIOD_MS;
            if (!bringup_start_grayscale_reporting(now_ms)) {
                bringup_stop_motors();
            }
            bringup_update_grayscale(now_ms);
            bringup_update_line_sample();
            bringup_update_pendulum_imu(now_ms);
            display_imu_valid = imu_navigation_valid;

            if (!Device_KeyInput_Update(&g_start_key, now_ms)) {
                g_BRINGUP_Fault = 2U;
                bringup_stop_motors();
            }

            if (g_tracking_active &&
                (g_competition_mode != BRINGUP_COMPETITION_MODE_ITEM_2)) {
                (void)bringup_update_navigation(
                    dt_seconds, &sample_a, &sample_b,
                    &imu_snapshot, imu_navigation_valid);
            }

            for (size_t event_count = 0U;
                 event_count < DEVICE_KEY_EVENT_QUEUE_CAPACITY;
                 ++event_count) {
                Device_Key_Event key_event;

                if (!Device_KeyInput_ReadEvent(&g_start_key, &key_event)) {
                    break;
                }
                if (key_event == DEVICE_KEY_EVENT_LONG_PRESS) {
                    if (!g_tracking_active) {
                        g_competition_mode =
                            bringup_next_competition_mode(
                                g_competition_mode);
                        g_competition_mode_confirmed = false;
                        g_inspection_elapsed_ms = 0U;
                        g_display_status_line = 3U;
                    }
                    continue;
                }
                if (key_event != DEVICE_KEY_EVENT_SHORT_PRESS) {
                    continue;
                }
                if (g_tracking_active) {
                    if ((g_BRINGUP_Fault == 0U) &&
                        !bringup_request_stop(&sample_a, &sample_b)) {
                        bringup_stop_motors();
                    }
                    continue;
                }
                if (g_competition_mode ==
                        BRINGUP_COMPETITION_MODE_ITEM_3) {
                    g_competition_mode_confirmed =
                        g_devices_ready && (g_BRINGUP_Fault == 0U) &&
                        g_peer_link_ready && g_peer_link.status.online;
                    g_display_status_line = 3U;
                    continue;
                }
                if (bringup_launch_hardware_ready(
                        now_ms, imu_navigation_valid)) {
                    bringup_start_tracking(
                        now_ms, &sample_a, &sample_b, yaw_deg);
                    g_competition_mode_confirmed = g_tracking_active;
                }
            }
            if (g_tracking_active) {
                /* Unsigned elapsed time remains valid across Tick wrap. The
                 * last value is retained when the planned stop completes. */
                g_inspection_elapsed_ms =
                    (uint32_t)(now_ms - g_tracking_started_ms);
                bringup_update_tracking(
                    now_ms, dt_seconds, &sample_a, &sample_b,
                    &imu_snapshot, imu_navigation_valid);
                bringup_latch_competition_time(
                    now_ms, bringup_mean_distance(&sample_a, &sample_b));
            }

            if (g_peer_link_ready &&
                ((uint32_t)(now_ms - last_car_state_ms) >=
                    TEMPLATE_PEER_LINK_CAR_STATE_PERIOD_MS)) {
                const Device_PeerLink_CarState state =
                    bringup_make_car_state(
                        now_ms, &sample_a, &sample_b,
                        encoder_samples_valid, &imu_snapshot,
                        imu_navigation_valid);

                last_car_state_ms = now_ms;
                if (!Device_PeerLink_SendCarState(
                        &g_peer_link, &state)) {
                    g_peer_link_fault = 3U;
                }
            }

            if (telemetry_due) {
                last_telemetry_ms = now_ms;
                bringup_send_firewater(
                    now_ms, &sample_a, &sample_b, yaw_deg,
                    imu_snapshot.pitch_deg, imu_valid);
            }
        }
        if (display_tracking_previous && !g_tracking_active) {
            /* Runtime TFT work is completely frozen. On the falling edge,
             * update only the vehicle row immediately with the final time. */
            g_display_status_line = 1U;
            last_display_ms = now_ms;
            bringup_update_display(now_ms, display_imu_valid);
        } else if (!g_tracking_active &&
            ((now_ms - last_display_ms) >=
                TEMPLATE_BRINGUP_DISPLAY_STEP_PERIOD_MS)) {
            last_display_ms = now_ms;
            bringup_update_display(now_ms, display_imu_valid);
        }
        display_tracking_previous = g_tracking_active;
        bringup_show_status();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BRINGUP_TASK_PERIOD_MS));
    }
}

bool Task_BringupChassis_Init(void)
{
    TaskHandle_t task = xTaskCreateStatic(
        Task_BringupChassis,
        BRINGUP_TASK_NAME,
        BRINGUP_TASK_STACK_DEPTH,
        NULL,
        BRINGUP_TASK_PRIORITY,
        g_bringup_stack,
        &g_bringup_tcb);

    if (task == NULL) {
        g_BRINGUP_Fault = 7U;
        return false;
    }
    return true;
}
