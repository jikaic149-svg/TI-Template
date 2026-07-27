# Communicate Mode

纯通信策略层，不访问 UART、BSP、Device、FreeRTOS、Task 或跨模块全局变量。

- `BLUETOOTH_MODE_PC`：电脑通信，使用原始字节流；具体上层格式由产品 Task 决定。
- `BLUETOOTH_MODE_DUAL_MCU`：双 MCU 通信，使用 Packet V1、周期 `HELLO` 和节点
  身份。

目录与其他 Mode 一致：每个具体模式一个实现文件，公共类型和 API 统一从
`include/mode_communicate.h` 导出。

- `Bluetooth/mode_communicate_bluetooth_pc.c`：实现 PC 模式的 `Init/Update`。
- `Bluetooth/mode_communicate_bluetooth_dual_mcu.c`：实现双 MCU 模式的
  `Init/Update`。

Task 负责选择并调用其中一个模式、持有唯一 UART 实例、初始化对应协议并执行收发；
本组件不进行模式选择或运行期切换，也不定义 ACK、重发或控制命令语义。
