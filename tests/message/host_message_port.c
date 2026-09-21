#include "core_message_port.h"
#include <assert.h>

/* Serialized host tests: assert balanced, non-reentrant copy sections.
 * Firmware uses the separately compiled real FreeRTOS port. */
static unsigned int depth;
void Core_Message_Port_Enter(void)
{
    assert(depth == 0U);
    ++depth;
}
void Core_Message_Port_Exit(void)
{
    assert(depth == 1U);
    --depth;
}
