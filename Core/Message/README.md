# 静态消息通道

采用调用方持有的 Topic、订阅表和独立存储，不用字符串注册表、堆、订阅回调或运行期注册。
先在启动阶段完成 `Core_Message_Init`，再由 Task 发布与消费。业务接口封装具体类型；
底层 `void * + size` 只检查长度，不能识别两个同样大小但语义不同的 C 类型。

| 策略 | 适用内容 | 消费与满载行为 |
|---|---|---|
| FIFO | 离散命令、不可跳过的事件 | 顺序 Take；满则 Publish 返回 FULL |
| LATEST | 控制目标、状态、遥测 | 覆盖旧值；Take 获取新值后清空；Snapshot 保留当前副本 |

同一 Topic 的订阅者各有副本。任何 FIFO 满载时，整次发布失败，所有订阅者保持原样，
包含 LATEST；因此不要把慢速日志消费者配置为关键命令 Topic 的 FIFO 订阅者。
对于只需最新值的命令，用 LATEST 订阅即可形成 mailbox。Snapshot 是订阅副本，
不是读取另一个模块的私有变量；重复读取不等于有新状态，业务应检查序号/时间戳。

`OK` 表示所有副本已提交，不表示设备已执行。无订阅者、错误长度、非法存储返回
`INVALID`；未初始化返回 `NOT_READY`；无待消费数据返回 `EMPTY`。
失败接收不修改输出。初版不提供运行期删除、重新绑定、ISR 发布或自动重试。

实例和存储必须活到系统停止，且不能复制已初始化的控制结构。禁止输入/输出缓冲区
与 Topic 控制结构或订阅存储别名重叠。初始化拒绝同一 Topic 内重叠的订阅缓冲区；
不同 Topic 的存储所有权由装配方保证。禁止运行期直接修改结构体字段。

FreeRTOS port 用短临界区保护完整 fanout 与 payload copy。没有互斥等待，耗时与
`订阅数量 × payload 字节数` 成正比。只用于少量小消息；图片和长字节流不走此通道。
ISR 保持在 BSP，通过已有缓冲区/通知交给 Task，不能调用这里的 Task API。
host port 只用于串行逻辑测试，不能用于多线程或实机。

在 32 位 ARM ABI 下，Topic 为 12 B，Subscription 为 28 B；每个订阅另需
`sizeof(Payload) × 槽数` 字节，具体以目标编译结果为准。分配全为静态，无堆开销。
本层不计算 CRC：它解决内存中消息的所有权和并发，传输协议校验属于 Service/Device。

资料来源：H7_BSP 的 FIFO 与 latest-value 发送语义、basic_framework 的命令/反馈分离，
以及 TI-Template 的分层约束。实现为本次新增 C 代码；未复制两个参考框架的源文件。
验证入口：`tests/run_gcc_tests.ps1 -Scope Message`，另由示例固件编译真实 FreeRTOS port。
