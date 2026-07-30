#include "mode_car_control.h"
#include "core_config.h"

#include <math.h>
#include <stddef.h>

static bool finite_value(float value)
{
    return isfinite(value) != 0;
}

static bool valid_config(const Mode_Car_LinearProfileConfig *config)
{
    return (config != NULL) &&
        finite_value(config->maximum_speed_mm_per_second) &&
        finite_value(config->acceleration_up_mm_per_second2) &&
        finite_value(config->acceleration_down_mm_per_second2) &&
        finite_value(config->guard_distance_mm) &&
        finite_value(config->preview_time_seconds) &&
        finite_value(config->stopped_speed_threshold_mm_per_second) &&
        (config->maximum_speed_mm_per_second > 0.0F) &&
        (config->acceleration_up_mm_per_second2 > 0.0F) &&
        (config->acceleration_down_mm_per_second2 > 0.0F) &&
        (config->guard_distance_mm >= 0.0F) &&
        (config->preview_time_seconds >= 0.0F) &&
        (config->stopped_speed_threshold_mm_per_second >= 0.0F);
}

static bool valid_input(const Mode_Car_LinearProfileInput *input)
{
    return (input != NULL) && finite_value(input->dt_seconds) &&
        finite_value(input->odometry_distance_mm) &&
        finite_value(input->measured_speed_mm_per_second) &&
        (input->dt_seconds > 0.0F);
}

static float clamp_nonnegative(float value)
{
    return (value > 0.0F) ? value : 0.0F;
}

static float stopping_distance(
    const Mode_Car_LinearSpeedProfile *profile, float speed)
{
    const float speed_abs = fabsf(speed);

    return (speed_abs * speed_abs) /
        (2.0F * profile->config.acceleration_down_mm_per_second2);
}

Mode_Car_LinearProfileConfig Mode_Car_LinearProfile_DefaultConfig(void)
{
    return (Mode_Car_LinearProfileConfig){
        .maximum_speed_mm_per_second =
            TEMPLATE_CAR_PROFILE_MAXIMUM_SPEED_MM_PER_SECOND,
        .acceleration_up_mm_per_second2 =
            TEMPLATE_CAR_PROFILE_ACCELERATION_UP_MM_PER_SECOND2,
        .acceleration_down_mm_per_second2 =
            TEMPLATE_CAR_PROFILE_ACCELERATION_DOWN_MM_PER_SECOND2,
        .guard_distance_mm = TEMPLATE_CAR_PROFILE_GUARD_DISTANCE_MM,
        .preview_time_seconds = TEMPLATE_CAR_PROFILE_PREVIEW_TIME_SECONDS,
        .stopped_speed_threshold_mm_per_second =
            TEMPLATE_CAR_PROFILE_STOPPED_SPEED_THRESHOLD_MM_PER_SECOND,
    };
}

bool Mode_Car_LinearSpeedProfile_Init(
    Mode_Car_LinearSpeedProfile *profile,
    const Mode_Car_LinearProfileConfig *config)
{
    if (profile != NULL) {
        *profile = (Mode_Car_LinearSpeedProfile){0};
    }
    if ((profile == NULL) || !valid_config(config)) {
        return false;
    }
    profile->config = *config;
    profile->output.state = MODE_CAR_PROFILE_IDLE;
    profile->initialized = true;
    return true;
}

void Mode_Car_LinearSpeedProfile_Reset(
    Mode_Car_LinearSpeedProfile *profile)
{
    if ((profile == NULL) || !profile->initialized) {
        return;
    }
    profile->output = (Mode_Car_LinearProfileOutput){0};
    profile->output.state = MODE_CAR_PROFILE_IDLE;
    profile->running = false;
    profile->stop_requested = false;
    profile->goal_valid = false;
}

bool Mode_Car_LinearSpeedProfile_Start(
    Mode_Car_LinearSpeedProfile *profile,
    float odometry_distance_mm,
    float cruise_speed_mm_per_second)
{
    if ((profile == NULL) || !profile->initialized ||
        !finite_value(odometry_distance_mm) ||
        !finite_value(cruise_speed_mm_per_second) ||
        (cruise_speed_mm_per_second < 0.0F) ||
        (cruise_speed_mm_per_second >
            profile->config.maximum_speed_mm_per_second)) {
        return false;
    }
    profile->start_distance_mm = odometry_distance_mm;
    profile->goal_distance_mm = 0.0F;
    profile->cruise_speed_mm_per_second = cruise_speed_mm_per_second;
    profile->output = (Mode_Car_LinearProfileOutput){0};
    profile->output.state = MODE_CAR_PROFILE_ACCELERATING;
    profile->running = true;
    profile->stop_requested = false;
    profile->goal_valid = false;
    return true;
}

bool Mode_Car_LinearSpeedProfile_SetCruiseSpeed(
    Mode_Car_LinearSpeedProfile *profile,
    float cruise_speed_mm_per_second)
{
    if ((profile == NULL) || !profile->initialized || !profile->running ||
        !finite_value(cruise_speed_mm_per_second) ||
        (cruise_speed_mm_per_second < 0.0F) ||
        (cruise_speed_mm_per_second >
            profile->config.maximum_speed_mm_per_second)) {
        return false;
    }
    profile->cruise_speed_mm_per_second = cruise_speed_mm_per_second;
    return true;
}

bool Mode_Car_LinearSpeedProfile_SetGoal(
    Mode_Car_LinearSpeedProfile *profile,
    float goal_distance_mm)
{
    if ((profile == NULL) || !profile->initialized || !profile->running ||
        !finite_value(goal_distance_mm)) {
        return false;
    }
    profile->goal_distance_mm = goal_distance_mm;
    profile->goal_valid = true;
    return true;
}

