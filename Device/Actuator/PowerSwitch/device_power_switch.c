#include "device_power_switch.h"

#include "bsp_power_switch.h"

#include <stddef.h>

bool Device_PowerSwitch_DefaultConfig(
    size_t index, Device_PowerSwitch_Config *config)
{
    if ((config == NULL) || (index >= BSP_PowerSwitch_Count())) {
        return false;
    }
    *config = (Device_PowerSwitch_Config){
        .backend_index = index,
    };
    return true;
}

bool Device_PowerSwitch_Init(
    Device_PowerSwitch *power_switch,
    const Device_PowerSwitch_Config *config)
{
    if (power_switch != NULL) {
        *power_switch = (Device_PowerSwitch){0};
    }
    if ((power_switch == NULL) || (config == NULL) ||
        (config->backend_index >= BSP_PowerSwitch_Count()) ||
        !BSP_PowerSwitch_Set(config->backend_index, false)) {
        return false;
    }
    power_switch->config = *config;
    power_switch->initialized = true;
    return true;
}

Device_PowerSwitch_Result Device_PowerSwitch_Set(
    Device_PowerSwitch *power_switch, bool enabled)
{
    if ((power_switch == NULL) || !power_switch->initialized) {
        return DEVICE_POWER_SWITCH_RESULT_INVALID_ARGUMENT;
    }
    if (power_switch->enabled == enabled) {
        return DEVICE_POWER_SWITCH_RESULT_OK;
    }
    if (!BSP_PowerSwitch_Set(
            power_switch->config.backend_index, enabled)) {
        power_switch->enabled = false;
        (void)BSP_PowerSwitch_Set(power_switch->config.backend_index, false);
        return DEVICE_POWER_SWITCH_RESULT_BACKEND_ERROR;
    }
    power_switch->enabled = enabled;
    return DEVICE_POWER_SWITCH_RESULT_OK;
}

bool Device_PowerSwitch_IsEnabled(
    const Device_PowerSwitch *power_switch)
{
    return (power_switch != NULL) && power_switch->initialized &&
           power_switch->enabled;
}
