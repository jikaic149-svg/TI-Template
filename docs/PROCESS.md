# 工程变更记录

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
  `APP/Algorithm/Balance` 和 `Device/Communication/ChassisCommand`；`Filter` 按低通、
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
  减速比20.409；MG513X使用减速比28。用户最终确认按GMR 500线和QEI四倍频
  换算，输出轴每圈计数分别固定为MG310 `500×20.409×4=40818`、MG513X
  `500×28×4=56000`。
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
  哪一种驱动profile。输出轴CPR的软件换算已经确定；手转一圈实测仅作为实物验证。
