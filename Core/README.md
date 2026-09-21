# Core：跨模块基础设施

初版提供 [Message](Message/README.md)：静态、复制式、非阻塞 Pub/Sub。
业务层声明有具体 C 类型的命令/状态，并通过小型类型化接口收发；Core 只负责传递。

现有健康策略继续使用 `APP/System/Watchdog`，时间来源继续使用 Task/BSP 的既有接口。
不重复创建 Health、Time、Common 层。后续出现第二个实际使用者时再提取公共能力。

依赖方向：Task/Service 可以使用 Core；Core 不包含产品、Mode 或 Device。
仅 `Message/ports` 依赖执行环境。初版仅支持单核 FreeRTOS Task 上下文。
