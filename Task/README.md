# Task

基础 preset 只提供一个静态 FreeRTOS 控制任务骨架，用于验证调度器和移植层。

具体项目在 Task 中直接读取 Device、调用 Mode 并提交 Device 输出。当前可显式选择两个
10 ms 循迹 profile：

- `LineTrackingReadout`：UART3 查询芯流 T8，计算八路模拟量差比和，通过蓝牙输出诊断；
  始终保持电机锁定。
- `AnalogTracking`：芯流 T8 循迹 PID → ICM45686 yaw 角度 PID → 双轮轮速 PI；
  KEY 启停，RGB 指示状态，蓝牙固定输出 14 列 VOFA 数据。

两个 profile 互斥，基础 preset 不会自动启用。具体参数见
[`docs/LINE_TRACKING_T8_READOUT.md`](../docs/LINE_TRACKING_T8_READOUT.md)。
