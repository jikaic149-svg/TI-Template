#include "algorithm_imu_fusion.h"
#include "core_config.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#define IMU_FUSION_PI 3.14159265358979323846F
#define IMU_FUSION_DEG_TO_RAD (IMU_FUSION_PI / 180.0F)
#define IMU_FUSION_RAD_TO_DEG (180.0F / IMU_FUSION_PI)
#define IMU_FUSION_STATE_COUNT ((unsigned int)ALGORITHM_IMU_FUSION_STATE_COUNT)
#define IMU_FUSION_AXIS_COUNT 3U
#define IMU_FUSION_EPSILON 1.0e-12F
#define IMU_FUSION_MIN_GRAVITY_CONFIDENCE 0.05F
#define IMU_FUSION_MIN_CALIBRATED_GYRO_VARIANCE_RATIO 0.10F
#define IMU_FUSION_MAX_DT_SECONDS 0.050F

static bool finite3(const float value[3])
{
    return (value != NULL) && isfinite(value[0]) && isfinite(value[1]) &&
        isfinite(value[2]);
}

static bool finite4(const Algorithm_Mahony_Quaternion *value)
{
    return (value != NULL) && isfinite(value->w) && isfinite(value->x) &&
        isfinite(value->y) && isfinite(value->z);
}

static float dot3(const float lhs[3], const float rhs[3])
{
    return (lhs[0] * rhs[0]) + (lhs[1] * rhs[1]) + (lhs[2] * rhs[2]);
}

static float norm3(const float value[3])
{
    const float squared = dot3(value, value);

    return isfinite(squared) && (squared >= 0.0F) ? sqrtf(squared) : NAN;
}

static void cross3(const float lhs[3], const float rhs[3], float result[3])
{
    const float x = (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]);
    const float y = (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]);
    const float z = (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]);

    result[0] = x;
    result[1] = y;
    result[2] = z;
}

static void map_vector(
    const Algorithm_IMUFusion_AxisMapping mapping[3],
    const float sensor[3],
    float body[3])
{
    unsigned int axis;

    for (axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        body[axis] = (float)mapping[axis].direction *
            sensor[(unsigned int)mapping[axis].source];
    }
}

static bool valid_axis_mapping(
    const Algorithm_IMUFusion_AxisMapping mapping[3])
{
    int matrix[3][3] = {{0}};
    unsigned int axis;
    unsigned int source;
    int source_value;
    unsigned int used = 0U;
    int determinant;

    if (mapping == NULL) {
        return false;
    }
    for (axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        source_value = (int)mapping[axis].source;
        if ((source_value < 0) ||
            (source_value > (int)ALGORITHM_IMU_FUSION_SENSOR_AXIS_Z) ||
            ((mapping[axis].direction != 1) &&
             (mapping[axis].direction != -1))) {
            return false;
        }
        source = (unsigned int)source_value;
        if ((used & (1U << source)) != 0U) {
            return false;
        }
        used |= 1U << source;
        matrix[axis][source] = (int)mapping[axis].direction;
    }

    /* A signed permutation with determinant +1 is the only mapping that
     * preserves the right-handed body frame.  A mirrored frame would invert
     * one of the cross products used by the attitude model. */
    determinant =
        matrix[0][0] *
            ((matrix[1][1] * matrix[2][2]) -
             (matrix[1][2] * matrix[2][1])) -
        matrix[0][1] *
            ((matrix[1][0] * matrix[2][2]) -
             (matrix[1][2] * matrix[2][0])) +
        matrix[0][2] *
            ((matrix[1][0] * matrix[2][1]) -
             (matrix[1][1] * matrix[2][0]));
    return (used == 0x07U) && (determinant == 1);
}

static bool normalize3(float value[3])
{
    const float length = norm3(value);

    if (!isfinite(length) || (length <= IMU_FUSION_EPSILON)) {
        return false;
    }
    value[0] /= length;
    value[1] /= length;
    value[2] /= length;
    return finite3(value);
}

static bool normalize_quaternion(Algorithm_Mahony_Quaternion *quaternion)
{
    float squared;
    float inverse;

    if (!finite4(quaternion)) {
        return false;
    }
    squared = (quaternion->w * quaternion->w) +
        (quaternion->x * quaternion->x) +
        (quaternion->y * quaternion->y) +
        (quaternion->z * quaternion->z);
    if (!isfinite(squared) || (squared <= IMU_FUSION_EPSILON)) {
        return false;
    }
    inverse = 1.0F / sqrtf(squared);
    if (!isfinite(inverse)) {
        return false;
    }
    quaternion->w *= inverse;
    quaternion->x *= inverse;
    quaternion->y *= inverse;
    quaternion->z *= inverse;
    return finite4(quaternion);
}

static bool quaternion_to_euler(
    const Algorithm_Mahony_Quaternion *quaternion,
    Algorithm_Mahony_Euler *euler)
{
    float pitch_sine;

    if (!finite4(quaternion) || (euler == NULL)) {
        return false;
    }
    pitch_sine = 2.0F * ((quaternion->w * quaternion->y) -
        (quaternion->z * quaternion->x));
    if (pitch_sine > 1.0F) {
        pitch_sine = 1.0F;
    } else if (pitch_sine < -1.0F) {
        pitch_sine = -1.0F;
    }
    euler->roll_deg = IMU_FUSION_RAD_TO_DEG * atan2f(
        2.0F * ((quaternion->w * quaternion->x) +
            (quaternion->y * quaternion->z)),
        1.0F - 2.0F * ((quaternion->x * quaternion->x) +
            (quaternion->y * quaternion->y)));
    euler->pitch_deg = IMU_FUSION_RAD_TO_DEG * asinf(pitch_sine);
    euler->yaw_deg = IMU_FUSION_RAD_TO_DEG * atan2f(
        2.0F * ((quaternion->w * quaternion->z) +
            (quaternion->x * quaternion->y)),
        1.0F - 2.0F * ((quaternion->y * quaternion->y) +
            (quaternion->z * quaternion->z)));
    return isfinite(euler->roll_deg) && isfinite(euler->pitch_deg) &&
        isfinite(euler->yaw_deg);
}

static float quaternion_yaw_radians(
    const Algorithm_Mahony_Quaternion *quaternion)
{
    if (!finite4(quaternion)) {
        return NAN;
    }
    return atan2f(
        2.0F * ((quaternion->w * quaternion->z) +
            (quaternion->x * quaternion->y)),
        1.0F - 2.0F * ((quaternion->y * quaternion->y) +
            (quaternion->z * quaternion->z)));
}

static void quaternion_multiply(
    const Algorithm_Mahony_Quaternion *lhs,
    const Algorithm_Mahony_Quaternion *rhs,
    Algorithm_Mahony_Quaternion *result)
{
    Algorithm_Mahony_Quaternion next;

    next.w = (lhs->w * rhs->w) - (lhs->x * rhs->x) -
        (lhs->y * rhs->y) - (lhs->z * rhs->z);
    next.x = (lhs->w * rhs->x) + (lhs->x * rhs->w) +
        (lhs->y * rhs->z) - (lhs->z * rhs->y);
    next.y = (lhs->w * rhs->y) - (lhs->x * rhs->z) +
        (lhs->y * rhs->w) + (lhs->z * rhs->x);
    next.z = (lhs->w * rhs->z) + (lhs->x * rhs->y) -
        (lhs->y * rhs->x) + (lhs->z * rhs->w);
    *result = next;
}

static void quaternion_from_level_acceleration(
    const float acceleration[3],
    Algorithm_Mahony_Quaternion *quaternion)
{
    const float roll = atan2f(acceleration[1], acceleration[2]);
    const float horizontal = hypotf(acceleration[1], acceleration[2]);
    const float pitch = atan2f(-acceleration[0], horizontal);
    const float half_roll = 0.5F * roll;
    const float half_pitch = 0.5F * pitch;
    const float cr = cosf(half_roll);
    const float sr = sinf(half_roll);
    const float cp = cosf(half_pitch);
    const float sp = sinf(half_pitch);

    quaternion->w = cr * cp;
    quaternion->x = sr * cp;
    quaternion->y = cr * sp;
    quaternion->z = -sr * sp;
}

static void gravity_from_quaternion(
    const Algorithm_Mahony_Quaternion *quaternion,
    float gravity[3])
{
    gravity[0] = 2.0F * ((quaternion->x * quaternion->z) -
        (quaternion->w * quaternion->y));
    gravity[1] = 2.0F * ((quaternion->w * quaternion->x) +
        (quaternion->y * quaternion->z));
    gravity[2] = (quaternion->w * quaternion->w) -
        (quaternion->x * quaternion->x) -
        (quaternion->y * quaternion->y) +
        (quaternion->z * quaternion->z);
}

static uint32_t increment_saturating(uint32_t value)
{
    return (value == UINT32_MAX) ? UINT32_MAX : value + 1U;
}

static bool valid_positive(float value)
{
    return isfinite(value) && (value > 0.0F);
}

static bool valid_nonnegative(float value)
{
    return isfinite(value) && (value >= 0.0F);
}

