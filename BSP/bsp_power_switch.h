#ifndef BSP_POWER_SWITCH_H
#define BSP_POWER_SWITCH_H

#include <stdbool.h>
#include <stddef.h>

/* Active-high GPIO controls for external power switches. On MSPM0G3519,
 * output 0 enables the switched 12 V rail and output 1 enables the switched
 * 5 V rail. Load current never comes from an MCU pin. */
typedef enum {
    BSP_POWER_SWITCH_12V = 0,
    BSP_POWER_SWITCH_5V,
} BSP_PowerSwitch_Channel;

void BSP_PowerSwitch_Init(void);
size_t BSP_PowerSwitch_Count(void);
bool BSP_PowerSwitch_Set(size_t index, bool enabled);
bool BSP_PowerSwitch_Get(size_t index, bool *enabled);

#endif
