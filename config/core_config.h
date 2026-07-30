#ifndef TEMPLATE_CORE_CONFIG_H
#define TEMPLATE_CORE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 模板核心参数唯一入口。
 * 只放源码当前实际消费、需要按产品整定的默认值；固定公式、协议常量、
 * 寄存器值、引脚和时钟不放在这里。各分区注释给出用途、单位与主要约束。
 */

/* ==================== Task 与调度 ==================== */

/* Task identity: used only by the generic control-task skeleton. */
#define TEMPLATE_TASK_CONTROL_NAME "Control"

/* Scheduling: period in milliseconds and priority above tskIDLE_PRIORITY. */
#define TEMPLATE_TASK_CONTROL_PERIOD_MS UINT32_C(10)
#define TEMPLATE_TASK_CONTROL_PRIORITY_OFFSET 2U

/* Static memory: stack depth is measured in FreeRTOS StackType_t entries. */
#define TEMPLATE_TASK_CONTROL_STACK_DEPTH 512U

/* ==================== 首步底盘 Bring-up 测试 ==================== */

/*
 * Final clamp applied to every bring-up motor command. 8000 = 80% duty and
 * must be positive and <= 10000. PWM duty is not current limiting: TB6612 is
 * rated 1.2 A continuous per bridge while MG513X stalls near 3.2 A at 12 V.
 */
#define TEMPLATE_BRINGUP_MOTOR_COMMAND_LIMIT 8000

/*
 * VOFA+ FireWater telemetry period in milliseconds. Each line has 22
 * car/pendulum/raw-grayscale debug channels in a 192-byte static buffer; normal bounded
 * values at 20 ms (50 Hz) fit the 115200-baud budget and 1023-byte TX ring.
 */
#define TEMPLATE_BRINGUP_TELEMETRY_PERIOD_MS UINT32_C(20)

/* TFT checks one of four compact status rows per step. At 50 ms this scans
 * the page in about 200 ms without coupling display work to 50 Hz telemetry. */
#define TEMPLATE_BRINGUP_DISPLAY_STEP_PERIOD_MS UINT32_C(50)

/* Moving-link JY61P is indication/calibration only. Its 0x53 Pitch is added
 * to the chassis ICM45686 Pitch for local CH12 telemetry and never enters
 * navigation or motor control. An older Euler sample is invalid. */
#define TEMPLATE_BRINGUP_JY61P_ANGLE_TIMEOUT_MS UINT32_C(500)

/* Start-key recognition; all values are milliseconds and must be nonzero. */
#define TEMPLATE_BRINGUP_KEY_DEBOUNCE_MS UINT32_C(20)
#define TEMPLATE_BRINGUP_KEY_LONG_PRESS_MS UINT32_C(800)
#define TEMPLATE_BRINGUP_KEY_DOUBLE_CLICK_MS UINT32_C(250)

/*
 * Encoder counting direction per wheel, +1 or -1, so forward motion reads
 * positive. B (right wheel, PB6/PB7) is mounted mirrored: confirmed reversed
 * on hardware 2026-07-29.
 */
#define TEMPLATE_BRINGUP_ENCODER_A_DIRECTION 1
#define TEMPLATE_BRINGUP_ENCODER_B_DIRECTION (-1)

/*
 * Yahboom UART analog line-tracking product policy. The forward target is
 * converted to the normalized Mode drive intention using the shared wheel
 * speed ceiling. A short sample gap is bridged by INS; prolonged loss requests
 * the linear profile stop and cannot energize the chassis after it settles.
 */
#define TEMPLATE_BRINGUP_LINE_BASE_SPEED_MM_PER_SECOND 500.0F
#define TEMPLATE_BRINGUP_LINE_CURVE_SPEED_MM_PER_SECOND 380.0F
/* On the final D->A semicircle, reach each mode's finish-approach cap at the
 * lower edge of the marker window. M2 uses 220 mm/s so its 180 mm marker
 * runout exceeds the current profile braking envelope; other marker modes
 * retain their conservative 100 mm/s cap. Steering differential is applied
 * independently after this longitudinal profile. */
#define TEMPLATE_BRINGUP_ITEM2_FINISH_APPROACH_SPEED_MM_PER_SECOND 220.0F
#define TEMPLATE_BRINGUP_FINISH_APPROACH_SPEED_MM_PER_SECOND 100.0F
#define TEMPLATE_BRINGUP_GRAYSCALE_TIMEOUT_MS UINT32_C(100)
#define TEMPLATE_BRINGUP_LINE_OUTER_WEIGHT 0.70F
#define TEMPLATE_BRINGUP_LINE_MIDDLE_WEIGHT 0.45F
#define TEMPLATE_BRINGUP_LINE_INNER_WEIGHT 0.20F
#define TEMPLATE_BRINGUP_LINE_STEERING_KP 0.65F
/* M2 consumes the complete grayscale steering output. After the finish marker
 * is confirmed, the launch-yaw angle PID is added only during planned braking
 * so the chassis approaches its original heading while it stops. */
