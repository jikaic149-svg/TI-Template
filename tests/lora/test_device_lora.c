#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device_lora.h"

static Device_Serial_Config g_last_serial_config;
static Device_Serial_Result g_write_result;
static Device_Serial_Result g_read_result;
static uint8_t g_written[64U];
static size_t g_written_length;
static uint8_t g_read_bytes[8U];
static size_t g_read_length;
static bool g_serial_init_ok;
static bool g_clear_ok;
static bool g_recover_ok;

bool Device_Serial_Init(
    Device_Serial *serial, const Device_Serial_Config *config)
{
    if ((serial == NULL) || (config == NULL) || !g_serial_init_ok) {
        return false;
    }
    g_last_serial_config = *config;
    serial->backend = serial;
    serial->initialized = true;
    return true;
}

Device_Serial_Result Device_Serial_WriteBuffered(
    Device_Serial *serial, const uint8_t *data, size_t length)
{
    assert(serial != NULL);
    assert(data != NULL);
    assert(length <= sizeof(g_written));
    memcpy(g_written, data, length);
    g_written_length = length;
    return g_write_result;
}

Device_Serial_Result Device_Serial_ReadAvailable(
    Device_Serial *serial,
    uint8_t *data,
    size_t capacity,
    size_t *transferred)
{
    size_t copied;

    assert(serial != NULL);
    if (g_read_result != DEVICE_SERIAL_RESULT_OK) {
        return g_read_result;
    }
    assert(transferred != NULL);
    copied = (g_read_length < capacity) ? g_read_length : capacity;
    if (copied != 0U) {
        assert(data != NULL);
        memcpy(data, g_read_bytes, copied);
    }
    *transferred = copied;
    return DEVICE_SERIAL_RESULT_OK;
}

bool Device_Serial_GetRxStatus(
    Device_Serial *serial, Device_Serial_RxStatus *status)
{
    assert(serial != NULL);
    assert(status != NULL);
    *status = (Device_Serial_RxStatus){
        .software_overflow_count = 1U,
        .hardware_overrun_count = 2U,
        .framing_error_count = 3U,
        .parity_error_count = 4U,
        .dma_error_count = 5U,
        .sticky_errors = DEVICE_SERIAL_RX_ERROR_FRAMING,
        .buffered_bytes = 6U,
        .buffer_capacity = 127U,
    };
    return true;
}

bool Device_Serial_ClearRxStatus(Device_Serial *serial)
{
    assert(serial != NULL);
    return g_clear_ok;
}

bool Device_Serial_Recover(Device_Serial *serial)
{
    assert(serial != NULL);
    return g_recover_ok;
}

static void reset_stub(void)
{
    g_last_serial_config = (Device_Serial_Config){0};
    g_write_result = DEVICE_SERIAL_RESULT_OK;
    g_read_result = DEVICE_SERIAL_RESULT_OK;
    g_written_length = 0U;
    g_read_length = 0U;
    g_serial_init_ok = true;
    g_clear_ok = true;
    g_recover_ok = true;
}

static void test_captured_profiles(void)
{
    Device_LoRa_Config config;
    uint8_t workspace[32U];

    assert(DEVICE_LORA_CAPTURED_RADIO_PROFILE.uart_baud_rate == 115200U);
    assert(DEVICE_LORA_CAPTURED_RADIO_PROFILE.air_rate_level == 2U);
    assert(DEVICE_LORA_CAPTURED_RADIO_PROFILE.air_rate_bps == 2148U);
    assert(DEVICE_LORA_CAPTURED_RADIO_PROFILE.tx_power_dbm == 22);
    assert(DEVICE_LORA_CAPTURED_RADIO_PROFILE.encryption_enabled);
    assert(DEVICE_LORA_CAPTURED_RADIO_PROFILE.encryption_key == 12345U);
    assert(DEVICE_LORA_ENDPOINT_7302.address == 0x7302U);
    assert(DEVICE_LORA_ENDPOINT_7302.channel == 0x4AU);
    assert(DEVICE_LORA_ENDPOINT_7302.frequency_khz == 507150U);
    assert(DEVICE_LORA_ENDPOINT_1007.address == 0x1007U);
    assert(DEVICE_LORA_ENDPOINT_1007.channel == 0x30U);
    assert(Device_LoRa_MakeCapturedPairConfig(
        DEVICE_LORA_PAIR_SIDE_ADDRESS_7302, 3U,
        workspace, sizeof(workspace), &config));
    assert(config.local_endpoint.address == 0x7302U);
    assert(config.fixed_route_prefix[0] == 0x10U);
    assert(config.fixed_route_prefix[1] == 0x07U);
    assert(config.fixed_route_prefix[2] == 0x30U);

    assert(Device_LoRa_MakeCapturedPairConfig(
        DEVICE_LORA_PAIR_SIDE_ADDRESS_1007, 4U,
        workspace, sizeof(workspace), &config));
    assert(config.local_endpoint.address == 0x1007U);
    assert(config.fixed_route_prefix[0] == 0x73U);
    assert(config.fixed_route_prefix[1] == 0x02U);
    assert(config.fixed_route_prefix[2] == 0x4AU);
    assert(!Device_LoRa_MakeCapturedPairConfig(
        (Device_LoRa_PairSide)99, 0U,
        workspace, sizeof(workspace), &config));
}

