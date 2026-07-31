#ifndef DEVICE_PEER_LINK_H
#define DEVICE_PEER_LINK_H

#include "algorithm_packet.h"
#include "bsp_uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_PEER_LINK_BAUD_RATE UINT32_C(115200)
#define DEVICE_PEER_LINK_RX_CHUNK_CAPACITY ALGORITHM_PACKET_MAX_FRAME_SIZE
#define DEVICE_PEER_LINK_TX_PAYLOAD_CAPACITY 64U

#define DEVICE_PEER_LINK_CAR_SPEED_ABS_MAX_MM_PER_SECOND INT32_C(5000)
#define DEVICE_PEER_LINK_CAR_ACCEL_ABS_MAX_MM_PER_SECOND2 INT32_C(50000)
#define DEVICE_PEER_LINK_CAR_PLAN_ACCEL_ABS_MAX_MM_PER_SECOND2 INT32_C(5000)
#define DEVICE_PEER_LINK_CAR_PITCH_RATE_ABS_MAX_CENTI_DPS INT32_C(100000)
#define DEVICE_PEER_LINK_COMPETITION_MODE_MIN UINT8_C(2)
#define DEVICE_PEER_LINK_COMPETITION_MODE_MAX UINT8_C(6)
#define DEVICE_PEER_LINK_BALL_TARGET_ABS_MAX_0P1_MM INT32_C(1250)
#define DEVICE_PEER_LINK_DEBUG_STATUS_MASK UINT16_C(0x01FF)

typedef enum {
    DEVICE_PEER_LINK_SEND_QUEUED = 0,
    DEVICE_PEER_LINK_SEND_BUSY,
    DEVICE_PEER_LINK_SEND_INVALID_ARGUMENT,
    DEVICE_PEER_LINK_SEND_ERROR,
} Device_PeerLink_SendResult;

/* G3519(node 2) -> F407(node 1) latest chassis sample. Acceleration and
 * angular rate already use the Device_IMU body frame: +X forward, +Y left,
 * +Z up. The current physical sensor-to-body mapping remains a product
 * mounting fact and must be confirmed on hardware. */
typedef struct {
    uint32_t sample_ms;
    int32_t speed_x_mm_per_second;
    int32_t acceleration_x_mm_per_second2;
    int32_t acceleration_z_mm_per_second2;
    int32_t pitch_rate_centi_dps;
    int32_t planned_acceleration_x_mm_per_second2;
    uint8_t competition_mode;
    bool competition_task_active;
    bool valid;
    bool planned_acceleration_valid;
    uint32_t received_at_ms;
} Device_PeerLink_CarState;

/* F407(node 1) -> G3519(node 2) fixed-point debug Latest Value. */
typedef struct {
    int32_t chassis_acceleration_mm_s2;
    int32_t chassis_compensation_mdeg;
    int32_t rod_command_mdeg;
    uint16_t motor_target_raw;
    uint16_t motor_feedback_raw;
    int32_t motor_command_current_ma;
    int32_t motor_feedback_current_ma;
    int32_t motor_speed_centi_rpm;
    uint8_t control_phase;
    uint16_t status_flags;
    uint32_t received_at_ms;
} Device_PeerLink_DebugTelemetry;

typedef struct {
    size_t transport_index;
    uint8_t local_node_id;
    uint8_t remote_node_id;
    uint32_t heartbeat_period_ms;
    uint32_t offline_timeout_ms;
    size_t maximum_rx_bytes_per_step;
} Device_PeerLink_Config;

typedef struct {
    bool online;
    uint32_t last_heartbeat_rx_ms;
    uint32_t last_remote_uptime_ms;
    uint32_t heartbeat_tx_count;
    uint32_t heartbeat_tx_drop_count;
    uint32_t heartbeat_tx_error_count;
    uint32_t heartbeat_rx_count;
    uint32_t packet_parser_error_count;
    uint32_t heartbeat_payload_error_count;
    uint32_t heartbeat_source_error_count;
    uint32_t duplicate_sequence_count;
    uint32_t car_state_tx_count;
    uint32_t car_state_tx_drop_count;
    uint32_t car_state_tx_error_count;
    uint32_t car_state_rx_count;
    uint32_t car_state_payload_error_count;
    uint32_t car_state_source_error_count;
    uint32_t car_state_duplicate_sequence_count;
    uint32_t car_state_sample_time_error_count;
    uint32_t ball_target_tx_count;
    uint32_t ball_target_tx_busy_count;
    uint32_t ball_target_tx_error_count;
    uint32_t ball_stop_tx_count;
    uint32_t ball_stop_tx_busy_count;
    uint32_t ball_stop_tx_error_count;
    uint32_t debug_rx_count;
    uint32_t debug_payload_error_count;
    uint32_t debug_source_error_count;
    uint32_t debug_duplicate_sequence_count;
    uint32_t debug_old_sequence_count;
    uint32_t step_error_count;
    BSP_UART_RxStatus uart_rx;
} Device_PeerLink_Status;

