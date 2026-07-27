# APP/Algorithm/IMUFusion

`Algorithm_IMUFusion` 是双 IMU 融合算法，独立于单颗 IMU 使用的
`Algorithm_Mahony`。它保留现有启动零偏标定、双陀螺置信度融合、重力方向更新、
四元数传播和圆锥补偿行为，不依赖 BSP、Device 或 RTOS。

公共姿态数据类型统一使用 `Algorithm_Mahony_Quaternion` 和
`Algorithm_Mahony_Euler`。产品安装方向、杆臂、采样方差和门限仍由 Task 配置。

## Tests

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_gcc_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_arm_compile.ps1
```
