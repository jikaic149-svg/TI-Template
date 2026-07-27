#include "device_lingke_m0.h"

#include <limits.h>
#include <stddef.h>

#define DEVICE_LINGKE_M0_HEADER       UINT8_C(0x5A)
#define DEVICE_LINGKE_M0_ANGULAR_RATE UINT8_C(0xAA)
#define DEVICE_LINGKE_M0_YAW          UINT8_C(0xBB)
#define DEVICE_LINGKE_M0_RATE_SCALE_DPS 2000.0F
#define DEVICE_LINGKE_M0_YAW_SCALE_DEG  180.0F

static void increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++(*value);
    }
}

static int16_t read_i16_le(const uint8_t *bytes)
{
    const uint16_t raw = (uint16_t)(
        (uint16_t)bytes[0] |
        (uint16_t)((uint16_t)bytes[1] << 8U));

    if (raw <= (uint16_t)INT16_MAX) {
        return (int16_t)raw;
    }
    return (int16_t)((int32_t)raw - INT32_C(65536));
}

static float normalize_angle(float angle)
{
    while (angle >= 180.0F) {
        angle -= 360.0F;
    }
    while (angle < -180.0F) {
        angle += 360.0F;
    }
    return angle;
}

static bool valid_type(uint8_t type)
{
    return (type == DEVICE_LINGKE_M0_ANGULAR_RATE) ||
        (type == DEVICE_LINGKE_M0_YAW);
}

static bool checksum_valid(const uint8_t *frame)
{
    uint8_t sum = 0U;

    for (uint8_t index = 0U; index < 4U; ++index) {
        sum = (uint8_t)(sum + frame[index]);
    }
    return sum == frame[4];
}

static void decode_frame(Device_LingkeM0 *device, uint32_t now_ms)
{
    const float normalized =
        (float)read_i16_le(&device->frame[2]) / 32768.0F;

    if (device->frame[1] == DEVICE_LINGKE_M0_ANGULAR_RATE) {
        device->snapshot.yaw_rate_dps =
            normalized * DEVICE_LINGKE_M0_RATE_SCALE_DPS;
        device->snapshot.has_yaw_rate = true;
    }
    else {
        device->snapshot.yaw_deg =
            normalized * DEVICE_LINGKE_M0_YAW_SCALE_DEG;
        if (!device->yaw_offset_ready) {
            device->yaw_offset_deg = device->snapshot.yaw_deg;
            device->yaw_offset_ready = true;
        }
        device->snapshot.yaw_zeroed_deg = normalize_angle(
            device->snapshot.yaw_deg - device->yaw_offset_deg);
        device->snapshot.has_yaw = true;
    }

    device->snapshot.last_valid_frame_ms = now_ms;
    device->snapshot.online = true;
    increment_saturating(&device->snapshot.valid_frame_count);
}

Device_LingkeM0_Config Device_LingkeM0_DefaultConfig(void)
{
    const Device_LingkeM0_Config config = {
        .offline_timeout_ms = 500U,
    };
    return config;
}

bool Device_LingkeM0_Init(
    Device_LingkeM0 *device, const Device_LingkeM0_Config *config)
{
    const Device_LingkeM0_Config selected = (config == NULL) ?
        Device_LingkeM0_DefaultConfig() : *config;

    if (device != NULL) {
        *device = (Device_LingkeM0){0};
    }
    if ((device == NULL) || (selected.offline_timeout_ms == 0U) ||
        (selected.offline_timeout_ms > (UINT32_MAX / 2U))) {
        return false;
    }
    device->config = selected;
    device->initialized = true;
    return true;
}

bool Device_LingkeM0_FeedByte(
    Device_LingkeM0 *device, uint8_t byte, uint32_t now_ms)
{
    if ((device == NULL) || !device->initialized) {
        return false;
    }
    if (device->frame_index >= sizeof(device->frame)) {
        device->frame_index = 0U;
    }

    if (device->frame_index == 0U) {
        if (byte == DEVICE_LINGKE_M0_HEADER) {
            device->frame[0] = byte;
            device->frame_index = 1U;
        }
        return false;
    }
    if (device->frame_index == 1U) {
        if (valid_type(byte)) {
            device->frame[1] = byte;
            device->frame_index = 2U;
        }
        else if (byte != DEVICE_LINGKE_M0_HEADER) {
            device->frame_index = 0U;
        }
        return false;
    }

    device->frame[device->frame_index] = byte;
    ++device->frame_index;
    if (device->frame_index < sizeof(device->frame)) {
        return false;
    }
    device->frame_index = 0U;
    if (!checksum_valid(device->frame)) {
        increment_saturating(&device->snapshot.checksum_error_count);
        if (byte == DEVICE_LINGKE_M0_HEADER) {
            device->frame[0] = DEVICE_LINGKE_M0_HEADER;
            device->frame_index = 1U;
        }
        return false;
    }

    decode_frame(device, now_ms);
    return true;
}

void Device_LingkeM0_UpdateOnline(
    Device_LingkeM0 *device, uint32_t now_ms)
{
    if ((device == NULL) || !device->initialized ||
        (device->snapshot.valid_frame_count == 0U)) {
        return;
    }
    device->snapshot.online =
        (uint32_t)(now_ms - device->snapshot.last_valid_frame_ms) <=
        device->config.offline_timeout_ms;
}

bool Device_LingkeM0_TareYaw(Device_LingkeM0 *device)
{
    if ((device == NULL) || !device->initialized ||
        !device->snapshot.has_yaw) {
        return false;
    }
    device->yaw_offset_deg = device->snapshot.yaw_deg;
    device->yaw_offset_ready = true;
    device->snapshot.yaw_zeroed_deg = 0.0F;
    return true;
}

bool Device_LingkeM0_GetSnapshot(
    const Device_LingkeM0 *device, Device_LingkeM0_Snapshot *snapshot)
{
    if ((device == NULL) || !device->initialized || (snapshot == NULL)) {
        return false;
    }
    *snapshot = device->snapshot;
    return true;
}
