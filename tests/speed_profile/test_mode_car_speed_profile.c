#include "mode_car_control.h"
#include "core_config.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static Mode_Car_LinearSpeedProfile make_profile(void)
{
    Mode_Car_LinearSpeedProfile profile;
    Mode_Car_LinearProfileConfig config = {
        .maximum_speed_mm_per_second = 300.0F,
        .acceleration_up_mm_per_second2 = 100.0F,
        .acceleration_down_mm_per_second2 = 100.0F,
        .jerk_up_mm_per_second3 = 10000.0F,
        .jerk_down_mm_per_second3 = 10000.0F,
        .measured_acceleration_filter_time_constant_seconds = 0.0F,
        .acceleration_feedback_gain = 0.0F,
        .maximum_measured_acceleration_mm_per_second2 = 1000.0F,
        .guard_distance_mm = 5.0F,
        .preview_time_seconds = 0.05F,
        .stopped_speed_threshold_mm_per_second = 2.0F,
    };

    assert(Mode_Car_LinearSpeedProfile_Init(&profile, &config));
    return profile;
}

static Mode_Car_LinearSpeedProfile make_ball_profile(
    float acceleration_mm_per_second2,
    float jerk_mm_per_second3);

static void test_linear_start(void)
{
    Mode_Car_LinearSpeedProfile profile = make_profile();
    Mode_Car_LinearProfileOutput output = {0};
    float distance = 0.0F;

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 100.0F, 200.0F));
    for (unsigned int index = 0U; index < 10U; ++index) {
        assert(Mode_Car_LinearSpeedProfile_Update(&profile,
            &(Mode_Car_LinearProfileInput){
                .dt_seconds = 0.1F,
                .odometry_distance_mm = 100.0F + distance,
                .measured_speed_mm_per_second = output.reference_speed_mm_per_second,
            }, &output));
        assert(isfinite(output.reference_speed_mm_per_second));
        assert(fabsf(output.reference_speed_mm_per_second -
            (10.0F * (float)(index + 1U))) < 0.01F);
        distance += output.reference_speed_mm_per_second * 0.1F;
    }
    assert(fabsf(output.reference_speed_mm_per_second - 100.0F) < 0.01F);
}

static void test_goal_braking_and_stop(void)
{
    Mode_Car_LinearSpeedProfile profile = make_profile();
    Mode_Car_LinearProfileOutput output = {0};
    float distance = 0.0F;
    float speed = 0.0F;

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F, 200.0F));
    assert(Mode_Car_LinearSpeedProfile_SetGoal(&profile, 500.0F));
    for (unsigned int index = 0U; index < 2000U; ++index) {
        assert(Mode_Car_LinearSpeedProfile_Update(&profile,
            &(Mode_Car_LinearProfileInput){
                .dt_seconds = 0.01F,
                .odometry_distance_mm = distance,
                .measured_speed_mm_per_second = speed,
            }, &output));
        speed = output.reference_speed_mm_per_second;
        assert(speed <= 200.01F);
        distance += speed * 0.01F;
        if (output.stopped) {
            break;
        }
    }
    assert(output.stopped);
    assert(distance <= 505.0F);
    assert(output.state == MODE_CAR_PROFILE_STOPPED);
}

static void test_cruise_speed_change_is_slewed(void)
{
    Mode_Car_LinearSpeedProfile profile = make_profile();
    Mode_Car_LinearProfileOutput output = {0};

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F, 200.0F));
    assert(Mode_Car_LinearSpeedProfile_SetCruiseSpeed(&profile, 50.0F));
    assert(Mode_Car_LinearSpeedProfile_Update(&profile,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 0.0F,
            .measured_speed_mm_per_second = 0.0F,
        }, &output));
    assert(fabsf(output.reference_speed_mm_per_second - 10.0F) < 0.01F);
}

