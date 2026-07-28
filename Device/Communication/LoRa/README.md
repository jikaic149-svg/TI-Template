# LoRa 无线串口 Device

本 Device 对应用户在 2026-07-27 提供的两块 LoRa 参数页。它复用非阻塞
`Device_Serial`，发送时把一个原始 3 字节定点路由前缀和载荷拼成一次原子 UART
入队；接收端提供透明 UART 字节流。

## 已登记的两端参数

| 本地地址 | 本地信道 | 频率 | 发往对端的实测前缀 |
|---|---:|---:|---|
| `0x7302` | `0x4A` | 507.15 MHz | `10 07 30` |
| `0x1007` | `0x30` | 481.15 MHz | `73 02 4A` |

共同参数：UART `115200 8N1`、3.3 V TTL、空口速率 LEVEL 2 / 2148 bps、
22 dBm、定点传输、高时效模式、密钥开关开启、模块密钥 `12345`。

2026-07-27 使用两块实物在电脑端验证：COM13 对应 `0x7302`，发送
`10 07 30 + payload` 后 COM14 只收到完全一致的 payload；COM14 对应 `0x1007`，
发送 `73 02 4A + payload` 后 COM13 同样精确接收。反向使用错误目标地址时接收为
0 字节。因此本模块对的格式已确认为
`[目标地址高字节][目标地址低字节][目标信道]`，接收端不会向 UART 输出该前缀。

## 软件边界

- `Device_LoRa_Send()` 返回 `OK` 只表示完整前缀和载荷已进入 MCU UART 发送队列，
  不表示对端收到。
- Device 不自动增加 ACK、重发、CRC、分包或消息边界。需要可靠消息时，由明确的
  上层协议处理。
- `tx_frame_buffer` 由调用者静态提供并保持有效，且只供 Device 组帧；发送载荷不能
  与它重叠。容量决定单次软件载荷上限，不是模块手册宣称的 RF 单包上限。模块最大
  载荷仍待手册确认。
- 两块模块必须先用厂家工具写入参数；当前 Device 不发送未知 AT/配置命令，也无法
  从参数页证明模块实际保存值与代码期望值一致。

LoRa 空口按半双工使用。Device 只负责一次物理发送/接收，不拥有通信轮次；双端交替
发送由 `Mode_Communicate_LoRaHalfDuplex` 管理。发送返回 `BUSY` 或错误时不能交出本地
轮次，只有返回 `OK` 后才通知 Mode；Task 确认收到一帧完整有效消息后再通知 Mode 把
发送权交回本端。

LoRa 模块不绑定某一颗 MCU、开发板或 DAPLink。模块内部保存地址与信道；换到另一块
MCU 时，只需让产品配置中的 `local_side` 与所接模块地址一致，并为新板提供满足要求的
UART transport。探针串号只能帮助烧录时区分当前板卡，不能作为运行期设备身份。

启用 `TEMPLATE_ENABLE_DEVICE_LORA=ON` 会自动编译其 `Device_Serial` 依赖。产品 Task
确定 UART 后，可按本机模块地址创建一端配置：

```c
static uint8_t lora_tx_frame[128U];
static Device_LoRa lora;
Device_LoRa_Config config;

Device_LoRa_MakeCapturedPairConfig(
    DEVICE_LORA_PAIR_SIDE_ADDRESS_7302,
    product_lora_uart_index,
    lora_tx_frame,
    sizeof(lora_tx_frame),
    &config);
Device_LoRa_Init(&lora, &config);
```

此例的最大软件载荷为 `128 - 3 = 125` 字节；实际允许的模块单包长度仍以待补充的
手册为准。地址 `0x1007` 一端把枚举改成
`DEVICE_LORA_PAIR_SIDE_ADDRESS_1007`。

## 接线边界

信号连接为 `MCU TX -> LoRa RX`、`MCU RX <- LoRa TX`、两端 `GND` 共地，UART
逻辑电平按参数页使用 3.3 V TTL。参数页没有确认模块供电电压，不能把“3.3 V TTL”
直接当成 VCC 规格。具体 MCU UART index、TX/RX 引脚和模块 VCC 必须根据实际板卡
再在 BSP/产品 Task 中绑定；本次没有修改 SysConfig。
