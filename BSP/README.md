# BSP 结构

BSP 只负责 MCU 外设和引脚。APP、Mode、Device、Algorithm 都不能直接包含
`ti_msp_dl_config.h`，也不能写 PA/PB 引脚、定时器实例或中断号。

```text
BSP/
├─ bsp_init.h                 BSP 总初始化
├─ bsp_time.h                 忙等延时
├─ bsp_flash.h                DATA-Flash 读取与整扇区替换
├─ bsp_rgb.h                  RGB 接口
├─ bsp_buzzer.h               PA7 蜂鸣器接口
├─ bsp_power_switch.h         受控 12 V/5 V 电源开关接口
├─ bsp_key.h                  原始按键电平
├─ bsp_pwm.h                  0..10000 PWM
├─ bsp_motor.h                方向 + PWM 的普通电机输出
├─ bsp_encoder.h              编码器累计值和增量
├─ bsp_watchdog.h             WWDT 运行状态与唯一喂狗后端
├─ bsp_uart.h                 UART blocking / 可选 DMA
├─ bsp_can.h                  Classic CAN / CAN FD 公共帧接口
├─ bsp_line_sensor.h          数字/模拟循迹采样接口
├─ bsp_not_configured.c       未分配外设时的安全空实现
├─ MSPM0G3507/
│  ├─ MSPM0G3507.syscfg       3507 的引脚和外设配置
│  ├─ bsp_resource_map.h      SysConfig 名称映射
│  ├─ bsp_manifest.cmake      只加入实际使用的 DriverLib 文件
│  └─ bsp_init/rgb/time.c     3507 的实际实现
└─ MSPM0G3519/
   ├─ MSPM0G3519.syscfg              TB6612 profile
   ├─ MSPM0G3519_AT8236.syscfg       AT8236 profile
   ├─ bsp_motor_tb6612.c
   ├─ bsp_motor_at8236.c
   └─ 其余 G3519 BSP 实现
```

使用规则很简单：

1. 上层只调用 `bsp_*.h`。
2. 换引脚时只修改对应 MCU 的 `.syscfg` 和 `bsp_resource_map.h`。
3. 新外设的实际实现放在 `MSPM0G3507/` 或 `MSPM0G3519/`。
4. 没配置的外设由 `bsp_not_configured.c` 返回零资源/失败，不猜引脚。

WWDT 是无引脚的 MCU 系统资源。G3507/G3519 均由 SysConfig 分配 `WWDT1`，周期
250 ms；BSP 只提供后端，禁止 APP、Mode 或普通 Task 绕过全局健康策略直接喂狗。
完整所有权和扩展规则见 [`docs/WATCHDOG.md`](../docs/WATCHDOG.md)。

当前暂不启用 CAN。公共 CAN API 已建立，但两个 BSP 的
`BSP_RESOURCE_HAS_CAN` 均保持 `0`；G3519 SysConfig 暂不生成 MCAN 初始化，
PA12/PA13 暂未分配。

## MSPM0G3519 当前小车资源

| BSP 资源/顺序 | 外设与引脚 | 参数 |
|---|---|---|
| UART 0 | UART1：PB4 TX、PB5 RX | 115200，主调试 |
| UART 1 | UART0：PB0 TX、PB1 RX | 115200，辅助串口 |
| UART 2 | UART4：PB10 TX、PB11 RX | 蓝牙，115200，RX DMA_CH2，上拉 |
| TB6612 Motor 0/1 | PC2/CCP0、PC4/CCP1；PA24..PA27；PB21 STBY | 默认 profile，20 kHz |
| AT8236 Motor 0 | PC2/CCP0 IN1、PB20/CCP2 IN2 | AT8236 profile，20 kHz |
| AT8236 Motor 1 | PC4/CCP1 IN1、PA25/CCP3 IN2 | AT8236 profile，20 kHz |
| Encoder 0 | TIMG9 QEI：PB29 A、PB30 B | 左编码器，硬件 x4 正交计数、上拉 |
| Encoder 1 | TIMG8 QEI：PB6 A、PB7 B | 右编码器，硬件 x4 正交计数、上拉 |
| LineSensor 0 | PB8 DAT、PB9 CLK | 8 路同步串行数字量，DAT 上拉 |
| Key 0 | PB31 USER KEY | 低电平按下，内部上拉 |
| Servo 0 | TIMG12_CCP0：PB13 | 50 Hz，舵机接口 1 |
| Servo 1 | TIMG12_CCP1：PA16 | 50 Hz，舵机接口 2 |
| Power switch 0 | PA15 | 受控 12 V，高电平使能 |
| Power switch 1 | PB26 | 受控 5 V，高电平使能 |
| Buzzer | PA7 GPIO | 初始低电平；通过 `BSP_Buzzer_*` 控制 |
| CANFD0 | 暂未分配 | 当前不启用 CAN；实现 BSP backend 后再恢复 PinMux |
| System watchdog | WWDT1，无引脚 | 250 ms，睡眠继续计数，调试暂停时停止 |

蓝牙串口使用 UART4、115200 baud；SysConfig 与上层 Device 配置必须保持
相同波特率。灰度模块后端每次读取 8 bit，
第一个时钟采样映射为通道 0，低电平返回 0（黑线极性由 Device 配置处理）。

## MSPM0G3507 当前编码器资源

| BSP 资源/顺序 | 外设与引脚 | 参数 |
|---|---|---|
| Encoder 0 | TIMG8 QEI：PA29 A、PA30 B | 左编码器，硬件 x4 正交计数、上拉 |
| Encoder 1 | TIMG7 Capture：PA26 A；GPIO PA27 B | 右编码器，A 上升沿中断判向，x1 结果按 4 折算为 x4 计数单位 |

G3507 的 PA29/PA30 对应 LaunchPad J12 QEI 接口；PA26/PA27 与板载光线传感器
跳线复用，接第二路编码器前必须按板卡说明确认 J12 焊接并断开冲突跳线。
