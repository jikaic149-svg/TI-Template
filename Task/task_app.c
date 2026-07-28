#include "task_app.h"

#include "FreeRTOS.h"
#include "app_watchdog.h"
#include "device_watchdog.h"
#include "task.h"
#include "core_config.h"

#define TASK_CONTROL_PRIORITY \
    (tskIDLE_PRIORITY + TEMPLATE_TASK_CONTROL_PRIORITY_OFFSET)

static StaticTask_t g_TASK_ControlTCB;
static StackType_t
    g_TASK_ControlStack[TEMPLATE_TASK_CONTROL_STACK_DEPTH];
static Device_Watchdog g_TASK_Watchdog;

volatile uint32_t g_TASK_Heartbeat = 0U;
volatile uint32_t g_TASK_Fault = 0U;
volatile uint32_t g_TASK_Ready = 0U;
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
        {
            const APP_Watchdog_Health watchdog_health = {
                .required_sources = APP_WATCHDOG_SOURCE_CONTROL_TASK,
                .completed_sources = APP_WATCHDOG_SOURCE_CONTROL_TASK,
            };

            if (!APP_Watchdog_ShouldFeed(&watchdog_health) ||
                (Device_Watchdog_Feed(&g_TASK_Watchdog) !=
                    DEVICE_WATCHDOG_RESULT_OK)) {
                g_TASK_Fault = 3U;
            }
        }
        vTaskDelayUntil(
            &last_wake,
            pdMS_TO_TICKS(TEMPLATE_TASK_CONTROL_PERIOD_MS));
    }
}

bool Task_Init(void)
{
    TaskHandle_t task;

    g_TASK_Heartbeat = 0U;
    g_TASK_Fault = 0U;
    g_TASK_Ready = 0U;
    g_TASK_UpdateCount = 0U;
    g_TASK_NowMs = 0U;

    if (!Device_Watchdog_Init(&g_TASK_Watchdog)) {
        g_TASK_Fault = 3U;
        return false;
    }

    task = xTaskCreateStatic(
        Task_Control,
        TEMPLATE_TASK_CONTROL_NAME,
        TEMPLATE_TASK_CONTROL_STACK_DEPTH,
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