#define TEMPLATE_BRINGUP_ITEM2_LINE_GUIDANCE_RATIO 1.0F
/* M2 line-tracking wheel-speed half difference. */
#define TEMPLATE_BRINGUP_ITEM2_MAXIMUM_HALF_DIFFERENCE_MM_PER_SECOND 230.0F

/* Longitudinal motion profile.  Distances/speeds are mm and mm/s; the
 * acceleration limits are mm/s^2.  Guard distance covers encoder quantizing,
 * drivetrain delay and the physical stop offset and must be calibrated on the
 * final chassis before scoring runs. */
#define TEMPLATE_CAR_PROFILE_ACCELERATION_UP_MM_PER_SECOND2 180.0F
#define TEMPLATE_CAR_PROFILE_ACCELERATION_DOWN_MM_PER_SECOND2 185.0F
#define TEMPLATE_CAR_PROFILE_MAXIMUM_SPEED_MM_PER_SECOND 600.0F
#define TEMPLATE_CAR_PROFILE_GUARD_DISTANCE_MM 35.0F
#define TEMPLATE_CAR_PROFILE_PREVIEW_TIME_SECONDS 0.060F
#define TEMPLATE_CAR_PROFILE_STOPPED_SPEED_THRESHOLD_MM_PER_SECOND 10.0F
#define TEMPLATE_BRINGUP_GRAYSCALE_AUXILIARY_HOLD_MS UINT32_C(250)
/* R500赛道的惯导前视。200 mm把切入直线/半圆的提前量限制在0.4R；
 * 600 mm会在理想运动学仿真中产生约160 mm的一圈相位位置误差。 */
#define TEMPLATE_BRINGUP_INS_LOOKAHEAD_MM 200.0F

/* H题赛道：AB/CD 直道各 1.5 m，BC/DA 为半径 0.5 m 的半圆。启动点
 * 默认为 A；M2 直接使用这一圈编码器累计距离作为停车目标，其他路线
 * 模式按各自的终点策略使用。 */
#define TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM 1500.0F
#define TEMPLATE_BRINGUP_H_SEMICIRCLE_RADIUS_MM 500.0F
#define TEMPLATE_BRINGUP_H_SEMICIRCLE_LENGTH_MM \
    (3.14159265359F * TEMPLATE_BRINGUP_H_SEMICIRCLE_RADIUS_MM)
#define TEMPLATE_BRINGUP_H_ONE_LAP_DISTANCE_MM \
    ((2.0F * TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM) + \
        (2.0F * TEMPLATE_BRINGUP_H_SEMICIRCLE_LENGTH_MM))
/* User-confirmed full chassis length. Pass-through items keep this much
 * route after B/A before their planned stop; it is not the finish-sensor
 * offset below. */
#define TEMPLATE_BRINGUP_CAR_LENGTH_MM 320.0F
#define TEMPLATE_BRINGUP_ROUTE_GOAL_DISTANCE_MM \
    TEMPLATE_BRINGUP_H_ONE_LAP_DISTANCE_MM

/* Finish marker policy for the Yahboom raw analog stream. Inside the bounded
 * end window, require both at least six channels >=900 and an eight-channel
 * average >=1700 for two consecutive new frames. The common-mode threshold
 * rejects ordinary narrow-line patterns observed in VOFA. */
#define TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_CENTER_MM 5800.0F
/* Retained physical M2 overshoot measurement. The current one-lap odometry
 * stop does not apply this offset; use it only if a later calibration change
 * is explicitly requested. */
#define TEMPLATE_BRINGUP_M2_STOP_ADVANCE_MM 180.0F
#define TEMPLATE_BRINGUP_FINISH_MARKER_STOP_RUNOUT_MM 0.0F
#define TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_HALF_WIDTH_MM 300.0F
#define TEMPLATE_BRINGUP_FINISH_MARKER_ADC_THRESHOLD UINT16_C(900)
#define TEMPLATE_BRINGUP_FINISH_MARKER_ADC_AVERAGE_THRESHOLD UINT16_C(1700)
#define TEMPLATE_BRINGUP_FINISH_MARKER_MINIMUM_CHANNELS 6U
#define TEMPLATE_BRINGUP_FINISH_MARKER_CONFIRM_SAMPLES 2U

/* RGB indicator brightness 0..255; kept low to limit LED current. */
#define TEMPLATE_BRINGUP_RGB_BRIGHTNESS 32U

/* ==================== MCU 板间 UART 心跳 ==================== */

