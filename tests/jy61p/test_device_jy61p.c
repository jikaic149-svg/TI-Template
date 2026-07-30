#include "device_jy61p.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void write_i16_le(uint8_t *bytes, int16_t value)
{
    const uint16_t raw = (uint16_t)value;

    bytes[0] = (uint8_t)(raw & UINT16_C(0x00FF));
    bytes[1] = (uint8_t)(raw >> 8U);
}

static void feed_frame(
    Device_JY61P *device,
    uint8_t type,
    int16_t x,
    int16_t y,
    int16_t z,
    uint32_t now_ms)
{
    uint8_t frame[11] = {0x55U, type};
    uint8_t sum = 0U;
    bool completed = false;

    write_i16_le(&frame[2], x);
    write_i16_le(&frame[4], y);
    write_i16_le(&frame[6], z);
    for (size_t index = 0U; index < 10U; ++index) {
        sum = (uint8_t)(sum + frame[index]);
    }
    frame[10] = sum;
    for (size_t index = 0U; index < sizeof(frame); ++index) {
        completed = Device_JY61P_FeedByte(
            device, frame[index], now_ms) || completed;
    }
    assert(completed);
}

int main(void)
{
    Device_JY61P device;
    Device_JY61P_Snapshot snapshot;

    assert(Device_JY61P_Init(&device, NULL));
    feed_frame(&device, 0x53U, 0, INT16_C(16384), 0, 100U);
    assert(Device_JY61P_GetSnapshot(&device, &snapshot));
    assert(snapshot.has_euler);
    assert(fabsf(snapshot.euler_deg[1] - 90.0F) < 0.001F);
    assert(snapshot.last_valid_frame_ms == 100U);
    assert(snapshot.last_euler_frame_ms == 100U);
    assert(snapshot.valid_frame_count == 1U);
    assert(snapshot.euler_frame_count == 1U);

    /* A newer gyro frame refreshes generic liveness, but must not make the
     * older Euler value look time-synchronized with that gyro frame. */
    feed_frame(&device, 0x52U, 0, 0, INT16_C(100), 120U);
    assert(Device_JY61P_GetSnapshot(&device, &snapshot));
    assert(snapshot.has_angular_rate);
    assert(snapshot.last_valid_frame_ms == 120U);
    assert(snapshot.last_euler_frame_ms == 100U);
    assert(snapshot.valid_frame_count == 2U);
    assert(snapshot.euler_frame_count == 1U);

    Device_JY61P_UpdateOnline(&device, 620U);
    assert(Device_JY61P_GetSnapshot(&device, &snapshot));
    assert(snapshot.online);
    Device_JY61P_UpdateOnline(&device, 621U);
    assert(Device_JY61P_GetSnapshot(&device, &snapshot));
    assert(!snapshot.online);

    puts("JY61P timestamp tests: PASS");
    return 0;
}
