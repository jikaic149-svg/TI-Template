# 工程变更记录

## 2026-08-01：修复M3短按任务状态被链路门控吞掉

- 修改原因：实机中F407持续收到`mode=3`，但`task_active`始终为0。M3启动分支在读取短按事件后仍要求`g_BRINGUP_Fault==0`、PeerLink已初始化且远端心跳在线；任一条件暂时不满足都会吞掉该次按键。通用底盘停止分支还先于M3分支执行，存在被无关`g_tracking_active`状态截获的风险。
- 最终决策：M3短按优先于通用底盘启停处理，并且每次都无条件翻转本地任务活动位。该状态只随下一帧100 Hz `CAR.task_active`发送；不提交`BTG/BST`，不调用底盘电机或F407 QD4310/CAN的Enable/Disable。M3活动期间长按仍不切换赛题，其他赛题的启停门控保持不变。
- 涉及文件：`Task/task_bringup_chassis.c`、`tests/mcu_peer_link/test_device_peer_link.c`及本记录；未增加Task、动态内存、协议字段、周期或公共API。
- 验证方式和结果：Communication Host GCC测试通过，回归明确验证活动位1编码为九字段`CAR`的第九字段；G3519 Bring-up Debug/Release完整链接通过，FLASH/SRAM_BANK0分别为`85584/19280 B`和`81200/19272 B`；`git diff --check`通过。
- 已知限制与待确认：本轮未烧录。部署后需在M3短按一次确认F407状态出现`0x4000`且Maix开始Task3，再短按一次确认仅清除任务位、F407本地电机使能状态不变。

## 2026-07-29：回退 G3519 Bring-up 边缘权重与循迹 Kp

- 修改原因：用户实车观察到边缘灰度权重和循迹比例增益过大，强偏差时纠偏过猛。
- 最终决策：仅调整 G3519 Bring-up 产品参数。T8 从左到右的位置权重由
  `-1,-0.60,-0.25,0,0,0.25,0.60,1` 回退为
  `-0.70,-0.45,-0.20,0,0,0.20,0.45,0.70`；该 Task 的循迹 `Kp` 由继承通用
  `1.2` 改为产品私有 `0.70`。停车、重新发车和初始化都使用同一私有配置，避免复位后
  恢复到通用增益。通用模拟/数字循迹默认值、`Ki/Kd`、250 mm/s 基速、80% 电机命令
  限幅、极性、周期和安全门均未改变。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、循迹 host 测试、
  Task/设备参数/协作说明和本文档。
- 验证方式和结果：LineTracking host 测试、Cortex-M0+ 严格编译、G3519 Bring-up
  Debug/Release 完整链接及 `git diff HEAD --check` 通过；Debug/Release FLASH 分别为
  `64912/60736 B`，SRAM_BANK0 分别为 `18552/18544 B`。
- 实物边界：本次未烧录、未重新采集 VOFA，软件验证不能替代赛道实测；下一次实测重点
  观察 CH0 强偏差时 CH3/CH4 是否仍频繁或持续达到 `±8000`。

## 2026-07-29：G3519 Bring-up 切换为芯流 T8 八路模拟循迹闭环

- 修改原因：用户要求小车使用模拟灰度实现闭环，并在完成后烧录，同时确认控制极性。
- 最终决策：保留同一 10 ms 静态 Bring-up Task 和现有双轮速度内环；撤销按键后的
  原地绝对 yaw 锁定。第一次 KEY 启用两路底盘电机和模拟循迹，第二次 KEY 仍立即
  `Stop + SetUnlocked(false)`，并向 F407 非阻塞发送一次 `DIS`。T8 继续每 20 ms
  查询一次；每个新 ADC 快照经 `Device_LineTracking_UpdateAnalogRaw()` 做八路共同
  基线消除和模拟位置估计，再由 `Mode_Car_AnalogTracking` 以 250 mm/s 为前进基准
  生成左右轮速目标。通道 CH1..CH8 从左到右的权重为
  `-1,-0.60,-0.25,0,0,0.25,0.60,1`；丢线、置信度低于 0.10 或快照超过 100 ms 时
  立即清零两路电机命令，后续有效新帧恢复闭环。未新增 Task、动态内存、重试或状态机。
- 遥测与显示：VOFA 保持固定 21 列，CH0 从已撤销的 yaw 锁定目标改为归一化线误差
  （右偏为正、样本过期为 0），CH1～CH20 顺序不变。TFT 第一行显示 `LINE:STALE`、
  `LINE:LOST` 或带符号误差，其他 PeerLink/QD4310 状态页字段保持不变。
- 构建接入：Bring-up profile 自动带入通用 `device_line_tracking` 和差比和算法；验证
  清单将 IMU、Navigation 和 LineTracking 的修改映射到 Bring-up Debug/Release，避免
  只通过 host 或 feature-smoke 构建得出产品结论。
- 涉及文件：根/Task CMake、`Task/task_bringup_chassis.*`、`config/core_config.h`、
  `config/verification/components.json`、Task/设备参数/协作说明和本文档。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过，共执行十个
  组件；模板架构、差异检查、循迹及相关 host 测试、Cortex-M0+ 严格编译和完整链接均
  通过。Bring-up Debug/Release 使用 FLASH `64880/60720 B`，SRAM_BANK0
  `18552/18544 B`，编译零警告。
