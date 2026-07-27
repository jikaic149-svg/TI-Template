# 工程变更记录

## 2026-07-27：分离模拟循迹产品配置与 Task 控制流程

- 修改原因：模拟循迹 Task 原先把设备逻辑索引、机械参数、已验证方向和控制参数直接
  定义在 `task_analog_tracking.c`，产品配置与周期控制流程混在同一实现文件中。
- 最终决策：新增 Task 私有声明式配置 `task_analog_tracking_config.h`，集中保存上述产品
  绑定与参数；Task 继续负责实例化 Device 并向 Mode 提交数据，Mode 不依赖 Device/BSP。
  编码器每轮 1040 计数、轮周 150.796447 mm、左右方向及所有运行行为均保持不变。
- 验证边界：本次不改变调度超时策略，也不把 KEY 当作 HardFault、断言或栈溢出时的
  电机保护；致命异常安全停机需要独立的异常钩子或硬件失能链路。

## 2026-07-27：同步已验证的芯流 T8 模拟循迹链路

- 修改原因：用户完成当前 `test` 工程实车调试后，明确要求把现行循迹代码同步回
  `D:\TI MSPM0\TI Template`。
- 最终决策：仅增量同步循迹链路，不覆盖模板已有惯导、导航、Communicate、电机和
  功能底板改动，也不恢复已删除的视觉循迹。新增芯流 T8 `0x19` 八路 ADC UART Device、
  `Device_LineTracking_UpdateRaw/UpdateAnalogRaw`、独立 Digital/Analog Tracking Mode，
  以及互斥的 `LineTrackingReadout` 和 `AnalogTracking` 产品 Task profile。
- 模拟量算法：每帧减去八路共同最小值，不做逐通道阈值或黑白标定；有线判定和置信度
  使用八路总差值，方向使用加权差比和。T8 产品权重为
  `-1,-0.60,-0.25,0,0,0.25,0.60,1`，中心两路只确认有线，边缘权重更大。
- 控制行为：10 ms 静态 Task。`AnalogTracking` 为循迹 PID（Kp 1.5）→ yaw 角度 PID
  （Kp 10.0）→ 双轮轮速 PI 三级串级；基础速度 250 mm/s，相对 yaw 最大 ±25°，
  单轮目标限幅 ±800 mm/s。KEY 启停并以当前 yaw 为基准，RGB 指示状态，蓝牙约 25 Hz
  输出固定 14 列。来源工程已由用户确认轮速环、角度环和循迹极性。
- 硬件资源：G3519 两份 SysConfig 都增加 UART3，PC6 TX、PC7 RX、115200 8N1；
  BSP UART 逻辑索引 3，新增 `UART3_IRQHandler`。蓝牙继续使用 UART4/PB10/PB11。
- 涉及文件：`APP/Algorithm/LineTracking`、`BSP/MSPM0G3519`、
  `Device/Sensor/LineTracking`、`Mode/Car/Tracking`、`Task/LineTracking`、根/子目录
  CMake 与 presets、`tools/mspm0.ps1`、循迹测试和文档。`vofa+.csv` 未同步。
- 验证方式和结果：Template 全部 host 测试通过，包含 T8 解析、模拟相对差、中心有线
  判定、非线性权重和两个 Mode。AnalogTracking Debug/Release 完整 ARM 构建通过，
  text/data/bss 为 `51176/5412/7212`、`48864/5412/7212`；LineTrackingReadout
  Debug/Release 为 `26744/5412/6420`、`26008/5412/6412`，HEX 范围检查通过。
- 已知限制：本次只同步并构建 Template，未重新烧录 Template 产物；T8 供电/UART
  电平、机械高度和间距仍需按实物确认。
- 待确认事项：第二种八路数字量模块的品牌、协议和硬件绑定；它继续走独立 Digital Mode。

## 2026-07-27：同步 PC 与双 MCU Bluetooth Communicate Mode

- 修改原因：用户要求中央模板提供单 MCU 对电脑与双 MCU 两种蓝牙通信模式，供具体
  产品 Task 按需求选择。
- 最终决策：模板新增 `Mode/Communicate/Bluetooth`，公开 `BLUETOOTH_MODE_PC` 和
  `BLUETOOTH_MODE_DUAL_MCU`。严格沿用现有 Mode 结构：两个具体模式各有独立源文件和
  `Context/Init/Update`，公共类型集中在 `include/mode_communicate.h`。Mode 不访问
  UART、BSP、Device、FreeRTOS 或 Task，不持有协议实例，不选择或切换模式。模板只
  提供通用组件；双车专用 Task 没有复制进模板。
