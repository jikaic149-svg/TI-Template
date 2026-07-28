# TI MSPM0 Template：AI 工作入口

本文件只记录每次任务都需要的当前规则。历史原因查 `docs/PROCESS.md`；不要把历史记录
当作当前源码事实。

## 首要原则

- 先读目标源码和同目录 README，再修改；不要依赖旧对话或历史记录猜行为。
- 保留工作树中的既有改动，只修改用户授权范围；禁止顺手重构。
- 使用 `rg`/`rg --files` 定位文件。需要搜索 `.rgignore` 排除内容时，明确指定文件，
  或使用 `rg -uu`。
- 只编辑源码和声明式配置；不编辑 `build/`、SysConfig 生成物、ELF/HEX/MAP。
- 本地文件修改使用补丁；构建、格式化和生成工具可以更新其正常输出目录。
- 未经明确要求不烧录、不改 SysConfig/引脚、不增加 Task/状态机/重试/看门狗行为。

## 分层边界

- `BSP`：唯一拥有 MCU、引脚、寄存器、SysConfig、DMA、ISR 和外设实例。
- `Device`：器件协议、状态和单位；不选择产品模式，不写固定 MCU 引脚。
- `APP/Algorithm`：无 BSP、Device、RTOS 依赖的纯算法。
- `APP/System`：允许全局使用的硬件无关系统策略；不得直接访问寄存器。
- `Mode`：单一业务/控制策略；不访问 BSP、Device、FreeRTOS 或 Task。
- `Task`：周期、优先级、产品选择、Device/Mode 编排和运行期安全门。
- `main.c`：初始化、创建 Task、启动调度器，不放产品业务。

跨模块运行期数据优先走现有 Pub/Sub；初始化、模块私有实现和纯算法可直接调用。
禁止用跨层 `extern` 或持续 Getter 轮询共享可变业务状态。

## 当前真值入口

| 问题 | 唯一优先入口 |
|---|---|
| MCU 引脚、外设、DMA、ISR | `BSP/<MCU>/*.syscfg`、`bsp_resource_map.h`、BSP实现 |
| 器件参数、单位、待确认项 | `docs/DEVICE_PARAMETERS.md` |
| CMake组件与传递依赖 | 根 `CMakeLists.txt`、组件 `CMakeLists.txt` |
| 构建profile | `CMakePresets.json` |
| 当前产品行为 | `Task/` 源码及 `Task/README.md` |
| 架构与保护约束 | `README.md`、`docs/WATCHDOG.md`、`tools/check_template.ps1` |
| 修改历史和否决原因 | `docs/PROCESS.md`，只在确实需要历史时读取 |

同一事实冲突时，以当前源码/声明式硬件配置为准；文档冲突必须同步修正，不能自行选择
更方便的旧结论。

## 通信边界

- 普通蓝牙无线透传直接使用 `Device_Serial`；没有额外 Bluetooth Device。
- Bluetooth Communicate Mode 的 `UNRESTRICTED` 只表示软件不互斥 TX/RX，不证明无线
  PHY 物理全双工。
- `Device_LoRa` 组合定点路由前缀和透明 UART；LoRa 地址属于模块，不绑定 MCU 或探针。
- LoRa 半双工轮次由 `Mode_Communicate_LoRaHalfDuplex` 管理；默认没有 ACK、重发、
  超时或失步恢复。

## 修改与验证路由

- 日常源码、配置和构建修改：优先运行
  `tools/project.ps1 -Action Verify -Scope Auto`；它按变更组件选择 host 测试、ARM 编译和
  需要的构建 profile，不把缓存结果当作实物验证。
- 仅文档：运行 `tools/check_template.ps1` 和 `git diff HEAD --check`，不触发组件编译。
- 首次建立缓存或准备可审计基线：运行
  `tools/project.ps1 -Action Verify -Scope All -Clean`；不得把日常 Auto 验证表述为全量验证。
- CMake、BSP、Task 或公共 API 必须在 `config/verification/components.json` 中映射到受影响的
  Debug/Release profile，不能只通过 host 或单文件编译得出完整构建结论。
- SysConfig/引脚：同时运行 TI SysConfig 生成，核对资源映射、波特率/PWM/时钟和对应
  Debug/Release 完整链接。
- 烧录：只有用户明确要求时执行；显式选择探针，写入后完整回读校验，再报告运行结果。

PowerShell 脚本受本机执行策略限制时，使用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File <script>
```

验证结论必须区分 host 测试、ARM 编译、完整链接和实物验证；前三级通过不能写成硬件
已验证。

## 禁止扩大范围

- “同步/移植/适配”默认保持来源行为，不授权重新设计。
- 通用模板不自动接收仍在调试的产品 Task、机械极性、私有状态机或临时测试入口。
- 不因一个 Device 需要 Serial 就默认启用所有通信组件；使用现有 CMake 传递依赖。
- 不为未来需求预建抽象层、回调表、自动恢复或兼容 API。
