# 公开版发布复验

2026-11-14，在独立Ubuntu 24.04测试副本中对公开源码执行离线复验。原实板VM、部署、固件和归档保持原状态；测试副本的物理串口关闭，使用私有PTY、临时数据库和回环broker。

受测公开提交：`a03fe2be7150288880b89c31c7bc3749981a14a5`；源码快照：`20261114T143620Z-5cc046f6`。后续发布提交只补充文档和本次结果，应用、头文件、测试、配置和工具输入与该受测提交相同。

| 检查 | 结果 |
| --- | --- |
| 普通全回归 | 90项通过，0失败/错误/跳过 |
| ASan/UBSan全回归 | 90项通过，0失败/错误/跳过 |
| TSan | 存储20、MQTT9、IPC11、诊断8项通过 |
| MCU实际C harness | 宏0/1，普通及ASan/UBSan全部通过，12条向量逐字节相同 |
| 离线演示 | 20条遥测、12条诊断全部解析并入库 |
| 来源完整性 | 90个原始输入SHA256一致；1个MQTT测试补充连接就绪等待；阶段1公开认证安全修复保留 |
| 公开材料 | 精选证据哈希、JSON、文档链接及完整祖先历史检查通过 |

当前90项包含原阶段6的83项与7项公开安全回归。TSan仅对私有测试进程族使用setarch，测试前后全局ASLR保持不变。五个C检查程序随普通和ASan/UBSan运行。

初轮普通与ASan各90项通过；TSan的MQTT测试在模拟端接受连接前索引空列表，出现IndexError。为该谓词增加连接就绪条件后，在新快照完整重跑全部检查。生产代码、期限和业务断言保持一致。[初轮结果](public-release/initial-result.json)与[初轮TSan](public-release/initial-tsan.json)保留；具体测试差异及原/公开哈希见[SOURCE_CODE](SOURCE_CODE.json)。

测试环境：gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0，Python 3.12.3，Linux 6.8.0-149-generic。

[汇总](public-release/summary.json) · [完整结果](public-release/full-result.json) · [普通逐项](public-release/normal-suite.json) · [ASan逐项](public-release/asan-suite.json) · [TSan专项](public-release/tsan.json)

原阶段6实板结论及83项历史统计见[验收索引](README.md)，公开版软件复验在这里单独记录。
