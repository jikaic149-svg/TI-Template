# Communicate Mode

纯通信策略层，不访问 UART、BSP、Device、FreeRTOS、Task 或跨模块全局变量。

- `BLUETOOTH_MODE_PC`：电脑通信，使用原始字节流；具体上层格式由产品 Task 决定。
- `BLUETOOTH_MODE_DUAL_MCU`：双 MCU 通信，使用 Packet V1、周期 `HELLO` 和节点
  身份。
- `Mode_Communicate_LoRaHalfDuplex`：LoRa 点对点半双工轮次。两端以互补的初始
  `LOCAL/PEER` 轮次启动；本端发送成功后交出轮次，确认收到一帧完整有效消息后取得
  轮次。

目录与其他 Mode 一致：每个具体模式一个实现文件，公共类型和 API 统一从
`include/mode_communicate.h` 导出。

- `Bluetooth/mode_communicate_bluetooth_pc.c`：实现 PC 模式的 `Init/Update`。
- `Bluetooth/mode_communicate_bluetooth_dual_mcu.c`：实现双 MCU 模式的
  `Init/Update`。
- `LoRa/mode_communicate_lora_half_duplex.c`：实现不带自动重发或超时猜测的双端
  交替收发。

Task 负责选择并调用其中一个模式、持有唯一 UART 实例、初始化对应协议并执行收发；
本组件不进行模式选择或运行期切换，也不定义 ACK、重发或控制命令语义。

Bluetooth 的既有实现只表示软件不对 TX/RX 做互斥仲裁。MCU UART 有独立 TX/RX，
但这不等于外部无线 PHY 已被证明物理全双工，因此公共字段使用
`MODE_COMMUNICATE_DUPLEX_UNRESTRICTED`。普通蓝牙透传的数据收发由
`Device_Serial` 完成。

LoRa Mode 不把 UART 的单个字节当作完整消息。Task 必须先用明确的长度/结束符/帧协议
验证一帧，再调用 `NotifyPeerFrameReceived()`。`Device_LoRa_Send()` 返回 `OK` 后调用
`NotifyTxAccepted()`；返回 `BUSY` 或错误时保留本地轮次。本 Mode 不自动增加 ACK、
超时、重发或失步恢复。
