#ifndef DEVICE_VISION_DEVICE_VISION_H
#define DEVICE_VISION_DEVICE_VISION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "algorithm_packet.h"
#include "algorithm_reliable_link.h"

#define DEVICE_VISION_CRITICAL_EVENT_QUEUE_CAPACITY 8U
#define DEVICE_VISION_ACK_FRAME_MAX_SIZE 29U
#define DEVICE_VISION_INTERFACE_VERSION_MAJOR 1U
#define DEVICE_VISION_INTERFACE_VERSION_MINOR 0U
#define DEVICE_VISION_WIRE_VERSION            1U

typedef enum {
    DEVICE_VISION_COLOR_UNKNOWN = 0,
    DEVICE_VISION_COLOR_RED = 1,
    DEVICE_VISION_COLOR_GREEN = 2,
    DEVICE_VISION_COLOR_BLUE = 3,
    DEVICE_VISION_COLOR_YELLOW = 4,
    DEVICE_VISION_COLOR_BLACK = 5,
    DEVICE_VISION_COLOR_WHITE = 6,
} Device_Vision_Color;

typedef enum {
    DEVICE_VISION_SHAPE_UNKNOWN = 0,
    DEVICE_VISION_SHAPE_CIRCLE = 1,
    DEVICE_VISION_SHAPE_TRIANGLE = 2,
    DEVICE_VISION_SHAPE_SQUARE = 3,
    DEVICE_VISION_SHAPE_RECTANGLE = 4,
} Device_Vision_Shape;

typedef struct {
    Device_Vision_Color color;
    Device_Vision_Shape shape;
    uint16_t context_id;
    uint16_t class_id;
    uint16_t confidence_per_mille;
    int16_t center_x;
    int16_t center_y;
    bool has_classification;
} Device_Vision_Result;

typedef enum {
    DEVICE_VISION_REQUEST_COLOR = 0,
    DEVICE_VISION_REQUEST_SHAPE = 1,
    DEVICE_VISION_REQUEST_BOTH = 2,
} Device_Vision_Request;

typedef enum {
    DEVICE_VISION_COMMAND_START = 0,
    DEVICE_VISION_COMMAND_END = 1,
    DEVICE_VISION_COMMAND_REQUEST = 2,
} Device_Vision_Command;

typedef enum {
    DEVICE_VISION_EVENT_REMOTE_START = 0,
    DEVICE_VISION_EVENT_REMOTE_END = 1,
    DEVICE_VISION_EVENT_REMOTE_REQUEST = 2,
    DEVICE_VISION_EVENT_REMOTE_CONTEXT = 3,
    DEVICE_VISION_EVENT_REMOTE_JUNCTION = 4,
    DEVICE_VISION_EVENT_REMOTE_DETECTION = 5,
    DEVICE_VISION_EVENT_REMOTE_SCAN_DONE = 6,
} Device_Vision_EventType;

typedef struct {
    Device_Vision_EventType type;
    uint32_t sender_session;
    uint16_t sequence;
    Device_Vision_Request request;
    uint16_t context_id;
    uint16_t context_value2;
    bool has_context_value2;
    uint8_t from_point;
    uint8_t to_point;
    uint8_t direction_slot;
    char letter;
    uint16_t center_x;
    uint16_t center_y;
    uint16_t left_angle_deci_degrees;
    uint16_t front_angle_deci_degrees;
    uint16_t right_angle_deci_degrees;
    uint16_t confidence_per_mille;
    uint16_t class_id;
    uint16_t letter_mask;
    Device_Vision_Shape shape;
} Device_Vision_Event;

typedef struct {
    uint32_t sender_session;
    uint32_t generation;
    uint16_t sequence;
    uint16_t lane_center_x;
    uint16_t lane_angle_deci_degrees;
    uint16_t junction_center_y;
    uint16_t confidence_per_mille;
    uint8_t from_point;
    uint8_t to_point;
} Device_Vision_TrackSnapshot;