/* One Task owns each instance. UART/DMA ISR only fills/drains BSP rings;
 * protocol parsing and online-state updates happen in Step(). */
typedef struct {
    Device_PeerLink_Config config;
    Device_PeerLink_Status status;
    BSP_UART_Handle uart;
    Algorithm_Packet_Parser parser;
    uint8_t parser_storage[ALGORITHM_PACKET_PARSER_STORAGE_SIZE];
    uint8_t rx_chunk[DEVICE_PEER_LINK_RX_CHUNK_CAPACITY];
    uint8_t tx_payload[DEVICE_PEER_LINK_TX_PAYLOAD_CAPACITY];
    uint8_t tx_frame[ALGORITHM_PACKET_MAX_FRAME_SIZE];
    Device_PeerLink_CarState remote_car_state;
    Device_PeerLink_DebugTelemetry remote_debug_telemetry;
    uint32_t next_heartbeat_tx_ms;
    uint16_t heartbeat_tx_sequence;
    uint16_t car_state_tx_sequence;
    uint16_t ball_target_tx_sequence;
    uint16_t ball_stop_tx_sequence;
    uint16_t last_heartbeat_rx_sequence;
    uint16_t last_car_state_rx_sequence;
    uint16_t last_debug_rx_sequence;
    uint32_t last_car_state_sample_ms;
    bool heartbeat_rx_sequence_seen;
    bool car_state_rx_sequence_seen;
    bool car_state_sample_time_seen;
    bool remote_car_state_seen;
    bool debug_rx_sequence_seen;
    bool remote_debug_telemetry_seen;
    bool initialized;
} Device_PeerLink;

bool Device_PeerLink_Init(
    Device_PeerLink *link,
    const Device_PeerLink_Config *config,
    uint32_t now_ms);

/* Non-blocking periodic service. A false result records a local API/transport
 * error but never stops other product functions; call again next Task cycle. */
bool Device_PeerLink_Step(Device_PeerLink *link, uint32_t now_ms);

/* Queue one G3519 chassis sample. A valid planned acceleration appends the
 * optional ninth/tenth CAR fields; otherwise the legacy eight-field frame is
 * retained. BUSY drops the complete frame without advancing its sequence. */
bool Device_PeerLink_SendCarState(
    Device_PeerLink *link,
    const Device_PeerLink_CarState *state);

/* Queue one G3519 -> F407 ball target in 0.1 mm. The protocol coding domain
 * is inclusive +/-125.0 mm; a product Task may impose a narrower reachable
 * range. BUSY means no bytes were queued and the independent sequence did not
 * advance, so the Task may perform its own explicitly bounded retry. */
Device_PeerLink_SendResult Device_PeerLink_SendBallTarget(
    Device_PeerLink *link,
    int32_t target_0p1mm);

/* Queue one G3519 -> F407 request to end ball control with 0 A then Disable.
 * This is independent from chassis stopping and does not imply an ACK from
 * F407. BUSY leaves the independent sequence unchanged. */
Device_PeerLink_SendResult Device_PeerLink_SendBallStop(
    Device_PeerLink *link);

/* Copy the latest syntactically valid CAR frame. Local receive age, not the
 * remote sample clock, owns freshness; callers separately honor state.valid. */
bool Device_PeerLink_GetRemoteCarState(
    const Device_PeerLink *link,
    uint32_t now_ms,
    uint32_t maximum_age_ms,
    Device_PeerLink_CarState *state);

/* Copy the latest valid DBG sample while its local receive age is within the
 * inclusive maximum. Stale or never-seen data returns false. */
bool Device_PeerLink_GetRemoteDebugTelemetry(
    const Device_PeerLink *link,
    uint32_t now_ms,
    uint32_t maximum_age_ms,
    Device_PeerLink_DebugTelemetry *telemetry);

#endif
