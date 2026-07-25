#include "bsp_motor.h"

#include <stddef.h>

#include "bsp_resource_map.h"

#define BSP_MOTOR_COUNT 2U

struct BSP_Motor_Resource {
    BSP_PWM_Handle in1_pwm;
    BSP_PWM_Handle in2_pwm;
    BSP_Motor_State state;
    bool enabled;
};

static struct BSP_Motor_Resource g_motor[BSP_MOTOR_COUNT];

static struct BSP_Motor_Resource *motor_from_handle(BSP_Motor_Handle motor)
{
    for (size_t index = 0U; index < BSP_MOTOR_COUNT; ++index) {
        if (motor == &g_motor[index]) {
            return &g_motor[index];
        }
    }
    return NULL;
}

static bool motor_force_coast(const struct BSP_Motor_Resource *motor)
{
    const bool in1_stopped = BSP_PWM_SetDuty(motor->in1_pwm, 0U);
    const bool in2_stopped = BSP_PWM_SetDuty(motor->in2_pwm, 0U);

    return in1_stopped && in2_stopped;
}

static bool motor_apply_outputs(
    const struct BSP_Motor_Resource *motor, BSP_Motor_State state)
{
    switch (state.mode) {
    case BSP_MOTOR_MODE_COAST:
        return motor_force_coast(motor);
    case BSP_MOTOR_MODE_BRAKE:
        return BSP_PWM_SetDuty(motor->in1_pwm, BSP_PWM_DUTY_MAX) &&
               BSP_PWM_SetDuty(motor->in2_pwm, BSP_PWM_DUTY_MAX);
    case BSP_MOTOR_MODE_FORWARD:
        return BSP_PWM_SetDuty(motor->in2_pwm, 0U) &&
               BSP_PWM_SetDuty(motor->in1_pwm, state.duty);
    case BSP_MOTOR_MODE_REVERSE:
        return BSP_PWM_SetDuty(motor->in1_pwm, 0U) &&
               BSP_PWM_SetDuty(motor->in2_pwm, state.duty);
    default:
        return false;
    }
}

void BSP_Motor_Init(void)
{
    g_motor[0] = (struct BSP_Motor_Resource){
        .in1_pwm = BSP_PWM_GetHandle(0U),
        .in2_pwm = BSP_PWM_GetHandle(2U),
        .state = {BSP_MOTOR_MODE_COAST, 0U},
    };
    g_motor[1] = (struct BSP_Motor_Resource){
        .in1_pwm = BSP_PWM_GetHandle(1U),
        .in2_pwm = BSP_PWM_GetHandle(3U),
        .state = {BSP_MOTOR_MODE_COAST, 0U},
    };
    (void)motor_force_coast(&g_motor[0]);
    (void)motor_force_coast(&g_motor[1]);
}

size_t BSP_Motor_Count(void)
{
    return BSP_MOTOR_COUNT;
}

BSP_Motor_Handle BSP_Motor_GetHandle(size_t index)
{
    return (index < BSP_MOTOR_COUNT) ? &g_motor[index] : NULL;
}

bool BSP_Motor_Apply(BSP_Motor_Handle motor, BSP_Motor_State state)
{
    struct BSP_Motor_Resource *resource = motor_from_handle(motor);

    if ((resource == NULL) || (state.mode > BSP_MOTOR_MODE_REVERSE) ||
        (state.duty > BSP_PWM_DUTY_MAX) ||
        (((state.mode == BSP_MOTOR_MODE_FORWARD) ||
          (state.mode == BSP_MOTOR_MODE_REVERSE)) &&
         !resource->enabled)) {
        return false;
    }
    if ((state.mode == BSP_MOTOR_MODE_COAST) ||
        (state.mode == BSP_MOTOR_MODE_BRAKE)) {
        state.duty = 0U;
    }

    if (!resource->enabled) {
        if (!motor_force_coast(resource)) {
            return false;
        }
        resource->state = state;
        return true;
    }
    if (!motor_apply_outputs(resource, state)) {
        (void)motor_force_coast(resource);
        resource->state =
            (BSP_Motor_State){BSP_MOTOR_MODE_COAST, 0U};
        return false;
    }
    resource->state = state;
    return true;
}

bool BSP_Motor_GetState(BSP_Motor_Handle motor, BSP_Motor_State *state)
{
    struct BSP_Motor_Resource *resource = motor_from_handle(motor);

    if ((resource == NULL) || (state == NULL)) {
        return false;
    }
    *state = resource->state;
    return true;
}

bool BSP_Motor_SetEnabled(BSP_Motor_Handle motor, bool enabled)
{
    struct BSP_Motor_Resource *resource = motor_from_handle(motor);

    if (resource == NULL) {
        return false;
    }
    if (enabled) {
        if ((resource->state.duty != 0U) ||
            ((resource->state.mode != BSP_MOTOR_MODE_COAST) &&
             (resource->state.mode != BSP_MOTOR_MODE_BRAKE))) {
            return false;
        }
        if (!motor_apply_outputs(resource, resource->state)) {
            (void)motor_force_coast(resource);
            return false;
        }
        resource->enabled = true;
        return true;
    }

    resource->enabled = false;
    const bool coasted = motor_force_coast(resource);

    resource->state = (BSP_Motor_State){BSP_MOTOR_MODE_COAST, 0U};
    return coasted;
}

bool BSP_Motor_IsEnabled(BSP_Motor_Handle motor, bool *enabled)
{
    struct BSP_Motor_Resource *resource = motor_from_handle(motor);

    if ((resource == NULL) || (enabled == NULL)) {
        return false;
    }
    *enabled = resource->enabled;
    return true;
}
