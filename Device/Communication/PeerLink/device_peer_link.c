#include "device_peer_link.h"

#include <limits.h>
#include <string.h>

#define DEVICE_PEER_LINK_CAR_STATE_LEGACY_FIELD_COUNT 8U
#define DEVICE_PEER_LINK_CAR_STATE_PLANNED_FIELD_COUNT 10U
#define DEVICE_PEER_LINK_CAR_STATE_TASK_FIELD_COUNT 9U
#define DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_FIELD_COUNT 11U
#define DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_WHEEL_FIELD_COUNT 13U
#define DEVICE_PEER_LINK_CAR_STATE_FIELD_CAPACITY \
    DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_WHEEL_FIELD_COUNT
#define DEVICE_PEER_LINK_DEBUG_FIELD_COUNT 11U
#define DEVICE_PEER_LINK_F407_NODE_ID 1U
#define DEVICE_PEER_LINK_G3519_NODE_ID 2U

static void increment_saturating(uint32_t *value)
{
    if (*value < UINT32_MAX) {
        ++*value;
    }
}

static bool time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool serial_u32_newer(uint32_t value, uint32_t previous)
{
    const uint32_t delta = value - previous;

    return (delta != 0U) && (delta <= (uint32_t)INT32_MAX);
}

static bool serial_u16_newer(uint16_t value, uint16_t previous)
{
    const uint16_t delta = (uint16_t)(value - previous);

    return (delta != 0U) && (delta < UINT16_C(0x8000));
}

static bool node_id_valid(uint8_t node_id)
{
    return (node_id == 1U) || (node_id == 2U);
}

static size_t format_u32(uint8_t *output, size_t capacity, uint32_t value)
{
    uint8_t reverse[10];
    size_t count = 0U;

    do {
        reverse[count++] = (uint8_t)('0' + (value % 10U));
        value /= 10U;
    } while (value > 0U);
    if (count > capacity) {
        return 0U;
    }
    for (size_t index = 0U; index < count; ++index) {
        output[index] = reverse[count - index - 1U];
    }
    return count;
}

static size_t format_i32(uint8_t *output, size_t capacity, int32_t value)
{
    uint32_t magnitude;
    size_t length = 0U;

    if (value < 0) {
        if (capacity == 0U) {
            return 0U;
        }
        output[length++] = '-';
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    }
    else {
        magnitude = (uint32_t)value;
    }
    {
        const size_t digits = format_u32(
            &output[length], capacity - length, magnitude);

        return (digits == 0U) ? 0U : (length + digits);
    }
}

