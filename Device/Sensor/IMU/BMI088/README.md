# Device/Sensor/IMU/BMI088

独立于 MCU 的 BMI088 设备驱动。Device 负责芯片 ID、初始化寄存器序列、量程换算、
温度解析和完整样本；BSP backend 负责 SPI handle、CS、dummy byte、DMA 与中断。

默认配置为 Accel 3 g / 400 Hz、Gyro 2000 dps / 1000-116 Hz。`ReadSample()`
输出 m/s²、deg/s、温度、时间戳和真实 dt，可直接交给 `APP/INS`。

`device_bmi088.c` 不依赖 MCU、固定引脚或 FreeRTOS Task；
`device_bmi088_spi.c` 复用 BSP 逻辑 SPI handle，负责双 CS、Accel dummy byte 和
首次 CS 事务切换 SPI 模式。当前 G3519 产品绑定 SPI0、10 MHz Mode 0、PC0 Accel CS
和 PC1 Gyro CS；具体引脚仍只存在于 SysConfig/BSP。

`Device_BMI088_UpdateIMU()` 是单 BMI088 解算入口：驱动完成物理量换算后，把样本
提交给统一 `Device_IMU`，由后者完成安装轴映射、静止启动零偏标定和
`Algorithm_Mahony` 六轴姿态解算。具体安装轴由产品配置确认，不能写死在驱动中。
任一陀螺原始值达到 ±32000 count 时保留诊断值，但拒绝提交姿态算法。
