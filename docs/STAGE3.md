# 阶段3：pthread与SQLite历史存储

## 目标

让串口接收与磁盘I/O独立调度，以事务保存遥测、事件和运行身份，支持故障恢复及历史保留。

## 实现

主线程解析并复制记录到256项有界队列；`gw-storage`线程独占SQLite连接。`record_queue.c`负责队列，`storage.c`负责调度和状态，`history.c`负责数据库操作。数据库、格式化及文件I/O在队列锁外执行。

## 关键设计

- 批次累计10项、约1秒或退出排空时提交。帧、测点、事件和运行提交位置在同一事务更新。
- 队列满时丢新并计数，原始流和JSONL继续记录；可用接收ID核对已入库子集。
- busy、FULL或写错误进入DEGRADED，保留当前批次，解除原因后重试并记录RECOVERED。
- 7天历史保留，每小时安排清理，每事务最多128帧及对应测点、128事件和128个空旧run，让出调度处理新数据。
- SIGINT/SIGTERM触发排空和join；持续存储故障采用约2秒恢复预算并统计未持久化项。实际耗时受OS调度与I/O影响。

| 表 | 内容 |
| --- | --- |
| runs | run_id、起止时间、模式、提交及故障计数 |
| frames | `(run_id, rx_id)`身份、协议头、raw范围和原正文 |
| points | 两测点全部源字段、整数值、质量与采样时间 |
| events | 主线程事件和存储线程诊断 |

阶段3引入schema v2，最终阶段6使用v3并新增诊断表；迁移在事务中完成，保留原历史身份。

## 验证结果

阶段3最终普通、ASan/UBSan各49项通过，覆盖锁竞争、FULL/IOERR解除恢复、信号收尾、迁移和保留清理；并发队列执行15万次尝试并核对FIFO与计数。最终集成版继续通过[83项回归及TSan专项](TESTING.md)。

[阶段3软件结果](../evidence/results/stage3-software.json) · [实板存储结果](../evidence/results/stage3-board.json)

## 复现方式

```bash
make
demo=$(mktemp -d)
build/gateway --replay examples/mixed.bin --boundary aligned \
  --output "$demo/run" --db "$demo/history.sqlite"
python3 scripts/linux/check_stage6_evidence.py --help
```

完整存储回归随 `make test`执行；`gateway-fault`仅用于私有PTY和临时库中的故障注入。生产网关与测试故障控制分离。
