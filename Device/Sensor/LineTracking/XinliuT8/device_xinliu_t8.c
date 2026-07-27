#include "device_xinliu_t8.h"

#include <string.h>

#define DEVICE_XINLIU_T8_HEADER_0 UINT8_C(0xAA)
#define DEVICE_XINLIU_T8_HEADER_1 UINT8_C(0x55)
#define DEVICE_XINLIU_T8_TAIL UINT8_C(0x66)
#define DEVICE_XINLIU_T8_READ_FUNCTION UINT8_C(0x00)
#define DEVICE_XINLIU_T8_QUERY_PAYLOAD_LENGTH UINT8_C(0x01)
#define DEVICE_XINLIU_T8_READ_ANALOG_COMMAND UINT8_C(0x19)
#define DEVICE_XINLIU_T8_ANALOG_DATA_LENGTH UINT8_C(0x10)
#define DEVICE_XINLIU_T8_MAX_DATA_LENGTH 16U
#define DEVICE_XINLIU_T8_FIXED_RESPONSE_BYTES 6U

static void increment_saturating(uint32_t *value)
{
    if ((value != NULL) && (*value != UINT32_MAX)) {
        ++(*value);
    }
}

static uint8_t checksum(
    const uint8_t *bytes, size_t offset, size_t length)
{
    uint8_t sum = 0U;

    for (size_t index = 0U; index < length; ++index) {
        sum = (uint8_t)(sum + bytes[offset + index]);
    }
    return sum;
}

static uint16_t read_u16_le(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
        ((uint16_t)bytes[offset + 1U] << 8U));
}

static void refresh_expected_length(Device_XinliuT8 *device)
{
    device->expected_frame_length = 0U;
    if (device->frame_length >= 4U) {
        const size_t data_length = device->frame[3];

        if (data_length <= DEVICE_XINLIU_T8_MAX_DATA_LENGTH) {
            device->expected_frame_length =
                data_length + DEVICE_XINLIU_T8_FIXED_RESPONSE_BYTES;
        }
    }
}

static void resynchronize(Device_XinliuT8 *device)
{
    size_t index;

    for (index = 1U; index + 1U < device->frame_length; ++index) {
        if ((device->frame[index] == DEVICE_XINLIU_T8_HEADER_0) &&
            (device->frame[index + 1U] == DEVICE_XINLIU_T8_HEADER_1)) {
            const size_t retained = device->frame_length - index;

            memmove(device->frame, &device->frame[index], retained);
            device->frame_length = retained;
            refresh_expected_length(device);
            return;
        }
    }
    if ((device->frame_length != 0U) &&
        (device->frame[device->frame_length - 1U] ==
            DEVICE_XINLIU_T8_HEADER_0)) {
        device->frame[0] = DEVICE_XINLIU_T8_HEADER_0;
        device->frame_length = 1U;
    }
    else {
        device->frame_length = 0U;
    }
    device->expected_frame_length = 0U;
}

static Device_XinliuT8_FeedResult reject_frame(
    Device_XinliuT8 *device, bool checksum_error)
{
    increment_saturating(&device->stats.rejected_frames);
    if (checksum_error) {
        increment_saturating(&device->stats.checksum_errors);
    }
    else {
        increment_saturating(&device->stats.format_errors);
    }
    resynchronize(device);
    return DEVICE_XINLIU_T8_FEED_FRAME_REJECTED;
}

static bool response_checksum_valid(const Device_XinliuT8 *device)
{
    const size_t data_length = device->frame[3];
    const size_t checksum_index = 4U + data_length;

    return device->frame[checksum_index] ==
        checksum(device->frame, 2U, 2U + data_length);
}

static bool analog_response_structure_valid(const Device_XinliuT8 *device)
{
    return (device->expected_frame_length ==
            DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE) &&
        (device->frame[0] == DEVICE_XINLIU_T8_HEADER_0) &&
        (device->frame[1] == DEVICE_XINLIU_T8_HEADER_1) &&
        (device->frame[2] == DEVICE_XINLIU_T8_READ_ANALOG_COMMAND) &&
        (device->frame[3] == DEVICE_XINLIU_T8_ANALOG_DATA_LENGTH) &&
        (device->frame[DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE - 1U] ==
            DEVICE_XINLIU_T8_TAIL);
}

static void decode_analog_response(
    Device_XinliuT8 *device, uint32_t now_ms)
{
    Device_XinliuT8_Snapshot next = {
        .received_at_ms = now_ms,
        .sequence = (device->stats.accepted_frames == UINT32_MAX) ?
            UINT32_MAX : device->stats.accepted_frames + 1U,
    };

    for (size_t index = 0U;
         index < DEVICE_XINLIU_T8_CHANNEL_COUNT;
         ++index) {
        next.adc[index] = read_u16_le(device->frame, 4U + (index * 2U));
    }
    device->latest = next;
    device->has_latest = true;
}