- 烧录与回读：显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908` 的 G3519 DAPLink，写入
  Bring-up Debug `64880 B`；从主 Flash `0x00000000` 按精确长度完整回读，源 BIN 与
  回读 SHA-256 均为
  `A994469198A8417E2F6370D52BC96425A115BD4AC9883B123BB64CB4DBF37A45`，随后执行
  `reset run`。F407 探针和固件未修改。
- 极性实物结论：用户确认车辆纠偏方向正确；同步读取的 VOFA 文件有 1197 帧固定
  21 列数据，其中 204 帧存在闭环电机输出。强偏差 `|CH0|>=0.3` 共 94 帧，94/94 帧
  的 `CH3-CH4` 与 CH0 同号，88/94 帧的 `CH1-CH2` 与 CH0 同号，其余为换向瞬态。
  代表帧包括右偏 `CH0=+0.749` 时 A/B 命令 `+8000/-3372`，以及左偏
  `CH0=-0.526` 时 A/B 命令 `-5454/+8000`。结合用户实车观察，T8 左右通道、黑线
  有效极性、左右电机及编码器符号链在当前安装上确认正确。
- 待确认事项：极性不再是阻碍；下一步按赛道曲率和机械速度能力整定 250 mm/s 基速、
  循迹 PID，并关注当前强偏差时两路命令达到 80% 限幅的持续时间。

## 2026-07-29：Agent A 接收 QD4310 原始绝对值并追加 VOFA CH20

- 修改原因：用户要求读取协作文档并完成交接 `20260729-01`，把 F407 已发送的
  QD4310 绝对编码器原始值加入 VOFA 回传。
- 最终决策：G3519 PeerLink 新增独立 `ENC` 接收：Payload 固定为
  `<node_id>,<motor_id>,<angle_raw>,<online>`，只接受 F407 节点 1 发往 G3519
  节点 2。`angle_raw` 原样保存为 `uint16_t 0..65535`、无单位，并使用独立序号、
  Latest Value、接收时间和 Payload/来源/重复诊断。蓝牙保持 CH0～CH19 原顺序与单位，
  仅在末尾追加 CH20；`ENC` 超过 200 ms 或 `online=0` 时 CH20 置 0。
- 架构边界：没有修改 UART/DMA/ISR/引脚、10 ms Task 周期或 20 ms VOFA 周期；没有
  新增 Task、动态内存、ACK、重发或控制动作。保留 Agent A 已实现的 `DIS`，并与
  Agent B 的 `ENC` 增量合并。静态 FireWater 缓冲仍为 224 B。
- 涉及文件：`Device/Communication/PeerLink/`、`Task/task_bringup_chassis.*`、
  `config/core_config.h`、PeerLink host 测试、Task/PeerLink README、桌面
  `COOPERATION.md` 和本文档。
- 验证方式和结果：Auto 增量验证通过；模板架构、`git diff HEAD --check`、
  Communication host 测试和 7 个通信源 Cortex-M0+ 严格编译均通过。测试覆盖原始值
  `61047`、`65535` 边界、非法 `65536`、规范十进制、来源、重复序号和 200/201 ms
  新鲜度。G3519 Bring-up Debug/Release 完整链接通过，text/data/bss 分别为
  `50552/5956/7572 B`、`47064/5956/7564 B`，BIN 为 `56440/52952 B`；相对前一版
  静态 RAM 增加 32 B。同步复核 F407 当前 Debug/Release 构建通过，BIN 为
  `20900/17648 B`；Debug SHA-256 为
  `8D7DC1F20A563476A97AEE2FF96382D6BA1F0DA0AD31DAB23C7D3B77DEB65BDA`，G3519
  Debug SHA-256 为
  `EA097067C7D7ADB1F8137F2191B44E6AE50ACAA45B1E86BDC597339574DD8D36`。
- 已知限制：本轮未烧录；F407 `ENC` 发送端也尚未烧录，因此 21 列 FireWater、原始值
  跨零和 200 ms 置零尚未完成双端实物验证。原始 16 位字段不代表物理编码器分辨率。
- 待确认事项：双端烧录后在 COM5/VOFA 检查每行固定 21 列；转动 QD4310 验证 CH20
  在 `0..65535` 跨零、静止值稳定；断开 F407 TX 后确认 CH14～CH20 不超过 220 ms
  置零且 M0 CH0～CH13 继续更新。

## 2026-07-29：Agent A 停车 KEY 提交 F407 主电机 Disable 请求

- 修改原因：用户要求 G3519 主 KEY 进入停车状态时，不仅停车，还要 Disable 滚球侧
  主电机。
- 最终决策：保留第二次 KEY 按下时两只底盘电机原有 `Stop + SetUnlocked(false)`；
  随后由同一 10 ms Task 非阻塞提交一次独立 Packet `DIS`。Payload 固定为来源节点
  `2`，仅允许 G3519 节点 2 发往 F407 节点 1，使用独立 16 位序号。重复合法帧仍产生
  幂等 Disable 事件；发送队列满整帧丢弃并计数，不阻塞、不自动重试、无 ACK。
- 架构边界：Agent A 只实现 G3519 发送、共享 PeerLink 编解码/诊断和接收事件 API；
  没有修改 F407 滚球业务。Agent B 需在 F407 Task 的 PeerLink Step 后消费事件并调用
  主 QD4310 `0 A + Disable`。`HB`、`TEL`、`ENC`、Task 周期、DMA/ISR 和 VOFA 字段
  不变。
- 涉及文件：`Device/Communication/PeerLink/`、`Task/task_bringup_chassis.*`、
  PeerLink host 测试、Task/PeerLink README、桌面 `COOPERATION.md` 和本文档。
- 验证方式和结果：Auto 增量验证通过；模板架构和 `git diff HEAD --check`、
  Communication host 测试、7 个通信源 Cortex-M0+ 严格编译均通过。G3519 Bring-up
  Debug/Release 完整链接通过，text/data/bss 分别为 `49992/5956/7540 B`、
  `46576/5956/7532 B`，FLASH/SRAM 分别为 `55880/13488 B`、`52464/13480 B`。
- 已知限制：本轮未烧录。F407 还未消费 `DIS`，因此尚未形成 KEY 到 QD4310 Disable
  的双端实物闭环；无 ACK/自动重试意味着队列满时该次请求会被丢弃并留在诊断计数中。
- 待确认事项：Agent B 完成 `20260729-02` 交接后，先使能 QD4310，再按 G3519 停车
  KEY，确认两只底盘轮电机锁定且 `TEL.enabled` 变为 0，双方 `DIS` 错误/丢弃计数为 0。

## 2026-07-29：F407 与 M0 数据合并到 G3519 蓝牙固定 20 路 VOFA

- 修改原因：用户要求 F4 数据和 M0 数据都从蓝牙串口回传。
- 最终决策：保留 G3519 UART4 PB10/PB11 为唯一蓝牙出口；F407 每 20 ms 经现有
  USART1 DMA 链路发送独立 Packet `TEL`，Payload 为主 QD4310 的节点/电机 ID、使能、
  电流 mA、速度 0.01 rpm、单圈角度 mrad、在线。`TEL` 与 500 ms `HB` 使用独立序号；
  队列满整帧丢弃，不阻塞、不重试、不推进序号。G3519 保存最新 `TEL`，在原 M0 14 路
  FireWater 后追加 ID/使能/电流 A/速度 rpm/角度 rad/在线，蓝牙固定输出 20 路；超过
  200 ms 无新 `TEL` 时末 6 路置零。F407 的 QD4310 周期 NOP 不再依赖 ESP8266 Ready，
  因而没有 Wi-Fi 客户端时蓝牙仍可获得电机反馈。
- 架构边界：没有修改 UART/DMA/ISR/引脚，没有新增 Task、ACK、重发、状态机或动态
  内存；ISR/DMA 仍只搬运字节，双端现有 10 ms Task 解析和组帧。`HB` 在线语义不变，
  `TEL` 新鲜度只影响显示值，不触发控制或恢复动作。
- 涉及文件：双端 `Device/PeerLink`，F407 `APP/Gateway`、Task/config/协议/README，
  G3519 `Task/task_bringup_chassis.*`、config、PeerLink 测试/README 和本文档。
- 验证方式和结果：Communication host 测试和 Cortex-M0+ 7 个通信源严格编译通过；
  模板架构与 `git diff HEAD --check` 通过；G3519 Bring-up Debug/Release 完整链接，
  FLASH/SRAM 分别为 `55456/13448 B`、`52144/13440 B`。F407 Debug/Release 完整链接，
  FLASH/RAM 分别为 `18460/12248 B`、`15632/12240 B`。双端 PeerLink 源文件 SHA-256
  保持一致。D 盘 OpenOCD 烧录最新 Debug BIN，并完整校验 F407 `18460` 字节、G3519
  `55456` 字节。同步启动约 62.5 s 后双方心跳均为 `126/126`，F407 `TEL` 发送
  `3142`、G3519 接收 `3142`；双方在线，所有发送丢弃、协议、UART、DMA、Task/
  PeerLink 故障计数均为 0。
- 已知限制：调试快照中的最新 `TEL` 有效且电机 ID 为 1，但 QD4310 在线位为 0；
  20 路蓝牙实收、200 ms 置零边界和 Task WCET/栈高水位仍待硬件验证。
- 待确认事项：恢复 QD4310 CAN 反馈后在 VOFA 核对固定 20 列及末 6 列单位，再断开
  F407 TX 验证 M0 前 14 路继续更新、末 6 路在不超过 220 ms 内置零。

## 2026-07-29：G3519 与 F407 双向 UART DMA 心跳实机闭环

- 修改原因：用户要求两块 MCU 继续收发数据，并双端调试到心跳通信稳定。
- 最终决策：G3519 使用 UART1 PB4 TX/PB5 RX、DMA_CH1/CH0、节点 2；F407 使用
  USART1 PA9 TX/PA10 RX、DMA2 Stream7/Stream2 Channel4、节点 1。双方保持
  115200 8N1、500 ms `HB`、1500 ms 离线超时、Packet CRC16 协议；各自由现有
  10 ms Task 非阻塞推进，不新增 Task、重发或动态内存。
- 涉及文件：`Device/Communication/PeerLink/`、`Task/task_bringup_chassis.c`、
  G3519 UART/DMA BSP 与 SysConfig、`config/core_config.h`、相关 CMake/测试及本文档；
  F407 对端实现和唯一线上协议位于 `C:\Users\rlyoulo\Desktop\F407-Gateway`。
- 构建与烧录：当前 Bring-up Debug ELF 为 text/data/bss `48512/5956/7420`，BIN
  54400 字节；DAPLink 烧录后的全长 readback 与构建 BIN 的 SHA-256 一致。
- 双端实机验证：分别锁定 G3519 DAPLink `0D28:0204` 和 F407 HID CMSIS-DAP
  `0416:5051`，同步复位后连续运行约 46 s。G3519 发送/接收 `92/92`，F407
  `94/92`，双方在线；两端 Task/PeerLink 故障、TX 丢弃、Packet/CRC/节点/重复、
  UART 溢出/帧错误和 DMA 错误全部为 0。
- 已知限制：当前只定义 `HB`，不承载底盘、滚球、视觉或电机业务数据；尚未执行
  故意断线后的 1500 ms 精确边界和长时间压力测试。单端长时间 SWD halt 会使持续运行
  的另一端 UART 产生调试扰动，因此最终验收采用双端同步复位和同时取样。
- 待确认事项：后续需要传输业务命令时先扩展唯一协议契约并定义 Topic/幂等语义；
  现阶段心跳链路无需调整。

## 2026-07-29：G3519 SPI0 改接板载 ST7735S TFT，并新增 Display Device

- 修改原因：用户要求判断 G3519 能否直接接入 1.8 英寸 TFT，并明确不需要 BMI088，
  允许将 SPI0 分配给 TFT；随后授权烧录。
- 最终决策：两套 G3519 SysConfig 均删除 BMI088 的 SPI0/双 CS 分配，改为板载显示
  SPI0 Mode 0、MSB first、10 MHz、仅发送：PB3 SCLK、PB2 MOSI、PC9 CS（低有效）、
  PC8 DC、PB23 RESET（低有效）、PA30 背光（高有效）；SPI1 板载 ICM-45686 保持不变。
  新增 `BSP_Display` 和 TX-only SPI 能力描述，新增 `Device_ST7735S` 的约 420 ms 有界
  初始化、四向旋转、RGB565 区域写入/填充/像素和背光接口。绘图只用 64 像素临时块，
  不新增字体、UI、整屏 framebuffer、DMA、Task 或产品状态机。模块只允许 3.0–3.3 V。
- 涉及文件：G3519 两份 SysConfig、BSP resource map/SPI/Display/init/manifest、
  `Device/Display/ST7735S`、根/Device CMake 与 feature-smoke preset、Display host/ARM
  测试、验证映射和板级/参数说明。
- 验证方式和结果：TB6612、AT8236 两份 SysConfig 生成通过，并确认 SPI0/PB3/PB2及
  四个控制 GPIO 宏；ST7735S 假 BSP 主机测试和 Cortex-M0+ `-Werror` 严格编译通过；
  `tools/project.ps1 -Action Verify -Scope Auto` 通过，覆盖架构/差异检查、相关 host/ARM
  测试及 G3507、G3519 TB6612/AT8236、G3519 feature-smoke Debug/Release 完整链接。
  显式选择 MSPM0G3519 + DAPLink，烧录 G3519 TB6612 Debug 17832 字节并完整回读，
  SHA-256 与 BIN 一致
  （`9C3869841FA8D02DD5A3ED170D62E0A9AD648B32EF9390862F8299AF2B2134C2`），随后
  `reset run`。
- 已知限制：烧录验证只证明映射后的基础固件可写入并运行；当前没有 TFT 调用者，
  BSP 初始化会保持 RESET 有效且背光关闭，因此屏幕不会自动点亮。尚未用实物验证
  10 MHz 波形、颜色、GRAM 偏移、背光极性或面板初始化；未启用显示 DMA。
- 待确认事项：需要点屏时由用户明确授权最小显示测试入口，再调用
  `Device_ST7735S_Init()` 和纯色填充，上板核对红/绿/蓝、四角坐标和旋转方向。

## 2026-07-29：角度锁外环 Kp 从 3.0 提高到 4.0

- 修改原因：用户确认角度纠偏极性正确。对 2010 个 VOFA 样本分析后，锁定目标为
  `-0.87°`，扰动误差约达 ±34°，回正过程没有持续振荡；最大左右电机命令分别为
  5876/6803，未达到 8000 限幅，但多次在约 1.3°～4° 残差处轮速归零并缓慢累积
  电机命令，表明小角度比例输出克服静摩擦偏慢。
- 最终决策：遵循单变量整定，只将绝对角度外环 `Kp` 从 `3.0` 提高到 `4.0`；保持
  `Ki=0.05`、`Kd=1.0`、轮速内环、±160 mm/s 外环轮速限幅和 80% 最终命令钳位不变。
- 涉及文件：`config/core_config.h`、本记录。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过，本次按影响映射
  执行 Navigation/LineTracking 两个组件；另行完整链接 Bring-up Debug/Release，分别
  使用 FLASH 9.62%/9.12%、SRAM_BANK0 18.71%/18.70%。显式选择 MSPM0G3519 +
  DAPLink，烧录 Bring-up Debug 50416 字节；从主 Flash `0x00000000` 全长回读后
  SHA-256 与构建 BIN 一致
  （`79461DBD357C17DA60D931897EF20357EE94A7CA2F35DFCA05424E22AE455687`），随后执行
  `reset run`。
- 已知限制：Kp=4.0 的实物回正时间、残差和过冲尚未复测；六轴 yaw 漂移和机械静摩擦
  仍会影响长时间角度保持。
- 待确认事项：采集新的 ±5°、±10° 扰动数据；若仍无回摆且回正偏软，再评估 Kp=4.5，
  若出现回摆则保持 Kp 并提高 Kd。

## 2026-07-29：Bring-up 从定时轮速阶跃切换为按键保持角度锁

- 修改原因：300 mm/s 双轮速度闭环测试完成后，用户要求测试绝对角度闭环：第一次
  按 KEY 锁定当前角度并保持原地，第二次按 KEY 退出。
- 最终决策：保留同一 10 ms 静态 Bring-up Task、双轮速度内环和 80% 最终命令钳位；
  删除 3 s 定时定速行为。第一次消抖按下且 ICM-45686 姿态/陀螺 bias 有效时，记录
  当前 Mahony yaw；现有角度外环（`Kp/Ki/Kd=3/0.05/1`、轮速目标±160 mm/s）输出
  等大反向左右轮速目标，再由双轮速度环产生电机命令。锁定持续到下一次按下，退出时
  电机清零并重新锁定 Device。当前 IMU 样本无效时不沿用旧 PWM，本周期输出零且保留
  用户选择的锁定状态。未新增 Task、超时、自动退出或硬件配置。
- 遥测语义：FireWater 保持 14 列和 20 ms 周期；CH0 改为锁定 yaw 目标度数（空闲为
  0），CH1/2 仍为左右轮速，CH3/4 为电机命令，CH5 为实测 yaw，CH6..13 为灰度 ADC。
- 涉及文件：`Task/task_bringup_chassis.*`、`Task/README.md`、根 CMake、
  `config/core_config.h`、本记录。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过，共执行九个组件；
  模板架构、差异检查、相关 host 测试、Cortex-M0+ 严格编译以及 Bring-up Debug/Release
  完整链接通过。Debug 使用 FLASH 9.62%、SRAM_BANK0 18.71%；Release 使用 FLASH
  9.12%、SRAM_BANK0 18.70%。已显式选择 MSPM0G3519 + DAPLink，烧录角度锁定版
  Bring-up Debug 50416 字节；从主 Flash `0x00000000` 全长回读后，SHA-256 与构建 BIN
  一致（`44D59625A7FA0BCD1CF2510812D76B117627F025D60ACBC321FE569D11DDF0C2`），随后
  执行 `reset run`。角度纠偏方向仍待实车确认。
- 已知限制：六轴 Mahony 没有绝对 yaw 参考，长时间锁定会跟随陀螺积分漂移；机械打滑
  也不能保证位置绝对不平移。Task 的 WCET、调度抖动和栈高水位仍未上板测量。
- 待确认事项：烧录后等待约 2 s 校准完成再按 KEY，小角度手推验证纠偏符号；若方向
  正确，再采集目标 yaw、实测 yaw、轮速和电机命令，整定角度环刚度与阻尼。

## 2026-07-29：提高底盘轮速环比例增益并将 Bring-up 输出上限扩大到 80%

- 修改原因：首轮 300 mm/s VOFA 阶跃中，左右轮上升偏慢且后段命令持续达到原 55%
  上限；用户要求加大 Kp，并明确将实际 PWM 输出上限扩大到 80%。
- 最终决策：保持轮速环 Ki、Kd、前馈和目标平滑不变；低速段 Kp 从 `0.15` 提高到
  `0.25`，高速段 Kp 按相同比例从 `0.12` 提高到 `0.20`。速度控制器最大 effort
  从 55% 提高到 80%，Bring-up 最终电机命令钳位同步从 6000 提高到 8000，避免两层
  限幅不一致。当前 300 mm/s 测试因平滑目标严格不大于 300 mm/s，实际使用低速段参数。
- 涉及文件：`config/core_config.h`、本记录。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过；模板架构、
  `git diff HEAD --check`、相关 host 测试、Cortex-M0+ 严格编译，以及 bring-up
  Debug/Release 完整链接均通过。Debug 使用 FLASH 9.28%、SRAM_BANK0 18.66%；
  Release 使用 FLASH 8.77%、SRAM_BANK0 18.65%。已显式选择 DAPLink 烧录 Bring-up
  Debug 48680 字节，完整回读主 Flash 后 SHA-256 与构建 BIN 一致，并 `reset run`。
  用户回传的 3 s 阶跃中，末帧左右轮为 289/287 mm/s，峰值命令 5824/6131，无超调、
  振荡或 80% 饱和；末段仍缓慢上升，说明速度环稳定但现有前馈偏小。
- 已知限制：80% PWM 不是电流限流；MG513X 12 V 堵转电流约 3.2 A，高于 TB6612
  单桥 1.2 A 连续额定值，堵转或重载会显著增加驱动器风险。MG513X 输出轴 CPR 仍需
  实测确认。
- 待确认事项：MG513X 输出轴 CPR 和有效滚动轮周仍需实测；后续按多个速度工作点重新
  标定前馈，减少积分建立时间。

## 2026-07-29：bring-up 接入模板轮速闭环；灰度无数据定位为物理链路

- 修改原因：用户要求测试速度闭环；同时报告 VOFA 灰度通道恒 0。
- 最终决策：bring-up 运行段从固定 PWM 改为模板 `Mode_Car_SpeedController`
  双频段 PID + 前馈 + 目标平滑闭环，目标
  `TEMPLATE_BRINGUP_TARGET_SPEED_MM_PER_SECOND = 300 mm/s`（两轮同目标，参数
  沿用 `TEMPLATE_CAR_SPEED_*` 既有标定），effort 百分比 ×100 转电机命令后仍经
  ±6000 限幅；`TEMPLATE_BRINGUP_MOTOR_COMMAND` 已删除。遥测 CH0 改发平滑后
  目标速度；新增故障码 9=速度控制器。CMake bring-up 档自动启用
  `MODE_CAR`（连带 `ALGORITHM_PID`）。
- 灰度诊断：DAPLink 在线读 `g_grayscale` 统计，accepted/rejected/checksum/
  format/discarded 全为 0，故障码 0——UART3 RX 从未收到任何字节，排除波特率与
  协议原因，判定为物理链路问题（T8 供电、TX/RX 未交叉或未共地）。文档记录
  T8 在既往测试工程 115200 下工作过。
- 涉及文件：`Task/task_bringup_chassis.*`、`Task/CMakeLists.txt`、根 CMake、
  `config/core_config.h`、本记录。
- 验证方式和结果：`g3519-bringup-debug` 构建通过（FLASH 9.28%、SRAM_BANK0
  18.66%），DAPLink 烧录 48680 字节成功并复位运行；闭环阶跃响应待实车 VOFA
  确认。
- 已知限制：假设电机 i 与编码器 i 同侧配对；若实际交叉，两轮闭环会互相耦合振荡，
  现象出现时需交换电机或编码器索引。首次闭环测试需悬空驱动轮。
- 待确认事项：300 mm/s 阶跃的超调与稳态误差；灰度物理链路排查结果；右轮编码器
  取反后 CH1/CH2 是否同为正。

## 2026-07-29：bring-up 升级为 VOFA+ 回传并接入 IMU/串口灰度，撤销受控电源误用

- 修改原因：用户要求回传改 VOFA+ 分通道并增加目标速度、实际速度、PWM、yaw、
  8 路灰度；要求 PWM 输出限幅保护 TB6612；并纠正 12 V/5 V 受控电源与 TB6612
  无关（电机供电走电池，不经受控轨），撤销上一版在 bring-up 中打开受控电源的
  行为。
- 最终决策：`task_bringup_chassis` 重写为 VOFA+ 回传，经蓝牙 UART4 每 20 ms 一帧，
  14 个数值：`0` 目标速度占位（速度环未实现，恒 0）、`1/2` 左右轮速 mm/s
  （`Device_Encoder`，500 线×4×28:1，名义轮周 204.20 mm）、`3/4` 电机命令、
  `5` yaw 度（板载 ICM-45686 SPI1 + Mahony，上电静止校零偏 200 样本）、
  `6..13` 芯流 T8 八路 ADC（UART3 PC6/PC7 115200，每 20 ms 发 0x19 查询）。
  首版 JustFloat 二进制帧在用户端出现乱码/解析错误，用户要求改用模板组件：现按
  模板约定接入 `Mode_Communicate_BluetoothPC`（策略：原始字节流，上层格式由产品
  Task 决定），帧格式改为 VOFA+ FireWater 纯 ASCII 行（逗号分隔、`\n` 结尾、
  整数化 snprintf，yaw 定点两位小数），任何串口助手可直接读，便于区分波特率
  问题与协议问题。所有电机命令统一经 `bringup_command_motor()` 限幅
  `TEMPLATE_BRINGUP_MOTOR_COMMAND_LIMIT=6000`（60%）。不再初始化 PowerSwitch
  Device；故障码 6 改为 IMU、8 为灰度串口。按键固定 PWM 3 s 行为不变。
- 涉及文件：`Task/task_bringup_chassis.*`、`Task/CMakeLists.txt`、根 CMake、
  `config/core_config.h`、本记录。
- 验证方式和结果：FireWater 版 `g3519-bringup-debug` 构建通过（FLASH 8.70%、
  SRAM_BANK0 18.53%），DAPLink 烧录 45632 字节成功并 `reset run`；实收波形
  待用户 VOFA 确认。
- 已知限制：IMU 初始化失败会红灯停机（探测不到 ICM-45686 时无降级路径）；芯流
  T8 具体波特率按 115200 假设，需实物确认；yaw 为 Mahony 融合值，长时间静止后
  仍会缓慢漂移。
- 待确认事项：DAPLink 重新接入后完成烧录；VOFA 实收帧率；灰度模块响应与波特率；
  电机转向/编码器符号（沿用上一条待确认）。
- 2026-07-29 实测更新：右轮（B 路，PB6/PB7）编码器前进计数为负，已在
  `Device_Encoder` 配置将 `TEMPLATE_BRINGUP_ENCODER_B_DIRECTION` 定为 `-1`
  （A 路保持 `+1`），FireWater 版重新编译烧录（45640 字节）。电机转向未报告
  问题，视为正确。

## 2026-07-29：H 题底盘第一步 bring-up Task 完成编译并烧录

- 修改原因：H 题起步测试需要最小闭环——按键触发固定 PWM、GMR 原始计数蓝牙回传、
  RGB 状态指示，用于核对电机/编码器方向并采集首批标定数据（16.1 节步骤 1）。
- 最终决策：新增可选 `Task/task_bringup_chassis.c(.h)`，由
  `TEMPLATE_ENABLE_TASK_BRINGUP_CHASSIS` 开关和 `g3519-bringup-debug/release`
  preset 编译，自动带入 Key/Motor/RGB/PowerSwitch/Encoder/Serial Device。行为：
  上电全部 Device 初始化并打开 12 V/5 V 受控电源（已被 2026-07-29 后续记录撤销，
  受控电源与 TB6612 供电无关），绿色=待机，按 PB31 按键后两电机
  以 +2000（20%）开环运行 3 s（蓝色），每 100 ms 经蓝牙 UART4（PB10 TX/PB11 RX,
  115200）发送 `B,ms,run,cmd,cntA,cntB` 帧，结束发送 `END,dA,dB` 并回待机；任何
  Device 失败红灯并写 `g_BRINGUP_Fault`（1 RGB/2 按键/3 电机/4 编码器/5 串口/
  6 电源/7 任务）。电机型号确认为 MG513X（500 线 GMR，28:1），TB6612 驱动。
  参数集中在 `core_config.h` 的 `TEMPLATE_BRINGUP_` 前缀分区，验证清单 Firmware
  组件已覆盖该前缀和两个 bring-up preset。
- 涉及文件：`Task/task_bringup_chassis.*`、`Task/CMakeLists.txt`、根 CMake/Presets、
  `main.c`、`config/core_config.h`、`config/verification/components.json`、本记录。
- 验证方式和结果：`g3519-bringup-debug` 完整构建通过（FLASH 5.92%、SRAM_BANK0
  17.74%）；OpenOCD DAPLink 烧录 31024 字节成功并 `reset run`。
- 已知限制：实物行为（电机转向、编码器计数极性、蓝牙输出、按键、RGB）尚未回读
  确认；开环 20% 占空比要求首次测试将驱动轮悬空。
- 待确认事项：左右电机正方向与编码器符号对应关系；轮径与轮距实测；蓝牙模块波特
  率是否为 115200（模块出厂常见 9600，需要用 AT 命令核对或改配置）。

## 2026-07-28：区分芯流 T8 模拟循迹与感为数字循迹 Device

- 修改原因：用户纠正第二种循迹不是 UART，而是感为 DATA+CLK 同步串行数字量模块，
  并确认 Mode 必须保留数字循迹和模拟循迹两个独立入口。
- 最终决策：新增具体 `Device_Ganwei`，只绑定恰好八路数字通道的现有
  `BSP_LineSensor` 后端，并始终调用 `Device_LineTracking_Update()` 的逐通道阈值、
  位图和交叉口判定路径；后续对应 `Mode_Car_DigitalTracking`。芯流 T8 继续由
  `Device_XinliuT8` 解析 UART ADC，并调用 `Device_LineTracking_UpdateAnalogRaw()`
  的共同基线消除和模拟置信度路径，后续对应 `Mode_Car_AnalogTracking`。两者只复用
  通用位置估计和 PID 核心。新增独立 CMake 开关，启用感为时自动带入通用循迹 Device；
  G3519 feature-smoke Debug/Release 同时编译两个具体 Device。PB8 DAT、PB9 CLK、八个
  时钟的 BSP 读取时序、SysConfig、Task 和现有 T8 行为均未修改。
- 涉及文件：`Device/Sensor/LineTracking/Ganwei`、根/Device CMake 与 Presets、循迹
  host/ARM 测试、BSP/Device/根说明、设备参数、验证组件描述和本记录。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过；模板架构检查、
  `git diff HEAD --check`、循迹 host 测试和 7 个循迹源文件 Cortex-M0+ 严格编译通过；
  G3519 feature-smoke Debug/Release 完整构建通过并生成 `device_ganwei` 静态库。由于根
  构建输入变化，增量验证实际执行九个组件，均通过。
- 已知限制：本次未新增产品循迹 Task、未烧录，也未用逻辑分析仪验证感为 DAT/CLK
  波形、位序和极性；编译和 host 测试不能替代实物验证。
- 待确认事项：感为具体型号、供电与 DATA 电平、黑线有效极性、机械安装、模块数量
  和产品调用周期。

## 2026-07-28：修复增量验证并补齐可选组件覆盖

- 修改原因：模板需要为 AI 辅助和日常开发提供可信且省时的验证入口；原增量缓存会因
  任一核心参数或 Markdown 改动扩大编译范围，验证清单中的错误引用也可能导致漏检，
  Windows PowerShell 5.1 还存在模块编码和子脚本参数传递问题；提交前审计又发现 AI
  上下文、构建复用证据、可选源码严格编译和 clean 基线判断存在遗漏。
- 最终决策：验证清单升级为 schema 2，`core_config.h` 保持单文件，并按
  `TEMPLATE_CAR_`、`TEMPLATE_GIMBAL_`、`TEMPLATE_IMU_FUSION_` 等宏前缀只使所属
  组件缓存失效；变更中的 Markdown 会进入 AI 优先文件清单，但不参与编译缓存哈希。
  架构检查会校验清单输入、依赖、host/ARM scope、构建 preset 和核心参数前缀覆盖。
  统一工具使用 UTF-8 BOM 兼容 Windows PowerShell 5.1，以命名参数向子脚本传递
  `Scope`，保留复用 preset 名称，并要求所有构建组件同时具有 fresh、clean 和完整
  preset 证据后才宣布基线就绪。日常入口固定为
  `tools/project.ps1 -Action Verify -Scope Auto`；首次建立缓存或准备基线才运行
  `-Action Verify -Scope All -Clean`。
- 构建覆盖：新增 G3519 `feature-smoke` Debug/Release preset，同时打开当前所有可选
  Algorithm、Device 和 Mode。可选静态库没有产品 Task 引用，因此只将结果表述为组件
  ARM 构建通过，不表述为产品固件集成完成。
- 涉及文件：`AGENTS.md`、根/Mode/config 说明、`CMakePresets.json`、
  `config/verification/components.json`、ARM 编译与模板检查脚本、统一项目工具及本记录；
  未增加 Task、状态机、硬件配置或运行期行为。
- 验证方式和结果：全部 host GCC 测试通过；ARM `All` 共 62 个源文件通过，并验证
  Watchdog、Communication、IMU、Motor、Navigation、LineTracking、Gimbal、Optional
  独立 scope；
  G3519 FeatureSmoke Debug/Release 均完成 142 步干净构建。九个增量组件全量验证通过后，
  `Verify Auto` 只执行全局架构与 `git diff HEAD --check`，九个组件全部命中缓存；模板检查
  和最终差异空白检查通过。
- 已知限制：本次未烧录、未上板，也未验证传感器、电机、通信或控制参数的实物行为；
  FeatureSmoke 不证明可选组件已被某个产品 Task 调用。
- 待确认事项：无；后续新增核心参数、组件或 preset 时必须同步验证清单。

## 2026-07-28：核心整定参数集中到单一分类配置

- 修改原因：用户要求把 PID、Kalman、滤波以及常调的 Task 周期/优先级/栈、阈值、
  限幅和超时汇总到 `config/`，配置必须有注释和分类，同时明确不要按算法或模块拆得
  过细；同一要求还需固化到嵌入式工作流 skill。
- 最终决策：新增单一 `config/core_config.h`，在文件内按 Task、车辆控制、循迹、云台、
  Mahony、双 IMU 融合/Kalman 六个大类分区。现有默认配置函数直接消费这些宏，所有
  既有数值和启用状态保持不变；特别保留导航 slip gate 默认关闭。通用一维 Kalman 和
  Filter API 没有业务默认值，继续由调用方显式传参，不创建未使用的空参数表。固定
  公式、公共 API 结构容量、协议常量、寄存器值、引脚/时钟和构建开关继续留在原 owner。
- Skill 规则：`embedded-firmware-workflow` 新增“核心参数与配置”约束，要求少量配置
  文件、按 owner/领域分区、注释用途/单位/约束/影响、默认函数消费单一真值，并禁止
  未使用占位参数和破坏硬件/FreeRTOS/Device/CMake 的事实边界。
- 涉及文件：`config/core_config.h` 与说明/验证映射，Task、Car/Gimbal Mode、
  LineTracking/Mahony/IMUFusion Algorithm 的默认配置入口和私有 include 路径，以及
  对应 README、测试脚本和本记录；未新增产品 Task、状态机或保护逻辑。
- 验证方式和结果：全部 host GCC 测试、Cortex-M0+ 严格编译、模板架构检查和
  `git diff --check` 通过；启用 Car、Gimbal、LineTracking、BMI088 和 ICM45686 的
  G3519 Debug/Release 均完成 SysConfig 生成、ARM 编译和完整链接。Skill 经
  skill-creator `quick_validate.py` 校验通过。
- 已知限制：本次只验证 host、ARM 编译和完整链接，未烧录或执行实物参数整定；
  配置迁移不代表这些通用默认值已经适配任一具体机械系统。

## 2026-07-28：从通用模板删除循迹产品 Task

- 修改原因：用户确认通用 Template 不应自带 `LineTrackingReadout` 和
  `AnalogTracking` 产品任务，明确要求直接删除。
- 最终决策：删除 `Task/LineTracking` 及专用接入文档，移除两个 Product Task CMake
  选项、四组循迹 configure/build preset 和 `mspm0.ps1 -TaskProfile` 入口。模板只保留
  10 ms 静态 `Task_Control` 骨架及其全局看门狗唯一喂狗职责；同步删除仅服务于产品
  Task 的输出许可、锁定和 Mode 故障调试全局量。通用控制 Task 的名称、周期、栈深度
  和优先级偏移集中到 `config/task_config.h`。可复用的 LineTracking Algorithm、Device、
  Car Tracking Mode、参数事实和 host 测试继续保留，由具体产品自行编排。
- 涉及文件：`Task`、根 CMake/presets、`tools/mspm0.ps1`、验证组件清单、架构检查配置
  白名单、根/Task/Watchdog/XinliuT8/config 文档；历史记录不改写。
- 验证方式和结果：全部 host 测试、Cortex-M0+ 严格编译、模板架构检查和
  `git diff --check` 通过；剩余 6 个 G3507/G3519 TB6612/AT8236 Debug/Release preset
  均以 `--fresh` 配置，完成 TI SysConfig 生成、ARM 编译和完整链接。
- 已知限制：本次未烧录或执行实物验证；删除后模板不再提供 T8 查询、循迹遥测或电机
  循迹产品行为。
- 待确认事项：后续产品如需循迹，应在具体产品工程新增 Task 编排，不再把产品 Task
  回填通用模板。

## 2026-07-27：加入全局 WWDT1 健康监督链路

- 修改原因：用户要求模板提供全局硬件看门狗，并确认 APP 负责全局健康策略，后续
  合作者必须能明确看到唯一喂狗者和硬件分层约束。
- 最终决策：G3507/G3519 三份 SysConfig 均启用应用级 WWDT1，LFCLK 32.768 kHz、
  除以 8、`2^10` 计数、250 ms、0% closed window、睡眠继续计数；BSP 初始化把
  Core Halt 行为设为 STOP。`APP/System/Watchdog` 只做 required/completed 位掩码
  判定，`Task/task_app.c` 是唯一喂狗决策者，调用经 `Device/System/Watchdog` 到 BSP。
  控制周期完整返回即代表执行仍推进，Mode 安全故障不会造成无意义复位循环。
- 架构约束：APP 不依赖 MCU/Device/RTOS，Task 不绕过 Device；架构检查新增唯一
  喂狗者规则，禁止其他 Task、Mode、ISR 或 Device 旁路喂狗。完整扩展说明写入
  `docs/WATCHDOG.md`，根目录及 APP/BSP/Device/Task README 均建立入口。
- 涉及文件：`APP/System/Watchdog`、`Device/System/Watchdog`、两套 BSP 的
  SysConfig/resource map/manifest/init/watchdog backend、`Task/task_app.c`、根 CMake、
  DriverLib manifest、host/ARM 测试、架构检查和文档。
- 验证方式和结果：APP Watchdog host 单元测试、全部 host 回归、Cortex-M0+ 严格
  编译和模板架构检查通过。10 个正式 preset 均以 `--fresh` 重新生成 SysConfig 并
  完整链接通过；生成代码确认 WWDT1 为 250 ms、Window0/1 关闭、睡眠继续计数。
  AnalogTracking Debug/Release 的 text/data/bss 为 `51488/5412/7212`、
  `49168/5412/7212`。
- 已知限制：尚未上板故意停止喂狗测量 250 ms 复位和复位原因；WWDT 在超时前不能
  立即关闭电机，也不能发现仍在运行的控制逻辑错误。TB6612 STBY 复位期间的默认
  安全状态仍需确认硬件下拉，致命异常立即拉低 STBY 需要独立保护路径。
- 待确认事项：实物测试初始化到首次喂狗小于 250 ms、调试暂停不复位、停止喂狗后
  WWDT1 产生 SYSRST，以及复位期间 TB6612 输出确实失能。

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

## 2026-07-27：同步蓝牙透传策略与 LoRa 半双工通信组件

- 修改原因：用户要求把当前 `test` 中已验证的 LoRa 和蓝牙无线透传能力同步回中央模板，
  并确认 LoRa 不应绑定某一颗 MCU。
- 最终决策：模板既有 `Device_Serial` 与来源工程逐字一致，继续作为普通蓝牙无线透传的
  非阻塞 UART Device，不新增无协议意义的 Bluetooth Device。Bluetooth PC/双 MCU Mode
  输出新增 `duplex_policy=UNRESTRICTED`，只表示软件不互斥 TX/RX，不宣称无线 PHY 已
  验证物理全双工。新增 `Device_LoRa`，按实测格式原子发送
  `[目标地址高][目标地址低][目标信道]+payload`，并透明接收字节流；新增纯
  `Mode_Communicate_LoRaHalfDuplex` 管理互补轮次。LoRa 选项自动带入 Device_Serial。
  没有同步双板测试 Task、产品消息协议、默认 preset、ACK、超时重发或自动恢复。
- 硬件与身份边界：已登记模块地址 `0x7302/0x1007`、信道 `0x4A/0x30`、115200 8N1、
  空口参数和定点路由头。模块地址属于 LoRa 模块内部配置，不属于 MCU、开发板或
  DAPLink；换板只需让产品 `local_side` 与所接模块地址一致，并提供满足能力的 UART
  transport。G3519 PB10/PB11 是已实测映射，不是通用 Device 的固定依赖。
- 涉及文件：`Device/Communication/LoRa`、既有 Serial 说明、`Mode/Communicate`、根/
  Device CMake 与 Presets、对应 host/ARM 测试、设备参数和本记录。模板既有 Watchdog、
  BSP/SysConfig、Task 周期、产品状态机和默认启动行为未修改。
- 验证方式和结果：LoRa Device、Bluetooth/LoRa Communicate Mode 及模板全部 host 回归
  通过；Cortex-M0+ `-Wall -Wextra -Wpedantic -Werror` 严格编译和架构检查通过。显式
  打开 LoRa+Communicate 的 G3519 Debug/Release 完整构建成功，确认 Serial、LoRa 和
  三个通信 Mode 源进入编译链，Flash/SRAM 分别为 `17736/9256 B` 和
  `17264/9248 B`。原 AnalogTracking Debug/Release 回归构建通过，证明既有蓝牙调用
  兼容，Flash/SRAM 分别为 `56816/12616 B` 和 `54512/12616 B`。LoRa 核心源码和
  host 测试与来源工程规范化内容一致；来源工程此前已完成双板空口连续序号/CRC 实测，
  本次模板未重复烧录。
- 已知限制：`Device_LoRa_Send()` 的 `OK` 只代表 UART 接受完整数据；Mode 没有 ACK、
  重发、超时或失步恢复，任一轮次消息丢失会停在等待。模块完整型号、最大 RF 单包、
  VCC 和 AUX/空口完成语义仍未知。
- 待确认事项：具体产品接入时定义消息边界、最大 payload、两端初始轮次和所需可靠性；
  模块换接板卡后按实际模块地址更新 `local_side`，不要按 MCU 或探针串号猜测。

## 2026-07-29：部署双 MCU `ENC`/21 路 VOFA 联调固件

- 修改原因：用户要求烧录已经完成构建验证的双端 Debug 固件，使 F407 的 QD4310
  绝对编码器原始值能经板间串口进入 G3519，并追加到蓝牙 VOFA CH20。
- 最终决策：F407 继续使用独立 `ENC=<node_id>,<motor_id>,<angle_raw>,<online>`；
  G3519 在不改变 CH0～CH19 的前提下固定输出 21 列，新鲜且在线时 CH20 输出
  `angle_raw`，超过 200 ms 或离线时输出 0。G3519 停车键边沿发送的 `DIS` 仍只是
  发送端实现；F407 尚未消费该命令，不能视为滚球电机已随小车停车键 Disable。
- 烧录与回读：显式选择 VID:PID `0416:5051` 的 F407 HID CMSIS-DAP，以及序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908` 的 G3519 DAPLink。分别以 BIN
  写入 F407 Debug `20900 B` 和 G3519 Bring-up Debug `56440 B`，按精确长度完整回读；
  SHA-256 分别与源固件一致：`8D7DC1F20A563476A97AEE2FF96382D6BA1F0DA0AD31DAB23C7D3B77DEB65BDA`
  和 `EA097067C7D7ADB1F8137F2191B44E6AE50ACAA45B1E86BDC597339574DD8D36`。
  两端随后均已复位运行。