static bool valid_config(const Algorithm_IMUFusion_Config *config)
{
    return (config != NULL) &&
        valid_axis_mapping(config->primary_body_axis) &&
        valid_axis_mapping(config->secondary_body_axis) &&
        finite3(config->secondary_position_m) &&
        valid_positive(config->primary_gyro_variance_dps2) &&
        valid_positive(config->secondary_gyro_variance_dps2) &&
        valid_positive(config->primary_acceleration_variance_mps4) &&
        valid_positive(config->secondary_acceleration_variance_mps4) &&
        valid_nonnegative(
            config->gyro_rate_process_noise_dps2_per_second) &&
        valid_nonnegative(
            config->gyro_rate_prediction_adaptation_gain) &&
        valid_positive(config->initial_gyro_rate_variance_dps2) &&
        valid_positive(config->maximum_gyro_rate_prediction_variance_dps2) &&
        (config->initial_gyro_rate_variance_dps2 <=
         config->maximum_gyro_rate_prediction_variance_dps2) &&
        valid_positive(config->gravity_mps2) &&
        valid_nonnegative(config->acceleration_norm_tolerance_mps2) &&
        valid_nonnegative(config->maximum_gyro_disagreement_dps) &&
        valid_nonnegative(config->maximum_acceleration_disagreement_mps2) &&
        valid_nonnegative(config->maximum_gravity_direction_innovation) &&
        valid_nonnegative(config->angular_acceleration_time_constant_seconds) &&
        valid_positive(config->maximum_dt_seconds) &&
        (config->maximum_dt_seconds <= IMU_FUSION_MAX_DT_SECONDS) &&
        (config->startup_calibration_settle_samples > 0U) &&
        (config->startup_calibration_required_samples > 0U) &&
        valid_nonnegative(config->calibration_acceleration_tolerance_mps2) &&
        valid_nonnegative(config->calibration_gyro_maximum_dps) &&
        valid_nonnegative(config->calibration_gyro_stddev_maximum_dps) &&
        valid_nonnegative(
            config->calibration_filter_time_constant_seconds) &&
        valid_nonnegative(config->runtime_bias_ema_alpha) &&
        (config->runtime_bias_ema_alpha <= 1.0F) &&
        valid_nonnegative(
            config->runtime_bias_filter_time_constant_seconds) &&
        (!config->enable_runtime_gyro_bias_update ||
         ((config->runtime_bias_ema_alpha > 0.0F) &&
          (config->runtime_bias_confirm_samples > 0U))) &&
        valid_nonnegative(config->gyro_process_noise_rad2_per_second) &&
        valid_nonnegative(config->gyro_bias_process_noise_rad2_per_second3) &&
        valid_positive(config->gravity_direction_measurement_variance) &&
        valid_nonnegative(config->initial_attitude_variance_rad2) &&
        valid_nonnegative(config->initial_bias_variance_rad2_per_second2) &&
        !config->estimate_gyro_bias;
}

Algorithm_IMUFusion_Config Algorithm_IMUFusion_DefaultConfig(void)
{
    const Algorithm_IMUFusion_AxisMapping identity[3] = {
        {TEMPLATE_IMU_FUSION_BODY_X_SOURCE,
            TEMPLATE_IMU_FUSION_BODY_X_DIRECTION},
        {TEMPLATE_IMU_FUSION_BODY_Y_SOURCE,
            TEMPLATE_IMU_FUSION_BODY_Y_DIRECTION},
        {TEMPLATE_IMU_FUSION_BODY_Z_SOURCE,
            TEMPLATE_IMU_FUSION_BODY_Z_DIRECTION},
    };
    Algorithm_IMUFusion_Config config = {
        .primary_body_axis = {{0}},
        .secondary_body_axis = {{0}},
        .secondary_position_m = {
            TEMPLATE_IMU_FUSION_SECONDARY_POSITION_X_M,
            TEMPLATE_IMU_FUSION_SECONDARY_POSITION_Y_M,
            TEMPLATE_IMU_FUSION_SECONDARY_POSITION_Z_M,
        },
        .primary_gyro_variance_dps2 =
            TEMPLATE_IMU_FUSION_PRIMARY_GYRO_VARIANCE_DPS2,
        .secondary_gyro_variance_dps2 =
            TEMPLATE_IMU_FUSION_SECONDARY_GYRO_VARIANCE_DPS2,
        .primary_acceleration_variance_mps4 =
            TEMPLATE_IMU_FUSION_PRIMARY_ACCEL_VARIANCE_MPS4,
        .secondary_acceleration_variance_mps4 =
            TEMPLATE_IMU_FUSION_SECONDARY_ACCEL_VARIANCE_MPS4,
        .gyro_rate_process_noise_dps2_per_second =
            TEMPLATE_IMU_FUSION_GYRO_RATE_PROCESS_NOISE_DPS2_PER_SECOND,
        .gyro_rate_prediction_adaptation_gain =
            TEMPLATE_IMU_FUSION_PREDICTION_ADAPTATION_GAIN,
        .initial_gyro_rate_variance_dps2 =
            TEMPLATE_IMU_FUSION_INITIAL_GYRO_VARIANCE_DPS2,
        .maximum_gyro_rate_prediction_variance_dps2 =
            TEMPLATE_IMU_FUSION_MAXIMUM_GYRO_VARIANCE_DPS2,
        .gravity_mps2 = TEMPLATE_IMU_FUSION_GRAVITY_MPS2,
        .acceleration_norm_tolerance_mps2 =
            TEMPLATE_IMU_FUSION_ACCEL_NORM_TOLERANCE_MPS2,
        .maximum_gyro_disagreement_dps =
            TEMPLATE_IMU_FUSION_MAXIMUM_GYRO_DISAGREEMENT_DPS,
        .maximum_acceleration_disagreement_mps2 =
            TEMPLATE_IMU_FUSION_MAXIMUM_ACCEL_DISAGREEMENT_MPS2,
        .maximum_gravity_direction_innovation =
            TEMPLATE_IMU_FUSION_MAXIMUM_GRAVITY_INNOVATION,
        .angular_acceleration_time_constant_seconds =
            TEMPLATE_IMU_FUSION_ANGULAR_ACCEL_FILTER_TIME_CONSTANT_SECONDS,
        .maximum_dt_seconds = TEMPLATE_IMU_FUSION_MAXIMUM_DT_SECONDS,
        .startup_calibration_settle_samples =
            TEMPLATE_IMU_FUSION_CALIBRATION_SETTLE_SAMPLES,
        .startup_calibration_required_samples =
            TEMPLATE_IMU_FUSION_CALIBRATION_REQUIRED_SAMPLES,
        .calibration_acceleration_tolerance_mps2 =
            TEMPLATE_IMU_FUSION_CALIBRATION_ACCEL_TOLERANCE_MPS2,
        .calibration_gyro_maximum_dps =
            TEMPLATE_IMU_FUSION_CALIBRATION_GYRO_MAXIMUM_DPS,
        .calibration_gyro_stddev_maximum_dps =
            TEMPLATE_IMU_FUSION_CALIBRATION_GYRO_STDDEV_MAXIMUM_DPS,
        .calibration_filter_time_constant_seconds =
            TEMPLATE_IMU_FUSION_CALIBRATION_FILTER_TIME_CONSTANT_SECONDS,
        .enable_startup_bias_trend_fit =
            TEMPLATE_IMU_FUSION_ENABLE_STARTUP_BIAS_TREND_FIT,
        .enable_startup_gyro_bias_calibration =
            TEMPLATE_IMU_FUSION_ENABLE_STARTUP_GYRO_BIAS_CALIBRATION,
        .enable_runtime_gyro_bias_update =
            TEMPLATE_IMU_FUSION_ENABLE_RUNTIME_GYRO_BIAS_UPDATE,
        .runtime_bias_ema_alpha =
            TEMPLATE_IMU_FUSION_RUNTIME_BIAS_EMA_ALPHA,
        .runtime_bias_confirm_samples =
            TEMPLATE_IMU_FUSION_RUNTIME_BIAS_CONFIRM_SAMPLES,
        .runtime_bias_filter_time_constant_seconds =
            TEMPLATE_IMU_FUSION_RUNTIME_BIAS_FILTER_TIME_CONSTANT_SECONDS,
        .gyro_process_noise_rad2_per_second =
            TEMPLATE_IMU_FUSION_GYRO_PROCESS_NOISE_RAD2_PER_SECOND,
        .gyro_bias_process_noise_rad2_per_second3 =
            TEMPLATE_IMU_FUSION_GYRO_BIAS_PROCESS_NOISE_RAD2_PER_SECOND3,
        .gravity_direction_measurement_variance =
            TEMPLATE_IMU_FUSION_GRAVITY_MEASUREMENT_VARIANCE,
        .initial_attitude_variance_rad2 =
            TEMPLATE_IMU_FUSION_INITIAL_ATTITUDE_VARIANCE_RAD2,
        .initial_bias_variance_rad2_per_second2 =
            TEMPLATE_IMU_FUSION_INITIAL_BIAS_VARIANCE_RAD2_PER_SECOND2,
        .estimate_gyro_bias = TEMPLATE_IMU_FUSION_ESTIMATE_GYRO_BIAS,
    };

    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        config.primary_body_axis[axis] = identity[axis];
        config.secondary_body_axis[axis] = identity[axis];
    }
    return config;
}

static void reset_calibration_window(
    Algorithm_IMUFusion *fusion, bool rejected)
{
    const bool had_progress =
        (fusion->snapshot.calibration_settle_samples != 0U) ||
        (fusion->snapshot.calibration_samples_collected != 0U);

    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fusion->primary_calibration_mean_dps[axis] = 0.0F;
        fusion->primary_calibration_m2_dps2[axis] = 0.0F;
        fusion->secondary_calibration_mean_dps[axis] = 0.0F;
        fusion->secondary_calibration_m2_dps2[axis] = 0.0F;
        fusion->primary_calibration_filtered_dps[axis] = 0.0F;
        fusion->secondary_calibration_filtered_dps[axis] = 0.0F;
        fusion->primary_calibration_fit_mean_dps[axis] = 0.0F;
        fusion->secondary_calibration_fit_mean_dps[axis] = 0.0F;
        fusion->primary_calibration_fit_cross_dps_seconds[axis] = 0.0F;
        fusion->secondary_calibration_fit_cross_dps_seconds[axis] = 0.0F;
    }
    fusion->calibration_fit_mean_time_seconds = 0.0F;
    fusion->calibration_fit_time_m2_seconds2 = 0.0F;
    fusion->calibration_fit_elapsed_seconds = 0.0F;
    fusion->calibration_fit_last_time_seconds = 0.0F;
    fusion->calibration_filter_seeded = false;
    fusion->snapshot.calibration_settle_samples = 0U;
    fusion->snapshot.calibration_samples_collected = 0U;
    fusion->snapshot.calibration_status =
        ALGORITHM_IMU_FUSION_CALIBRATION_SETTLING;
    fusion->snapshot.calibration_sample_rejected = rejected;
    if (rejected && had_progress) {
        fusion->snapshot.calibration_restart_count = increment_saturating(
            fusion->snapshot.calibration_restart_count);
    }
}