/* G3519 为节点 2，F407 为节点 1；现有 10 ms Task 每次最多解析 128 字节。 */
#define TEMPLATE_PEER_LINK_LOCAL_NODE_ID UINT8_C(2)
#define TEMPLATE_PEER_LINK_REMOTE_NODE_ID UINT8_C(1)
#define TEMPLATE_PEER_LINK_HEARTBEAT_PERIOD_MS UINT32_C(500)
#define TEMPLATE_PEER_LINK_OFFLINE_TIMEOUT_MS UINT32_C(1500)
#define TEMPLATE_PEER_LINK_RX_BYTES_PER_STEP 128U
/* G3519 submits one latest chassis state to F407 at 100 Hz. */
#define TEMPLATE_PEER_LINK_CAR_STATE_PERIOD_MS UINT32_C(10)

/* ==================== 车辆控制 ==================== */

/*
 * Straight heading PID.
 * Gains preserve the legacy 5 ms discrete tuning. Correction values use the
 * product-defined drive unit; correction ratio is constrained to >= 0.
 */
#define TEMPLATE_CAR_STRAIGHT_KP 1.5F
#define TEMPLATE_CAR_STRAIGHT_KI 0.0F
#define TEMPLATE_CAR_STRAIGHT_KD 0.2F
#define TEMPLATE_CAR_STRAIGHT_GAIN_REFERENCE_SECONDS 0.005F
#define TEMPLATE_CAR_STRAIGHT_INTEGRAL_OUTPUT_LIMIT 300.0F
#define TEMPLATE_CAR_STRAIGHT_MAXIMUM_CORRECTION 200.0F
#define TEMPLATE_CAR_STRAIGHT_CORRECTION_RATIO 0.5F

/*
 * In-place turning PID and arrival limits.
 * Angles are degrees. Drive values use the product-defined drive unit.
 * Minimum drive must not exceed maximum drive; arrival tolerance must not
 * exceed the deceleration zone.
 */
#define TEMPLATE_CAR_TURNING_KP 1.5F
#define TEMPLATE_CAR_TURNING_KI 0.5F
#define TEMPLATE_CAR_TURNING_KD 2.0F
#define TEMPLATE_CAR_TURNING_GAIN_REFERENCE_SECONDS 0.005F
#define TEMPLATE_CAR_TURNING_INTEGRAL_OUTPUT_LIMIT 300.0F
#define TEMPLATE_CAR_TURNING_MAXIMUM_DRIVE 150.0F
#define TEMPLATE_CAR_TURNING_MINIMUM_DRIVE 50.0F
#define TEMPLATE_CAR_TURNING_DECELERATION_ZONE_DEG 45.0F
#define TEMPLATE_CAR_TURNING_ARRIVAL_TOLERANCE_DEG 0.5F

/*
 * Curve cascade controller.
 * The outer loop produces deg/s; the inner loop produces differential drive.
 * Periods are seconds and slew is drive units per second.
 */
#define TEMPLATE_CAR_CURVE_OUTER_KP 100.0F
#define TEMPLATE_CAR_CURVE_OUTER_KI 0.0F
#define TEMPLATE_CAR_CURVE_OUTER_INTEGRAL_OUTPUT_LIMIT 160.0F
#define TEMPLATE_CAR_CURVE_MAXIMUM_HEADING_RATE_DPS 160.0F
#define TEMPLATE_CAR_CURVE_OUTER_PERIOD_SECONDS 0.020F
#define TEMPLATE_CAR_CURVE_INNER_KP 1.5F
#define TEMPLATE_CAR_CURVE_INNER_KI 0.1F
#define TEMPLATE_CAR_CURVE_INNER_INTEGRAL_OUTPUT_LIMIT 100.0F
#define TEMPLATE_CAR_CURVE_MAXIMUM_DRIVE_DIFFERENCE 250.0F
#define TEMPLATE_CAR_CURVE_DRIVE_DIFFERENCE_SLEW_PER_SECOND 700.0F

/* Direct spin command limit in the product-defined drive unit; must be > 0. */
#define TEMPLATE_CAR_SPIN_MAXIMUM_DRIVE 1.0F

/*
 * Yaw-rate PI and feedforward for spin/navigation.
 * Rate inputs are deg/s; drive values use the caller's consistent unit.
 */
#define TEMPLATE_CAR_SPIN_RATE_KP 1.5F
#define TEMPLATE_CAR_SPIN_RATE_KI 0.5F
#define TEMPLATE_CAR_SPIN_RATE_INTEGRAL_OUTPUT_LIMIT 60.0F
#define TEMPLATE_CAR_SPIN_RATE_MAXIMUM_CORRECTION_DRIVE 100.0F
#define TEMPLATE_CAR_SPIN_RATE_FEEDFORWARD_DRIVE_PER_DPS 2.0F
#define TEMPLATE_CAR_SPIN_RATE_MAXIMUM_DRIVE 220.0F

/*
 * Wheel-speed PID bands.
 * Speeds are mm/s, effort values are percent, and D gains act on measured
 * speed. The feedforward slope is percent per (mm/s).
 */