- 验证边界：本轮证明的是目标识别、Flash 写入、完整回读和复位运行；VOFA+ 进程与
  COM5 仍存在，但没有重新独占串口采集，因此不能宣称当前 21 列帧率、CH20 动态值、
  跨零行为或 200 ms 置零已经实物验证。
- 待确认事项：恢复 QD4310 在线反馈后在 VOFA 核对固定 21 列和 CH20；断开 F407 TX
  验证置零边界。由滚球 Agent 消费 `DIS` 后，再验证小车停车键同时锁定底盘和 Disable
  QD4310。

## 2026-07-29：G3519 Bring-up 启用 ST7735S 红屏自检

- 修改原因：用户纠正 TFT 应由 MSPM0G3519 驱动，不是 F407；现有 G3519 显示 BSP、
  SPI0 和 ST7735S Device 虽已具备，但 bring-up profile 与 Task 没有启用或调用它，
  所以屏幕保持黑色。
- 最终决策：F407 TFT 接入方向已撤回。`g3519-bringup-debug/release` 自动启用并链接
  `device_st7735s`；`Task_BringupChassis` 在调度器启动后执行一次约 420 ms 面板初始化
  和约 40 ms 全屏红色填充。该 Task 优先级为 `tskIDLE_PRIORITY+1`，初始化期间
  `tskIDLE_PRIORITY+2` 的 10 ms `Task_Control` 可继续抢占并喂 250 ms WWDT1，避免把
  初始化放在调度器启动前造成持续复位。显示错误单独写
  `g_BRINGUP_DisplayFault=1..4`，不改变底盘主故障，也不停止蓝牙或 PeerLink；不新增
  Task、重试、状态机或周期界面刷新。
- 硬件映射：沿用当前 G3519 SysConfig 真值，SPI0 Mode 0/MSB first/10 MHz，SCL PB3、
  MOSI PB2、CS PC9、DC PC8、低有效 RESET PB23、高有效背光 PA30；面板 VCC 为 3.3 V。
  SPI1/PB12 CS 继续专用于 ICM-45686，BMI088 未启用。
- 涉及文件：根/Task CMake、`Task/task_bringup_chassis.*`、Task README、验证组件清单
  和本记录；未修改 SysConfig 或 BSP 引脚。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过，包含 ST7735S
  host 测试、ARM 严格编译、模板架构检查、`git diff HEAD --check` 以及 G3519
  Bring-up Debug/Release 完整链接。Debug/Release Flash 分别为 `58344/54544 B`，
  SRAM_BANK0 分别为 `13552/13544 B`；两个链接均确认编译并链接
  `device_st7735s.c` 与 `task_bringup_chassis.c`。
- 烧录与回读：显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908` 的 G3519 DAPLink，写入
  Bring-up Debug `58344 B` 并按主 Flash 完整回读；源 BIN 与回读 SHA-256 均为
  `D6A4BDF3EBF0B69986398AF3F70DE119A394D01D050B49349268F2CBFFE2735D`。目标随后
  已执行 `reset run`；F407 探针未被选择。
- 已知限制：写入与回读一致不能替代屏幕实物观察；约 460 ms 启动段的实际时长、
  任务抖动和栈高水位尚未上板测量。当前显示只做一次红屏，不显示运行数据。
- 待确认事项：观察红屏、RGB、蓝牙和双 MCU 通信是否同时正常；若
  `g_BRINGUP_DisplayFault` 非零，按对应阶段检查 SPI/控制线和 3.3 V 供电。

## 2026-07-29：G3519 TFT 增加低开销运行状态页

- 修改原因：用户确认红屏自检成功，并要求 TFT 显示 QD4310、双 MCU 通信和发车
  状态，同时明确保持低开销。
- 最终决策：状态页归 `Task_BringupChassis` 产品编排所有，不给通用 ST7735S Device
  增加字体或产品字段。页面固定 10 行：标题、`CAR` 发车/停车、PeerLink 在线和本地
  故障、QD4310 在线、ID/使能、电流 A、转速 rpm、单圈角度 rad、绝对编码器原始值、
  心跳接收数和底盘主故障。Task 私有 5×7 大写字模按 8×16 字符格渲染，使用一个
  4 KiB 静态行缓冲，无全屏 framebuffer、动态内存或新增 Task。
- 刷新策略：现有低优先级 Bring-up Task 每 20 ms 最多检查一行，约 200 ms 扫描全页；
  每行缓存上次文字和颜色，内容不变时不发 SPI。显示调度独立于底盘 Device 是否全部
  初始化成功，所以主故障时仍能显示状态；单行写失败记录独立显示故障码 5，不停止
  控制、蓝牙或 PeerLink。
- 涉及文件：`Task/task_bringup_chassis.*`、`Task/README.md` 和本记录；未修改 Device
  公共 API、BSP、SysConfig、Task 周期或优先级。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过模板架构检查、
  `git diff HEAD --check` 和 Firmware 组件 Debug/Release 完整链接；最终针对性重链也
  通过。Bring-up Debug/Release Flash 分别为 `60416/56272 B`，SRAM_BANK0 分别为
  `17848/17840 B`（约 27.2%）。
- 烧录与回读：显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908` 的 G3519 DAPLink，写入
  状态页 Debug `60416 B` 并按主 Flash 全长回读；源 BIN 与回读 SHA-256 均为
  `5B7108B8564A53E981C5D3BC653CC6C5646F6A28C1DE9DDC26A50B2F0AA8BF9B`，随后
  已执行 `reset run`。F407 探针未被选择。
- 已知限制：单行 SPI 写入的实测 WCET、Bring-up Task 抖动和栈高水位仍待硬件测量。
  字体仅覆盖当前页面需要的 ASCII 大写、数字和标点。
- 待确认事项：观察约 200 ms 页面刷新、数值可读性、PeerLink/QD4310 断开变红以及
  发车/停车颜色切换，同时确认蓝牙和控制环不受影响。

## 2026-07-30：H 题车端惯导、灰度辅助与线性速度规划

- 修改原因：用户要求小车以里程计预测制动点，发车和停车均采用线性速度变化，并让惯导作为主链、灰度作为横向辅助。
- 最终决策：新增 `Mode_Car_LinearSpeedProfile`，以左右轮累计距离平均值计算制动包络、目标剩余距离和前瞻预告；起步按 `+a_up` 线性加速，目标段按 `-a_down` 线性减速，只有稳定零速后才执行正常停止和 `DIS`。Bring-up Task 复用 `Mode_Car_FusedNavigation` 完成编码器增量与 ICM-45686 偏置校正陀螺的局部 `x/y/yaw` 惯导；Xinliu T8 仅更新横向 PID，短时串口空窗使用 INS 直线修正，超过辅助窗口则请求同一条规划停车轨迹。关闭速度环原有指数目标平滑，避免二次改变加速度。
- 涉及文件：`Mode/Car/include/mode_car_control.h`、`Mode/Car/Control/mode_car_speed_profile.c`、`Mode/Car/CMakeLists.txt`、`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、`config/core_config.h`、`config/verification/components.json`、速度规划测试脚本和 `tests/speed_profile`。
- 验证方式和结果：速度规划 host 测试、导航 host 测试、Cortex-M0+ 严格编译通过；`tools/project.ps1 -Action Verify -Scope Auto` 通过，最后的稳定零速补丁又重新完成了 G3519 Bring-up Debug/Release 完整链接。
- 已知限制：尚未烧录或实车验证；`guard_distance_mm`、CPR/有效轮周、加减速度、灰度辅助窗口和安装轴向仍需依据实车日志标定；本次未修改 SysConfig、引脚、通信协议或滚球端代码。
- 待确认事项：先在空载直线、带滚球直线和评分点前瞻停车三组日志中标定参数，再接入后续运球赛段的 `SetGoal` 里程目标。
## 2026-07-30 follow-up: 2026 H track route binding

- Confirmed this is the 2026 H track. Added H geometry constants (AB/CD 1500 mm,
  semicircles 1570.8 mm, lap 6141.6 mm), route-goal binding, straight/arc speed
  caps, and a slewed cruise-cap API. Added startup grayscale grace and preserved
  INS fallback through short invalid-frame windows. No SysConfig, BSP, protocol,
  or ball-side code changed; `vofa+.csv` user edits remain untouched.

## 2026-07-30 follow-up: geometry-aware INS lookahead

- The short grayscale-gap fallback now generates its lookahead point from the
  2026 H route geometry (A→B straight, B→C semicircle, C→D straight, D→A
  semicircle). This keeps the INS fallback on the circular path rather than
  forcing a straight chord through a corner. A configured route goal clamps the
  lookahead at the scoring point so the braking envelope and path preview end at
  the same distance.
- Host navigation/speed-profile tests, strict Cortex-M0+ compilation, and the
  G3519 Bring-up Debug/Release full links pass after this change. Hardware
  calibration and physical verification remain pending.

## 2026-07-30 follow-up: profile ownership and acceleration feedforward

- Fixed the valid-grayscale control composition so the linear speed profile
  remains the sole longitudinal reference. INS now contributes only the wheel
  differential required by the 2026 H route, and T8 contributes only its line
  differential; measured mean speed no longer replaces the planned reference.
- `reference_acceleration_mm_per_second2` is now the actual reference-speed
  delta divided by `dt`, including a partial final ramp step. This prevents a
  future `ACC` packet from reporting zero on the sample that reaches a cap.
- Speed-profile and fused-navigation host tests, strict Cortex-M0+ compilation,
  template checks, and G3519 Bring-up Debug/Release full links pass. No
  SysConfig, BSP, PeerLink packet, ball-side code, or Task timing was changed.
  Hardware calibration, flashing, and physical 2026 H verification remain
  pending.

## 2026-07-30 follow-up: clockwise route and bounded preview speed

- Corrected the 2026 H route orientation from the rejected counter-clockwise
  interpretation to the specified clockwise A→B→C→D→A path. With vehicle-left
  defined as `+Y`, both semicircles and the C→D straight now lie at negative
  `Y`; repeated-lap route lookup wraps by lap distance.
- Fixed the distance-goal envelope so it is always limited by the active
  straight/curve cruise cap. A configured lap goal can no longer make the
  profile accelerate above 420 mm/s on straights or 320 mm/s on semicircles,
  and a stop request cannot raise the current reference speed.
- Added a constant-deceleration preview on both straights. With the current
  420/320 mm/s and 220 mm/s² defaults, deceleration starts about 168 mm before
  B and D so curve speed is reached at the tangent point.
- The earlier 380/300 mm/s candidate was rejected after the completed
  start/curve/stop simulation took about 20.08 s. The 420/320 mm/s candidate
  takes about 18.9 s in the same ideal odometry model; this is not hardware
  evidence and retains the same 180/220 mm/s² acceleration limits.
- The latest fresh T8 correction is now recomposed every 10 ms around the
  current profile reference and INS differential. A held grayscale target can
  no longer freeze the longitudinal ramp between 20 ms sensor frames.
- Host tests, Navigation/SpeedProfile strict Cortex-M0+ compilation, template
  architecture checks, and G3519 Bring-up Debug/Release full links pass.
  Debug/Release Flash are `76624/73200 B`; SRAM_BANK0 is `19056/19048 B`.
  No SysConfig, BSP, PeerLink packet, Task timing, flashing, or hardware test
  was changed or performed.

## 2026-07-30 follow-up: compact car-debug VOFA telemetry

- Replaced the bring-up Bluetooth FireWater line with 12 numeric channels for
  car tuning: route distance, measured/target wheel speeds, reference speed and
  acceleration, remaining/braking distances, yaw, line error, and profile state.
  The 20 ms period, buffered non-blocking UART path, and board-to-board
  `HB/TEL/ENC/DIS` protocol are unchanged; F407/QD4310 values remain available
  on the peer link and TFT rather than occupying this waveform.
- Updated the Task header, Task README, and core telemetry buffer contract to
  match the current format. Existing `vofa+.csv` user data was preserved.
- Host/ARM checks and G3519 Bring-up Debug/Release full links pass. Flashing is
  explicitly requested for this turn and is pending the final G3519 DAPLink
  Doctor/build/read-back sequence; hardware waveform reception remains to be
  checked after reset.

## 2026-07-30 follow-up: VOFA compact telemetry flashed to G3519

- The user explicitly requested flashing the current car-debug firmware. The
  selected target was MSPM0G3519 Bring-up Debug, using the configured DAPLink
  probe and the existing custom OpenOCD path; the ordinary non-bring-up
  `g3519-debug` image was not used.
- DAPLink Doctor passed, OpenOCD identified the MSPM0G3519 Cortex-M0+, and
  76624 bytes were programmed. A full main-Flash read-back of the same length
  produced SHA-256
  `C651E5B0F3316A72D4C31A568135EF84A243C61BFCB3F1C879F72367E9E7974E`, matching
  the source BIN; the target was then reset and run.
- The result proves probe/target access, programming, and read-back integrity.
  It does not yet prove COM/VOFA reception, 12-column ordering, encoder
  polarity, or closed-loop 26H behavior; those remain hardware observations.

## 2026-07-30 follow-up: post-flash 26H route audit

- The clockwise route correction, bounded goal/cruise envelope, 420/320 mm/s
  candidate, B/D preview braking, and 10 ms grayscale recomposition were made
  after the compact-VOFA image above was flashed. The current source therefore
  must not be described as deployed even though its Debug image is also 76624 B;
  binary size equality is not image identity.
- Current source retains the approved 12-column Bluetooth car-debug frame and
  leaves `HB/TEL/ENC/DIS` unchanged. Host tests, full strict ARM compilation,
  and current G3519 Bring-up Debug/Release links pass; no additional flashing
  or 26H hardware run was performed.

## 2026-07-30：维特摆杆角度调试串口

- 修改原因：用户要求增加串口读取安装在摆杆运动部分、距右侧转轴 O 约 10～30 mm 的
  维特陀螺仪；它只作为摆杆/QD4310 调试角度参考，不参与任何控制闭环。
- 最终决策：Bring-up Task 复用现有 G3519 AUX UART0（PB0 TX/PB1 RX、115200 8N1），
  启用已有 `Device_JY61P` 解析 `0x55`、11 字节和校验和协议。每 10 ms 最多读取
  128 字节，只有 500 ms 内的新合法 `0x53` 帧才令角度有效；离线不阻止小车运行。
  安装约定 Y 轴平行转轴 O、X 轴沿摆杆；按用户最终坐标约定，CH12 输出绝对摆杆
  Pitch=`ICM45686车体Pitch + JY61P摆杆Pitch`；CH13 输出最近合法 `0x53` 角度帧年龄
  ms，任一 Pitch 无效时为 `0,9999`；
  原 CH0～CH11 不变，FireWater 总列数从 12 增为 14。
- 涉及文件：根/Task CMake、`Task/task_bringup_chassis.*`、`Task/README.md`、
  `Device/Sensor/IMU/JY61P` 源码/README、`tests/jy61p`、两个测试入口、
  `config/core_config.h`、验证组件清单、`docs/DEVICE_PARAMETERS.md` 和 `COOPERATION.md`。
  未修改 SysConfig、BSP、DMA/ISR、PeerLink协议、Task周期/优先级或F407源码。
- 验证方式和结果：Auto增量验证10个组件通过，包括架构/差异检查、相关host测试、
  Cortex-M0+严格编译及G3519 Bring-up Debug/Release完整链接；当前BIN分别为
  `77888/74176 B`，SRAM_BANK0分别为`19152/19144 B`。
- 实物只读检查：显式选择 MSPM0G3519 DAPLink 后，UART0 PB1 收到连续合法
  `55 51`、`55 52`、`55 53` 帧，115200 配置和抽查校验和正确；示例角度帧
  `55 53 9d 30 bb 0f 16 cf 9f 46 09` 的 JY61P Pitch 约为 22.12°。检查后恢复目标运行，
  未在该诊断步骤写 Flash。
- 烧录与回读：DAPLink Doctor 通过；显式选择 MSPM0G3519 Bring-up Debug 后写入
  `77888 B`，完整回读 SHA-256
  `CD4DEEEA64CF09A3D90F9B6405B08E19B2005491F833E8A84E6BE8B5AE4B43FA` 与源 BIN
  一致，随后复位运行。
- 运行快照：主故障为 0，JY61P ready/online/has_euler 均为真；有效帧 19618、`0x53`
  帧 6538、校验错误 1。ICM45686 与 JY61P 的样本时间戳同为 33166 ms；当时 ICM Pitch
  `4.81097°`、JY Pitch `16.20483°`，求和 `21.01580°`，证明当前固件在运行期消费并按
  公式组合两个快照。同步边界是同一 10 ms Task 心跳时间戳，并非硬件同时采样；该
  调试器读取也不等同于蓝牙 FireWater 完整行抓取。
- 已知限制：JY61P Pitch 方向、杆面水平安装零偏和相对 QD4310 原始角的映射尚未标定；
  当前源码不执行 Pitch Tare。代码编译、串口帧和求和正确都不能证明机械角度已经准确。
- 待确认事项：从实际蓝牙无线串口确认固定 14 列、CH13 通常小于 20～30 ms；手动抬杆
  确认组合 Pitch 单调和符号后，再与 QD4310 原始角做五点标定。

## 2026-07-30：TFT 加入本次巡检时间

- 修改原因：用户要求在现有 G3519 Bring-up TFT 状态页显示巡检时间，便于现场直接观察
  单次发车至停车的运行耗时。
- 最终决策：复用现有 `CAR` 行，不增加显示行或 Task。成功发车时将计时清零；小车处于
  巡检运行状态时，以现有毫秒 Tick 的无符号时间差累计；规划停车或故障停车后保留最后
  一次累计值。显示格式为 `CAR:RUN mm:ss.d` 或 `CAR:STOP mm:ss.d`，精度 0.1 s；受
  16 字符行宽限制，显示值在 `99:59.9` 钳制，内部毫秒计数不钳制。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/README.md` 和本记录。未修改 TFT Device、
  BSP/SysConfig、Task 周期/优先级、VOFA 14 列、PeerLink/F407 协议或控制逻辑；新增静态
  RAM 为一个 `uint32_t` 计时值。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过模板架构检查、差异
  检查和 G3519 Bring-up Debug/Release 完整链接；修正定长字符构造后 Debug 无字符串截断
  告警。最终 Debug/Release Flash 为 `78104/74368 B`，SRAM_BANK0 为
  `19160/19152 B`。
