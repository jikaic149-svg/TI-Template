# Yahboom Track UART Device

亚博八路巡线模块的无阻塞 UART 模拟量 Device。参考资料固定使用 `115200 8N1`；当前
产品在 Device 初始化 1 s 后由主机发送一次 `$0,1,0#` 开启模拟量主动上报，模块随后发送：

```text
$A,x1:value,x2:value,x3:value,x4:value,x5:value,x6:value,x7:value,x8:value#
```

解析器要求 `x1..x8` 顺序完整，接受 `uint16_t` 十进制值，支持任意 UART 分块、噪声
丢弃、帧头重同步、超长帧和数值溢出统计；不访问 BSP、不等待串口、不使用堆内存，
也不使用参考代码中的全局包缓冲、`strtok` 或 `atoi`。

`adc[]` 只保留原始八路数值。厂家示例中的灰度算法、旧工程的逐路白底与阈值均不进入
本 Device；通道安装顺序、归一化、位置估计和误差正方向继续由现有产品灰度链配置。
