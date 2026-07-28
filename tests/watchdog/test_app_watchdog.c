#include "app_watchdog.h"

#include <stdio.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at line %d: %s\n",                \
                __LINE__, #condition);                                        \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main(void)
{
    const APP_Watchdog_Health complete = {
        .required_sources = APP_WATCHDOG_SOURCE_CONTROL_TASK,
        .completed_sources = APP_WATCHDOG_SOURCE_CONTROL_TASK,
    };
    const APP_Watchdog_Health incomplete = {
        .required_sources = APP_WATCHDOG_SOURCE_CONTROL_TASK | UINT32_C(0x2),
        .completed_sources = APP_WATCHDOG_SOURCE_CONTROL_TASK,
    };
    const APP_Watchdog_Health no_required_sources = {0};

    CHECK(!APP_Watchdog_ShouldFeed(NULL));
    CHECK(!APP_Watchdog_ShouldFeed(&no_required_sources));
    CHECK(!APP_Watchdog_ShouldFeed(&incomplete));
    CHECK(APP_Watchdog_ShouldFeed(&complete));
    puts("APP watchdog policy tests: PASS");
    return 0;
}
