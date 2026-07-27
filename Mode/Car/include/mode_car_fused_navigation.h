#ifndef MODE_CAR_FUSED_NAVIGATION_H
#define MODE_CAR_FUSED_NAVIGATION_H

#include "mode_car.h"
#include "mode_car_control.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float x_mm;
    float y_mm;
    /* Continuous local heading. It is intentionally not wrapped at +/-180. */
    float yaw_deg;
} Mode_Car_FusedNavigationPose;

typedef struct {
    /* Effective left/right contact-patch spacing used by differential
     * odometry. This is a calibrated physical value, not a BSP constant. */
    float track_width_mm;
    /* Encoder heading is the low-frequency reference for gyro integration.
     * Zero selects encoder heading immediately; positive values apply a
     * first-order complementary correction with this time constant. */
    float heading_correction_time_constant_seconds;
    /* False keeps encoder heading as diagnostic data but propagates pose yaw
     * only from the bias-corrected gyro. */
    bool encoder_heading_correction_enabled;
    /* When enabled, reject encoder heading correction while the encoder and
     * gyro yaw rates disagree. Enter must be greater than exit; recovery
     * requires this many consecutive samples at or below the exit threshold. */
    bool slip_gate_enabled;
    float slip_gate_enter_threshold_deg_per_second;
    float slip_gate_exit_threshold_deg_per_second;
    uint32_t slip_gate_recovery_sample_count;
    /* XY lookahead produces drive-axle curvature after compensating the
     * longitudinal tracking-point offset. One keeps the geometric result. */
    float pure_pursuit_curvature_gain;
    Mode_Car_SpinRateConfig yaw_rate_controller;
    Mode_Car_SpeedControllerConfig speed_controller;
} Mode_Car_FusedNavigationConfig;

typedef struct {
    float dt_seconds;
    /* All wheel quantities use physical-forward-positive millimetres. */
    float left_distance_delta_mm;
    float right_distance_delta_mm;
    float measured_left_speed_mm_per_second;
    float measured_right_speed_mm_per_second;
    /* Bias-corrected body yaw rate. Positive must agree with
     * (right_distance-left_distance)/track_width. */
    float measured_yaw_rate_deg_per_second;
    /* The planned path is referenced to a point in front of the drive axle.
     * Task supplies that longitudinal offset and a forward lookahead point. */
    float tracking_point_offset_mm;
    float lookahead_x_mm;
    float lookahead_y_mm;
    float forward_speed_mm_per_second;
} Mode_Car_FusedNavigationInput;

typedef struct {
    Mode_Car_FusedNavigationPose pose;
    float forward_distance_delta_mm;
    float encoder_heading_delta_deg;
    float gyro_heading_delta_deg;
    float heading_innovation_deg;
    float encoder_yaw_rate_deg_per_second;
    float slip_residual_deg_per_second;
    float tracking_x_mm;
    float tracking_y_mm;
    float target_heading_deg;
    float heading_error_deg;
    float target_yaw_rate_deg_per_second;
    float turn_speed_mm_per_second;
    float target_left_speed_mm_per_second;
    float target_right_speed_mm_per_second;
    float effort_left_percent;
    float effort_right_percent;
    bool slip_gate_active;
} Mode_Car_FusedNavigationOutput;

typedef struct {
    Mode_Car_FusedNavigationConfig config;
    Mode_Car_FusedNavigationPose pose;
    /* Raw encoder-only integral retained for diagnostics. */
    float encoder_heading_deg;
    /* Accepted low-frequency reference. It follows the gyro only while the
     * slip gate rejects encoder heading increments. */
    float heading_reference_deg;
    Mode_Car_SpinRateContext yaw_rate_controller;
    Mode_Car_SpeedController speed_controller;
    uint32_t slip_gate_recovery_sample_count;
    bool slip_gate_active;
    bool initialized;
} Mode_Car_FusedNavigation;

Mode_Car_FusedNavigationConfig
Mode_Car_FusedNavigation_DefaultConfig(void);
bool Mode_Car_FusedNavigation_Init(
    Mode_Car_FusedNavigation *mode,
    const Mode_Car_FusedNavigationConfig *config);
bool Mode_Car_FusedNavigation_ResetPose(
    Mode_Car_FusedNavigation *mode,
    const Mode_Car_FusedNavigationPose *pose);
bool Mode_Car_FusedNavigation_Update(
    Mode_Car_FusedNavigation *mode,
    const Mode_Car_FusedNavigationInput *input,
    Mode_Car_FusedNavigationOutput *output);

#endif
