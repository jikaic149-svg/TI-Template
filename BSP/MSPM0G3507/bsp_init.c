#include "bsp_init.h"

#include "bsp_internal.h"
#include "bsp_led.h"
#include "bsp_rgb.h"
#include "bsp_watchdog.h"

void BSP_Init(void)
{
    (void)BSP_Watchdog_Init();
    BSP_Encoder_Init();
    BSP_LED_Init();
    BSP_RGB_Init();
}
