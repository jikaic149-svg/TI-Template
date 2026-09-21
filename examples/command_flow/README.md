# 串口命令到设备反馈的最小示例

用已有板载 LED 展示 Service、Task、Device 与 Pub/Sub 的完整连接。
此示例没有控制算法或产品模式需求；复杂产品在 Task 执行步骤中调用 Mode/Algorithm。

```text
Serial Device -> Line service -> Example_Command FIFO(2)
                                      |
                               控制 Task 每周期取 1 条
                                      |
                                 Device_LED -> BSP
                                      |
                              Example_Status Topic
                               /                \
                    Latest: 串口反馈      Latest: 独立观察者
```

只在 `g3519-command-flow-debug/release` 启用；基础 preset 继续运行原来的 Task 骨架。
示例使用 G3519 既有逻辑 Serial 2（UART4/PB10、PB11，115200）和 LED 0，
实际映射由 BSP 决定；该串口不能同时交给另一个 LoRa/Serial/Vision 消费者。
示例配置集中在 `config/core_config.h`。

```powershell
cmake --preset g3519-command-flow-debug
cmake --build --preset build-g3519-command-flow-debug
```

如 SDK 不在模板同级的 TI ENV，首次 configure 使用 README 中的显式 SDK 路径覆盖。
构建不等于烧录。未在本次任务中烧录，LED 极性和真实串口通信未重新实测。

| 输入 | 返回 |
|---|---|
| `LED 1\n` | Device 成功后 `OK LED 1\n` |
| `LED 0\n` | Device 成功后 `OK LED 0\n` |
| 其他命令/超长/非法字符 | `ERR COMMAND\n` |
| 命令 FIFO 已满 | `ERR FULL\n`，不执行该命令 |
| Device 提交失败 | `ERR DEVICE\n`，状态仍报告最后成功值 |

没有 CRC、自动重试、序号 ACK 或新的保护状态机。推荐请求方一问一答；
批量输入时即时拒绝响应可能先于先前已排队命令的执行响应。
TX 满时整条响应被丢弃，Update 返回 TX_DROPPED；设备可能已经执行，
因此该简单协议不保证端到端可靠交付。合法命令重复执行为设置同一 LED 状态。

状态 payload 含本地接受序号（uint32 回绕）、执行时间戳（ms、uint32 回绕）、
最后成功 LED 状态与本次是否执行成功。它不直接序列化为线上 C 结构体。
一个 LATEST 订阅负责串口反馈，另一个由 `Example_CommandFlow_TakeStatus` 消费；
慢观察者允许跳过旧状态，不会抢走串口反馈，也不会阻塞控制 Task。

| Task 契约 | 初版 |
|---|---|
| owner | 原 Control Task；唯一示例 LED 提交者 |
| 周期 / 优先级 | 原 10 ms / Idle+2，不新增 Task |
| 每周期预算 | 最多 64 个 RX 字节、1 条命令、1 条执行反馈；输入拒绝处理也受字节预算限制 |
| 设计 deadline | 一次 Update 在当前 10 ms 周期内完成；实际 WCET/抖动尚未测量 |
| 栈 | 原 512 个 StackType_t，即 ARM 下 2048 B；实测高水位待硬件确认 |
| 静态存储 | 命令 payload 16 B、状态副本 24 B、3 个 Subscription 84 B、2 个 Topic 24 B，另有解析/Device/RX 状态 |
| 时间源 | 原 Control Task 的 NowMs；业务消息传值，不读取跨模块可变全局量 |
| 诊断 | `g_TASK_ExampleEvents` 为仅供调试器观察的当周期事件位，不用作业务通信或喂狗策略 |

初始化失败沿用 Task 初始化失败出口。Watchdog 策略、任务周期/栈和 MCU 配置保持现有值。
资料来源：basic_framework 的命令/反馈应用组织方式；实现复用 TI 自己的 LED/Serial Device，
没有迁入底盘业务。测试实际执行 Device 代码，只有 BSP 和临界区 port 是 host 替身。