- 旧方向：单一 `mode_communicate.c` 加通用 `GetPolicy()` 的特殊结构已被用户否决并删除。
- 涉及文件：`Mode/Communicate`、根 CMake/Presets、`Mode/README.md`、host/ARM
  测试脚本、`tests/mode_communicate` 和本记录。同步为最小增量，模板原有未提交修改保留。
- 验证方式和结果：JSON 与 PowerShell 语法、全部 host 测试、Cortex-M0+ 严格编译和
  架构检查通过。显式启用 `TEMPLATE_ENABLE_MODE_COMMUNICATE=ON` 后，G3507
  Debug/Release 与 G3519 Debug/Release 均完整构建通过，text/data/bss 分别为
  `5208/124/3444`、`4712/124/3444`、`11832/4140/3804`、
  `11416/4140/3796`。
- 已知限制：模板没有选择通信模式的产品 Task，也没有蓝牙模块硬件映射；UART、波特率、
  帧收发和协议实例必须由派生产品提供。
- 待确认事项：无；派生产品按自身需求在 Task 初始化时请求其中一个模式。

## 2026-07-22：MSPM0G3519 功能底板硬件映射

- 修改原因：按功能底板原理图释放 PA12/PA13 给 CAN，并增加第二路舵机与受控 5 V。
- 最终决策：CANFD0 使用 PA12/PA13；舵机使用 TIMG12_CCP0 PB13 和 CCP1 PA16；受控 12 V/5 V 分别使用 PA15/PB26；蜂鸣器按用户最终确认改为 PA7 GPIO。
- 涉及文件：`BSP/MSPM0G3519/MSPM0G3519.syscfg`、`bsp_resource_map.h`、`bsp_servo.c`、`bsp_power_switch.c`、BSP 构建清单及板级文档。
- 验证方式和结果：TI SysConfig 1.27.1 生成成功；43 个显式分配引脚无重复；MSPM0G3519 Debug 完整构建成功。TIMG12 生成时钟 10 MHz、周期 200000 ticks。
- 已知限制：CAN 仅完成 PinMux、控制器和 DriverLib 配置；`BSP_CAN` backend 仍未实现，因此能力标志保持关闭。蜂鸣器仅完成 GPIO 配置，尚无公共 BSP 接口。
- 待确认事项：CAN 收发器 CRX 的 3.3 V 电平兼容性；两只舵机堵转电流与 5 V 电源容量；受控 5 V 外部开关的默认关闭电路；实物 PA7 蜂鸣器网络是否已同步改板。

## 2026-07-22：暂时停用 CAN，补齐电源与蜂鸣器接口

- 当前决策：暂不启用 CAN；移除 G3519 SysConfig 的 MCAN 实例和 `mcan` DriverLib 模块，PA12/PA13 暂不分配。
- 接口调整：受控 12 V/5 V 统一使用 `BSP_PowerSwitch_*` 和 `Device_PowerSwitch_*`；PA7 增加 `BSP_Buzzer_*` 公共控制接口。
- 验证方式和结果：G3519 Debug/Release、G3507 Debug，以及 Servo + PowerSwitch + QD4310 全功能构建通过；架构检查通过。

## 2026-07-24：同步 Vision 与单 IMU 可复用模型；撤回 Cannon 状态机

- 修改原因：用户指定将 `vision_track`、单 ICM-45686、单 BMI088 解算，以及
  `summer_jixun3` 的继电器/LCD控制同步回中央模板；随后明确否决未经批准新增继电器/
  LCD 状态机。
- 最终决策：`vision_track_mode` 进入 `Mode/Car`，保持 400 mm/s 基础速度、横向与
  航向误差合成、yaw 外环和双轮速度内环，丢线立即归零。ICM-45686 与 BMI088
  分别通过 `Device_*_UpdateIMU()` 提交统一 `Device_IMU`，复用右手轴映射、静止
  零偏标定和六轴 Mahony；任一陀螺轴达到 ±32000 count 时不提交姿态。此前擅自新增的
  `Mode_Cannon`、CMake 选项、测试和文档已全部删除；继电器/LCD 同步方式待用户明确。
- 构建与依赖：启用任一 ICM45686 或 BMI088 选项会自动带入 `Device_IMU` 与
  `Algorithm_Mahony`。没有修改 BSP、SysConfig、Task 周期、优先级或启动行为。