static bool calibration_sample_stationary(
    const Algorithm_IMUFusion_Config *config,
    const float primary_acceleration[3],
    const float primary_angular_rate[3],
    const float secondary_acceleration[3],
    const float secondary_angular_rate[3])
{
    const float primary_acceleration_norm = norm3(primary_acceleration);
    const float primary_gyro_norm = norm3(primary_angular_rate);
    const float secondary_acceleration_norm = norm3(secondary_acceleration);
    const float secondary_gyro_norm = norm3(secondary_angular_rate);

    return isfinite(primary_acceleration_norm) &&
        isfinite(primary_gyro_norm) &&
        isfinite(secondary_acceleration_norm) &&
        isfinite(secondary_gyro_norm) &&
        (fabsf(primary_acceleration_norm - config->gravity_mps2) <=
         config->calibration_acceleration_tolerance_mps2) &&
        (fabsf(secondary_acceleration_norm - config->gravity_mps2) <=
         config->calibration_acceleration_tolerance_mps2) &&
        (primary_gyro_norm <= config->calibration_gyro_maximum_dps) &&
        (secondary_gyro_norm <= config->calibration_gyro_maximum_dps);
}

static bool update_calibration_statistics(
    float mean[3],
    float m2[3],
    const float sample[3],
    uint32_t count)
{
    const float divisor = (float)count;

    if ((count == 0U) || !isfinite(divisor) || (divisor <= 0.0F)) {
        return false;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        const float delta = sample[axis] - mean[axis];

        mean[axis] += delta / divisor;
        m2[axis] += delta * (sample[axis] - mean[axis]);
    }
    return finite3(mean) && finite3(m2);
}

static bool calibration_standard_deviation_valid(
    const Algorithm_IMUFusion *fusion)
{
    const uint32_t count =
        fusion->snapshot.calibration_samples_collected;

    if (count <= 1U) {
        return true;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        const float divisor = (float)(count - 1U);
        const float primary_variance =
            fusion->primary_calibration_m2_dps2[axis] / divisor;
        const float secondary_variance =
            fusion->secondary_calibration_m2_dps2[axis] / divisor;

        if (!isfinite(primary_variance) || !isfinite(secondary_variance) ||
            (primary_variance < 0.0F) || (secondary_variance < 0.0F) ||
            (sqrtf(primary_variance) >
             fusion->config.calibration_gyro_stddev_maximum_dps) ||
            (sqrtf(secondary_variance) >
             fusion->config.calibration_gyro_stddev_maximum_dps)) {
            return false;
        }
    }
    return true;
}

static bool update_calibration_fit(
    Algorithm_IMUFusion *fusion,
    const float primary_angular_rate[3],
    const float secondary_angular_rate[3],
    float dt_seconds,
    uint32_t count)
{
    const float divisor = (float)count;
    float sample_time;
    float filter_gain;
    float time_delta;

    if ((fusion == NULL) || !finite3(primary_angular_rate) ||
        !finite3(secondary_angular_rate) || !valid_positive(dt_seconds) ||
        (count == 0U) || !valid_positive(divisor)) {
        return false;
    }
    sample_time = fusion->calibration_fit_elapsed_seconds;
    filter_gain = dt_seconds /
        (fusion->config.calibration_filter_time_constant_seconds +
         dt_seconds);
    if (!isfinite(filter_gain) || (filter_gain <= 0.0F) ||
        (filter_gain > 1.0F)) {
        return false;
    }

    if (count == 1U) {
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            fusion->primary_calibration_filtered_dps[axis] =
                primary_angular_rate[axis];
            fusion->secondary_calibration_filtered_dps[axis] =
                secondary_angular_rate[axis];
            fusion->primary_calibration_fit_mean_dps[axis] =
                primary_angular_rate[axis];
            fusion->secondary_calibration_fit_mean_dps[axis] =
                secondary_angular_rate[axis];
        }
        fusion->calibration_fit_mean_time_seconds = sample_time;
        fusion->calibration_fit_time_m2_seconds2 = 0.0F;
        fusion->calibration_filter_seeded = true;
    } else {
        if (!fusion->calibration_filter_seeded) {
            return false;
        }
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            fusion->primary_calibration_filtered_dps[axis] += filter_gain *
                (primary_angular_rate[axis] -
                 fusion->primary_calibration_filtered_dps[axis]);
            fusion->secondary_calibration_filtered_dps[axis] += filter_gain *
                (secondary_angular_rate[axis] -
                 fusion->secondary_calibration_filtered_dps[axis]);
        }
        time_delta = sample_time -
            fusion->calibration_fit_mean_time_seconds;
        fusion->calibration_fit_mean_time_seconds += time_delta / divisor;
        fusion->calibration_fit_time_m2_seconds2 += time_delta *
            (sample_time - fusion->calibration_fit_mean_time_seconds);
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            const float primary_delta =
                fusion->primary_calibration_filtered_dps[axis] -
                fusion->primary_calibration_fit_mean_dps[axis];
            const float secondary_delta =
                fusion->secondary_calibration_filtered_dps[axis] -
                fusion->secondary_calibration_fit_mean_dps[axis];

            fusion->primary_calibration_fit_mean_dps[axis] +=
                primary_delta / divisor;
            fusion->secondary_calibration_fit_mean_dps[axis] +=
                secondary_delta / divisor;
            fusion->primary_calibration_fit_cross_dps_seconds[axis] +=
                time_delta *
                (fusion->primary_calibration_filtered_dps[axis] -
                 fusion->primary_calibration_fit_mean_dps[axis]);
            fusion->secondary_calibration_fit_cross_dps_seconds[axis] +=
                time_delta *
                (fusion->secondary_calibration_filtered_dps[axis] -
                 fusion->secondary_calibration_fit_mean_dps[axis]);
        }
    }
    fusion->calibration_fit_last_time_seconds = sample_time;
    fusion->calibration_fit_elapsed_seconds += dt_seconds;
    return finite3(fusion->primary_calibration_filtered_dps) &&
        finite3(fusion->secondary_calibration_filtered_dps) &&
        finite3(fusion->primary_calibration_fit_mean_dps) &&
        finite3(fusion->secondary_calibration_fit_mean_dps) &&
        finite3(fusion->primary_calibration_fit_cross_dps_seconds) &&
        finite3(fusion->secondary_calibration_fit_cross_dps_seconds) &&
        isfinite(fusion->calibration_fit_mean_time_seconds) &&
        isfinite(fusion->calibration_fit_time_m2_seconds2) &&
        isfinite(fusion->calibration_fit_elapsed_seconds) &&
        isfinite(fusion->calibration_fit_last_time_seconds);
}

static bool commit_calibration_fit(Algorithm_IMUFusion *fusion)
{
    uint32_t count;
    float evaluation_time;

    if (fusion == NULL) {
        return false;
    }
    count = fusion->snapshot.calibration_samples_collected;
    evaluation_time = fusion->calibration_fit_last_time_seconds +
        fusion->config.calibration_filter_time_constant_seconds;
    if ((count == 0U) || !isfinite(evaluation_time)) {
        return false;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        float primary_slope = 0.0F;
        float secondary_slope = 0.0F;

        if (count > 1U) {
            if (!valid_positive(
                    fusion->calibration_fit_time_m2_seconds2)) {
                return false;
            }
            primary_slope =
                fusion->primary_calibration_fit_cross_dps_seconds[axis] /
                fusion->calibration_fit_time_m2_seconds2;
            secondary_slope =
                fusion->secondary_calibration_fit_cross_dps_seconds[axis] /
                fusion->calibration_fit_time_m2_seconds2;
        }
        fusion->snapshot.primary_gyro_bias_fit_slope_dps_per_second[axis] =
            primary_slope;
        fusion->snapshot.secondary_gyro_bias_fit_slope_dps_per_second[axis] =
            secondary_slope;
        fusion->snapshot.primary_gyro_bias_dps[axis] =
            fusion->primary_calibration_fit_mean_dps[axis] +
            primary_slope *
                (evaluation_time -
                 fusion->calibration_fit_mean_time_seconds);
        fusion->snapshot.secondary_gyro_bias_dps[axis] =
            fusion->secondary_calibration_fit_mean_dps[axis] +
            secondary_slope *
                (evaluation_time -
                 fusion->calibration_fit_mean_time_seconds);
    }
    return finite3(fusion->snapshot.primary_gyro_bias_dps) &&
        finite3(fusion->snapshot.secondary_gyro_bias_dps) &&
        finite3(
            fusion->snapshot.primary_gyro_bias_fit_slope_dps_per_second) &&
        finite3(
            fusion->snapshot.secondary_gyro_bias_fit_slope_dps_per_second);
}

