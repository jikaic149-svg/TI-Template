#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#include <stdbool.h>
#include <stddef.h>

/* Active-high PA7 drive for the external buzzer driver. */
void BSP_Buzzer_Init(void);
bool BSP_Buzzer_Set(bool enabled);
bool BSP_Buzzer_Get(bool *enabled);

#endif