static void test_partial_final_step_reports_actual_acceleration(void)
{
    Mode_Car_LinearSpeedProfile profile = make_profile();
    Mode_Car_LinearProfileOutput output = {0};

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F, 15.0F));
    assert(Mode_Car_LinearSpeedProfile_Update(&profile,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 0.0F,
            .measured_speed_mm_per_second = 0.0F,
        }, &output));
    assert(fabsf(output.reference_speed_mm_per_second - 10.0F) < 0.01F);
    assert(fabsf(output.reference_acceleration_mm_per_second2 - 100.0F) <
        0.01F);

    assert(Mode_Car_LinearSpeedProfile_Update(&profile,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 1.0F,
            .measured_speed_mm_per_second = 10.0F,
        }, &output));
    assert(fabsf(output.reference_speed_mm_per_second - 15.0F) < 0.01F);
    assert(fabsf(output.reference_acceleration_mm_per_second2 - 50.0F) <
        0.01F);
}

static void test_stop_request_is_forward_planned(void)
{
    Mode_Car_LinearSpeedProfile profile = make_profile();
    Mode_Car_LinearProfileOutput output = {0};

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F, 200.0F));
    output.reference_speed_mm_per_second = 100.0F;
    profile.output = output;
    assert(Mode_Car_LinearSpeedProfile_RequestStop(&profile, 100.0F, 90.0F));
    assert(profile.goal_valid);
    assert(profile.stop_requested);
    assert(profile.goal_distance_mm > 100.0F);
    assert(Mode_Car_LinearSpeedProfile_Update(&profile,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 100.0F,
            .measured_speed_mm_per_second = 300.0F,
        }, &output));
    assert(output.reference_speed_mm_per_second <= 100.0F);
}

static void test_jerk_limits_reference_acceleration(void)
{
    Mode_Car_LinearSpeedProfile profile = make_profile();
    Mode_Car_LinearProfileOutput output = {0};
    float previous_acceleration = 0.0F;
    float measured_speed = 0.0F;

    profile.config.jerk_up_mm_per_second3 = 200.0F;
    profile.config.jerk_down_mm_per_second3 = 200.0F;
    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F, 100.0F));
    for (unsigned int index = 0U; index < 8U; ++index) {
        assert(Mode_Car_LinearSpeedProfile_Update(&profile,
            &(Mode_Car_LinearProfileInput){
                .dt_seconds = 0.1F,
                .odometry_distance_mm = 0.0F,
                .measured_speed_mm_per_second = measured_speed,
            }, &output));
        assert(fabsf(output.reference_acceleration_mm_per_second2 -
            previous_acceleration) <= 20.01F);
        assert(fabsf(output.reference_acceleration_mm_per_second2) <=
            100.01F);
        previous_acceleration =
            output.reference_acceleration_mm_per_second2;
        measured_speed = output.reference_speed_mm_per_second;
    }
}

static void test_measured_acceleration_feedback_reduces_overshoot(void)
{
    Mode_Car_LinearSpeedProfile profile = make_profile();
    Mode_Car_LinearProfileOutput output = {0};

    profile.config.acceleration_feedback_gain = 0.5F;
    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F, 200.0F));
    assert(Mode_Car_LinearSpeedProfile_Update(&profile,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 0.0F,
            .measured_speed_mm_per_second = 0.0F,
        }, &output));
    assert(fabsf(output.reference_acceleration_mm_per_second2 - 100.0F) <
        0.01F);
    assert(Mode_Car_LinearSpeedProfile_Update(&profile,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 1.0F,
            .measured_speed_mm_per_second = 20.0F,
        }, &output));
    assert(fabsf(output.measured_acceleration_mm_per_second2 - 200.0F) <
        0.01F);
    assert(output.reference_acceleration_mm_per_second2 < 100.0F);
    assert(output.acceleration_error_mm_per_second2 < 0.0F);
    assert(output.acceleration_feedback_correction_mm_per_second2 < 0.0F);
    assert(fabsf(output.acceleration_feedback_correction_mm_per_second2 +
        50.0F) < 0.01F);
}

