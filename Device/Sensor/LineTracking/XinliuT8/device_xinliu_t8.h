#ifndef DEVICE_XINLIU_T8_H
#define DEVICE_XINLIU_T8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_XINLIU_T8_CHANNEL_COUNT 8U
#define DEVICE_XINLIU_T8_READ_ANALOG_QUERY_SIZE 7U
#define DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE 22U

typedef enum {
    DEVICE_XINLIU_T8_FEED_IN_PROGRESS = 0,
    DEVICE_XINLIU_T8_FEED_SAMPLE_READY,
    DEVICE_XINLIU_T8_FEED_FRAME_REJECTED,
    DEVICE_XINLIU_T8_FEED_INVALID_ARGUMENT,
} Device_XinliuT8_FeedResult;

typedef struct {
    uint16_t adc[DEVICE_XINLIU_T8_CHANNEL_COUNT];
    uint32_t received_at_ms;
    uint32_t sequence;
} Device_XinliuT8_Snapshot;

typedef struct {
    uint32_t accepted_frames;
    uint32_t rejected_frames;
    uint32_t checksum_errors;
    uint32_t format_errors;
    uint32_t discarded_bytes;
} Device_XinliuT8_Stats;

typedef struct {
    uint8_t frame[DEVICE_XINLIU_T8_ANALOG_RESPONSE_SIZE];
    size_t frame_length;
    size_t expected_frame_length;
    Device_XinliuT8_Snapshot latest;
    Device_XinliuT8_Stats stats;
    bool has_latest;
    bool initialized;
} Device_XinliuT8;

bool Device_XinliuT8_Init(Device_XinliuT8 *device);
/* Discards only a partial byte stream. Statistics and the latest sample are
 * preserved so a Task can recover cleanly after a UART timeout/error. */
void Device_XinliuT8_ResetStream(Device_XinliuT8 *device);

/* Builds the manual's full-resolution eight-channel ADC query:
 * AA 55 00 01 19 1A 66. */
bool Device_XinliuT8_BuildReadAnalogQuery(
    uint8_t *frame, size_t capacity, size_t *length);

Device_XinliuT8_FeedResult Device_XinliuT8_FeedByte(
    Device_XinliuT8 *device, uint32_t now_ms, uint8_t byte);
bool Device_XinliuT8_GetLatest(
    const Device_XinliuT8 *device, Device_XinliuT8_Snapshot *snapshot);
bool Device_XinliuT8_GetStats(
    const Device_XinliuT8 *device, Device_XinliuT8_Stats *stats);
bool Device_XinliuT8_SnapshotFresh(
    const Device_XinliuT8_Snapshot *snapshot,
    uint32_t now_ms,
    uint32_t timeout_ms);

#endif
