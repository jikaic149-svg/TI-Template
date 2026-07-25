"""Fixed MaixCAM Pro UART binding: A16 TX, A17 RX, UART0."""

from vision_link import VisionBoardProfile


BOARD_PROFILE = VisionBoardProfile(
    name="MaixCAM Pro",
    uart_device="/dev/ttyS0",
    baud_rate=115200,
    tx_pin="A16",
    rx_pin="A17",
    tx_function="UART0_TX",
    rx_function="UART0_RX",
)

