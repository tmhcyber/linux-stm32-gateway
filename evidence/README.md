# 六阶段验收索引

阶段1—6已于2026-11-14全部结项。这里精选实际验收结果；来源与公开文件校验见[MANIFEST](MANIFEST.json)，应用源码对应关系见[SOURCE_CODE](SOURCE_CODE.json)。

| 阶段 | 验收重点 | 核心证据 |
| --- | --- | --- |
| 1 | 30秒实板、29条合法记录、连续序号 | [收口报告](stage1-closeout-2026-11-11/REPORT.md) |
| 2 | 1小时3599条、原始流ASan回放一致 | [摘要](results/stage2-hour-summary.json)、[分析](results/stage2-hour-analysis.json)、[比较](results/stage2-replay-comparison.json) |
| 3 | 普通/ASan各49项、故障恢复与存储核对 | [软件](results/stage3-software.json)、[实板](results/stage3-board.json) |
| 4 | 普通/ASan各64项、600秒停机恢复 | [软件](results/stage4-software.json)、[停机](results/stage4-outage.json)、[实板](results/stage4-board.json) |
| 5 | 普通/ASan各75项、查询与服务运维 | [软件](results/stage5-software.json)、[集成结项](../docs/STAGE5_CLOSEOUT.md) |
| 6 | 普通/ASan各83项、TSan48项、诊断实板与长测 | [软件](results/stage6-software.json)、[TSan](results/stage6-tsan.json)、[实板](../docs/STAGE6_FINAL.md) |

## 最终连续8小时

- 单调跨度 **28,800.134802秒**；同一服务身份，0重启。
- **28,794条遥测、57,588个测点、5,759条诊断全部入库**。
- **28,794条MQTT遥测获得PUBACK**；所有唯一身份与源记录一致。
- **1,437项历史返回逐字段匹配**；479次资源采样，RSS6,080—8,176KiB，fd固定18。
- 存储队列高水位2，退出队列/在途归零；记录的解析、存储及MQTT错误计数0。
- UTC与单调观测差值保存在[时钟记录](results/stage6-clock.json)，持续时长按单调时钟核验。

[运行结果](results/stage6-8h.json) · [独立退出核对](results/stage6-8h-postcheck.json) · [版本与方法](../docs/STAGE6_FINAL.md)

## 结果阅读

每阶段的软件统计对应当时版本；最终83项是阶段6历史回归结果。公开版追加的安全/发布检查在[公开版复验](PUBLIC_RELEASE.md)单独记录。

原阶段5约2小时57分钟长测因输入中断结束，[原结果](results/stage5-interrupted.json)保留false；持续运行要求由阶段6最终版本的独立8小时窗口完成。Fault实板窗口按MQTT最新值语义发布74/75条，SQLite完整保存75条；最终8小时窗口遥测发布28,794/28,794。

复现使用私有PTY、临时数据库与回环broker；实板使用STM32 USART1→Windows CH340→VirtualBox UART2→Ubuntu ttyS1。运行结果中的受控SIGTERM退出143与验收器passed字段分别表示服务退出方式和验收结论。