typedef struct {
    uint32_t request_sender_session;
    uint16_t request_sequence;
} Device_Vision_Correlation;

typedef struct {
    Device_Vision_Result result;
    Device_Vision_Correlation correlation;
    uint32_t sender_session;
    uint32_t generation;
    uint16_t sequence;
    bool has_correlation;
} Device_Vision_ResultSnapshot;

typedef struct {
    Algorithm_ReliableLink_Config reliable_link;
    /* Required nonzero boot/session epoch. The application must supply a new
     * value after every local boot; DefaultConfig deliberately returns zero. */
    uint32_t local_session;
    uint16_t initial_critical_sequence;
    uint16_t initial_result_sequence;
} Device_Vision_Config;

typedef enum {
    DEVICE_VISION_FEED_IN_PROGRESS = 0,
    DEVICE_VISION_FEED_CRITICAL_EVENT_AND_ACK_READY = 1,
    DEVICE_VISION_FEED_DUPLICATE_ACK_READY = 2,
    DEVICE_VISION_FEED_RESULT_UPDATED = 3,
    DEVICE_VISION_FEED_ACK_ACCEPTED = 4,
    DEVICE_VISION_FEED_ACK_IGNORED = 5,
    DEVICE_VISION_FEED_FRAME_REJECTED = 6,
    DEVICE_VISION_FEED_SEMANTIC_REJECTED = 7,
    DEVICE_VISION_FEED_CRITICAL_QUEUE_FULL = 8,
    DEVICE_VISION_FEED_ACK_OUTPUT_TOO_SMALL = 9,
    DEVICE_VISION_FEED_INVALID_ARGUMENT = 10,
} Device_Vision_FeedResult;

typedef enum {
    DEVICE_VISION_BEGIN_OK = 0,
    DEVICE_VISION_BEGIN_BUSY = 1,
    DEVICE_VISION_BEGIN_INVALID_ARGUMENT = 2,
    DEVICE_VISION_BEGIN_FRAME_ERROR = 3,
} Device_Vision_BeginResult;

typedef enum {
    DEVICE_VISION_TX_IDLE = 0,
    DEVICE_VISION_TX_WAITING_FOR_ACK = 1,
    DEVICE_VISION_TX_IN_FLIGHT = 2,
    DEVICE_VISION_TX_SEND_INITIAL = 3,
    DEVICE_VISION_TX_SEND_RETRY = 4,
    DEVICE_VISION_TX_ACK_LATCHED = 5,
    DEVICE_VISION_TX_FAILED = 6,
    DEVICE_VISION_TX_INVALID_ARGUMENT = 7,
} Device_Vision_TxResult;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    uint16_t sequence;
} Device_Vision_TxView;

typedef struct {
    uint32_t packet_frames_accepted;
    uint32_t packet_frames_rejected;
    uint32_t semantic_frames_rejected;
    uint32_t critical_queue_full_count;
    uint32_t acknowledgements_sent;
    uint32_t acknowledgements_accepted;
    uint32_t acknowledgements_ignored;
    uint32_t duplicate_critical_frames;
    uint32_t result_snapshots_updated;
} Device_Vision_Stats;

/* Caller-owned single-context protocol state. UART ISR/DMA only moves bytes;
 * protocol APIs run in one serialized main/task context. */
typedef struct {
    Algorithm_Packet_Parser parser;
    uint8_t parser_storage[ALGORITHM_PACKET_PARSER_STORAGE_SIZE];
    Algorithm_ReliableLink critical_tx;
    Algorithm_ReliableLink_Receiver critical_rx;
    Device_Vision_Event
        critical_events[DEVICE_VISION_CRITICAL_EVENT_QUEUE_CAPACITY];
    Device_Vision_ResultSnapshot latest_result;
    Device_Vision_TrackSnapshot latest_track;
    Device_Vision_Stats stats;
    uint32_t local_session;
    uint16_t next_result_sequence;
    uint8_t critical_event_head;
    uint8_t critical_event_tail;
    uint8_t critical_event_count;
    bool has_latest_result;
    bool has_latest_track;
    bool initialized;
} Device_Vision;