static bool update_startup_calibration(
    Algorithm_IMUFusion *fusion,
    bool primary_valid,
    bool secondary_valid,
    const float primary_acceleration[3],
    const float primary_angular_rate[3],
    const float secondary_acceleration[3],
    const float secondary_angular_rate[3],
    float dt_seconds,
    bool *completed)
{
    uint32_t sample_count;

    if ((fusion == NULL) || (completed == NULL)) {
        return false;
    }
    *completed = false;
    fusion->snapshot.calibration_sample_rejected = false;
    if (!fusion->config.enable_startup_gyro_bias_calibration ||
        (fusion->snapshot.calibration_status ==
         ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE)) {
        return true;
    }
    if (!primary_valid || !secondary_valid ||
        !calibration_sample_stationary(&fusion->config,
            primary_acceleration, primary_angular_rate,
            secondary_acceleration, secondary_angular_rate)) {
        reset_calibration_window(fusion, true);
        return true;
    }
    if (fusion->snapshot.calibration_status ==
        ALGORITHM_IMU_FUSION_CALIBRATION_SETTLING) {
        fusion->snapshot.calibration_settle_samples = increment_saturating(
            fusion->snapshot.calibration_settle_samples);
        if (fusion->snapshot.calibration_settle_samples >=
            fusion->config.startup_calibration_settle_samples) {
            fusion->snapshot.calibration_status =
                ALGORITHM_IMU_FUSION_CALIBRATION_COLLECTING;
        }
        return true;
    }
    if (fusion->snapshot.calibration_status !=
        ALGORITHM_IMU_FUSION_CALIBRATION_COLLECTING) {
        return false;
    }

    sample_count = increment_saturating(
        fusion->snapshot.calibration_samples_collected);
    if (!update_calibration_statistics(
            fusion->primary_calibration_mean_dps,
            fusion->primary_calibration_m2_dps2,
            primary_angular_rate, sample_count) ||
        !update_calibration_statistics(
            fusion->secondary_calibration_mean_dps,
            fusion->secondary_calibration_m2_dps2,
            secondary_angular_rate, sample_count) ||
        (fusion->config.enable_startup_bias_trend_fit &&
         !update_calibration_fit(fusion, primary_angular_rate,
             secondary_angular_rate, dt_seconds, sample_count))) {
        return false;
    }
    fusion->snapshot.calibration_samples_collected = sample_count;
    if (sample_count <
        fusion->config.startup_calibration_required_samples) {
        return true;
    }
    if (!calibration_standard_deviation_valid(fusion)) {
        reset_calibration_window(fusion, true);
        return true;
    }
    if (fusion->config.enable_startup_bias_trend_fit) {
        if (!commit_calibration_fit(fusion)) {
            return false;
        }
    } else {
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            fusion->snapshot.primary_gyro_bias_dps[axis] =
                fusion->primary_calibration_mean_dps[axis];
            fusion->snapshot.secondary_gyro_bias_dps[axis] =
                fusion->secondary_calibration_mean_dps[axis];
            fusion->snapshot
                .primary_gyro_bias_fit_slope_dps_per_second[axis] = 0.0F;
            fusion->snapshot
                .secondary_gyro_bias_fit_slope_dps_per_second[axis] = 0.0F;
        }
    }
    fusion->snapshot.gyro_bias_valid = true;
    fusion->snapshot.calibration_status =
        ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE;
    *completed = true;
    return true;
}

static void reset_runtime_bias_window(Algorithm_IMUFusion *fusion)
{
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fusion->runtime_primary_residual_mean_dps[axis] = 0.0F;
        fusion->runtime_primary_residual_var_dps2[axis] = 0.0F;
        fusion->runtime_secondary_residual_mean_dps[axis] = 0.0F;
        fusion->runtime_secondary_residual_var_dps2[axis] = 0.0F;
    }
    fusion->runtime_stationary_samples = 0U;
    fusion->runtime_primary_seeded = false;
    fusion->runtime_secondary_seeded = false;
    fusion->snapshot.runtime_bias_stationary = false;
}

static bool runtime_sensor_stationary(
    const Algorithm_IMUFusion_Config *config,
    const float acceleration[3],
    const float rate_residual_dps[3])
{
    const float acceleration_norm = norm3(acceleration);
    const float rate_norm = norm3(rate_residual_dps);

    return isfinite(acceleration_norm) && isfinite(rate_norm) &&
        (fabsf(acceleration_norm - config->gravity_mps2) <=
         config->calibration_acceleration_tolerance_mps2) &&
        (rate_norm <= config->calibration_gyro_maximum_dps);
}

/* Exponentially weighted mean/variance of one sensor's stationary rate
 * residual. Returns false when any axis exceeds the calibration noise gate,
 * which marks the window as contaminated by unmodelled motion. */
static bool runtime_update_residual_statistics(
    const Algorithm_IMUFusion_Config *config,
    float mean_dps[3],
    float var_dps2[3],
    const float residual_dps[3],
    bool *seeded,
    float filter_gain)
{
    const float stddev_maximum =
        config->calibration_gyro_stddev_maximum_dps;

    if (!*seeded) {
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            mean_dps[axis] = residual_dps[axis];
            var_dps2[axis] = 0.0F;
        }
        *seeded = true;
        return true;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        const float delta = residual_dps[axis] - mean_dps[axis];

        mean_dps[axis] += filter_gain * delta;
        var_dps2[axis] = (1.0F - filter_gain) *
            (var_dps2[axis] + (filter_gain * delta * delta));
        if (!isfinite(mean_dps[axis]) || !isfinite(var_dps2[axis]) ||
            (var_dps2[axis] > (stddev_maximum * stddev_maximum))) {
            return false;
        }
    }
    return true;
}

/* Runtime counterpart of the startup calibration, modelled on long-run
 * dead-reckoning practice: an external odometry stillness report plus the
 * calibration rate/gravity/noise gates must all hold for a confirmation
 * window before each valid sensor bias slowly absorbs its remaining rate
 * residual. Constant turning cannot qualify because the mean-rate gate and
 * the odometry report both fail. The update is deliberately non-fatal:
 * a rejected window only resets the confirmation state. */
static void update_runtime_gyro_bias(
    Algorithm_IMUFusion *fusion,
    bool primary_valid,
    const float primary_residual_dps[3],
    const float primary_acceleration[3],
    bool secondary_valid,
    const float secondary_residual_dps[3],
    const float secondary_acceleration[3],
    bool odometry_stationary,
    float dt_seconds)
{
    const Algorithm_IMUFusion_Config *config = &fusion->config;
    float filter_gain;
    bool stationary;

    if (!config->enable_runtime_gyro_bias_update ||
        !fusion->snapshot.gyro_bias_valid) {
        return;
    }
    stationary = odometry_stationary &&
        (primary_valid || secondary_valid) &&
        (!primary_valid || runtime_sensor_stationary(config,
            primary_acceleration, primary_residual_dps)) &&
        (!secondary_valid || runtime_sensor_stationary(config,
            secondary_acceleration, secondary_residual_dps));
    if (!stationary) {
        reset_runtime_bias_window(fusion);
        return;
    }
    /* A dropped sensor invalidates its stale statistics; the window of the
     * remaining sensor keeps running. */
    if (!primary_valid) {
        fusion->runtime_primary_seeded = false;
    }
    if (!secondary_valid) {
        fusion->runtime_secondary_seeded = false;
    }
    filter_gain = dt_seconds /
        (config->runtime_bias_filter_time_constant_seconds + dt_seconds);
    if (!isfinite(filter_gain) || (filter_gain <= 0.0F) ||
        (filter_gain > 1.0F)) {
        reset_runtime_bias_window(fusion);
        return;
    }
    if ((primary_valid && !runtime_update_residual_statistics(config,
            fusion->runtime_primary_residual_mean_dps,
            fusion->runtime_primary_residual_var_dps2,
            primary_residual_dps,
            &fusion->runtime_primary_seeded, filter_gain)) ||
        (secondary_valid && !runtime_update_residual_statistics(config,
            fusion->runtime_secondary_residual_mean_dps,
            fusion->runtime_secondary_residual_var_dps2,
            secondary_residual_dps,
            &fusion->runtime_secondary_seeded, filter_gain))) {
        reset_runtime_bias_window(fusion);
        return;
    }
    fusion->runtime_stationary_samples = increment_saturating(
        fusion->runtime_stationary_samples);
    if (fusion->runtime_stationary_samples <
        config->runtime_bias_confirm_samples) {
        fusion->snapshot.runtime_bias_stationary = false;
        return;
    }
    fusion->snapshot.runtime_bias_stationary = true;
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        if (primary_valid) {
            fusion->snapshot.primary_gyro_bias_dps[axis] +=
                config->runtime_bias_ema_alpha *
                fusion->runtime_primary_residual_mean_dps[axis];
            /* The absorbed fraction leaves the residual window so the same
             * error is not integrated into the bias twice. */
            fusion->runtime_primary_residual_mean_dps[axis] *=
                1.0F - config->runtime_bias_ema_alpha;
        }
        if (secondary_valid) {
            fusion->snapshot.secondary_gyro_bias_dps[axis] +=
                config->runtime_bias_ema_alpha *
                fusion->runtime_secondary_residual_mean_dps[axis];
            fusion->runtime_secondary_residual_mean_dps[axis] *=
                1.0F - config->runtime_bias_ema_alpha;
        }
    }
    if (!finite3(fusion->snapshot.primary_gyro_bias_dps) ||
        !finite3(fusion->snapshot.secondary_gyro_bias_dps)) {
        reset_runtime_bias_window(fusion);
        return;
    }
    fusion->snapshot.runtime_bias_update_count = increment_saturating(
        fusion->snapshot.runtime_bias_update_count);
}

static bool fuse_vector(
    const float primary[3],
    bool primary_valid,
    float primary_variance,
    const float secondary[3],
    bool secondary_valid,
    float secondary_variance,
    float maximum_disagreement,
    float result[3],
    uint8_t *source_mask)
{
    float difference[3];
    float distance;
    float primary_weight;
    float secondary_weight;
    float weight_sum;
    float variance_scale;

    if ((result == NULL) || (source_mask == NULL) ||
        (!primary_valid && !secondary_valid)) {
        return false;
    }
    if (primary_valid && !finite3(primary)) {
        return false;
    }
    if (secondary_valid && !finite3(secondary)) {
        return false;
    }
    if (primary_valid && secondary_valid) {
        difference[0] = primary[0] - secondary[0];
        difference[1] = primary[1] - secondary[1];
        difference[2] = primary[2] - secondary[2];
        distance = norm3(difference);
        if (!isfinite(distance)) {
            return false;
        }
        if (distance > maximum_disagreement) {
            /* ICM is the explicitly designated primary reference.  A gross
             * disagreement is therefore isolated to the secondary channel;
             * variance weighting is used only while both measurements agree. */
            (void)secondary_variance;
            result[0] = primary[0];
            result[1] = primary[1];
            result[2] = primary[2];
            *source_mask = 0x01U;
            return finite3(result);
        }
        /* Normalize before taking the inverse-variance ratio so a valid but
         * very small configured variance cannot overflow 1/variance. */
        variance_scale = fmaxf(primary_variance, secondary_variance);
        if (!isfinite(variance_scale) || (variance_scale <= 0.0F)) {
            return false;
        }
        primary_weight = secondary_variance / variance_scale;
        secondary_weight = primary_variance / variance_scale;
        weight_sum = primary_weight + secondary_weight;
        if (!isfinite(primary_weight) || !isfinite(secondary_weight) ||
            !isfinite(weight_sum) || (weight_sum <= 0.0F)) {
            return false;
        }
        result[0] = (primary_weight * primary[0] +
            secondary_weight * secondary[0]) / weight_sum;
        result[1] = (primary_weight * primary[1] +
            secondary_weight * secondary[1]) / weight_sum;
        result[2] = (primary_weight * primary[2] +
            secondary_weight * secondary[2]) / weight_sum;
        *source_mask = 0x03U;
        return finite3(result);
    }
    if (primary_valid) {
        result[0] = primary[0];
        result[1] = primary[1];
        result[2] = primary[2];
        *source_mask = 0x01U;
    } else {
        result[0] = secondary[0];
        result[1] = secondary[1];
        result[2] = secondary[2];
        *source_mask = 0x02U;
    }
    return finite3(result);
}

