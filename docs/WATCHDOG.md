# 全局硬件看门狗

## 当前行为

- MSPM0G3507 与 MSPM0G3519 均使用应用级 `WWDT1`，LFCLK 32.768 kHz、除以 8、
  `2^10` 计数，周期 250 ms。
- Window0/Window1 都是 0% closed，不限制最早喂狗时间；WWDT 在睡眠时继续计数，
  调试器暂停 CPU 时停止计数。
- SysConfig 在 `SYSCFG_DL_init()` 阶段启动 WWDT1。BSP 初始化后到第一个控制周期
  完成的总时间必须小于 250 ms，否则芯片复位。
- `Task/task_app.c` 是唯一运行期喂狗者。每个 10 ms 控制周期完整返回，并且 APP
  判定所有 required source 本周期均完成后，才调用 Device/BSP 喂狗。
- Mode 已进入安全故障状态但控制 Task 仍能周期运行时继续喂狗；看门狗监督的是
  执行是否继续推进，不负责修复 PID、极性或传感器逻辑错误。

## 固定依赖方向

```text
APP/System/Watchdog       纯健康策略，输出 should_feed
          ↓
Task/task_app.c           唯一喂狗决策者
          ↓
Device/System/Watchdog    稳定接口与后端状态验证
          ↓
BSP/*/bsp_watchdog.c      WWDT1、DriverLib、SysConfig
```

禁止从其他 Task、Mode、APP、Device 或 ISR 直接调用 `Device_Watchdog_Feed()` 或
`BSP_Watchdog_Feed()`。架构检查会拒绝新增的旁路调用。允许任意模块喂狗会导致一个
仍存活的非关键模块掩盖控制 Task 卡死，使执行器继续保持最后输出。

## 后续增加关键 Task

1. 在 `app_watchdog.h` 为关键执行源分配不重复的 `APP_WATCHDOG_SOURCE_*` 位。
2. 使用非阻塞 Pub/Sub 把各 Task 当前监督周期的完成状态汇总到唯一监督者。
3. 把所有关键位加入 `required_sources`；只有本周期全部出现时才允许喂狗。
4. 不得复用上个周期的完成位，也不得在 Tick ISR、Idle Task 或外设 ISR 中喂狗。
5. 实测各关键 Task 的最坏执行时间、抖动和栈高水位，确保 250 ms 留有余量。

## 安全边界

- WWDT 超时前，PWM 和 TB6612 可能保持最后输出；看门狗不是立即停车机制。
- 致命异常要立即停车，仍需单独增加异常安全的 STBY 拉低路径；硬件上应确认
  TB6612 STBY 存在可靠下拉，使 MCU 复位和引脚高阻期间驱动器保持关闭。
- 控制代码仍在运行但计算错误时会继续喂狗，必须由 Task 的数据新鲜度、范围、
  `isfinite`、超时和状态检查发现。
- 编译与模拟测试只能验证配置和调用链；250 ms 复位时间、复位原因及电机失能
  时序必须在实物上通过故意停止喂狗测试确认。
