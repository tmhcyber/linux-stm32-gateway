# 阶段4：MQTT实时分发

## 目标

将最新遥测与设备状态发布到MQTT，同时保持接收、存储和网络故障的独立处理。

## 实现

显式 `--mqtt`启用独立 `gw-mqtt`线程，使用libmosquitto。网关连接本机回环broker，支持自定义端口、gateway ID和device ID。主线程提供不可变快照，MQTT线程独占网络状态与日志。

## 关键设计

- 遥测、设备状态、网关状态三个主题均使用QoS1，状态使用retained及遗嘱；主题定义见[src/mqtt.c](../src/mqtt.c)。
- 最新值邮箱将网络积压限制在固定容量；SQLite保存完整历史，MQTT恢复时发送当前快照。
- 每主题门控in-flight，连接、ACK、重连退避和退出均有期限；网络恢复不会清除历史故障计数。
- SIGTERM收尾尝试排空快照、发布offline并等待ACK，`offline_acked`记录结果。
- PUBACK记录broker确认；订阅端业务处理可在上层增加独立确认。诊断状态采用最新快照语义。

## 验证结果

阶段4普通、ASan/UBSan各64项，TSan存储20项、MQTT9项通过。私有broker停机 **600.086秒**，期间 **601条遥测全部入库**，恢复后发布最新值。60秒实板窗口59条遥测入库且获得59个PUBACK。

[软件结果](../evidence/results/stage4-software.json) · [停机恢复](../evidence/results/stage4-outage.json) · [实板结果](../evidence/results/stage4-board.json)

## 复现方式

`make test`自动启动独立私有broker，覆盖初始broker不可用、断开重连、邮箱容量和正常退出。实板模式：

```bash
build/gateway --port /dev/ttyS1 --duration 60 --output NEW_DIR \
  --db HISTORY_DB --mqtt --mqtt-port 1883
```

broker应已运行；数据库与输出使用明确路径。当前传输使用本机MQTT，远端TLS接入可在后续部署中扩展。
