# LineTracking

通用多通道循迹传感器 Device。BSP 提供从左到右排列的数字或模拟通道，
并统一归一化到 `0..10000`；本层负责每通道最小/最大值标定、极性、阈值、
权重、偏差、丢线和交叉口识别，不包含任何 MCU 引脚或 ADC/GPIO 寄存器。

位置估计使用 `APP/Algorithm/LineTracking` 的差比和算法。多通道差比和先按各探头
离目标中心的距离加权，因此外侧探头比内侧探头产生更大的误差；同时输出加权质心、
融合原始位置、低通位置和置信度，方便 Live Watch 调参与诊断。

默认权重为 `-1..1`，偏差为 `position - target_position`：正值表示线在车体
右侧。实际传感器的通道数、极性、阈值和机械位置应由 APP 配置。

输入会同时保留两组值：`normalized[]` 是逐通道标定和极性处理后的 `0..1`
灰度。数字量/阈值路径的 `line_strength[]` 是阈值抑制后的强度；模拟量路径调用
`Device_LineTracking_UpdateAnalogRaw()`，此时先从八路 `normalized[]` 中减去当前帧
共同最小值，`line_strength[]` 只保留通道间相对差再进入差比和。该路径不需要预存
黑白标定端点，也不使用每路阈值。UART 外部设备无需伪装成 BSP ADC/GPIO 后端。

模拟量的有线判定使用八路 `line_strength[]` 总和；位置权重只用于计算方向。不能用
加权后的左右分母判断有无线，否则靠近中心、位置权重较小的有效通道会被误判为丢线。
模拟量置信度同样来自八路总差值，不受产品方向权重影响。

当前具体器件入口保持分离：芯流 T8 查询帧和亚博主动上报帧都只生成原始 UART ADC
快照，由产品选择是否调用 `Device_LineTracking_UpdateAnalogRaw()`；感为（Ganwei）
DATA/CLK Device 通过 BSP 取得八路
数字电平并调用 `Device_LineTracking_Update()`。数字量路径保留逐通道阈值、位图和
交叉口判定，后续接 `Mode_Car_DigitalTracking`；模拟量路径接
`Mode_Car_AnalogTracking`。
