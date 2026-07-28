# Device 结构

Device 负责“这是什么设备、协议怎样、状态怎样”，不负责 MCU 引脚。

```text
Device/
├─ Input/
│  └─ Key/                    短按、长按、双击
├─ Actuator/
│  ├─ Motor/                  普通方向 + PWM 电机
│  │  ├─ MG310/               MG310 GMR 500线参数
│  │  └─ MG513X/              MG513X GMR 500线参数
│  ├─ PowerSwitch/            受控 12 V/5 V 电源开关
│  └─ QD4310/                 QD4310 CAN 电机协议
├─ Sensor/
│  ├─ Encoder/                累计值、增量、速度、里程
│  ├─ LineTracking/           循迹归一化、差比和、丢线、交叉口
│  │  └─ XinliuT8/            芯流 T8 八路 ADC UART 查询协议
│  └─ IMU/
│     ├─ device_imu.*         通用 IMU 融合与安装方向接口
│     ├─ BMI088/              BMI088 SPI 寄存器驱动
│     ├─ ICM45686/            板载 ICM-45686 SPI 驱动与姿态接入
│     ├─ JY61P/               JY61P UART 协议驱动
│     └─ LingkeM0/            棱科 M0 单轴 UART 协议驱动
├─ Storage/
│  └─ Flash/                  隐藏 MCU 地址与擦写规则的持久化适配
├─ System/
│  └─ Watchdog/               WWDT 后端状态与单一喂狗接口
└─ Communication/
   ├─ Serial/                 通用缓冲 UART；蓝牙无线透传直接复用
   ├─ LoRa/                   LoRa 定点无线串口适配
   └─ Vision/                 视觉数据包通信
```

`IMU` 不是某个传感器型号。它是 APP 使用的统一接口；`BMI088`、`ICM45686` 和
`JY61P`、`LingkeM0` 才是具体设备。单 ICM-45686 与单 BMI088 都把物理量提交给同一个
`Device_IMU`，这样更换传感器时，零偏标定、姿态算法和云台 Mode 不需要重写。

所有 Device 的实物型号、产品归属、连接参数、软件默认值和待确认项统一记录在
[`docs/DEVICE_PARAMETERS.md`](../docs/DEVICE_PARAMETERS.md)。型号或实物参数没有可靠来源时，
必须保留“待确认”，不得用常见模块参数代替。

模块通过 CMake 选项按需加入编译链，例如：

```text
TEMPLATE_ENABLE_DEVICE_BMI088
TEMPLATE_ENABLE_DEVICE_ICM45686
TEMPLATE_ENABLE_DEVICE_JY61P
TEMPLATE_ENABLE_DEVICE_LINGKE_M0
TEMPLATE_ENABLE_DEVICE_POWER_SWITCH
TEMPLATE_ENABLE_DEVICE_QD4310
TEMPLATE_ENABLE_DEVICE_LINE_TRACKING
TEMPLATE_ENABLE_DEVICE_SERIAL
TEMPLATE_ENABLE_DEVICE_LORA
```
