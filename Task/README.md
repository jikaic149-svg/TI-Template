# Task

模板只提供一个 10 ms 静态 FreeRTOS 控制任务骨架，用于验证调度器、移植层和全局
看门狗调用链，不包含具体产品 Task 或 Device/Mode 编排。
任务名称、周期、栈深度和优先级偏移集中定义在
[`config/core_config.h`](../config/core_config.h) 的“Task 与调度”分区。

显式选择 `g3519-command-flow-*` 时，原控制 Task 在每个周期调用串口 LED 示例：
Service 接收 → 命令 FIFO → Device 提交 → 状态发布 → Service 反馈。
周期、优先级、栈和唯一喂狗者保持不变；示例不创建新 Task。
`g_TASK_ExampleEvents` 是当周期诊断位，具体预算和语义见
[`examples/command_flow`](../examples/command_flow/README.md)。

`task_app.c` 是全局 250 ms WWDT1 的唯一运行期喂狗者，只在当前
10 ms 控制周期完整返回且 APP 健康策略满足后喂狗。禁止把喂狗移入 Tick/Idle/ISR，
也禁止其他 Task 独立喂狗；新增关键 Task 时按
[`docs/WATCHDOG.md`](../docs/WATCHDOG.md) 汇总当前周期健康状态。
