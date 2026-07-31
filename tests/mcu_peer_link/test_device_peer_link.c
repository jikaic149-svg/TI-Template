#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_peer_link.h"

#define FAKE_RX_CAPACITY 512U

struct BSP_UART_Resource {
    uint8_t token;
};

static struct BSP_UART_Resource s_uart_resource;
static BSP_UART_Info s_uart_info;
static BSP_UART_RxStatus s_uart_status;
static uint8_t s_rx_bytes[FAKE_RX_CAPACITY];
static size_t s_rx_length;
static uint8_t s_last_tx[ALGORITHM_PACKET_MAX_FRAME_SIZE];
static size_t s_last_tx_length;
static uint32_t s_tx_call_count;
static size_t s_last_read_capacity;
static bool s_tx_busy;

static void fake_reset(void)
{
    memset(&s_uart_status, 0, sizeof(s_uart_status));
    memset(s_rx_bytes, 0, sizeof(s_rx_bytes));
    memset(s_last_tx, 0, sizeof(s_last_tx));
    s_uart_info = (BSP_UART_Info){
        .baud_rate = DEVICE_PEER_LINK_BAUD_RATE,
        .supports_tx_dma = true,
        .supports_rx_dma = true,
        .supports_buffered_tx = true,
        .supports_buffered_rx = true,
        .tx_buffer_capacity = 511U,
        .rx_buffer_capacity = 511U,
    };
    s_uart_status.buffer_capacity = 511U;
    s_rx_length = 0U;
    s_last_tx_length = 0U;
    s_tx_call_count = 0U;
    s_last_read_capacity = 0U;
    s_tx_busy = false;
}

static void fake_queue_rx(const uint8_t *bytes, size_t length)
{
    assert(bytes != NULL);
    assert(length <= (sizeof(s_rx_bytes) - s_rx_length));
    memcpy(&s_rx_bytes[s_rx_length], bytes, length);
    s_rx_length += length;
}

size_t BSP_UART_Count(void)
{
    return 1U;
}

BSP_UART_Handle BSP_UART_GetHandle(size_t index)
{
    return (index == 0U) ? &s_uart_resource : NULL;
}

bool BSP_UART_GetInfo(BSP_UART_Handle uart, BSP_UART_Info *info)
{
    if ((uart != &s_uart_resource) || (info == NULL)) {
        return false;
    }
    *info = s_uart_info;
    return true;
}

BSP_UART_Result BSP_UART_WriteBuffered(
    BSP_UART_Handle uart, const uint8_t *data, size_t length)
{
    if ((uart != &s_uart_resource) || (data == NULL) ||
        (length > sizeof(s_last_tx))) {
        return BSP_UART_RESULT_INVALID_ARGUMENT;
    }
    ++s_tx_call_count;
    if (s_tx_busy) {
        return BSP_UART_RESULT_BUSY;
    }
    memcpy(s_last_tx, data, length);
    s_last_tx_length = length;
    return BSP_UART_RESULT_OK;
}

BSP_UART_Result BSP_UART_ReadAvailable(
    BSP_UART_Handle uart,
    uint8_t *data,
    size_t capacity,
    size_t *transferred)
{
    size_t count;

    if ((uart != &s_uart_resource) || (transferred == NULL) ||
        ((data == NULL) && (capacity != 0U))) {
        return BSP_UART_RESULT_INVALID_ARGUMENT;
    }
    s_last_read_capacity = capacity;
    count = (s_rx_length < capacity) ? s_rx_length : capacity;
    if (count > 0U) {
        memcpy(data, s_rx_bytes, count);
        memmove(s_rx_bytes, &s_rx_bytes[count], s_rx_length - count);
        s_rx_length -= count;
    }
    *transferred = count;
    return BSP_UART_RESULT_OK;
}

