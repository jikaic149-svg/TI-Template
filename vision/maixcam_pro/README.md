# MaixCAM Pro

本目录是一套可独立部署和运行的 MaixCAM Pro 应用，不需要 `shared` 或父目录文件。

- Camera TX：`A16 / UART0_TX`
- Camera RX：`A17 / UART0_RX`
- Linux UART：`/dev/ttyS0`
- 波特率：`115200 8N1`

接线时 `A16/TX` 连接 MCU RX，`A17/RX` 连接 MCU TX，并连接公共 GND。

将本目录整体复制到 MaixCAM Pro 后运行：

```sh
python main.py
```

`main.py` 默认启动串口并打印收到的有效协议包。产品视觉业务直接在本目录内扩展。