static bool parse_u32_canonical(
    const uint8_t *text, size_t length, uint32_t *value)
{
    uint32_t result = 0U;

    if ((text == NULL) || (value == NULL) || (length == 0U) ||
        (length > 10U) || ((length > 1U) && (text[0] == '0'))) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        uint32_t digit;

        if ((text[index] < '0') || (text[index] > '9')) {
            return false;
        }
        digit = (uint32_t)(text[index] - '0');
        if (result > ((UINT32_MAX - digit) / 10U)) {
            return false;
        }
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

static bool parse_i32_canonical(
    const uint8_t *text, size_t length, int32_t *value)
{
    uint32_t magnitude;

    if ((text == NULL) || (value == NULL) || (length == 0U)) {
        return false;
    }
    if (text[0] != '-') {
        if (!parse_u32_canonical(text, length, &magnitude) ||
            (magnitude > (uint32_t)INT32_MAX)) {
            return false;
        }
        *value = (int32_t)magnitude;
        return true;
    }
    if ((length <= 1U) ||
        !parse_u32_canonical(&text[1], length - 1U, &magnitude) ||
        (magnitude == 0U) ||
        (magnitude > ((uint32_t)INT32_MAX + 1U))) {
        return false;
    }
    *value = (magnitude == ((uint32_t)INT32_MAX + 1U))
        ? INT32_MIN : -(int32_t)magnitude;
    return true;
}

static bool absolute_i32_within(int32_t value, int32_t maximum)
{
    return (value >= -maximum) && (value <= maximum);
}

static bool car_state_values_valid(const Device_PeerLink_CarState *state)
{
    if ((state == NULL) ||
        !absolute_i32_within(state->speed_x_mm_per_second,
            DEVICE_PEER_LINK_CAR_SPEED_ABS_MAX_MM_PER_SECOND) ||
        !absolute_i32_within(state->acceleration_x_mm_per_second2,
            DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2) ||
        !absolute_i32_within(state->acceleration_z_mm_per_second2,
            DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2) ||
        !absolute_i32_within(state->pitch_rate_centi_dps,
            DEVICE_PEER_LINK_CAR_PITCH_RATE_ABS_MAX_CENTI_DPS) ||
        !absolute_i32_within(state->planned_acceleration_x_mm_per_second2,
            DEVICE_PEER_LINK_CAR_PLAN_ACCEL_ABS_MAX_MM_PER_SECOND2) ||
        !absolute_i32_within(
            state->filtered_wheel_acceleration_x_mm_per_second2,
            DEVICE_PEER_LINK_CAR_WHEEL_ACCEL_ABS_MAX_MM_PER_SECOND2) ||
        (state->competition_mode < DEVICE_PEER_LINK_COMPETITION_MODE_MIN) ||
        (state->competition_mode > DEVICE_PEER_LINK_COMPETITION_MODE_MAX)) {
        return false;
    }
    if (!state->planned_acceleration_valid &&
        (state->planned_acceleration_x_mm_per_second2 != 0)) {
        return false;
    }
    if ((!state->filtered_wheel_acceleration_valid &&
         (state->filtered_wheel_acceleration_x_mm_per_second2 != 0)) ||
        (state->filtered_wheel_acceleration_valid &&
         !state->planned_acceleration_valid)) {
        return false;
    }
    return state->valid ||
        ((state->speed_x_mm_per_second == 0) &&
         (state->acceleration_x_mm_per_second2 == 0) &&
         (state->acceleration_z_mm_per_second2 == 0) &&
         (state->pitch_rate_centi_dps == 0));
}

static bool append_separator(
    uint8_t *payload, size_t capacity, size_t *length)
{
    if ((*length) >= capacity) {
        return false;
    }
    payload[(*length)++] = ',';
    return true;
}

static bool append_u32(
    uint8_t *payload, size_t capacity, size_t *length, uint32_t value)
{
    const size_t count = format_u32(
        &payload[*length], capacity - *length, value);

    if (count == 0U) {
        return false;
    }
    *length += count;
    return true;
}

static bool append_i32(
    uint8_t *payload, size_t capacity, size_t *length, int32_t value)
{
    const size_t count = format_i32(
        &payload[*length], capacity - *length, value);

    if (count == 0U) {
        return false;
    }
    *length += count;
    return true;
}

static bool is_heartbeat(const Algorithm_Packet_View *packet)
{
    return (packet->type_length == 2U) &&
           (memcmp(packet->type, "HB", 2U) == 0);
}

static bool is_car_state(const Algorithm_Packet_View *packet)
{
    return (packet->type_length == 3U) &&
           (memcmp(packet->type, "CAR", 3U) == 0);
}

static bool is_debug_telemetry(const Algorithm_Packet_View *packet)
{
    return (packet->type_length == 3U) &&
           (memcmp(packet->type, "DBG", 3U) == 0);
}

static void consume_heartbeat(
    Device_PeerLink *link,
    const Algorithm_Packet_View *packet,
    uint32_t now_ms)
{
    size_t comma = 0U;
    uint32_t node_id;
    uint32_t remote_uptime_ms;

    while ((comma < packet->payload_length) &&
           (packet->payload[comma] != ',')) {
        ++comma;
    }
    if ((comma == 0U) || ((comma + 1U) >= packet->payload_length) ||
        !parse_u32_canonical(packet->payload, comma, &node_id) ||
        !parse_u32_canonical(
            &packet->payload[comma + 1U],
            packet->payload_length - comma - 1U,
            &remote_uptime_ms)) {
        increment_saturating(&link->status.heartbeat_payload_error_count);
        return;
    }
    if (node_id != link->config.remote_node_id) {
        increment_saturating(&link->status.heartbeat_source_error_count);
        return;
    }

    if (link->heartbeat_rx_sequence_seen &&
        (packet->sequence == link->last_heartbeat_rx_sequence)) {
        increment_saturating(&link->status.duplicate_sequence_count);
    }
    else {
        link->last_heartbeat_rx_sequence = packet->sequence;
        link->heartbeat_rx_sequence_seen = true;
    }

    if ((link->status.heartbeat_rx_count > 0U) &&
        (remote_uptime_ms != link->status.last_remote_uptime_ms) &&
        !serial_u32_newer(
            remote_uptime_ms, link->status.last_remote_uptime_ms)) {
        /* A remote reboot starts a new CAR sequence/sample-time session. The
         * last sample must not remain fresh across that restart. */
        memset(&link->remote_car_state, 0, sizeof(link->remote_car_state));
        link->car_state_rx_sequence_seen = false;
        link->car_state_sample_time_seen = false;
        link->remote_car_state_seen = false;
        memset(&link->remote_debug_telemetry, 0,
            sizeof(link->remote_debug_telemetry));
        link->debug_rx_sequence_seen = false;
        link->remote_debug_telemetry_seen = false;
    }

    link->status.last_heartbeat_rx_ms = now_ms;
    link->status.last_remote_uptime_ms = remote_uptime_ms;
    link->status.online = true;
    increment_saturating(&link->status.heartbeat_rx_count);
}

static void consume_car_state(
    Device_PeerLink *link,
    const Algorithm_Packet_View *packet,
    uint32_t now_ms)
{
    size_t start[DEVICE_PEER_LINK_CAR_STATE_FIELD_CAPACITY];
    size_t length[DEVICE_PEER_LINK_CAR_STATE_FIELD_CAPACITY];
    size_t field_count = 0U;
    size_t field_start = 0U;
    uint32_t node_id;
    uint32_t sample_ms;
    int32_t speed_x_mm_per_second;
    int32_t acceleration_x_mm_per_second2;
    int32_t acceleration_z_mm_per_second2;
    int32_t pitch_rate_centi_dps;
    uint32_t valid;
    uint32_t competition_mode;
    uint32_t competition_task_active = 0U;
    int32_t planned_acceleration = 0;
    uint32_t planned_acceleration_valid = 0U;
    int32_t filtered_wheel_acceleration = 0;
    uint32_t filtered_wheel_acceleration_valid = 0U;
    size_t planned_field_start =
        DEVICE_PEER_LINK_CAR_STATE_LEGACY_FIELD_COUNT;

    for (size_t index = 0U; index <= packet->payload_length; ++index) {
        if ((index == packet->payload_length) ||
            (packet->payload[index] == ',')) {
            if ((field_count >= DEVICE_PEER_LINK_CAR_STATE_FIELD_CAPACITY) ||
                (index == field_start)) {
                increment_saturating(
                    &link->status.car_state_payload_error_count);
                return;
            }
            start[field_count] = field_start;
            length[field_count] = index - field_start;
            ++field_count;
            field_start = index + 1U;
        }
    }
    if (((field_count != DEVICE_PEER_LINK_CAR_STATE_LEGACY_FIELD_COUNT) &&
         (field_count != DEVICE_PEER_LINK_CAR_STATE_PLANNED_FIELD_COUNT) &&
         (field_count != DEVICE_PEER_LINK_CAR_STATE_TASK_FIELD_COUNT) &&
         (field_count !=
            DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_FIELD_COUNT) &&
         (field_count !=
            DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_WHEEL_FIELD_COUNT)) ||
        !parse_u32_canonical(
            &packet->payload[start[0]], length[0], &node_id)) {
        increment_saturating(&link->status.car_state_payload_error_count);
        return;
    }
    if ((field_count == DEVICE_PEER_LINK_CAR_STATE_TASK_FIELD_COUNT) ||
        (field_count == DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_FIELD_COUNT) ||
        (field_count ==
            DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_WHEEL_FIELD_COUNT)) {
        if (!parse_u32_canonical(
                &packet->payload[start[8]], length[8],
                &competition_task_active) ||
            (competition_task_active > 1U)) {
            increment_saturating(&link->status.car_state_payload_error_count);
            return;
        }
        planned_field_start = DEVICE_PEER_LINK_CAR_STATE_TASK_FIELD_COUNT;
    }
    if (((field_count == DEVICE_PEER_LINK_CAR_STATE_PLANNED_FIELD_COUNT) ||
         (field_count == DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_FIELD_COUNT) ||
         (field_count ==
            DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_WHEEL_FIELD_COUNT)) &&
        (!parse_i32_canonical(
            &packet->payload[start[planned_field_start]],
            length[planned_field_start],
            &planned_acceleration) ||
         !parse_u32_canonical(
            &packet->payload[start[planned_field_start + 1U]],
            length[planned_field_start + 1U],
            &planned_acceleration_valid) ||
         (planned_acceleration_valid > 1U))) {
        increment_saturating(&link->status.car_state_payload_error_count);
        return;
    }
    if ((field_count ==
            DEVICE_PEER_LINK_CAR_STATE_TASK_PLANNED_WHEEL_FIELD_COUNT) &&
        (!parse_i32_canonical(
            &packet->payload[start[11]], length[11],
            &filtered_wheel_acceleration) ||
         !parse_u32_canonical(
            &packet->payload[start[12]], length[12],
            &filtered_wheel_acceleration_valid) ||
         (filtered_wheel_acceleration_valid > 1U))) {
        increment_saturating(&link->status.car_state_payload_error_count);
        return;
    }
    if ((link->config.local_node_id != DEVICE_PEER_LINK_F407_NODE_ID) ||
        (node_id != DEVICE_PEER_LINK_G3519_NODE_ID) ||
        (node_id != link->config.remote_node_id)) {
        increment_saturating(&link->status.car_state_source_error_count);
        return;
    }
    if (!parse_u32_canonical(
            &packet->payload[start[1]], length[1], &sample_ms) ||
        !parse_i32_canonical(
            &packet->payload[start[2]], length[2],
            &speed_x_mm_per_second) ||
        !parse_i32_canonical(
            &packet->payload[start[3]], length[3],
            &acceleration_x_mm_per_second2) ||
        !parse_i32_canonical(
            &packet->payload[start[4]], length[4],
            &acceleration_z_mm_per_second2) ||
        !parse_i32_canonical(
            &packet->payload[start[5]], length[5],
            &pitch_rate_centi_dps) ||
        !parse_u32_canonical(
            &packet->payload[start[6]], length[6], &valid) ||
        !parse_u32_canonical(
            &packet->payload[start[7]], length[7], &competition_mode) ||
        (valid > 1U) ||
        (competition_mode < DEVICE_PEER_LINK_COMPETITION_MODE_MIN) ||
        (competition_mode > DEVICE_PEER_LINK_COMPETITION_MODE_MAX)) {
        increment_saturating(&link->status.car_state_payload_error_count);
        return;
    }
    {
        const Device_PeerLink_CarState state = {
            .sample_ms = sample_ms,
            .speed_x_mm_per_second = speed_x_mm_per_second,
            .acceleration_x_mm_per_second2 =
                acceleration_x_mm_per_second2,
            .acceleration_z_mm_per_second2 =
                acceleration_z_mm_per_second2,
            .pitch_rate_centi_dps = pitch_rate_centi_dps,
            .planned_acceleration_x_mm_per_second2 =
                planned_acceleration,
            .filtered_wheel_acceleration_x_mm_per_second2 =
                filtered_wheel_acceleration,
            .competition_mode = (uint8_t)competition_mode,
            .competition_task_active = competition_task_active != 0U,
            .valid = valid != 0U,
            .planned_acceleration_valid =
                planned_acceleration_valid != 0U,
            .filtered_wheel_acceleration_valid =
                filtered_wheel_acceleration_valid != 0U,
            .received_at_ms = now_ms,
        };

        if (!car_state_values_valid(&state)) {
            increment_saturating(
                &link->status.car_state_payload_error_count);
            return;
        }
        if (link->car_state_rx_sequence_seen &&
            (packet->sequence == link->last_car_state_rx_sequence)) {
            increment_saturating(
                &link->status.car_state_duplicate_sequence_count);
            return;
        }
        if (link->car_state_sample_time_seen &&
            !serial_u32_newer(
                state.sample_ms, link->last_car_state_sample_ms)) {
            increment_saturating(
                &link->status.car_state_sample_time_error_count);
            return;
        }
        link->last_car_state_rx_sequence = packet->sequence;
        link->last_car_state_sample_ms = state.sample_ms;
        link->car_state_rx_sequence_seen = true;
        link->car_state_sample_time_seen = true;
        link->remote_car_state_seen = true;
        link->remote_car_state = state;
    }
    increment_saturating(&link->status.car_state_rx_count);
}

static void consume_debug_telemetry(
    Device_PeerLink *link,
    const Algorithm_Packet_View *packet,
    uint32_t now_ms)
{
    size_t start[DEVICE_PEER_LINK_DEBUG_FIELD_COUNT];
    size_t length[DEVICE_PEER_LINK_DEBUG_FIELD_COUNT];
    size_t field_count = 0U;
    size_t field_start = 0U;
    uint32_t node_id;
    int32_t acceleration;
    int32_t compensation;
    int32_t rod_command;
    uint32_t target_raw;
    uint32_t feedback_raw;
    int32_t command_current;
    int32_t feedback_current;
    int32_t speed;
    uint32_t phase;
    uint32_t flags;

    for (size_t index = 0U; index <= packet->payload_length; ++index) {
        if ((index == packet->payload_length) ||
            (packet->payload[index] == ',')) {
            if ((field_count >= DEVICE_PEER_LINK_DEBUG_FIELD_COUNT) ||
                (index == field_start)) {
                increment_saturating(
                    &link->status.debug_payload_error_count);
                return;
            }
            start[field_count] = field_start;
            length[field_count] = index - field_start;
            ++field_count;
            field_start = index + 1U;
        }
    }
    if ((field_count != DEVICE_PEER_LINK_DEBUG_FIELD_COUNT) ||
        !parse_u32_canonical(
            &packet->payload[start[0]], length[0], &node_id)) {
        increment_saturating(&link->status.debug_payload_error_count);
        return;
    }
    if ((link->config.local_node_id != DEVICE_PEER_LINK_G3519_NODE_ID) ||
        (node_id != DEVICE_PEER_LINK_F407_NODE_ID) ||
        (node_id != link->config.remote_node_id)) {
        increment_saturating(&link->status.debug_source_error_count);
        return;
    }
    if (!parse_i32_canonical(
            &packet->payload[start[1]], length[1], &acceleration) ||
        !parse_i32_canonical(
            &packet->payload[start[2]], length[2], &compensation) ||
        !parse_i32_canonical(
            &packet->payload[start[3]], length[3], &rod_command) ||
        !parse_u32_canonical(
            &packet->payload[start[4]], length[4], &target_raw) ||
        !parse_u32_canonical(
            &packet->payload[start[5]], length[5], &feedback_raw) ||
        !parse_i32_canonical(
            &packet->payload[start[6]], length[6], &command_current) ||
        !parse_i32_canonical(
            &packet->payload[start[7]], length[7], &feedback_current) ||
        !parse_i32_canonical(
            &packet->payload[start[8]], length[8], &speed) ||
        !parse_u32_canonical(
            &packet->payload[start[9]], length[9], &phase) ||
        !parse_u32_canonical(
            &packet->payload[start[10]], length[10], &flags) ||
        !absolute_i32_within(acceleration, INT32_C(60000)) ||
        !absolute_i32_within(compensation, INT32_C(60000)) ||
        !absolute_i32_within(rod_command, INT32_C(60000)) ||
        (target_raw > UINT16_MAX) || (feedback_raw > UINT16_MAX) ||
        !absolute_i32_within(command_current, INT32_C(2000)) ||
        !absolute_i32_within(feedback_current, INT32_C(10000)) ||
        !absolute_i32_within(speed, INT32_C(100000)) ||
        (phase > 2U) || (flags > DEVICE_PEER_LINK_DEBUG_STATUS_MASK)) {
        increment_saturating(&link->status.debug_payload_error_count);
        return;
    }
    if (link->debug_rx_sequence_seen) {
        if (packet->sequence == link->last_debug_rx_sequence) {
            increment_saturating(
                &link->status.debug_duplicate_sequence_count);
            return;
        }
        if (!serial_u16_newer(
                packet->sequence, link->last_debug_rx_sequence)) {
            increment_saturating(&link->status.debug_old_sequence_count);
            return;
        }
    }
    link->remote_debug_telemetry = (Device_PeerLink_DebugTelemetry){
        .chassis_acceleration_mm_s2 = acceleration,
        .chassis_compensation_mdeg = compensation,
        .rod_command_mdeg = rod_command,
        .motor_target_raw = (uint16_t)target_raw,
        .motor_feedback_raw = (uint16_t)feedback_raw,
        .motor_command_current_ma = command_current,
        .motor_feedback_current_ma = feedback_current,
        .motor_speed_centi_rpm = speed,
        .control_phase = (uint8_t)phase,
        .status_flags = (uint16_t)flags,
        .received_at_ms = now_ms,
    };
    link->last_debug_rx_sequence = packet->sequence;
    link->debug_rx_sequence_seen = true;
    link->remote_debug_telemetry_seen = true;
    increment_saturating(&link->status.debug_rx_count);
}

static bool send_heartbeat(Device_PeerLink *link, uint32_t now_ms)
{
    size_t payload_length;
    size_t length;
    size_t frame_length = 0U;
    BSP_UART_Result result;

    payload_length = format_u32(
        link->tx_payload, sizeof(link->tx_payload),
        link->config.local_node_id);
    if ((payload_length == 0U) ||
        (payload_length >= sizeof(link->tx_payload))) {
        increment_saturating(&link->status.heartbeat_tx_error_count);
        return false;
    }
    link->tx_payload[payload_length++] = ',';
    length = format_u32(
        &link->tx_payload[payload_length],
        sizeof(link->tx_payload) - payload_length,
        now_ms);
    if (length == 0U) {
        increment_saturating(&link->status.heartbeat_tx_error_count);
        return false;
    }
    payload_length += length;

    if (Algorithm_Packet_Encode(
            "HB", link->heartbeat_tx_sequence,
            link->tx_payload, payload_length,
            link->tx_frame, sizeof(link->tx_frame), &frame_length) !=
        ALGORITHM_PACKET_ENCODE_OK) {
        increment_saturating(&link->status.heartbeat_tx_error_count);
        return false;
    }

    result = BSP_UART_WriteBuffered(
        link->uart, link->tx_frame, frame_length);
    if (result == BSP_UART_RESULT_BUSY) {
        increment_saturating(&link->status.heartbeat_tx_drop_count);
        return true;
    }
    if (result != BSP_UART_RESULT_OK) {
        increment_saturating(&link->status.heartbeat_tx_error_count);
        return false;
    }

    link->heartbeat_tx_sequence =
        (uint16_t)(link->heartbeat_tx_sequence + 1U);
    increment_saturating(&link->status.heartbeat_tx_count);
    return true;
}

bool Device_PeerLink_Init(
    Device_PeerLink *link,
    const Device_PeerLink_Config *config,
    uint32_t now_ms)
{
    BSP_UART_Info info;
    BSP_UART_Handle uart;

    if ((link == NULL) || (config == NULL)) {
        return false;
    }
    memset(link, 0, sizeof(*link));
    uart = BSP_UART_GetHandle(config->transport_index);
    if ((uart == NULL) || !node_id_valid(config->local_node_id) ||
        !node_id_valid(config->remote_node_id) ||
        (config->local_node_id == config->remote_node_id) ||
        (config->heartbeat_period_ms == 0U) ||
        (config->heartbeat_period_ms > (uint32_t)INT32_MAX) ||
        (config->offline_timeout_ms < config->heartbeat_period_ms) ||
        (config->offline_timeout_ms > (uint32_t)INT32_MAX) ||
        (config->maximum_rx_bytes_per_step == 0U) ||
        (config->maximum_rx_bytes_per_step > sizeof(link->rx_chunk)) ||
        !BSP_UART_GetInfo(uart, &info) ||
        (info.baud_rate != DEVICE_PEER_LINK_BAUD_RATE) ||
        !info.supports_tx_dma || !info.supports_rx_dma ||
        !info.supports_buffered_tx || !info.supports_buffered_rx ||
        (info.tx_buffer_capacity < ALGORITHM_PACKET_MAX_FRAME_SIZE) ||
        (info.rx_buffer_capacity < ALGORITHM_PACKET_MAX_FRAME_SIZE) ||
        !Algorithm_Packet_Parser_Init(
            &link->parser, link->parser_storage,
            sizeof(link->parser_storage))) {
        return false;
    }

    link->config = *config;
    link->uart = uart;
    link->next_heartbeat_tx_ms = now_ms;
    link->initialized = true;
    if (!BSP_UART_GetRxStatus(link->uart, &link->status.uart_rx)) {
        memset(link, 0, sizeof(*link));
        return false;
    }
    return true;
}

bool Device_PeerLink_Step(Device_PeerLink *link, uint32_t now_ms)
{
    size_t received = 0U;
    bool ok = true;

    if ((link == NULL) || !link->initialized) {
        return false;
    }

    if (BSP_UART_ReadAvailable(
            link->uart, link->rx_chunk,
            link->config.maximum_rx_bytes_per_step, &received) !=
        BSP_UART_RESULT_OK) {
        received = 0U;
        ok = false;
    }
    for (size_t index = 0U; index < received; ++index) {
        Algorithm_Packet_View packet;

        if (Algorithm_Packet_Parser_Feed(
                &link->parser, link->rx_chunk[index], &packet) ==
            ALGORITHM_PACKET_FEED_FRAME_READY) {
            if (is_heartbeat(&packet)) {
                consume_heartbeat(link, &packet, now_ms);
            }
            else if (is_car_state(&packet)) {
                consume_car_state(link, &packet, now_ms);
            }
            else if (is_debug_telemetry(&packet)) {
                consume_debug_telemetry(link, &packet, now_ms);
            }
        }
    }
    link->status.packet_parser_error_count =
        Algorithm_Packet_Parser_ErrorCount(&link->parser);

    if (link->status.online &&
        ((uint32_t)(now_ms - link->status.last_heartbeat_rx_ms) >=
         link->config.offline_timeout_ms)) {
        link->status.online = false;
    }

    if (time_reached(now_ms, link->next_heartbeat_tx_ms)) {
        link->next_heartbeat_tx_ms =
            now_ms + link->config.heartbeat_period_ms;
        if (!send_heartbeat(link, now_ms)) {
            ok = false;
        }
    }

    if (!BSP_UART_GetRxStatus(
            link->uart, &link->status.uart_rx)) {
        ok = false;
    }
    if (!ok) {
        increment_saturating(&link->status.step_error_count);
    }
    return ok;
}

bool Device_PeerLink_SendCarState(
    Device_PeerLink *link,
    const Device_PeerLink_CarState *state)
{
    size_t payload_length = 0U;
    size_t frame_length = 0U;
    BSP_UART_Result result;

    if ((link == NULL) || !link->initialized ||
        !car_state_values_valid(state) ||
        (link->config.local_node_id != DEVICE_PEER_LINK_G3519_NODE_ID) ||
        (link->config.remote_node_id != DEVICE_PEER_LINK_F407_NODE_ID)) {
        if ((link != NULL) && link->initialized) {
            increment_saturating(&link->status.car_state_tx_error_count);
        }
        return false;
    }
    if (!append_u32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, link->config.local_node_id) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_u32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->sample_ms) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_i32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->speed_x_mm_per_second) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_i32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->acceleration_x_mm_per_second2) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_i32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->acceleration_z_mm_per_second2) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_i32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->pitch_rate_centi_dps) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_u32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->valid ? 1U : 0U) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_u32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->competition_mode) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_u32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, state->competition_task_active ? 1U : 0U) ||
        (state->planned_acceleration_valid &&
         (!append_separator(link->tx_payload, sizeof(link->tx_payload),
              &payload_length) ||
          !append_i32(link->tx_payload, sizeof(link->tx_payload),
              &payload_length,
              state->planned_acceleration_x_mm_per_second2) ||
          !append_separator(link->tx_payload, sizeof(link->tx_payload),
              &payload_length) ||
           !append_u32(link->tx_payload, sizeof(link->tx_payload),
               &payload_length, 1U))) ||
        (state->filtered_wheel_acceleration_valid &&
         (!append_separator(link->tx_payload, sizeof(link->tx_payload),
              &payload_length) ||
          !append_i32(link->tx_payload, sizeof(link->tx_payload),
              &payload_length,
              state->filtered_wheel_acceleration_x_mm_per_second2) ||
          !append_separator(link->tx_payload, sizeof(link->tx_payload),
              &payload_length) ||
          !append_u32(link->tx_payload, sizeof(link->tx_payload),
              &payload_length, 1U))) ||
        (Algorithm_Packet_Encode(
            "CAR", link->car_state_tx_sequence,
            link->tx_payload, payload_length,
            link->tx_frame, sizeof(link->tx_frame), &frame_length) !=
         ALGORITHM_PACKET_ENCODE_OK)) {
        increment_saturating(&link->status.car_state_tx_error_count);
        return false;
    }

    result = BSP_UART_WriteBuffered(
        link->uart, link->tx_frame, frame_length);
    if (result == BSP_UART_RESULT_BUSY) {
        increment_saturating(&link->status.car_state_tx_drop_count);
        return true;
    }
    if (result != BSP_UART_RESULT_OK) {
        increment_saturating(&link->status.car_state_tx_error_count);
        return false;
    }
    link->car_state_tx_sequence =
        (uint16_t)(link->car_state_tx_sequence + 1U);
    increment_saturating(&link->status.car_state_tx_count);
    return true;
}

