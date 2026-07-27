# FusedNavigation

`Mode_Car_FusedNavigation` 只消费标准物理量，不依赖 ICM-45686、GMR、UART、
FreeRTOS 或具体 MCU。调用方统一左右轮前进正方向，并把陀螺 Z 轴正方向转换为与
`(right-left)/track_width` 一致后再调用。

估计器用陀螺增量传播航向，以编码器累计偏航为低频参考，通过一阶互补校正得到局部
连续 `x/y/yaw`。可选打滑门控比较编码器和陀螺偏航角速度；残差超过进入门限时停止
编码器航向修正，并在连续恢复样本达到配置值后重新启用。纯编码器积分
`encoder_heading_deg` 始终保留作诊断，门控期间只有内部低频参考跟随陀螺，因此恢复后
不会重新注入打滑段的差分航向。门控不修正左右轮平均增量，`x/y` 仍可能受纵向打滑污染。

这里的 `x/y` 始终是差速驱动轮轴中点。车头、相机或其他刚体参考点由产品 Task 按实物
安装尺寸换算，不写入通用 Mode。`heading_innovation_deg`、`encoder_yaw_rate_deg_per_second`、
`slip_residual_deg_per_second` 和 `slip_gate_active` 供运行期诊断；Mode 不会假装完成
绝对重定位。

Task 提供刚体跟踪点纵向偏移和路径前视点。Mode 把跟踪点到前视点的向量转换到车体系，
并按下式求后轮轴目标曲率，其中 `L` 为跟踪点相对后轮轴的纵向偏移，`x/y` 为前视向量的
前向/左向分量：

```text
kappa = 2*y / (x*x + y*y + 2*L*x)
target_yaw_rate = average(measured_left_speed, measured_right_speed)
                  * kappa * 180/pi
```

曲率增益默认为 `1.0`。前视方向对应的目标航向和航向误差继续输出作诊断，前视几何负责
在到达曲线前逐步建立目标曲率，但不再用 `Kp*航向误差` 直接决定整段转弯角速度。目标
yaw-rate 再由运动学差速前馈和现有 yaw-rate P 环生成左右轮速度目标，最后进入双轮速度
PID；实际总半差速限幅 `115 mm/s`。Task 给出的路径速度只作为左右轮基础目标，车辆不会
为了等待角度而停车。Task 只负责推进原路径游标、沿路径插值前视点、启停和提交电机命令。