static void test_settled_cruise_ignores_acceleration_measurement_noise(void)
{
    Mode_Car_LinearSpeedProfile profile = make_ball_profile(
        TEMPLATE_BRINGUP_SHARED_ACCELERATION_MM_PER_SECOND2,
        TEMPLATE_BRINGUP_SHARED_JERK_MM_PER_SECOND3);
    Mode_Car_LinearProfileOutput output = {0};
    float measured_speed = 0.0F;

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F, 100.0F));
    for (unsigned int index = 0U; index < 500U; ++index) {
        assert(Mode_Car_LinearSpeedProfile_Update(&profile,
            &(Mode_Car_LinearProfileInput){
                .dt_seconds = 0.01F,
                .odometry_distance_mm = 0.0F,
                .measured_speed_mm_per_second = measured_speed,
            }, &output));
        measured_speed = output.reference_speed_mm_per_second;
        if ((fabsf(output.reference_speed_mm_per_second - 100.0F) < 0.001F) &&
            (fabsf(output.reference_acceleration_mm_per_second2) < 0.001F)) {
            break;
        }
    }
    assert(fabsf(output.reference_speed_mm_per_second - 100.0F) < 0.001F);
    assert(fabsf(output.reference_acceleration_mm_per_second2) < 0.001F);

    measured_speed += 30.0F;
    assert(Mode_Car_LinearSpeedProfile_Update(&profile,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.01F,
            .odometry_distance_mm = 0.0F,
            .measured_speed_mm_per_second = measured_speed,
        }, &output));
    assert(fabsf(output.reference_speed_mm_per_second - 100.0F) < 0.001F);
    assert(fabsf(output.reference_acceleration_mm_per_second2) < 0.001F);
    assert(fabsf(output.acceleration_feedback_correction_mm_per_second2) <
        0.01F);
}

static void test_high_measured_speed_never_delays_braking(void)
{
    Mode_Car_LinearSpeedProfile nominal = make_profile();
    Mode_Car_LinearSpeedProfile fast;
    Mode_Car_LinearProfileOutput nominal_output = {0};
    Mode_Car_LinearProfileOutput fast_output = {0};

    assert(Mode_Car_LinearSpeedProfile_Start(&nominal, 0.0F, 200.0F));
    nominal.output.reference_speed_mm_per_second = 100.0F;
    nominal.output.reference_acceleration_mm_per_second2 = 0.0F;
    assert(Mode_Car_LinearSpeedProfile_SetGoal(&nominal, 300.0F));
    fast = nominal;
    assert(Mode_Car_LinearSpeedProfile_Update(&nominal,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 0.0F,
            .measured_speed_mm_per_second = 100.0F,
        }, &nominal_output));
    assert(Mode_Car_LinearSpeedProfile_Update(&fast,
        &(Mode_Car_LinearProfileInput){
            .dt_seconds = 0.1F,
            .odometry_distance_mm = 0.0F,
            .measured_speed_mm_per_second = 300.0F,
        }, &fast_output));

    assert(!nominal_output.braking);
    assert(fast_output.braking);
    assert(fast_output.reference_speed_mm_per_second <=
        nominal_output.reference_speed_mm_per_second + 0.01F);
}

static Mode_Car_LinearSpeedProfile make_ball_profile(
    float acceleration_mm_per_second2,
    float jerk_mm_per_second3)
{
    Mode_Car_LinearSpeedProfile profile;
    Mode_Car_LinearProfileConfig config =
        Mode_Car_LinearProfile_DefaultConfig();

    config.maximum_speed_mm_per_second =
        TEMPLATE_CAR_SPEED_MAXIMUM_TARGET_MM_PER_SECOND;
    config.acceleration_up_mm_per_second2 =
        acceleration_mm_per_second2;
    config.acceleration_down_mm_per_second2 =
        acceleration_mm_per_second2;
    config.jerk_up_mm_per_second3 = jerk_mm_per_second3;
    config.jerk_down_mm_per_second3 = jerk_mm_per_second3;
    config.acceleration_feedback_gain =
        TEMPLATE_BRINGUP_SHARED_ACCELERATION_FEEDBACK_GAIN;
    assert(Mode_Car_LinearSpeedProfile_Init(&profile, &config));
    return profile;
}

static void assert_acceleration_slew(
    float previous_acceleration,
    float acceleration,
    float jerk_mm_per_second3,
    float dt_seconds,
    float speed_mm_per_second)
{
    if (fabsf(acceleration - previous_acceleration) >
        jerk_mm_per_second3 * dt_seconds + 0.05F) {
        fprintf(stderr,
            "acceleration slew violation: previous=%f current=%f limit=%f speed=%f\n",
            (double)previous_acceleration,
            (double)acceleration,
            (double)(jerk_mm_per_second3 * dt_seconds),
            (double)speed_mm_per_second);
    }
    assert(fabsf(acceleration - previous_acceleration) <=
        jerk_mm_per_second3 * dt_seconds + 0.05F);
}

