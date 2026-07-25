#include "bsp_power_switch.h"

#include "bsp_resource_map.h"

#include <stdint.h>

static bool g_power_switch_enabled[BSP_RESOURCE_POWER_SWITCH_COUNT];
static GPIO_Regs *const g_power_switch_port[
    BSP_RESOURCE_POWER_SWITCH_COUNT] = {
    BSP_RESOURCE_POWER_SWITCH_0_PORT,
    BSP_RESOURCE_POWER_SWITCH_1_PORT,
};
static const uint32_t g_power_switch_pin[
    BSP_RESOURCE_POWER_SWITCH_COUNT] = {
    BSP_RESOURCE_POWER_SWITCH_0_PIN,
    BSP_RESOURCE_POWER_SWITCH_1_PIN,
};

void BSP_PowerSwitch_Init(void)
{
    for (size_t index = 0U;
         index < BSP_RESOURCE_POWER_SWITCH_COUNT; ++index) {
        DL_GPIO_clearPins(g_power_switch_port[index],
            g_power_switch_pin[index]);
        g_power_switch_enabled[index] = false;
    }
}

size_t BSP_PowerSwitch_Count(void)
{
    return BSP_RESOURCE_POWER_SWITCH_COUNT;
}

bool BSP_PowerSwitch_Set(size_t index, bool enabled)
{
    if (index >= BSP_RESOURCE_POWER_SWITCH_COUNT) {
        return false;
    }
    if (enabled) {
        DL_GPIO_setPins(g_power_switch_port[index],
            g_power_switch_pin[index]);
    }
    else {
        DL_GPIO_clearPins(g_power_switch_port[index],
            g_power_switch_pin[index]);
    }
    g_power_switch_enabled[index] = enabled;
    return true;
}

bool BSP_PowerSwitch_Get(size_t index, bool *enabled)
{
    if ((index >= BSP_RESOURCE_POWER_SWITCH_COUNT) || (enabled == NULL)) {
        return false;
    }
    *enabled = g_power_switch_enabled[index];
    return true;
}
