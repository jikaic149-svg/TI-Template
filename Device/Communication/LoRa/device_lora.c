#include "device_lora.h"

#include <string.h>

const Device_LoRa_RadioProfile DEVICE_LORA_CAPTURED_RADIO_PROFILE = {
    .uart_baud_rate = UINT32_C(115200),
    .air_rate_bps = UINT32_C(2148),
    .encryption_key = UINT16_C(12345),
    .air_rate_level = UINT8_C(2),
    .tx_power_dbm = INT8_C(22),
    .fixed_transmission = true,
    .high_timeliness_mode = true,
    .encryption_enabled = true,
};

const Device_LoRa_EndpointProfile DEVICE_LORA_ENDPOINT_7302 = {
    .address = UINT16_C(0x7302),
    .channel = UINT8_C(0x4A),
    .frequency_khz = UINT32_C(507150),
};

const Device_LoRa_EndpointProfile DEVICE_LORA_ENDPOINT_1007 = {
    .address = UINT16_C(0x1007),
    .channel = UINT8_C(0x30),
    .frequency_khz = UINT32_C(481150),
};

static Device_LoRa_Result map_serial_result(Device_Serial_Result result)
{
    switch (result) {
    case DEVICE_SERIAL_RESULT_OK:
        return DEVICE_LORA_RESULT_OK;
    case DEVICE_SERIAL_RESULT_INVALID_ARGUMENT:
        return DEVICE_LORA_RESULT_INVALID_ARGUMENT;
    case DEVICE_SERIAL_RESULT_UNSUPPORTED:
        return DEVICE_LORA_RESULT_UNSUPPORTED;
    case DEVICE_SERIAL_RESULT_BUSY:
        return DEVICE_LORA_RESULT_BUSY;
    case DEVICE_SERIAL_RESULT_TIMEOUT:
        return DEVICE_LORA_RESULT_TIMEOUT;
    case DEVICE_SERIAL_RESULT_IO_ERROR:
    default:
        return DEVICE_LORA_RESULT_IO_ERROR;
    }
}

static bool initialized(const Device_LoRa *lora)
{
    return (lora != NULL) && lora->initialized;
}

static bool payload_overlaps_workspace(
    const Device_LoRa *lora,
    const uint8_t *payload,
    size_t payload_length)
{
    const uintptr_t payload_begin = (uintptr_t)payload;
    const uintptr_t workspace_begin = (uintptr_t)lora->tx_frame_buffer;

    if ((payload_length > (size_t)(UINTPTR_MAX - payload_begin)) ||
        (lora->tx_frame_capacity >
            (size_t)(UINTPTR_MAX - workspace_begin))) {
        return true;
    }
    return (payload_begin <
            (workspace_begin + lora->tx_frame_capacity)) &&
        (workspace_begin < (payload_begin + payload_length));
}

bool Device_LoRa_MakeCapturedPairConfig(
    Device_LoRa_PairSide local_side,
    size_t transport_index,
    uint8_t *tx_frame_buffer,
    size_t tx_frame_capacity,
    Device_LoRa_Config *config)
{
    const Device_LoRa_EndpointProfile *local;
    const Device_LoRa_EndpointProfile *peer;

    if (config != NULL) {
        *config = (Device_LoRa_Config){0};
    }
    if ((config == NULL) ||
        ((local_side != DEVICE_LORA_PAIR_SIDE_ADDRESS_7302) &&
         (local_side != DEVICE_LORA_PAIR_SIDE_ADDRESS_1007))) {
        return false;
    }

    if (local_side == DEVICE_LORA_PAIR_SIDE_ADDRESS_7302) {
        local = &DEVICE_LORA_ENDPOINT_7302;
        peer = &DEVICE_LORA_ENDPOINT_1007;
    }
    else {
        local = &DEVICE_LORA_ENDPOINT_1007;
        peer = &DEVICE_LORA_ENDPOINT_7302;
    }

    *config = (Device_LoRa_Config){
        .transport_index = transport_index,
        .expected_radio = DEVICE_LORA_CAPTURED_RADIO_PROFILE,
        .local_endpoint = *local,
        .fixed_route_prefix = {
            (uint8_t)(peer->address >> 8U),
            (uint8_t)peer->address,
            peer->channel,
        },
        .tx_frame_buffer = tx_frame_buffer,
        .tx_frame_capacity = tx_frame_capacity,
    };
    return true;
}

