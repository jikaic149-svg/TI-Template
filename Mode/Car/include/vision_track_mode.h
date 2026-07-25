#ifndef VISION_TRACK_MODE_H
#define VISION_TRACK_MODE_H

#include <stdbool.h>

#include "mode_car_control.h"

typedef struct {
    float forward_speed_mm_per_second;
    float position_error_gain_deg;
    float maximum_steering_error_deg;
    Mode_Car_AngleControllerConfig yaw_controller;
    Mode_Car_SpeedControllerConfig speed_controller;
} Mode_Car_VisionTrackConfig;

typedef struct {
    float dt_seconds;
    bool track_valid;
    float control_error_x;
    float control_heading_deg;
    float measured_yaw_deg;
    float measured_yaw_rate_deg_per_second;
    float measured_speed_a_mm_per_second;
    float measured_speed_b_mm_per_second;
} Mode_Car_VisionTrackInput;

typedef struct {
    bool track_active;
    float steering_error_deg;
    float target_yaw_deg;
    float turn_speed_mm_per_second;
    float target_speed_a_mm_per_second;
    float target_speed_b_mm_per_second;
    float effort_a_percent;
    float effort_b_percent;
} Mode_Car_VisionTrackOutput;

typedef struct {
    Mode_Car_VisionTrackConfig config;
    Mode_Car_AngleController yaw_controller;
    Mode_Car_SpeedController speed_controller;
    bool initialized;
} Mode_Car_VisionTrack;

Mode_Car_VisionTrackConfig Mode_Car_VisionTrack_DefaultConfig(void);
bool Mode_Car_VisionTrack_Init(
    Mode_Car_VisionTrack *mode,
    const Mode_Car_VisionTrackConfig *config);
void Mode_Car_VisionTrack_Reset(Mode_Car_VisionTrack *mode);
bool Mode_Car_VisionTrack_Update(
    Mode_Car_VisionTrack *mode,
    const Mode_Car_VisionTrackInput *input,
    Mode_Car_VisionTrackOutput *output);

#endif