bool BSP_UART_GetRxStatus(
    BSP_UART_Handle uart, BSP_UART_RxStatus *status)
{
    if ((uart != &s_uart_resource) || (status == NULL)) {
        return false;
    }
    s_uart_status.buffered_bytes = s_rx_length;
    *status = s_uart_status;
    return true;
}

static Device_PeerLink_Config make_config(void)
{
    return (Device_PeerLink_Config){
        .transport_index = 0U,
        .local_node_id = 1U,
        .remote_node_id = 2U,
        .heartbeat_period_ms = 500U,
        .offline_timeout_ms = 1500U,
        .maximum_rx_bytes_per_step = 128U,
    };
}

static size_t encode_heartbeat(
    uint8_t node_id,
    uint32_t uptime_ms,
    uint16_t sequence,
    uint8_t *frame)
{
    char payload[16];
    int payload_length = snprintf(
        payload, sizeof(payload), "%u,%lu",
        (unsigned)node_id, (unsigned long)uptime_ms);
    size_t frame_length = 0U;

    assert(payload_length > 0);
    assert((size_t)payload_length < sizeof(payload));
    assert(Algorithm_Packet_Encode(
        "HB", sequence, (const uint8_t *)payload, (size_t)payload_length,
        frame, ALGORITHM_PACKET_MAX_FRAME_SIZE, &frame_length) ==
        ALGORITHM_PACKET_ENCODE_OK);
    return frame_length;
}

static size_t encode_car_state(
    uint8_t node_id,
    uint32_t sample_ms,
    int32_t speed_x_mm_per_second,
    int32_t acceleration_x_mm_per_second2,
    int32_t acceleration_z_mm_per_second2,
    int32_t pitch_rate_centi_dps,
    unsigned valid,
    unsigned competition_mode,
    uint16_t sequence,
    uint8_t *frame)
{
    char payload[64];
    int payload_length = snprintf(
        payload, sizeof(payload), "%u,%lu,%ld,%ld,%ld,%ld,%u,%u",
        (unsigned)node_id, (unsigned long)sample_ms,
        (long)speed_x_mm_per_second,
        (long)acceleration_x_mm_per_second2,
        (long)acceleration_z_mm_per_second2,
        (long)pitch_rate_centi_dps, valid, competition_mode);
    size_t frame_length = 0U;

    assert(payload_length > 0);
    assert((size_t)payload_length < sizeof(payload));
    assert(Algorithm_Packet_Encode(
        "CAR", sequence, (const uint8_t *)payload, (size_t)payload_length,
        frame, ALGORITHM_PACKET_MAX_FRAME_SIZE, &frame_length) ==
        ALGORITHM_PACKET_ENCODE_OK);
    return frame_length;
}

static size_t encode_debug_telemetry(
    uint8_t node_id,
    int32_t acceleration,
    int32_t compensation,
    int32_t rod_command,
    uint16_t target_raw,
    uint16_t feedback_raw,
    int32_t command_current,
    int32_t feedback_current,
    int32_t speed,
    unsigned phase,
    unsigned flags,
    uint16_t sequence,
    uint8_t *frame)
{
    char payload[96];
    int payload_length = snprintf(
        payload, sizeof(payload),
        "%u,%ld,%ld,%ld,%u,%u,%ld,%ld,%ld,%u,%u",
        (unsigned)node_id, (long)acceleration, (long)compensation,
        (long)rod_command, (unsigned)target_raw, (unsigned)feedback_raw,
        (long)command_current, (long)feedback_current, (long)speed,
        phase, flags);
    size_t frame_length = 0U;

    assert(payload_length > 0);
    assert((size_t)payload_length < sizeof(payload));
    assert(Algorithm_Packet_Encode(
        "DBG", sequence, (const uint8_t *)payload, (size_t)payload_length,
        frame, ALGORITHM_PACKET_MAX_FRAME_SIZE, &frame_length) ==
        ALGORITHM_PACKET_ENCODE_OK);
    return frame_length;
}

