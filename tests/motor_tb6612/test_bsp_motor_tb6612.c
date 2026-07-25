#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t g_mock_direction_port;
static uint8_t g_mock_standby_port;

#define BSP_MSPM0G3519_RESOURCE_MAP_H
#define BSP_RESOURCE_MOTOR_DIRECTION_PORT ((void *)&g_mock_direction_port)
#define BSP_RESOURCE_MOTOR_A_IN1 (UINT32_C(1) << 0)
#define BSP_RESOURCE_MOTOR_A_IN2 (UINT32_C(1) << 1)
#define BSP_RESOURCE_MOTOR_B_IN1 (UINT32_C(1) << 2)
#define BSP_RESOURCE_MOTOR_B_IN2 (UINT32_C(1) << 3)
#define BSP_RESOURCE_MOTOR_STANDBY_PORT ((void *)&g_mock_standby_port)
#define BSP_RESOURCE_MOTOR_STANDBY_PIN (UINT32_C(1) << 0)

#include "bsp_motor.h"

struct BSP_PWM_Resource {
    size_t index;
    BSP_PWM_Duty duty;
};

static struct BSP_PWM_Resource g_mock_pwm[2] = {
    {0U, 0U},
    {1U, 0U},
};
static uint32_t g_direction_output;
static uint32_t g_standby_output;

static void DL_GPIO_setPins(void *port, uint32_t pins)
{
    if (port == BSP_RESOURCE_MOTOR_DIRECTION_PORT) {
        g_direction_output |= pins;
    } else {
        assert(port == BSP_RESOURCE_MOTOR_STANDBY_PORT);
        g_standby_output |= pins;
    }
}

static void DL_GPIO_clearPins(void *port, uint32_t pins)
{
    if (port == BSP_RESOURCE_MOTOR_DIRECTION_PORT) {
        g_direction_output &= ~pins;
    } else {
        assert(port == BSP_RESOURCE_MOTOR_STANDBY_PORT);
        g_standby_output &= ~pins;
    }
}

size_t BSP_PWM_Count(void)
{
    return 2U;
}

BSP_PWM_Handle BSP_PWM_GetHandle(size_t index)
{
    return (index < 2U) ? &g_mock_pwm[index] : NULL;
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

#include "../../BSP/MSPM0G3519/bsp_motor_tb6612.c"

static void expect_motor(size_t motor_index, BSP_PWM_Duty pwm_duty,
    bool in1, bool in2)
{
    const uint32_t in1_pin = (motor_index == 0U)
        ? BSP_RESOURCE_MOTOR_A_IN1 : BSP_RESOURCE_MOTOR_B_IN1;
    const uint32_t in2_pin = (motor_index == 0U)
        ? BSP_RESOURCE_MOTOR_A_IN2 : BSP_RESOURCE_MOTOR_B_IN2;

    assert(g_mock_pwm[motor_index].duty == pwm_duty);
    assert(((g_direction_output & in1_pin) != 0U) == in1);
    assert(((g_direction_output & in2_pin) != 0U) == in2);
}

static void expect_standby(bool enabled)
{
    assert(((g_standby_output & BSP_RESOURCE_MOTOR_STANDBY_PIN) != 0U) ==
           enabled);
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
    expect_standby(false);
    expect_motor(0U, 0U, false, false);
    expect_motor(1U, 0U, false, false);

    state = (BSP_Motor_State){BSP_MOTOR_MODE_FORWARD, 3000U};
    assert(!BSP_Motor_Apply(motor_a, state));
    state = (BSP_Motor_State){BSP_MOTOR_MODE_BRAKE, 0U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_standby(false);
    expect_motor(0U, 0U, false, false);

    assert(BSP_Motor_SetEnabled(motor_a, true));
    expect_standby(true);
    expect_motor(0U, BSP_PWM_DUTY_MAX, true, true);

    state = (BSP_Motor_State){BSP_MOTOR_MODE_FORWARD, 3000U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_motor(0U, 3000U, true, false);
    state = (BSP_Motor_State){BSP_MOTOR_MODE_REVERSE, 3000U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_motor(0U, 3000U, false, true);
    state = (BSP_Motor_State){BSP_MOTOR_MODE_COAST, 0U};
    assert(BSP_Motor_Apply(motor_a, state));
    expect_motor(0U, BSP_PWM_DUTY_MAX, false, false);

    assert(BSP_Motor_SetEnabled(motor_b, true));
    expect_standby(true);
    expect_motor(1U, BSP_PWM_DUTY_MAX, false, false);
    assert(BSP_Motor_SetEnabled(motor_a, false));
    expect_standby(true);
    expect_motor(0U, BSP_PWM_DUTY_MAX, false, false);

    state = (BSP_Motor_State){BSP_MOTOR_MODE_FORWARD, 2500U};
    assert(BSP_Motor_Apply(motor_b, state));
    expect_motor(1U, 2500U, true, false);
    assert(BSP_Motor_SetEnabled(motor_b, false));
    expect_standby(false);
    expect_motor(0U, 0U, false, false);
    expect_motor(1U, 0U, false, false);
    assert(BSP_Motor_IsEnabled(motor_b, &enabled) && !enabled);

    return 0;
}


