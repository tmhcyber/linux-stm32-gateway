# 阶段2：C11接收与严格协议解析

## 目标

将串口字节流转换为类型化温湿度记录，同时保存数据质量、序号、原始位置和异常时间线。

## 实现

`serial.c`管理串口和termios；`framer.c`以CRLF组帧；`protocol.c`校验v1遥测；`output.c`写原始流、JSONL和运行摘要；`main.c`用poll驱动接收及回放。

串口采用只读、非阻塞方式。`flock`和`TIOCEXCL`提供排他接入；连接失败按1/2/4/8/16/30秒退避，收到合法遥测后重置。15秒合法遥测静默产生SILENT，恢复后产生TELEMETRY_RESUMED。

## 关键设计

- 正文上限509字节；超长后丢弃至下一个CRLF再恢复。支持任意切分、跨块CRLF和连续多帧。
- v1包含27个必需字段：`v/reason/flags/gap/coal/snap/n`及两个点各10字段。重复、缺失、未知字段和非法整数分类计数。
- 两个点分别为SHT30温度、湿度；整数值配合scale=-3表达物理量。质量状态区分VALID、STALE、OFFLINE、NOT_AVAILABLE；失败时保留最后成功值。
- `rx_id`表示Linux接收身份，`seq`表示MCU成功采样序号；UTC记录Linux接收时间，MCU毫秒字段记录启动后时间。
- 回放显式选择 `aligned`（从完整正文开始）或 `stream`（先同步到CRLF）。UART v1按格式和语义校验，帧级CRC与MCU唯一启动ID属于协议扩展点。

| 输出 | 内容 |
| --- | --- |
| raw.bin | 实际接收字节，包含同步舍弃和末尾片段 |
| records.jsonl | 类型化遥测、质量、时间、接收编号及raw字节范围 |
| events.jsonl | 解析错误、静默、恢复、重连和退出事件 |
| summary.json | 计数、序号关系、连接与退出状态 |

退出码0表示正常结束、有合法输入且无运行错误；1表示已记录错误；2表示正常结束但无合法输入。INT/TERM返回130/143。`complete`标记到达时限或文件EOF，服务的受控停止使用信号退出语义。

## 验证结果

阶段2普通、ASan/UBSan各29项通过；真实MCU格式器20条状态向量通过。正式实板窗口 **3600.008328秒、3599条合法记录**，完整原始流的ASan回放与逐条比较通过。

[实板摘要](../evidence/results/stage2-hour-summary.json) · [独立分析](../evidence/results/stage2-hour-analysis.json) · [回放比较](../evidence/results/stage2-replay-comparison.json)

## 复现方式

```bash
make
demo=$(mktemp -d)
build/gateway --replay examples/telemetry.bin --boundary aligned --output "$demo/run"
make test
make sanitize
```

实板接收使用 `build/gateway --port /dev/ttyS1 --duration 60 --output NEW_DIR`。端口应由一个接收器独占，输出目录每次新建。最终版本同时支持阶段3—6的存储、分发和诊断能力。
