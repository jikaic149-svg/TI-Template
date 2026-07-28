#ifndef DEVICE_LORA_H
#define DEVICE_LORA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_serial.h"

#define DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE 3U

typedef enum {
    DEVICE_LORA_RESULT_OK = 0,
    DEVICE_LORA_RESULT_INVALID_ARGUMENT,
    DEVICE_LORA_RESULT_UNSUPPORTED,
    DEVICE_LORA_RESULT_BUSY,
    DEVICE_LORA_RESULT_TIMEOUT,
    DEVICE_LORA_RESULT_IO_ERROR,
} Device_LoRa_Result;

typedef enum {
    DEVICE_LORA_PAIR_SIDE_ADDRESS_7302 = 0,
    DEVICE_LORA_PAIR_SIDE_ADDRESS_1007,
} Device_LoRa_PairSide;

/* Expected settings already programmed into the external module. This
 * Device does not issue undocumented configuration or AT commands. */
typedef struct {
    uint32_t uart_baud_rate;
    uint32_t air_rate_bps;
    uint16_t encryption_key;
    uint8_t air_rate_level;
    int8_t tx_power_dbm;
    bool fixed_transmission;
    bool high_timeliness_mode;
    bool encryption_enabled;
} Device_LoRa_RadioProfile;

typedef struct {
    uint16_t address;
    uint8_t channel;
    uint32_t frequency_khz;
} Device_LoRa_EndpointProfile;

typedef struct {
    size_t transport_index;
    Device_LoRa_RadioProfile expected_radio;
    Device_LoRa_EndpointProfile local_endpoint;
    /* Raw bytes consumed by the module before each payload. The captured pair
     * was verified as [address high][address low][channel] over COM13/COM14. */
    uint8_t fixed_route_prefix[DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE];
    /* Caller-owned program-lifetime workspace reserved for this Device. A
     * payload passed to Send must not overlap it, so BUSY retries preserve the
     * caller's original payload. */
    uint8_t *tx_frame_buffer;
    size_t tx_frame_capacity;
} Device_LoRa_Config;

typedef struct {
    uint32_t software_overflow_count;
    uint32_t hardware_overrun_count;
    uint32_t framing_error_count;
    uint32_t parity_error_count;
    uint32_t dma_error_count;
    uint32_t sticky_errors;
    size_t buffered_bytes;
    size_t buffer_capacity;
} Device_LoRa_RxStatus;

typedef struct {
    Device_Serial serial;
    Device_LoRa_RadioProfile expected_radio;
    Device_LoRa_EndpointProfile local_endpoint;
    uint8_t fixed_route_prefix[DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE];
    uint8_t *tx_frame_buffer;
    size_t tx_frame_capacity;
    bool initialized;
} Device_LoRa;

extern const Device_LoRa_RadioProfile DEVICE_LORA_CAPTURED_RADIO_PROFILE;
extern const Device_LoRa_EndpointProfile DEVICE_LORA_ENDPOINT_7302;
extern const Device_LoRa_EndpointProfile DEVICE_LORA_ENDPOINT_1007;

/* Build one side of the captured two-module setup. Its route prefix
 * [destination address high][address low][channel] was verified by exact
 * bidirectional payload reception over the two physical modules. */
bool Device_LoRa_MakeCapturedPairConfig(
    Device_LoRa_PairSide local_side,
    size_t transport_index,
    uint8_t *tx_frame_buffer,
    size_t tx_frame_capacity,
    Device_LoRa_Config *config);

bool Device_LoRa_Init(Device_LoRa *lora, const Device_LoRa_Config *config);

/* OK means the complete route prefix and payload entered the MCU UART TX
 * queue. It does not mean that the RF peer received the payload. */
Device_LoRa_Result Device_LoRa_Send(
    Device_LoRa *lora, const uint8_t *payload, size_t payload_length);

/* Received bytes are the module's transparent UART payload stream. This
 * layer does not invent message boundaries, CRC, acknowledgement, or retry. */
Device_LoRa_Result Device_LoRa_ReadAvailable(
    Device_LoRa *lora,
    uint8_t *data,
    size_t capacity,
    size_t *transferred);

bool Device_LoRa_GetRxStatus(
    Device_LoRa *lora, Device_LoRa_RxStatus *status);
bool Device_LoRa_ClearRxStatus(Device_LoRa *lora);
bool Device_LoRa_Recover(Device_LoRa *lora);

#endif