- 烧录与回读：用户明确授权后，DAPLink Doctor 通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908` 的 G3519 DAPLink，写入当前
  Bring-up Debug `78104 B`。从主 Flash `0x00000000` 按相同长度完整回读，源 BIN 与
  回读 SHA-256 均为
  `2936A735853ED726F9A86519F33ED38865A6C37245D92E2FAD181BBEDC2D8698`，随后复位运行。
- 已知限制：计时表示 Task 记录的发车后运行时间，不是独立硬件计时器或比赛官方计时；
  超过 `99:59.9` 后 TFT 保持显示上限。烧录与回读一致只能证明目标写入完整，尚未实际
  操作 KEY 并观察 TFT 的动态刷新。
- 待确认事项：现场观察发车清零、运行递增、正常/故障停车保持以及再次发车重新清零
  是否符合使用习惯。

## 2026-07-30：TFT 按变化字符局部刷新

- 修改原因：用户要求说明当前 TFT 页面，并降低无变化内容和动态数字刷新造成的 CPU/SPI
  开销。
- 最终决策：保留现有 10 行内容、每 20 ms 检查一行和约 200 ms 全页扫描周期。整行文字
  与颜色不变时继续零 SPI 写入；普通文字变化时只生成并发送首尾变化位置覆盖的连续
  `8×16` 字符格，单字符变化由整行 `4096 B` 像素数据降为 `256 B`；首次显示或颜色变化
  仍完整重绘 `128×16` 行，以清除红屏自检背景并统一字色。QD4310 遥测只在第 3～7 行、
  绝对编码器只在第 8 行扫描时读取，其他行不再获取无关快照。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/README.md` 和本记录。未修改 ST7735S
  Device、BSP/SysConfig、SPI 时钟、Task 周期/优先级、控制、VOFA 或板间协议；未增加
  Task、堆内存或静态 RAM。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过模板架构检查、差异
  检查和 G3519 Bring-up Debug/Release 完整链接，无新增编译告警。Debug/Release Flash 为
  `78384/74592 B`，SRAM_BANK0 保持 `19160/19152 B`。
- 已知限制：当前只完成源码和完整链接验证，尚未测量局部 SPI 写入 WCET、Task 抖动或
  栈高水位，也未观察字符缩短、颜色切换时是否残留旧像素。板上仍运行此前已回读验证的
  `78104 B` 巡检计时版本，不包含本次局部刷新优化。
- 待确认事项：明确授权烧录本版后，观察十行首次清屏、动态数字、在线/离线和 RUN/STOP
  颜色切换，并测量优化前后的 SPI 写入时间与 Bring-up Task 余量。

## 2026-07-30：TFT 精简为三行状态页

- 修改原因：用户明确只保留灰度状态、车辆巡检时间，并将当前 TFT 的 PeerLink/QD4310
  通信状态合并成一行；ID/使能、电流、转速、角度、绝对编码器、心跳和故障明细均不再
  需要。上条“保留 10 行”的显示内容决策已被本次要求覆盖。
- 最终决策：页面固定为三行。第 1 行只显示 `LINE:OK/LOST/STALE`，不显示灰度误差；
  第 2 行保留 `CAR:RUN/STOP mm:ss.d`；第 3 行显示 `COM:Lx Qx`，`L` 表示 PeerLink
  当前在线、`Q` 表示 200 ms 内新鲜 `TEL` 中的 QD4310 在线位，二者均在线为绿色，任一
  离线为红色。每 50 ms 检查一行，约 150 ms 扫描全页；继续使用变化字符局部刷新，未变
  内容不写 SPI。红屏自检后一次清黑下方未使用区域，运行期不再访问该区域。
- 涉及文件：`Task/task_bringup_chassis.c/.h`、`Task/README.md`、`config/core_config.h` 和
  本记录。未修改 ST7735S Device、BSP/SysConfig、SPI 时钟、控制、VOFA 14 列、PeerLink
  协议或 F407 源码；未增加 Task、状态机、堆内存或静态缓冲。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过通信 host 测试、
  ARM 严格编译、模板架构/差异检查及相关 Debug/Release 完整链接。G3519 Bring-up
  Debug/Release BIN 为 `77392/73784 B`，SRAM_BANK0 为 `19032/19024 B`；相比十行局部
  刷新版，静态 SRAM 各减少 128 B。
- 烧录与回读：用户明确授权后，DAPLink Doctor 通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908` 的 G3519 DAPLink，写入当前
  Bring-up Debug `77392 B`。从主 Flash `0x00000000` 按相同长度完整回读，源 BIN 与
  回读 SHA-256 均为
  `6379E595878D2A9565A14661226240FFFB9F65ED0CF6F355247E65E3C95C9812`，随后复位运行。
- 已知限制：烧录和回读一致证明目标已部署当前镜像，但约 150 ms 页面周期、启动下半屏
  清黑、局部刷新无残影以及通信颜色切换仍未进行肉眼观察；Bring-up Task WCET、抖动和
  栈高水位也尚未实测。
- 待确认事项：现场观察 `LINE` 三态、巡检计时、`COM:Lx Qx` 断联切换和下方区域清黑，
  并测量 Bring-up Task WCET/栈高水位。

## 2026-07-30：运行期冻结 TFT 与发车硬件门

- 修改原因：用户要求发车后不再刷新 TFT，停车后才显示最终巡检时间；任何可检测的
  控制硬件掉线时禁止发车。
- 最终决策：停车待机时继续每 50 ms 检查一行；成功发车后不再调用状态页更新函数，
  因而不做显示快照读取、文字格式化或 TFT SPI 写入。线性制动期间仍属于运行态并保持
  冻结；`g_tracking_active` 由真变假时立即只更新车辆行的 `CAR:STOP mm:ss.d` 最终时间，
  随后恢复待机扫描。运行期间保留发车前画面，不绘制递增的 `CAR:RUN` 时间。
- 发车门槛：每个新 T8 帧无论停车或运行均只解析一次；按键发车同时要求灰度样本在
  100 ms 新鲜窗口内且检测到线、ICM45686 当前 `feedback_valid`、PeerLink 当前在线、
  200 ms 内存在新鲜 `TEL` 且 QD4310 在线，以及 `g_BRINGUP_Fault==0`。任一条件不满足
  时按键不启动电机。JY61P 继续只作摆杆标定和指示，不参与门槛或任何控制。
- 显示决策：第三行由 `COM:Lx Qx` 更新为 `COM:Ix Lx Qx`；`I/L/Q` 分别表示 ICM45686
  有效、PeerLink 在线和 QD4310 新鲜在线，三项全真为绿色，否则为红色。`LINE` 行独立
  表示灰度发车条件。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/README.md` 和本记录。未修改 BSP、
  SysConfig、引脚、Task 周期/优先级、速度规划参数、JY61P 控制边界、VOFA 14 列或
  `HB/TEL/ENC/DIS` 协议；未增加 Task、堆内存、重试或自动恢复状态。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过模板架构、差异检查
  以及 G3519 Bring-up Debug/Release ARM 完整编译和链接，无新增编译告警。Debug/Release
  Flash 为 `77728/74104 B`，SRAM_BANK0 为 `19032/19024 B`。
- 烧录与回读：用户明确授权后，G3519+DAPLink Doctor 通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入 Bring-up Debug
  `77728 B`。从主 Flash `0x00000000` 按相同长度完整回读，源 BIN 与目标回读 SHA-256
  均为 `3E31077AE45EB25F78BD33B5B5BD524795A6EAC1CA2E8D0EE58BE8AA10EA7A90`，随后
  `reset run`。
- 已知限制：烧录和全长回读证明当前镜像完整部署，不证明实际按键拒绝、TFT 运行期
  零 SPI、最终时间刷新、26H 闭环行为或 Task WCET/栈高水位。发车门只约束启动瞬间；
  本次需求未授权把 PeerLink/QD4310/ICM 运行中掉线改成新的自动停车策略。
- 待确认事项：现场分别断开 T8、ICM45686、PeerLink/F407 或令 `TEL/QD4310` 过期，
  确认按键不发车；三项通信正常且 `LINE:OK` 时确认可发车、运行中屏幕完全静止，并在
  规划停车后确认最终时间一次更新。

## 2026-07-30：G3519 灰度切换为亚博 UART 模拟量主动上报

- 修改原因：用户将现有灰度硬件更换为亚博八路巡线红外传感器；旧固件仍周期发送芯流
  T8 二进制查询，因协议不兼容收不到灰度帧，导致 `LINE` 发车条件始终不能成立。
- 最终决策：保留 `Device_XinliuT8` 和 `Device_Ganwei` 通用驱动，新增独立
  `Device_YahboomTrack`。解析层只复用厂家资料中的 115200 8N1、`$0,1,0#` 启动命令和
  `$A,x1:...,...,x8:...#` 原始模拟帧，不采用厂家/旧参考工程的白底、阈值或加权算法。
  解析器要求 x1～x8 顺序完整、纯十进制 `uint16_t` 和 `#` 帧尾，支持 UART 任意分块、
  新 `$` 重同步、超长/溢出/格式错误统计，且无堆、无 `strtok/atoi/sscanf`。Task 在串口和
  Device 初始化后只提交一次启动命令，之后每 10 ms 只排空主动上报字节；每个新快照仍
  原样调用既有 `Device_LineTracking_UpdateAnalogRaw()`，保留右偏为正、共同最小值消除、
  Mode、速度规划、发车门、TFT、JY61P 和 `HB/TEL/ENC/DIS` 行为。
- 涉及文件：新增 `Device/Sensor/LineTracking/YahboomTrack/`；修改根/Device/Task CMake、
  `CMakePresets.json`、`Task/task_bringup_chassis.c/.h`、LineTracking host/ARM 测试入口、
  验证组件描述、相关 README、`docs/DEVICE_PARAMETERS.md`、`COOPERATION.md` 和本记录。
  未修改 BSP、SysConfig、UART3 PC6/PC7、115200 8N1、Task 周期/优先级、灰度算法参数、
  速度参数、VOFA 14 列或板间协议；未增加 Task、堆内存、重试或自动恢复状态。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 执行 10 个受影响组件并
  通过模板架构/差异检查、相关 host 测试、Cortex-M0+ 严格编译和完整链接。新增测试覆盖
  启动命令、跨块帧、帧外噪声、重同步、乱序/缺通道、`uint16_t` 溢出和 128 B 超长帧；
  G3519 Bring-up Debug/Release Flash 为 `77800/74168 B`，SRAM_BANK0 为
  `19136/19128 B`。
- 烧录与回读：用户明确授权后，G3519+DAPLink Doctor 通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入 Bring-up Debug
  `77800 B`。从主 Flash `0x00000000` 按相同长度完整回读，源 BIN 与目标回读 SHA-256
  均为 `3B33B3F58B06430EEC522D292D37005316200ADA7178C02A6C8891B85B69FA46`，随后
  `reset run`。
- 实物只读检查：复位运行约 3 s 后，解析器累计接受 291 帧、拒绝 1 个格式错误帧，
  无数值溢出或超长帧；最新 x1～x8 为 `835,1066,812,866,594,642,870,554`。
  原灰度算法输出 `line_present=true`、`line_lost=false`、位置 `-0.2615`、置信度
  `0.9566`，主故障和 TFT 故障均为 0；检查后再次 `reset run`。
- 已知限制：厂家资料未给主动上报频率；冷上电资料要求等待传感器稳定，但当前按用户边界
  只提交一次启动命令且不重试，若模块单独掉电重启，需要复位 G3519 才会再次启动上报。
  当前抽样已证明 UART 收到连续合法帧并进入原灰度算法，但单个静态位置不能确认安装方向、
  白底/黑线完整 ADC 分布或全赛道 `LINE` 状态切换。
- 待确认事项：在赛道白底与黑线上移动模块，确认 x1 为车左、x8 为车右以及原灰度算法
  能正确切换 `LINE:OK/LOST/STALE`；实车发车前仍需确认 ICM、PeerLink 和 QD4310 发车门
  同时在线。

## 2026-07-30：终点横杠两帧确认与停车余量

- 修改原因：用户确认小黑横杠作为一圈终点参考，要求识别后再走 `210 mm`，并要求横杠
  必须经过两帧确认；同时根据首轮停车趋势下调有效减速度和弯道速度。
- 最终决策：只在路线平均里程达到 `5000 mm` 后检查亚博原始模拟量；单个新帧至少
  `6/8` 路 ADC 不低于 `900` 才算候选，连续两个不同 `snapshot.sequence` 的新帧均为
  候选才锁存，任一新帧不满足即将计数清零。同一帧不会被 10 ms Task 重复累计。锁存
  仅发生一次，纵向目标改为第二帧确认时左右轮平均里程 `+210 mm`；默认一圈
  `6141.6 mm` 目标仍负责横杠漏检回退和锁存前的制动包络。普通横向循迹继续使用原共同
  最小值消除算法，不采用厂家阈值循迹算法。
- 参数变化：纵向减速度由 `220` 下调为 `185 mm/s²`；直道保持 `420 mm/s`，半圆由
  `320` 下调为 `300 mm/s`。横杠锁存后仍由同一线性速度规划器前瞻减速，不直接清零
  电机；VOFA 固定 14 列不变，CH7 会反映更新后的剩余距离。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`、
  `COOPERATION.md` 和本记录。未修改 BSP、SysConfig、UART 协议、Task 周期/优先级、
  JY61P 控制边界、PeerLink 协议或 F407 源码。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过 Navigation、
  SpeedProfile 和 LineTracking host 测试、模板检查、Cortex-M0+ 严格编译，以及 Feature
  Smoke 与 G3519 Bring-up Debug/Release 完整链接。Debug/Release Flash 分别为
  `78072/74392 B`，SRAM_BANK0 分别为 `19144/19136 B`。
- 烧录与回读：用户明确授权后，G3519+DAPLink Doctor 通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入 Bring-up Debug
  `78072 B`。从主 Flash `0x00000000` 按相同长度完整回读，源 BIN 与回读 SHA-256 均为
  `9C32A3F9B55F1786D230DDFE24164E1175A4BFD9EB1B1F0A8222B79B25B0BC05`，随后执行
  `reset run`；F407 未修改。
- 已知限制：横杠检测位于产品 Task 私有逻辑，没有独立 host 单测；`900`、`6/8`、
  `5000 mm` 和 `210 mm` 是当前初值，尚未用终点横杠处的 x1～x8 与最终停车偏差实测
  校准。烧录和全长回读只能证明镜像完整部署，不能视为横杠两帧确认和停车精度已经完成
  26H 实物验证。
- 待确认事项：实车记录横杠前后 x1～x8、两帧确认计数、锁存平均里程、更新后的目标
  里程和最终停车偏差，再决定是否调整 ADC 阈值、通道数、`210 mm` 或减速度。

## 2026-07-30：按 26H 题图收紧 A 点横杠检测窗口

- 修改原因：上一版把横杠检测从 `5000 mm` 开放，实车在 D→A 半圆上把弧线误认为横杠。
  题图明确启停线只位于 A 点，长度 `5 cm`、宽度 `1.8±0.2 cm` 且与环路垂直；D→A
  半圆的名义里程范围为 `4570.8～6141.6 mm`，因此旧门控会在弧线内提前开放。
- 实测证据：最新 `vofa+.csv` 为固定 14 列、2034 帧；单次运行在 CH0=`5000 mm` 时
  CH7 仍为名义剩余 `1141 mm`，下一帧 CH0=`5005 mm` 时 CH7 跳为 `207 mm`、速度阶段
  进入制动且 yaw=`113.12°`，最终约停在 `5265 mm`。这证明旧逻辑在门控刚开放后的
  第二个新帧立即误锁存，不是制动 PID 或两帧去重失效。旧 `5000 mm` 开放策略已否决。
- 最终决策：按一圈 `6141.6 mm` 和横杠后运行 `210 mm` 推导预计过线里程
  `5931.6 mm`，只在其 `±120 mm` 编码器窗口内处理横杠。进入窗口后必须先收到至少
  一个非横杠新帧才武装；随后仍要求连续两个不同序号的新帧均满足至少 `6/8` 路
  ADC≥`900` 才锁存。窗口外清除武装和确认计数；漏检仍由原 `6141.6 mm` 目标停车。
  横向共同最小值灰度算法、速度/减速度、Task 周期和 VOFA 14 列均未改变。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`、
  `COOPERATION.md` 和本记录。新增一个 Task 私有静态布尔状态；未增加 Task、堆内存、
  阻塞、重试或通信字段，未修改 BSP、SysConfig、PeerLink、JY61P 或 F407。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto` 通过模板架构/差异检查、
  Cortex-M0+ 严格编译及 G3519 Bring-up Debug/Release 完整链接。Debug/Release Flash
  分别为 `78136/74440 B`，SRAM_BANK0 分别为 `19144/19136 B`；静态 SRAM 相比上一版
  未增加。
- 烧录与回读：用户明确授权后，G3519+DAPLink Doctor 通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入 Bring-up Debug
  `78136 B`。从主 Flash `0x00000000` 按相同长度完整回读，源 BIN 与回读 SHA-256 均为
  `A886DDC54231E82E9867563C4F944383A3DA0047BBCE89523FCFC46CFFE3C8FC`，随后执行
  `reset run`；F407 未修改。
- 已知限制：横杠窗口逻辑位于产品 Task，没有独立 host 单测；`±120 mm`、ADC `900`、
  `6/8` 和 `210 mm` 尚未在真实 A 点横杠上完成闭环标定。烧录和全长回读不能证明弧线
  误识别已在实车消失，也不能证明最终停车偏差满足 `2 cm`。
- 待确认事项：下一圈确认 CH0=`5000～5811 mm` 期间 CH7 不再跳到约 `210 mm`；在 A 点
  前记录横杠锁存时 CH0/CH7、x1～x8 和最终停车偏差，再只调整窗口、阈值或余量中的
  必要参数。

## 2026-07-30：惯导航向有效门与编码器回退修正

- 修改原因：ICM45686 的 `feedback_valid` 可能早于陀螺零偏标定完成，旧发车门会过早把
  未完成零偏标定的 yaw-rate 当作可用惯导；运行中陀螺失效时，Task 又把无效 yaw-rate
  作为 `0 deg/s` 交给融合 Mode，使弯道短时航向被冻结。
- 最终决策：发车、TFT I 状态和融合惯导统一要求 ICM 当前反馈有效、陀螺零偏有效且
  标定状态为 `DEVICE_IMU_CALIBRATION_COMPLETE`。融合输入新增显式 yaw-rate 有效位；
  有效时保留扣除零偏后的陀螺传播和打滑门，无效时以
  `(right-left)/track_width` 编码器角速度连续传播航向并供 yaw-rate 控制器使用，同时
  清除旧打滑门。输出新增 `yaw_rate_fallback_active` 诊断位。F407 `CAR` 车体状态和
  `ICM45686 Pitch + JY61P Pitch` 绝对摆杆角仍使用原 ICM 当前反馈有效位，JY61P 不进入
  控制或发车门。
- 涉及文件：`Mode/Car/include/mode_car_fused_navigation.h`、
  `Mode/Car/Navigation/mode_car_fused_navigation.c`、对应 README 和 host 测试，以及
  `Task/task_bringup_chassis.c`、`Task/README.md` 与本记录。未修改 ICM 安装轴、轮距、
  BSP/SysConfig、引脚、Task 周期/优先级、速度规划参数或 JY61P 控制边界。
- 验证方式和结果：新增 host 测试覆盖陀螺失效后用差速里程传播航向；
  `tools/project.ps1 -Action Verify -Scope Auto` 通过模板架构/差异检查、Communication、
  Navigation、SpeedProfile、LineTracking host 测试、Cortex-M0+ ARM 严格编译及受影响
  Debug/Release 完整链接。随后经 TI SysConfig 重新生成并完成 G3519 Bring-up
  Debug/Release 干净链接；FLASH 分别为 `79024/75080 B`，SRAM_BANK0 分别为
  `19144/19136 B`。
- 烧录与回读：G3519+DAPLink Doctor 通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入 Bring-up Debug
  `79024 B`。从主 Flash `0x00000000` 按相同长度完整回读，源 BIN 与回读 SHA-256 均为
  `35927063DA620D2FB1CFF25C9398126DB5F95F0078D80C76DE122DB2CEC509D4`，随后执行
  `reset run`；F407 未修改。
- 已知限制：烧录与回读一致只证明镜像完整部署；ICM 安装轴、有效轮距、实车航向正负、
  弯道航向闭环、打滑门和编码器回退切换尚未完成 26H 实物标定。编码器回退只能在陀螺
  无效时维持短时连续性，不能替代绝对航向重定位，也不能消除左右轮共同打滑误差。
- 待确认事项：静止上电等待零偏标定完成后确认 TFT `I1` 才允许发车；实车记录直线与
  左右半圆的编码器 yaw-rate、ICM yaw-rate、融合 yaw 和回退位，确认安装轴极性、
  `120 mm` 有效轮距及切换过程后再做机械参数标定。

## 2026-07-30：按 F407 接收契约加入 ACC/ACC_MEAS 发送

- 修改原因：Agent B 当前 F407 已按六字段 `ACC` 和六字段 `ACC_MEAS` 接收，G3519
  源码仍以 100 Hz 发送旧七字段 `CAR`，双方文档与实码不一致。
- 最终决策：PeerLink 新增 `ACC` 100 Hz 和 `ACC_MEAS` 50 Hz 非阻塞发送 API、严格范围/
  flags 语义校验、独立16位序号及成功/丢弃/错误计数。BUSY 整帧丢弃且不推进对应序号，
  不增加 ACK、重发、Task 或状态机；旧 `CAR` Device API 暂留兼容，但 Bring-up Task
  不再周期调用，活动发送收口为 `HB/ACC/ACC_MEAS/DIS`。
- 安全有效位：杆轴相对车头方向、MG513X输出轴CPR/有效轮周、ICM安装轴和去重力线
  加速度均未完成实物标定。当前 `TEMPLATE_PEER_LINK_ROD_AXIS_DIRECTION=0`、轮速尺度
  标定门为0，因此 `ACC` 规范发送 `0,0,65535,0`，`ACC_MEAS` 规范发送
  `processing_age=0,a=0,wheel=0,flags=0`。F407 前馈增益必须保持0；当前规划Mode没有
  原子化下一切换时刻，后续轴向标定后也只能先用 `a_next=a_now,next_after_ms=65535`。
- 涉及文件：`Device/Communication/PeerLink/`、`Task/task_bringup_chassis.c`、
  `config/core_config.h`、PeerLink主机测试及对应 README、设备参数和协作文档。未修改
  BSP、SysConfig、引脚、Task周期/优先级、速度规划器控制行为或 F407 源码。
- 验证方式和结果：Communication host测试及 ARM严格编译通过；
  `tools/project.ps1 -Action Verify -Scope Auto` 通过模板架构/差异检查和受影响完整链接；
  TI SysConfig 重新生成后，G3519 Bring-up Debug/Release 干净完整链接通过，FLASH分别
  为 `79216/75232 B`，SRAM_BANK0分别为 `19176/19160 B`。主机测试覆盖线上格式、边界、
  无效值/flags语义和 BUSY 不推进序号。
- 烧录与回读：用户明确授权后，显式选择 DAPLink 序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入 G3519 Bring-up Debug
  `79216 B`。从 `0x00000000` 按相同长度完整回读，源 BIN 与回读 SHA-256 均为
  `48B3F2DEDA506255FF07147CA516846E8DFF40DA22C583D83A0967424088452B`，随后执行
  `reset run`。
- 已知限制：烧录回读只证明新镜像完整部署；尚未在两板 UART 上抓取 100/50 Hz 实际
  帧率、序号和 F407 接收计数，也未完成任何轴向/尺度标定。因此本版不能启用底盘前馈，
  也不能宣称已经提供真实未来切换预告。
- 待确认事项：先抓取双端 `ACC/ACC_MEAS` 计数和断流失效，再依次标定杆轴正负、输出轴
  CPR/有效轮周、ICM安装轴及去重力补偿；每完成一项才开放对应有效位。

## 2026-07-30：VOFA 增加亚博八路原始灰度

- 修改原因：最新 `vofa+.csv` 共1998行，全部为固定14列；只有 CH10 归一化线误差，
  无法直接观察终点横杠处 x1～x8 原始 ADC，用户要求立即加入八路原始数据。
- 最终决策：保持原 CH0～CH13 语义不变，在蓝牙 FireWater 末尾固定追加 CH14～CH21，
  依次为亚博 `x1..x8` 原始 `uint16_t` ADC。Task只读取自身持有的 Yahboom Device
  Latest Value；未收到合法帧或样本超过现有100 ms新鲜度时，八路统一输出0。20 ms周期、
  115200 8N1、非阻塞 BUSY 丢帧、控制算法、TFT、PeerLink和发车门均不改变。
- 资源与带宽：继续使用原192字节Task栈内行缓冲，不新增Task、状态机、堆内存或BSP资源；
  正常22列行仍在缓冲和115200单向预算内。实际WCET、栈高水位和无线丢帧率尚未实测。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/README.md`、`config/core_config.h`、
  `COOPERATION.md`和本记录；保留用户现有 `vofa+.csv`，不修改历史采集数据。
