#include "bsp_watchdog.h"

#include "bsp_resource_map.h"

bool BSP_Watchdog_Init(void)
{
    DL_WWDT_setCoreHaltBehavior(
        BSP_RESOURCE_WATCHDOG, DL_WWDT_CORE_HALT_STOP);
    return BSP_Watchdog_IsRunning();
}

bool BSP_Watchdog_IsRunning(void)
{
    return DL_WWDT_isRunning(BSP_RESOURCE_WATCHDOG);
}

bool BSP_Watchdog_Feed(void)
{
    if (!BSP_Watchdog_IsRunning()) {
        return false;
    }
    DL_WWDT_restart(BSP_RESOURCE_WATCHDOG);
    return true;
}
