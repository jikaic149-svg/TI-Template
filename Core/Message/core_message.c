#include "core_message.h"
#include "core_message_port.h"

#include <string.h>

Core_Message_Result Core_Message_Init(
    Core_Message_Topic *topic, size_t item_size,
    Core_Message_Subscription *subscriptions, size_t subscriber_count)
{
    if ((topic == NULL) || (item_size == 0U) ||
        (subscriptions == NULL) || (subscriber_count == 0U) ||
        (topic->subscriptions != NULL)) {
        return CORE_MESSAGE_INVALID;
    }
    for (size_t i = 0U; i < subscriber_count; ++i) {
        const Core_Message_Subscription *sub = &subscriptions[i];
        if ((sub->owner != NULL) || (sub->storage == NULL) ||
            (sub->storage_bytes < item_size) ||
            ((sub->storage_bytes % item_size) != 0U) ||
            ((sub->policy != CORE_MESSAGE_FIFO) &&
             (sub->policy != CORE_MESSAGE_LATEST)) ||
            ((sub->policy == CORE_MESSAGE_LATEST) &&
             (sub->storage_bytes != item_size))) {
            return CORE_MESSAGE_INVALID;
        }
        /* Reject overlapping subscriber buffers: each consumer needs its
         * own copy. Integer ranges also avoid comparing unrelated pointers. */
        const uintptr_t start = (uintptr_t)sub->storage;
        if (sub->storage_bytes > (UINTPTR_MAX - start)) {
            return CORE_MESSAGE_INVALID;
        }
        for (size_t j = 0U; j < i; ++j) {
            const uintptr_t other = (uintptr_t)subscriptions[j].storage;
            if ((start < (other + subscriptions[j].storage_bytes)) &&
                (other < (start + sub->storage_bytes))) {
                return CORE_MESSAGE_INVALID;
            }
        }
    }
    for (size_t i = 0U; i < subscriber_count; ++i) {
        subscriptions[i].owner = topic;
        subscriptions[i].capacity = subscriptions[i].storage_bytes / item_size;
        subscriptions[i].head = 0U;
        subscriptions[i].count = 0U;
    }
    *topic = (Core_Message_Topic){subscriptions, subscriber_count, item_size};
    return CORE_MESSAGE_OK;
}

Core_Message_Result Core_Message_Publish(
    Core_Message_Topic *topic, const void *item, size_t item_size)
{
    if ((topic == NULL) || (item == NULL)) {
        return CORE_MESSAGE_INVALID;
    }
    if (topic->subscriptions == NULL) {
        return CORE_MESSAGE_NOT_READY;
    }
    if (item_size != topic->item_size) {
        return CORE_MESSAGE_INVALID;
    }
    Core_Message_Port_Enter();
    for (size_t i = 0U; i < topic->subscriber_count; ++i) {
        const Core_Message_Subscription *sub = &topic->subscriptions[i];
        if ((sub->policy == CORE_MESSAGE_FIFO) &&
            (sub->count == sub->capacity)) {
            Core_Message_Port_Exit();
            return CORE_MESSAGE_FULL;
        }
    }
    for (size_t i = 0U; i < topic->subscriber_count; ++i) {
        Core_Message_Subscription *sub = &topic->subscriptions[i];
        size_t index = 0U;
        if (sub->policy == CORE_MESSAGE_FIFO) {
            /* Overflow-free (head + count) modulo capacity. */
            const size_t remaining = sub->capacity - sub->head;
            index = (sub->count >= remaining) ?
                (sub->count - remaining) : (sub->head + sub->count);
            ++sub->count;
        }
        else {
            sub->count = 1U;
        }
        memcpy((uint8_t *)sub->storage + index * item_size, item, item_size);
    }
    Core_Message_Port_Exit();
    return CORE_MESSAGE_OK;
}

static Core_Message_Result receive(
    Core_Message_Subscription *sub, void *item, size_t item_size, int consume)
{
    if ((sub == NULL) || (item == NULL)) {
        return CORE_MESSAGE_INVALID;
    }
    if (sub->owner == NULL) {
        return CORE_MESSAGE_NOT_READY;
    }
    if ((item_size != sub->owner->item_size) ||
        (!consume && (sub->policy != CORE_MESSAGE_LATEST))) {
        return CORE_MESSAGE_INVALID;
    }
    Core_Message_Port_Enter();
    if (sub->count == 0U) {
        Core_Message_Port_Exit();
        return CORE_MESSAGE_EMPTY;
    }
    memcpy(item, (const uint8_t *)sub->storage + sub->head * item_size,
        item_size);
    if (consume) {
        --sub->count;
        ++sub->head;
        if (sub->head == sub->capacity) {
            sub->head = 0U;
        }
    }
    Core_Message_Port_Exit();
    return CORE_MESSAGE_OK;
}

Core_Message_Result Core_Message_Take(
    Core_Message_Subscription *subscription, void *item, size_t item_size)
{
    return receive(subscription, item, item_size, 1);
}

Core_Message_Result Core_Message_Snapshot(
    Core_Message_Subscription *subscription, void *item, size_t item_size)
{
    return receive(subscription, item, item_size, 0);
}