- 验证方式和结果：最新 CSV 静态检查确认1998/1998行均为14列；修改后模板架构和
  `git diff HEAD --check` 通过，G3519 Bring-up Debug/Release完整链接通过，Flash分别为
  `79176/75232 B`，SRAM_BANK0分别为 `19144/19160 B`。格式字符串在目标严格警告配置下
  编译通过；尚未从蓝牙实抓22列行。
- 烧录与回读：用户先要求暂不烧录，随后明确解除限制。显式选择 DAPLink 序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入 G3519 Bring-up Debug
  `79176 B`；从 `0x00000000` 全长回读，源 BIN 与回读 SHA-256 均为
  `A1DB55D37FEA2D471500408123994AC81A9FF21A800D4A5B230DAB0BAA5C5F6B`，随后执行
  `reset run`。
- 待确认事项：从实际蓝牙串口保存一份新CSV，确认每行固定22列、CH14～CH21与亚博
  调试原帧一致，并记录终点横杠前后八路波形后再调整900阈值、6/8条件或检测窗口。

## 2026-07-30：G3519 接入 F407 `ACC/ACC_MEAS` 发送契约

- 修改原因：用户要求按滚球侧 Agent 的现有接收接口加入板间通信并烧录车端固件；此前
  文档曾写成新接口，但 Bring-up Task 实际仍周期发送旧 `CAR`。
- 最终决策：保留旧 `CAR` Device API 兼容面，但 Bring-up Task 停止调用。新增独立
  `ACC` 六字段 100 Hz 发送和 `ACC_MEAS` 六字段 50 Hz 发送，二者各用独立16位序号；
  UART BUSY 时整帧丢弃、不推进序号、无 ACK/重发。`ACC` 范围为 `±40000 mm/s²`；
  `ACC_MEAS` 用 flags 声明 IMU/轮速有效及轴向、去重力、尺度标定状态。
- 安全有效位：杆轴相对车头方向、MG513X输出轴CPR/有效轮周、ICM安装轴和去重力补偿
  尚未实物标定。当前方向配置为0、轮速尺度标定门为0，所以车端规范发送
  `ACC=0,0,65535,valid0` 和 `ACC_MEAS=age0,a0,wheel0,flags0`；F407前馈增益必须保持0。
  现有规划器未原子输出下一切换时刻，未来方向标定后的首版只能使用
  `a_next=a_now,next_after_ms=65535`，不得称为真实前瞻。
- 涉及文件：`Device/Communication/PeerLink/device_peer_link.[ch]`及README、
  `Task/task_bringup_chassis.c`及README、`config/core_config.h`、PeerLink主机测试、
  `docs/DEVICE_PARAMETERS.md`、`COOPERATION.md`和本记录。未修改BSP、SysConfig、引脚、
  Task周期/优先级、速度规划参数、JY61P控制边界或F407源码。
- 验证方式和结果：Communication host测试和Cortex-M0+严格编译通过；
  `tools/project.ps1 -Action Verify -Scope Auto` 通过架构/差异检查及受影响完整链接。
  随后经TI SysConfig重新生成并完成G3519 Bring-up Debug/Release干净链接，FLASH分别为
  `79216/75232 B`，SRAM_BANK0分别为`19176/19160 B`。Debug BIN SHA-256为
  `48B3F2DEDA506255FF07147CA516846E8DFF40DA22C583D83A0967424088452B`。
- 烧录状态：用户明确要求烧录，但指定序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908` 的 DAPLink 未被 Windows 枚举；
  Doctor 和 OpenOCD 均确认找不到 CMSIS-DAP，因此未执行擦除、写入、回读或复位，板上
  仍是此前镜像。重新连接探针后须写入上述 Bring-up Debug HEX，并按 `79216 B` 完整回读
  比较 SHA-256 后才能报告部署完成。
- 已知限制与待确认：当前只验证无效占位帧的软件格式，尚未做双端100/50 Hz抓帧、丢帧
  计数、F407 500 Hz持值/失效或任何带球前馈实测。先完成物理尺度和轴向标定，再分别
  开放有效位并从 `b=0` 开始联调。

## 2026-07-30：终点横杠前线性降速至 250 mm/s

- 修改原因：最新22列VOFA采集在约5833 mm完成横杠两帧确认，当时参考速度约296 mm/s、
  剩余目标约204 mm；按185 mm/s²减速度和35 mm guard需要约275 mm停车包络，实车最终
  停在约6098 mm，检测后多走约265 mm，较210 mm目标超出约55～60 mm。
- 最终决策：保持185 mm/s²柔和线性减速度，最后D→A半圆在约5737.3 mm起把巡航上限
  从300降为250 mm/s，并在预计横杠检测窗口下沿5811.6 mm前完成。推导距离为
  `(300²-250²)/(2×185)=74.3 mm`；250 mm/s时停车包络约
  `250²/(2×185)+35=203.9 mm`。继续复用既有线性速度规划器，不新增状态机。
- 行为边界：黑块 `6/8≥900`、连续两帧、`5931.6±120 mm`窗口、锁存里程后
  `+210 mm`目标和一圈兜底均不变；灰度循迹、INS、VOFA字段、PeerLink、SysConfig、BSP
  和F407均未修改。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`、
  `COOPERATION.md`和本记录。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构、差异检查、
  相关host测试、Cortex-M0+严格编译及受影响完整链接；G3519 Bring-up Debug/Release
  随后经SysConfig生成并干净完整链接，FLASH分别为78880/74864 B，SRAM_BANK0分别为
  19120/19104 B，零编译错误。
- 烧录与回读：DAPLink Doctor通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，写入Bring-up Debug 78880 B。
  从主Flash `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `19EE30C14AABE7909CC31F71859EF43188C99FF8F0A271FD6F45CB2F0A406579`，随后执行
  `reset run`。
- 已知限制：烧录回读只证明新镜像完整部署，不能证明实车能在横杠后精确210 mm停车；
  实际轮速滞后、地面摩擦、带球负载和编码器误差仍可能改变停车位置。
- 待确认事项：下一圈用VOFA确认约5737 mm后CH5以约-185 mm/s²降到250 mm/s，横杠
  两帧确认时CH5接近250、CH7跳到约210 mm，并测量最终停车里程；再决定是否微调
  250 mm/s接近速度、35 mm guard或210 mm标定值。

## 2026-07-30：修正车体尺寸并加入26H五种车端模式

- 修改原因：用户要求按26H第2～6项在TFT选择比赛模式，停车长按切换、短按确认；并纠正
  车体尺寸为两轮间距200 mm、轮胎直径65 mm、全车长度320 mm。此前源码轮距120 mm错误；
  用户中途给出的180 mm已被随后200 mm最终值覆盖，不再有效。
- 最终决策：差分惯导有效轮距改为200 mm；轮胎继续使用名义轮周204.20 mm；320 mm作为
  “通过B/A后再运行一车长停车”的产品几何量，不替代横杠检测后到车体停车基准的210 mm
  偏置。相同轮速差下，编码器航向变化由错误的`(vR-vL)/120`修正为`(vR-vL)/200`。
- 模式行为：M2顺时针一圈并停A；M3底盘锁定用于静车滚球；M4在1500 mm通过B并锁定成绩，
  再运行320 mm规划停车；M5/M6在车体基准通过A时锁定成绩，再运行320 mm规划停车。
  M2横杠目标为检测里程+210 mm，M5/M6为检测里程+530 mm；漏检目标分别为一圈和
  一圈+320 mm。M5/M6通过A后的速度上限保持250 mm/s，不在里程取模后重新加速到420。
- 按键/TFT：`Device_KeyInput`新增同一Task上下文的Update/ReadEvent分类事件接口；停车长按
  循环M2～M6，短按确认并在移动模式发车，运行中短按保留线性停车、长按不切模式、双击
  不执行动作。TFT从3行增至4行，新增模式行；黄色待确认、绿色已确认。继续遵守用户既定
  约束：发车后完全冻结TFT，M4/M5/M6的提前锁存成绩在最终停车后显示。
- 通信边界：Agent B只读核对F407当前只解析HB/CAR，没有MODE/TARGET/START或run_id接口；
  本轮未修改PeerLink线上协议。M3滚球动作与M6指定目标仍未实现双板单键联动，不得因车端
  菜单存在而宣称滚球端已经切换模式。
- 涉及文件：`Device/Input/Key/device_key_input.[ch]`及README、
  `Task/task_bringup_chassis.[ch]`及README、`config/core_config.h`、
  `docs/DEVICE_PARAMETERS.md`、仓库/桌面`COOPERATION.md`和本记录。未修改SysConfig、BSP、
  PeerLink、F407源码或VOFA字段。
- 验证方式和结果：两次`tools/project.ps1 -Action Verify -Scope Auto`均通过架构/差异检查和
  受影响host、Cortex-M0+严格编译及完整链接；随后G3519 Bring-up Debug/Release经SysConfig
  生成并干净完整链接，FLASH分别79656/75480 B，SRAM_BANK0分别19152/19144 B，零错误。
- 烧录与回读：用户明确授权后，DAPLink Doctor通过；显式选择序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug 79656 B。从
  `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `08755AA28CC206E5AC737EE166E9B64DA91BE3BBF7E0F681E7E26F32FF80DCAC`，随后执行
  `reset run`。
- 已知限制与待确认：烧录回读不证明按键800 ms长按手感、TFT第四色行实物显示、200 mm
  等效轮距、M4过B锁时和M5/M6过A后320 mm停车已经实车通过。先静止验证M2→M6循环、
  短按确认颜色和硬件离线禁发车，再按M4低速记录1500/1820 mm时刻，最后分别验证M2停A
  与M5/M6通过A语义。M6目标输入与F407接收契约需A/B双方另行冻结。

## 2026-07-30：`HB/CAR`最终收口与双板部署

- 修改原因：用户最终要求板间不再发送QD4310/CAN明细，只向F407提供其需要的车速、
  ICM45686加速度计比力和Pitch角速度，并明确授权烧录两端最终Debug镜像。
- 最终决策：活动协议只保留双向500 ms `HB`和G3519到F407的100 Hz七字段`CAR`。
  `TEL/ENC/ACC/ACC_MEAS/DIS`的编解码、Task调用和跨板状态均已删除；停车稳定零速后仅
  锁定G3519本地两轮。F407本地QD4310/CAN控制不变，纵向IMU预处理已接入但融合开关与
  前馈增益均为0，不会改变滚球目标或电机控制。
- 涉及文件：PeerLink Device/测试/README、G3519 Bring-up Task/README、
  `docs/DEVICE_PARAMETERS.md`、双端协议文档、F407 PeerLink/Task内既有CAR消费链、
  `COOPERATION.md`和双端process记录；未修改SysConfig引脚、Task周期或电机控制参数。
- 验证方式和结果：TI Communication主机测试、Cortex-M0+严格编译、架构检查和差异检查
  通过。首次烧录后工作区又完成一次用户既有的按键事件API、Bring-up Task和产品配置
  更新；未回退该并发改动，以更新后的当前源码重新执行Auto验证并使用其clean-first产物。
  G3519 Bring-up Debug/Release最终完整链接为FLASH `79368/75192 B`、SRAM_BANK0
  `19152/19136 B`。F407 PeerLink CAR与ChassisImuAcceleration主机测试通过，
  Debug/Release clean-first完整链接为FLASH `34708/30180 B`、RAM `17672/17656 B`。
- 烧录与回读：G3519显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，最终写入当前Bring-up Debug
  79368 B并从`0x00000000`全长回读，源BIN与回读SHA-256均为
  `E5E0E1E4D75A56A84940B3E9EDB1323AB68E16F5F6CDBCD830E45F18BDE5385C`。F407显式选择
  `VID:PID 0416:5051`的CMSIS-DAP，识别Cortex-M4、DEV_ID尾码`0x413`和512 KiB Flash，
  写入Debug 34708 B并从`0x08000000`全长回读，源BIN与回读SHA-256均为
  `56362B8704CF4822E8008C2781EF096270E2D152747D8F73F34A44A86B9E6A36`。两端校验后均已
  执行`reset run`。
- 已知限制：烧录回读不等于板间业务验收；最终双端镜像尚未连续抓取100 Hz CAR、验证
  50 ms断流失效、动态ICM轴向/轮速比例或滚球前馈。F407融合和前馈保持关闭。
- 待确认事项：保持滚球电机失能，抓取至少10 s双端HB/CAR计数、序号、sample_ms、
  valid和UART/DMA错误；再做正反向手推、起步和制动日志，确认轴向与延迟后再讨论启用融合。

## 2026-07-30：终点横杠后停车距离由210 mm缩短为150 mm

- 修改原因：最新22列VOFA中，横杠在路线里程约5880/5885 mm连续两帧达到`6/8≥900`
  并确认，目标约6090 mm、最终停车约6097 mm。用户实物确认仍偏晚，明确要求改为15 cm。
- 最终决策：黑块条件保持“大于5路黑”和连续两帧；搜索窗口继续独立保持
  `5931.6±120 mm`。M2目标改为标记`+150 mm`，M5/M6改为标记`+470 mm`。
- 速度约束：250 mm/s在185 mm/s²和35 mm guard下名义制动包络约203.9 mm，无法满足
  150 mm；终点接近速度同步改为200 mm/s，名义包络约143.1 mm，并从窗口下沿前约
  135.1 mm开始由300 mm/s线性降速。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.[ch]`、`Task/README.md`、
  `docs/DEVICE_PARAMETERS.md`、仓库/桌面`COOPERATION.md`和本记录。未修改ADC阈值、
  确认帧数、窗口宽度、Task周期、SysConfig、PeerLink、F407或VOFA字段。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构、差异检查、ARM编译及
  受影响完整链接；G3519 Bring-up Debug/Release分别使用FLASH `79656/75480 B`、
  SRAM_BANK0 `19152/19144 B`。用户授权后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `79656 B`；
  从主Flash `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `51556E2091FFB23E11B6E0F93B4CD2BC2E8D85EF8ADE5023F38B1A9DFA5A1AD6`，随后执行
  `reset run`。
- 已知限制：同一VOFA还显示发车初始`v_ref≈1 mm/s`时左右目标约`-196/+199 mm/s`，说明
  灰度差速会绕过单轮目标线性斜率；本次只修正终点停车距离，该问题仍待单独修复。
- 待确认事项：下一圈确认约5676.5 mm后CH5从300线性降至200 mm/s，横杠确认后CH7
  跳到约150 mm，并测量最终停车相对黑块前沿的位置。

## 2026-07-30：终点横杠确认后立即开始停车

- 修改原因：用户要求观察识别到黑块后的即时停车响应；此前`150 mm`附加距离已被覆盖。
- 最终决策：`TEMPLATE_BRINGUP_FINISH_MARKER_STOP_RUNOUT_MM`改为`0.0F`。继续保持
  `6/8≥900`、连续两帧和`5931.6±120 mm`窗口；M2在确认后立即把目标设为当前里程，
  M5/M6仍在该基准后运行一车长，目标为标记里程`+320 mm`。
- 行为边界：不直接清零或锁死电机，仍由现有规划器按`185 mm/s²`线性减速，因此识别后
  立即开始制动，但物理停车距离不会为零。
- 涉及文件：`config/core_config.h`、`Task/README.md`、`docs/DEVICE_PARAMETERS.md`、
  仓库/桌面`COOPERATION.md`和本记录；未修改阈值、确认帧数、Task、BSP或通信。
- 验证结果：`tools/project.ps1 -Action Verify -Scope Auto`通过；当前G3519 Bring-up
  Debug/Release完整链接BIN分别为`80736/76408 B`。
- 部署状态：用户明确授权后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入G3519 Bring-up Debug
  `80736 B`。从主Flash `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `CB8A90A2E6642A20E58AAF25AEF809D5C17F10C9BDAF8BD343C2DB7ADB876ABC`，随后执行
  `reset run`。

## 2026-07-30：F407滚球关键量追加到COM5/VOFA

- 修改原因：用户要求用DAPLink相关串口链路持续送入VOFA，避免SWD逐次读取，方便观察
  ICM补偿、角度目标、raw、电流和phase。
- 最终决策：G3519 PeerLink接收F407节点1的50 Hz只读`DBG`，执行规范十进制、范围、来源、
  独立序号和100 ms本地新鲜度检查。蓝牙FireWater保留CH0～CH21，追加CH22～CH31；
  CH31 bit9由G3519置位表示DBG新鲜，101 ms起十列全0。DBG不进入任何控制。
- 涉及文件：`Device/Communication/PeerLink/`、`Task/task_bringup_chassis.[ch]`、Task/根README、
  主机测试、协议/协作文档和本记录；未改Task周期、优先级、SysConfig、引脚或电机控制。
- 验证方式和结果：Communication主机测试通过，覆盖合法边界、100/101 ms、错误来源、
  phase越界、重复/旧序号和F407重启清旧值。Bring-up Debug/Release clean-first完整链接通过，
  FLASH为`80768/76520 B`，SRAM_BANK0为`19216/19200 B`。
- 已知限制：FireWater格式器尚无独立host测试，也未做双端字节互喂；旧22列约6.1 kB/s，
  新增最坏约3.0 kB/s，理论总占用约79%，必须上板观察BUSY计数和实际帧完整性。
- 待确认事项：烧录双端后用COM5、115200、8N1、FireWater连续采集，确认每行严格32列、
  约50 Hz，CH31 bit9新鲜，CH22～CH30随ICM/电机状态变化，断开F407后101 ms起十列归零。

## 2026-07-30：终点停车前瞻速度降至100 mm/s

- 修改原因：用户实测认为横杠确认时才从`200 mm/s`停车仍然过晚，要求依据里程计提前减速。
- 最终决策：保持黑块确认后的附加距离`0 mm`、`6/8≥900`、连续两帧和
  `5931.6±120 mm`窗口不变；终点接近速度由`200 mm/s`降为`100 mm/s`。
- 前瞻行为：规划器按`185 mm/s²`在窗口下沿前约`216.2 mm`开始由`300 mm/s`线性减速，
  约在路线里程`5595.4 mm`开始，进入窗口前达到`100 mm/s`。横杠确认后继续线性减速到零。
- 预期影响：理想匀减速制动距离由约`108.1 mm`降至`27.0 mm`，但实际仍受电机响应、
  地面摩擦、编码器误差和带球负载影响，不能视为实物停车距离保证。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`、
  仓库/桌面`COOPERATION.md`和本记录；未修改减速度、灰度阈值、Task、BSP或通信。
- 验证结果：`tools/project.ps1 -Action Verify -Scope Auto`通过；当前G3519 Bring-up
  Debug/Release完整链接BIN分别为`80768/76520 B`。
- 部署状态：用户明确授权后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入G3519 Bring-up Debug
  `80768 B`。从主Flash `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `DD4AD3109F426E02CEC9F339B1D75D63447D6E528B3FFCB17582B537E34AF1BC`，随后执行
  `reset run`。

## 2026-07-30：依据VOFA修正高速轮速PID

- 数据证据：最新`vofa+.csv`为32列、5405帧，包含两次完整运行。最后一次运行中轮速目标
  到反馈的最佳对齐约为`20 ms`；全部有效运动帧左/右轮误差偏置分别约`-37.7/-42.4 mm/s`，
  MAE约`48.4/52.5 mm/s`。低速弯道反馈达到目标的`93%～96%`，直道高速段仅`80%～88%`。
- 停车与航向：两次横杠确认到稳定停车分别多走约`23/40 mm`，最终里程`5868/5862 mm`；
  停车航向相对各自发车航向残留`+5.78/+5.18°`。当前Task在速度规划稳定为零后直接锁轮，
  没有显式停车前航向对齐，因此“车头回正”尚未完成。
- 最终决策：只修正`>300 mm/s`高速轮速段，`Kp`由`0.20`增至`0.26`、`Ki`由`0.20`
  增至`0.30`，`Kd=0.0005`不变；低速PID、前馈、减速度和停车参数均不修改。
- 修改边界：仅修改`config/core_config.h`和协作/过程记录，不增加Task状态、回正动作、
  通信字段或硬件配置。
- 验证结果：Navigation/SpeedProfile/LineTracking host测试、ARM严格编译、架构检查和
  G3519 Bring-up Debug/Release完整链接通过，BIN分别为`80768/76520 B`。
- 已知限制：发车首帧仍存在`v_ref≈1 mm/s`而左右目标约`-210/+213 mm/s`的差速突跳，
  该问题来自横向修正，不是轮速PID；本轮未修复，且尚无新PID下的VOFA闭环数据。
- 部署状态：用户明确授权后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入G3519 Bring-up Debug
  `80768 B`。从主Flash `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `C1C2BC242F6C2272521DCC8397664D7CBB973ECFDB901B3997F2A9EC633C567D`，随后执行
  `reset run`。
