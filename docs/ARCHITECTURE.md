# 架构与源码导航

## 数据流与所有权

主线程拥有串口fd、组帧器、解析器和IPC连接，记录原始流与类型化JSONL。启用数据库后，主线程复制记录到有界队列，由 `gw-storage`独占SQLite连接；启用MQTT后，`gw-mqtt`独占libmosquitto客户端。队列锁只保护值复制和状态移交。

历史查询沿用同一个存储线程，经请求/结果队列和eventfd返回；每请求ID避免客户端断开后的迟到结果误投。存储故障、MQTT连接状态、遥测新鲜度和MCU源质量各有独立字段。

## 源码入口

| 子系统 | 代码 | 关注点 |
| --- | --- | --- |
| 主循环 | [main.c](../src/main.c)、[serial.c](../src/serial.c) | poll、termios、重连、信号、退出 |
| 协议 | [framer.c](../src/framer.c)、[protocol.c](../src/protocol.c)、[diagnostic.c](../src/diagnostic.c) | 有界组帧、严格字段、质量与诊断有效性 |
| 输出 | [output.c](../src/output.c) | 原始位置、JSONL、统计与身份 |
| 存储 | [record_queue.c](../src/record_queue.c)、[storage.c](../src/storage.c)、[history.c](../src/history.c) | 复制所有权、事务、降级恢复、迁移与清理 |
| 分发 | [mqtt.c](../src/mqtt.c) | 邮箱、QoS1、期限、在线状态 |
| 查询 | [ipc.c](../src/ipc.c)、[query.c](../src/query.c)、[gatewayctl.c](../src/gatewayctl.c) | 非阻塞客户端、分页、协议与退出码 |
| 运维 | [service配置](../config/stm32-gateway.service)、[安装器](../scripts/linux/install_service.py) | 固定release、普通用户、启动校验与回滚 |

## 关键取舍

- 完整历史与实时快照分别处理：SQLite保存所有成功提交的记录，MQTT恢复时发布最新状态。
- 内存上界显式化：主队列256项、客户端8个、查询/结果8项，过载按分类计数。
- 数据身份与源时间独立：Linux run/rx身份用于核对，MCU seq与uptime用于观察采样进展。
- 故障可观测：原始流、事件、提交计数和运行摘要共同解释异常及恢复。

## 设计与验收对应

| 设计目标 | 对应验证 |
| --- | --- |
| 磁盘故障时接收与存储分别调度 | 私有PTY与临时库覆盖慢写、队列满、busy、FULL和IOERR恢复；原始记录与提交计数分别核对 |
| 网络不可用时保留本地历史 | broker停机约600秒，期间601条遥测全部入库，恢复后发布最新快照 |
| 查询负载与采集并行 | 慢客户端、请求期限、队列压力及迟到结果测试；实板长测1,437项历史返回逐字段核对 |
| 诊断与遥测分别表示数据新鲜度 | 混合输入、诊断重复/晚连接和遥测静默测试；实板软件复位与UsageFault/IWDG恢复 |
| 长时间运行保持版本和数据可追溯 | 连续8小时同一服务身份、0重启，28,794条遥测及5,759条诊断全量持久化与跨通道核对 |

软件测试入口见[测试指南](TESTING.md)，实板数据与逐项结果见[验收索引](../evidence/README.md)。

## 接口约定

协议、CLI和schema与最终受测应用保持一致。阶段1新建VM脚本沿用公开仓库的认证安全修复；公开来源清单和精选证据由[发布说明](PUBLICATION.md)管理。