#define TEMPLATE_CAR_SPEED_LOW_KP 0.25F
#define TEMPLATE_CAR_SPEED_LOW_KI 0.30F
#define TEMPLATE_CAR_SPEED_LOW_KD 0.0005F
#define TEMPLATE_CAR_SPEED_HIGH_KP 0.26F
#define TEMPLATE_CAR_SPEED_HIGH_KI 0.30F
#define TEMPLATE_CAR_SPEED_HIGH_KD 0.0005F
#define TEMPLATE_CAR_SPEED_HIGH_THRESHOLD_MM_PER_SECOND 300.0F
#define TEMPLATE_CAR_SPEED_ERROR_DEADBAND_MM_PER_SECOND 2.0F
/* Measured plant estimate: 260 mm/s starts near 14.3 percent effort. */
#define TEMPLATE_CAR_SPEED_FEEDFORWARD_PERCENT_PER_MM_PER_SECOND 0.055F
/* Equivalent to alpha=0.15 at the legacy 5 ms update period. */
#define TEMPLATE_CAR_SPEED_TARGET_FILTER_TIME_CONSTANT_SECONDS 0.030765F
#define TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND 600.0F
#define TEMPLATE_CAR_SPEED_MAXIMUM_EFFORT_PERCENT 80.0F
#define TEMPLATE_CAR_SPEED_STOPPED_THRESHOLD_MM_PER_SECOND 20.0F

/*
 * Absolute-angle outer loop.
 * Output and integral limits are differential wheel speeds in mm/s. Angle
 * tolerance is degrees and rate tolerance is deg/s.
 */
#define TEMPLATE_CAR_ANGLE_KP 4.0F
#define TEMPLATE_CAR_ANGLE_KI 0.05F
#define TEMPLATE_CAR_ANGLE_KD 1.0F
#define TEMPLATE_CAR_ANGLE_INTEGRAL_SPEED_LIMIT_MM_PER_SECOND 10.0F
#define TEMPLATE_CAR_ANGLE_MAXIMUM_WHEEL_SPEED_MM_PER_SECOND 160.0F
#define TEMPLATE_CAR_ANGLE_TOLERANCE_DEG 0.5F
#define TEMPLATE_CAR_ANGLE_RATE_TOLERANCE_DPS 3.0F

/*
 * Differential-drive navigation geometry. Track width is still required by
 * the yaw-rate-to-wheel-speed feedforward and encoder diagnostics. The 26H
 * product uses the calibrated ICM gyro as its only heading source; encoder
 * average distance remains the longitudinal odometer.
 */
/* User-confirmed two-wheel spacing: 20 cm. */
#define TEMPLATE_CAR_NAV_TRACK_WIDTH_MM 200.0F
#define TEMPLATE_CAR_NAV_HEADING_CORRECTION_TIME_CONSTANT_SECONDS 2.0F
#define TEMPLATE_CAR_NAV_ENCODER_HEADING_CORRECTION_ENABLED false
#define TEMPLATE_CAR_NAV_SLIP_GATE_ENABLED false
#define TEMPLATE_CAR_NAV_SLIP_GATE_ENTER_THRESHOLD_DPS 25.0F
#define TEMPLATE_CAR_NAV_SLIP_GATE_EXIT_THRESHOLD_DPS 12.0F
#define TEMPLATE_CAR_NAV_SLIP_GATE_RECOVERY_SAMPLE_COUNT UINT32_C(10)
#define TEMPLATE_CAR_NAV_PURE_PURSUIT_CURVATURE_GAIN 1.0F

/* Navigation yaw-rate controller; drive values are wheel-speed half-difference
 * in mm/s. Feedforward is derived from track width during initialization. */
#define TEMPLATE_CAR_NAV_YAW_RATE_KP 0.8F
#define TEMPLATE_CAR_NAV_YAW_RATE_KI 0.0F
#define TEMPLATE_CAR_NAV_YAW_RATE_INTEGRAL_OUTPUT_LIMIT 0.0F
#define TEMPLATE_CAR_NAV_YAW_RATE_MAXIMUM_CORRECTION_MM_PER_SECOND 10.0F
#define TEMPLATE_CAR_NAV_YAW_RATE_INITIAL_FEEDFORWARD 0.0F
#define TEMPLATE_CAR_NAV_YAW_RATE_MAXIMUM_DRIVE_MM_PER_SECOND 115.0F

/*
 * Brake completion gate.
 * Speed is in the caller's wheel-speed unit, stable time is seconds, and the
 * timeout is milliseconds. Timeout must be in (0, INT32_MAX].
 */
#define TEMPLATE_CAR_BRAKE_STOPPED_SPEED_THRESHOLD 10.0F
#define TEMPLATE_CAR_BRAKE_STABLE_TIME_SECONDS 0.025F
#define TEMPLATE_CAR_BRAKE_TIMEOUT_MS UINT32_C(500)

/* ==================== 循迹算法与控制 ==================== */

/*
 * Difference-over-sum estimator and position filter.
 * Strength and weights are normalized. The low-pass time constant is seconds;
 * zero disables filtering. Epsilon must be > 0 and difference weight [0, 1].
 */
