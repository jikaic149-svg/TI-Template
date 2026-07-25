#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compile the production G3519 Motor BSP against deterministic host mocks. */
#define BSP_MSPM0G3519_RESOURCE_MAP_H

#include "bsp_motor.h"

struct BSP_PWM_Resource {
    size_t index;
    BSP_PWM_Duty duty;
};

static struct BSP_PWM_Resource g_mock_pwm[4] = {
    {0U, 0U},
    {1U, 0U},
    {2U, 0U},
    {3U, 0U},
};

size_t BSP_PWM_Count(void)
{
    return 4U;
}

BSP_PWM_Handle BSP_PWM_GetHandle(size_t index)
{
    return (index < 4U) ? &g_mock_pwm[index] : NULL;
}

bool BSP_PWM_SetDuty(BSP_PWM_Handle pwm, BSP_PWM_Duty duty)
{
    if ((pwm == NULL) || (duty > BSP_PWM_DUTY_MAX)) {
        return false;
    }
    ((struct BSP_PWM_Resource *)pwm)->duty = duty;
    return true;
}

bool BSP_PWM_GetDuty(BSP_PWM_Handle pwm, BSP_PWM_Duty *duty)
{
    if ((pwm == NULL) || (duty == NULL)) {
        return false;
    }
    *duty = pwm->duty;
    return true;
}

bool BSP_PWM_GetFrequencyHz(
    BSP_PWM_Handle pwm, uint32_t *frequency_hz)
{
    if ((pwm == NULL) || (frequency_hz == NULL)) {
        return false;
    }
    *frequency_hz = UINT32_C(20000);
    return true;
}

#include "../../BSP/MSPM0G3519/bsp_motor_at8236.c"

static void expect_outputs(size_t motor_index,
    BSP_PWM_Duty in1_duty, BSP_PWM_Duty in2_duty)
{
    const size_t in1_index = motor_index;
    const size_t in2_index = motor_index + 2U;

    assert(g_mock_pwm[in1_index].duty == in1_duty);
    assert(g_mock_pwm[in2_index].duty == in2_duty);
}

int main(void)
{
    BSP_Motor_Handle motor_a;
    BSP_Motor_Handle motor_b;
    BSP_Motor_State state;
    bool enabled;

    BSP_Motor_Init();
    assert(BSP_Motor_Count() == 2U);
    motor_a = BSP_Motor_GetHandle(0U);
    motor_b = BSP_Motor_GetHandle(1U);
    assert((motor_a != NULL) && (motor_b != NULL));
    expect_outputs(0U, 0U, 0U);
    expect_outputs(1U, 0U, 0U);

    /* Motion is rejected while disabled. A remembered brake zero remains
     * physically coasted until the handle is enabled. */
    state = (BSP_Motor_State){BSP_MOTOR_MODE_FORWARD, 3000U};
    assert(!BSP_Motor_Apply(motor_a, state));
    state = (BSP_Motor_State){BSP_MOTOR_MODE_BRAKE, 0U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_outputs(0U, 0U, 0U);
    assert(BSP_Motor_SetEnabled(motor_a, true));
    expect_outputs(0U, BSP_PWM_DUTY_MAX, BSP_PWM_DUTY_MAX);

    /* Forward and reverse use symmetric PWM/0 and 0/PWM fast decay. */
    state = (BSP_Motor_State){BSP_MOTOR_MODE_FORWARD, 3000U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_outputs(0U, 3000U, 0U);
    state = (BSP_Motor_State){BSP_MOTOR_MODE_REVERSE, 3000U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_outputs(0U, 0U, 3000U);
    state = (BSP_Motor_State){BSP_MOTOR_MODE_REVERSE,
        BSP_PWM_DUTY_MAX};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_outputs(0U, 0U, BSP_PWM_DUTY_MAX);
    state = (BSP_Motor_State){BSP_MOTOR_MODE_COAST, 0U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_outputs(0U, 0U, 0U);

    assert(BSP_Motor_SetEnabled(motor_a, false));
    assert(BSP_Motor_IsEnabled(motor_a, &enabled) && !enabled);
    assert(BSP_Motor_GetState(motor_a, &state));
    assert((state.mode == BSP_MOTOR_MODE_COAST) && (state.duty == 0U));
    expect_outputs(0U, 0U, 0U);

    /* The second bridge owns an independent pair of PWM channels. */
    assert(BSP_Motor_SetEnabled(motor_b, true));
    state = (BSP_Motor_State){BSP_MOTOR_MODE_FORWARD, 2500U};
    assert(BSP_Motor_Apply(motor_b, state));
    expect_outputs(1U, 2500U, 0U);
    expect_outputs(0U, 0U, 0U);
    assert(BSP_Motor_SetEnabled(motor_b, false));
    expect_outputs(1U, 0U, 0U);

    return 0;
}