static void test_item4_keeps_pass_and_stop_positions(void)
{
    const float dt_seconds = 0.01F;
    const float point_b_mm = TEMPLATE_BRINGUP_H_STRAIGHT_LENGTH_MM;
    const float goal_mm = point_b_mm +
        TEMPLATE_BRINGUP_ITEM4_PASS_B_RUNOUT_MM;
    Mode_Car_LinearSpeedProfile profile = make_ball_profile(
        TEMPLATE_BRINGUP_SHARED_ACCELERATION_MM_PER_SECOND2,
        TEMPLATE_BRINGUP_SHARED_JERK_MM_PER_SECOND3);
    Mode_Car_LinearProfileOutput output = {0};
    float previous_acceleration = 0.0F;
    float measured_speed = 0.0F;
    float distance = 0.0F;
    float elapsed_seconds = 0.0F;
    float pass_b_seconds = -1.0F;

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F,
        TEMPLATE_BRINGUP_ITEM4_AB_CRUISE_SPEED_MM_PER_SECOND));
    assert(Mode_Car_LinearSpeedProfile_SetGoal(&profile, goal_mm));
    for (unsigned int index = 0U; index < 2000U; ++index) {
        assert(Mode_Car_LinearSpeedProfile_Update(&profile,
            &(Mode_Car_LinearProfileInput){
                .dt_seconds = dt_seconds,
                .odometry_distance_mm = distance,
                .measured_speed_mm_per_second = measured_speed,
            }, &output));
        assert_acceleration_slew(previous_acceleration,
            output.reference_acceleration_mm_per_second2,
            TEMPLATE_BRINGUP_SHARED_JERK_MM_PER_SECOND3,
            dt_seconds, output.reference_speed_mm_per_second);
        previous_acceleration =
            output.reference_acceleration_mm_per_second2;
        measured_speed = output.reference_speed_mm_per_second;
        distance += measured_speed * dt_seconds;
        elapsed_seconds += dt_seconds;
        if ((pass_b_seconds < 0.0F) && (distance >= point_b_mm)) {
            pass_b_seconds = elapsed_seconds;
        }
        if (output.stopped) {
            break;
        }
    }

    assert(output.stopped);
    assert(fabsf(output.reference_speed_mm_per_second) < 0.01F);
    assert(fabsf(output.reference_acceleration_mm_per_second2) < 0.01F);
    assert(pass_b_seconds > 0.0F);
    assert(pass_b_seconds < 8.0F);
    assert(fabsf(profile.goal_distance_mm - goal_mm) < 0.01F);
    assert(distance <= goal_mm + 0.1F);
    assert((goal_mm - distance) <=
        profile.config.guard_distance_mm +
            profile.config.stopped_speed_threshold_mm_per_second *
                dt_seconds + 0.1F);
}

