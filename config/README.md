# 工程配置

本目录只保存参与构建、烧录或调试的机器无关声明式配置：

- `core_config.h`：核心可调参数的唯一入口，按 Task、车辆控制、循迹、云台、
  Mahony 和双 IMU 融合/Kalman 大类分区；
- `cmake/`：GNU Arm 工具链定义与 DriverLib 模块映射；
- `openocd/`：DAPLink/XDS110 的工程配置与无序列号示例；
- `verification/`：增量验证的组件范围、依赖和构建 preset 清单。

增量验证不会拆散 `core_config.h`。`components.json` 只按宏前缀判断某一类参数属于哪个
验证组件：例如 `TEMPLATE_CAR_` 归车辆验证，`TEMPLATE_GIMBAL_` 归云台验证。这样修改
一个车辆 PID 时，不会重复验证与它无关的通信和云台代码；参数的编辑入口仍然只有
`core_config.h`。

`core_config.h` 只收录源码当前实际消费、需要经常整定的默认值，例如 Task 周期、
优先级和栈，PID/Kalman/滤波参数，以及阈值、限幅和超时。参数按大类集中，不为每个
算法单独拆文件；每组参数必须注释用途、单位、约束和影响，不得加入未使用的占位项。

引脚、时钟、DMA 和 IRQ 仍以 SysConfig/BSP 为准；FreeRTOS 内核配置仍在
`FreeRTOSConfig.h`；器件物理参数、协议常量和构建开关仍由 Device、协议头文件和
CMake/presets 持有。固定数学公式和公共 API 结构容量也不迁入核心参数表。

编译产物、缓存和日志只能进入根目录 `build/`。项目决策、审计和验证记录只能
进入根目录 `docs/`。真实探针序列号属于机器本地状态，只能放在模板同级的
`TI ENV/local/openocd/`，不得写入本目录。
