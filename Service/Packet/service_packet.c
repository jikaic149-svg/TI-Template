#include "service_packet.h"

#include <string.h>

bool Service_Packet_Init(Service_Packet *service, Core_Message_Topic *output)
{
    if (service == NULL) {
        return false;
    }
    service->output = NULL;
    if ((output == NULL) || (output->subscriptions == NULL) ||
        (output->item_size != sizeof(Service_Packet_Message)) ||
        !Algorithm_Packet_Parser_Init(&service->parser, service->storage,
            sizeof(service->storage))) {
        return false;
    }
    service->output = output;
    return true;
}

Service_Packet_Result Service_Packet_Feed(
    Service_Packet *service, uint8_t byte, uint32_t now_ms)
{
    Algorithm_Packet_View view;
    Algorithm_Packet_FeedResult result;
    Core_Message_Result published;

    if ((service == NULL) || (service->output == NULL)) {
        return SERVICE_PACKET_INVALID;
    }
    result = Algorithm_Packet_Parser_Feed(&service->parser, byte, &view);
    if (result == ALGORITHM_PACKET_FEED_IN_PROGRESS) {
        return SERVICE_PACKET_IN_PROGRESS;
    }
    if (result == ALGORITHM_PACKET_FEED_FRAME_REJECTED) {
        return SERVICE_PACKET_REJECTED;
    }
    if (result != ALGORITHM_PACKET_FEED_FRAME_READY) {
        return SERVICE_PACKET_INVALID;
    }
    memset(&service->decoded, 0, sizeof(service->decoded));
    service->decoded.timestamp_ms = now_ms;
    service->decoded.sequence = view.sequence;
    service->decoded.payload_length = (uint16_t)view.payload_length;
    memcpy(service->decoded.type, view.type, view.type_length);
    memcpy(service->decoded.payload, view.payload, view.payload_length);
    published = Core_Message_Publish(service->output, &service->decoded,
        sizeof(service->decoded));
    if (published == CORE_MESSAGE_FULL) {
        return SERVICE_PACKET_FULL;
    }
    return (published == CORE_MESSAGE_OK) ? SERVICE_PACKET_ACCEPTED :
        SERVICE_PACKET_INVALID;
}

Core_Message_Result Service_Packet_Take(
    Core_Message_Subscription *subscription, Service_Packet_Message *message)
{
    return Core_Message_Take(subscription, message, sizeof(*message));
}
