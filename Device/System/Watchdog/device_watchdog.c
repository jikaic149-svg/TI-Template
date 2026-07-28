#include "device_watchdog.h"

#include <stddef.h>

#include "bsp_watchdog.h"

bool Device_Watchdog_Init(Device_Watchdog *watchdog)
{
    if (watchdog != NULL) {
        *watchdog = (Device_Watchdog){0};
    }
    if ((watchdog == NULL) || !BSP_Watchdog_IsRunning()) {
        return false;
    }
    watchdog->initialized = true;
    return true;
}

Device_Watchdog_Result Device_Watchdog_Feed(Device_Watchdog *watchdog)
{
    if ((watchdog == NULL) || !watchdog->initialized) {
        return DEVICE_WATCHDOG_RESULT_INVALID_STATE;
    }
    if (!BSP_Watchdog_Feed()) {
        return DEVICE_WATCHDOG_RESULT_BACKEND_ERROR;
    }
    return DEVICE_WATCHDOG_RESULT_OK;
}