static void test_dma_required_and_first_frame(void)
{
    static const char expected[] = "@1|HB|0|3|1,0|CEC5\n";
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();

    fake_reset();
    s_uart_info.supports_rx_dma = false;
    assert(!Device_PeerLink_Init(&link, &config, 0U));
    s_uart_info.supports_rx_dma = true;
    assert(Device_PeerLink_Init(&link, &config, 0U));
    assert(Device_PeerLink_Step(&link, 0U));
    assert(s_last_tx_length == (sizeof(expected) - 1U));
    assert(memcmp(s_last_tx, expected, sizeof(expected) - 1U) == 0);
    assert(link.status.heartbeat_tx_count == 1U);
}

static void test_receive_duplicate_errors_and_timeout(void)
{
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();
    uint8_t frame[ALGORITHM_PACKET_MAX_FRAME_SIZE];
    size_t length;

    fake_reset();
    assert(Device_PeerLink_Init(&link, &config, 0U));
    length = encode_heartbeat(2U, 10U, 7U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 10U));
    assert(link.status.online);
    assert(link.status.heartbeat_rx_count == 1U);

    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 20U));
    assert(link.status.duplicate_sequence_count == 1U);
    assert(link.status.heartbeat_rx_count == 2U);

    length = encode_heartbeat(1U, 30U, 8U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 30U));
    assert(link.status.heartbeat_source_error_count == 1U);
    assert(link.status.last_heartbeat_rx_ms == 20U);

    length = encode_heartbeat(2U, 40U, 9U, frame);
    frame[length - 2U] = (frame[length - 2U] == '0') ? '1' : '0';
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 40U));
    assert(link.status.packet_parser_error_count == 1U);
    assert(link.status.last_heartbeat_rx_ms == 20U);

    assert(Device_PeerLink_Step(&link, 1519U));
    assert(link.status.online);
    assert(Device_PeerLink_Step(&link, 1520U));
    assert(!link.status.online);
}

static void test_queue_full_does_not_advance_sequence(void)
{
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();
    Algorithm_Packet_Parser parser;
    Algorithm_Packet_View packet;
    uint8_t storage[ALGORITHM_PACKET_PARSER_STORAGE_SIZE];

    fake_reset();
    s_tx_busy = true;
    assert(Device_PeerLink_Init(&link, &config, 0U));
    assert(Device_PeerLink_Step(&link, 0U));
    assert(link.status.heartbeat_tx_drop_count == 1U);
    assert(link.status.heartbeat_tx_count == 0U);
    assert(s_tx_call_count == 1U);
    assert(Device_PeerLink_Step(&link, 499U));
    assert(s_tx_call_count == 1U);

    s_tx_busy = false;
    assert(Device_PeerLink_Step(&link, 500U));
    assert(link.status.heartbeat_tx_count == 1U);
    assert(Algorithm_Packet_Parser_Init(&parser, storage, sizeof(storage)));
    for (size_t index = 0U; index < s_last_tx_length; ++index) {
        Algorithm_Packet_FeedResult result = Algorithm_Packet_Parser_Feed(
            &parser, s_last_tx[index], &packet);

        if (index + 1U == s_last_tx_length) {
            assert(result == ALGORITHM_PACKET_FEED_FRAME_READY);
        }
    }
    assert(packet.sequence == 0U);
}

static void test_wrap_and_per_step_limit(void)
{
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();
    uint8_t frame[ALGORITHM_PACKET_MAX_FRAME_SIZE];
    uint8_t noise[200];
    size_t length;
    uint32_t received_at = UINT32_MAX - 2U;

    fake_reset();
    assert(Device_PeerLink_Init(&link, &config, UINT32_MAX - 4U));
    assert(Device_PeerLink_Step(&link, UINT32_MAX - 4U));
    length = encode_heartbeat(2U, UINT32_MAX, 0U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, received_at));
    assert(link.status.online);
    assert(Device_PeerLink_Step(&link, received_at + 1499U));
    assert(link.status.online);
    assert(Device_PeerLink_Step(&link, received_at + 1500U));
    assert(!link.status.online);

    memset(noise, '@', sizeof(noise));
    fake_queue_rx(noise, sizeof(noise));
    assert(Device_PeerLink_Step(&link, received_at + 1510U));
    assert(s_last_read_capacity == 128U);
    assert(s_rx_length == (sizeof(noise) - 128U));
}