Device_PeerLink_SendResult Device_PeerLink_SendBallTarget(
    Device_PeerLink *link,
    int32_t target_0p1mm)
{
    size_t payload_length = 0U;
    size_t frame_length = 0U;
    BSP_UART_Result result;

    if ((link == NULL) || !link->initialized ||
        (link->config.local_node_id != DEVICE_PEER_LINK_G3519_NODE_ID) ||
        (link->config.remote_node_id != DEVICE_PEER_LINK_F407_NODE_ID) ||
        !absolute_i32_within(target_0p1mm,
            DEVICE_PEER_LINK_BALL_TARGET_ABS_MAX_0P1_MM)) {
        if ((link != NULL) && link->initialized) {
            increment_saturating(&link->status.ball_target_tx_error_count);
        }
        return DEVICE_PEER_LINK_SEND_INVALID_ARGUMENT;
    }
    if (!append_u32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, link->config.local_node_id) ||
        !append_separator(link->tx_payload, sizeof(link->tx_payload),
            &payload_length) ||
        !append_i32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, target_0p1mm) ||
        (Algorithm_Packet_Encode(
            "BTG", link->ball_target_tx_sequence,
            link->tx_payload, payload_length,
            link->tx_frame, sizeof(link->tx_frame), &frame_length) !=
         ALGORITHM_PACKET_ENCODE_OK)) {
        increment_saturating(&link->status.ball_target_tx_error_count);
        return DEVICE_PEER_LINK_SEND_ERROR;
    }

    result = BSP_UART_WriteBuffered(
        link->uart, link->tx_frame, frame_length);
    if (result == BSP_UART_RESULT_BUSY) {
        increment_saturating(&link->status.ball_target_tx_busy_count);
        return DEVICE_PEER_LINK_SEND_BUSY;
    }
    if (result != BSP_UART_RESULT_OK) {
        increment_saturating(&link->status.ball_target_tx_error_count);
        return DEVICE_PEER_LINK_SEND_ERROR;
    }
    link->ball_target_tx_sequence =
        (uint16_t)(link->ball_target_tx_sequence + 1U);
    increment_saturating(&link->status.ball_target_tx_count);
    return DEVICE_PEER_LINK_SEND_QUEUED;
}

