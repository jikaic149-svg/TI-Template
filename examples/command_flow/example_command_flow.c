#include "example_command_flow.h"

#include "core_config.h"
#include "device_led.h"
#include "device_serial.h"
#include "service_line.h"

#include <string.h>

typedef struct {
    uint32_t sequence;
    bool on;
} Example_Command;

/* FIFO holds discrete commands. Status is latest-value telemetry with an
 * independent copy for the serial service and the diagnostic consumer. */
static Example_Command g_commands[2];
static Example_CommandFlow_Status g_serial_status;
static Example_CommandFlow_Status g_monitor_status;
static Core_Message_Subscription g_command_sub = {
    .storage = g_commands,
    .storage_bytes = sizeof(g_commands),
    .policy = CORE_MESSAGE_FIFO,
};
static Core_Message_Subscription g_status_subs[] = {
    {.storage = &g_serial_status, .storage_bytes = sizeof(g_serial_status),
     .policy = CORE_MESSAGE_LATEST},
    {.storage = &g_monitor_status, .storage_bytes = sizeof(g_monitor_status),
     .policy = CORE_MESSAGE_LATEST},
};
static Core_Message_Topic g_command_topic;
static Core_Message_Topic g_status_topic;
static Service_Line g_line;
static char g_line_storage[16];
static uint8_t g_rx[TEMPLATE_EXAMPLE_COMMAND_RX_BUDGET];
static Device_Serial g_serial;
static Device_LED g_led;
static uint32_t g_sequence;
static bool g_applied_on;
static bool g_initialized;

static uint32_t send_text(const char *text)
{
    return (Device_Serial_WriteBuffered(&g_serial, (const uint8_t *)text,
        strlen(text)) == DEVICE_SERIAL_RESULT_OK) ? 0U :
        EXAMPLE_COMMAND_FLOW_TX_DROPPED;
}

/* Service adapter: validate the complete line and publish a typed command.
 * No device output or product-mode selection occurs in the receive path. */
static uint32_t service_receive(void)
{
    uint32_t events = 0U;
    size_t received = 0U;
    if (Device_Serial_ReadAvailable(&g_serial, g_rx, sizeof(g_rx),
        &received) != DEVICE_SERIAL_RESULT_OK) {
        return EXAMPLE_COMMAND_FLOW_IO_ERROR;
    }
    for (size_t i = 0U; i < received; ++i) {
        const Service_Line_Result parsed = Service_Line_Feed(&g_line, g_rx[i]);
        if (parsed == SERVICE_LINE_IN_PROGRESS) {
            continue;
        }
        if ((parsed != SERVICE_LINE_READY) ||
            ((strcmp(g_line_storage, "LED 0") != 0) &&
             (strcmp(g_line_storage, "LED 1") != 0))) {
            events |= EXAMPLE_COMMAND_FLOW_REJECTED | send_text("ERR COMMAND\n");
            continue;
        }
        const Example_Command command = {
            .sequence = g_sequence + UINT32_C(1),
            .on = g_line_storage[4] == '1',
        };
        const Core_Message_Result published = Core_Message_Publish(
            &g_command_topic, &command, sizeof(command));
        if (published == CORE_MESSAGE_OK) {
            g_sequence = command.sequence;
        }
        else {
            events |= EXAMPLE_COMMAND_FLOW_FULL | send_text("ERR FULL\n");
        }
    }
    return events;
}

/* Task step: exactly one command per cycle; this is the only LED writer.
 * A product needing control modes calls Mode/Algorithm here before Device. */
static uint32_t task_execute(uint32_t now_ms)
{
    Example_Command command;
    if (Core_Message_Take(&g_command_sub, &command, sizeof(command)) !=
        CORE_MESSAGE_OK) {
        return 0U;
    }
    const bool applied = Device_LED_Set(&g_led, command.on) ==
        DEVICE_LED_RESULT_OK;
    if (applied) {
        g_applied_on = command.on;
    }
    const Example_CommandFlow_Status status = {
        .sequence = command.sequence,
        .timestamp_ms = now_ms,
        .on = g_applied_on,
        .applied = applied,
    };
    const Core_Message_Result published = Core_Message_Publish(
        &g_status_topic, &status, sizeof(status));
    return (applied && (published == CORE_MESSAGE_OK)) ? 0U :
        EXAMPLE_COMMAND_FLOW_DEVICE_ERROR;
}

static uint32_t service_feedback(void)
{
    Example_CommandFlow_Status status;
    if (Core_Message_Take(&g_status_subs[0], &status, sizeof(status)) !=
        CORE_MESSAGE_OK) {
        return 0U;
    }
    return send_text(!status.applied ? "ERR DEVICE\n" :
        (status.on ? "OK LED 1\n" : "OK LED 0\n"));
}

bool Example_CommandFlow_Init(void)
{
    Device_LED_Config led_config;
    const Device_Serial_Config serial_config = {
        .transport_index = TEMPLATE_EXAMPLE_COMMAND_SERIAL_INDEX,
        .expected_baud_rate = TEMPLATE_EXAMPLE_COMMAND_BAUD_RATE,
        .minimum_tx_capacity = 16U,
        .minimum_rx_capacity = sizeof(g_rx),
        .require_buffered_tx = true,
        .require_buffered_rx = true,
    };
    if (g_initialized) {
        return true;
    }
    if (!Device_Serial_Init(&g_serial, &serial_config) ||
        !Device_LED_DefaultConfig(TEMPLATE_EXAMPLE_COMMAND_LED_INDEX,
            &led_config) || !Device_LED_Init(&g_led, &led_config) ||
        !Service_Line_Init(&g_line, g_line_storage, sizeof(g_line_storage)) ||
        (Core_Message_Init(&g_command_topic, sizeof(Example_Command),
            &g_command_sub, 1U) != CORE_MESSAGE_OK) ||
        (Core_Message_Init(&g_status_topic, sizeof(Example_CommandFlow_Status),
            g_status_subs, 2U) != CORE_MESSAGE_OK)) {
        return false;
    }
    g_initialized = true;
    return true;
}

uint32_t Example_CommandFlow_Update(uint32_t now_ms)
{
    if (!g_initialized) {
        return EXAMPLE_COMMAND_FLOW_NOT_READY;
    }
    uint32_t events = service_receive();
    events |= task_execute(now_ms);
    events |= service_feedback();
    return events;
}

Core_Message_Result Example_CommandFlow_TakeStatus(
    Example_CommandFlow_Status *status)
{
    return Core_Message_Take(&g_status_subs[1], status, sizeof(*status));
}
