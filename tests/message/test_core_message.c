#include "core_message.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void invalid_initialization(void)
{
    Core_Message_Topic topic = {0};
    uint32_t storage[3] = {0};
    Core_Message_Subscription subs[2] = {
        {.storage = storage, .storage_bytes = sizeof(uint32_t),
         .policy = CORE_MESSAGE_FIFO},
        {.storage = storage, .storage_bytes = sizeof(uint32_t),
         .policy = CORE_MESSAGE_LATEST},
    };
    assert(Core_Message_Publish(&topic, storage, sizeof(uint32_t)) ==
        CORE_MESSAGE_NOT_READY);
    assert(Core_Message_Init(&topic, sizeof(uint32_t), subs, 2U) ==
        CORE_MESSAGE_INVALID); /* overlapping ownership */
    assert(subs[0].owner == NULL && topic.subscriptions == NULL);
    subs[1].storage = &storage[1];
    subs[1].storage_bytes = 3U;
    assert(Core_Message_Init(&topic, sizeof(uint32_t), subs, 2U) ==
        CORE_MESSAGE_INVALID);
    subs[1].storage_bytes = 2U * sizeof(uint32_t);
    assert(Core_Message_Init(&topic, sizeof(uint32_t), subs, 2U) ==
        CORE_MESSAGE_INVALID); /* LATEST must be one slot */
    subs[1].storage_bytes = sizeof(uint32_t);
    assert(Core_Message_Init(&topic, sizeof(uint32_t), subs, 2U) ==
        CORE_MESSAGE_OK);
    assert(Core_Message_Init(&topic, sizeof(uint32_t), subs, 2U) ==
        CORE_MESSAGE_INVALID);
    Core_Message_Topic other = {0};
    assert(Core_Message_Init(&other, sizeof(uint32_t), subs, 2U) ==
        CORE_MESSAGE_INVALID); /* no sharing subscriptions */
    assert(Core_Message_Publish(&topic, storage, 1U) == CORE_MESSAGE_INVALID);
    assert(Core_Message_Take(&subs[0], storage, 1U) == CORE_MESSAGE_INVALID);
    assert(Core_Message_Snapshot(&subs[0], storage, sizeof(uint32_t)) ==
        CORE_MESSAGE_INVALID);
}

int main(void)
{
    uint32_t first[3], second[5], latest;
    Core_Message_Topic topic = {0};
    Core_Message_Subscription subs[3] = {
        {.storage = first, .storage_bytes = sizeof(first),
         .policy = CORE_MESSAGE_FIFO},
        {.storage = second, .storage_bytes = sizeof(second),
         .policy = CORE_MESSAGE_FIFO},
        {.storage = &latest, .storage_bytes = sizeof(latest),
         .policy = CORE_MESSAGE_LATEST},
    };
    uint32_t model[2][5] = {{0}};
    size_t count[2] = {0U, 0U};
    const size_t capacity[2] = {3U, 5U};
    uint32_t expected_latest = 0U;
    int latest_valid = 0;
    uint32_t random = 19U;
    invalid_initialization();
    assert(Core_Message_Init(&topic, sizeof(uint32_t), subs, 3U) ==
        CORE_MESSAGE_OK);
    for (uint32_t step = 1U; step < 10000U; ++step) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        const unsigned action = (unsigned)(random >> 28U) % 6U;
        if (action < 3U) {
            uint32_t value = step;
            const int full = count[0] == capacity[0] || count[1] == capacity[1];
            assert(Core_Message_Publish(&topic, &value, sizeof(value)) ==
                (full ? CORE_MESSAGE_FULL : CORE_MESSAGE_OK));
            if (!full) {
                for (size_t i = 0U; i < 2U; ++i) {
                    model[i][count[i]++] = value;
                }
                expected_latest = value;
                latest_valid = 1;
            }
            value = 0U; /* source lifetime must not affect subscriber copies */
        }
        else if (action < 5U) {
            const size_t which = (size_t)(action - 3U);
            uint32_t output = UINT32_MAX;
            assert(Core_Message_Take(&subs[which], &output, sizeof(output)) ==
                (count[which] == 0U ? CORE_MESSAGE_EMPTY : CORE_MESSAGE_OK));
            if (count[which] != 0U) {
                assert(output == model[which][0]);
                --count[which];
                memmove(model[which], &model[which][1],
                    count[which] * sizeof(uint32_t));
            }
            else {
                assert(output == UINT32_MAX);
            }
        }
        else {
            uint32_t output = UINT32_MAX;
            assert(Core_Message_Take(&subs[2], &output, sizeof(output)) ==
                (latest_valid ? CORE_MESSAGE_OK : CORE_MESSAGE_EMPTY));
            if (latest_valid) {
                assert(output == expected_latest);
            }
            latest_valid = 0;
        }
        uint32_t snapshot = UINT32_MAX;
        assert(Core_Message_Snapshot(&subs[2], &snapshot, sizeof(snapshot)) ==
            (latest_valid ? CORE_MESSAGE_OK : CORE_MESSAGE_EMPTY));
        assert(snapshot == (latest_valid ? expected_latest : UINT32_MAX));
    }
    puts("core_message: ownership, FIFO ordering, fanout atomicity, latest and wrap passed");
    return 0;
}