#define TEMPLATE_LINE_ESTIMATOR_DENOMINATOR_EPSILON 0.02F
#define TEMPLATE_LINE_ESTIMATOR_DIFFERENCE_WEIGHT 0.70F
#define TEMPLATE_LINE_ESTIMATOR_LOW_PASS_TIME_CONSTANT_SECONDS 0.015F

/*
 * Car line-tracking PID and safety gates.
 * Drive and correction values are normalized product intentions. Confidence
 * is [0, 1]; stop_on_intersection selects the reusable Mode default only.
 */
#define TEMPLATE_LINE_STEERING_KP 1.2F
#define TEMPLATE_LINE_STEERING_KI 0.0F
#define TEMPLATE_LINE_STEERING_KD 0.03F
#define TEMPLATE_LINE_STEERING_INTEGRAL_OUTPUT_LIMIT 0.5F
#define TEMPLATE_LINE_STEERING_MAXIMUM_CORRECTION 0.8F
#define TEMPLATE_LINE_STEERING_MAXIMUM_DRIVE 1.0F
#define TEMPLATE_LINE_STEERING_ERROR_DEADBAND 0.0F
#define TEMPLATE_LINE_STEERING_STOP_ON_INTERSECTION false
#define TEMPLATE_LINE_ANALOG_MINIMUM_CONFIDENCE 0.10F

/* ==================== 云台控制 ==================== */

/* Shared physical pitch target range in degrees. */
#define TEMPLATE_GIMBAL_PITCH_MINIMUM_DEG 19.0F
#define TEMPLATE_GIMBAL_PITCH_MAXIMUM_DEG 112.0F

/*
 * Encoder pitch angle-loop PID, limits, and feedforward.
 * Angles are degrees, rates are deg/s, and outputs use product-defined current
 * units. Polarity must be non-zero and all magnitude limits non-negative.
 */
#define TEMPLATE_GIMBAL_PITCH_LOOP_KP 0.038F
#define TEMPLATE_GIMBAL_PITCH_LOOP_KI 0.006F
#define TEMPLATE_GIMBAL_PITCH_LOOP_KD_SPEED 0.0045F
#define TEMPLATE_GIMBAL_PITCH_LOOP_POLARITY 1.0F
#define TEMPLATE_GIMBAL_PITCH_LOOP_DEADBAND_DEG 0.3F
#define TEMPLATE_GIMBAL_PITCH_LOOP_MAXIMUM_ERROR_DEG 14.0F
#define TEMPLATE_GIMBAL_PITCH_LOOP_INTEGRAL_SEPARATION_DEG 1.5F
#define TEMPLATE_GIMBAL_PITCH_LOOP_INTEGRAL_LIMIT 0.025F
#define TEMPLATE_GIMBAL_PITCH_LOOP_OUTPUT_LIMIT 0.75F
#define TEMPLATE_GIMBAL_PITCH_LOOP_FEEDFORWARD_THRESHOLD_DEG 0.8F
#define TEMPLATE_GIMBAL_PITCH_LOOP_POSITIVE_FEEDFORWARD 0.0F
#define TEMPLATE_GIMBAL_PITCH_LOOP_NEGATIVE_FEEDFORWARD 0.0F
#define TEMPLATE_GIMBAL_PITCH_LOOP_WRAP_ERROR false
#define TEMPLATE_GIMBAL_PITCH_LOOP_TARGET_LIMIT_ENABLED true

/*
 * Encoder yaw angle-loop PID, limits, and feedforward.
 * Yaw error wraps at +/-180 degrees. Target limits remain disabled, so their
 * placeholder values stay zero and are not exposed as tuning parameters.
 */
#define TEMPLATE_GIMBAL_YAW_LOOP_KP 0.032F
#define TEMPLATE_GIMBAL_YAW_LOOP_KI 0.006F
#define TEMPLATE_GIMBAL_YAW_LOOP_KD_SPEED 0.005F
#define TEMPLATE_GIMBAL_YAW_LOOP_POLARITY 1.0F
#define TEMPLATE_GIMBAL_YAW_LOOP_DEADBAND_DEG 0.35F
#define TEMPLATE_GIMBAL_YAW_LOOP_MAXIMUM_ERROR_DEG 8.0F
#define TEMPLATE_GIMBAL_YAW_LOOP_INTEGRAL_SEPARATION_DEG 2.0F
#define TEMPLATE_GIMBAL_YAW_LOOP_INTEGRAL_LIMIT 0.015F
#define TEMPLATE_GIMBAL_YAW_LOOP_OUTPUT_LIMIT 0.32F
#define TEMPLATE_GIMBAL_YAW_LOOP_FEEDFORWARD_THRESHOLD_DEG 0.8F
#define TEMPLATE_GIMBAL_YAW_LOOP_POSITIVE_FEEDFORWARD 0.0F
#define TEMPLATE_GIMBAL_YAW_LOOP_NEGATIVE_FEEDFORWARD 0.0F
#define TEMPLATE_GIMBAL_YAW_LOOP_WRAP_ERROR true
#define TEMPLATE_GIMBAL_YAW_LOOP_TARGET_LIMIT_ENABLED false