- 待确认事项：烧录后比较直道高速反馈/目标比例、MAE、超调和停车距离；航向回正应另行
  冻结允许的原地转向速度、角度容差、稳定时间和超时后再实现。

## 2026-07-30：发车与停车必须为同一位姿

- 修改原因：用户明确要求“哪里发车，哪里停车”，且角度和位置都必须精准。
- 最终决策：闭环跑圈以驱动轮轴中点为位置参考，发车时锁存位置原点与ICM相对yaw；
  最终停车必须同时满足回到发车位置和恢复发车航向。横杠、灰度、ICM与编码器分别只承担
  纵向锚点、横向锚点、航向锚点和前瞻连续预测，任一单项满足都不能代替整个位姿验收。
- 覆盖关系：旧M5/M6“通过A后再运行全车长320 mm停车”被本要求否决；成绩时刻仍可在
  通过A时锁存，但最终静止点必须回到发车位姿。M4按题目B点目标位姿单独验收。
- 涉及文件：只更新仓库/桌面`COOPERATION.md`、`docs/H_BALANCING_BALL_CONTROL.md`和
  本记录；未修改Task、Mode、PID、状态机、保护、通信、引脚或当前已烧录镜像。
- 当前证据：最新两次运行停车yaw相对发车仍残留`+5.78/+5.18 deg`，当前Task在速度规划
  稳定为零后直接锁轮，M5/M6仍在源码中增加320 mm，故当前固件明确未达标。
- 已知限制：仅靠一圈里程无法长期保证20 mm，横杠触发仍有制动距离；是否允许低速倒车、
  原地微转以及带球时的最终修正速度会改变安全行为，本轮未擅自决定。
- 待确认事项：冻结角度容差、是否允许倒车/原地微转和最终低速上限后，再实现终端位姿控制；
  验收至少连续5次记录纵向误差、横向误差、yaw误差、停车时间和低电量重复性。

## 2026-07-30：26H改为单陀螺仪航向并修正路线前视

- 修改原因：用户明确否决编码器航向与陀螺仪融合，要求ICM45686单独负责航向，并重新
  对照26H题图核查轨迹；同时强调每个模式的发车点、终止点和车头角度必须精确。
- 最终决策：关闭编码器航向低频校正。左右编码器平均值继续负责纵向里程和速度规划，
  差分航向仅保留诊断及转向前馈所需的200 mm轮距；陀螺无效时导航更新失败，不静默回退
  编码器航向。当前模式终点语义以Task源码为准，本轮不新增倒车、原地回正或终端状态机。
- 轨迹核对与修正：题面及源码均为A左上、B右上、C右下、D左下，顺时针
  `A→B→C→D→A`，AB/CD各1500 mm、BC/DA各R500半圆、总长6141.6 mm，几何公式正确。
  INS前视由600 mm降为200 mm，并按M2/M4/M5/M6各自路线终点钳位，修复M5/M6过A后仍
  回看第一圈A的错误。理想无扰运动学相位仿真中，600 mm的一圈最大位置误差约167 mm，
  200 mm约10 mm；这不是实车验收。
- 涉及文件：`config/core_config.h`、FusedNavigation Mode源码/头文件/README/主机测试、
  `Task/task_bringup_chassis.c`及README、`docs/DEVICE_PARAMETERS.md`、仓库/桌面
  `COOPERATION.md`和本记录。未修改BSP、SysConfig、引脚、通信协议、F407或视觉端。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查，
  Navigation、SpeedProfile、LineTracking主机测试，Navigation/SpeedProfile Cortex-M0+
  严格编译及受影响完整链接。G3519 Bring-up Debug/Release使用FLASH `80800/76616 B`、
  SRAM_BANK0 `19216/19200 B`。这是增量验证，不是全量基线或实物验证。
- 已知限制：题面停车偏差按车体中心轴唯一测试点到A基准线计算，不按灰度探头。当前
  `5931.6±120 mm`窗口与横杠后`0 mm`附加距离没有形成可证明的`≤2 cm`关系；速度归零后
  仍直接锁轮，也没有终端yaw微调，因此本轮不能宣称位置和角度已经实车精准。
- 待确认事项：同次记录灰度探头触线里程、车体唯一测试点过基准线里程、最终停车纵横向
  偏差和相对发车yaw；据此分别冻结评分偏置、停车目标和是否需要终端低速姿态修正。
- 部署状态：用户随后明确授权烧录；显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入G3519 Bring-up Debug
  `80800 B`。从主Flash `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `F2DE86DC777ADAD0F315D95B22B5E799F4E5753B86BC00BF8EB4482506C48055`，随后执行
  `reset run`。该证据只证明镜像部署，不提升轨迹或停车实物结论。

## 2026-07-30：M2停车目标按A线越界量前移180 mm

- 修改原因：单陀螺仪/200 mm前视镜像实测后，用户确认最终停车点越过A停车基准线
  `180 mm`；该数值是最终位置偏差，不是横杠识别后的单纯制动距离。
- 最终决策：M2不再等横杠出现后才请求停车。发车时将纵向里程目标设为预计横杠中心
  `5931.6 mm`前`180 mm`，即`5751.6 mm`，由既有速度规划器提前执行线性制动。横杠后
  附加距离仍为0；M5/M6通过A后320 mm语义不变。几何路径终点与纵向停车目标解耦，
  D→A路径不会因M2提前停车目标而被截断到5751.6 mm相位。
- 否决方案：检测横杠后把目标设为负180 mm不能让车辆回到已经越过的位置，只会得到与
  当前目标相同的立即减速行为；本轮不采用。把终点接近速度从100大幅降到30 mm/s也会
  进一步恶化题目第2项20 s时限，未采用。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`及README、
  `docs/DEVICE_PARAMETERS.md`、仓库/桌面`COOPERATION.md`和本记录。未修改Mode、PID、
  减速度、灰度阈值、Task周期、通信、BSP、SysConfig、F407或视觉端。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查及受影响
  Firmware完整链接；G3519 Bring-up Debug/Release使用FLASH `80848/76584 B`、
  SRAM_BANK0 `19216/19200 B`。本轮没有新的VOFA文件或硬件复测。
- 已知限制：180 mm只来自一次人工停车偏差，尚未形成重复统计；M2将在横杠窗口下沿
  5811.6 mm之前按里程停车，因此本轮位置锚完全依赖轮周、打滑和本次起点一致性，横杠
  不会在停车前提供二次校正。车头最终yaw仍未闭环验收。
- 待确认事项：烧录后连续至少3次记录最终停车相对A的有符号偏差和CH0/CH5/CH7；若平均
  仍有偏差，只调整`TEMPLATE_BRINGUP_M2_STOP_ADVANCE_MM`，不要同时改减速度或轮周。
- 部署状态：本次修改后尚未获得新的烧录授权，未写入目标板。

## 2026-07-30：M4改为AB直行隔离测试

- 修改原因：实车在AB段出现大量循迹转向，用户要求先去掉横向控制变量，只观察最简单的
  双轮直行和停车效果。
- 最终决策：M4纵向目标由通过B后再运行320 mm改为1500 mm。M4运行时左右轮直接使用
  相同的线性速度规划目标，不叠加灰度或INS差速；灰度、ICM和INS仍更新并保留VOFA观测。
  旧M4“通过B后再运行320 mm”行为被本次隔离测试临时覆盖。M2/M3/M5/M6保持不变。
- 行为边界：保留现有发车硬件门、灰度长时间无效后的规划停车、双轮速度PID、180/185
  `mm/s²`加减速、电机80%限幅和停稳锁车。1500 mm是规划器目标；现有35 mm guard和
  实车惯性仍影响最终物理停车点，不能表述为已在B点精准停车。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`和本记录；未修改Task周期、公共API、Mode、Device、BSP、SysConfig、
  PeerLink、其他赛题模式或硬件配置。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查和
  受影响Firmware完整链接；G3519 Bring-up Debug/Release使用FLASH `80864/76616 B`、
  SRAM_BANK0 `19216/19200 B`。这是增量构建验证，不是实物验证。
- 部署状态：已使用本机明确指定的DAPLink写入G3519 Bring-up Debug `80864 B`；从主Flash
  `0x00000000`完整回读，源BIN与回读SHA-256均为
  `F34B305BE196C0BB4B5B2D81FECAD3C4422D5396F9C209B04F179EF40CBE7D9A`，随后执行`reset run`。
- 待确认事项：实测M4时确认CH3/CH4全程相同，并记录CH0、CH1/CH2、CH9和
  最终停车位置；若仍明显转弯，优先检查左右轮机械/轮径差、编码器比例和双轮速度闭环，
  不再归因于灰度或INS转向输出。

## 2026-07-30：M4 AB直行加入发车航向PID

- 修改原因：左右同速隔离测试后，用户要求用角度PID保证AB直行并立即烧录。
- 最终决策：M4继续以1500 mm为纵向停车目标，并继续禁止灰度和路线前视参与转向；发车时
  锁存ICM45686 yaw，运行中由现有`Mode_Car_AngleController`以该yaw为目标生成左右轮差速，
  叠加在线性速度规划目标两侧。ICM导航无效时角度修正为零，不新增停机或回退状态机。
  M2/M3/M5/M6行为、Task周期、BSP、SysConfig、引脚和通信均未改变。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`和本记录。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查和受影响固件
  完整链接；G3519 Bring-up Debug/Release使用FLASH `82104/77960 B`、SRAM_BANK0
  `19256/19240 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug `82104 B`，从主Flash
  `0x00000000`全长回读，源BIN与回读SHA-256均为
  `C7564B9C1092D21447573AD47C0294721C38C6B65E3162EC6E5C2733A1604A62`，随后执行
  `reset run`。
- 已知限制与待确认：烧录回读只证明镜像完整部署；角度PID方向、增益、左右轮机械差异和
  1500 mm实际停车位置仍需本次AB实车运行确认。

## 2026-07-30：M2改为航向回零立即停车

- 修改原因：用户要求第2项不再以终点黑线作为停车触发，改为循迹航向回到发车0°时
  立即停车，并明确要求跳过验证和烧录回读。
- 最终决策：仅M2在进入最后半圆后检查连续惯导航向；相对发车yaw进入现有角度容差
  `±0.5°`即立即停止并锁定两轮，最后半圆门避免发车时0°误触发。M2忽略横杠停车事件，
  线性规划目标保留一圈加320 mm作为角度漏触发兜底。M3/M4/M5/M6保持不变。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`和本记录；未修改Task周期、Mode/Device公共API、BSP、SysConfig、引脚或通信。
- 构建与部署：按用户要求未运行项目验证、测试、Release链接或烧录回读。仅为生成镜像完成
  G3519 Bring-up Debug链接，FLASH/SRAM_BANK0为`82264/19256 B`；显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入`82264 B`并执行`reset run`。
- 已知限制与待确认：本轮没有回读完整性证据；`±0.5°`触发窗口、陀螺累计漂移和直接锁停
  的实际制动距离均待M2实车确认。

## 2026-07-30：取消M2独立导航差速上限

- 修改原因：用户实测M2最后航向回正过慢，要求取消左右轮差速上限并立即烧录。
- 最终决策：仅M2把导航yaw-rate控制原单侧`±115 mm/s`半差上限放宽到每轮目标安全边界
  `±600 mm/s`，因此不再存在独立的导航差速上限；合成后的每轮目标仍钳位在`±600 mm/s`。
  M3/M4/M5/M6继续使用原导航配置，灰度PID、速度PID和电机80%输出限幅不变。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`和本记录；未修改公共API、BSP、SysConfig、引脚、Task周期或通信。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查和受影响固件
  Debug/Release完整链接，FLASH为`82328/78120 B`、SRAM_BANK0为`19256/19240 B`。
  显式选择DAPLink序列号`000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug
  `82328 B`，从`0x00000000`全长回读，源BIN与回读SHA-256均为
  `81E69C4573017A07680CB24768E2D4457B812F7797046152680311DCF37875D6`，随后执行`reset run`。
  用户要求“不需要回读”的消息在本次回读完成后到达，因此没有再执行其他检查。
- 已知限制与待确认：取消独立差速上限会允许更激烈的瞬时转向，实际回正速度、轮胎打滑、
  轨迹超调和直接锁停位置仍需M2实车确认。

## 2026-07-30：修复M2航向采样跨零漏停

- 修改原因：最新`vofa+.csv`共2068帧、一次运行；发车yaw约`-4.08°`，最后半圆相对
  发车yaw误差在`6138→6139 mm`由`+3.06°`跳到`-0.68°`，最接近样本仍超出原
  `±0.5°`窗口，导致漏停并继续到约`6333 mm`里程兜底。
- 最终决策：仅M2保留`±0.5°`命中，同时增加相邻10 ms惯导样本真实跨0°即完成；以
  前后归一化误差之差小于180°排除`+180°/-180°`角度回绕假过零。最后半圆门、直接锁停、
  一圈加320 mm兜底和其他模式不变。
- 灰度核对：当前亚博模拟量链已先减去当帧八路共同最小值，再执行加权差比和
  `(R-L)/(R+L+epsilon)`并与加权质心融合；可抑制共同加性基线和共同增益变化，但没有逐路
  黑白标定，不能完全消除不均匀照明或通道个体差异。本轮未修改灰度算法。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`和本记录；未修改公共API、灰度Device/Algorithm、BSP、SysConfig或通信。
- 验证方式和结果：VOFA离线复算会在上述`+3.06°→-0.68°`样本跨零时触发，并拒绝数据中
  三次约`±180°`回绕；`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查和
  受影响固件Debug/Release完整链接，FLASH为`82440/78232 B`、SRAM_BANK0为
  `19256/19240 B`。
- 已知限制与待确认：尚未烧录；离线数据只能验证判定逻辑，直接锁停里程和高差速下的实际
  过零行为仍需新固件实车确认。

## 2026-07-30：恢复M2扩大后的组合差速限幅并提前灰度使能

- 修改原因：VOFA显示取消差速限制后M2出现`-600/+600 mm/s`级目标和脱轨；用户要求恢复
  比原值更宽的限幅，并把亚博灰度主动上报使能从上电20 s提前到1 s后发送。
- 最终决策：仅M2把导航yaw-rate半差上限设为`230 mm/s`，并在导航与灰度修正合成后再次
  把半差钳位到`±230 mm/s`，因此左右轮目标差不超过`460 mm/s`；这是原半差
  `115 mm/s`上限的两倍，每轮`±600 mm/s`安全边界继续保留。M3/M4/M5/M6不使用新增
  组合限幅。亚博Device初始化1 s后仍只发送一次`$0,1,0#`，不增加重试或状态机。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`、
  `Device/Sensor/LineTracking/YahboomTrack/device_yahboom_track.h`及其README、
  `docs/DEVICE_PARAMETERS.md`和本记录；未修改BSP、SysConfig、引脚、公共API或Task周期。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查、
  LineTracking host/ARM检查和受影响完整链接；G3519 Bring-up Debug/Release FLASH为
  `82536/78328 B`，SRAM_BANK0为`19256/19240 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug `82536 B`；从主Flash
  `0x00000000`全长回读，源BIN与回读SHA-256均为
  `51628A3C37AD9D9A1BCA8BCDAECF0A6ACF8F58BC07613882FC694C4CEEDD8A41`，随后执行
  `reset run`。该镜像也包含上一条M2真实跨0°立即停车修复。
- 已知限制与待确认：回读只证明镜像完整部署；`230 mm/s`半差限幅的循迹效果，以及灰度
  模块冷上电1 s时能否稳定接收一次性使能命令，仍需实车确认。

## 2026-07-30：M2改为惯导主控、灰度仅辅助

- 修改原因：用户要求加强两段直线的直走，并明确灰度循迹只应作为指导，不能成为主要
  转向来源；第二个半圆结束后仍须在车头回到发车方向时立即停车。
- 最终决策：仅M2在AB使用发车绝对yaw、在CD使用发车yaw加180°作为角度PID目标，两段
  直线不叠加路线前视转向；BC/DA两个半圆继续由INS几何前视主控。灰度PID输出只按20%
  叠加为辅助修正，灰度失鲜或丢线不再请求M2停车。最后半圆内命中发车相对yaw
  `±0.5°`或真实跨0°仍立即锁停，`±230 mm/s`组合半差和每轮`±600 mm/s`限幅不变。
  M3/M4/M5/M6保持原控制和灰度失鲜停车行为。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、
  `Task/task_bringup_chassis.h`、`Task/README.md`和本记录；未修改公共API、Mode、Device、
  BSP、SysConfig、引脚、Task周期或通信。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查和受影响
  固件Debug/Release完整链接；G3519 Bring-up Debug/Release FLASH为`83080/78760 B`，
  SRAM_BANK0为`19256/19240 B`。这是构建验证，未烧录、未做实车验证。
- 已知限制与待确认：20%灰度辅助比例、直线角度PID强度、弯道INS轨迹以及第二半圆直接
  锁停位置均需实车确认。

## 2026-07-30：M2第二半圆改为连续圆弧并按一圈里程停车

- 修改原因：第二半圆末端前视点固定在A后可能落到车后并触发回头转向；用户要求先把
  第二半圆当作普通循迹圆，并明确纠正为“根据里程计，跑完一圈就停车”。
- 最终决策：仅M2删除最后半圆的`0°±0.5°`及真实跨零直接锁停，纵向目标改为左右编码器
  平均里程一圈`6141.6 mm`。D→A保持与B→C相同的300 mm/s圆弧速度上限，最终减速只由
  一圈里程目标的既有制动包络产生。几何前视路线额外保留200 mm并跨过A接入AB，避免
  停车前把目标钳在A；这不增加纵向行驶目标。终点横杠仍不触发M2停车。
- 否决方案：先提出的“A后继续320 mm再停车”被用户明确否决，M2不采用该行为。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/README.md`、`config/core_config.h`和
  本记录；未修改Mode/Device公共API、Task周期、BSP、SysConfig、引脚或通信。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查和受影响
  固件Debug/Release完整链接；G3519 Bring-up Debug/Release FLASH为`82808/78536 B`，
  SRAM_BANK0为`19256/19240 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug`82808 B`，从主Flash
  `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `77C48426594E06DA1DFECB809B7F51DFB86CAE33511E670DA07EF721D3B84BDC`，随后执行
  `reset run`。
- 已知限制与待确认：回读只证明镜像完整部署；平均轮里程仍受轮周误差和打滑影响，
  `6141.6 mm`规划目标的实际停车位置及第二半圆轨迹需实车确认。

## 2026-07-30：提高M2灰度辅助并修正圆弧差速

- 修改原因：最新VOFA中两次D→A半圆实际只转约`169.6°/170.7°`，等效半径约
  `530.5/527.1 mm`，持续大于R500；同时CD段灰度绝对误差均值约`0.367`，约76%的样本
  不低于`0.3`，原20%灰度辅助不足以消除持续横向偏差。
- 最终决策：仅M2把灰度PID辅助比例从20%提高到30%；BC/DA使用INS几何前视时，只把
  导航左右目标的半差放大`1.06`倍，并保持导航平均轮速不变。真实R500路线几何、
  `6141.6 mm`一圈里程目标、300 mm/s圆弧速度、`±230 mm/s`组合半差和每轮
  `±600 mm/s`限幅均不改变。AB/CD角度PID与其他比赛模式不使用该圆弧倍率。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`和
  本记录；未修改Mode/Device公共API、Task周期、BSP、SysConfig、引脚或通信。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查和
  受影响固件Debug/Release完整链接；G3519 Bring-up Debug/Release FLASH为
  `82888/78632 B`，SRAM_BANK0为`19256/19240 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug`82888 B`，从主Flash
  `0x00000000`按相同长度完整回读，源BIN与回读SHA-256均为
  `10D460C60D57087633C9132706A898F63C92C28745FF4074CCA01EB3DEE64F87`，随后执行
  `reset run`。
- 已知限制与待确认：回读只证明镜像完整部署；`1.06`来自两次实测的近似反算，灰度
  提高后与惯导在第二半圆末端可能互相抵消，须用新VOFA复测两个半圆转角、灰度误差和
  最终航向。

## 2026-07-30：M2改为全程灰度循迹并保留一圈里程停车

- 修改原因：将灰度阵列前置230 mm加入INS几何并增强圆弧角速度/轮速环后，用户实测
  误差明显恶化，明确否决继续混合惯导，要求M2改为完全循迹。
- 最终决策：仅M2全程使用亚博灰度循迹的完整差速输出，不再叠加INS路线前视或AB/CD
  绝对yaw PID；ICM继续采集供显示和VOFA，但不作为M2发车门槛。左右编码器平均里程
  仍开启并以`6141.6 mm`作为正常停车目标；终点横杠仍不触发M2停车。灰度失鲜或丢线
  超过既有250 ms缓冲后请求规划停车，M2半差`±230 mm/s`、单轮`±600 mm/s`不变。
  上一版230 mm INS跟踪点偏置、M2专用角速度环和增强轮速PID方案已被本版撤销。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、
  `Task/task_bringup_chassis.h`、`Task/README.md`和本记录；未修改公共API、Task周期、
  BSP、SysConfig、引脚、通信或其他比赛模式。
- 验证与部署：按用户要求直接准备烧录，只执行必需的G3519 Bring-up Debug完整链接，
  FLASH/SRAM_BANK0为`82488/19256 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug `82488 B`；从主Flash
  `0x00000000`完整回读，源BIN与回读SHA-256均为
  `71D97D942B1317E338978E0DAC2E9ED2C3750831CE5275EB69D29CF766C8DB9E`，随后执行
  `reset run`。未运行Auto/Release/host测试。
- 已知限制与待确认：回读只证明镜像完整部署；纯灰度循迹的直线稳定性、两个半圆轨迹、
  丢线停车和`6141.6 mm`最终停点仍需本次实车确认。

## 2026-07-30：M2识别终点黑标后继续循迹230 mm停车

- 修改原因：最新纯灰度VOFA显示M2按理论一圈里程在`6106 mm`停稳，但灰度在
  `5726～5755 mm`出现唯一的八路共同抬高脉冲，峰值位于`5745 mm`；车辆越过该物理
  标记后继续约361 mm，说明`6141.6 mm`不适合作为当前轮轴路径的主要停点。
