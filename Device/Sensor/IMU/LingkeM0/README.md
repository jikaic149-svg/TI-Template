# Device/Sensor/IMU/LingkeM0

该模块解析棱科 M0 单轴陀螺仪的 UART 主动输出：

- `5A AA WzL WzH SUM`：Z 轴角速度，按串口手册换算为 `°/s`。
- `5A BB YawL YawH SUM`：Yaw，按串口手册换算为 `°`。
- `SUM` 是前四个字节累加后的低 8 位。

模块只拥有协议解析、软件 yaw 零点和 500 ms 离线状态。UART、DMA、ISR
及字节环形缓冲区仍由 BSP/Serial 持有；解析在 Task 上下文执行，无阻塞等待。

串口手册和厂商串口例程均给出角速度比例 `raw / 32768 * 2000°/s`，但资料的
参数页标注量程 `±400°/s`。当前按串口协议实现，比例仍需用实物已知角速度确认。