bool Mode_Car_LinearSpeedProfile_RequestStop(
    Mode_Car_LinearSpeedProfile *profile,
    float odometry_distance_mm,
    float measured_speed_mm_per_second)
{
    float stop_distance;

    if ((profile == NULL) || !profile->initialized || !profile->running ||
        !finite_value(odometry_distance_mm) ||
        !finite_value(measured_speed_mm_per_second)) {
        return false;
    }
    if (profile->stop_requested) {
        return true;
    }
    stop_distance = stopping_distance(profile,
        fmaxf(fabsf(measured_speed_mm_per_second),
            profile->output.reference_speed_mm_per_second));
    if (!finite_value(stop_distance)) {
        return false;
    }
    profile->goal_distance_mm = odometry_distance_mm + stop_distance +
        profile->config.guard_distance_mm;
    /* Extending the goal for a high measured speed must not make a stop
     * request raise the commanded forward speed. */
    profile->cruise_speed_mm_per_second =
        profile->output.reference_speed_mm_per_second;
    profile->goal_valid = true;
    profile->stop_requested = true;
    return true;
}

bool Mode_Car_LinearSpeedProfile_Update(
    Mode_Car_LinearSpeedProfile *profile,
    const Mode_Car_LinearProfileInput *input,
    Mode_Car_LinearProfileOutput *output)
{
    Mode_Car_LinearSpeedProfile next;
    Mode_Car_LinearProfileOutput next_output;
    float remaining = 0.0F;
    float braking_distance = 0.0F;
    float braking_speed_cap;
    float speed_cap;
    float speed;
    float previous_speed;
    float delta;
    bool braking = false;

    if (output != NULL) {
        *output = (Mode_Car_LinearProfileOutput){0};
        output->state = MODE_CAR_PROFILE_IDLE;
    }
    if ((profile == NULL) || !profile->initialized ||
        !valid_config(&profile->config) || !valid_input(input) ||
        (output == NULL)) {
        return false;
    }

    next = *profile;
    next_output = next.output;
    if (!next.running) {
        next_output.reference_speed_mm_per_second = 0.0F;
        next_output.reference_acceleration_mm_per_second2 = 0.0F;
        next_output.running = false;
        next_output.stopped =
            next.output.state == MODE_CAR_PROFILE_STOPPED;
        next_output.valid = true;
        *profile = next;
        *output = next_output;
        return true;
    }

    if (next.goal_valid) {
        remaining = next.goal_distance_mm - input->odometry_distance_mm;
        braking_distance = stopping_distance(&next,
            next.output.reference_speed_mm_per_second);
        braking_speed_cap = sqrtf(
            2.0F * next.config.acceleration_down_mm_per_second2 *
            clamp_nonnegative(remaining - next.config.guard_distance_mm));
        if (!finite_value(braking_speed_cap)) {
            return false;
        }
        if (braking_speed_cap < next.cruise_speed_mm_per_second) {
            braking = true;
        }
        speed_cap = fminf(
            next.cruise_speed_mm_per_second, braking_speed_cap);
        if (remaining <= next.config.guard_distance_mm) {
            speed_cap = 0.0F;
            braking = true;
        }
    }
    else {
        speed_cap = next.cruise_speed_mm_per_second;
    }
    if (speed_cap > next.config.maximum_speed_mm_per_second) {
        speed_cap = next.config.maximum_speed_mm_per_second;
    }

    speed = next.output.reference_speed_mm_per_second;
    previous_speed = speed;
    if (speed < speed_cap) {
        delta = next.config.acceleration_up_mm_per_second2 *
            input->dt_seconds;
        speed = fminf(speed + delta, speed_cap);
    }
    else if (speed > speed_cap) {
        delta = next.config.acceleration_down_mm_per_second2 *
            input->dt_seconds;
        speed = fmaxf(speed - delta, speed_cap);
    }
    if (!finite_value(speed) || !finite_value(remaining)) {
        return false;
    }
    next_output.reference_acceleration_mm_per_second2 =
        (speed - previous_speed) / input->dt_seconds;
    if (!finite_value(next_output.reference_acceleration_mm_per_second2)) {
        return false;
    }
    next_output.reference_speed_mm_per_second = speed;
    next_output.braking_speed_cap_mm_per_second = speed_cap;
    next_output.remaining_distance_mm = remaining;
    next_output.braking_trigger_distance_mm =
        braking_distance + next.config.guard_distance_mm;
    next_output.announce_distance_mm =
        next_output.braking_trigger_distance_mm +
        fmaxf(fabsf(input->measured_speed_mm_per_second), speed) *
            next.config.preview_time_seconds;
    next_output.goal_valid = next.goal_valid;
    next_output.running = true;
    next_output.braking = braking;
    next_output.state = braking ? MODE_CAR_PROFILE_BRAKING :
        ((speed < next.cruise_speed_mm_per_second) ?
            MODE_CAR_PROFILE_ACCELERATING : MODE_CAR_PROFILE_CRUISING);
    next_output.stopped = next.goal_valid &&
        (speed <= next.config.stopped_speed_threshold_mm_per_second) &&
        (fabsf(input->measured_speed_mm_per_second) <=
            next.config.stopped_speed_threshold_mm_per_second) &&
        (remaining <= next.config.guard_distance_mm);
    if (next_output.stopped) {
        next_output.reference_speed_mm_per_second = 0.0F;
        next_output.reference_acceleration_mm_per_second2 = 0.0F;
        next_output.state = MODE_CAR_PROFILE_STOPPED;
        next.running = false;
    }
    next_output.valid = true;
    next.output = next_output;
    *profile = next;
    *output = next_output;
    return true;
}
