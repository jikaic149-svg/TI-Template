#ifndef DEVICE_LINGKE_M0_DEVICE_LINGKE_M0_H
#define DEVICE_LINGKE_M0_DEVICE_LINGKE_M0_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float yaw_deg;
    float yaw_zeroed_deg;
    float yaw_rate_dps;
    uint32_t last_valid_frame_ms;
    uint32_t valid_frame_count;
    uint32_t checksum_error_count;
    bool has_yaw;
    bool has_yaw_rate;
    bool online;
} Device_LingkeM0_Snapshot;

typedef struct {
    uint32_t offline_timeout_ms;
} Device_LingkeM0_Config;

typedef struct {
    Device_LingkeM0_Config config;
    Device_LingkeM0_Snapshot snapshot;
    uint8_t frame[5];
    uint8_t frame_index;
    float yaw_offset_deg;
    bool yaw_offset_ready;
    bool initialized;
} Device_LingkeM0;

Device_LingkeM0_Config Device_LingkeM0_DefaultConfig(void);
bool Device_LingkeM0_Init(
    Device_LingkeM0 *device, const Device_LingkeM0_Config *config);

/* Feed bytes drained from the owning UART/SPSC ring in Task context.
 * Returns true only when this byte completes a checksum-valid five-byte
 * 0x5A/0xAA angular-rate or 0x5A/0xBB yaw frame. */
bool Device_LingkeM0_FeedByte(
    Device_LingkeM0 *device, uint8_t byte, uint32_t now_ms);
void Device_LingkeM0_UpdateOnline(
    Device_LingkeM0 *device, uint32_t now_ms);
bool Device_LingkeM0_TareYaw(Device_LingkeM0 *device);
bool Device_LingkeM0_GetSnapshot(
    const Device_LingkeM0 *device, Device_LingkeM0_Snapshot *snapshot);

#endif
