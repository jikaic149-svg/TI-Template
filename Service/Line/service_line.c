#include "service_line.h"

bool Service_Line_Init(Service_Line *service, char *storage, size_t capacity)
{
    if (service == NULL) {
        return false;
    }
    *service = (Service_Line){0};
    if ((storage == NULL) || (capacity < 2U)) {
        return false;
    }
    service->storage = storage;
    service->capacity = capacity;
    storage[0] = '\0';
    return true;
}

Service_Line_Result Service_Line_Feed(Service_Line *service, uint8_t byte)
{
    if ((service == NULL) || (service->storage == NULL)) {
        return SERVICE_LINE_INVALID;
    }
    if (byte == (uint8_t)'\n') {
        const bool rejected = service->discarding || (service->length == 0U);
        service->storage[service->length] = '\0';
        service->length = 0U;
        service->discarding = false;
        service->carriage_return = false;
        return rejected ? SERVICE_LINE_REJECTED : SERVICE_LINE_READY;
    }
    if (service->discarding) {
        return SERVICE_LINE_IN_PROGRESS;
    }
    if (service->carriage_return) {
        service->discarding = true;
    }
    else if (byte == (uint8_t)'\r') {
        service->carriage_return = true;
    }
    else if ((byte < 0x20U) || (byte > 0x7EU) ||
        (service->length == (service->capacity - 1U))) {
        service->discarding = true;
    }
    else {
        service->storage[service->length++] = (char)byte;
    }
    return SERVICE_LINE_IN_PROGRESS;
}
