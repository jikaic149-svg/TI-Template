# Service：外部协议与内部消息的边界

Service 解析外部输入、检查协议和参数，把有效数据转换为命令；Task 决定何时执行、
选择 Mode 并提交 Device。反馈由 Task 发布，Service 订阅并编码。
Service 可以使用传输 Device，但不直接控制业务执行器。

| 组件 | 用途 | 校验 |
|---|---|---|
| [Line](Line/README.md) | 短文本命令、调试接口 | 长度、字符、分帧；命令适配器再检查名称与取值 |
| [Packet](Packet/README.md) | 已选用 TI 数据包协议的连接 | 版本、类型、长度、序号格式、CRC16 |

两者独立启用。基础命令示例采用 Line；内部 Pub/Sub 不额外加 CRC。
完整选择依据见 [架构说明](../docs/FRAMEWORK_V1.md)。
