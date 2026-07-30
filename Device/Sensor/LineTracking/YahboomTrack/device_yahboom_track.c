#include "device_yahboom_track.h"

#include <string.h>

typedef enum {
    PARSE_OK = 0,
    PARSE_FORMAT_ERROR,
    PARSE_VALUE_OVERFLOW,
} ParseResult;

static void increment_saturating(uint32_t *value)
{
    if ((value != NULL) && (*value != UINT32_MAX)) {
        ++(*value);
    }
}

static void add_saturating(uint32_t *value, size_t amount)
{
    if (value == NULL) {
        return;
    }
    if (amount > (size_t)(UINT32_MAX - *value)) {
        *value = UINT32_MAX;
    }
    else {
        *value += (uint32_t)amount;
    }
}

static ParseResult parse_frame(
    const uint8_t *frame,
    size_t length,
    Device_YahboomTrack_Snapshot *snapshot)
{
    size_t offset = 0U;

    if ((frame == NULL) || (snapshot == NULL) || (length < 6U) ||
        (frame[0] != (uint8_t)'$') ||
        (frame[1] != (uint8_t)'A') ||
        (frame[2] != (uint8_t)',')) {
        return PARSE_FORMAT_ERROR;
    }
    offset = 3U;

    for (size_t channel = 0U;
         channel < DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT;
         ++channel) {
        uint32_t value = 0U;
        bool has_digit = false;

        if ((offset + 3U > length) ||
            (frame[offset] != (uint8_t)'x') ||
            (frame[offset + 1U] != (uint8_t)('1' + channel)) ||
            (frame[offset + 2U] != (uint8_t)':')) {
            return PARSE_FORMAT_ERROR;
        }
        offset += 3U;

        while ((offset < length) &&
            (frame[offset] >= (uint8_t)'0') &&
            (frame[offset] <= (uint8_t)'9')) {
            const uint32_t digit =
                (uint32_t)(frame[offset] - (uint8_t)'0');

            has_digit = true;
            if (value > ((UINT16_MAX - digit) / 10U)) {
                return PARSE_VALUE_OVERFLOW;
            }
            value = (value * 10U) + digit;
            ++offset;
        }
        if (!has_digit) {
            return PARSE_FORMAT_ERROR;
        }
        snapshot->adc[channel] = (uint16_t)value;

        if (channel + 1U < DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT) {
            if ((offset >= length) ||
                (frame[offset] != (uint8_t)',')) {
                return PARSE_FORMAT_ERROR;
            }
        }
        else if ((offset >= length) ||
            (frame[offset] != (uint8_t)'#')) {
            return PARSE_FORMAT_ERROR;
        }
        ++offset;
    }

    return (offset == length) ? PARSE_OK : PARSE_FORMAT_ERROR;
}

static Device_YahboomTrack_FeedResult reject_frame(
    Device_YahboomTrack *device,
    ParseResult reason,
    bool overlength)
{
    increment_saturating(&device->stats.rejected_frames);
    if (reason == PARSE_VALUE_OVERFLOW) {
        increment_saturating(&device->stats.value_overflow_errors);
    }
    else {
        increment_saturating(&device->stats.format_errors);
    }
    if (overlength) {
        increment_saturating(&device->stats.overlength_frames);
    }
    device->frame_length = 0U;
    device->receiving = false;
    return DEVICE_YAHBOOM_TRACK_FEED_FRAME_REJECTED;
}

bool Device_YahboomTrack_Init(Device_YahboomTrack *device)
{
    if (device == NULL) {
        return false;
    }
    *device = (Device_YahboomTrack){0};
    device->initialized = true;
    return true;
}

void Device_YahboomTrack_ResetStream(Device_YahboomTrack *device)
{
    if ((device == NULL) || !device->initialized) {
        return;
    }
    device->frame_length = 0U;
    device->receiving = false;
}

bool Device_YahboomTrack_BuildStartCommand(
    uint8_t *frame, size_t capacity, size_t *length)
{
    static const uint8_t command[DEVICE_YAHBOOM_TRACK_START_COMMAND_SIZE] = {
        (uint8_t)'$', (uint8_t)'0', (uint8_t)',', (uint8_t)'1',
        (uint8_t)',', (uint8_t)'0', (uint8_t)'#',
    };

    if (length != NULL) {
        *length = 0U;
    }
    if ((frame == NULL) || (length == NULL) ||
        (capacity < DEVICE_YAHBOOM_TRACK_START_COMMAND_SIZE)) {
        return false;
    }
    (void)memcpy(frame, command, DEVICE_YAHBOOM_TRACK_START_COMMAND_SIZE);
    *length = DEVICE_YAHBOOM_TRACK_START_COMMAND_SIZE;
    return true;
}

Device_YahboomTrack_FeedResult Device_YahboomTrack_FeedByte(
    Device_YahboomTrack *device, uint32_t now_ms, uint8_t byte)
{
    if ((device == NULL) || !device->initialized) {
        return DEVICE_YAHBOOM_TRACK_FEED_INVALID_ARGUMENT;
    }

    if (byte == (uint8_t)'$') {
        if (device->receiving && (device->frame_length > 0U)) {
            add_saturating(
                &device->stats.discarded_bytes, device->frame_length);
            increment_saturating(&device->stats.rejected_frames);
            increment_saturating(&device->stats.format_errors);
        }
        device->frame[0] = byte;
        device->frame_length = 1U;
        device->receiving = true;
        return DEVICE_YAHBOOM_TRACK_FEED_IN_PROGRESS;
    }
    if (!device->receiving) {
        increment_saturating(&device->stats.discarded_bytes);
        return DEVICE_YAHBOOM_TRACK_FEED_IN_PROGRESS;
    }
    if (device->frame_length >= sizeof(device->frame)) {
        return reject_frame(
            device, PARSE_FORMAT_ERROR, true);
    }

    device->frame[device->frame_length] = byte;
    ++device->frame_length;
    if (byte == (uint8_t)'#') {
        Device_YahboomTrack_Snapshot next = {
            .received_at_ms = now_ms,
            .sequence = (device->stats.accepted_frames == UINT32_MAX) ?
                UINT32_MAX : device->stats.accepted_frames + 1U,
        };
        const ParseResult result = parse_frame(
            device->frame, device->frame_length, &next);

        if (result != PARSE_OK) {
            return reject_frame(device, result, false);
        }
        device->latest = next;
        device->has_latest = true;
        device->frame_length = 0U;
        device->receiving = false;
        increment_saturating(&device->stats.accepted_frames);
        return DEVICE_YAHBOOM_TRACK_FEED_SAMPLE_READY;
    }

    return DEVICE_YAHBOOM_TRACK_FEED_IN_PROGRESS;
}

bool Device_YahboomTrack_GetLatest(
    const Device_YahboomTrack *device,
    Device_YahboomTrack_Snapshot *snapshot)
{
    if ((device == NULL) || !device->initialized ||
        !device->has_latest || (snapshot == NULL)) {
        return false;
    }
    *snapshot = device->latest;
    return true;
}

bool Device_YahboomTrack_GetStats(
    const Device_YahboomTrack *device, Device_YahboomTrack_Stats *stats)
{
    if ((device == NULL) || !device->initialized || (stats == NULL)) {
        return false;
    }
    *stats = device->stats;
    return true;
}

bool Device_YahboomTrack_SnapshotFresh(
    const Device_YahboomTrack_Snapshot *snapshot,
    uint32_t now_ms,
    uint32_t timeout_ms)
{
    return (snapshot != NULL) &&
        ((uint32_t)(now_ms - snapshot->received_at_ms) <= timeout_ms);
}
