#ifndef TASK_APP_H
#define TASK_APP_H

#include <stdbool.h>
#include <stdint.h>

extern volatile uint32_t g_TASK_Heartbeat;
extern volatile uint32_t g_TASK_Fault;
extern volatile uint32_t g_TASK_Ready;
extern volatile uint32_t g_TASK_MotorLocked;
extern volatile uint32_t g_TASK_OutputEnabled;
extern volatile uint32_t g_TASK_ModeFault;
extern volatile uint32_t g_TASK_UpdateCount;
extern volatile uint32_t g_TASK_NowMs;

/* Create the statically allocated product task before starting FreeRTOS. */
bool Task_Init(void);

#endif
