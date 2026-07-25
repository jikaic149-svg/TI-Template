# Device/Sensor/IMU/BMI088

独立于 MCU 的 BMI088 设备驱动。Device 负责芯片 ID、初始化寄存器序列、量程换算、
温度解析和完整样本；BSP backend 负责 SPI handle、CS、dummy byte、DMA 与中断。

默认配置为 Accel 3 g / 400 Hz、Gyro 2000 dps / 1000-116 Hz。`ReadSample()`
输出 m/s²、deg/s、温度、时间戳和真实 dt，可直接交给 `APP/INS`。

本目录没有 STM32 HAL、固定 SPI、固定引脚或 FreeRTOS Task。当前两颗 MSPM0 的
BSP 尚未配置 BMI088 SPI 资源，因此模块可编译和主机测试，但不会在上电时访问总线。

`Device_BMI088_UpdateIMU()` 是单 BMI088 解算入口：驱动完成物理量换算后，把样本
提交给统一 `Device_IMU`，由后者完成安装轴映射、静止启动零偏标定和
`Algorithm_Mahony` 六轴姿态解算。具体安装轴由产品配置确认，不能写死在驱动中。
任一陀螺原始值达到 ±32000 count 时保留诊断值，但拒绝提交姿态算法。
