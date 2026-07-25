#ifndef ALGORITHM_MAHONY_H
#define ALGORITHM_MAHONY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float w;
    float x;
    float y;
    float z;
} Algorithm_Mahony_Quaternion;

typedef struct {
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
} Algorithm_Mahony_Euler;

typedef struct {
    /* Mahony nonlinear complementary-filter gains in 1/s and 1/s^2. */
    float proportional_gain;
    float integral_gain;
    float acceleration_reference_mps2;
    float acceleration_tolerance_mps2;
    float maximum_dt_seconds;
    float gyro_bias_rad_per_second[3];
} Algorithm_Mahony_Config;

typedef struct {
    float acceleration_mps2[3];
    float angular_rate_rad_per_second[3];
    float dt_seconds;
} Algorithm_Mahony_Input;

typedef struct {
    Algorithm_Mahony_Quaternion quaternion;
    Algorithm_Mahony_Euler euler;
    float gyro_bias_rad_per_second[3];
    uint32_t update_count;
    bool acceleration_correction_used;
    /* A 6-axis filter has no absolute heading observation. This remains false;
     * yaw is gyro integration and will drift with residual Z-axis bias. */
    bool yaw_absolute_observable;
    bool valid;
} Algorithm_Mahony_Snapshot;

typedef struct {
    Algorithm_Mahony_Config config;
    Algorithm_Mahony_Snapshot snapshot;
    float integral_feedback_rad_per_second[3];
    bool initialized;
} Algorithm_Mahony;

Algorithm_Mahony_Config Algorithm_Mahony_DefaultConfig(void);
bool Algorithm_Mahony_Init(
    Algorithm_Mahony *attitude,
    const Algorithm_Mahony_Config *config);

/* Input acceleration must use a body frame whose stationary level vector is
 * +Z. Gyroscope axes use the same right-handed body frame and radians/second.
 * Update is transactional: failure leaves context and output unchanged. */
bool Algorithm_Mahony_Update(
    Algorithm_Mahony *attitude,
    const Algorithm_Mahony_Input *input,
    Algorithm_Mahony_Snapshot *snapshot);

/* Quaternion input may be non-unit but must have a finite nonzero norm.
 * Setting orientation resets integral feedback but preserves configured bias. */
bool Algorithm_Mahony_SetQuaternion(
    Algorithm_Mahony *attitude,
    const Algorithm_Mahony_Quaternion *quaternion);
bool Algorithm_Mahony_SetGyroBias(
    Algorithm_Mahony *attitude,
    const float bias_rad_per_second[3]);
bool Algorithm_Mahony_GetSnapshot(
    const Algorithm_Mahony *attitude,
    Algorithm_Mahony_Snapshot *snapshot);

#endif
