# TI-Template 框架初版

TI-Template 继续定义职责和依赖；H7_BSP 提供基础设施设计参考，
basic_framework 提供命令、执行模块和反馈分离的应用组织参考。
现有 APP/Algorithm、APP/System、Device、Mode、Task 路径继续使用。

本次加入：

- Core/Message：静态 Pub/Sub，离散命令 FIFO、最新值 mailbox/状态副本，明确 FULL。
- Service/Line：简单命令输入；Service/Packet：复用已有 CRC 协议的可选适配。
- examples/command_flow：带两份状态订阅的串口 LED 完整示例，单独配置构建。
- 配套 host 测试、ARM 编译、示例固件 Debug/Release 和增量验证映射。

Task 管理时间、仲裁、模式选择和设备输出；Mode 只实现自己的策略；Algorithm 纯计算；
Service 负责外部协议，Core 负责内部交付，BSP 持有 MCU 资源。直接函数调用仍用于
初始化、模块内部、Device 提交及纯算法；不同业务模块间的运行期数据使用消息。
健康管理复用既有 APP/System/Watchdog，不新增自动恢复、急停锁存或第二个喂狗者。

## 校验按边界选择

| 场景 | 初版规则 |
|---|---|
| 同 MCU 内部命令/状态 | 类型化业务接口、长度、独立副本、同步；不做协议 CRC |
| 本地简单文本控制/调试 | 帧边界、长度、字符、命令与参数范围 |
| CAN/USB 等已有链路校验的连接 | 先遵守器件协议；是否另加应用 CRC 取决于端到端需求 |
| 裸串口长帧、噪声链路、固件/参数传输 | 按协议选择 CRC 等完整性校验，不只看字段多少 |
| 关键控制命令 | 同时考虑执行后果、有效期、来源与执行反馈；CRC 本身不解决这些问题 |

用户明确否决“简单命令默认全部 CRC”。指示灯示例因此使用 Line，
Packet 独立可选。格式校验不能检测所有仍然格式合法的数据损坏，例如 0 变成 1。
数据短不必然表示误执行代价低；选校验方式应同时考虑链路和语义。

H7_BSP 参考快照为 `be12d09`：CAN_Tx_Submit/最新值发送接口没有附加通用应用层 CRC，
CAN 控制器提供帧级 CRC。其 `Application/Communication/Com.h` 有 Checksum 字段，
但 `Com.cpp` 的 Communication_Callback 为空，不能当作已完成的校验机制。
因此未从 H7 推导“所有消息都应 CRC”的规则。

## 后续扩展边界

v1 先完成可用的消息与应用链路。CAN 收发注册、更多健康统计、日志/存储适配、
ISR 消息入口均按下一项实际产品需求增加，不预建空层。
大数据继续使用专用缓冲，不能将图像或 DMA 数据机械装进小消息 Pub/Sub。
移植参考代码前应确认其依赖与许可；本次只有设计借鉴，没有复制 H7 或 basic 源码。