/* Encoder-lock acquisition timeout in milliseconds; must be > 0. */
#define TEMPLATE_GIMBAL_FEEDBACK_TIMEOUT_MS UINT32_C(100)

/*
 * IMU pitch stabilization PID and rate filter.
 * Filter old-weight is in [0, 1); larger values retain more prior rate. The
 * yaw mapping polarity converts IMU yaw error to an encoder target increment.
 */
#define TEMPLATE_GIMBAL_STAB_PITCH_KP 0.030F
#define TEMPLATE_GIMBAL_STAB_PITCH_KI 0.0F
#define TEMPLATE_GIMBAL_STAB_PITCH_KD_SPEED 0.0055F
#define TEMPLATE_GIMBAL_STAB_PITCH_POLARITY 1.0F
#define TEMPLATE_GIMBAL_STAB_PITCH_DEADBAND_DEG 0.3F
#define TEMPLATE_GIMBAL_STAB_PITCH_MAXIMUM_ERROR_DEG 16.0F
#define TEMPLATE_GIMBAL_STAB_PITCH_INTEGRAL_LIMIT 0.0F
#define TEMPLATE_GIMBAL_STAB_PITCH_OUTPUT_LIMIT 0.75F
#define TEMPLATE_GIMBAL_STAB_PITCH_RATE_FILTER_OLD_WEIGHT 0.3F
#define TEMPLATE_GIMBAL_STAB_YAW_ENCODER_FROM_IMU_POLARITY -1.0F

/*
 * Vision input timing, clipping, deadbands, and exponential filter.
 * Time is seconds/ms as named, pixel values are image-space error magnitudes,
 * decay and alpha values are in [0, 1], and polarities must be non-zero.
 */
#define TEMPLATE_GIMBAL_VISION_NOMINAL_CONTROL_PERIOD_SECONDS 0.010F
#define TEMPLATE_GIMBAL_VISION_INPUT_TIMEOUT_MS UINT32_C(250)
#define TEMPLATE_GIMBAL_VISION_MAXIMUM_DX_PIXELS 180.0F
#define TEMPLATE_GIMBAL_VISION_MAXIMUM_DY_PIXELS 140.0F
#define TEMPLATE_GIMBAL_VISION_YAW_DEADBAND_PIXELS 2.0F
#define TEMPLATE_GIMBAL_VISION_PITCH_DEADBAND_PIXELS 2.0F
#define TEMPLATE_GIMBAL_VISION_YAW_INTEGRAL_DEADBAND_PIXELS 1.0F
#define TEMPLATE_GIMBAL_VISION_PITCH_INTEGRAL_DEADBAND_PIXELS 1.0F
#define TEMPLATE_GIMBAL_VISION_CENTER_INTEGRAL_DECAY 0.90F
#define TEMPLATE_GIMBAL_VISION_REVERSAL_INTEGRAL_DECAY 0.50F
#define TEMPLATE_GIMBAL_VISION_INPUT_FILTER_ALPHA 0.72F
#define TEMPLATE_GIMBAL_VISION_YAW_PIXEL_POLARITY 1.0F
#define TEMPLATE_GIMBAL_VISION_PITCH_PIXEL_POLARITY 1.0F

/*
 * Vision PID-like tuning for normal targets.
 * I/D gains are normalized to the nominal control step; integral limits and
 * maximum steps are degrees of accumulated/commanded target offset.
 */
#define TEMPLATE_GIMBAL_VISION_NORMAL_YAW_KP 0.00018F
#define TEMPLATE_GIMBAL_VISION_NORMAL_YAW_KI_PER_STEP 0.000002F
#define TEMPLATE_GIMBAL_VISION_NORMAL_YAW_KD_PER_STEP 0.0F
#define TEMPLATE_GIMBAL_VISION_NORMAL_YAW_INTEGRAL_LIMIT 0.004F
#define TEMPLATE_GIMBAL_VISION_NORMAL_YAW_MAXIMUM_STEP_DEG 0.010F
#define TEMPLATE_GIMBAL_VISION_NORMAL_PITCH_KP 0.00018F
#define TEMPLATE_GIMBAL_VISION_NORMAL_PITCH_KI_PER_STEP 0.000003F
#define TEMPLATE_GIMBAL_VISION_NORMAL_PITCH_KD_PER_STEP 0.0F
#define TEMPLATE_GIMBAL_VISION_NORMAL_PITCH_INTEGRAL_LIMIT 0.006F
#define TEMPLATE_GIMBAL_VISION_NORMAL_PITCH_MAXIMUM_STEP_DEG 0.012F

