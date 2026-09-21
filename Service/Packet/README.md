# 可选数据包服务

复用 `APP/Algorithm/Packet` 已有的 `@1|TYPE|SEQ|LEN|PAYLOAD|CRC16\n` 协议。
CRC-16/CCITT-FALSE、格式和最大帧长由该算法定义，这里不另建协议。
Service 将解析器的临时指针视图深拷贝为 `Service_Packet_Message`，再发布到预先装配的
Topic。消费方用 `Service_Packet_Take` 取得有类型的副本。

Message 包含完成帧时的 Task 时间戳（ms）、16 位协议序号、长度、NUL 结尾的类型和
按长度读取的 payload；ARM 下共 136 B。一个两槽 FIFO 的 payload 存储为 272 B，
另加 Core 控制结构、Service 解析状态/暂存。为每条连接配置自己的单 owner 实例。

Feed 的 ACCEPTED 仅表示消息发布成功，FULL 表示当前帧未交付给任何订阅者；
REJECTED 表示格式或 CRC 不合法。调用者处理结果，本组件不重发、不创建 ACK，
也不认为 CRC 可以代替业务范围检查、身份鉴别或过期/重复命令处理。

与 Line 分别启用：`TEMPLATE_ENABLE_SERVICE_PACKET=ON` 自动带入 Core/Message 和
Algorithm/Packet，但不绑定串口，不进入默认指示灯命令链。

资料来源：仓库既有 Algorithm/Packet API 与协议文档；适配代码为本次新增实现。
host 测试覆盖消息所有权、队列满和 CRC 拒绝。实际连接传输仍需按产品验证。