static bool scalar_confidence_weights(
    float prediction_variance,
    bool primary_used,
    float primary_variance,
    bool secondary_used,
    float secondary_variance,
    float *prediction_weight,
    float *primary_weight,
    float *secondary_weight,
    float *posterior_variance)
{
    float variance_scale = prediction_variance;
    float prediction_precision;
    float primary_precision = 0.0F;
    float secondary_precision = 0.0F;
    float precision_sum;

    if ((prediction_weight == NULL) || (primary_weight == NULL) ||
        (secondary_weight == NULL) || (posterior_variance == NULL) ||
        !valid_positive(prediction_variance) ||
        (primary_used && !valid_positive(primary_variance)) ||
        (secondary_used && !valid_positive(secondary_variance)) ||
        (!primary_used && !secondary_used)) {
        return false;
    }
    if (primary_used) {
        variance_scale = fminf(variance_scale, primary_variance);
    }
    if (secondary_used) {
        variance_scale = fminf(variance_scale, secondary_variance);
    }
    prediction_precision = variance_scale / prediction_variance;
    if (primary_used) {
        primary_precision = variance_scale / primary_variance;
    }
    if (secondary_used) {
        secondary_precision = variance_scale / secondary_variance;
    }
    precision_sum = prediction_precision + primary_precision +
        secondary_precision;
    if (!isfinite(precision_sum) || (precision_sum <= 0.0F)) {
        return false;
    }
    *prediction_weight = prediction_precision / precision_sum;
    *primary_weight = primary_precision / precision_sum;
    *secondary_weight = secondary_precision / precision_sum;
    *posterior_variance = variance_scale / precision_sum;
    return isfinite(*prediction_weight) && isfinite(*primary_weight) &&
        isfinite(*secondary_weight) && valid_positive(*posterior_variance);
}

static float calibrated_gyro_variance(
    const Algorithm_IMUFusion *fusion,
    bool primary,
    unsigned int axis)
{
    const float baseline = primary ?
        fusion->config.primary_gyro_variance_dps2 :
        fusion->config.secondary_gyro_variance_dps2;
    const uint32_t count = fusion->snapshot.calibration_samples_collected;
    float sample_variance;
    float minimum_variance;

    if ((axis >= IMU_FUSION_AXIS_COUNT) ||
        !fusion->snapshot.gyro_bias_valid ||
        (fusion->snapshot.calibration_status !=
         ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE) ||
        (count < 2U)) {
        return baseline;
    }
    sample_variance = (primary ?
        fusion->primary_calibration_m2_dps2[axis] :
        fusion->secondary_calibration_m2_dps2[axis]) /
        (float)(count - 1U);
    minimum_variance = baseline *
        IMU_FUSION_MIN_CALIBRATED_GYRO_VARIANCE_RATIO;
    if (!valid_positive(sample_variance) ||
        !valid_positive(minimum_variance)) {
        return baseline;
    }
    return fmaxf(sample_variance, minimum_variance);
}

static bool select_gyro_after_disagreement(
    const Algorithm_IMUFusion *fusion,
    const float prediction[3],
    const float prediction_variance[3],
    const float primary_variance[3],
    const float secondary_variance[3],
    const float primary[3],
    const float secondary[3],
    bool *primary_selected)
{
    float primary_score = 0.0F;
    float secondary_score = 0.0F;

    if ((fusion == NULL) || (prediction == NULL) ||
        (prediction_variance == NULL) || (primary_selected == NULL)) {
        return false;
    }
    if (!fusion->gyro_rate_filter_seeded) {
        *primary_selected =
            (primary_variance[0] + primary_variance[1] +
             primary_variance[2]) <=
            (secondary_variance[0] + secondary_variance[1] +
             secondary_variance[2]);
        return true;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        const float primary_innovation = primary[axis] - prediction[axis];
        const float secondary_innovation = secondary[axis] - prediction[axis];
        const float primary_innovation_variance = prediction_variance[axis] +
            primary_variance[axis];
        const float secondary_innovation_variance = prediction_variance[axis] +
            secondary_variance[axis];

        if (!valid_positive(primary_innovation_variance) ||
            !valid_positive(secondary_innovation_variance)) {
            return false;
        }
        primary_score += (primary_innovation * primary_innovation) /
            primary_innovation_variance;
        secondary_score += (secondary_innovation * secondary_innovation) /
            secondary_innovation_variance;
    }
    if (!isfinite(primary_score) || !isfinite(secondary_score)) {
        return false;
    }
    *primary_selected = primary_score <= secondary_score;
    return true;
}

static bool fuse_gyro_with_prediction(
    Algorithm_IMUFusion *fusion,
    const float primary[3],
    bool primary_valid,
    const float secondary[3],
    bool secondary_valid,
    float dt_seconds,
    float result[3],
    float measurement_result[3],
    uint8_t *source_mask)
{
    float prediction[3];
    float prediction_variance[3];
    float primary_variance[3];
    float secondary_variance[3];
    float difference[3] = {0.0F, 0.0F, 0.0F};
    float disagreement = 0.0F;
    bool primary_used = primary_valid;
    bool secondary_used = secondary_valid;

    if ((fusion == NULL) || (result == NULL) ||
        (measurement_result == NULL) || (source_mask == NULL) ||
        (!primary_valid && !secondary_valid) ||
        (primary_valid && !finite3(primary)) ||
        (secondary_valid && !finite3(secondary)) ||
        !valid_positive(dt_seconds)) {
        return false;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        primary_variance[axis] =
            calibrated_gyro_variance(fusion, true, axis);
        secondary_variance[axis] =
            calibrated_gyro_variance(fusion, false, axis);
        if (fusion->gyro_rate_filter_seeded) {
            const float acceleration_dps2 =
                fusion->angular_acceleration_rad_per_second2[axis] *
                IMU_FUSION_RAD_TO_DEG;
            const float process_variance =
                fusion->config.gyro_rate_process_noise_dps2_per_second *
                dt_seconds;

            prediction[axis] = fusion->gyro_rate_estimate_dps[axis] +
                (acceleration_dps2 * dt_seconds);
            prediction_variance[axis] =
                fusion->gyro_rate_variance_dps2[axis] + process_variance;
            prediction_variance[axis] = fminf(prediction_variance[axis],
                fusion->config.maximum_gyro_rate_prediction_variance_dps2);
        } else {
            prediction[axis] = 0.0F;
            prediction_variance[axis] =
                fusion->config.initial_gyro_rate_variance_dps2;
        }
    }
    if (!finite3(prediction) || !finite3(prediction_variance) ||
        !finite3(primary_variance) || !finite3(secondary_variance)) {
        return false;
    }

    if (primary_valid && secondary_valid) {
        bool primary_selected;

        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            difference[axis] = primary[axis] - secondary[axis];
        }
        disagreement = norm3(difference);
        if (!isfinite(disagreement)) {
            return false;
        }
        if (disagreement > fusion->config.maximum_gyro_disagreement_dps) {
            if (!select_gyro_after_disagreement(fusion, prediction,
                    prediction_variance, primary_variance,
                    secondary_variance, primary, secondary,
                    &primary_selected)) {
                return false;
            }
            primary_used = primary_selected;
            secondary_used = !primary_selected;
        }
    }

    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        float prediction_weight = 0.0F;
        float primary_weight = 0.0F;
        float secondary_weight = 0.0F;
        float posterior_variance;

        if (!fusion->gyro_rate_filter_seeded) {
            float measurement_precision_sum = 0.0F;
            float primary_precision = 0.0F;
            float secondary_precision = 0.0F;
            float variance_scale;

            if (primary_used && secondary_used) {
                variance_scale = fminf(
                    primary_variance[axis], secondary_variance[axis]);
            } else if (primary_used) {
                variance_scale = primary_variance[axis];
            } else {
                variance_scale = secondary_variance[axis];
            }
            if (primary_used) {
                primary_precision = variance_scale /
                    primary_variance[axis];
            }
            if (secondary_used) {
                secondary_precision = variance_scale /
                    secondary_variance[axis];
            }
            measurement_precision_sum = primary_precision +
                secondary_precision;
            if (!valid_positive(measurement_precision_sum)) {
                return false;
            }
            primary_weight = primary_precision / measurement_precision_sum;
            secondary_weight = secondary_precision /
                measurement_precision_sum;
            posterior_variance = variance_scale / measurement_precision_sum;
        } else {
            if (primary_used && secondary_used) {
                const float measurement_variance =
                    (primary_variance[axis] * secondary_variance[axis]) /
                    (primary_variance[axis] + secondary_variance[axis]);
                const float measurement =
                    ((secondary_variance[axis] * primary[axis]) +
                     (primary_variance[axis] * secondary[axis])) /
                    (primary_variance[axis] + secondary_variance[axis]);
                const float innovation = measurement - prediction[axis];
                const float expected_innovation_squared =
                    prediction_variance[axis] + measurement_variance;
                const float excess_innovation_squared =
                    (innovation * innovation) - expected_innovation_squared;

                if (excess_innovation_squared > 0.0F) {
                    prediction_variance[axis] +=
                        fusion->config.gyro_rate_prediction_adaptation_gain *
                        excess_innovation_squared;
                    prediction_variance[axis] = fminf(
                        prediction_variance[axis],
                        fusion->config
                            .maximum_gyro_rate_prediction_variance_dps2);
                }
            }
            if (!scalar_confidence_weights(prediction_variance[axis],
                    primary_used,
                    primary_variance[axis],
                    secondary_used,
                    secondary_variance[axis],
                    &prediction_weight, &primary_weight, &secondary_weight,
                    &posterior_variance)) {
                return false;
            }
        }
        result[axis] = prediction_weight * prediction[axis];
        if (primary_used) {
            result[axis] += primary_weight * primary[axis];
        }
        if (secondary_used) {
            result[axis] += secondary_weight * secondary[axis];
        }
        {
            const float measurement_weight_sum = primary_weight +
                secondary_weight;

            if (!valid_positive(measurement_weight_sum)) {
                return false;
            }
            measurement_result[axis] = 0.0F;
            if (primary_used) {
                measurement_result[axis] += primary_weight * primary[axis];
            }
            if (secondary_used) {
                measurement_result[axis] +=
                    secondary_weight * secondary[axis];
            }
            measurement_result[axis] /= measurement_weight_sum;
        }
        fusion->gyro_rate_estimate_dps[axis] = result[axis];
        fusion->gyro_rate_variance_dps2[axis] = posterior_variance;
        fusion->snapshot.gyro_prediction_weight[axis] = prediction_weight;
        fusion->snapshot.primary_gyro_weight[axis] = primary_weight;
        fusion->snapshot.secondary_gyro_weight[axis] = secondary_weight;
    }
    fusion->gyro_rate_filter_seeded = true;
    *source_mask = (primary_used ? 0x01U : 0x00U) |
        (secondary_used ? 0x02U : 0x00U);
    return finite3(result) && finite3(measurement_result) &&
        finite3(fusion->gyro_rate_variance_dps2) &&
        finite3(fusion->snapshot.gyro_prediction_weight) &&
        finite3(fusion->snapshot.primary_gyro_weight) &&
        finite3(fusion->snapshot.secondary_gyro_weight);
}

