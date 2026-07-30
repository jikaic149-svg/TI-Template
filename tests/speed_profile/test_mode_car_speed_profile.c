#include "mode_car_control.h"

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
        .guard_distance_mm = 5.0F,
        .preview_time_seconds = 0.05F,
        .stopped_speed_threshold_mm_per_second = 2.0F,
    };

    assert(Mode_Car_LinearSpeedProfile_Init(&profile, &config));
    return profile;
}

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

int main(void)
{
    test_linear_start();
    test_goal_braking_and_stop();
    test_cruise_speed_change_is_slewed();
    test_partial_final_step_reports_actual_acceleration();
    test_stop_request_is_forward_planned();
    puts("linear speed profile tests: PASS");
    return 0;
}
