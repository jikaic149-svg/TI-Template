# Device/Actuator/Motor

`Device_Motor` 把 APP 的有符号 `-10000..10000` 命令转换为 BSP 的方向和 PWM 状态。

当前登记的电机型号参数分开存放：

- `MG310/device_motor_mg310_gmr.h`：MG310、GMR 500 线、实际减速比 20.409。
- `MG513X/device_motor_mg513x_gmr.h`：MG513X、GMR 500 线、减速比 28。

电机型号与驱动模块是两个维度。G3519 通过编译 profile 选择驱动：

| Profile | BSP 输出 |
|---|---|
| `TB6612`（默认） | 两路 20 kHz PWM、四方向 GPIO、共享 STBY |
| `AT8236` | 四路同步 20 kHz PWM；正转 `PWM/0`、反转 `0/PWM`、滑行 `0/0`、刹车 `1/1` |

- 初始化立即向 backend 提交零命令，并保持锁定。
- 解锁前再次向 backend 提交零命令；零命令失败时绝不解锁。
- 重新锁定先改变软件锁，再请求 backend 停止。
- 非零命令提交失败会重新锁定并尽力提交零命令。
- `positive_is_forward` 只适配产品机械方向；引脚和 PWM 极性仍属于 BSP。

测试：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_gcc_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_arm_compile.ps1
```
