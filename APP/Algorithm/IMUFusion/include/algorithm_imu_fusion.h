#ifndef ALGORITHM_IMU_FUSION_H
#define ALGORITHM_IMU_FUSION_H

#include "algorithm_mahony.h"

#include <stdbool.h>
#include <stdint.h>

#define ALGORITHM_IMU_FUSION_AXIS_COUNT 3U
#define ALGORITHM_IMU_FUSION_STATE_COUNT 6U

typedef enum {
    ALGORITHM_IMU_FUSION_SENSOR_AXIS_X = 0,
    ALGORITHM_IMU_FUSION_SENSOR_AXIS_Y,
    ALGORITHM_IMU_FUSION_SENSOR_AXIS_Z,
} Algorithm_IMUFusion_SensorAxis;

typedef struct {
    Algorithm_IMUFusion_SensorAxis source;
    int8_t direction;
} Algorithm_IMUFusion_AxisMapping;

typedef enum {
    ALGORITHM_IMU_FUSION_CALIBRATION_DISABLED = 0,
    ALGORITHM_IMU_FUSION_CALIBRATION_SETTLING,
    ALGORITHM_IMU_FUSION_CALIBRATION_COLLECTING,
    ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE,
} Algorithm_IMUFusion_CalibrationStatus;

typedef struct {
    Algorithm_IMUFusion_AxisMapping primary_body_axis[3];
    Algorithm_IMUFusion_AxisMapping secondary_body_axis[3];

    /* Secondary sensor position relative to the primary sensor, expressed in
     * the common body frame. Units are metres. */
    float secondary_position_m[3];

    /* Only relative values matter for the inverse-variance measurement
     * fusion. They must remain finite and strictly positive. */
    float primary_gyro_variance_dps2;
    float secondary_gyro_variance_dps2;
    float primary_acceleration_variance_mps4;
    float secondary_acceleration_variance_mps4;

    /* Scalar Kalman confidence model for the common angular-rate state.
     * Sensor variances are the measurement-noise baselines. The predicted
     * variance grows with process noise and with a shared measurement
     * innovation, so genuine fast motion shifts weight away from the prior. */
    float gyro_rate_process_noise_dps2_per_second;
    float gyro_rate_prediction_adaptation_gain;
    float initial_gyro_rate_variance_dps2;
    float maximum_gyro_rate_prediction_variance_dps2;

    float gravity_mps2;
    float acceleration_norm_tolerance_mps2;
    float maximum_gyro_disagreement_dps;
    float maximum_acceleration_disagreement_mps2;
    float maximum_gravity_direction_innovation;
    float angular_acceleration_time_constant_seconds;
    float maximum_dt_seconds;

    /* Optional per-boot, fixed gyro-bias calibration. Samples are mapped to
     * the common body frame before the two independent sensor means are
     * accumulated. Motion or a missing sensor restarts the consecutive
     * window. The committed biases are never updated online. */
    uint32_t startup_calibration_settle_samples;
    uint32_t startup_calibration_required_samples;
    float calibration_acceleration_tolerance_mps2;
    float calibration_gyro_maximum_dps;
    float calibration_gyro_stddev_maximum_dps;
    float calibration_filter_time_constant_seconds;
    bool enable_startup_bias_trend_fit;
    bool enable_startup_gyro_bias_calibration;

    /* Optional runtime gyro-bias refinement for long dead-reckoning runs.
     * While the caller reports mechanical stillness (odometry_stationary)
     * and every valid sensor passes the calibration stationarity gates for
     * runtime_bias_confirm_samples consecutive updates, each valid sensor
     * bias absorbs runtime_bias_ema_alpha of its exponentially averaged
     * rate residual per update. This only refines biases committed by the
     * startup calibration; it never sets gyro_bias_valid on its own. */
    bool enable_runtime_gyro_bias_update;
    float runtime_bias_ema_alpha;
    uint32_t runtime_bias_confirm_samples;
    float runtime_bias_filter_time_constant_seconds;

    /* Continuous-time covariance densities used by the six-state error
     * model [attitude error, gyro bias]. Phase one requires
     * estimate_gyro_bias=false; true is rejected until drift removal is
     * enabled and validated as a separate phase. */
    float gyro_process_noise_rad2_per_second;
    float gyro_bias_process_noise_rad2_per_second3;
    float gravity_direction_measurement_variance;
    float initial_attitude_variance_rad2;
    float initial_bias_variance_rad2_per_second2;
    bool estimate_gyro_bias;
} Algorithm_IMUFusion_Config;