- 涉及文件：`Mode/Car/Tracking/vision_track_mode.c`、对应公开头文件与 CMake，
  BMI088/ICM45686 Device/CMake/README，根 CMake/README、Device/参数/决策文档，
  以及 `tests/`。
- 验证方式和结果：Vision、单 ICM/BMI 两组 host 测试通过；新增/修改源在
  Cortex-M0+ 上以 `-Wall -Wextra -Wpedantic -Werror` 严格编译通过；模板架构检查和
  JSON 解析通过。全功能 G3519 Debug/Release 分别为 text/data/bss
  `11664/4140/3804`、`11328/4140/3796`，G3507 Debug 为 `5208/124/3444`；
  ICM-only 与 BMI-only 两套 G3519 Debug 均独立完整构建通过。
- 已知限制：本次未烧录。两颗 IMU 的 SPI backend、安装轴和实物静止零偏仍需产品
  配置；视觉转向符号和增益仍需车辆实测。继电器/LCD没有同步任何新增实现。
- 待确认事项：用户明确继电器/LCD希望同步的具体文件和边界后再处理；IMU 产品接入时
  登记安装轴与采样时序，再执行静止标定和低风险视觉转向测试。

## 2026-07-24：按实际算法整理目录与名称

- 修改原因：用户要求去除模糊名称、占位算法和不需要的串口命令解析器，并拆开混合的滤波实现。
- 最终决策：`Attitude` 完整改名为 `Mahony`，`Control` 完整改名为 `PID`；删除
  `APP/Algorithm/Balance` 和不需要的串口命令解析组件；`Filter` 按低通、
  滑动平均、三点中值、Biquad、通用一维 Kalman 拆成独立头文件和源文件。
- 涉及文件：根 CMake/Presets/README、`APP/Algorithm`、`Device/Sensor/IMU`、
  `Mode/Car`、测试脚本和对应文档。
- 验证方式和结果：Vision 与单 IMU host 测试通过；Cortex-M0+ 严格编译和架构检查通过；
  G3519 Debug 启用 Car、Filter、ICM45686、BMI088 后完整构建通过，六个 Filter 源文件均进入编译。
- 已知限制：这是公共 API 改名，不保留旧 `Algorithm_Attitude`/`Algorithm_Control` 兼容层。
- 待确认事项：`LineTracking` 是否保留由用户另行决定；本次未修改其代码。

## 2026-07-24：精简 Kalman 并删除 Balance Mode

- 修改原因：用户只保留一个可由任意调用方使用的 Kalman，并明确删除 `Mode/Balance`。
- 最终决策：删除角度专用 Kalman，将标量实现改为通用一维
  `Algorithm_Filter_Kalman_*`；删除 `Mode/Balance` 及其 CMake 选项和文档入口。
- 涉及文件：`APP/Algorithm/Filter`、根 CMake/Presets/README、`Mode/Balance` 和对应文档。
- 验证方式和结果：Vision、单 IMU host 测试通过；Cortex-M0+ 严格编译、架构检查和
  JSON 解析通过；G3519 Debug 启用 Car、Filter、ICM45686、BMI088 后完整构建通过，
  编译链中只有 `algorithm_filter_kalman.c` 一份 Kalman 实现。
- 已知限制：不保留 AngleKalman/ScalarKalman 兼容 API；删除内容无法从当前未跟踪仓库恢复。
- 待确认事项：310 与 513x 电机参数由用户后续提供；参数到齐前不拆 Motor、不创建占位目录。

## 2026-07-24：同步 AT8236/TB6612 profile 与两种 GMR 电机参数

- 修改原因：用户指定参考 `C:\Users\rlyoulo\Desktop\test` 的已验证 MSPM0
  电机实现，并确认实际使用 MG310 GMR版和 MG513X GMR版，两者均为500线。
- 最终决策：保留一个 `Device_Motor` 公共接口；G3519 在编译期通过
  `MSPM0_MOTOR_DRIVER=TB6612|AT8236` 选择互斥 SysConfig 与 BSP backend。
  TB6612 保持默认和原有引脚；AT8236 使用 TIMA0 四路同步 20 kHz PWM：
  PC2/CCP0、PC4/CCP1、PB20/CCP2、PA25/CCP3，输出为正转 `PWM/0`、
  反转 `0/PWM`、滑行 `0/0`、刹车 `1/1`。没有新增状态机或保护机制。
