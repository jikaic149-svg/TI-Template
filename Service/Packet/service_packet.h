#ifndef SERVICE_PACKET_H
#define SERVICE_PACKET_H

#include "algorithm_packet.h"
#include "core_message.h"

/* Owned value; no pointer into parser storage crosses the topic boundary.
 * timestamp_ms is supplied by the calling Task when LF completes a frame. */
typedef struct {
    uint32_t timestamp_ms;
    uint16_t sequence;
    uint16_t payload_length;
    char type[ALGORITHM_PACKET_MAX_TYPE_LENGTH + 1U];
    uint8_t payload[ALGORITHM_PACKET_MAX_PAYLOAD_LENGTH];
} Service_Packet_Message;

typedef enum {
    SERVICE_PACKET_IN_PROGRESS = 0,
    SERVICE_PACKET_ACCEPTED,
    SERVICE_PACKET_REJECTED,
    SERVICE_PACKET_FULL,
    SERVICE_PACKET_INVALID
} Service_Packet_Result;

typedef struct {
    Algorithm_Packet_Parser parser;
    uint8_t storage[ALGORITHM_PACKET_PARSER_STORAGE_SIZE];
    Core_Message_Topic *output;
    Service_Packet_Message decoded;
} Service_Packet;

/* Bind an already initialized topic of Service_Packet_Message values.
 * Init/Feed belong to one serialized owner. The service owns no transport;
 * UART/USB/network adapters feed bytes from their Task context. */
bool Service_Packet_Init(Service_Packet *service, Core_Message_Topic *output);
Service_Packet_Result Service_Packet_Feed(
    Service_Packet *service, uint8_t byte, uint32_t now_ms);
Core_Message_Result Service_Packet_Take(
    Core_Message_Subscription *subscription, Service_Packet_Message *message);

#endif
