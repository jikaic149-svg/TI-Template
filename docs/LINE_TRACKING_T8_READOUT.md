# 芯流 T8 八路模拟量循迹

## 范围与分层

模板提供两个互斥固件 profile。`LineTrackingReadout` 只读取八路 ADC、做归一化与
差比和位置估计，并运行模拟量循迹 Mode 计算左右驱动意图；它不调用 Motor Device，
不向电机发送命令。`AnalogTracking` 在同一数据链上运行循迹、角度、轮速三级串级。

```text
PC6/PC7 UART3 BSP
        ↓
Device_Serial → Device_XinliuT8（0x19 帧解析）
        ↓
Device_LineTracking（满量程缩放、帧内共同底值消除、极性、权重、差比和）
        ↓
Mode_Car_AnalogTracking（置信度门控和左右驱动意图）
        ↓
蓝牙 UART2 / Live Watch，只读诊断
```

数字量和模拟量在 Mode 层明确分开：

- `Mode_Car_DigitalTracking` 接收数字/阈值化传感器产生的误差；
- `Mode_Car_AnalogTracking` 接收模拟量归一化、差比和后的误差和置信度；
- 本 profile 只调用 `AnalogTracking`。

## 接线

| 芯流 T8 | MSPM0G3519 | 说明 |
|---|---|---|
| TX | PC7 / UART3_RX | 传感器响应进入 MCU |
| RX | PC6 / UART3_TX | MCU 发送 `0x19` 查询 |
| GND | GND | 必须共地 |
| VCC | 按模块手册额定电源 | 上电前确认供电和 UART 电平；MCU IO 为 3.3 V |

UART3 固定为 `115200 8N1`、BSP 逻辑索引 `3`。PB8/PB9 保留给同步数字/串行
循迹后端，不与本模拟量 UART3 复用。蓝牙诊断继续使用 PB10/PB11、UART 索引 `2`、
115200。

## 归一化与差比和

T8 的 `uint16_t` ADC 先按有效满量程缩放，不要求提前采集黑白端点：

```text
n[i] = clamp(adc[i] / 4095, 0, 1)
s[i] = n[i] - min(n[0..7])
```

如果黑线对应低值，则通过 `line_is_high=false` 使用 `1-n[i]`。模拟量路径不做
逐通道阈值截断；减去的是当前帧八路共同底值，因此共同环境光和 ADC 直流底值不会
进入方向，只有八路相对差进入差比和。阈值只属于独立的数字量循迹路径。

当前 T8 产品权重从左到右为 `-1, -0.60, -0.25, 0, 0, 0.25, 0.60, 1`。
中心两路有强度时认为线在中心，只增加有线置信度，不产生方向误差；从第三路开始
非线性增强，边缘响应明显大于中心。算法同时计算：

```text
centroid = sum(s[i] * weight[i]) / sum(s[i])
distance[i] = abs(weight[i] - target)
L = sum_left(s[i] * distance[i]) / sum_left(distance[i])
R = sum_right(s[i] * distance[i]) / sum_right(distance[i])
ratio    = (R - L) / (R + L + epsilon)
contrast = sum(s[i])
```

其中 `L/R` 是按探头离目标中心距离加权并归一化后的左右强度；外侧探头的影响大于
内侧探头。默认用 70% 加权差比和位置与 30% 加权质心融合，再经过 15 ms 一阶低通。
最终 `error = position - target`，正值表示线在车体右侧，模拟量 Mode 因此给出
“左侧意图更大、右侧意图更小”。

`contrast` 只用于判断八路是否存在可用差异，不乘位置权重；方向计算才使用 `L/R`。
这样中心通道即使方向权重小，只要相对背景有明显大值也不会被忽略。`epsilon` 是整组
差值的底噪保护，不是逐通道模拟量阈值。模拟量置信度也由 `contrast` 计算，不使用
方向加权后的 L/R 分母。

当前上板使用 ADC 满量程 `0..4095`、黑线高值。模拟量路径不使用逐通道阈值，也不从
模拟量活跃通道数推断交叉口。`0..4095` 只是数值缩放范围，不是黑白标定值；位置权重
集中在 `Task/LineTracking/task_line_tracking_readout.c` 的常量数组中。

仅保留差比和分母 `epsilon` 的数值保护；它不是逐通道判定阈值。实物只需确认通道
顺序和黑线高/低极性，不能在模拟量差比和前增加数字化阈值。

## 构建与烧录

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\mspm0.ps1 `
  -Action Build -Device MSPM0G3519 -Configuration Debug `
  -MotorDriver TB6612 -TaskProfile LineTrackingReadout

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\mspm0.ps1 `
  -Action Flash -Device MSPM0G3519 -Configuration Debug `
  -MotorDriver TB6612 -TaskProfile LineTrackingReadout -Probe DAPLink
```

完整模拟循迹 profile：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\mspm0.ps1 `
  -Action Build -Device MSPM0G3519 -Configuration Debug `
  -MotorDriver TB6612 -TaskProfile AnalogTracking
```

其固定控制链为：模拟循迹 PID（Kp 1.5）生成相对 yaw 目标，最大 `±25°`；ICM45686
yaw 角度 PID（Kp 10.0）生成轮间差速；双轮轮速 PI 最终驱动 TB6612。基础速度
`250 mm/s`，每个车轮目标限幅 `±800 mm/s`。用户已在来源测试工程实物确认轮速环和
角度环极性；Template 同步版本只完成构建验证，尚未另行烧录。

KEY 在传感器、IMU 和编码器可用后启停循迹；启动时以当前 yaw 为基准。RGB 分别指示
校准、停止、运行和故障。蓝牙以约 25 Hz 输出 14 列：状态、A 轮目标/实际、B 轮
目标/实际、八路 ADC、yaw。

## 蓝牙 CSV 与 Live Watch

蓝牙每 20 ms 输出一行 CSV：

| 列 | 内容 |
|---:|---|
| 1..8 | 原始 ADC CH1..CH8 |
| 9..16 | 逐通道 `normalized`，范围 0..1 |
| 17 | 差比和 `difference_ratio` |
| 18 | 加权质心 |
| 19 | 低通融合位置 |
| 20 | 循迹误差 |
| 21 | 置信度 |
| 22..23 | AnalogTracking 左/右驱动意图，仅诊断 |
| 24..26 | `line_present`、`line_lost`、`intersection`；模拟量路径当前不做交叉口数字化判定 |
| 27..31 | 接受帧、拒绝帧、查询超时、UART 错误、丢弃遥测帧 |
| 32 | 状态：0 等待、1 在线、2 数据过期、3 运行故障 |

也可直接观察 `g_TASK_LineTrackingRaw`、`g_TASK_LineTrackingNormalized`、
`g_TASK_LineTrackingStrength`、`g_TASK_LineTrackingDifferenceRatio`、
`g_TASK_LineTrackingError`、`g_TASK_LineTrackingConfidence`、左右意图和错误计数。

## 实测确认顺序

1. 固定传感器高度，确认均匀底面上八路原始 ADC 相近。
2. 如果压在线上时 ADC 变低，统一切换为 `line_is_high=false`。
3. 确认从左到右移动线时 CH1..CH8 顺序和误差符号正确。
4. 检查均匀背景减去当前帧共同底值后是否判为丢线；不要增加逐通道阈值。
5. 先用 `LineTrackingReadout` 确认 `difference_ratio`、位置和置信度，再选择
   `AnalogTracking`；只读 profile 始终不启用电机输出。
