#ifndef DEVICE_YAHBOOM_TRACK_H
#define DEVICE_YAHBOOM_TRACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT 8U
#define DEVICE_YAHBOOM_TRACK_START_COMMAND_SIZE 7U
#define DEVICE_YAHBOOM_TRACK_FRAME_CAPACITY 128U
/* Product-confirmed delay before the one-shot active-report command. */
#define DEVICE_YAHBOOM_TRACK_STARTUP_DELAY_MS UINT32_C(1000)
/* Manufacturer analog conversion full scale. */
#define DEVICE_YAHBOOM_TRACK_ADC_FULL_SCALE UINT16_C(4096)

typedef enum {
    DEVICE_YAHBOOM_TRACK_FEED_IN_PROGRESS = 0,
    DEVICE_YAHBOOM_TRACK_FEED_SAMPLE_READY,
    DEVICE_YAHBOOM_TRACK_FEED_FRAME_REJECTED,
    DEVICE_YAHBOOM_TRACK_FEED_INVALID_ARGUMENT,
} Device_YahboomTrack_FeedResult;

typedef struct {
    /* Raw x1..x8 decimal values reported by the module. */
    uint16_t adc[DEVICE_YAHBOOM_TRACK_CHANNEL_COUNT];
    uint32_t received_at_ms;
    uint32_t sequence;
} Device_YahboomTrack_Snapshot;

typedef struct {
    uint32_t accepted_frames;
    uint32_t rejected_frames;
    uint32_t format_errors;
    uint32_t value_overflow_errors;
    uint32_t overlength_frames;
    uint32_t discarded_bytes;
} Device_YahboomTrack_Stats;

typedef struct {
    uint8_t frame[DEVICE_YAHBOOM_TRACK_FRAME_CAPACITY];
    size_t frame_length;
    Device_YahboomTrack_Snapshot latest;
    Device_YahboomTrack_Stats stats;
    bool receiving;
    bool has_latest;
    bool initialized;
} Device_YahboomTrack;

bool Device_YahboomTrack_Init(Device_YahboomTrack *device);
/* Discards only a partial ASCII frame. Statistics and the latest snapshot are
 * preserved so a Task can recover after a UART error without losing history. */
void Device_YahboomTrack_ResetStream(Device_YahboomTrack *device);

/* Builds the reference source's command that enables analog active reports:
 * $0,1,0# */
bool Device_YahboomTrack_BuildStartCommand(
    uint8_t *frame, size_t capacity, size_t *length);

Device_YahboomTrack_FeedResult Device_YahboomTrack_FeedByte(
    Device_YahboomTrack *device, uint32_t now_ms, uint8_t byte);
bool Device_YahboomTrack_GetLatest(
    const Device_YahboomTrack *device,
    Device_YahboomTrack_Snapshot *snapshot);
bool Device_YahboomTrack_GetStats(
    const Device_YahboomTrack *device, Device_YahboomTrack_Stats *stats);
bool Device_YahboomTrack_SnapshotFresh(
    const Device_YahboomTrack_Snapshot *snapshot,
    uint32_t now_ms,
    uint32_t timeout_ms);

#endif
