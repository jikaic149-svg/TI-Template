#ifndef ALGORITHM_PID_H
#define ALGORITHM_PID_H

#include <stdbool.h>

typedef enum {
    ALGORITHM_PID_DERIVATIVE_ON_ERROR = 0,
    ALGORITHM_PID_DERIVATIVE_ON_MEASUREMENT,
} Algorithm_PID_DerivativeMode;

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral_min;
    float integral_max;
    float output_min;
    float output_max;
    /* <= 0 disables integral separation. Otherwise integration occurs only
     * while abs(error) <= integral_separation. */
    float integral_separation;
    Algorithm_PID_DerivativeMode derivative_mode;
} Algorithm_PID_Config;

typedef struct {
    Algorithm_PID_Config config;
    float integral;
    float previous_error;
    float previous_measurement;
    bool initialized;
} Algorithm_PID;

/* All functions reject non-finite values and invalid ranges. On failure an
 * output argument and PID state remain unchanged. */
bool Algorithm_PID_Clamp(
    float value, float minimum, float maximum, float *output);
bool Algorithm_PID_Deadband(
    float value, float half_width, float *output);
bool Algorithm_PID_NormalizeAngleDeg(float angle_deg, float *output_deg);
bool Algorithm_PID_AngleErrorDeg(
    float target_deg, float measured_deg, float *error_deg);

/* Move current toward target by at most rate_per_second * dt_seconds. */
bool Algorithm_PID_Slew(
    float current,
    float target,
    float rate_per_second,
    float dt_seconds,
    float *output);

bool Algorithm_PID_Init(
    Algorithm_PID *pid,
    const Algorithm_PID_Config *config);
/* Reset/Update require a PID instance previously initialized successfully. */
void Algorithm_PID_Reset(Algorithm_PID *pid);
bool Algorithm_PID_Update(
    Algorithm_PID *pid,
    float setpoint,
    float measurement,
    float dt_seconds,
    float *output);

#endif
