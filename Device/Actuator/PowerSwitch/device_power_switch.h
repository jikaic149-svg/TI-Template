#ifndef DEVICE_POWER_SWITCH_H
#define DEVICE_POWER_SWITCH_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DEVICE_POWER_SWITCH_RESULT_OK = 0,
    DEVICE_POWER_SWITCH_RESULT_INVALID_ARGUMENT,
    DEVICE_POWER_SWITCH_RESULT_BACKEND_ERROR,
} Device_PowerSwitch_Result;

typedef struct {
    size_t backend_index;
} Device_PowerSwitch_Config;

typedef struct {
    Device_PowerSwitch_Config config;
    bool enabled;
    bool initialized;
} Device_PowerSwitch;

bool Device_PowerSwitch_DefaultConfig(
    size_t index, Device_PowerSwitch_Config *config);
/* Init always disables the output before reporting success. */
bool Device_PowerSwitch_Init(
    Device_PowerSwitch *power_switch,
    const Device_PowerSwitch_Config *config);
Device_PowerSwitch_Result Device_PowerSwitch_Set(
    Device_PowerSwitch *power_switch, bool enabled);
bool Device_PowerSwitch_IsEnabled(
    const Device_PowerSwitch *power_switch);

#endif