typedef struct {
    float primary_acceleration_mps2[3];
    float primary_angular_rate_dps[3];
    float secondary_acceleration_mps2[3];
    float secondary_angular_rate_dps[3];
    float dt_seconds;
    bool primary_valid;
    bool secondary_valid;
    /* External mechanical-stillness confirmation for the runtime bias
     * update, typically both wheel encoders reporting no counts. Platforms
     * without odometry may pass true so the gate falls back to the IMU-only
     * stationarity thresholds. Ignored while the runtime update is off. */
    bool odometry_stationary;
} Algorithm_IMUFusion_Input;

typedef struct {
    Algorithm_Mahony_Quaternion quaternion;
    Algorithm_Mahony_Euler euler;
    float acceleration_mps2[3];
    float angular_rate_dps[3];
    float gyro_bias_dps[3];
    float primary_gyro_bias_dps[3];
    float secondary_gyro_bias_dps[3];
    float primary_gyro_bias_fit_slope_dps_per_second[3];
    float secondary_gyro_bias_fit_slope_dps_per_second[3];
    float gyro_prediction_weight[3];
    float primary_gyro_weight[3];
    float secondary_gyro_weight[3];
    float secondary_lever_acceleration_mps2[3];
    uint32_t update_count;
    uint32_t calibration_settle_samples;
    uint32_t calibration_samples_collected;
    uint32_t calibration_restart_count;
    uint32_t runtime_bias_update_count;
    uint8_t gyro_source_mask;
    uint8_t acceleration_source_mask;
    Algorithm_IMUFusion_CalibrationStatus calibration_status;
    bool acceleration_correction_used;
    bool secondary_lever_compensation_used;
    bool calibration_sample_rejected;
    bool gyro_bias_valid;
    bool runtime_bias_stationary;
    bool yaw_absolute_observable;
    bool valid;
} Algorithm_IMUFusion_Snapshot;

typedef struct {
    Algorithm_IMUFusion_Config config;
    Algorithm_IMUFusion_Snapshot snapshot;
    float covariance[ALGORITHM_IMU_FUSION_STATE_COUNT]
                    [ALGORITHM_IMU_FUSION_STATE_COUNT];
    float previous_angular_rate_rad_per_second[3];
    float angular_acceleration_rad_per_second2[3];
    float primary_calibration_mean_dps[3];
    float primary_calibration_m2_dps2[3];
    float secondary_calibration_mean_dps[3];
    float secondary_calibration_m2_dps2[3];
    float primary_calibration_filtered_dps[3];
    float secondary_calibration_filtered_dps[3];
    float primary_calibration_fit_mean_dps[3];
    float secondary_calibration_fit_mean_dps[3];
    float primary_calibration_fit_cross_dps_seconds[3];
    float secondary_calibration_fit_cross_dps_seconds[3];
    float calibration_fit_mean_time_seconds;
    float calibration_fit_time_m2_seconds2;
    float calibration_fit_elapsed_seconds;
    float calibration_fit_last_time_seconds;
    float gyro_rate_estimate_dps[3];
    float gyro_rate_variance_dps2[3];
    float previous_attitude_angular_rate_rad_per_second[3];
    float runtime_primary_residual_mean_dps[3];
    float runtime_primary_residual_var_dps2[3];
    float runtime_secondary_residual_mean_dps[3];
    float runtime_secondary_residual_var_dps2[3];
    uint32_t runtime_stationary_samples;
    bool runtime_primary_seeded;
    bool runtime_secondary_seeded;
    bool has_previous_angular_rate;
    bool has_previous_attitude_angular_rate;
    bool gyro_rate_filter_seeded;
    bool calibration_filter_seeded;
    bool attitude_seeded;
    bool initialized;
} Algorithm_IMUFusion;

Algorithm_IMUFusion_Config Algorithm_IMUFusion_DefaultConfig(void);
bool Algorithm_IMUFusion_Init(
    Algorithm_IMUFusion *fusion,
    const Algorithm_IMUFusion_Config *config);

/* Both valid sensor vectors are mapped to one right-handed body frame before
 * fusion. If one sensor is invalid, the other can carry the update. The
 * update is transactional: invalid dt, nonfinite data or numerical failure
 * leaves both context and output unchanged. A six-axis solution never makes
 * absolute yaw observable. */
bool Algorithm_IMUFusion_Update(
    Algorithm_IMUFusion *fusion,
    const Algorithm_IMUFusion_Input *input,
    Algorithm_IMUFusion_Snapshot *snapshot);

#endif