Device_Vision_Config Device_Vision_DefaultConfig(void);
bool Device_Vision_Init(
    Device_Vision *vision, const Device_Vision_Config *config);

/* Feed one received byte and the byte's current monotonic millisecond time.
 * For *_ACK_READY, ack_output contains one complete LF frame. If submitted by
 * zero-copy DMA, the caller must keep that buffer alive and unchanged until
 * the DMA completion callback; do not reuse a stack/scratch buffer in flight.
 * ack_length is zero for every other result. Critical events are not ACKed
 * when their dedicated queue is full. RESULT never consumes that queue. */
Device_Vision_FeedResult Device_Vision_FeedByte(
    Device_Vision *vision,
    uint32_t now_ms,
    uint8_t byte,
    uint8_t *ack_output,
    size_t ack_capacity,
    size_t *ack_length);

Device_Vision_BeginResult Device_Vision_BeginCommand(
    Device_Vision *vision,
    Device_Vision_Command command,
    Device_Vision_Request request);

/* Sends a reliable, application-defined numeric context.  The Device layer
 * does not assign meaning to the value; a Task may use it for a waypoint,
 * workpiece slot, camera pose, or another protocol context. */
Device_Vision_BeginResult Device_Vision_BeginContext(
    Device_Vision *vision, uint16_t context_id);

/* Sends two reliable application-defined numeric context values.  This is
 * useful when the receiver needs both a waypoint and a measured pose. */
Device_Vision_BeginResult Device_Vision_BeginContextPair(
    Device_Vision *vision, uint16_t context_id, uint16_t context_value2);

Device_Vision_BeginResult Device_Vision_BeginRouteContext(
    Device_Vision *vision,
    uint16_t mode,
    uint16_t from_point,
    uint16_t to_point,
    uint16_t heading_sector);

/* Peek does not consume a send attempt. After UART returns OK, call Commit;
 * after blocking completion or exactly one DMA callback, call TxComplete. */
Device_Vision_TxResult Device_Vision_PeekCriticalTx(
    Device_Vision *vision,
    uint32_t now_ms,
    Device_Vision_TxView *view);
bool Device_Vision_CommitCriticalTxAccepted(
    Device_Vision *vision, uint32_t now_ms);
bool Device_Vision_CriticalTxComplete(Device_Vision *vision);
bool Device_Vision_CriticalTxOutstanding(const Device_Vision *vision);
bool Device_Vision_CriticalTxInFlight(const Device_Vision *vision);
bool Device_Vision_CancelCriticalTx(Device_Vision *vision);

/* A NULL correlation emits an unsolicited/latest telemetry RESULT. A non-null
 * correlation identifies the remote REQUEST event being answered and must
 * contain a nonzero request_sender_session. The caller owns output through any
 * zero-copy DMA completion. */
bool Device_Vision_EncodeResult(
    Device_Vision *vision,
    Device_Vision_Result result,
    const Device_Vision_Correlation *correlation,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

bool Device_Vision_ReadCriticalEvent(
    Device_Vision *vision, Device_Vision_Event *event);
bool Device_Vision_GetLatestResult(
    const Device_Vision *vision, Device_Vision_ResultSnapshot *snapshot);
bool Device_Vision_GetLatestTrack(
    const Device_Vision *vision, Device_Vision_TrackSnapshot *snapshot);
bool Device_Vision_GetStats(
    const Device_Vision *vision, Device_Vision_Stats *stats);
void Device_Vision_ClearCriticalEvents(Device_Vision *vision);

const char *Device_Vision_ColorName(Device_Vision_Color color);
const char *Device_Vision_ShapeName(Device_Vision_Shape shape);

#endif