- 最终决策：M2仍全程纯灰度循迹；在`5800±300 mm`末段窗口先见普通帧后，候选必须
  同时满足至少6/8路原始ADC不低于900、八路平均值不低于1700，并连续两个新帧成立。
  确认后把纵向目标改为标记里程加`230 mm`，该距离按用户要求定义在
  `task_bringup_chassis.c`顶部宏；减速期间继续灰度循迹。原`6141.6 mm`目标只作漏检
  兜底，其他模式原有标记后距离不变。
- 防误识别证据：离线回放5872帧VOFA，最新运行只在`5731 mm`满足连续确认；另一趟在
  `6019 mm`确认，两趟无强共同抬高的运行不触发。最新运行搜索窗内普通单线八路均值
  低于1700，不能仅靠原6/8路`>=900`误判。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`、`config/core_config.h`、`docs/DEVICE_PARAMETERS.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚或通信。
- 验证与部署：`git diff --check`通过；最新/历史VOFA离线判据回放通过；G3519 Bring-up
  Debug完整链接通过，FLASH/SRAM_BANK0为`82504/19256 B`。未运行Auto、Release或host
  测试。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug `82504 B`；从主Flash
  `0x00000000`完整回读，源BIN与回读SHA-256均为
  `955BC86396E1915561F38ED089B907C847B31ADFB084CF4E779C64DF3A595D12`，随后执行
  `reset run`。
- 已知限制与待确认：1700平均阈值和230 mm后续距离来自当前VOFA与安装尺寸，仍需新一趟
  实车确认标记只触发一次、CH7在标记后跳到约230 mm，并测量最终相对A的有符号偏差。

## 2026-07-30：M2终点标记后纯灰度循迹距离调整为180 mm

- 修改原因：最新9611帧VOFA中，最近两趟均在约5724～5728 mm确认终点横杠并更新目标；
  230 mm配置下，确认后实走约221 mm和242 mm，用户要求将后续距离缩短至180 mm。
- 最终决策：只把`task_bringup_chassis.c`顶部M2标记后距离宏改为`180.0F`。全程纯灰度
  循迹、`5800±300 mm`搜索窗、先见普通帧布防、6/8路ADC不低于900、八路平均不低于
  1700、连续两个新帧确认，以及`6141.6 mm`漏检兜底均保持不变。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`、`docs/DEVICE_PARAMETERS.md`和本记录；未修改公共API、Task周期、BSP、
  SysConfig、引脚、通信、循迹算法或防误识别阈值。
- 验证与部署：最新VOFA离线检查确认最近两趟触发帧分别为8/8路过阈值、八路平均2044
  和1896；G3519 Bring-up Debug完整链接通过，FLASH/SRAM_BANK0为`82504/19256 B`。
  显式选择DAPLink序列号`000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入
  Bring-up Debug `82504 B`；从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `DFCC5D513EED8C66EA37A45344914F6D05B68145420B2BACE9BB01676E10405B`，随后执行
  `reset run`。未运行Auto、Release或host测试。
- 已知限制与待确认：横杠处约300 mm/s且规划减速度185 mm/s²，理论制动距离约243 mm；
  将目标改为180 mm不保证实车只前进180 mm，最终停车位置仍须实测。

## 2026-07-30：M2终点窗口前降至220 mm/s并保留完整循迹差速

- 修改原因：最新12853帧VOFA中，180 mm固件的最近两趟均在约5724 mm确认横杠，目标
  均更新到约5901 mm，但确认时纵向参考速度仍为298 mm/s，CH8制动触发距离为278 mm、
  CH7只剩177 mm；最终分别在5940 mm和5961 mm停稳，标记后实走216 mm和237 mm。
- 最终决策：仅M2在D→A末段按既有185 mm/s²线性包络，从300 mm/s降至220 mm/s，并
  在`5800±300 mm`搜索窗下沿前达到该速度。220 mm/s对应当前含35 mm保护距离的制动
  包络约166 mm，小于180 mm标记后目标。灰度循迹仍以100%比例叠加，M2半差
  `±230 mm/s`、左右差`460 mm/s`及单轮`±600 mm/s`限幅均不变；其他模式仍使用
  100 mm/s终点接近速度。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、
  `Task/task_bringup_chassis.h`、`Task/README.md`、`docs/DEVICE_PARAMETERS.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚、通信、标记判据或停车后续距离。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查及映射
  到的固件Debug/Release完整链接；G3519 Bring-up Debug/Release FLASH为
  `82552/78312 B`，SRAM_BANK0为`19256/19240 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `82552 B`；
  从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `CD2898FBA729ACBB0DF6E2DC64D97E573113B8CBFCCF7695F73261723738B22B`，随后执行
  `reset run`。尚未做实车验证。
- 已知限制与待确认：166 mm为当前规划器计算值，不是实车刹停测量；须在烧录后用新VOFA
  确认横杠处CH5已降至约220 mm/s、CH8小于CH7，并测量最终相对A的有符号偏差。

## 2026-07-30：M2提速、降低循迹Kp并在停车段回正车头

- 修改原因：220 mm/s终点接近版的最新两趟VOFA在横杠处CH5均为220 mm/s、CH8约
  165 mm且CH7约175～177 mm，最终标记后实走141～143 mm；停车距离明显改善，但停稳
  航向仍相对发车方向偏`+5.94°/+7.85°`，运行约21.74～23.36 s，且循迹轻微抖动。
- Git基线：变更前完整工作树已提交为`e0ea17d`（“完成车载联调与任务2终点停车优化”）
  并推送到`origin/car`，可作为220 mm/s纯灰度停车版本回退点。
- 最终决策：直道/普通半圆速度上限由420/300提高到500/380 mm/s，终点窗口速度仍为
  220 mm/s，目标运行时间约18 s；产品Task循迹Kp由0.70降至0.65。M2在终点横杠锁存后
  的制动段才把发车yaw角度PID叠加到灰度差速，IMU无有效标定时保持灰度单独控制。
  M2组合差速仍限制在半差`±230 mm/s`；单轮接近`±600 mm/s`限幅时优先压低共同均速，
  保留已请求的左右差速，不让单轮钳位削弱转向。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、
  `Task/task_bringup_chassis.h`、`Task/README.md`、`docs/DEVICE_PARAMETERS.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚、通信、标记判据或180 mm后续距离。
- 验证与部署：按用户要求快速部署，`git diff --check`通过，G3519 Bring-up Debug完整链接
  通过，FLASH/SRAM_BANK0为`82696/19256 B`；未运行Auto、Release或host测试。显式选择
  DAPLink序列号`000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Debug
  `82696 B`，从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `E4F7034B3E63CD2F87D784DBB973EBE3D35F29D9B8AB93601A56ECB44AC9FD3D`，随后执行
  `reset run`。
- 已知限制与待确认：18 s为基于上一版分段耗时的调参目标，不能由源码静态保证；须用新
  VOFA核对总时长、横杠后CH3/CH4差速、最终相对发车yaw及高速下是否仍有循迹抖动。

## 2026-07-30：M4 A→B采用专用平滑加减速规划

- 修改原因：任务4要求小车在8 s内通过1500 mm处的B点；用户要求起步及全程速度尽量
  平缓，不允许目标速度突变，并准确限制纵向加速度。
- 最终决策：M4沿AB只用发车yaw角度PID保持直行，不叠加灰度或路径前视转向。纵向采用
  现有线性梯形规划器，巡航速度上限为`320 mm/s`，加速和减速均限制为
  `120 mm/s²`；10 ms完整控制周期内目标速度仅变化`1.2 mm/s`。停车目标设为B后
  `50 mm`，扣除共享`35 mm`规划保护距离后理论停点约在B后15 mm，确保通过B；理想
  运动学约6.9 s通过B。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、
  `Task/task_bringup_chassis.h`、`Task/README.md`、`docs/DEVICE_PARAMETERS.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚、通信或M2行为。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查及
  映射到的G3519 Bring-up Debug/Release完整链接；Debug/Release的FLASH为
  `82808/78568 B`，SRAM_BANK0为`19256/19240 B`。现有线性规划器host测试覆盖线性起步、
  加速度输出、巡航目标切换连续性及目标制动停车。用户授权后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `82808 B`；
  从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `D25146F297871237DFD2D1793F9F94D6F955A141AF35D955A3E1542DE5E9CE6A`，随后执行
  `reset run`。
- 已知限制与待确认：6.9 s是理想运动学估算；烧录和完整回读不等于实车控制验证，实车
  通过B的时间、VOFA速度斜率、轮速跟随误差和最终停点仍待确认。

## 2026-07-30：M5/M6采用30 s整圈平滑纯灰度循迹

- 修改原因：题面第4项只要求8 s内通过B，第5/6项要求30 s内循迹一圈并通过A；用户要求
  在完成M4的AB加速度规划时一并落实后续30 s循迹，并继续遵守限时内尽量慢、起步平滑、
  目标速度不突变的原则。
- 最终决策：M4行为不变。M5/M6只使用亚博八路灰度输出转向，INS路径前视仍更新供观察但
  不叠加到轮速。两模式全圈采用恒定`250 mm/s`纵向上限，避免在AB/BC/CD/DA切点切换
  速度；加速和减速均限制为`120 mm/s²`，10 ms完整控制周期内纵向参考速度最多变化
  `1.2 mm/s`。理想运动学约25.6 s通过一圈后的A，保留约4.4 s题面余量；通过A后仍按
  既有320 mm目标规划停车。含35 mm保护距离的250 mm/s制动包络约295.4 mm，小于
  A后320 mm余量。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、
  `Task/task_bringup_chassis.h`、`Task/README.md`、`docs/DEVICE_PARAMETERS.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚、通信、M2或M4行为。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查及
  映射到的固件完整链接；G3519 Bring-up Debug/Release的FLASH为`82840/78584 B`，
  SRAM_BANK0为`19256/19240 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `82840 B`；
  从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `B3C1C512655E69F98EEE3D6E3A4AC806AFAF79057188B474FC1876B30E27E545`，随后执行
  `reset run`。
- 已知限制与待确认：25.6 s和295.4 mm均为理想规划值，不含实车轮速跟随误差、循迹
  差速和打滑；烧录和完整回读不等于实车控制验证，M5/M6的实车整圈时间、灰度循迹
  稳定性、球位误差及A后停车过程仍需VOFA和场地测试确认。

## 2026-07-30：按VOFA修正M5/M6画龙、圆弧差速和A点停车

- 修改原因：最新两次M5/M6实测分别在25.38/25.44 s、里程5682/5661 mm确认终点横杠，
  最终只继续280/283 mm并在5962/5944 mm停车。CD直道左右目标全差平均绝对值达到
  110～163 mm/s、范围约`-423～+313 mm/s`，20 ms内全差最大跳变294 mm/s；第一/第二
  半圆目标全差均值约`-95/-111 mm/s`，实测仅`-77/-85 mm/s`，按200 mm轮距折算有效
  半径约619/561 mm。横杠确认时相对发车yaw约`-340°`，最终约`-358～-360°`，与前置
  230 mm探头提前看到A点一致，不是横杠误识别或最终航向未回正。
- 最终决策：仅M5/M6巡航速度由250降至`234 mm/s`，加减速度仍为`120 mm/s²`，理想
  整圈约27.2 s。两模式循迹Kd由0.03降至`0.01`；灰度修正重构为保持均速的左右半差，
  限制为`±70 mm/s`并按`300 mm/s²`斜率变化，即10 ms每轮修正最多变化3 mm/s、左右
  全差最多变化6 mm/s。横杠确认后的目标余量由320改为`265 mm`：234 mm/s制动距离约
  228.2 mm，加35 mm保护包络约263.2 mm，计划先前进约1.8 mm再制动，使轮轴累计前进
  约230 mm至A点；漏检兜底仍为一圈加320 mm。成绩时间改为横杠里程加230 mm时锁存，
  不再把前置探头看到横杠的时刻当作轮轴通过A。M2/M4行为不变。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、
  `Task/task_bringup_chassis.h`、`Task/README.md`、`docs/DEVICE_PARAMETERS.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚或通信。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查及映射到的固件
  完整链接；G3519 Bring-up Debug/Release的FLASH为`83160/78856 B`，SRAM_BANK0为
  `19256/19248 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `83160 B`；
  从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `F3C1B524F2A5579F5953731490AF19C1DE8E8D75A64593C2F3603E1CA35426DF`，随后执行
  `reset run`。
- 已知限制与待确认：差速跟随率和停车位置来自两次实测的首轮闭环，差速斜率限制可能使
  半圆入口建立转向偏慢；须用新VOFA确认CH3/CH4全差斜率、半圆CH1/CH2实际差速、横杠后
  实走距离、轮轴相对A位置和最终显示时间均不超过30 s，再决定是否放宽300 mm/s²斜率。

## 2026-07-31：撤销M5/M6过慢差速斜率

- 修改原因：用户实车确认300 mm/s²半差斜率版本画龙更严重；当前`vofa+.csv`仍停留在
  2026-07-30 23:47，未包含该次试跑。按R500所需约47 mm/s半差计算，300 mm/s²需要
  约157 ms建立转向，234 mm/s下车辆已前进约37 mm，新增相位滞后与实车现象一致。
- 最终决策：M5/M6循迹Kd恢复为`0.03`，半差斜率由300提高到`3000 mm/s²`，使47 mm/s
  半差约16 ms建立；半差`±70 mm/s`上限、234 mm/s巡航、120 mm/s²纵向加减速、230 mm
  探头偏置和265 mm标记后目标均不变。M2/M4不变。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.h`、`Task/README.md`、
  `docs/DEVICE_PARAMETERS.md`和本记录；未修改公共API、Task周期、BSP、SysConfig、引脚或
  通信。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查及映射到的固件
  完整链接；G3519 Bring-up Debug/Release的FLASH为`83160/78856 B`，SRAM_BANK0为
  `19256/19248 B`。首次烧录尝试因DAPLink未连接而未发生目标写入；重新连接后显式选择
  DAPLink序列号`000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug
  `83160 B`。从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `2E5CB039528FAF39B3064C277E84F9EF98421F29A5F73277C3D82E3148CA1EEA`，随后执行
  `reset run`。
- 已知限制与待确认：烧录和完整回读不等于实车控制验证；须用新VOFA比较直道全差绝对
  均值、半圆差速建立时间、实际半径和画龙幅度。

## 2026-07-31：依据VOFA提前降低M5/M6终点速度

- 修改原因：最新两次有效运行均在约`5716/5728 mm`确认终点横杠，确认时CH5仍为
  `232/233 mm/s`，参考速度归零里程约`5993/6001 mm`。扣除230 mm探头偏置后，轮轴约
  多越过A点`52/46 mm`；按确认速度和实际推进距离折算，实车有效减速度约
  `97～100 mm/s²`，低于旧规划值`120 mm/s²`。横杠识别和循迹正常，晚停来自终点速度与
  实车制动能力不匹配。
- 最终决策：仅M5/M6保持前段`234 mm/s`和原灰度循迹参数，在终点横杠窗口下沿前以
  `100 mm/s²`线性降至`210 mm/s`。横杠后的265 mm目标不变：210 mm/s按100 mm/s²的
  制动距离为220.5 mm，加35 mm规划保护后触发距离为255.5 mm，因此横杠后约9.5 mm开始
  制动，规划轮轴推进量为230 mm。M2/M4、循迹Kp/Kd、半差上限和差速斜率均未改变。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚或通信。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查和映射
  到的完整链接；G3519 Bring-up Debug/Release的FLASH为`83192/78872 B`，SRAM_BANK0为
  `19256/19248 B`。通用烧录脚本最初误选基础G3519 Debug镜像，发现镜像长度异常后立即
  使用显式DAPLink序列号`000100011a5a5bb60000000000045c0aa5a5a5a597969908`覆盖写入正确
  Bring-up Debug `83192 B`；从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `6500D73C09FA34AED462F7D05AAA09F54C9AA4E9FD711A09B33501EDE851B549`，随后执行`reset run`。
- 已知限制与待确认：烧录和回读只证明正确镜像已部署；210 mm/s与100 mm/s²来自最近两次
  VOFA的离线标定，仍须用下一次实车VOFA确认横杠处CH5、最终里程及相对A点的有符号偏差。

## 2026-07-31：M5/M6终点接近速度降至110 mm/s

- 修改原因：210 mm/s版本的最新三次VOFA均正确执行新参数，但横杠后仍运行
  `275～277 mm`才停，轮轴相对230 mm探头偏置继续晚`45～47 mm`。到达横杠后约230 mm
  的A点时，CH5参考速度仍为`73～74 mm/s`，左右轮实测均速仍为`99～108 mm/s`；横杠
  识别和循迹差速正常，210 mm/s方案被实测否决。
- 最终决策：仅将M5/M6终点窗口下沿的接近速度从`210`降至`110 mm/s`，前段巡航
  `234 mm/s`、末段减速度`100 mm/s²`、横杠后目标265 mm、循迹Kp/Kd、半差上限和斜率
  全部保持不变。按当前VOFA的速度跟随误差估算，最终晚停约15 mm，总时间约29.5 s。
- 涉及文件：`config/core_config.h`、`Task/README.md`和本记录；未修改公共API、Task周期、
  BSP、SysConfig、引脚、通信、M2或M4行为。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查和
  映射到的完整链接；G3519 Bring-up Debug/Release的FLASH为`83192/78872 B`，
  SRAM_BANK0为`19256/19248 B`。当前Bring-up Debug BIN的SHA-256为
  `573FF1B7D7845FB6352938231FD3EF819BDE4B1E37E0071E3FC98BE3F3A7AE84`。
- 部署与待确认：用户随后明确要求烧录；使用显式DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `83192 B`。
  从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `573FF1B7D7845FB6352938231FD3EF819BDE4B1E37E0071E3FC98BE3F3A7AE84`，随后执行
  `reset run`。110 mm/s的停车偏差和约29.5 s总时间仍为离线估算，须用新VOFA和实际A点
  位置确认。

## 2026-07-31：M5/M6改用任务2的横杠后180 mm停车距离

- 修改原因：110 mm/s版本最新两次有效运行均在约`5714 mm`确认终点横杠，最终停在
  `5958/5959 mm`，横杠后实际继续`244/245 mm`，总时间为`29.44/29.48 s`。用户确认
  停车位置仍不正确，并明确否决M5/M6的265 mm目标，要求与任务2一致。
- 最终决策：仅把M5/M6横杠后的固定停车目标由`265 mm`改为`180 mm`，确认横杠后仍继续
  纯灰度循迹并由原线性规划器停车。移除“停车距离必须大于230 mm传感器偏置”的初始化
  约束，改为要求距离大于0；原制动包络检查仍保留，110 mm/s、100 mm/s²和35 mm保护
  对应`95.5 mm`，小于180 mm。230 mm安装偏置只继续用于成绩时间锁存；234 mm/s巡航、
  110 mm/s接近速度、100 mm/s²末段减速度、循迹参数、一圈+320 mm漏检兜底、M2和M4均
  不变。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`和本记录；
  未修改公共API、Task周期、BSP、SysConfig、引脚或通信。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查和映射
  到的完整链接；G3519 Bring-up Debug/Release的FLASH为`83192/78896 B`，SRAM_BANK0为
  `19256/19248 B`。显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `83192 B`；
  从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `8E769D8DAE2FA64AE757925259D7BC3AD190259850A1B30974A216FC39F17CFD`，随后执行
  `reset run`。
- 已知限制与待确认：烧录和完整回读不等于赛道停车位置验证；180 mm为用户指定的下一轮
  实测参数，须用新VOFA确认横杠锁存里程、横杠后实走距离和相对A点的有符号停车偏差。

## 2026-07-31：M5/M6末段叠加发车航向PID回正车头

- 修改原因：180 mm版本最新两次有效运行分别在`5718/5695 mm`确认横杠，停在
  `5882/5842 mm`，用户确认停车位置“好多了”，只要求车头回正。两次停车yaw相对各自
  发车yaw仍偏`+6.05°/+12.13°`，说明纯灰度末段没有完全恢复发车方向。