static void test_car_state_send_boundaries_and_busy_sequence(void)
{
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();
    const Device_PeerLink_CarState state = {
        .sample_ms = UINT32_MAX,
        .speed_x_mm_per_second =
            -DEVICE_PEER_LINK_CAR_SPEED_ABS_MAX_MM_PER_SECOND,
        .acceleration_x_mm_per_second2 =
            -DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2,
        .acceleration_z_mm_per_second2 =
            DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2,
        .pitch_rate_centi_dps =
            DEVICE_PEER_LINK_CAR_PITCH_RATE_ABS_MAX_CENTI_DPS,
        .competition_mode = DEVICE_PEER_LINK_COMPETITION_MODE_MAX,
        .competition_task_active = true,
        .valid = true,
    };
    Algorithm_Packet_Parser parser;
    Algorithm_Packet_View packet;
    uint8_t storage[ALGORITHM_PACKET_PARSER_STORAGE_SIZE];

    fake_reset();
    config.local_node_id = 2U;
    config.remote_node_id = 1U;
    assert(Device_PeerLink_Init(&link, &config, 0U));
    assert(Device_PeerLink_SendCarState(&link, &state));
    assert(link.status.car_state_tx_count == 1U);
    assert(Algorithm_Packet_Parser_Init(&parser, storage, sizeof(storage)));
    for (size_t index = 0U; index < s_last_tx_length; ++index) {
        const Algorithm_Packet_FeedResult result =
            Algorithm_Packet_Parser_Feed(
                &parser, s_last_tx[index], &packet);

        if (index + 1U == s_last_tx_length) {
            assert(result == ALGORITHM_PACKET_FEED_FRAME_READY);
        }
    }
    assert(packet.type_length == 3U);
    assert(memcmp(packet.type, "CAR", 3U) == 0);
    assert(packet.sequence == 0U);
    assert(packet.payload_length ==
        sizeof("2,4294967295,-5000,-50000,50000,100000,1,6,1") - 1U);
    assert(memcmp(packet.payload,
        "2,4294967295,-5000,-50000,50000,100000,1,6,1",
        packet.payload_length) == 0);

    s_tx_busy = true;
    assert(Device_PeerLink_SendCarState(&link, &state));
    assert(link.status.car_state_tx_drop_count == 1U);
    assert(link.status.car_state_tx_count == 1U);
    s_tx_busy = false;
    assert(Device_PeerLink_SendCarState(&link, &state));
    assert(link.status.car_state_tx_count == 2U);
    Algorithm_Packet_Parser_Reset(&parser);
    for (size_t index = 0U; index < s_last_tx_length; ++index) {
        (void)Algorithm_Packet_Parser_Feed(
            &parser, s_last_tx[index], &packet);
    }
    assert(packet.sequence == 1U);

    {
        Device_PeerLink_CarState planned_state = state;

        planned_state.planned_acceleration_x_mm_per_second2 = 100;
        planned_state.planned_acceleration_valid = true;
        assert(Device_PeerLink_SendCarState(&link, &planned_state));
        Algorithm_Packet_Parser_Reset(&parser);
        for (size_t index = 0U; index < s_last_tx_length; ++index) {
            (void)Algorithm_Packet_Parser_Feed(
                &parser, s_last_tx[index], &packet);
        }
        assert(packet.sequence == 2U);
        assert(packet.payload_length ==
            sizeof("2,4294967295,-5000,-50000,50000,100000,1,6,1,100,1") -
                1U);
        assert(memcmp(packet.payload,
            "2,4294967295,-5000,-50000,50000,100000,1,6,1,100,1",
            packet.payload_length) == 0);

        planned_state.filtered_wheel_acceleration_x_mm_per_second2 = -80;
        planned_state.filtered_wheel_acceleration_valid = true;
        assert(Device_PeerLink_SendCarState(&link, &planned_state));
        Algorithm_Packet_Parser_Reset(&parser);
        for (size_t index = 0U; index < s_last_tx_length; ++index) {
            (void)Algorithm_Packet_Parser_Feed(
                &parser, s_last_tx[index], &packet);
        }
        assert(packet.sequence == 3U);
        assert(packet.payload_length == sizeof(
            "2,4294967295,-5000,-50000,50000,100000,1,6,1,100,1,-80,1") -
            1U);
        assert(memcmp(packet.payload,
            "2,4294967295,-5000,-50000,50000,100000,1,6,1,100,1,-80,1",
            packet.payload_length) == 0);
    }

    assert(!Device_PeerLink_SendCarState(
        &link, &(Device_PeerLink_CarState){
            .speed_x_mm_per_second =
                DEVICE_PEER_LINK_CAR_SPEED_ABS_MAX_MM_PER_SECOND + 1,
            .competition_mode = DEVICE_PEER_LINK_COMPETITION_MODE_MIN,
            .valid = true,
        }));
    assert(!Device_PeerLink_SendCarState(
        &link, &(Device_PeerLink_CarState){
            .speed_x_mm_per_second = 1,
            .competition_mode = DEVICE_PEER_LINK_COMPETITION_MODE_MIN,
            .valid = false,
        }));
    assert(!Device_PeerLink_SendCarState(
        &link, &(Device_PeerLink_CarState){
            .competition_mode = DEVICE_PEER_LINK_COMPETITION_MODE_MIN,
            .filtered_wheel_acceleration_x_mm_per_second2 = 1,
            .filtered_wheel_acceleration_valid = true,
        }));
    assert(link.status.car_state_tx_error_count == 3U);
}

