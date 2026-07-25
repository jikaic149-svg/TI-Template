#include "bsp_buzzer.h"

#include "bsp_resource_map.h"

static bool g_buzzer_enabled;

void BSP_Buzzer_Init(void)
{
    DL_GPIO_clearPins(BSP_RESOURCE_BUZZER_PORT,
        BSP_RESOURCE_BUZZER_PIN);
    g_buzzer_enabled = false;
}

bool BSP_Buzzer_Set(bool enabled)
{
    if (enabled) {
        DL_GPIO_setPins(BSP_RESOURCE_BUZZER_PORT,
            BSP_RESOURCE_BUZZER_PIN);
    }
    else {
        DL_GPIO_clearPins(BSP_RESOURCE_BUZZER_PORT,
            BSP_RESOURCE_BUZZER_PIN);
    }
    g_buzzer_enabled = enabled;
    return true;
}

bool BSP_Buzzer_Get(bool *enabled)
{
    if (enabled == NULL) {
        return false;
    }
    *enabled = g_buzzer_enabled;
    return true;
}
