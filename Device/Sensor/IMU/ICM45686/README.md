# ICM-45686

板载 ICM-45686 的 MCU 无关 Device。依据用户提供的 TDK/InvenSense
`DS-000489 Rev 1.1` 实现 4-wire SPI 寄存器初始化、WHO_AM_I 校验、14 字节
同步采样、物理量换算和 `Device_IMU`/`Algorithm_Mahony` 接入。

默认配置为 Accel ±4 g、Gyro ±1000 dps、两者 800 Hz Low-Noise，
INT1 为 push-pull、active-high、pulse Data Ready。WHO_AM_I 为 `0xE9`；上电后
等待至少 1 ms，软复位后再次校验，启用陀螺后等待 35 ms。

数据寄存器从 `0x00` 开始依次为 Accel XYZ、Gyro XYZ、Temperature，每项
16 bit。器件复位默认是 little-endian，本驱动不修改间接的
`SREG_DATA_ENDIAN_SEL`，因此 Init 只接受 little-endian；独立解码函数保留
big-endian 测试能力。温度公式为 `raw / 128 + 25 °C`。SPI backend 必须按
MSB first、上升沿采样/下降沿改变、CS 低有效实现，读取首字节为
`0x80 | register`，频率不得超过 24 MHz。

G3519 原理图连接为 SPI1：PB16 SCLK、PB15 PICO/MOSI、PB14 POCI/MISO、
PB12 CS1、PB22 INT1、PB28 INT2。当前 G3519 SysConfig 已应用 SPI1 与 PB12
软件 CS，使用 10 MHz Mode 0 轮询；INT1/INT2 尚未配置，Device 初始化关闭 DRDY。

`ReadSample()` 会继续返回接近原始 ADC 正负满量程的解码值供诊断，但当任一陀螺轴
原始绝对值达到 `32000` 时将 `sample.valid` 置为 false，避免 ±1000 dps 配置下的
削顶数据以高置信度进入双 IMU 姿态积分。量程、ODR 和寄存器配置不因此改变。

`device_icm45686_spi` 是单 ICM 产品使用的薄适配：只把现有逻辑 BSP SPI handle、
软件 CS 和有限事务超时绑定到本寄存器 Device，再把有效物理量样本提交给现有
`Device_IMU`。它不持有 MCU 引脚或寄存器，不修改 BSP/SysConfig；饱和样本不会提交
姿态算法。双 IMU 产品仍使用 `Device_DualIMU` 自己的私有传输绑定。
