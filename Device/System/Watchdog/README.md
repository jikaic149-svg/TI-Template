# Watchdog Device

该适配层只把 Task 的单一喂狗请求转交给 BSP，并验证 WWDT 后端已经运行。
它不判断哪些 Task 健康，也不允许其他模块绕过 `Task/task_app.c` 喂狗。

硬件实例、周期、低功耗和调试暂停行为由 BSP/SysConfig 持有；全局健康判定由
`APP/System/Watchdog` 持有。
