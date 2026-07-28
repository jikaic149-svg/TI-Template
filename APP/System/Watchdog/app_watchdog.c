#include "app_watchdog.h"

#include <stddef.h>

bool APP_Watchdog_ShouldFeed(const APP_Watchdog_Health *health)
{
    return (health != NULL) && (health->required_sources != 0U) &&
        ((health->completed_sources & health->required_sources) ==
            health->required_sources);
}