- 用户纠正：先前建立独立 `Device_AT8236` 的方向被参考工程否决，该目录、API、
  CMake选项和测试已经删除，不再代表当前实现。
- 电机型号：新增 `Motor/MG310` 与 `Motor/MG513X` 参数目录。MG310 使用实际
  减速比20.409，并按已确认的QEI四倍频记录输出轴每圈`40818`计数；MG513X使用
  减速比28和GMR 500线原始标称。先前把MG513X直接固定为`500×28×4`的方向已被
  否决，其“500线”边沿定义、QEI倍率解释和输出轴CPR需手转一圈实测确认。
- 蓝牙串口：G3519的TB6612/AT8236两套profile均配置UART4（PB10 TX、PB11 RX、
  115200 8N1、RX DMA_CH2）。`main -> BSP_Init -> BSP_UART_Init`会在上电时完成
  初始化并启动接收DMA；G3519 Debug/Release preset均启用现有Serial Device。
  没有新增蓝牙状态机、Task或回传协议。
- 涉及文件：根 CMake/Presets/README、`tools/mspm0.ps1`、G3519 SysConfig、
  Motor/PWM BSP与资源映射、Motor型号目录、Motor真值表/型号测试及硬件文档。
- 验证方式和结果：TB6612与AT8236两套host真值表、型号参数测试、ARM严格编译、
  JSON/PowerShell语法和模板架构检查通过；TI SysConfig 1.27.1 对两套profile均
  生成成功。G3519 Debug/Release完整构建通过：TB6612 text/data/bss为
  `11832/4140/3804`、`11416/4140/3796`，AT8236为`11760/4156/3796`、
  `11400/4156/3788`；四套构建的HEX范围检查均通过；G3507 Debug回归构建通过。
- 已知限制：本次未烧录，未对四路PWM波形、电机方向、电流和温升做实物验证。
- 待确认事项：编码器接口电平、所在轴、计数正方向，以及MG310/MG513X分别搭配
  哪一种驱动profile。MG513X输出轴CPR仍需手转一圈实测确认。

## 2026-07-26：同步两套 SPI IMU、棱科 M0 与 MG513X 轮径

- 修改原因：用户要求模板收录后车 BMI088 SPI、前车 ICM45686 SPI、棱科 M0，以及
  当前 MG513X 的已确认轮胎尺寸。
- 最终决策：BMI088 增加复用逻辑 BSP SPI handle 的双 CS 适配；ICM45686 增加单设备
  SPI 适配；棱科 M0 作为可选 UART IMU Device 加入根 CMake/Presets。三者保持来源
  工程行为，不新增 Task 或状态机。MG513X 记录轮径 `65 mm`、名义轮周 `204.20 mm`；
  只保留500线和28减速比，不写死未确认的输出轴CPR。
- 硬件资源：G3519 BMI088 为 SPI0 10 MHz Mode 0、PC0/PC1 双软件 CS；ICM45686 为
  SPI1 10 MHz Mode 0、PB12 软件 CS，INT1/INT2仍未启用。棱科 M0 的 UART 由产品绑定。
- 涉及文件：根 CMake/Presets/README、`Device/Sensor/IMU` 三个设备目录、MG513X
  参数目录、设备参数/决策文档和主机测试。
- 验证方式和结果：各驱动核心源码与对应来源工程 SHA-256 一致；主机测试全部通过。
  G3519 AT8236 Debug 同时启用 BMI088、ICM45686、棱科 M0 后，使用 `cmake --fresh`、
  `--clean-first` 完整构建成功，三套 Device 及两套 SPI 适配均进入编译清单。
- 已知限制：本次未烧录，未读取实物 Chip ID、检查 SPI/UART 波形或确认安装轴；
  `204.20 mm` 是由直径计算的名义轮周，有效滚动轮周仍需实测。
- 待确认事项：MG513X输出轴CPR、两颗IMU安装轴和棱科M0产品UART绑定。

## 2026-07-26：产品控制 Mode 不进入通用模板

- 修改原因：用户确认双车跟随、视觉循迹和融合导航仍需继续优化，不能作为稳定通用
  Mode 写入模板。
- 最终决策：先前把视觉循迹 Mode 同步回模板的方向已被否决。模板删除该 Mode 的源码、
  公开头文件、CMake入口、专用 host/ARM 测试和现行文档入口；双车逻辑与融合导航原本
  就未进入模板。通用 `Mode/Car` 角度环、轮速环和基础循迹 Mode 保留。前后车产品代码
  与 Task 行为均不修改。