- 最终决策：M5/M6横杠前行为不变，继续只使用既有灰度循迹；横杠确认后继续保留灰度
  差速，同时复用现有角度PID，以本次发车ICM yaw为目标生成等幅反向的左右轮速修正。
  航向修正保持左右均值为零，不修改180 mm纵向停车目标、速度规划、循迹参数、M2或M4。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/task_bringup_chassis.h`、
  `Task/README.md`、`config/core_config.h`和本记录；未修改公共API、Task周期、BSP、
  SysConfig、引脚或通信。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查和
  映射到的完整链接；G3519 Bring-up Debug/Release的FLASH为`83336/78976 B`，
  SRAM_BANK0为`19256/19248 B`。当前Bring-up Debug BIN的SHA-256为
  `7A85D13F0E9CFCB3E2572B89057EA09554A39B429E7C09702FA8427818A095F4`。用户随后明确要求
  烧录；显式选择DAPLink序列号`000100011a5a5bb60000000000045c0aa5a5a5a597969908`
  写入Bring-up Debug `83336 B`，从主Flash `0x00000000`完整回读，源BIN与回读SHA-256
  均为上述值，随后执行`reset run`。
- 已知限制与待确认：左右修正的数学均值为零不等于实车停点绝对不变；轮速跟随误差和
  轮胎打滑仍可能影响末段里程，须用新VOFA确认最终相对航向、横杠后实走距离及是否出现
  低速过度纠偏。

## 2026-07-31：G3519增加独立滚球目标与停止命令

- 修改原因：用户批准由MCP经G3519向F407设置任意球位目标和停止，同时要求不新增Task、
  不改变M3按键语义、底盘模式或现有限幅。
- 最终决策：新增`BTG=2,<target_0p1mm>`和`BST=2`；协议目标域为`±1250`，G3519产品/调试
  入口限制为`±1200`。BST语义固定为F407执行`0 A -> Disable`。两类命令分别使用独立
  16位序号，无ACK。现有10 ms Bring-up Task在PeerLink Step后消费单个32位原子调试邮箱；
  高16位为非零请求号，低16位为有符号目标或`INT16_MIN`停止码。BTG不隐式启动闭环。
- 涉及文件：`Device/Communication/PeerLink/`、`Task/task_bringup_chassis.[ch]`、
  `config/core_config.h`、PeerLink/Task README、PeerLink主机测试和本记录；未修改Task周期、
  M3按键、底盘模式、BSP、SysConfig、引脚或底盘电机限幅。
- 验证方式和结果：全套host测试通过；PeerLink新增测试覆盖BTG正负协议边界、BST、独立序号、
  BUSY不推进序号和越界拒绝。67个ARM源文件严格编译通过；G3519 Bring-up Debug/Release
  完整链接通过，FLASH为`84120/79656 B`，SRAM_BANK0为`19312/19296 B`。
- 已知限制：QUEUED只证明G3519本地UART入队，不是F407执行ACK；连续3次BUSY后停止尝试并
  显式报告`BUSY_EXHAUSTED`，必须换请求号重提。本轮未烧录、未连接硬件、未验证双端BTG/BST
  抓帧或球位闭环；F407仍须先由其本地KEY启动且视觉有效才接受BTG。
- 待确认事项：双端部署后先在中心附近验证目标符号、范围拒绝、STOP失能和无刷既有限幅，
  再逐步扩大到`±120.0 mm`端部安全余量。

## 2026-07-31：赛题mode同步至F407与视觉

- 修改原因：用户要求把G3519当前选择的赛题mode同步给另一块F407和Maix视觉程序。
- 最终决策：在现有`CAR`状态尾部追加`competition_mode`，直接取既有
  `g_competition_mode`，只允许`2..6`。mode独立于CAR物理量`valid`，供F407和视觉侧
  显示、诊断，不触发滚球控制、产品模式切换或其他启动行为；未新增Task、ACK、重发或
  状态机。
- 涉及文件：`Device/Communication/PeerLink/device_peer_link.[ch]`、PeerLink README、
  `Task/task_bringup_chassis.c`、Task README、PeerLink主机测试和本记录；未修改BSP、
  SysConfig、引脚或Task周期。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过；PeerLink主机测试和
  ARM严格编译通过，G3519 Bring-up Debug/Release完整链接通过，BIN分别为
  `83600/79488 B`。用户明确要求烧录后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `83600 B`；
  从主Flash `0x00000000`按镜像长度完整回读，源BIN与回读SHA-256均为
  `E9736C8CB3D455C68906947CF9E228AD77D944C4868660D8B383B2C2BE12BD55`，随后执行
  `reset run`。
- 已知限制与待确认：本轮只烧录并回读校验了G3519；F407与视觉端未烧录/部署，双端UART
  `CAR`实收、F407的1500 ms新鲜度处理以及`VST`到Maix的mode显示仍需联机实物验证。

## 2026-07-31：mode同步实物分段验证（G3519侧未连通）

- 修改原因：用户要求立即验证G3519→F407→Maix的mode实物链路。
- 最终状态：F407与Maix已部署当前版本，但F407运行约4分钟后PeerLink仍为
  `heartbeat_rx_count=0`、`car_state_rx_count=0`；因此Maix从真实VST收到`mode=0`。
  G3519当前未向F407提供可用串口帧。
- 验证方式和结果：G3519工程Doctor按本地固定配置检查指定DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`，明确报告其父设备
  `VID_0D28&PID_0204`不在场。Windows仅有另一个`VID_0D28&PID_4001/COM5`，以
  115200 8N1独占采样2.5 s为0字节。本轮未重新烧录或复位G3519。
- 已知限制与待确认：上一条记录只证明当时G3519镜像写入及回读正确；本次缺少目标DAPLink、
  G3519运行串口和八字段CAR实收，不能把完整链路写成通过。须重新连接指定DAPLink/目标板电源，
  并确认G3519 TX、F407 PA10 RX与共地后继续验收。

## 2026-07-31：停用JY61P初始化并部署G3519

- 修改原因：用户明确要求取消JY61P初始化并立即烧录。
- 最终决策：Bring-up初始化路径不再初始化JY61P专用`Device_Serial`，也不调用
  `Device_JY61P_Init`；`g_pendulum_imu_ready`保持默认`false`，现有串口消费、欧拉角解析、
  杆角采集和VOFA角度输出通过原安全门跳过。JY61P源码、构建选项、UART0 SysConfig资源和
  后续恢复入口均保留，未修改底盘控制、Task周期、BSP、SysConfig或引脚。
- 涉及文件：`Task/task_bringup_chassis.c`、`Task/README.md`和本记录。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构和差异检查，组件结果命中
  现有验证缓存；显式构建`g3519-bringup-debug`确认当前产物已是最新。使用DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug `82088 B`，从
  主Flash `0x00000000`按镜像长度完整回读，源BIN与回读SHA-256均为
  `D1928D5490AB35D618AD2452B7056DDE58389F57F45CB635A4F1D3B3F7588B87`，随后执行
  `reset run`。
- 已知限制与待确认：烧录和回读只证明镜像正确部署；未读取运行期变量或观察JY61P物理串口，
  不表述为实物功能验证。恢复JY61P时须同时恢复串口与Device初始化。

## 2026-07-31：M4/M5/M6纵向加速度平滑与轮速加速度环

- 修改原因：用户要求缓和滚球赛题的起步、停车和弯道纵向加速度，以便F407使用现有
  ICM加速度前馈补偿，同时明确要求保留M4与M5/M6停车位置。
- 最终决策：线性速度规划器增加独立加减速限跃度，并由左右轮平均实测速度差分、80 ms
  一阶低通形成纵向加速度反馈，M4/M5/M6启用`0.15`比例修正；制动包络使用分段S曲线精确
  计算，并包含正加速度回落到零的限跃度过渡。M4保持320 mm/s巡航和B后50 mm目标，使用
  对称100 mm/s²及400 mm/s³；M5/M6保持234 mm/s巡航、110 mm/s末段速度和横杠后180 mm目标，使用对称
  80 mm/s²及320 mm/s³，直道和圆弧切点不改变纵向速度。固定32列VOFA恢复为唯一输出，
  CH12改为保留两位小数的滤波实测纵向加速度；CH13改为闭环相对相同限跃度开环的实际
  加速度修正量，负值表示闭环压低参考加速度，CH6减CH12仍可得到加速度误差。
- 涉及文件：`Mode/Car/Control/mode_car_speed_profile.c`、
  `Mode/Car/include/mode_car_control.h`、`Mode/Car/README.md`、`Task/task_bringup_chassis.[ch]`、
  `Task/README.md`、`config/core_config.h`、SpeedProfile主机测试和本记录；未修改Task周期、
  优先级、BSP、SysConfig、引脚或JY61P停用状态。
- 验证方式和结果：SpeedProfile主机测试覆盖加速度限跃度、轮速加速度反馈、高实测轮速
  不延后制动、实际闭环修正量、M4在8 s内过B、M5/M6长直道零计划加速度及两类停车目标保护带。
  `tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查、Navigation/SpeedProfile
  主机测试、ARM严格编译及映射到的完整链接。G3519 Bring-up Debug/Release完整链接通过，
  当前FLASH分别为85064/80648 B，SRAM_BANK0分别为19280/19264 B。
- 烧录结果：用户明确允许烧录后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug 85064 B；从主Flash
  `0x00000000`完整回读85064 B，源BIN与回读SHA-256均为
  `C96383E2E24803E94F9AD1279E254DBD89F232A471779C5FC5F0686BFBD082EF`。回读后曾按先前要求保持
  `reset halt`；用户随后明确要求烧录完成后重启，已使用同一探针执行`reset run`。
- 已知限制与待确认：固件已经启动，但尚未取得新版VOFA实测曲线。共享35 mm保护距离会让理想无惯性
  仿真的参考速度在目标前约35 mm归零，实际停车依赖底盘惯性补足该保护距离；M4的B后50 mm
  和M5/M6横杠后180 mm仍须在实车上核对，尤其要观察轮胎打滑、速度内环滞后及停车点误差。
  当前`.vscode/launch.json`的G3519配置仍指向不含该产品Task的普通`MSPM0G3519`目标，并会在
  launch时烧录和运行；本轮未执行该配置，实际只构建了`MSPM0G3519-Bringup` Debug/Release。

## 2026-07-31：M4固定起步规划结束后切换F407本地BMI088前馈

- 修改原因：最新两次M4 VOFA中，发车固定段CH22=`100 mm/s²`、CH23=`-117 mdeg`正常，
  但3450 ms后的减速段CH6约为`-95～-99 mm/s²`时CH22/CH23始终为0。根因是G3519继续发送
  `plan_valid=1, acceleration=0`，压住F407已有的本地BMI088前馈，不是BMI088增益不足。修正后
  新VOFA确认运行3450 ms后BMI088已接管，但停车约40 ms后CH22从`-343 mm/s²`突变为0，进一步
  定位到停车态重新发送的有效零规划仍会过早屏蔽停车残余冲击。
- 最终决策：否决M4全程`plan_valid=1`。M4运行中仅在`elapsed_ms<=3450`时发送有效规划：
  0～600 ms为0，600～850 ms按`400 mm/s³`升至`100 mm/s²`，保持至3200 ms，再于3450 ms前
  按相同jerk降至0。旧“未运行或停车时发送有效零规划抑制静止噪声”方向也已否决；发车前、
  3450 ms后及停车后全部恢复八字段CAR，由F407按现有`0.20`增益使用本地BMI088。M2/M3/M5/M6
  不变。
- 涉及文件：`Task/task_bringup_chassis.c`、`config/core_config.h`、PeerLink/Task说明和本记录；
  未修改F407代码、Task周期、BSP、SysConfig、引脚、按键语义或电机保护。
- 验证与部署：`tools/project.ps1 -Action Verify -Scope Auto`通过架构/差异检查、ARM严格编译和
  Bring-up Debug/Release完整链接；FLASH分别为`85440/81032 B`，SRAM_BANK0分别为
  `19280/19272 B`。使用指定DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入正确Bring-up Debug `85440 B`；从
  主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `9BE753A2D7532E700994A62E001CAC2E5DB503BFC6DFABC77C55F90B02E01263`，随后执行`reset run`。
  未按KEY或发送启动命令，F407代码和固件未修改。
- 已知限制与待确认：烧录和回读只证明正确镜像已部署；须采集新版32列VOFA，确认3450 ms前
  CH22/CH23仍跟随固定规划，3450 ms后及停车后的CH22/CH23持续响应BMI088，并核对停车球位误差
  和M4仍在8 s内通过B。F407未处于加速度补偿phase或BMI088失效时仍会按其现有安全门停止前馈。

## 2026-07-31：M4固定起步后继续下发速度规划器加速度

- 修改原因：最新VOFA确认纯BMI088能够响应停车冲击，但减速段幅值显著小于M4已规划的
  `-99 mm/s²`；F407三状态融合需要3450 ms后的有符号规划量作为预测基准。
- 最终决策：M4的0～3450 ms固定起步规划保持不变；3450 ms后且循迹有效时，CAR规划字段改为
  直接发送同一jerk限速速度规划器的`reference_acceleration_mm_per_second2`，包含减速负值。
  循迹结束或规划器无效时继续发送`plan_valid=0`，使F407回退纯BMI088。未改变十字段CAR格式、
  M4的`100 mm/s²`与`400 mm/s³`参数、Task周期或停车逻辑。
- 涉及文件：`Task/task_bringup_chassis.c`及本记录；未修改BSP、SysConfig、ISR/DMA、引脚、按键
  语义或电机保护。
- 验证与部署：全套Host GCC测试PASS；Bring-up Debug/Release clean-first完整链接通过，FLASH/
  SRAM_BANK0分别为`85488/19280 B`和`81048/19272 B`。使用指定DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入Bring-up Debug BIN 85488 B，从
  `0x00000000`完整回读；源BIN与回读SHA-256均为
  `06154F90BBD2EA7BD3C5344F1A405A357AA3E820920F20871D50A48CBB9162A3`，随后`reset run`。
  未按KEY或发送启动命令。
- 已知限制与待确认：需用新版VOFA确认3450 ms交接无阶跃，减速时CAR规划确实为负值，且F407
  融合后的CH22/CH23连续；M4仍需实测确认在8 s约束内通过B。

## 2026-08-01：M4/M5/M6统一平滑起步与停车，明确排除M2

- 修改原因：用户要求把M4的起步和停车规划同步到需要杆球平衡的移动赛题，随后明确M2不需要
  平衡，不应采用这组缓加减速参数。
- 最终决策：仅M4/M5/M6共享对称`100 mm/s²`加减速度、`400 mm/s³`限跃度和`0.15`
  轮速加速度反馈增益。M2继续使用原通用`180/185 mm/s²`、近阶跃限跃度及零加速度反馈；
  M3保持静车。M4继续使用`320 mm/s`巡航和B后`50 mm`目标；M5/M6继续使用`234 mm/s`
  巡航、`110 mm/s`终点接近速度及横杠后`180 mm`停车目标。路线、距离、Task周期、状态机和
  PeerLink协议未改。
- 涉及文件：`config/core_config.h`、`Task/task_bringup_chassis.c`、`Task/README.md`、
  `docs/DEVICE_PARAMETERS.md`、SpeedProfile主机测试和本记录。
- 验证方式和结果：`tools/project.ps1 -Action Verify -Scope Auto`通过架构检查、差异检查、
  SpeedProfile/Navigation主机测试、ARM严格编译及G3519 Bring-up Debug/Release完整链接；
  主机测试继续断言M4在8 s内通过B，并覆盖M5/M6巡航段零计划纵向加速度、平滑末段减速和
  横杠后`180 mm`目标。用户授权后，显式选择DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入G3519 Bring-up Debug
  `85456 B`；从主Flash `0x00000000`完整回读，源BIN与回读SHA-256均为
  `E6A98C8296D9F6702A605D1667617EAB3F738609ADA293EB640269813AD9E38D`，随后执行
  `reset run`。
- 已知限制与待确认：烧录和回读只证明镜像已正确部署；尚未做实车起停、M4过B时间或
  M5/M6停车位置验证，`0.15`反馈增益仍需结合轮胎打滑、速度内环滞后和新版VOFA曲线确认。

## 2026-08-01：M4/M5/M6统一起步、巡航零加速度与jerk停车收尾

- 修改原因：用户进一步要求M4/M5/M6除起步和结尾外尽量保持车身纵向匀速，并指出停车时
  不能在参考速度到零后立即释放速度闭环；旧终端路径会把约`-72 mm/s²`直接清零。
- 最终决策：M4/M5/M6统一使用对称`100 mm/s²`和`400 mm/s³`。达到巡航后规划加速度
  保持严格为0，轮速差分噪声不再重新打开加速度反馈；停车时速度目标先钳在0，内部负加速度
  继续按jerk回到0后规划器才进入STOPPED。随后左右轮继续以0为目标闭环，均在`10 mm/s`
  内连续100 ms才锁定并结束。三题在规划器运行期间统一发送同一规划加速度，包含起步、巡航
  有效零值和停车尾段；M2不采用该规划，M3保持静车。
- 涉及文件：`Mode/Car/Control/mode_car_speed_profile.c`、`Task/task_bringup_chassis.c`、
  `config/core_config.h`、SpeedProfile测试、PeerLink/Task/Mode说明和本记录。未改Task周期、
  UART协议字段、BSP、ISR/DMA、按键语义或赛题距离目标。
- 验证方式和结果：SpeedProfile测试覆盖巡航噪声不产生非零参考、每10 ms加速度变化不超过
  `4 mm/s²`、终端加速度回零、M4在8 s内过B及M5/M6横杠后180 mm目标；
  `tools/project.ps1 -Action Verify -Scope Auto`通过。Bring-up Debug/Release clean-first完整
  链接，FLASH/SRAM_BANK0分别为`85512/19280 B`和`81120/19272 B`。
- 部署结果：通用Flash任务曾短暂写入不含产品Task的`18856 B`基础镜像，识别后立即纠正；
  最终显式使用`g3519-bringup-debug`和DAPLink序列号
  `000100011a5a5bb60000000000045c0aa5a5a5a597969908`写入`85512 B`产品镜像。
  从主Flash `0x00000000`全长回读，源BIN与回读SHA-256均为
  `8F809F452E5C9C4D981868C89F3648BCC56EAEC6F2F1F182E4065DA46859B448`，随后`reset run`。
- 已知限制与待确认：软件测试和回读不能代替赛道实测。下一轮应确认M4过B时间、M5/M6横杠
  后停车距离、中段CH6规划加速度长期为0、结尾CH6按jerk回零，以及停车后100 ms零速保持期间
  两轮速度确实收敛；本轮未按KEY。

## 2026-08-01：滤波轮速加速度估计、CAR 回传与 VOFA 诊断

- 结论：M4/M5/M6 已使用独立轮速加速度估计器向 F407 提供低噪声观测；原速度规划器的 CH12/加速度反馈输入保持不变。
- 修改原因：现有单步轮速差分在三份 VOFA 匀速段存在明显正偏，不能直接用于杆球阻尼融合。
- 最终决策：左右轮平均速度采用 13 点最小二乘斜率、100 ms 一阶低通和 `±1000 mm/s²` 限幅；完整 CAR 扩展为 13 字段并保留 8/9/10/11 字段兼容。只有 M4/M5/M6 规划有效且估计器预热完成时置 `wheel_accel_valid=1`；异常时间戳、采样间隔超过 30 ms、编码器无效或规划退出均复位。VOFA 追加 CH32 滤波值和 CH33 有效位。
- 涉及文件：`APP/Algorithm/Filter`、`Device/Communication/PeerLink`、`Task/task_bringup_chassis.c`、`config/core_config.h`、CMake/验证脚本、PeerLink/估计器测试与文档；未修改 Task 周期/优先级、规划参数、BSP、SysConfig、ISR/DMA、引脚或动态内存策略。
- 验证：轮速加速度与 PeerLink 主机测试通过；`tools/project.ps1 -Action Verify -Scope Auto` 全部通过；G3519 Bring-up Debug/Release 完整链接通过，BIN 分别为 87456/82872 B。
- 已知限制与待确认：本轮未烧录、未进行双端 UART 或实车动态验证。100 ms 低通与 F407 观测方差需根据新 VOFA 的 CH32/CH33、CH22/CH23 继续校准，不能仅凭构建结果认定阻尼已达到最优。

## 2026-08-01：13 字段融合版本双端部署

- 结论：G3519 Bring-up Debug 与配套 F407 Debug 均已完成烧录、全长回读校验并复位；未按 KEY，也未启动闭环。
- 修改原因：新增轮速加速度字段会被旧 F407 拒绝，用户明确要求将匹配版本同步部署到两块 MCU。
- 最终决策：G3519 锁定 `0D28:0204` DAPLink，最终写入 `87456 B` Bring-up 产品镜像；F407 锁定 `0416:5051` CMSIS-DAP，写入 `52068 B` Debug 镜像。通用 `tools/mspm0.ps1 -Action Flash` 曾按默认映射短暂写入 `18792 B` AT8236 基础镜像，发现 profile 不符后立即显式写回 Bring-up 镜像并以最终全长回读为准。
- 涉及文件：仅更新本记录；未修改源码、参数、协议、Task、BSP、SysConfig、ISR/DMA 或探针本机配置。
- 验证方式和结果：G3519 源 BIN 与 `0x00000000` 起始的 `87456 B` 回读 SHA-256 均为 `91EB1A2F0A2CFCCE10496F84892FF07E4F0BBEDF2B7FB41A5B51EC1B549CA644`；F407 源 BIN 与 `0x08000000` 起始的 `52068 B` 回读 SHA-256 均为 `55C8ED9F1AA3B3DB00A9E0714E7099C2FFC3C63661F4B98722EE3B2C35C8A953`。复位后 COM5 连续输出 34 列 VOFA；停止态 `CH30=0`、`CH31=552`、`CH32=0`、`CH33=0`，新增通道已部署且规划无效时正确清零。
- 已知限制与待确认：当前静态 DBG 的 motor_online/motor_enabled 位未置位，未验证运动时的轮速加速度融合。电机恢复在线后需实测 M4/M5/M6 的 CH32/CH33 连续性、停车段 CH22/CH23 与 M6 任意球位的杆角余量。

## 2026-08-01：M5/M6路线段号与四段融合隔离

- 结论：G3519现按本次发车平均编码器里程发送AB/BC/CD/DA段号；配套F407在每次换段前复位融合历史，四段仍使用原加速度补偿。
- 修改原因：用户要求保留直线补偿，同时防止半圆融合残差延续到下一条直线。
- 最终决策：仅在M5/M6原13字段条件成立时追加`route_segment=0..3`形成14字段CAR；旧8/9/10/11/13字段继续兼容。段界沿现有题面中心线，依次为`1500.0/3070.796/4570.796/6141.593 mm`；未改速度规划、循迹、停车目标、Task周期、DMA/ISR或按键语义。
- 涉及文件：`Device/Communication/PeerLink`、`Task/task_bringup_chassis.c`、PeerLink测试、Task说明和本记录。
- 验证方式和结果：Communication严格主机测试PASS；Bring-up Debug完整链接通过，FLASH/SRAM_BANK0为`87864/19472 B`。用户确认电机失能后，锁定`0D28:0204` DAPLink写入`87864 B`镜像并复位；源BIN SHA-256为`B85866654884F494236C5D95202883080A7927C960698E9E501C06D16808E21E`，按要求未verify/readback，未按KEY。
- 已知限制与待确认：尚未用实车CAR/DBG确认四个里程边界和F407复位后的短暂重建过程；需整圈采样验证。

## 2026-08-01：撤销路线段号，恢复13字段CAR

- 结论：四段融合方案实测更差，已按用户决定撤销。G3519源码恢复原13字段CAR，不再计算或发送AB/BC/CD/DA段号。
- 修改原因：F407每段重建后仍会重新产生融合噪声，分段不能解决根因；最终控制改由F407对规划和既有低通轮速加速度做无状态静态融合。
- 最终决策：不改G3519速度规划、轮速加速度滤波、Task周期、循迹、停车和KEY语义；13字段旧协议继续作为当前源码真值。
- 涉及文件：`Device/Communication/PeerLink`、`Task/task_bringup_chassis.c`、PeerLink测试、Task说明和本记录。
- 验证方式和结果：Communication严格主机测试PASS；Bring-up Debug完整链接通过，FLASH/SRAM_BANK0为`87456/19464 B`，源BIN SHA-256为`91EB1A2F0A2CFCCE10496F84892FF07E4F0BBEDF2B7FB41A5B51EC1B549CA644`。尝试烧录时`0D28:0204`未被Windows枚举，因此本源码镜像尚未部署；当前板上14字段过渡固件可被最终F407兼容接收并忽略段号。
- 已知限制与待确认：待DAPLink重新连接后才能把3519恢复为13字段；这不阻塞当前F407控制策略实测，但部署版本暂与G3519源码不一致。