/* Vision tuning for circle targets; units and constraints match normal mode. */
#define TEMPLATE_GIMBAL_VISION_CIRCLE_YAW_KP 0.00024F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_YAW_KI_PER_STEP 0.0000012F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_YAW_KD_PER_STEP 0.000030F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_YAW_INTEGRAL_LIMIT 0.009F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_YAW_MAXIMUM_STEP_DEG 0.022F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_PITCH_KP 0.00027F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_PITCH_KI_PER_STEP 0.0000018F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_PITCH_KD_PER_STEP 0.000052F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_PITCH_INTEGRAL_LIMIT 0.014F
#define TEMPLATE_GIMBAL_VISION_CIRCLE_PITCH_MAXIMUM_STEP_DEG 0.028F

/* Lost-target scan starts after delay_ms, spans +/-degrees, and moves deg/s. */
#define TEMPLATE_GIMBAL_VISION_LOST_SCAN_DELAY_MS UINT32_C(500)
#define TEMPLATE_GIMBAL_VISION_LOST_SCAN_HALF_SPAN_DEG 45.0F
#define TEMPLATE_GIMBAL_VISION_LOST_SCAN_RATE_DPS 8.0F

/*
 * Identified gravity feedforward model:
 * current = sin_coeff*sin(angle) + cos_coeff*cos(angle) + offset.
 * Coefficients use product-defined current units; maximum current must be > 0.
 */
#define TEMPLATE_GIMBAL_GRAVITY_SIN_COEFFICIENT 0.0300124F
#define TEMPLATE_GIMBAL_GRAVITY_COS_COEFFICIENT 0.0200932F
#define TEMPLATE_GIMBAL_GRAVITY_OFFSET 0.0038665F
#define TEMPLATE_GIMBAL_GRAVITY_MAXIMUM_CURRENT 0.6F

/*
 * Gravity calibration sweep.
 * Endpoint margin is degrees inside the shared pitch range; settle/sample
 * durations are milliseconds and must both be > 0.
 */
#define TEMPLATE_GIMBAL_GRAVITY_ENDPOINT_MARGIN_DEG 6.0F
#define TEMPLATE_GIMBAL_GRAVITY_SETTLE_TIME_MS UINT32_C(1200)
#define TEMPLATE_GIMBAL_GRAVITY_SAMPLE_TIME_MS UINT32_C(900)

/* ==================== Mahony 姿态估计 ==================== */

/*
 * Mahony attitude correction gains.
 * Gains are non-negative. Acceleration values are m/s^2 and maximum dt is
 * seconds; samples outside the acceleration band skip gravity correction.
 */
#define TEMPLATE_MAHONY_PROPORTIONAL_GAIN 2.0F
#define TEMPLATE_MAHONY_INTEGRAL_GAIN 0.05F
#define TEMPLATE_MAHONY_ACCELERATION_REFERENCE_MPS2 9.80665F
#define TEMPLATE_MAHONY_ACCELERATION_TOLERANCE_MPS2 3.0F
#define TEMPLATE_MAHONY_MAXIMUM_DT_SECONDS 0.050F

/* Initial gyroscope bias in rad/s for body X/Y/Z. */
#define TEMPLATE_MAHONY_GYRO_BIAS_X_RAD_PER_SECOND 0.0F
#define TEMPLATE_MAHONY_GYRO_BIAS_Y_RAD_PER_SECOND 0.0F
#define TEMPLATE_MAHONY_GYRO_BIAS_Z_RAD_PER_SECOND 0.0F

/* ==================== 双 IMU 融合与 Kalman ==================== */

/*
 * Sensor-to-body mapping and secondary-IMU lever arm.
 * Directions must be +1 or -1. Positions are metres in the body frame and
 * are product mounting facts, not MCU pin mappings.
 */
#define TEMPLATE_IMU_FUSION_BODY_X_SOURCE ALGORITHM_IMU_FUSION_SENSOR_AXIS_X
#define TEMPLATE_IMU_FUSION_BODY_Y_SOURCE ALGORITHM_IMU_FUSION_SENSOR_AXIS_Y
#define TEMPLATE_IMU_FUSION_BODY_Z_SOURCE ALGORITHM_IMU_FUSION_SENSOR_AXIS_Z
#define TEMPLATE_IMU_FUSION_BODY_X_DIRECTION 1
#define TEMPLATE_IMU_FUSION_BODY_Y_DIRECTION 1
#define TEMPLATE_IMU_FUSION_BODY_Z_DIRECTION 1
#define TEMPLATE_IMU_FUSION_SECONDARY_POSITION_X_M 0.035F
#define TEMPLATE_IMU_FUSION_SECONDARY_POSITION_Y_M 0.0F
#define TEMPLATE_IMU_FUSION_SECONDARY_POSITION_Z_M 0.0F

/*
 * Dual-sensor variance and prediction tuning.
 * Gyro variance is (deg/s)^2, acceleration variance is (m/s^2)^2, and gyro
 * rate process noise is (deg/s)^2 per second. All variances are >= 0.
 */