bool Device_XinliuT8_Init(Device_XinliuT8 *device)
{
    if (device == NULL) {
        return false;
    }
    *device = (Device_XinliuT8){0};
    device->initialized = true;
    return true;
}

void Device_XinliuT8_ResetStream(Device_XinliuT8 *device)
{
    if ((device == NULL) || !device->initialized) {
        return;
    }
    device->frame_length = 0U;
    device->expected_frame_length = 0U;
}

bool Device_XinliuT8_BuildReadAnalogQuery(
    uint8_t *frame, size_t capacity, size_t *length)
{
    static const uint8_t query[DEVICE_XINLIU_T8_READ_ANALOG_QUERY_SIZE] = {
        DEVICE_XINLIU_T8_HEADER_0,
        DEVICE_XINLIU_T8_HEADER_1,
        DEVICE_XINLIU_T8_READ_FUNCTION,
        DEVICE_XINLIU_T8_QUERY_PAYLOAD_LENGTH,
        DEVICE_XINLIU_T8_READ_ANALOG_COMMAND,
        UINT8_C(0x1A),
        DEVICE_XINLIU_T8_TAIL,
    };

    if (length != NULL) {
        *length = 0U;
    }
    if ((frame == NULL) || (length == NULL) ||
        (capacity < sizeof(query))) {
        return false;
    }
    memcpy(frame, query, sizeof(query));
    *length = sizeof(query);
    return true;
}

Device_XinliuT8_FeedResult Device_XinliuT8_FeedByte(
    Device_XinliuT8 *device, uint32_t now_ms, uint8_t byte)
{
    if ((device == NULL) || !device->initialized) {
        return DEVICE_XINLIU_T8_FEED_INVALID_ARGUMENT;
    }
    if (device->frame_length == 0U) {
        if (byte == DEVICE_XINLIU_T8_HEADER_0) {
            device->frame[0] = byte;
            device->frame_length = 1U;
        }
        else {
            increment_saturating(&device->stats.discarded_bytes);
        }
        return DEVICE_XINLIU_T8_FEED_IN_PROGRESS;
    }
    if (device->frame_length == 1U) {
        if (byte == DEVICE_XINLIU_T8_HEADER_1) {
            device->frame[1] = byte;
            device->frame_length = 2U;
        }
        else if (byte != DEVICE_XINLIU_T8_HEADER_0) {
            device->frame_length = 0U;
            increment_saturating(&device->stats.discarded_bytes);
        }
        return DEVICE_XINLIU_T8_FEED_IN_PROGRESS;
    }
    if (device->frame_length >= sizeof(device->frame)) {
        return reject_frame(device, false);
    }

    device->frame[device->frame_length] = byte;
    ++device->frame_length;
    if (device->frame_length == 4U) {
        if (device->frame[3] > DEVICE_XINLIU_T8_MAX_DATA_LENGTH) {
            return reject_frame(device, false);
        }
        refresh_expected_length(device);
    }
    if ((device->expected_frame_length == 0U) ||
        (device->frame_length < device->expected_frame_length)) {
        return DEVICE_XINLIU_T8_FEED_IN_PROGRESS;
    }
    if (device->frame_length != device->expected_frame_length) {
        return reject_frame(device, false);
    }
    if (!response_checksum_valid(device)) {
        return reject_frame(device, true);
    }
    if (!analog_response_structure_valid(device)) {
        return reject_frame(device, false);
    }

    decode_analog_response(device, now_ms);
    device->frame_length = 0U;
    device->expected_frame_length = 0U;
    increment_saturating(&device->stats.accepted_frames);
    return DEVICE_XINLIU_T8_FEED_SAMPLE_READY;
}

bool Device_XinliuT8_GetLatest(
    const Device_XinliuT8 *device, Device_XinliuT8_Snapshot *snapshot)
{
    if ((device == NULL) || !device->initialized ||
        !device->has_latest || (snapshot == NULL)) {
        return false;
    }
    *snapshot = device->latest;
    return true;
}

bool Device_XinliuT8_GetStats(
    const Device_XinliuT8 *device, Device_XinliuT8_Stats *stats)
{
    if ((device == NULL) || !device->initialized || (stats == NULL)) {
        return false;
    }
    *stats = device->stats;
    return true;
}

bool Device_XinliuT8_SnapshotFresh(
    const Device_XinliuT8_Snapshot *snapshot,
    uint32_t now_ms,
    uint32_t timeout_ms)
{
    return (snapshot != NULL) &&
        ((uint32_t)(now_ms - snapshot->received_at_ms) <= timeout_ms);
}
