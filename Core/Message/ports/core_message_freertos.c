#include "core_message_port.h"

#include "FreeRTOS.h"
#include "task.h"

void Core_Message_Port_Enter(void)
{
    taskENTER_CRITICAL();
}

void Core_Message_Port_Exit(void)
{
    taskEXIT_CRITICAL();
}
