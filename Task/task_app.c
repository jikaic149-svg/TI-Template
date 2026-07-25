#include "task_app.h"

#include "FreeRTOS.h"
#include "task.h"

#define TASK_CONTROL_PERIOD_MS UINT32_C(10)
#define TASK_CONTROL_STACK_DEPTH 512U
#define TASK_CONTROL_PRIORITY (tskIDLE_PRIORITY + 2U)

static StaticTask_t g_TASK_ControlTCB;
static StackType_t g_TASK_ControlStack[TASK_CONTROL_STACK_DEPTH];

volatile uint32_t g_TASK_Heartbeat = 0U;
volatile uint32_t g_TASK_Fault = 0U;
volatile uint32_t g_TASK_Ready = 0U;
volatile uint32_t g_TASK_MotorLocked = 1U;
volatile uint32_t g_TASK_OutputEnabled = 0U;
volatile uint32_t g_TASK_ModeFault = 0U;
volatile uint32_t g_TASK_UpdateCount = 0U;
volatile uint32_t g_TASK_NowMs = 0U;

static void Task_Control(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        g_TASK_NowMs =
            (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ++g_TASK_Heartbeat;
        ++g_TASK_UpdateCount;
        vTaskDelayUntil(
            &last_wake, pdMS_TO_TICKS(TASK_CONTROL_PERIOD_MS));
    }
}

bool Task_Init(void)
{
    TaskHandle_t task;

    g_TASK_Heartbeat = 0U;
    g_TASK_Fault = 0U;
    g_TASK_Ready = 0U;
    g_TASK_MotorLocked = 1U;
    g_TASK_OutputEnabled = 0U;
    g_TASK_ModeFault = 0U;
    g_TASK_UpdateCount = 0U;
    g_TASK_NowMs = 0U;

    task = xTaskCreateStatic(
        Task_Control,
        "Control",
        TASK_CONTROL_STACK_DEPTH,
        NULL,
        TASK_CONTROL_PRIORITY,
        g_TASK_ControlStack,
        &g_TASK_ControlTCB);
    if (task == NULL) {
        g_TASK_Fault = 1U;
        return false;
    }

    g_TASK_Ready = 1U;
    return true;
}
