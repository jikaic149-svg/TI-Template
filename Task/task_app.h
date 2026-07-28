#ifndef TASK_APP_H
#define TASK_APP_H

#include <stdbool.h>
#include <stdint.h>

extern volatile uint32_t g_TASK_Heartbeat;
extern volatile uint32_t g_TASK_Fault;
extern volatile uint32_t g_TASK_Ready;
extern volatile uint32_t g_TASK_UpdateCount;
extern volatile uint32_t g_TASK_NowMs;

/* Create the statically allocated control task before starting FreeRTOS. */
bool Task_Init(void);

#endif