Device_PeerLink_SendResult Device_PeerLink_SendBallStop(
    Device_PeerLink *link)
{
    size_t payload_length = 0U;
    size_t frame_length = 0U;
    BSP_UART_Result result;

    if ((link == NULL) || !link->initialized ||
        (link->config.local_node_id != DEVICE_PEER_LINK_G3519_NODE_ID) ||
        (link->config.remote_node_id != DEVICE_PEER_LINK_F407_NODE_ID)) {
        if ((link != NULL) && link->initialized) {
            increment_saturating(&link->status.ball_stop_tx_error_count);
        }
        return DEVICE_PEER_LINK_SEND_INVALID_ARGUMENT;
    }
    if (!append_u32(link->tx_payload, sizeof(link->tx_payload),
            &payload_length, link->config.local_node_id) ||
        (Algorithm_Packet_Encode(
            "BST", link->ball_stop_tx_sequence,
            link->tx_payload, payload_length,
            link->tx_frame, sizeof(link->tx_frame), &frame_length) !=
         ALGORITHM_PACKET_ENCODE_OK)) {
        increment_saturating(&link->status.ball_stop_tx_error_count);
        return DEVICE_PEER_LINK_SEND_ERROR;
    }

    result = BSP_UART_WriteBuffered(
        link->uart, link->tx_frame, frame_length);
    if (result == BSP_UART_RESULT_BUSY) {
        increment_saturating(&link->status.ball_stop_tx_busy_count);
        return DEVICE_PEER_LINK_SEND_BUSY;
    }
    if (result != BSP_UART_RESULT_OK) {
        increment_saturating(&link->status.ball_stop_tx_error_count);
        return DEVICE_PEER_LINK_SEND_ERROR;
    }
    link->ball_stop_tx_sequence =
        (uint16_t)(link->ball_stop_tx_sequence + 1U);
    increment_saturating(&link->status.ball_stop_tx_count);
    return DEVICE_PEER_LINK_SEND_QUEUED;
}

bool Device_PeerLink_GetRemoteCarState(
    const Device_PeerLink *link,
    uint32_t now_ms,
    uint32_t maximum_age_ms,
    Device_PeerLink_CarState *state)
{
    if ((link == NULL) || !link->initialized || (state == NULL) ||
        !link->remote_car_state_seen ||
        (!link->remote_car_state.valid &&
         !link->remote_car_state.planned_acceleration_valid) ||
        ((uint32_t)(now_ms - link->remote_car_state.received_at_ms) >
            maximum_age_ms)) {
        return false;
    }
    *state = link->remote_car_state;
    return true;
}

bool Device_PeerLink_GetRemoteDebugTelemetry(
    const Device_PeerLink *link,
    uint32_t now_ms,
    uint32_t maximum_age_ms,
    Device_PeerLink_DebugTelemetry *telemetry)
{
    if ((link == NULL) || !link->initialized || (telemetry == NULL) ||
        !link->remote_debug_telemetry_seen ||
        ((uint32_t)(now_ms -
            link->remote_debug_telemetry.received_at_ms) > maximum_age_ms)) {
        return false;
    }
    *telemetry = link->remote_debug_telemetry;
    return true;
}
