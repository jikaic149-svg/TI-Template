"""Fixed MaixCAM 2 UART binding: B0 TX, B1 RX, UART2."""

from vision_link import VisionBoardProfile


BOARD_PROFILE = VisionBoardProfile(
    name="MaixCAM 2",
    uart_device="/dev/ttyS2",
    baud_rate=115200,
    tx_pin="B0",
    rx_pin="B1",
    tx_function="UART2_TX",
    rx_function="UART2_RX",
)