bool Device_LoRa_Init(Device_LoRa *lora, const Device_LoRa_Config *config)
{
    Device_Serial_Config serial_config;

    if (lora != NULL) {
        *lora = (Device_LoRa){0};
    }
    if ((lora == NULL) || (config == NULL) ||
        (config->expected_radio.uart_baud_rate == 0U) ||
        (config->tx_frame_buffer == NULL) ||
        (config->tx_frame_capacity <= DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE)) {
        return false;
    }

    serial_config = (Device_Serial_Config){
        .transport_index = config->transport_index,
        .expected_baud_rate = config->expected_radio.uart_baud_rate,
        .minimum_tx_capacity = config->tx_frame_capacity,
        .minimum_rx_capacity = 1U,
        .require_buffered_tx = true,
        .require_buffered_rx = true,
    };
    if (!Device_Serial_Init(&lora->serial, &serial_config)) {
        return false;
    }

    lora->expected_radio = config->expected_radio;
    lora->local_endpoint = config->local_endpoint;
    memcpy(lora->fixed_route_prefix, config->fixed_route_prefix,
        sizeof(lora->fixed_route_prefix));
    lora->tx_frame_buffer = config->tx_frame_buffer;
    lora->tx_frame_capacity = config->tx_frame_capacity;
    lora->initialized = true;
    return true;
}

Device_LoRa_Result Device_LoRa_Send(
    Device_LoRa *lora, const uint8_t *payload, size_t payload_length)
{
    if (!initialized(lora) || (payload == NULL) ||
        (payload_length == 0U) ||
        (payload_length >
            (lora->tx_frame_capacity -
             DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE)) ||
        payload_overlaps_workspace(lora, payload, payload_length)) {
        return DEVICE_LORA_RESULT_INVALID_ARGUMENT;
    }

    memcpy(&lora->tx_frame_buffer[DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE],
        payload, payload_length);
    memcpy(lora->tx_frame_buffer, lora->fixed_route_prefix,
        sizeof(lora->fixed_route_prefix));
    return map_serial_result(Device_Serial_WriteBuffered(
        &lora->serial,
        lora->tx_frame_buffer,
        DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE + payload_length));
}

Device_LoRa_Result Device_LoRa_ReadAvailable(
    Device_LoRa *lora,
    uint8_t *data,
    size_t capacity,
    size_t *transferred)
{
    if (!initialized(lora)) {
        return DEVICE_LORA_RESULT_INVALID_ARGUMENT;
    }
    return map_serial_result(Device_Serial_ReadAvailable(
        &lora->serial, data, capacity, transferred));
}

bool Device_LoRa_GetRxStatus(
    Device_LoRa *lora, Device_LoRa_RxStatus *status)
{
    Device_Serial_RxStatus source;

    if (!initialized(lora) || (status == NULL) ||
        !Device_Serial_GetRxStatus(&lora->serial, &source)) {
        return false;
    }
    *status = (Device_LoRa_RxStatus){
        .software_overflow_count = source.software_overflow_count,
        .hardware_overrun_count = source.hardware_overrun_count,
        .framing_error_count = source.framing_error_count,
        .parity_error_count = source.parity_error_count,
        .dma_error_count = source.dma_error_count,
        .sticky_errors = source.sticky_errors,
        .buffered_bytes = source.buffered_bytes,
        .buffer_capacity = source.buffer_capacity,
    };
    return true;
}

bool Device_LoRa_ClearRxStatus(Device_LoRa *lora)
{
    return initialized(lora) && Device_Serial_ClearRxStatus(&lora->serial);
}

bool Device_LoRa_Recover(Device_LoRa *lora)
{
    return initialized(lora) && Device_Serial_Recover(&lora->serial);
}
