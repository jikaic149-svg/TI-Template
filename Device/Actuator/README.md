# Device/Actuator

执行器设备接收 APP 的安全命令并映射到公共 BSP handle。上电默认状态必须安全，具体引脚、极性和定时器仍属于 BSP。

普通电机型号参数与驱动芯片分开：`Motor/MG310`、`Motor/MG513X` 保存
电机与 GMR 编码器参数；AT8236/TB6612 由 G3519 编译 profile 选择对应
`BSP_Motor` 后端，不在运行时切换。
