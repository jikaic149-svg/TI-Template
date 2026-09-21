#include "example_command_flow.h"
#include "bsp_led.h"
#include "bsp_uart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Exercise the actual Device code through a deterministic host BSP. */
struct BSP_UART_Resource { unsigned id; };
static const struct BSP_UART_Resource uart = {2U};
static char rx[256], tx[512];
static size_t rx_length, tx_length;
static bool led_on, fail_led, busy_tx;

size_t BSP_LED_Count(void) { return 1U; }
bool BSP_LED_Set(size_t index, bool on)
{
    if (index != 0U || fail_led) { return false; }
    led_on = on;
    return true;
}
BSP_UART_Handle BSP_UART_GetHandle(size_t index)
{
    return index == 2U ? &uart : NULL;
}
bool BSP_UART_GetInfo(BSP_UART_Handle handle, BSP_UART_Info *info)
{
    assert(handle == &uart);
    *info = (BSP_UART_Info){.baud_rate = 115200U,
        .supports_buffered_tx = true, .supports_buffered_rx = true,
        .tx_buffer_capacity = 512U, .rx_buffer_capacity = 127U};
    return true;
}
BSP_UART_Result BSP_UART_ReadAvailable(BSP_UART_Handle handle, uint8_t *data,
    size_t capacity, size_t *transferred)
{
    assert(handle == &uart);
    *transferred = rx_length < capacity ? rx_length : capacity;
    memcpy(data, rx, *transferred);
    rx_length -= *transferred;
    memmove(rx, rx + *transferred, rx_length);
    return BSP_UART_RESULT_OK;
}
BSP_UART_Result BSP_UART_WriteBuffered(BSP_UART_Handle handle,
    const uint8_t *data, size_t length)
{
    assert(handle == &uart);
    if (busy_tx) { return BSP_UART_RESULT_BUSY; }
    assert(tx_length + length < sizeof(tx));
    memcpy(tx + tx_length, data, length);
    tx_length += length;
    tx[tx_length] = '\0';
    return BSP_UART_RESULT_OK;
}
bool BSP_UART_GetRxStatus(BSP_UART_Handle handle, BSP_UART_RxStatus *status)
{
    (void)handle; (void)status; return false;
}
bool BSP_UART_ClearRxStatus(BSP_UART_Handle handle) { (void)handle; return false; }
bool BSP_UART_Recover(BSP_UART_Handle handle) { (void)handle; return false; }

static void input(const char *text)
{
    assert(rx_length == 0U && strlen(text) < sizeof(rx));
    rx_length = strlen(text);
    memcpy(rx, text, rx_length);
    tx_length = 0U;
    tx[0] = '\0';
}

int main(void)
{
    Example_CommandFlow_Status status;
    assert(Example_CommandFlow_Update(0U) == EXAMPLE_COMMAND_FLOW_NOT_READY);
    assert(Example_CommandFlow_Init());
    assert(!led_on);
    assert(Example_CommandFlow_TakeStatus(&status) == CORE_MESSAGE_EMPTY);
    input("LE");
    assert(Example_CommandFlow_Update(10U) == 0U && !led_on);
    input("D 1\r\n");
    assert(Example_CommandFlow_Update(20U) == 0U && led_on);
    assert(strcmp(tx, "OK LED 1\n") == 0);
    assert(Example_CommandFlow_TakeStatus(&status) == CORE_MESSAGE_OK);
    assert(status.sequence == 1U && status.timestamp_ms == 20U && status.applied);
    assert(Example_CommandFlow_TakeStatus(&status) == CORE_MESSAGE_EMPTY);
    input("LED 2\n");
    assert(Example_CommandFlow_Update(30U) == EXAMPLE_COMMAND_FLOW_REJECTED);
    assert(led_on && strcmp(tx, "ERR COMMAND\n") == 0);
    input("LED 0\nLED 1\nLED 0\n");
    assert(Example_CommandFlow_Update(40U) == EXAMPLE_COMMAND_FLOW_FULL);
    assert(!led_on); /* one command per tick, third explicitly rejected */
    assert(Example_CommandFlow_Update(50U) == 0U && led_on);
    assert(Example_CommandFlow_TakeStatus(&status) == CORE_MESSAGE_OK);
    assert(status.sequence == 3U); /* skipped older diagnostic snapshot */
    fail_led = true;
    input("LED 0\n");
    assert(Example_CommandFlow_Update(60U) == EXAMPLE_COMMAND_FLOW_DEVICE_ERROR);
    assert(led_on && strcmp(tx, "ERR DEVICE\n") == 0);
    assert(Example_CommandFlow_TakeStatus(&status) == CORE_MESSAGE_OK);
    assert(!status.applied && status.on);
    fail_led = false;
    busy_tx = true;
    input("LED 0\n");
    assert(Example_CommandFlow_Update(70U) == EXAMPLE_COMMAND_FLOW_TX_DROPPED);
    assert(!led_on && tx_length == 0U);
    busy_tx = false;
    assert(Example_CommandFlow_Update(80U) == 0U && tx_length == 0U); /* no retry */
    assert(Example_CommandFlow_TakeStatus(&status) == CORE_MESSAGE_OK);
    assert(status.applied && !status.on); /* serial and diagnostic copies independent */
    input("LED 0000000000000000000000\nLED 1\n");
    assert(Example_CommandFlow_Update(90U) == EXAMPLE_COMMAND_FLOW_REJECTED);
    assert(led_on); /* resumes only at a complete next line */
    puts("command_flow: UART -> service -> FIFO -> Task -> Device/BSP -> status -> UART passed");
    return 0;
}
