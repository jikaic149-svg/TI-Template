#include "bsp_servo.h"

#include "bsp_resource_map.h"

#include <stddef.h>

#define BSP_SERVO_INITIAL_PULSE_US UINT32_C(1500)

struct BSP_Servo_Resource {
    uint32_t pulse_us;
    bool enabled;
    uint32_t cc_index;
    uint32_t iomux;
    uint32_t iomux_func;
    GPIO_Regs *port;
    uint32_t pin;
};

static struct BSP_Servo_Resource g_servo[BSP_RESOURCE_SERVO_COUNT] = {
    {
        .pulse_us = BSP_SERVO_INITIAL_PULSE_US,
        .cc_index = BSP_RESOURCE_SERVO_0_CC,
        .iomux = BSP_RESOURCE_SERVO_0_IOMUX,
        .iomux_func = BSP_RESOURCE_SERVO_0_IOMUX_FUNC,
        .port = BSP_RESOURCE_SERVO_0_PORT,
        .pin = BSP_RESOURCE_SERVO_0_PIN,
    },
    {
        .pulse_us = BSP_SERVO_INITIAL_PULSE_US,
        .cc_index = BSP_RESOURCE_SERVO_1_CC,
        .iomux = BSP_RESOURCE_SERVO_1_IOMUX,
        .iomux_func = BSP_RESOURCE_SERVO_1_IOMUX_FUNC,
        .port = BSP_RESOURCE_SERVO_1_PORT,
        .pin = BSP_RESOURCE_SERVO_1_PIN,
    },
};

static struct BSP_Servo_Resource *servo_from_handle(
    BSP_Servo_Handle servo)
{
    for (size_t index = 0U; index < BSP_RESOURCE_SERVO_COUNT; ++index) {
        if (servo == &g_servo[index]) {
            return &g_servo[index];
        }
    }
    return NULL;
}

static bool servo_any_enabled(void)
{
    bool enabled = false;

    for (size_t index = 0U; index < BSP_RESOURCE_SERVO_COUNT; ++index) {
        enabled = enabled || g_servo[index].enabled;
    }
    return enabled;
}

static void servo_pin_low(const struct BSP_Servo_Resource *servo)
{
    DL_GPIO_initDigitalOutput(servo->iomux);
    DL_GPIO_clearPins(servo->port, servo->pin);
    DL_GPIO_enableOutput(servo->port, servo->pin);
}

static void servo_pin_pwm(const struct BSP_Servo_Resource *servo)
{
    DL_GPIO_initPeripheralOutputFunction(servo->iomux, servo->iomux_func);
    DL_GPIO_enableOutput(servo->port, servo->pin);
}

static void servo_apply_pulse(
    const struct BSP_Servo_Resource *servo, uint32_t pulse_us)
{
    const uint32_t period_ticks = BSP_RESOURCE_SERVO_PERIOD_US *
        BSP_RESOURCE_SERVO_TICKS_PER_US;
    const uint32_t pulse_ticks = pulse_us *
        BSP_RESOURCE_SERVO_TICKS_PER_US;

    DL_TimerG_setCaptureCompareValue(BSP_RESOURCE_SERVO_TIMER,
        period_ticks - pulse_ticks, servo->cc_index);
}

void BSP_Servo_Init(void)
{
    DL_TimerG_stopCounter(BSP_RESOURCE_SERVO_TIMER);
    for (size_t index = 0U; index < BSP_RESOURCE_SERVO_COUNT; ++index) {
        struct BSP_Servo_Resource *servo = &g_servo[index];

        DL_TimerG_setCaptCompUpdateMethod(BSP_RESOURCE_SERVO_TIMER,
            DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE, servo->cc_index);
        servo_apply_pulse(servo, servo->pulse_us);
        /* Apply subsequent pulse changes only at a PWM frame boundary. */
        DL_TimerG_setCaptCompUpdateMethod(BSP_RESOURCE_SERVO_TIMER,
            DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, servo->cc_index);
        servo_pin_low(servo);
        servo->enabled = false;
    }
}

size_t BSP_Servo_Count(void)
{
    return BSP_RESOURCE_SERVO_COUNT;
}

BSP_Servo_Handle BSP_Servo_GetHandle(size_t index)
{
    return (index < BSP_RESOURCE_SERVO_COUNT) ? &g_servo[index] : NULL;
}

bool BSP_Servo_SetEnabled(BSP_Servo_Handle servo, bool enabled)
{
    struct BSP_Servo_Resource *resource = servo_from_handle(servo);

    if (resource == NULL) {
        return false;
    }
    if (resource->enabled == enabled) {
        return true;
    }
    if (!enabled) {
        resource->enabled = false;
        servo_pin_low(resource);
        if (!servo_any_enabled()) {
            DL_TimerG_stopCounter(BSP_RESOURCE_SERVO_TIMER);
        }
        return true;
    }

    const bool timer_running = servo_any_enabled();
    DL_TimerG_setCaptCompUpdateMethod(BSP_RESOURCE_SERVO_TIMER,
        timer_running ? DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT :
                        DL_TIMER_CC_UPDATE_METHOD_IMMEDIATE,
        resource->cc_index);
    servo_apply_pulse(resource, resource->pulse_us);
    DL_TimerG_setCaptCompUpdateMethod(BSP_RESOURCE_SERVO_TIMER,
        DL_TIMER_CC_UPDATE_METHOD_ZERO_EVT, resource->cc_index);
    if (!timer_running) {
        DL_TimerG_setTimerCount(BSP_RESOURCE_SERVO_TIMER,
            BSP_RESOURCE_SERVO_PERIOD_US * BSP_RESOURCE_SERVO_TICKS_PER_US);
    }
    servo_pin_pwm(resource);
    resource->enabled = true;
    if (!timer_running) {
        DL_TimerG_startCounter(BSP_RESOURCE_SERVO_TIMER);
    }
    return true;
}

bool BSP_Servo_IsEnabled(BSP_Servo_Handle servo, bool *enabled)
{
    struct BSP_Servo_Resource *resource = servo_from_handle(servo);

    if ((resource == NULL) || (enabled == NULL)) {
        return false;
    }
    *enabled = resource->enabled;
    return true;
}

bool BSP_Servo_SetPulseUs(BSP_Servo_Handle servo, uint32_t pulse_us)
{
    struct BSP_Servo_Resource *resource = servo_from_handle(servo);

    if ((resource == NULL) || (pulse_us == 0U) ||
        (pulse_us >= BSP_RESOURCE_SERVO_PERIOD_US)) {
        return false;
    }
    servo_apply_pulse(resource, pulse_us);
    resource->pulse_us = pulse_us;
    return true;
}

bool BSP_Servo_GetPulseUs(BSP_Servo_Handle servo, uint32_t *pulse_us)
{
    struct BSP_Servo_Resource *resource = servo_from_handle(servo);

    if ((resource == NULL) || (pulse_us == NULL)) {
        return false;
    }
    *pulse_us = resource->pulse_us;
    return true;
}

bool BSP_Servo_GetFramePeriodUs(
    BSP_Servo_Handle servo, uint32_t *period_us)
{
    if ((servo_from_handle(servo) == NULL) || (period_us == NULL)) {
        return false;
    }
    *period_us = BSP_RESOURCE_SERVO_PERIOD_US;
    return true;
}
