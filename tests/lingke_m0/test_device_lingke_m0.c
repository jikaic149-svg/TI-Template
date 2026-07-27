#include "device_lingke_m0.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

static void assert_close(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.001F);
}

static void make_frame(uint8_t type, int16_t raw, uint8_t frame[5])
{
    const uint16_t bits = (uint16_t)raw;

    frame[0] = UINT8_C(0x5A);
    frame[1] = type;
    frame[2] = (uint8_t)(bits & UINT16_C(0x00FF));
    frame[3] = (uint8_t)(bits >> 8U);
    frame[4] = (uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]);
}

static bool feed_frame(
    Device_LingkeM0 *device, const uint8_t frame[5], uint32_t now_ms)
{
    bool completed = false;

    for (uint8_t index = 0U; index < 5U; ++index) {
        completed = Device_LingkeM0_FeedByte(
            device, frame[index], now_ms);
        if (index < 4U) {
            assert(!completed);
        }
    }
    return completed;
}

int main(void)
{
    Device_LingkeM0 device;
    Device_LingkeM0_Snapshot snapshot;
    Device_LingkeM0_Config config = Device_LingkeM0_DefaultConfig();
    uint8_t frame[5];

    assert(config.offline_timeout_ms == 500U);
    assert(!Device_LingkeM0_Init(NULL, NULL));
    config.offline_timeout_ms = 0U;
    assert(!Device_LingkeM0_Init(&device, &config));
    assert(Device_LingkeM0_Init(&device, NULL));

    make_frame(UINT8_C(0xAA), INT16_C(16384), frame);
    assert(feed_frame(&device, frame, 100U));
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert(snapshot.online);
    assert(snapshot.has_yaw_rate);
    assert(!snapshot.has_yaw);
    assert_close(snapshot.yaw_rate_dps, 1000.0F);
    assert(snapshot.valid_frame_count == 1U);

    make_frame(UINT8_C(0xAA), INT16_MIN, frame);
    assert(feed_frame(&device, frame, 105U));
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert_close(snapshot.yaw_rate_dps, -2000.0F);

    make_frame(UINT8_C(0xBB), INT16_C(16384), frame);
    assert(feed_frame(&device, frame, 110U));
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert(snapshot.has_yaw);
    assert_close(snapshot.yaw_deg, 90.0F);
    assert_close(snapshot.yaw_zeroed_deg, 0.0F);

    make_frame(UINT8_C(0xBB), INT16_C(-16384), frame);
    assert(feed_frame(&device, frame, 120U));
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert_close(snapshot.yaw_deg, -90.0F);
    assert_close(snapshot.yaw_zeroed_deg, -180.0F);
    assert(Device_LingkeM0_TareYaw(&device));
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert_close(snapshot.yaw_zeroed_deg, 0.0F);

    make_frame(UINT8_C(0xBB), INT16_C(0), frame);
    assert(feed_frame(&device, frame, 130U));
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert_close(snapshot.yaw_zeroed_deg, 90.0F);

    frame[4] ^= UINT8_C(0x01);
    assert(!feed_frame(&device, frame, 140U));
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert(snapshot.valid_frame_count == 5U);
    assert(snapshot.checksum_error_count == 1U);

    assert(!Device_LingkeM0_FeedByte(&device, UINT8_C(0x12), 150U));
    assert(!Device_LingkeM0_FeedByte(&device, UINT8_C(0x5A), 150U));
    assert(!Device_LingkeM0_FeedByte(&device, UINT8_C(0x5A), 150U));
    make_frame(UINT8_C(0xBB), INT16_C(8192), frame);
    for (uint8_t index = 1U; index < 5U; ++index) {
        const bool completed = Device_LingkeM0_FeedByte(
            &device, frame[index], 150U);
        assert(completed == (index == 4U));
    }
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert_close(snapshot.yaw_deg, 45.0F);
    assert_close(snapshot.yaw_zeroed_deg, 135.0F);

    Device_LingkeM0_UpdateOnline(&device, 650U);
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert(snapshot.online);
    Device_LingkeM0_UpdateOnline(&device, 651U);
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert(!snapshot.online);

    assert(!Device_LingkeM0_GetSnapshot(NULL, &snapshot));
    assert(!Device_LingkeM0_GetSnapshot(&device, NULL));
    assert(!Device_LingkeM0_FeedByte(NULL, 0U, 0U));
    assert(!Device_LingkeM0_TareYaw(NULL));

    assert(Device_LingkeM0_Init(&device, NULL));
    make_frame(UINT8_C(0xBB), INT16_C(0), frame);
    assert(feed_frame(&device, frame, UINT32_MAX - 10U));
    Device_LingkeM0_UpdateOnline(&device, 10U);
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert(snapshot.online);
    Device_LingkeM0_UpdateOnline(&device, 600U);
    assert(Device_LingkeM0_GetSnapshot(&device, &snapshot));
    assert(!snapshot.online);
    return 0;
}
