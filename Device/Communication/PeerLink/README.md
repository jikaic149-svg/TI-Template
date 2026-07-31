# MCU PeerLink

`PeerLink` 是 G3519 与 F407 之间的最小板间 UART 链路。两端使用 115200 8N1、
静态收发环和 DMA；DMA/ISR 只搬运字节，现有 10 ms Task 调用
`Device_PeerLink_Step()` 完成 Packet 解析、500 ms 心跳发送和 1500 ms 离线判定。

当前协议处理五种 Packet：

- `HB`：双向心跳，Payload 为 `<node_id>,<uptime_ms>`。
- `CAR`：G3519 节点 2 以 100 Hz 向 F407 节点 1 提交底盘实测状态。
- `DBG`：F407 节点 1 以 50 Hz向G3519提交只读滚球调试量，仅用于VOFA转发。
- `BTG`：G3519 节点 2 按请求提交滚球目标位置。
- `BST`：G3519 节点 2 按请求结束滚球闭环并要求 `0 A -> Disable`。

`TEL`、`ENC`、`ACC`、`ACC_MEAS`、`DIS` 均不再发送或解析。除只读`DBG`外，
QD4310/CAN明细不跨板；JY61P、灰度和里程只留在G3519本地。

## 滚球目标与停止契约

```text
BTG=2,<target_0p1mm>
BST=2
```

`BTG`协议域为`-1250..1250`，单位0.1 mm；G3519当前调试入口保留5 mm端部余量，只
接受`-1200..1200`。`BST`是离散停止命令，不表示当前位置闭环保持。两类Packet各用
独立16位序号；没有ACK或线上重发。Device发送API明确区分QUEUED、BUSY、参数错误和
传输错误，BUSY不推进序号。Bring-up Task每10 ms最多尝试入队一次，连续3次BUSY后停止
尝试并保留`BUSY_EXHAUSTED`诊断；MCP改用新的请求号可重新提交。

`QUEUED`只证明G3519本地UART已接收整帧，不证明F407已经执行。F407仅在滚球闭环已运行
且上一控制周期视觉有效时接受BTG；BTG不会隐式使能无刷，BST始终可请求停止。

## CAR 实测状态契约

```text
TYPE=CAR
Payload=<node_id>,<sample_ms>,<speed_x_mm_s>,
        <specific_force_x_mm_s2>,<specific_force_z_mm_s2>,
        <pitch_rate_centi_dps>,<valid>,<competition_mode>,<task_active>
Payload=<上述九字段>,<planned_acceleration_x_mm_s2>,<plan_valid>
```

- `node_id` 固定为 `2`；`sample_ms` 与 G3519 `HB.uptime_ms` 同源。
- `speed_x_mm_s` 是左右编码器实测线速度的平均值，范围 `±5000 mm/s`。
- 两个加速度字段直接来自 ICM45686 的车体 X/Z 轴加速度计比力，范围
  `±50000 mm/s²`；未去重力，静止水平时 Z 轴理论上约为 `+9807 mm/s²`。
- `pitch_rate_centi_dps` 是 ICM45686 零偏校正后的车体 Y/Pitch 角速度，单位
  `0.01 deg/s`，范围 `±100000`。
- 只有左右编码器样本、ICM 样本、陀螺零偏和启动校准同时有效时 `valid=1`；否则四个
  物理量必须全部为0且 `valid=0`。
- `competition_mode`独立于物理量有效位，固定使用G3519当前赛题号`2..6`；F407只读
  保存并向视觉侧转发，不能用它启动、停止或切换任一控制状态。
- `task_active`只表示当前赛题任务是否运行。M3短按KEY只切换该状态，不能使能、失能或
  改变F407的QD4310/CAN状态；F407仅把它转发给视觉任务管理器。
- M4/M5/M6实际运行且轮速规划器仍在推进时发送十一字段：规划量范围`±5000 mm/s²`，
  `plan_valid`独立于物理量`valid`。三题都发送驱动轮速目标的同一有符号参考加速度；
  起步和结尾统一为100 mm/s²、400 mm/s³，巡航及直线/圆弧切点以`0,1`明确表示纵向
  匀速。终端jerk退出完成、发车前和运行结束后恢复九字段。M2/M3保持九字段。

`CAR` 使用独立 16 位序号。UART 队列 BUSY 时整帧丢弃，不阻塞、不立即重试、不推进
序号；没有 ACK。F407 用本地接收时刻判断 50 ms 新鲜度，不跨板相减两块 MCU 的时钟。
远端心跳显示重启时，接收端清除旧 `CAR` 会话，旧序号和旧样本不能继续生效。

当前 ICM45686 安装轴、编码器方向、输出轴 CPR 和有效轮周仍需实物确认，因此构建通过
不等于物理符号与尺度已经验证。F407只在心跳在线、M4/M5/M6、CAR新鲜且规划有效时采用规划、
本地BMI088和轮速融合；G3519实测加速度不进入滚球杆角前馈。

## DBG只读调试契约

```text
DBG=1,<a_icm_mm_s2>,<theta_imu_mdeg>,<theta_cmd_mdeg>,
      <target_raw>,<feedback_raw>,<command_current_ma>,<feedback_current_ma>,
      <speed_centi_rpm>,<phase>,<status_flags>
```

G3519只接受节点1、规范十进制、合法范围、phase `0..2`和flags `0..0x01FF`；使用独立
16位序号拒绝重复/旧帧，以本地接收时刻保存单槽Latest Value。年龄不超过100 ms时映射到
VOFA CH22～CH31；CH31在F407原bit0～bit8之上由G3519额外置bit9表示DBG新鲜。101 ms起
CH22～CH31全0。该帧不进入车端或滚球控制，不产生ACK或重发。

## 硬件与验证边界

G3519 UART1 使用 PB4 TX/PB5 RX、DMA_CH1/DMA_CH0；F407 USART1 使用
PA9 TX/PA10 RX。接线必须 TX/RX 交叉并共地。

源码/主机测试、ARM 编译、Debug/Release 链接、烧录回读和双端 UART 抓帧是不同结论。
上板后仍需检查 100 Hz 帧率、序号、BUSY 计数、50 ms 失效、重启会话，以及车速、
X/Z 比力和 Pitch 角速度的方向与尺度。
