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
        finite_value(config->jerk_up_mm_per_second3) &&
        finite_value(config->jerk_down_mm_per_second3) &&
        finite_value(
            config->measured_acceleration_filter_time_constant_seconds) &&
        finite_value(config->acceleration_feedback_gain) &&
        finite_value(
            config->maximum_measured_acceleration_mm_per_second2) &&
        finite_value(config->guard_distance_mm) &&
        finite_value(config->preview_time_seconds) &&
        finite_value(config->stopped_speed_threshold_mm_per_second) &&
        (config->maximum_speed_mm_per_second > 0.0F) &&
        (config->acceleration_up_mm_per_second2 > 0.0F) &&
        (config->acceleration_down_mm_per_second2 > 0.0F) &&
        (config->jerk_up_mm_per_second3 > 0.0F) &&
        (config->jerk_down_mm_per_second3 > 0.0F) &&
        (config->measured_acceleration_filter_time_constant_seconds >= 0.0F) &&
        (config->acceleration_feedback_gain >= 0.0F) &&
        (config->maximum_measured_acceleration_mm_per_second2 > 0.0F) &&
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

static float zero_acceleration_stopping_distance(
    const Mode_Car_LinearSpeedProfile *profile, float speed)
{
    const float speed_abs = fabsf(speed);
    const float deceleration =
        profile->config.acceleration_down_mm_per_second2;
    const float jerk = profile->config.jerk_down_mm_per_second3;
    const float transition_speed = deceleration * deceleration / jerk;

    if (speed_abs <= transition_speed) {
        return speed_abs * sqrtf(speed_abs / jerk);
    }
    return (speed_abs * speed_abs) / (2.0F * deceleration) +
        speed_abs * deceleration / (2.0F * jerk);
}

static float stopping_distance(
    const Mode_Car_LinearSpeedProfile *profile, float speed)
{
    const float speed_abs = fabsf(speed);
    const float positive_acceleration = fmaxf(
        profile->output.reference_acceleration_mm_per_second2, 0.0F);
    const float jerk = profile->config.jerk_down_mm_per_second3;
    const float transition_time = positive_acceleration / jerk;
    const float transition_speed = speed_abs +
        positive_acceleration * positive_acceleration / (2.0F * jerk);
    const float transition_distance =
        speed_abs * transition_time +
        positive_acceleration * positive_acceleration * positive_acceleration /
            (3.0F * jerk * jerk);

    return transition_distance +
        zero_acceleration_stopping_distance(profile, transition_speed);
}

static float braking_speed_for_distance(
    const Mode_Car_LinearSpeedProfile *profile, float distance)
{
    const float deceleration =
        profile->config.acceleration_down_mm_per_second2;
    const float jerk = profile->config.jerk_down_mm_per_second3;
    const float clamped_distance = clamp_nonnegative(distance);
    const float transition_distance =
        deceleration * deceleration * deceleration / (jerk * jerk);
    float offset;

    if (clamped_distance <= transition_distance) {
        return cbrtf(clamped_distance * clamped_distance * jerk);
    }
    offset = deceleration * deceleration / jerk;
    return 0.5F * (-offset + sqrtf(
        offset * offset + 8.0F * deceleration * clamped_distance));
}

