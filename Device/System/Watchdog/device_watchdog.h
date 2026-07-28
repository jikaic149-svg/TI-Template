#ifndef DEVICE_WATCHDOG_H
#define DEVICE_WATCHDOG_H

#include <stdbool.h>

typedef enum {
    DEVICE_WATCHDOG_RESULT_OK = 0,
    DEVICE_WATCHDOG_RESULT_INVALID_STATE,
    DEVICE_WATCHDOG_RESULT_BACKEND_ERROR,
} Device_Watchdog_Result;

typedef struct {
    bool initialized;
} Device_Watchdog;

bool Device_Watchdog_Init(Device_Watchdog *watchdog);
Device_Watchdog_Result Device_Watchdog_Feed(Device_Watchdog *watchdog);

#endif
