# 阶段6：MCU诊断贯通

## 目标

将板端复位、Fault、健康状态和看门狗信息接入Linux，统一保存、查询和展示诊断快照。

## 实现

MCU新增独立d=1诊断帧；Linux按族严格分流，诊断使用独立接收编号、年龄和计数。SQLite schema v3增加诊断表；IPC status和MQTT device/state提供字段与有效性，遥测latest/history保持既有数据语义。

## 关键设计

MCU通过 `APP_DIAGNOSTICS_ENABLED=1`显式启用诊断，主循环每5秒提交一份RAM报告，统一UART上行服务优先发送遥测。默认宏0保持遥测构建；[冻结主机参考](../fixtures/mcu-stage6/README.md)提供对应MCU版本的实际C测试输入。

d=1正文上限509字节，CRLF结尾，19字段必需；十进制为规范uint32，hex为 `0x`加8位大写十六进制。字段定义如下：

| 字段 | 范围/含义 |
| --- | --- |
| d / up_ms | 固定1 / uint32报告启动后毫秒；不是UTC |
| reset_captured | 0/1，0时原始及解码复位字段为0 |
| reset_raw / reset_reasons | hex完整RCC_CSR / hex(raw>>25)&0x7F；位0～6分别BOR/PIN/POR/SOFTWARE/IWDG/WWDG/LOW_POWER，多位同时保留 |
| fault_status | 0无已提交记录、1有效、2版本错误、3长度错误、4校验和错误、5内容错误 |
| fault_type | 0无有效记录、1 HardFault、2 MemManage、3 BusFault、4 UsageFault |
| frame_valid / frame_reject | 0/1 / hex位0堆栈错误、1未对齐、2越界，其他位0 |
| pc / cfsr / hfsr | hex原始寄存器；无有效fault记录全部0；无有效frame时pc=0，不当有效地址 |
| monitor_valid / health | 0/1 / 0 STARTING、1 HEALTHY、2 DEGRADED、3 UNHEALTHY |
| active / observed | hex健康位0～7：循环警告、硬超限、数据旧、源离线、采集器故障、上行停滞、上行错误、monitor无效；active必须是observed子集，保留位0 |
| watchdog_initialized / watchdog | 0/1 / 0 WAIT_PERIOD、1 REFRESH_DUE、2 HOLD、3 LATCHED；未初始化时决策及锁存为0 |
| latched | hex，mask=0x00070090：位4采集器故障、7 monitor无效、16输入无效、17未知原因、18刷新失败 |

故障记录使用有效性字段解释PC与寄存器；周期重复报告按快照保存。遥测年龄与诊断年龄独立，持续诊断输入期间仍按遥测规则判定静默。

诊断写入复用原存储队列和单连接；迁移v2→v3以事务完成。MQTT device/state展示最新诊断，telemetry主题保留原遥测结构。代码导航见[架构文档](ARCHITECTURE.md)。

## 验证结果

普通、ASan/UBSan各83项，TSan48项专项通过；MCU宏0/1实际C向量在Windows Clang与Ubuntu GCC普通/sanitizer下逐字节一致。实板晚连接、软件复位及UsageFault/IWDG恢复通过，最终8小时入库5,759条诊断并完成19字段核对。

[软件结果](../evidence/results/stage6-software.json) · [实板验收](STAGE6_FINAL.md) · [最终结项](STAGE6_CLOSEOUT.md)

## 复现方式

`make test`涵盖纯诊断、混合输入、重复/晚连接、非法字段、切分、迁移、故障注入和IPC/MQTT隔离。完整MCU host harness验证使用[测试指南](TESTING.md)中的 `validate_stage6.py`入口和本仓库冻结参考。