static bool update_measured_acceleration(
    Mode_Car_LinearSpeedProfile *profile,
    const Mode_Car_LinearProfileInput *input,
    float *filtered_acceleration)
{
    float raw_acceleration;
    float alpha;

    if ((profile == NULL) || (input == NULL) ||
        (filtered_acceleration == NULL)) {
        return false;
    }
    if (!profile->measured_speed_initialized) {
        profile->previous_measured_speed_mm_per_second =
            input->measured_speed_mm_per_second;
        profile->filtered_measured_acceleration_mm_per_second2 = 0.0F;
        profile->measured_speed_initialized = true;
        *filtered_acceleration = 0.0F;
        return true;
    }

    raw_acceleration =
        (input->measured_speed_mm_per_second -
         profile->previous_measured_speed_mm_per_second) /
        input->dt_seconds;
    raw_acceleration = fmaxf(
        -profile->config.maximum_measured_acceleration_mm_per_second2,
        fminf(
            profile->config.maximum_measured_acceleration_mm_per_second2,
            raw_acceleration));
    alpha =
        (profile->config.
            measured_acceleration_filter_time_constant_seconds == 0.0F) ?
        1.0F : 1.0F - expf(
            -input->dt_seconds /
            profile->config.
                measured_acceleration_filter_time_constant_seconds);
    if (!finite_value(raw_acceleration) || !finite_value(alpha) ||
        (alpha < 0.0F) || (alpha > 1.0F)) {
        return false;
    }
    profile->filtered_measured_acceleration_mm_per_second2 +=
        alpha * (raw_acceleration -
            profile->filtered_measured_acceleration_mm_per_second2);
    profile->previous_measured_speed_mm_per_second =
        input->measured_speed_mm_per_second;
    if (!finite_value(
            profile->filtered_measured_acceleration_mm_per_second2)) {
        return false;
    }
    *filtered_acceleration =
        profile->filtered_measured_acceleration_mm_per_second2;
    return true;
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
        .jerk_up_mm_per_second3 =
            TEMPLATE_CAR_PROFILE_JERK_UP_MM_PER_SECOND3,
        .jerk_down_mm_per_second3 =
            TEMPLATE_CAR_PROFILE_JERK_DOWN_MM_PER_SECOND3,
        .measured_acceleration_filter_time_constant_seconds =
            TEMPLATE_CAR_PROFILE_MEASURED_ACCEL_FILTER_TIME_CONSTANT_SECONDS,
        .acceleration_feedback_gain =
            TEMPLATE_CAR_PROFILE_ACCELERATION_FEEDBACK_GAIN,
        .maximum_measured_acceleration_mm_per_second2 =
            TEMPLATE_CAR_PROFILE_MAXIMUM_MEASURED_ACCEL_MM_PER_SECOND2,
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
    profile->measured_speed_initialized = false;
    profile->previous_measured_speed_mm_per_second = 0.0F;
    profile->filtered_measured_acceleration_mm_per_second2 = 0.0F;
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
    profile->measured_speed_initialized = false;
    profile->previous_measured_speed_mm_per_second = 0.0F;
    profile->filtered_measured_acceleration_mm_per_second2 = 0.0F;
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
    float current_acceleration;
    float desired_acceleration;
    float feedback_acceleration;
    float open_loop_acceleration;
    float next_acceleration;
    float open_loop_speed;
    float measured_acceleration;
    float velocity_to_cap;
    float acceleration_stop_velocity;
    float step_acceleration;
    float maximum_acceleration_change;
    float envelope_speed;
    float terminal_speed_tolerance;
    bool braking = false;
    bool zero_speed_acceleration_tail = false;

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
        next_output.measured_acceleration_mm_per_second2 = 0.0F;
        next_output.acceleration_error_mm_per_second2 = 0.0F;
        next_output.acceleration_feedback_correction_mm_per_second2 = 0.0F;
        next_output.running = false;
        next_output.stopped =
            next.output.state == MODE_CAR_PROFILE_STOPPED;
        next_output.valid = true;
        *profile = next;
        *output = next_output;
        return true;
    }

    if (!update_measured_acceleration(
            &next, input, &measured_acceleration)) {
        return false;
    }

    if (next.goal_valid) {
        remaining = next.goal_distance_mm - input->odometry_distance_mm;
        envelope_speed = fmaxf(
            fabsf(input->measured_speed_mm_per_second),
            next.output.reference_speed_mm_per_second);
        braking_distance = stopping_distance(&next, envelope_speed);
        braking_speed_cap = braking_speed_for_distance(
            &next, remaining - next.config.guard_distance_mm);
        if (!finite_value(braking_speed_cap)) {
            return false;
        }
        if ((braking_speed_cap < next.cruise_speed_mm_per_second) ||
            (remaining <= braking_distance + next.config.guard_distance_mm)) {
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
    current_acceleration =
        next.output.reference_acceleration_mm_per_second2;
    velocity_to_cap = speed_cap - speed;
    if ((fabsf(velocity_to_cap) <= 0.001F) &&
        (fabsf(current_acceleration) <= 0.001F)) {
        /* Once a ramp has settled, ignore floating-point residue around the
         * speed cap. Otherwise a sub-micrometre-per-second residue can reopen
         * the acceleration-feedback path and turn encoder-difference noise
         * into a nonzero cruise acceleration command. */
        velocity_to_cap = 0.0F;
    }
    if (velocity_to_cap > 0.0F) {
        step_acceleration = velocity_to_cap / input->dt_seconds;
        acceleration_stop_velocity =
            (fmaxf(current_acceleration, 0.0F) *
             fmaxf(current_acceleration, 0.0F)) /
            (2.0F * next.config.jerk_down_mm_per_second3);
        desired_acceleration =
            (velocity_to_cap <= acceleration_stop_velocity) ? 0.0F :
            fminf(next.config.acceleration_up_mm_per_second2,
                step_acceleration);
    }
    else if (velocity_to_cap < 0.0F) {
        step_acceleration = velocity_to_cap / input->dt_seconds;
        acceleration_stop_velocity =
            (fminf(current_acceleration, 0.0F) *
             fminf(current_acceleration, 0.0F)) /
            (2.0F * next.config.jerk_up_mm_per_second3) +
            fabsf(fminf(current_acceleration, 0.0F)) *
                input->dt_seconds;
        desired_acceleration =
            (-velocity_to_cap <= acceleration_stop_velocity) ? 0.0F :
            fmaxf(-next.config.acceleration_down_mm_per_second2,
                step_acceleration);
    }
    else {
        desired_acceleration = 0.0F;
    }
    if (fabsf(desired_acceleration) <= 0.001F) {
        /* Ramp exits and settled cruise are deterministic. Encoder-difference
         * noise may trim an intentional nonzero acceleration, but it must not
         * hold the profile away from zero or restart acceleration at cruise. */
        feedback_acceleration = 0.0F;
    }
    else {
        feedback_acceleration = desired_acceleration +
            next.config.acceleration_feedback_gain *
                (current_acceleration - measured_acceleration);
    }
    feedback_acceleration = fmaxf(
        -next.config.acceleration_down_mm_per_second2,
        fminf(next.config.acceleration_up_mm_per_second2,
            feedback_acceleration));
    if (desired_acceleration >= current_acceleration) {
        open_loop_acceleration = fminf(
            current_acceleration +
                next.config.jerk_up_mm_per_second3 * input->dt_seconds,
            desired_acceleration);
    }
    else {
        open_loop_acceleration = fmaxf(
            current_acceleration -
                next.config.jerk_down_mm_per_second3 * input->dt_seconds,
            desired_acceleration);
    }
    if (feedback_acceleration >= current_acceleration) {
        maximum_acceleration_change =
            next.config.jerk_up_mm_per_second3 * input->dt_seconds;
        next_acceleration = fminf(
            current_acceleration + maximum_acceleration_change,
            feedback_acceleration);
    }
    else {
        maximum_acceleration_change =
            next.config.jerk_down_mm_per_second3 * input->dt_seconds;
        next_acceleration = fmaxf(
            current_acceleration - maximum_acceleration_change,
            feedback_acceleration);
    }
    speed += next_acceleration * input->dt_seconds;
    if (speed < 0.0F) {
        speed = 0.0F;
        zero_speed_acceleration_tail = true;
    }
    speed = fminf(next.config.maximum_speed_mm_per_second, speed);
    open_loop_speed = previous_speed +
        open_loop_acceleration * input->dt_seconds;
    open_loop_speed = fmaxf(0.0F,
        fminf(next.config.maximum_speed_mm_per_second, open_loop_speed));
    if (!finite_value(speed) || !finite_value(open_loop_speed) ||
        !finite_value(remaining)) {
        return false;
    }
    /* Once the speed target reaches its physical zero boundary, retain the
     * jerk-limited acceleration state until it returns to zero. This avoids
     * converting the nonnegative-speed clamp into a final acceleration step,
     * and supplies F407 with a continuous terminal feedforward tail. */
    next_output.reference_acceleration_mm_per_second2 =
        zero_speed_acceleration_tail ? next_acceleration :
            (speed - previous_speed) / input->dt_seconds;
    if (!finite_value(next_output.reference_acceleration_mm_per_second2)) {
        return false;
    }
    next_output.reference_speed_mm_per_second = speed;
    next_output.measured_acceleration_mm_per_second2 =
        measured_acceleration;
    next_output.acceleration_error_mm_per_second2 =
        next_output.reference_acceleration_mm_per_second2 -
            measured_acceleration;
    next_output.acceleration_feedback_correction_mm_per_second2 =
        next_output.reference_acceleration_mm_per_second2 -
            (open_loop_speed - previous_speed) / input->dt_seconds;
    if (!finite_value(
            next_output.acceleration_feedback_correction_mm_per_second2)) {
        return false;
    }
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
    braking = braking ||
        (next_output.reference_acceleration_mm_per_second2 < 0.0F);
    next_output.braking = braking;
    next_output.state = braking ? MODE_CAR_PROFILE_BRAKING :
        (((speed < next.cruise_speed_mm_per_second) ||
          (next_output.reference_acceleration_mm_per_second2 > 0.0F)) ?
            MODE_CAR_PROFILE_ACCELERATING : MODE_CAR_PROFILE_CRUISING);
    terminal_speed_tolerance = fminf(
        next.config.stopped_speed_threshold_mm_per_second,
        next.config.jerk_up_mm_per_second3 * input->dt_seconds *
            input->dt_seconds);
    next_output.stopped = next.goal_valid &&
        (speed <= terminal_speed_tolerance + 0.001F) &&
        (fabsf(next_output.reference_acceleration_mm_per_second2) <=
            0.001F) &&
        (fabsf(input->measured_speed_mm_per_second) <=
            next.config.stopped_speed_threshold_mm_per_second) &&
        (remaining <= next.config.guard_distance_mm +
            next.config.stopped_speed_threshold_mm_per_second *
                input->dt_seconds);
    if (next_output.stopped) {
        next_output.reference_speed_mm_per_second = 0.0F;
        next_output.reference_acceleration_mm_per_second2 = 0.0F;
        next_output.acceleration_error_mm_per_second2 =
            -measured_acceleration;
        next_output.acceleration_feedback_correction_mm_per_second2 = 0.0F;
        next_output.state = MODE_CAR_PROFILE_STOPPED;
        next.running = false;
    }
    next_output.valid = true;
    next.output = next_output;
    *profile = next;
    *output = next_output;
    return true;
}
