# 设计决策与个人贡献

我是陶铭浩。本网关项目以我开发的 [STM32F407 数据采集与故障诊断终端](https://github.com/tmhcyber/stm32f407-data-terminal) 为设备端输入，将板端温湿度采样、数据质量与诊断信息接入 Linux 用户态存储、实时分发与运维查询链路。

## 我的主要技术工作

| 工作方向 | 我的具体工作 | 工程成果与证据 |
| --- | --- | --- |
| 板端采集与数据质量 | 联调 SHT30 周期采集、CRC-8 校验、有限重试与 `VALID / STALE / OFFLINE` 状态迁移，建立数据质量字段定义 | 设备端连续采样不伪装有效，网关完整持久化 57,588 个测点及质量状态 |
| 串口重同步与容错规则 | 在上位机侧设计超长坏行丢弃与 CRLF 重新同步策略，明确帧长边界与质量状态校验语义 | 构成网关 C11 509 字节有界组帧与严格解析基线，长测解析错误为 0 |
| MCU 故障与健康门控 | 设计 Fault 记录校验与启动消费规则，结合 CFSR、压栈 PC 与反汇编分析 UsageFault；制定 IWDG `ALLOW / HOLD / LATCH` 三态门控与预算 | 形成阶段 6 `d=1` 诊断帧 19 字段核心语义，实板长测 5,759 条诊断全入库 |
| 跨端架构规划与实板验收 | 规划串口接收、SQLite 历史、MQTT QoS 1、Unix Socket 查询、systemd 与 MCU 诊断六阶段目标；完成 STM32 至 Ubuntu `/dev/ttyS1` 实板接入、复位与 `UsageFault/IWDG` 恢复联调 | 最终集成版连续运行 **28,800.134802 秒、0 重启**，28,794 条遥测全获 MQTT 确认，见[实板结果](STAGE6_FINAL.md) |

## 项目交付与技术价值

项目在既有 MCU 采集终端上延伸出完整的 Linux 用户态数据链路：

| 交付 | 工程价值 | 证据入口 |
| --- | --- | --- |
| C11串口接收与严格解析 | 从字节流形成带质量、时间和原始位置的类型化数据 | [协议与接收](STAGE2.md) |
| pthread、SQLite与MQTT | 分离接收、磁盘和网络调度，结合完整历史与最新状态分发 | [并发存储](STAGE3.md) / [消息分发](STAGE4.md) |
| 本地查询与服务运维 | 为持续运行提供状态、历史、部署、故障重启及回滚入口 | [查询与部署](STAGE5.md) |
| MCU诊断贯通与实板验收 | 贯通复位、Fault与看门狗信息，完成故障恢复和连续运行验证 | [实板结果](STAGE6_FINAL.md) |

最终实板连续运行 **28,800.134802秒、0服务重启**，28,794条遥测与5,759条诊断全部入库；公开版普通、ASan/UBSan各90项及TSan专项48项通过。[验收索引](../evidence/README.md) · [公开版复验](../evidence/PUBLIC_RELEASE.md)

## 开发工具

使用 GCC / C11、pthread、SQLite、libmosquitto、Python 及 AddressSanitizer / UndefinedBehaviorSanitizer / ThreadSanitizer 完成构建、回归检查与实板验收。源码、复现入口和验收结果见仓库各对应目录。
