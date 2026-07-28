# Device/Communication

通信设备实现独立于 UART backend 的协议状态机。BSP/ISR 只搬运字节；完整解析在 main 或 task 上下文执行。

- `Serial/`：按逻辑 transport index 使用 BSP 缓冲 UART 的通用 Device；普通蓝牙
  无线透传直接使用它，不需要再包一层伪协议 Device。
- `LoRa/`：外部 LoRa 无线串口的定点路由前缀、非阻塞发送和透明接收。
- `Vision/`：ASCII 可靠视觉命令/结果协议。
