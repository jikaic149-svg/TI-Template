#ifndef CORE_MESSAGE_H
#define CORE_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CORE_MESSAGE_OK = 0,
    CORE_MESSAGE_EMPTY,
    CORE_MESSAGE_FULL,
    CORE_MESSAGE_INVALID,
    CORE_MESSAGE_NOT_READY
} Core_Message_Result;

typedef enum {
    CORE_MESSAGE_FIFO = 0,
    CORE_MESSAGE_LATEST
} Core_Message_Policy;

typedef struct Core_Message_Topic Core_Message_Topic;

/* Configuration and storage are caller-owned for the entire topic lifetime.
 * Zero-initialize before Init; do not copy/mutate a live subscription. */
typedef struct {
    void *storage;
    size_t storage_bytes;
    Core_Message_Policy policy;
    /* Private runtime state. */
    Core_Message_Topic *owner;
    size_t capacity;
    size_t head;
    size_t count;
} Core_Message_Subscription;

struct Core_Message_Topic {
    Core_Message_Subscription *subscriptions;
    size_t subscriber_count;
    size_t item_size;
};

/* Startup only, before any concurrent access. Each subscriber owns separate
 * storage. LATEST requires exactly one item; FIFO requires an integral number
 * of items. Init is transactional and a live topic cannot be reinitialized. */
Core_Message_Result Core_Message_Init(
    Core_Message_Topic *topic, size_t item_size,
    Core_Message_Subscription *subscriptions, size_t subscriber_count);

/* Task context only. Nonblocking, copy-based, no callbacks or allocation.
 * FULL leaves ALL subscribers unchanged, including LATEST subscribers.
 * OK means copied to subscribers, not executed by their consumers.
 * Payload/output must not alias any channel storage or control structures. */
Core_Message_Result Core_Message_Publish(
    Core_Message_Topic *topic, const void *item, size_t item_size);
Core_Message_Result Core_Message_Take(
    Core_Message_Subscription *subscription, void *item, size_t item_size);

/* Non-destructive snapshot of a LATEST subscriber only. EMPTY until first
 * publish (or after Take). Snapshot does not acknowledge freshness. */
Core_Message_Result Core_Message_Snapshot(
    Core_Message_Subscription *subscription, void *item, size_t item_size);

#endif
