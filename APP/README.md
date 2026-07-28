# APP 纯软件层

`APP` 只保存与硬件、业务模式和调度器无关的可复用算法与系统策略。组件采用
显式输入输出，不访问 BSP、Device、Mode、Task、FreeRTOS 或跨模块全局变量。

业务状态机、周期、优先级和 Device/Mode 数据搬运位于具体项目的 `Task`。
根目录 `main.c` 只负责初始化并启动调度器。

当前组件：

- `Algorithm/`：滤波、控制、协议和数学算法，每个组件拥有独立 CMake target；
- `System/Watchdog/`：全局关键执行源的健康判定。

“全局策略”不表示可以越层访问硬件。APP Watchdog 只能输出是否允许喂狗，唯一
喂狗者仍是 `Task/task_app.c`，硬件 WWDT1 由 Device/BSP 适配。完整约束见
[`docs/WATCHDOG.md`](../docs/WATCHDOG.md)。
