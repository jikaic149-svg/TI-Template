#ifndef BSP_WATCHDOG_H
#define BSP_WATCHDOG_H

#include <stdbool.h>

/* WWDT is started by SysConfig before BSP_Init. BSP_Init verifies the running
 * instance and makes it stop while the debugger halts the core. */
bool BSP_Watchdog_Init(void);
bool BSP_Watchdog_IsRunning(void);
bool BSP_Watchdog_Feed(void);

#endif