static void test_car_state_receive_validation_and_age(void)
{
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();
    Device_PeerLink_CarState state;
    uint8_t frame[ALGORITHM_PACKET_MAX_FRAME_SIZE];
    size_t length;

    fake_reset();
    assert(Device_PeerLink_Init(&link, &config, 0U));
    length = encode_car_state(
        2U, UINT32_MAX, -5000, -50000, 50000, 100000, 1U, 6U, 9U,
        frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 100U));
    assert(link.status.car_state_rx_count == 1U);
    assert(Device_PeerLink_GetRemoteCarState(&link, 149U, 50U, &state));
    assert(Device_PeerLink_GetRemoteCarState(&link, 150U, 50U, &state));
    assert(!Device_PeerLink_GetRemoteCarState(&link, 151U, 50U, &state));
    assert(state.sample_ms == UINT32_MAX);
    assert(state.speed_x_mm_per_second == -5000);
    assert(state.acceleration_x_mm_per_second2 == -50000);
    assert(state.acceleration_z_mm_per_second2 == 50000);
    assert(state.pitch_rate_centi_dps == 100000);
    assert(state.competition_mode == 6U);
    assert(state.valid);

    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 110U));
    assert(link.status.car_state_duplicate_sequence_count == 1U);

    length = encode_car_state(
        1U, 120U, 0, 0, 0, 0, 0U, 2U, 10U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 120U));
    assert(link.status.car_state_source_error_count == 1U);

    length = encode_car_state(
        2U, 0U, 0, 0, 0, 0, 0U, 3U, 10U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 130U));
    assert(!Device_PeerLink_GetRemoteCarState(&link, 130U, 50U, &state));

    {
        static const uint8_t invalid_payload[] = "2,140,1,0,0,0,0";
        size_t frame_length = 0U;

        assert(Algorithm_Packet_Encode(
            "CAR", 11U, invalid_payload, sizeof(invalid_payload) - 1U,
            frame, sizeof(frame), &frame_length) ==
            ALGORITHM_PACKET_ENCODE_OK);
        fake_queue_rx(frame, frame_length);
        assert(Device_PeerLink_Step(&link, 140U));
        assert(link.status.car_state_payload_error_count == 1U);
    }

    length = encode_car_state(
        2U, UINT32_MAX, 1, 2, 3, 4, 1U, 4U, 12U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 150U));
    assert(link.status.car_state_sample_time_error_count == 1U);
    assert(!Device_PeerLink_GetRemoteCarState(&link, 150U, 50U, &state));

    length = encode_heartbeat(2U, 100U, 1U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 160U));
    length = encode_car_state(
        2U, 200U, 1, 2, 3, 4, 1U, 5U, 12U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 161U));
    assert(Device_PeerLink_GetRemoteCarState(&link, 161U, 50U, &state));
    assert(state.sample_ms == 200U);
    assert(state.competition_mode == 5U);

    length = encode_heartbeat(2U, 10U, 2U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 170U));
    assert(!Device_PeerLink_GetRemoteCarState(&link, 170U, 50U, &state));
    length = encode_car_state(
        2U, 0U, 0, 0, 0, 0, 0U, 2U, 0U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 171U));
    assert(!Device_PeerLink_GetRemoteCarState(&link, 171U, 50U, &state));

    {
        static const uint8_t planned_payload[] =
            "2,1,0,0,0,0,0,4,100,1";
        size_t frame_length = 0U;

        assert(Algorithm_Packet_Encode(
            "CAR", 1U, planned_payload, sizeof(planned_payload) - 1U,
            frame, sizeof(frame), &frame_length) ==
            ALGORITHM_PACKET_ENCODE_OK);
        fake_queue_rx(frame, frame_length);
        assert(Device_PeerLink_Step(&link, 171U));
        assert(Device_PeerLink_GetRemoteCarState(
            &link, 171U, 50U, &state));
        assert(!state.valid);
        assert(state.planned_acceleration_valid);
        assert(state.planned_acceleration_x_mm_per_second2 == 100);
    }

    {
        static const uint8_t wheel_payload[] =
            "2,2,0,0,0,0,0,6,0,-80,1,-75,1";
        size_t frame_length = 0U;

        assert(Algorithm_Packet_Encode(
            "CAR", 2U, wheel_payload, sizeof(wheel_payload) - 1U,
            frame, sizeof(frame), &frame_length) ==
            ALGORITHM_PACKET_ENCODE_OK);
        fake_queue_rx(frame, frame_length);
        assert(Device_PeerLink_Step(&link, 172U));
        assert(Device_PeerLink_GetRemoteCarState(
            &link, 172U, 50U, &state));
        assert(state.planned_acceleration_valid);
        assert(state.filtered_wheel_acceleration_valid);
        assert(state.filtered_wheel_acceleration_x_mm_per_second2 == -75);
    }

    length = encode_car_state(
        2U, 1U, 0, 0, 0, 0, 2U, 2U, 1U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 172U));
    length = encode_car_state(
        2U, 1U, 0,
        DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2 + 1,
        0, 0, 1U, 2U, 1U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 173U));
    length = encode_car_state(
        2U, 2U, 0, 0, 0, 0, 0U, 1U, 2U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 174U));
    length = encode_car_state(
        2U, 2U, 0, 0, 0, 0, 0U, 7U, 2U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 175U));
    assert(link.status.car_state_payload_error_count == 5U);

    fake_reset();
    assert(Device_PeerLink_Init(&link, &config, UINT32_MAX - 20U));
    length = encode_car_state(
        2U, 5U, 0, 0, 9807, 0, 1U, 6U, 0U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, UINT32_MAX - 20U));
    assert(Device_PeerLink_GetRemoteCarState(&link, 29U, 50U, &state));
    assert(!Device_PeerLink_GetRemoteCarState(&link, 30U, 50U, &state));
}