static bool update_angular_acceleration(
    Algorithm_IMUFusion *fusion,
    const float angular_rate[3],
    float dt_seconds)
{
    float raw[3];
    float gain;

    if (!fusion->has_previous_angular_rate) {
        fusion->angular_acceleration_rad_per_second2[0] = 0.0F;
        fusion->angular_acceleration_rad_per_second2[1] = 0.0F;
        fusion->angular_acceleration_rad_per_second2[2] = 0.0F;
    } else {
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            raw[axis] = (angular_rate[axis] -
                fusion->previous_angular_rate_rad_per_second[axis]) /
                dt_seconds;
        }
        if (fusion->config.angular_acceleration_time_constant_seconds >
            0.0F) {
            gain = dt_seconds /
                (fusion->config.angular_acceleration_time_constant_seconds +
                 dt_seconds);
            for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
                fusion->angular_acceleration_rad_per_second2[axis] +=
                    gain * (raw[axis] -
                        fusion->angular_acceleration_rad_per_second2[axis]);
            }
        } else {
            for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
                fusion->angular_acceleration_rad_per_second2[axis] = raw[axis];
            }
        }
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fusion->previous_angular_rate_rad_per_second[axis] = angular_rate[axis];
    }
    fusion->has_previous_angular_rate = true;
    return finite3(fusion->previous_angular_rate_rad_per_second) &&
        finite3(fusion->angular_acceleration_rad_per_second2);
}

static void matrix_zero(float matrix[6][6])
{
    for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
        for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
             ++column) {
            matrix[row][column] = 0.0F;
        }
    }
}

static void matrix_symmetrize(float matrix[6][6])
{
    for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
        for (unsigned int column = row + 1U;
             column < IMU_FUSION_STATE_COUNT; ++column) {
            const float average = 0.5F *
                (matrix[row][column] + matrix[column][row]);
            matrix[row][column] = average;
            matrix[column][row] = average;
        }
    }
}

static bool covariance_finite(float covariance[6][6])
{
    for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
        for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
             ++column) {
            if (!isfinite(covariance[row][column])) {
                return false;
            }
        }
    }
    return true;
}

static void reset_solution_after_calibration(Algorithm_IMUFusion *fusion)
{
    const Algorithm_IMUFusion_Snapshot calibration = fusion->snapshot;

    fusion->snapshot = (Algorithm_IMUFusion_Snapshot){0};
    fusion->snapshot.quaternion.w = 1.0F;
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fusion->snapshot.primary_gyro_bias_dps[axis] =
            calibration.primary_gyro_bias_dps[axis];
        fusion->snapshot.secondary_gyro_bias_dps[axis] =
            calibration.secondary_gyro_bias_dps[axis];
        fusion->snapshot.primary_gyro_bias_fit_slope_dps_per_second[axis] =
            calibration.primary_gyro_bias_fit_slope_dps_per_second[axis];
        fusion->snapshot.secondary_gyro_bias_fit_slope_dps_per_second[axis] =
            calibration.secondary_gyro_bias_fit_slope_dps_per_second[axis];
        fusion->previous_angular_rate_rad_per_second[axis] = 0.0F;
        fusion->angular_acceleration_rad_per_second2[axis] = 0.0F;
        fusion->previous_attitude_angular_rate_rad_per_second[axis] = 0.0F;
        fusion->gyro_rate_estimate_dps[axis] = 0.0F;
        fusion->gyro_rate_variance_dps2[axis] =
            fusion->config.initial_gyro_rate_variance_dps2;
    }
    fusion->snapshot.calibration_settle_samples =
        calibration.calibration_settle_samples;
    fusion->snapshot.calibration_samples_collected =
        calibration.calibration_samples_collected;
    fusion->snapshot.calibration_restart_count =
        calibration.calibration_restart_count;
    fusion->snapshot.calibration_status =
        ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE;
    fusion->snapshot.gyro_bias_valid = true;
    fusion->snapshot.yaw_absolute_observable = false;
    matrix_zero(fusion->covariance);
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fusion->covariance[axis][axis] =
            fusion->config.initial_attitude_variance_rad2;
        fusion->covariance[axis + 3U][axis + 3U] =
            fusion->config.initial_bias_variance_rad2_per_second2;
    }
    fusion->has_previous_angular_rate = false;
    fusion->has_previous_attitude_angular_rate = false;
    fusion->gyro_rate_filter_seeded = false;
    fusion->attitude_seeded = false;
    reset_runtime_bias_window(fusion);
}

static bool predict_covariance(
    Algorithm_IMUFusion *fusion,
    const float angular_rate_rad_per_second[3],
    float dt_seconds)
{
    float transition[6][6];
    float temporary[6][6];
    float predicted[6][6];
    float q_attitude;
    float q_bias;

    matrix_zero(transition);
    for (unsigned int index = 0U; index < IMU_FUSION_STATE_COUNT; ++index) {
        transition[index][index] = 1.0F;
    }
    transition[0][1] = angular_rate_rad_per_second[2] * dt_seconds;
    transition[0][2] = -angular_rate_rad_per_second[1] * dt_seconds;
    transition[1][0] = -angular_rate_rad_per_second[2] * dt_seconds;
    transition[1][2] = angular_rate_rad_per_second[0] * dt_seconds;
    transition[2][0] = angular_rate_rad_per_second[1] * dt_seconds;
    transition[2][1] = -angular_rate_rad_per_second[0] * dt_seconds;
    if (fusion->config.estimate_gyro_bias) {
        transition[0][3] = -dt_seconds;
        transition[1][4] = -dt_seconds;
        transition[2][5] = -dt_seconds;
    }

    for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
        for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
             ++column) {
            float sum = 0.0F;
            for (unsigned int index = 0U; index < IMU_FUSION_STATE_COUNT;
                 ++index) {
                sum += transition[row][index] * fusion->covariance[index][column];
            }
            temporary[row][column] = sum;
        }
    }
    for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
        for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
             ++column) {
            float sum = 0.0F;
            for (unsigned int index = 0U; index < IMU_FUSION_STATE_COUNT;
                 ++index) {
                sum += temporary[row][index] * transition[column][index];
            }
            predicted[row][column] = sum;
        }
    }
    q_attitude = fusion->config.gyro_process_noise_rad2_per_second *
        dt_seconds;
    q_bias = fusion->config.estimate_gyro_bias ?
        fusion->config.gyro_bias_process_noise_rad2_per_second3 * dt_seconds :
        0.0F;
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        predicted[axis][axis] += q_attitude;
        predicted[axis + 3U][axis + 3U] += q_bias;
    }
    for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
        for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
             ++column) {
            fusion->covariance[row][column] = predicted[row][column];
        }
    }
    matrix_symmetrize(fusion->covariance);
    if (!covariance_finite(fusion->covariance)) {
        return false;
    }
    for (unsigned int index = 0U; index < IMU_FUSION_STATE_COUNT; ++index) {
        if (fusion->covariance[index][index] < 0.0F) {
            if (fusion->covariance[index][index] < -1.0e-6F) {
                return false;
            }
            fusion->covariance[index][index] = 0.0F;
        }
    }
    return true;
}

