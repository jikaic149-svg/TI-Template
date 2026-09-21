#ifndef CORE_MESSAGE_PORT_H
#define CORE_MESSAGE_PORT_H

/* A port serializes all runtime operations, including the complete payload
 * copy. Firmware uses FreeRTOS task critical sections on a single-core MCU.
 * These functions may not block or run subscriber code. */
void Core_Message_Port_Enter(void);
void Core_Message_Port_Exit(void);

#endif
