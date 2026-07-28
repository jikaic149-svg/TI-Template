# 全局看门狗策略

本组件只判断当前监督周期是否允许喂狗，不包含 MCU、BSP、Device、FreeRTOS、
寄存器或复位操作。它是全局健康策略，不是可以从任意位置调用的硬件入口。

固定调用链：

```text
APP_Watchdog_ShouldFeed（所有关键源本周期均完成）
    -> Task/task_app.c（唯一喂狗决策者）
    -> Device_Watchdog_Feed（稳定设备接口）
    -> BSP_Watchdog_Feed（WWDT1 寄存器）
```

禁止其他 Task、Mode、Device 或中断直接喂狗。否则某个非关键模块仍在运行时可能
掩盖控制 Task 卡死，使电机继续保持最后输出。新增关键 Task 后，必须为其分配独立
`APP_WATCHDOG_SOURCE_*` 位，并在同一监督周期内把它加入 `required_sources`；多 Task
健康上报应通过工程的 Pub/Sub 通道汇总，不能使用跨模块可变全局量。

当前只有一个 10 ms 控制 Task，因此基础控制周期完整返回后才置位
`APP_WATCHDOG_SOURCE_CONTROL_TASK`。Mode 进入已安全处理的故障状态不代表调度器失活，
控制周期仍能完成时继续喂狗，避免无意义的复位循环。
