# Device/Sensor

传感器设备负责帧/计数解释、单位换算、快照与在线状态，不直接包含 MCU 外设实例。
# Sensor

- `Encoder/`：编码器累计值、增量、速度与里程。
- `IMU/`：通用 IMU、BMI088、ICM-45686、JY61P 与棱科 M0。
- `LineTracking/`：数字/模拟多通道循迹的权重、位置、偏差、丢线与交叉口识别；
  `XinliuT8/` 解析 UART 查询返回的八路 ADC，模拟路径不做逐通道阈值化。