static void test_item56_cruises_smoothly_and_keeps_marker_runout(void)
{
    const float dt_seconds = 0.01F;
    const float finish_window_lower_mm =
        TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_CENTER_MM -
            TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_HALF_WIDTH_MM;
    const float approach_transition_mm =
        ((TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND *
          TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND) -
         (TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND *
          TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND)) /
        (2.0F * TEMPLATE_BRINGUP_SHARED_DECELERATION_MM_PER_SECOND2) +
        0.5F *
            (TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND +
                TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND) *
            TEMPLATE_BRINGUP_SHARED_DECELERATION_MM_PER_SECOND2 /
            TEMPLATE_BRINGUP_SHARED_JERK_MM_PER_SECOND3;
    const float approach_start_mm =
        finish_window_lower_mm - approach_transition_mm;
    Mode_Car_LinearSpeedProfile profile = make_ball_profile(
        TEMPLATE_BRINGUP_SHARED_ACCELERATION_MM_PER_SECOND2,
        TEMPLATE_BRINGUP_SHARED_JERK_MM_PER_SECOND3);
    Mode_Car_LinearProfileOutput output = {0};
    float previous_acceleration = 0.0F;
    float measured_speed = 0.0F;
    float distance = 0.0F;
    float marker_distance = 0.0F;
    float marker_goal = 0.0F;
    unsigned int zero_acceleration_samples = 0U;
    bool approach_selected = false;
    bool approach_reached_by_window = false;
    bool marker_latched = false;

    assert(Mode_Car_LinearSpeedProfile_Start(&profile, 0.0F,
        TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND));
    assert(Mode_Car_LinearSpeedProfile_SetGoal(&profile,
        TEMPLATE_BRINGUP_H_ONE_LAP_DISTANCE_MM +
            TEMPLATE_BRINGUP_CAR_LENGTH_MM));
    for (unsigned int index = 0U; index < 4000U; ++index) {
        if (!approach_selected && (distance >= approach_start_mm)) {
            assert(Mode_Car_LinearSpeedProfile_SetCruiseSpeed(&profile,
                TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND));
            approach_selected = true;
        }
        if (!marker_latched &&
            (distance >= TEMPLATE_BRINGUP_FINISH_MARKER_WINDOW_CENTER_MM)) {
            marker_distance = distance;
            marker_goal = marker_distance +
                TEMPLATE_BRINGUP_ITEM56_AFTER_MARKER_GOAL_RUNOUT_MM;
            assert(Mode_Car_LinearSpeedProfile_SetGoal(
                &profile, marker_goal));
            marker_latched = true;
        }
        assert(Mode_Car_LinearSpeedProfile_Update(&profile,
            &(Mode_Car_LinearProfileInput){
                .dt_seconds = dt_seconds,
                .odometry_distance_mm = distance,
                .measured_speed_mm_per_second = measured_speed,
            }, &output));
        assert_acceleration_slew(previous_acceleration,
            output.reference_acceleration_mm_per_second2,
            TEMPLATE_BRINGUP_SHARED_JERK_MM_PER_SECOND3,
            dt_seconds, output.reference_speed_mm_per_second);
        if ((distance >= 1500.0F) && (distance <= 4500.0F) &&
            (fabsf(output.reference_speed_mm_per_second -
                TEMPLATE_BRINGUP_ITEM56_LAP_CRUISE_SPEED_MM_PER_SECOND) <
                0.05F) &&
            (fabsf(output.reference_acceleration_mm_per_second2) < 0.05F)) {
            ++zero_acceleration_samples;
        }
        if ((distance >= finish_window_lower_mm) &&
            (output.reference_speed_mm_per_second <=
                TEMPLATE_BRINGUP_ITEM56_FINISH_APPROACH_SPEED_MM_PER_SECOND +
                    0.1F)) {
            approach_reached_by_window = true;
        }
        previous_acceleration =
            output.reference_acceleration_mm_per_second2;
        measured_speed = output.reference_speed_mm_per_second;
        distance += measured_speed * dt_seconds;
        if (output.stopped) {
            break;
        }
    }

    assert(output.stopped);
    assert(fabsf(output.reference_speed_mm_per_second) < 0.01F);
    assert(fabsf(output.reference_acceleration_mm_per_second2) < 0.01F);
    assert(approach_selected);
    assert(approach_reached_by_window);
    assert(marker_latched);
    assert(zero_acceleration_samples >= 100U);
    assert(fabsf(profile.goal_distance_mm - marker_goal) < 0.01F);
    assert(fabsf((marker_goal - marker_distance) -
        TEMPLATE_BRINGUP_ITEM56_AFTER_MARKER_GOAL_RUNOUT_MM) < 0.01F);
    assert(distance <= marker_goal + 0.1F);
    assert((marker_goal - distance) <=
        profile.config.guard_distance_mm +
            profile.config.stopped_speed_threshold_mm_per_second *
                dt_seconds + 0.1F);
}

int main(void)
{
    test_linear_start();
    test_goal_braking_and_stop();
    test_cruise_speed_change_is_slewed();
    test_partial_final_step_reports_actual_acceleration();
    test_stop_request_is_forward_planned();
    test_jerk_limits_reference_acceleration();
    test_measured_acceleration_feedback_reduces_overshoot();
    test_settled_cruise_ignores_acceleration_measurement_noise();
    test_high_measured_speed_never_delays_braking();
    test_item4_keeps_pass_and_stop_positions();
    test_item56_cruises_smoothly_and_keeps_marker_runout();
    puts("linear speed profile tests: PASS");
    return 0;
}
