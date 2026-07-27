# Tracking

循迹控制 Mode。输入只有循迹偏差、丢线/交叉口状态、基础速度和时间；输出仅是
左右轮速度意图，不读取传感器、不控制电机、不依赖 MCU。丢线时始终输出停车；
交叉口是否停车由 `stop_on_intersection` 配置。

数字量和模拟量通过独立入口接入：`Mode_Car_DigitalTracking_*` 消费阈值/位图形成
的误差；`Mode_Car_AnalogTracking_*` 消费连续模拟误差，并用八路总差值得到的
置信度决定是否允许控制。两者复用同一个无硬件依赖的循迹 PID 核心。