- 涉及文件：`Mode/Car`、`tests/run_gcc_tests.ps1`、`tests/run_arm_compile.ps1`、
  `docs/PROJECT_DECISIONS.md` 和本记录。
- 验证方式和结果：排除历史记录后，模板源码、测试及全新构建目录中无三类产品逻辑
  标识或文件。删后 host 测试和 Cortex-M0+ ARM 严格编译通过；G3519 AT8236 Debug
  同时启用通用 Car、BMI088、ICM45686、棱科 M0，使用 `cmake --fresh`、
  `--clean-first` 完整构建成功，编译清单只含通用 Car Mode。
- 已知限制：未烧录；本次只验证模板构建边界，不代表三套产品 Mode 已完成优化。
- 待确认事项：三套产品 Mode 分别在对应车辆工程内优化和验证，稳定前不再同步模板。

## 2026-07-27：仅将通用融合惯导 Mode 同步回模板

- 修改原因：用户明确要求只把当前 `test` 中的惯导融合能力同步回模板，其他产品功能
  继续在当前工程测试。
- 最终决策：此前“融合导航暂不进入模板”的方向已被本次用户指令覆盖。模板仅新增
  `Mode_Car_FusedNavigation`：消费标准左右轮距离/速度和已校正机体 Z 轴角速度，完成
  局部 `x/y/yaw` 融合、路径前视曲率、yaw-rate 与双轮速度串级计算；不绑定具体 IMU、
  编码器、UART、RTOS 或 MCU。模拟循迹、13 PPR 轮速参数、蓝牙、RGB 和产品 Task 均未同步。
- 涉及文件：`Mode/Car/Navigation/`、`Mode/Car/include/mode_car_fused_navigation.h`、
  `Mode/Car/CMakeLists.txt`、`Mode/Car/README.md`、对应主机单元测试和本记录。
- 验证方式和结果：模板源码通过现有 FusedNavigation 主机单元测试，并以
  `-Wall -Wextra -Wpedantic -Werror` 编译；G3519/TB6612 Debug、Release 均使用
  `cmake --fresh`、`--clean-first` 完整构建成功，编译清单包含 FusedNavigation 源码。
- 已知限制：未新增或启用产品 Task，未烧录，也未验证 ICM45686、轮编码器方向和实车轨迹。
- 待确认事项：其余车辆功能继续在 `C:\Users\rlyoulo\Desktop\test` 实测，用户确认后再
  决定是否同步模板。

## 2026-07-27：加入通用双 IMU 融合算法

- 修改原因：用户要求将当前工程的 `APP/Algorithm/IMUFusion` 加入通用模板。
- 最终决策：模板原样加入纯算法 `Algorithm_IMUFusion` 及公共 API；输入为已换算单位的
  两颗 IMU 加速度、角速度、有效标志和采样周期，算法负责机体系轴映射、逆方差与预测
  置信度融合、杆臂补偿、可选启动/运行时陀螺零偏处理、六状态协方差和重力方向修正。
  BMI088 与 ICM45686 同时启用时自动构建该算法及 Mahony 类型依赖。未新增产品 Task、
  Mode 或默认 preset，现有上电行为不变。
- 涉及文件：`APP/Algorithm/IMUFusion/`、根目录和 Algorithm CMake/README、IMUFusion
  主机测试、ARM 严格编译入口及本记录。
- 验证方式和结果：算法与测试源码的规范化内容和来源工程一致；全部主机测试、
  Cortex-M0+ 严格编译和模板架构检查通过。独立目录中的 G3519 Debug/Release 完整构建
  均通过，并确认 `algorithm_imu_fusion.c`、`device_dual_imu.c`、BMI088/ICM45686 SPI
  适配四个编译单元全部进入构建；尺寸分别为 text/data/bss `12088/5412/3852` 和
  `11600/5412/3844`。Cortex-M0+ O2 的静态栈报告中 `Algorithm_IMUFusion_Init` 为
  `184 B`、`Algorithm_IMUFusion_Update` 为 `1392 B`。
- 已知限制：本次没有把 Device 样本送入算法，没有创建周期调用者，也未烧录或做双 IMU
  实物验证；六轴输入不能提供绝对 yaw 可观测性。后续 Task 必须为 1392 B 更新路径及其
  调用链预留栈余量，并上板检查高水位。
- 待确认事项：产品调用方仍需配置两颗 IMU 的安装轴、相对位置、采样周期、有效状态及
  可选轮速静止判据，再进行实车参数验证。