static void test_init_send_and_busy(void)
{
    static const uint8_t payload[] = {0x41U, 0x42U, 0x43U};
    static const uint8_t expected[] = {
        0x10U, 0x07U, 0x30U, 0x41U, 0x42U, 0x43U,
    };
    Device_LoRa_Config config;
    Device_LoRa lora;
    uint8_t workspace[32U];

    reset_stub();
    assert(Device_LoRa_MakeCapturedPairConfig(
        DEVICE_LORA_PAIR_SIDE_ADDRESS_7302, 3U,
        workspace, sizeof(workspace), &config));
    assert(Device_LoRa_Init(&lora, &config));
    assert(g_last_serial_config.transport_index == 3U);
    assert(g_last_serial_config.expected_baud_rate == 115200U);
    assert(g_last_serial_config.minimum_tx_capacity == sizeof(workspace));
    assert(g_last_serial_config.minimum_rx_capacity == 1U);
    assert(g_last_serial_config.require_buffered_tx);
    assert(g_last_serial_config.require_buffered_rx);

    assert(Device_LoRa_Send(&lora, payload, sizeof(payload)) ==
        DEVICE_LORA_RESULT_OK);
    assert(g_written_length == sizeof(expected));
    assert(memcmp(g_written, expected, sizeof(expected)) == 0);

    g_write_result = DEVICE_SERIAL_RESULT_BUSY;
    assert(Device_LoRa_Send(&lora, payload, sizeof(payload)) ==
        DEVICE_LORA_RESULT_BUSY);
    assert(Device_LoRa_Send(&lora, NULL, sizeof(payload)) ==
        DEVICE_LORA_RESULT_INVALID_ARGUMENT);
    assert(Device_LoRa_Send(&lora, payload, 0U) ==
        DEVICE_LORA_RESULT_INVALID_ARGUMENT);
    assert(Device_LoRa_Send(&lora, payload, sizeof(workspace)) ==
        DEVICE_LORA_RESULT_INVALID_ARGUMENT);
}

static void test_workspace_overlap_rejected_and_reverse_route(void)
{
    static const uint8_t payload[] = {0xA1U, 0xB2U};
    static const uint8_t expected[] = {
        0x73U, 0x02U, 0x4AU, 0xA1U, 0xB2U,
    };
    Device_LoRa_Config config;
    Device_LoRa lora;
    uint8_t workspace[16U] = {0xA1U, 0xB2U};

    reset_stub();
    assert(Device_LoRa_MakeCapturedPairConfig(
        DEVICE_LORA_PAIR_SIDE_ADDRESS_1007, 1U,
        workspace, sizeof(workspace), &config));
    assert(Device_LoRa_Init(&lora, &config));
    assert(Device_LoRa_Send(&lora, workspace, 2U) ==
        DEVICE_LORA_RESULT_INVALID_ARGUMENT);
    assert(Device_LoRa_Send(&lora, payload, sizeof(payload)) ==
        DEVICE_LORA_RESULT_OK);
    assert(g_written_length == sizeof(expected));
    assert(memcmp(g_written, expected, sizeof(expected)) == 0);
}

static void test_receive_status_and_recovery(void)
{
    Device_LoRa_Config config;
    Device_LoRa_RxStatus status;
    Device_LoRa lora;
    uint8_t workspace[16U];
    uint8_t received[8U];
    size_t received_length = 99U;

    reset_stub();
    assert(Device_LoRa_MakeCapturedPairConfig(
        DEVICE_LORA_PAIR_SIDE_ADDRESS_7302, 0U,
        workspace, sizeof(workspace), &config));
    assert(Device_LoRa_Init(&lora, &config));
    g_read_bytes[0] = 0x11U;
    g_read_bytes[1] = 0x22U;
    g_read_length = 2U;
    assert(Device_LoRa_ReadAvailable(
        &lora, received, sizeof(received), &received_length) ==
        DEVICE_LORA_RESULT_OK);
    assert(received_length == 2U);
    assert(received[0] == 0x11U);
    assert(received[1] == 0x22U);

    assert(Device_LoRa_GetRxStatus(&lora, &status));
    assert(status.software_overflow_count == 1U);
    assert(status.hardware_overrun_count == 2U);
    assert(status.framing_error_count == 3U);
    assert(status.parity_error_count == 4U);
    assert(status.dma_error_count == 5U);
    assert(status.sticky_errors == DEVICE_SERIAL_RX_ERROR_FRAMING);
    assert(status.buffered_bytes == 6U);
    assert(status.buffer_capacity == 127U);
    assert(Device_LoRa_ClearRxStatus(&lora));
    assert(Device_LoRa_Recover(&lora));
}

static void test_init_failures_leave_device_uninitialized(void)
{
    static const uint8_t byte = 0x55U;
    Device_LoRa_Config config;
    Device_LoRa lora;
    uint8_t workspace[16U];

    reset_stub();
    assert(Device_LoRa_MakeCapturedPairConfig(
        DEVICE_LORA_PAIR_SIDE_ADDRESS_7302, 0U,
        workspace, sizeof(workspace), &config));
    g_serial_init_ok = false;
    assert(!Device_LoRa_Init(&lora, &config));
    assert(!lora.initialized);
    assert(Device_LoRa_Send(&lora, &byte, 1U) ==
        DEVICE_LORA_RESULT_INVALID_ARGUMENT);

    config.tx_frame_capacity = DEVICE_LORA_FIXED_ROUTE_PREFIX_SIZE;
    assert(!Device_LoRa_Init(&lora, &config));
    assert(!lora.initialized);
}

int main(void)
{
    test_captured_profiles();
    test_init_send_and_busy();
    test_workspace_overlap_rejected_and_reverse_route();
    test_receive_status_and_recovery();
    test_init_failures_leave_device_uninitialized();
    puts("LoRa Device tests: PASS");
    return 0;
}
