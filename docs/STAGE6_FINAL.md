# 阶段6实板验收与版本

## 实板结果

| 场景 | 遥测 / 诊断 | 核对结果 |
| --- | --- | --- |
| 正常60秒 | 59 / 12 | 全部入库、59条MQTT确认 |
| 晚连接30秒 | 28 / 6 | 周期诊断提供本次启动摘要 |
| 软件复位75秒 | 76 / 14 | uptime回退，复位位0x0A，恢复HEALTHY |
| UsageFault/IWDG恢复75秒 | 75 / 13 | 有效Fault记录、实际看门狗恢复，75条遥测入库、74条MQTT发布 |
| 固定服务120秒 | 119 / 24 | 普通用户服务、全部入库与MQTT遥测确认 |
| 最终连续8小时 | 28,794 / 5,759 | 同一服务身份、0重启、全量持久化与跨通道核对 |

[正常](../evidence/results/stage6-board-normal.json) · [晚连接](../evidence/results/stage6-board-late.json) · [复位](../evidence/results/stage6-board-reset.json) · [Fault](../evidence/results/stage6-board-fault.json) · [120秒服务](../evidence/results/stage6-board-service.json)

Fault测试以受控SRAM指令触发UsageFault，记录fault_type=4、PC=0x2001D000、CFSR=0x00010000，复位原因0x12（PIN/IWDG），之后恢复HEALTHY。MQTT按最新值邮箱发布；该窗口1条历史值被合并，SQLite保留全部75条。

## 最终版本绑定

- MCU来源标识：`1164e5d7aec7bd6672199e024ebb71b24b98c475`，诊断宏1。源代码主机参考见[fixtures](../fixtures/mcu-stage6/README.md)。
- AXF SHA256：`954aac1879edfa8cc3cd32846e93237647ad11804eb59861f03b01def51b97d3`。
- HEX SHA256：`6c3cebed3f5e98ca03a456188e9b706132dd04d045d77c55a9ca3db7800b9afe`。
- Flash读回验证：29,760字节，SHA256 `380fd59d097425055356ce03fb54a14c4d491e142e99ec1d4bea9e989d0192b6`。
- Linux应用来源标识：`6cab3ad828251fd4db5b318645592f34559f3c1a`；[公开源码与逐文件哈希](../evidence/SOURCE_CODE.json)。
- 受测gateway SHA256：`ae544c10b5a7ea105ee3b38f395e7c63787af36a3a74b5cb0caf638ff04cf0f6`。

上述提交值为原开发来源标识；公开版源码通过当前仓库路径浏览。实际运行以固定二进制和来源清单绑定。公开版的文档与安全工具更新由[发布复验记录](../evidence/PUBLIC_RELEASE.md)单独记录。

## 连续8小时方法

正式窗口按gateway的CLOCK_MONOTONIC计时，跨度28,800.134802秒；5秒暖机单列。481份观察快照和479次资源/查询采样贯穿同一服务身份，结束后检查原始流、JSONL、SQLite、MQTT和IPC返回项。

UTC原值为2026-11-14 02:54:56—10:54:50Z。原观测UTC与单调读数的差异见[时钟记录](../evidence/results/stage6-clock.json)，持续时长以单调时钟判定。

[最终运行结果](../evidence/results/stage6-8h.json)及[退出后独立检查](../evidence/results/stage6-8h-postcheck.json)的 `passed`、`continuous_8h_passed`均为true。SIGTERM退出143、complete=false对应连续服务的受控停止；验收通过由独立检查给出。

后续复验需先准备对应固件、固定release及FINAL.json，并使用 `scripts/linux/start_stage6_acceptance.sh`的固定发布入口；新环境先按[测试指南](TESTING.md)执行离线测试，再准备独立实板部署。串口路线、配置和数据库与版本一起绑定。