static void test_debug_receive_validation_order_and_age(void)
{
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();
    Device_PeerLink_DebugTelemetry telemetry;
    uint8_t frame[ALGORITHM_PACKET_MAX_FRAME_SIZE];
    size_t length;

    config.local_node_id = 2U;
    config.remote_node_id = 1U;
    fake_reset();
    assert(Device_PeerLink_Init(&link, &config, 0U));
    length = encode_debug_telemetry(
        1U, -60000, 60000, -60000, 0U, UINT16_MAX,
        -300, 10000, -100000, 2U,
        DEVICE_PEER_LINK_DEBUG_STATUS_MASK, 9U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 100U));
    assert(link.status.debug_rx_count == 1U);
    assert(Device_PeerLink_GetRemoteDebugTelemetry(
        &link, 200U, 100U, &telemetry));
    assert(!Device_PeerLink_GetRemoteDebugTelemetry(
        &link, 201U, 100U, &telemetry));
    assert(telemetry.chassis_acceleration_mm_s2 == -60000);
    assert(telemetry.chassis_compensation_mdeg == 60000);
    assert(telemetry.rod_command_mdeg == -60000);
    assert(telemetry.motor_target_raw == 0U);
    assert(telemetry.motor_feedback_raw == UINT16_MAX);
    assert(telemetry.motor_command_current_ma == -300);
    assert(telemetry.motor_feedback_current_ma == 10000);
    assert(telemetry.motor_speed_centi_rpm == -100000);
    assert(telemetry.control_phase == 2U);
    assert(telemetry.status_flags == DEVICE_PEER_LINK_DEBUG_STATUS_MASK);

    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 110U));
    assert(link.status.debug_duplicate_sequence_count == 1U);
    length = encode_debug_telemetry(
        1U, 0, 0, 0, 0U, 0U, 0, 0, 0, 0U, 0U, 8U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 120U));
    assert(link.status.debug_old_sequence_count == 1U);
    length = encode_debug_telemetry(
        2U, 0, 0, 0, 0U, 0U, 0, 0, 0, 0U, 0U, 10U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 130U));
    assert(link.status.debug_source_error_count == 1U);
    length = encode_debug_telemetry(
        1U, 0, 0, 0, 0U, 0U, 0, 0, 0, 3U, 0U, 10U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 140U));
    assert(link.status.debug_payload_error_count == 1U);

    length = encode_heartbeat(1U, 100U, 1U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 150U));
    length = encode_heartbeat(1U, 10U, 2U, frame);
    fake_queue_rx(frame, length);
    assert(Device_PeerLink_Step(&link, 160U));
    assert(!Device_PeerLink_GetRemoteDebugTelemetry(
        &link, 160U, 100U, &telemetry));
}