#define TEMPLATE_IMU_FUSION_PRIMARY_GYRO_VARIANCE_DPS2 1.0F
#define TEMPLATE_IMU_FUSION_SECONDARY_GYRO_VARIANCE_DPS2 1.0F
#define TEMPLATE_IMU_FUSION_PRIMARY_ACCEL_VARIANCE_MPS4 1.0F
#define TEMPLATE_IMU_FUSION_SECONDARY_ACCEL_VARIANCE_MPS4 1.0F
#define TEMPLATE_IMU_FUSION_GYRO_RATE_PROCESS_NOISE_DPS2_PER_SECOND 0.05F
#define TEMPLATE_IMU_FUSION_PREDICTION_ADAPTATION_GAIN 1.0F
#define TEMPLATE_IMU_FUSION_INITIAL_GYRO_VARIANCE_DPS2 1.0F
#define TEMPLATE_IMU_FUSION_MAXIMUM_GYRO_VARIANCE_DPS2 2500.0F

/*
 * Measurement confidence gates and time-domain filters.
 * Acceleration values are m/s^2, rates are deg/s, and time constants/dt are
 * seconds. These thresholds decide whether a measurement can correct state.
 */
#define TEMPLATE_IMU_FUSION_GRAVITY_MPS2 9.80665F
#define TEMPLATE_IMU_FUSION_ACCEL_NORM_TOLERANCE_MPS2 3.0F
#define TEMPLATE_IMU_FUSION_MAXIMUM_GYRO_DISAGREEMENT_DPS 30.0F
#define TEMPLATE_IMU_FUSION_MAXIMUM_ACCEL_DISAGREEMENT_MPS2 6.0F
#define TEMPLATE_IMU_FUSION_MAXIMUM_GRAVITY_INNOVATION 0.75F
#define TEMPLATE_IMU_FUSION_ANGULAR_ACCEL_FILTER_TIME_CONSTANT_SECONDS 0.05F
#define TEMPLATE_IMU_FUSION_MAXIMUM_DT_SECONDS 0.050F

/*
 * Startup calibration gates.
 * Sample counts are calls, acceleration tolerance is m/s^2, gyro thresholds
 * are deg/s, and the calibration filter time constant is seconds.
 */
#define TEMPLATE_IMU_FUSION_CALIBRATION_SETTLE_SAMPLES UINT32_C(100)
#define TEMPLATE_IMU_FUSION_CALIBRATION_REQUIRED_SAMPLES UINT32_C(400)
#define TEMPLATE_IMU_FUSION_CALIBRATION_ACCEL_TOLERANCE_MPS2 0.75F
#define TEMPLATE_IMU_FUSION_CALIBRATION_GYRO_MAXIMUM_DPS 5.0F
#define TEMPLATE_IMU_FUSION_CALIBRATION_GYRO_STDDEV_MAXIMUM_DPS 0.6F
#define TEMPLATE_IMU_FUSION_CALIBRATION_FILTER_TIME_CONSTANT_SECONDS 0.5F
#define TEMPLATE_IMU_FUSION_ENABLE_STARTUP_BIAS_TREND_FIT false
#define TEMPLATE_IMU_FUSION_ENABLE_STARTUP_GYRO_BIAS_CALIBRATION false

/*
 * Runtime bias adaptation.
 * EMA alpha is [0, 1], confirmation is consecutive samples, and the filter
 * time constant is seconds. Disabled defaults preserve existing behavior.
 */
#define TEMPLATE_IMU_FUSION_ENABLE_RUNTIME_GYRO_BIAS_UPDATE false
#define TEMPLATE_IMU_FUSION_RUNTIME_BIAS_EMA_ALPHA 0.0015F
#define TEMPLATE_IMU_FUSION_RUNTIME_BIAS_CONFIRM_SAMPLES UINT32_C(20)
#define TEMPLATE_IMU_FUSION_RUNTIME_BIAS_FILTER_TIME_CONSTANT_SECONDS 0.5F

/*
 * Error-state Kalman covariance tuning.
 * Process and initial covariance values use the units stated in each name;
 * gravity measurement variance is dimensionless normalized-vector variance.
 */
#define TEMPLATE_IMU_FUSION_GYRO_PROCESS_NOISE_RAD2_PER_SECOND 0.02F
#define TEMPLATE_IMU_FUSION_GYRO_BIAS_PROCESS_NOISE_RAD2_PER_SECOND3 1.0e-6F
#define TEMPLATE_IMU_FUSION_GRAVITY_MEASUREMENT_VARIANCE 0.02F
#define TEMPLATE_IMU_FUSION_INITIAL_ATTITUDE_VARIANCE_RAD2 0.25F
#define TEMPLATE_IMU_FUSION_INITIAL_BIAS_VARIANCE_RAD2_PER_SECOND2 0.01F
#define TEMPLATE_IMU_FUSION_ESTIMATE_GYRO_BIAS false

#endif