static bool apply_gravity_update(
    Algorithm_IMUFusion *fusion,
    const float normalized_acceleration[3],
    float acceleration_norm_error_mps2,
    bool *correction_used)
{
    float predicted_gravity[3];
    float innovation[3];
    float delta[6] = {0.0F};
    float h[6];
    float ph[6];
    float gain[6];
    float innovation_norm;
    float measurement_variance;
    float measurement_confidence = 1.0F;
    float yaw_before_correction;

    if ((fusion == NULL) || !finite3(normalized_acceleration) ||
        !valid_nonnegative(acceleration_norm_error_mps2) ||
        (correction_used == NULL)) {
        return false;
    }
    *correction_used = false;
    yaw_before_correction =
        quaternion_yaw_radians(&fusion->snapshot.quaternion);
    if (!isfinite(yaw_before_correction)) {
        return false;
    }
    gravity_from_quaternion(&fusion->snapshot.quaternion, predicted_gravity);
    if (!normalize3(predicted_gravity)) {
        return false;
    }
    innovation[0] = normalized_acceleration[0] - predicted_gravity[0];
    innovation[1] = normalized_acceleration[1] - predicted_gravity[1];
    innovation[2] = normalized_acceleration[2] - predicted_gravity[2];
    innovation_norm = norm3(innovation);
    if (!isfinite(innovation_norm)) {
        return false;
    }
    if (innovation_norm > fusion->config.maximum_gravity_direction_innovation) {
        return true;
    }

    /* A valid acceleration magnitude alone does not prove that the measured
     * direction is gravity.  During translation and impact, common linear
     * acceleration can affect both IMUs equally.  Fade the gravity update as
     * either the magnitude residual or the gyro-predicted direction residual
     * approaches its hard gate.  Genuine fast rotation remains observable by
     * the gyroscopes and therefore keeps a small direction residual. */
    if (fusion->config.acceleration_norm_tolerance_mps2 >
        IMU_FUSION_EPSILON) {
        const float ratio = acceleration_norm_error_mps2 /
            fusion->config.acceleration_norm_tolerance_mps2;

        measurement_confidence *= fmaxf(0.0F, 1.0F - (ratio * ratio));
    } else if (acceleration_norm_error_mps2 > IMU_FUSION_EPSILON) {
        return true;
    }
    if (fusion->config.maximum_gravity_direction_innovation >
        IMU_FUSION_EPSILON) {
        const float ratio = innovation_norm /
            fusion->config.maximum_gravity_direction_innovation;

        measurement_confidence *= fmaxf(0.0F, 1.0F - (ratio * ratio));
    } else if (innovation_norm > IMU_FUSION_EPSILON) {
        return true;
    }
    if (!isfinite(measurement_confidence) ||
        (measurement_confidence < IMU_FUSION_MIN_GRAVITY_CONFIDENCE)) {
        return true;
    }
    measurement_variance =
        fusion->config.gravity_direction_measurement_variance /
        (measurement_confidence * measurement_confidence);
    if (!valid_positive(measurement_variance)) {
        return false;
    }

    /* h = [g]x for a right-multiplicative body-frame attitude error. */
    for (unsigned int measurement = 0U; measurement < IMU_FUSION_AXIS_COUNT;
         ++measurement) {
        for (unsigned int index = 0U; index < IMU_FUSION_STATE_COUNT; ++index) {
            h[index] = 0.0F;
        }
        if (measurement == 0U) {
            h[1] = -predicted_gravity[2];
            h[2] = predicted_gravity[1];
        } else if (measurement == 1U) {
            h[0] = predicted_gravity[2];
            h[2] = -predicted_gravity[0];
        } else {
            h[0] = -predicted_gravity[1];
            h[1] = predicted_gravity[0];
        }
        for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
            float sum = 0.0F;
            for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
                 ++column) {
                sum += fusion->covariance[row][column] * h[column];
            }
            ph[row] = sum;
            gain[row] = sum;
            if (!fusion->config.estimate_gyro_bias && (row >= 3U)) {
                gain[row] = 0.0F;
            }
        }
        {
            float denominator = measurement_variance;
            float residual;

            for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
                 ++column) {
                denominator += h[column] * ph[column];
            }
            if (!isfinite(denominator) ||
                (denominator <= IMU_FUSION_EPSILON)) {
                return false;
            }
            for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
                gain[row] /= denominator;
            }
            {
                const float unobservable_gain =
                    (gain[0] * predicted_gravity[0]) +
                    (gain[1] * predicted_gravity[1]) +
                    (gain[2] * predicted_gravity[2]);

                /* Gravity has no information about rotation around itself.
                 * Cross-covariance can otherwise give the unconstrained
                 * Kalman gain a component in that yaw-null direction and
                 * inject a false heading correction while leveling. */
                gain[0] -= unobservable_gain * predicted_gravity[0];
                gain[1] -= unobservable_gain * predicted_gravity[1];
                gain[2] -= unobservable_gain * predicted_gravity[2];
            }
            residual = innovation[measurement];
            for (unsigned int column = 0U; column < IMU_FUSION_STATE_COUNT;
                 ++column) {
                residual -= h[column] * delta[column];
            }
            if (!isfinite(residual)) {
                return false;
            }
            for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
                delta[row] += gain[row] * residual;
            }
            /* Joseph form reduced to scalar arithmetic.  ph is P*h and
             * denominator is h*P*h+R, so this avoids a second 6x6 copy. */
            for (unsigned int row = 0U; row < IMU_FUSION_STATE_COUNT; ++row) {
                for (unsigned int column = 0U;
                     column < IMU_FUSION_STATE_COUNT; ++column) {
                    fusion->covariance[row][column] -=
                        gain[row] * ph[column] + ph[row] * gain[column] -
                        gain[row] * denominator * gain[column];
                }
            }
        }
    }
    matrix_symmetrize(fusion->covariance);
    if (!covariance_finite(fusion->covariance)) {
        return false;
    }
    for (unsigned int index = 0U; index < IMU_FUSION_STATE_COUNT; ++index) {
        if (!isfinite(delta[index])) {
            return false;
        }
    }
    for (unsigned int index = 0U; index < IMU_FUSION_STATE_COUNT; ++index) {
        if (fusion->covariance[index][index] < 0.0F) {
            if (fusion->covariance[index][index] < -1.0e-5F) {
                return false;
            }
            fusion->covariance[index][index] = 0.0F;
        }
    }
    {
        Algorithm_Mahony_Quaternion correction = {
            1.0F, 0.5F * delta[0], 0.5F * delta[1], 0.5F * delta[2]};
        Algorithm_Mahony_Quaternion updated;
        Algorithm_Mahony_Quaternion yaw_preserved;
        Algorithm_Mahony_Quaternion yaw_correction;
        float yaw_after_correction;
        float yaw_offset;

        if (!finite4(&correction)) {
            return false;
        }
        quaternion_multiply(&fusion->snapshot.quaternion, &correction,
            &updated);
        if (!normalize_quaternion(&updated)) {
            return false;
        }
        yaw_after_correction = quaternion_yaw_radians(&updated);
        if (!isfinite(yaw_after_correction)) {
            return false;
        }
        yaw_offset = yaw_before_correction - yaw_after_correction;
        while (yaw_offset > IMU_FUSION_PI) {
            yaw_offset -= 2.0F * IMU_FUSION_PI;
        }
        while (yaw_offset < -IMU_FUSION_PI) {
            yaw_offset += 2.0F * IMU_FUSION_PI;
        }
        yaw_correction.w = cosf(0.5F * yaw_offset);
        yaw_correction.x = 0.0F;
        yaw_correction.y = 0.0F;
        yaw_correction.z = sinf(0.5F * yaw_offset);
        quaternion_multiply(&yaw_correction, &updated, &yaw_preserved);
        if (!normalize_quaternion(&yaw_preserved)) {
            return false;
        }
        fusion->snapshot.quaternion = yaw_preserved;
        if (fusion->config.estimate_gyro_bias) {
            for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
                fusion->snapshot.gyro_bias_dps[axis] +=
                    delta[axis + 3U] * IMU_FUSION_RAD_TO_DEG;
            }
            if (!finite3(fusion->snapshot.gyro_bias_dps)) {
                return false;
            }
        }
        *correction_used = true;
    }
    return true;
}

static bool integrate_quaternion(
    Algorithm_Mahony_Quaternion *quaternion,
    const float angular_rate_rad_per_second[3],
    const float previous_angular_rate_rad_per_second[3],
    bool has_previous_angular_rate,
    float dt_seconds)
{
    float rotation_vector[3];
    float coning[3] = {0.0F, 0.0F, 0.0F};
    float angle;
    float vector_scale;
    Algorithm_Mahony_Quaternion increment;
    Algorithm_Mahony_Quaternion updated;

    if ((quaternion == NULL) || !finite4(quaternion) ||
        !finite3(angular_rate_rad_per_second) ||
        (has_previous_angular_rate &&
         !finite3(previous_angular_rate_rad_per_second)) ||
        !valid_positive(dt_seconds)) {
        return false;
    }
    if (has_previous_angular_rate) {
        cross3(previous_angular_rate_rad_per_second,
            angular_rate_rad_per_second, coning);
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            rotation_vector[axis] = 0.5F *
                (previous_angular_rate_rad_per_second[axis] +
                 angular_rate_rad_per_second[axis]) * dt_seconds +
                coning[axis] * dt_seconds * dt_seconds / 12.0F;
        }
    } else {
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            rotation_vector[axis] =
                angular_rate_rad_per_second[axis] * dt_seconds;
        }
    }
    angle = norm3(rotation_vector);
    if (!isfinite(angle)) {
        return false;
    }
    if (angle > 1.0e-6F) {
        vector_scale = sinf(0.5F * angle) / angle;
    } else {
        const float angle_squared = angle * angle;

        vector_scale = 0.5F - (angle_squared / 48.0F);
    }
    increment.w = cosf(0.5F * angle);
    increment.x = vector_scale * rotation_vector[0];
    increment.y = vector_scale * rotation_vector[1];
    increment.z = vector_scale * rotation_vector[2];
    if (!finite4(&increment)) {
        return false;
    }
    quaternion_multiply(quaternion, &increment, &updated);
    if (!normalize_quaternion(&updated)) {
        return false;
    }
    *quaternion = updated;
    return true;
}

bool Algorithm_IMUFusion_Init(
    Algorithm_IMUFusion *fusion,
    const Algorithm_IMUFusion_Config *config)
{
    const Algorithm_IMUFusion_Config selected = (config == NULL) ?
        Algorithm_IMUFusion_DefaultConfig() : *config;

    if (fusion != NULL) {
        *fusion = (Algorithm_IMUFusion){0};
    }
    if ((fusion == NULL) || !valid_config(&selected)) {
        return false;
    }
    fusion->config = selected;
    fusion->snapshot.quaternion.w = 1.0F;
    fusion->snapshot.calibration_status =
        selected.enable_startup_gyro_bias_calibration ?
        ALGORITHM_IMU_FUSION_CALIBRATION_SETTLING :
        ALGORITHM_IMU_FUSION_CALIBRATION_DISABLED;
    fusion->snapshot.yaw_absolute_observable = false;
    fusion->snapshot.valid =
        !selected.enable_startup_gyro_bias_calibration;
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fusion->covariance[axis][axis] = selected.initial_attitude_variance_rad2;
        fusion->covariance[axis + 3U][axis + 3U] =
            selected.initial_bias_variance_rad2_per_second2;
        fusion->gyro_rate_variance_dps2[axis] =
            selected.initial_gyro_rate_variance_dps2;
    }
    fusion->initialized = true;
    return true;
}

