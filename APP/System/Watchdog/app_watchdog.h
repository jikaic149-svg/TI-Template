#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t APP_Watchdog_SourceMask;

#define APP_WATCHDOG_SOURCE_CONTROL_TASK UINT32_C(0x00000001)

typedef struct {
    APP_Watchdog_SourceMask required_sources;
    APP_Watchdog_SourceMask completed_sources;
} APP_Watchdog_Health;

/* Pure system policy: hardware feeding is permitted only when every required
 * source completed its current supervision cycle. */
bool APP_Watchdog_ShouldFeed(const APP_Watchdog_Health *health);

#endif
