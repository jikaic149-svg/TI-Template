#include "bsp_motor.h"

#include <stddef.h>

#include "bsp_resource_map.h"

#define BSP_MOTOR_COUNT 2U

struct BSP_Motor_Resource {
    BSP_PWM_Handle pwm;
    uint32_t in1_pin;
    uint32_t in2_pin;
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

static void motor_set_direction(
    const struct BSP_Motor_Resource *motor, bool in1, bool in2)
{
    const uint32_t clear_mask = (in1 ? 0U : motor->in1_pin) |
                                (in2 ? 0U : motor->in2_pin);
    const uint32_t set_mask = (in1 ? motor->in1_pin : 0U) |
                              (in2 ? motor->in2_pin : 0U);

    if (clear_mask != 0U) {
        DL_GPIO_clearPins(BSP_RESOURCE_MOTOR_DIRECTION_PORT, clear_mask);
    }
    if (set_mask != 0U) {
        DL_GPIO_setPins(BSP_RESOURCE_MOTOR_DIRECTION_PORT, set_mask);
    }
}

static void motor_set_standby(bool enabled)
{
    if (enabled) {
        DL_GPIO_setPins(
            BSP_RESOURCE_MOTOR_STANDBY_PORT,
            BSP_RESOURCE_MOTOR_STANDBY_PIN);
    } else {
        DL_GPIO_clearPins(
            BSP_RESOURCE_MOTOR_STANDBY_PORT,
            BSP_RESOURCE_MOTOR_STANDBY_PIN);
    }
}

static bool motor_any_enabled(void)
{
    for (size_t index = 0U; index < BSP_MOTOR_COUNT; ++index) {
        if (g_motor[index].enabled) {
            return true;
        }
    }
    return false;
}

static bool motor_force_safe_low(const struct BSP_Motor_Resource *motor)
{
    const bool pwm_stopped = BSP_PWM_SetDuty(motor->pwm, 0U);

    motor_set_direction(motor, false, false);
    return pwm_stopped;
}

static bool motor_apply_outputs(
    const struct BSP_Motor_Resource *motor, BSP_Motor_State state)
{
    bool in1 = false;
    bool in2 = false;
    BSP_PWM_Duty duty = BSP_PWM_DUTY_MAX;

    if (!BSP_PWM_SetDuty(motor->pwm, 0U)) {
        motor_set_direction(motor, false, false);
        return false;
    }

    switch (state.mode) {
    case BSP_MOTOR_MODE_COAST:
        break;
    case BSP_MOTOR_MODE_BRAKE:
        in1 = true;
        in2 = true;
        break;
    case BSP_MOTOR_MODE_FORWARD:
        in1 = true;
        duty = state.duty;
        break;
    case BSP_MOTOR_MODE_REVERSE:
        in2 = true;
        duty = state.duty;
        break;
    default:
        return false;
    }

    motor_set_direction(motor, in1, in2);
    if (!BSP_PWM_SetDuty(motor->pwm, duty)) {
        (void)BSP_PWM_SetDuty(motor->pwm, 0U);
        motor_set_direction(motor, false, false);
        return false;
    }
    return true;
}

static bool motor_force_all_safe_low(void)
{
    bool safe = true;

    for (size_t index = 0U; index < BSP_MOTOR_COUNT; ++index) {
        if (!motor_force_safe_low(&g_motor[index])) {
            safe = false;
        }
    }
    return safe;
}

void BSP_Motor_Init(void)
{
    g_motor[0] = (struct BSP_Motor_Resource){
        .pwm = BSP_PWM_GetHandle(0U),
        .in1_pin = BSP_RESOURCE_MOTOR_A_IN1,
        .in2_pin = BSP_RESOURCE_MOTOR_A_IN2,
        .state = {BSP_MOTOR_MODE_COAST, 0U},
    };
    g_motor[1] = (struct BSP_Motor_Resource){
        .pwm = BSP_PWM_GetHandle(1U),
        .in1_pin = BSP_RESOURCE_MOTOR_B_IN1,
        .in2_pin = BSP_RESOURCE_MOTOR_B_IN2,
        .state = {BSP_MOTOR_MODE_COAST, 0U},
    };

    motor_set_standby(false);
    (void)motor_force_all_safe_low();
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
        const bool safe = motor_any_enabled()
            ? motor_apply_outputs(resource,
                  (BSP_Motor_State){BSP_MOTOR_MODE_COAST, 0U})
            : motor_force_safe_low(resource);

        if (!safe) {
            return false;
        }
        resource->state = state;
        return true;
    }

    if (!motor_apply_outputs(resource, state)) {
        (void)motor_force_safe_low(resource);
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
            (void)motor_force_safe_low(resource);
            return false;
        }
        resource->enabled = true;
        motor_set_standby(true);
        return true;
    }

    resource->enabled = false;
    resource->state = (BSP_Motor_State){BSP_MOTOR_MODE_COAST, 0U};
    if (motor_any_enabled()) {
        return motor_apply_outputs(resource, resource->state);
    }

    motor_set_standby(false);
    return motor_force_all_safe_low();
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
