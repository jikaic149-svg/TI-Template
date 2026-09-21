#include "service_line.h"
#include "service_packet.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Service_Line_Result line_feed(Service_Line *line, const char *text)
{
    Service_Line_Result result = SERVICE_LINE_IN_PROGRESS;
    for (size_t i = 0U; text[i] != '\0'; ++i) {
        result = Service_Line_Feed(line, (uint8_t)text[i]);
    }
    return result;
}

static void test_lines(void)
{
    Service_Line line;
    char storage[6];
    assert(Service_Line_Init(&line, storage, sizeof(storage)));
    assert(line_feed(&line, "LED 1\r\n") == SERVICE_LINE_READY);
    assert(strcmp(storage, "LED 1") == 0);
    assert(line_feed(&line, "LED 10\n") == SERVICE_LINE_REJECTED);
    assert(line_feed(&line, "\n") == SERVICE_LINE_REJECTED);
    assert(line_feed(&line, "LED\r 1\n") == SERVICE_LINE_REJECTED);
    assert(line_feed(&line, "LED\t1\n") == SERVICE_LINE_REJECTED);
    assert(line_feed(&line, "LED 0\n") == SERVICE_LINE_READY);
    assert(strcmp(storage, "LED 0") == 0);
    assert(line_feed(&line, "LE") == SERVICE_LINE_IN_PROGRESS);
    assert(line_feed(&line, "D 1\n") == SERVICE_LINE_READY);
    assert(strcmp(storage, "LED 1") == 0);
    assert(Service_Line_Feed(&line, 0U) == SERVICE_LINE_IN_PROGRESS);
    assert(line_feed(&line, "LED 1\n") == SERVICE_LINE_REJECTED);
}

int main(void)
{
    Core_Message_Topic topic = {0};
    Service_Packet_Message queue[1];
    Core_Message_Subscription sub = {
        .storage = queue, .storage_bytes = sizeof(queue),
        .policy = CORE_MESSAGE_FIFO,
    };
    Service_Packet service;
    Service_Packet_Message output;
    uint8_t wire[128];
    size_t length;
    test_lines();
    assert(Core_Message_Init(&topic, sizeof(Service_Packet_Message), &sub, 1U)
        == CORE_MESSAGE_OK);
    assert(Service_Packet_Init(&service, &topic));
    assert(Algorithm_Packet_Encode("SET", 42U, (const uint8_t *)"100", 3U,
        wire, sizeof(wire), &length) == ALGORITHM_PACKET_ENCODE_OK);
    for (size_t pass = 0U; pass < 2U; ++pass) {
        for (size_t i = 0U; i < length; ++i) {
            assert(Service_Packet_Feed(&service, wire[i], 1234U) ==
                (i + 1U != length ? SERVICE_PACKET_IN_PROGRESS :
                (pass == 0U ? SERVICE_PACKET_ACCEPTED : SERVICE_PACKET_FULL)));
        }
    }
    assert(Service_Packet_Take(&sub, &output) == CORE_MESSAGE_OK);
    assert(output.sequence == 42U && output.timestamp_ms == 1234U);
    assert(output.payload_length == 3U && strcmp(output.type, "SET") == 0);
    assert(memcmp(output.payload, "100", 3U) == 0);
    wire[length - 3U] = wire[length - 3U] == '0' ? '1' : '0';
    for (size_t i = 0U; i < length; ++i) {
        assert(Service_Packet_Feed(&service, wire[i], 0U) ==
            (i + 1U == length ? SERVICE_PACKET_REJECTED : SERVICE_PACKET_IN_PROGRESS));
    }
    assert(Service_Packet_Take(&sub, &output) == CORE_MESSAGE_EMPTY);
    puts("services: simple validation, fragmentation, resync, optional CRC and full queue passed");
    return 0;
}
