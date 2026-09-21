#ifndef SERVICE_LINE_H
#define SERVICE_LINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SERVICE_LINE_IN_PROGRESS = 0,
    SERVICE_LINE_READY,
    SERVICE_LINE_REJECTED,
    SERVICE_LINE_INVALID
} Service_Line_Result;

typedef struct {
    char *storage;
    size_t capacity;
    size_t length;
    bool discarding;
    bool carriage_return;
} Service_Line;

/* One Task owns each parser. Storage includes space for the trailing NUL.
 * Printable ASCII and LF/CRLF only. An invalid/overlong line is discarded
 * through LF; REJECTED is returned once at that LF. No CRC, heap or timeout. */
bool Service_Line_Init(Service_Line *service, char *storage, size_t capacity);
Service_Line_Result Service_Line_Feed(Service_Line *service, uint8_t byte);
/* READY exposes a NUL-terminated line in caller storage until the next Feed.
 * The consumer validates command names/ranges before publishing typed data. */

#endif
