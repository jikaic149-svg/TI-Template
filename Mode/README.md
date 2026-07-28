# Mode

纯业务状态机层，不访问 BSP、Device、FreeRTOS、Task 或跨模块全局变量。

- `Car`：Idle、Straight、Turning、Curve、Brake、Done；输出左右轮速度意图。
- `Gimbal`：Scheduler、AngleLoop、编码器锁定、GimbalStab、VisionStab、
  GravityCalib；输出双轴电流意图。
- `Communicate`：Bluetooth PC/双 MCU 策略与 LoRa 半双工轮次；不持有 UART 或协议实例。

Mode 只输出控制意图，不自动解锁电机。Task 负责输入快照、业务状态机、安全门、
周期调度和 Device 提交。