bool Algorithm_IMUFusion_Update(
    Algorithm_IMUFusion *fusion,
    const Algorithm_IMUFusion_Input *input,
    Algorithm_IMUFusion_Snapshot *snapshot)
{
    Algorithm_IMUFusion next;
    float primary_acceleration[3] = {0.0F, 0.0F, 0.0F};
    float secondary_acceleration[3] = {0.0F, 0.0F, 0.0F};
    float primary_angular_rate[3] = {0.0F, 0.0F, 0.0F};
    float secondary_angular_rate[3] = {0.0F, 0.0F, 0.0F};
    float fused_angular_rate_dps[3] = {0.0F, 0.0F, 0.0F};
    float measured_angular_rate_dps[3] = {0.0F, 0.0F, 0.0F};
    float fused_angular_rate_rad_per_second[3] = {0.0F, 0.0F, 0.0F};
    float measured_angular_rate_rad_per_second[3] = {0.0F, 0.0F, 0.0F};
    float corrected_secondary_acceleration[3] = {0.0F, 0.0F, 0.0F};
    float fused_acceleration[3] = {0.0F, 0.0F, 0.0F};
    float normalized_acceleration[3] = {0.0F, 0.0F, 0.0F};
    float lever_acceleration[3] = {0.0F, 0.0F, 0.0F};
    float centripetal[3];
    float tangential[3];
    float omega_cross_r[3];
    float norm;
    uint8_t gyro_source_mask;
    uint8_t acceleration_source_mask;
    bool acceleration_correction_used = false;
    bool acceleration_direction_usable = false;
    bool calibration_completed = false;

    if ((fusion == NULL) || !fusion->initialized || (input == NULL) ||
        (snapshot == NULL) || !isfinite(input->dt_seconds) ||
        (input->dt_seconds <= 0.0F) ||
        (input->dt_seconds > fusion->config.maximum_dt_seconds) ||
        fusion->config.estimate_gyro_bias) {
        return false;
    }
    if (input->primary_valid &&
        (!finite3(input->primary_acceleration_mps2) ||
         !finite3(input->primary_angular_rate_dps))) {
        return false;
    }
    if (input->secondary_valid &&
        (!finite3(input->secondary_acceleration_mps2) ||
         !finite3(input->secondary_angular_rate_dps))) {
        return false;
    }
    if (!input->primary_valid && !input->secondary_valid &&
        (!fusion->config.enable_startup_gyro_bias_calibration ||
         (fusion->snapshot.calibration_status ==
          ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE))) {
        return false;
    }

    next = *fusion;
    if (input->primary_valid) {
        map_vector(next.config.primary_body_axis,
            input->primary_acceleration_mps2, primary_acceleration);
        map_vector(next.config.primary_body_axis,
            input->primary_angular_rate_dps, primary_angular_rate);
    }
    if (input->secondary_valid) {
        map_vector(next.config.secondary_body_axis,
            input->secondary_acceleration_mps2, secondary_acceleration);
        map_vector(next.config.secondary_body_axis,
            input->secondary_angular_rate_dps, secondary_angular_rate);
    }
    if (next.config.enable_startup_gyro_bias_calibration &&
        (next.snapshot.calibration_status !=
         ALGORITHM_IMU_FUSION_CALIBRATION_COMPLETE)) {
        if (!update_startup_calibration(&next,
                input->primary_valid, input->secondary_valid,
                primary_acceleration, primary_angular_rate,
                secondary_acceleration, secondary_angular_rate,
                input->dt_seconds,
                &calibration_completed)) {
            return false;
        }
        if (!calibration_completed) {
            *fusion = next;
            *snapshot = next.snapshot;
            return true;
        }
        reset_solution_after_calibration(&next);
    }
    if (next.snapshot.gyro_bias_valid) {
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            if (input->primary_valid) {
                primary_angular_rate[axis] -=
                    next.snapshot.primary_gyro_bias_dps[axis];
            }
            if (input->secondary_valid) {
                secondary_angular_rate[axis] -=
                    next.snapshot.secondary_gyro_bias_dps[axis];
            }
        }
    }
    /* The refreshed biases only affect subsequent frames; this frame keeps
     * the residual rates computed above. */
    update_runtime_gyro_bias(&next, input->primary_valid,
        primary_angular_rate, primary_acceleration,
        input->secondary_valid, secondary_angular_rate,
        secondary_acceleration, input->odometry_stationary,
        input->dt_seconds);
    if (!fuse_gyro_with_prediction(&next, primary_angular_rate,
            input->primary_valid, secondary_angular_rate,
            input->secondary_valid, input->dt_seconds,
            fused_angular_rate_dps, measured_angular_rate_dps,
            &gyro_source_mask)) {
        return false;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fused_angular_rate_rad_per_second[axis] =
            fused_angular_rate_dps[axis] * IMU_FUSION_DEG_TO_RAD;
        measured_angular_rate_rad_per_second[axis] =
            measured_angular_rate_dps[axis] * IMU_FUSION_DEG_TO_RAD;
    }
    if (!finite3(fused_angular_rate_rad_per_second) ||
        !finite3(measured_angular_rate_rad_per_second) ||
        !update_angular_acceleration(&next,
            measured_angular_rate_rad_per_second, input->dt_seconds)) {
        return false;
    }

    if (input->secondary_valid) {
        cross3(next.angular_acceleration_rad_per_second2,
            next.config.secondary_position_m, tangential);
        cross3(fused_angular_rate_rad_per_second,
            next.config.secondary_position_m, omega_cross_r);
        cross3(fused_angular_rate_rad_per_second, omega_cross_r, centripetal);
        for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
            lever_acceleration[axis] = tangential[axis] + centripetal[axis];
            corrected_secondary_acceleration[axis] =
                secondary_acceleration[axis] - lever_acceleration[axis];
        }
    }
    if (!fuse_vector(primary_acceleration, input->primary_valid,
            next.config.primary_acceleration_variance_mps4,
            corrected_secondary_acceleration, input->secondary_valid,
            next.config.secondary_acceleration_variance_mps4,
            next.config.maximum_acceleration_disagreement_mps2,
            fused_acceleration, &acceleration_source_mask)) {
        return false;
    }
    norm = norm3(fused_acceleration);
    if (!isfinite(norm)) {
        return false;
    }
    if ((norm > IMU_FUSION_EPSILON) &&
        (fabsf(norm - next.config.gravity_mps2) <=
         next.config.acceleration_norm_tolerance_mps2)) {
        normalized_acceleration[0] = fused_acceleration[0] / norm;
        normalized_acceleration[1] = fused_acceleration[1] / norm;
        normalized_acceleration[2] = fused_acceleration[2] / norm;
        acceleration_direction_usable = normalize3(normalized_acceleration);
    }

    if (!next.attitude_seeded && acceleration_direction_usable) {
        quaternion_from_level_acceleration(normalized_acceleration,
            &next.snapshot.quaternion);
        if (!normalize_quaternion(&next.snapshot.quaternion)) {
            return false;
        }
        next.attitude_seeded = true;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        fused_angular_rate_rad_per_second[axis] -=
            next.snapshot.gyro_bias_dps[axis] * IMU_FUSION_DEG_TO_RAD;
    }
    if (!finite3(fused_angular_rate_rad_per_second) ||
        !integrate_quaternion(&next.snapshot.quaternion,
            fused_angular_rate_rad_per_second,
            next.previous_attitude_angular_rate_rad_per_second,
            next.has_previous_attitude_angular_rate,
            input->dt_seconds) ||
        !predict_covariance(&next, fused_angular_rate_rad_per_second,
            input->dt_seconds)) {
        return false;
    }
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        next.previous_attitude_angular_rate_rad_per_second[axis] =
            fused_angular_rate_rad_per_second[axis];
    }
    next.has_previous_attitude_angular_rate = true;
    if (acceleration_direction_usable &&
        !apply_gravity_update(&next, normalized_acceleration,
            fabsf(norm - next.config.gravity_mps2),
            &acceleration_correction_used)) {
        return false;
    }
    if (!quaternion_to_euler(&next.snapshot.quaternion, &next.snapshot.euler)) {
        return false;
    }
    next.snapshot.acceleration_mps2[0] = fused_acceleration[0];
    next.snapshot.acceleration_mps2[1] = fused_acceleration[1];
    next.snapshot.acceleration_mps2[2] = fused_acceleration[2];
    for (unsigned int axis = 0U; axis < IMU_FUSION_AXIS_COUNT; ++axis) {
        next.snapshot.angular_rate_dps[axis] =
            fused_angular_rate_dps[axis];
        next.snapshot.secondary_lever_acceleration_mps2[axis] =
            lever_acceleration[axis];
    }
    next.snapshot.gyro_source_mask = gyro_source_mask;
    next.snapshot.acceleration_source_mask = acceleration_source_mask;
    next.snapshot.acceleration_correction_used = acceleration_correction_used;
    next.snapshot.secondary_lever_compensation_used =
        (input->secondary_valid && ((acceleration_source_mask & 0x02U) != 0U) &&
         (norm3(next.config.secondary_position_m) > IMU_FUSION_EPSILON));
    next.snapshot.yaw_absolute_observable = false;
    next.snapshot.valid = true;
    next.snapshot.update_count = increment_saturating(
        next.snapshot.update_count);

    if (!finite3(next.snapshot.acceleration_mps2) ||
        !finite3(next.snapshot.angular_rate_dps) ||
        !finite3(next.snapshot.gyro_bias_dps) ||
        !finite3(next.snapshot.gyro_prediction_weight) ||
        !finite3(next.snapshot.primary_gyro_weight) ||
        !finite3(next.snapshot.secondary_gyro_weight) ||
        !finite3(next.gyro_rate_estimate_dps) ||
        !finite3(next.gyro_rate_variance_dps2) ||
        !finite3(next.previous_attitude_angular_rate_rad_per_second) ||
        !finite3(next.snapshot.secondary_lever_acceleration_mps2) ||
        !covariance_finite(next.covariance)) {
        return false;
    }
    *fusion = next;
    *snapshot = next.snapshot;
    return true;
}
