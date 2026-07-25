# APP/Algorithm/Filter

每种滤波器使用独立头文件和源文件：低通、滑动平均、三点中值、Biquad 和通用
一维 Kalman。它们不使用动态内存、RTOS 或全局实例；
`algorithm_filter.h` 只作为统一包含入口，不再混放类型和实现。

Biquad 与 Kalman 从 `jixun3/Application/algorithm/attitude_algorithm.*` 迁移。
旧文件中的全局 Mahony 实现不重复保留，统一使用 `Algorithm/Mahony` 的显式
Context/Input/Output 版本。

所有 Update 都先在局部变量中完成验证，只有输出有限时才提交内部状态。滑动平均还会验证 `count/next/capacity/sum` 不变量，求和溢出不会部分增加计数或覆盖窗口。

测试：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\APP\Algorithm\Filter\tests\run_gcc_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\APP\Algorithm\Filter\tests\run_arm_compile.ps1
```
