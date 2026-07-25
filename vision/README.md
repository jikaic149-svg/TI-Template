# Independent MaixCAM applications

`vision` 包含两套可分别复制、部署和运行的 MaixCAM 应用：

```text
vision/
├─ maixcam_pro/     A16/A17、UART0、/dev/ttyS0
└─ maixcam2/        B0/B1、UART2、/dev/ttyS2
```

两个目录都自带 `main.py`、`board_config.py`、`vision_link.py` 和 Host 测试，
不依赖 `shared`、另一个板型目录或 `vision` 父目录中的 Python 文件。部署时只需
复制目标板型的整个目录。

| Board | Camera TX | Camera RX | Function | Device |
|---|---|---|---|---|
| MaixCAM Pro | A16 | A17 | UART0_TX / UART0_RX | `/dev/ttyS0` |
| MaixCAM 2 | B0 | B1 | UART2_TX / UART2_RX | `/dev/ttyS2` |

两款板均固定为 `115200 8N1`。模板入口只负责启动 UART 并接收、打印协议包；
具体模型、路线、类别表或产品识别状态机应在对应目录内按产品需求实现。

Host 验证：

```powershell
python -B -m unittest discover -s .\vision\maixcam_pro -p "test_*.py" -v
python -B -m unittest discover -s .\vision\maixcam2 -p "test_*.py" -v
```
