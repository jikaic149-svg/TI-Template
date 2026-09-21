#ifndef EXAMPLE_COMMAND_FLOW_H
#define EXAMPLE_COMMAND_FLOW_H

#include <stdbool.h>
#include <stdint.h>
#include "core_message.h"

/* Bitmask returned per Update for diagnostics, not watchdog policy. */
#define EXAMPLE_COMMAND_FLOW_REJECTED UINT32_C(1)
#define EXAMPLE_COMMAND_FLOW_FULL UINT32_C(2)
#define EXAMPLE_COMMAND_FLOW_IO_ERROR UINT32_C(4)
#define EXAMPLE_COMMAND_FLOW_TX_DROPPED UINT32_C(8)
#define EXAMPLE_COMMAND_FLOW_DEVICE_ERROR UINT32_C(16)
#define EXAMPLE_COMMAND_FLOW_NOT_READY UINT32_C(32)

typedef struct {
    uint32_t sequence;       /* Local accepted-command sequence, wraps. */
    uint32_t timestamp_ms;   /* Device submission time, wraps. */
    bool on;                /* Last successfully applied logical state. */
    bool applied;           /* This command reached Device successfully. */
} Example_CommandFlow_Status;

/* Startup once, after BSP init. Selected example owns one Serial and LED.
 * Update runs in the existing control Task; it creates no additional Task. */
bool Example_CommandFlow_Init(void);
uint32_t Example_CommandFlow_Update(uint32_t now_ms);

/* Independent diagnostic subscription. Does not consume the serial reply.
 * EMPTY before the first executed command and after the last Take. */
Core_Message_Result Example_CommandFlow_TakeStatus(
    Example_CommandFlow_Status *status);

#endif