static void test_ball_command_send_boundaries_busy_and_sequences(void)
{
    Device_PeerLink link;
    Device_PeerLink_Config config = make_config();
    Algorithm_Packet_Parser parser;
    Algorithm_Packet_View packet;
    uint8_t storage[ALGORITHM_PACKET_PARSER_STORAGE_SIZE];

    config.local_node_id = 2U;
    config.remote_node_id = 1U;
    fake_reset();
    assert(Device_PeerLink_Init(&link, &config, 0U));
    assert(Algorithm_Packet_Parser_Init(&parser, storage, sizeof(storage)));

    assert(Device_PeerLink_SendBallTarget(
        &link, DEVICE_PEER_LINK_BALL_TARGET_ABS_MAX_0P1_MM) ==
        DEVICE_PEER_LINK_SEND_QUEUED);
    for (size_t index = 0U; index < s_last_tx_length; ++index) {
        (void)Algorithm_Packet_Parser_Feed(
            &parser, s_last_tx[index], &packet);
    }
    assert(packet.type_length == 3U);
    assert(memcmp(packet.type, "BTG", 3U) == 0);
    assert(packet.sequence == 0U);
    assert(packet.payload_length == sizeof("2,1250") - 1U);
    assert(memcmp(packet.payload, "2,1250", packet.payload_length) == 0);
    assert(link.status.ball_target_tx_count == 1U);

    assert(Device_PeerLink_SendBallStop(&link) ==
        DEVICE_PEER_LINK_SEND_QUEUED);
    Algorithm_Packet_Parser_Reset(&parser);
    for (size_t index = 0U; index < s_last_tx_length; ++index) {
        (void)Algorithm_Packet_Parser_Feed(
            &parser, s_last_tx[index], &packet);
    }
    assert(packet.type_length == 3U);
    assert(memcmp(packet.type, "BST", 3U) == 0);
    assert(packet.sequence == 0U);
    assert(packet.payload_length == sizeof("2") - 1U);
    assert(memcmp(packet.payload, "2", packet.payload_length) == 0);
    assert(link.status.ball_stop_tx_count == 1U);

    s_tx_busy = true;
    assert(Device_PeerLink_SendBallTarget(
        &link, -DEVICE_PEER_LINK_BALL_TARGET_ABS_MAX_0P1_MM) ==
        DEVICE_PEER_LINK_SEND_BUSY);
    assert(Device_PeerLink_SendBallStop(&link) ==
        DEVICE_PEER_LINK_SEND_BUSY);
    assert(link.status.ball_target_tx_busy_count == 1U);
    assert(link.status.ball_stop_tx_busy_count == 1U);
    s_tx_busy = false;

    assert(Device_PeerLink_SendBallTarget(
        &link, -DEVICE_PEER_LINK_BALL_TARGET_ABS_MAX_0P1_MM) ==
        DEVICE_PEER_LINK_SEND_QUEUED);
    Algorithm_Packet_Parser_Reset(&parser);
    for (size_t index = 0U; index < s_last_tx_length; ++index) {
        (void)Algorithm_Packet_Parser_Feed(
            &parser, s_last_tx[index], &packet);
    }
    assert(memcmp(packet.type, "BTG", 3U) == 0);
    assert(packet.sequence == 1U);
    assert(packet.payload_length == sizeof("2,-1250") - 1U);
    assert(memcmp(packet.payload, "2,-1250", packet.payload_length) == 0);

    assert(Device_PeerLink_SendBallStop(&link) ==
        DEVICE_PEER_LINK_SEND_QUEUED);
    Algorithm_Packet_Parser_Reset(&parser);
    for (size_t index = 0U; index < s_last_tx_length; ++index) {
        (void)Algorithm_Packet_Parser_Feed(
            &parser, s_last_tx[index], &packet);
    }
    assert(memcmp(packet.type, "BST", 3U) == 0);
    assert(packet.sequence == 1U);

    assert(Device_PeerLink_SendBallTarget(
        &link, DEVICE_PEER_LINK_BALL_TARGET_ABS_MAX_0P1_MM + 1) ==
        DEVICE_PEER_LINK_SEND_INVALID_ARGUMENT);
    assert(Device_PeerLink_SendBallTarget(
        &link, -DEVICE_PEER_LINK_BALL_TARGET_ABS_MAX_0P1_MM - 1) ==
        DEVICE_PEER_LINK_SEND_INVALID_ARGUMENT);
    assert(link.status.ball_target_tx_error_count == 2U);
}

int main(void)
{
    test_dma_required_and_first_frame();
    test_receive_duplicate_errors_and_timeout();
    test_queue_full_does_not_advance_sequence();
    test_wrap_and_per_step_limit();
    test_car_state_send_boundaries_and_busy_sequence();
    test_car_state_receive_validation_and_age();
    test_debug_receive_validation_order_and_age();
    test_ball_command_send_boundaries_busy_and_sequences();
    puts("device_peer_link tests: PASS");
    return 0;
}
