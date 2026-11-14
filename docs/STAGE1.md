# 阶段1：Ubuntu环境与串口接入

## 目标

建立可重复的 Linux 用户态开发环境，将 STM32 USART1 的温湿度数据接入 Ubuntu，并保存来源可追溯的采集记录。

## 实现

采用 Windows CH340 → VirtualBox UART2 → Ubuntu `/dev/ttyS1` 路线。Ubuntu使用普通用户，串口为115200 / 8N1 / raw；日常串口配置与专用VM的一次性配置分离。

PowerShell管理VM与SSH；tar + SSH传输源码，每次生成SHA256文件清单和独立快照。源码、构建、运行证据分目录，便于复验和版本定位。

## 关键设计

- 环境检查按串口、存储、MQTT分组输出状态，避免对未启用功能误判。
- 接收器处理信号、中断与断链；原始流、完整行和摘要保存在新目录。
- `configure_serial.sh`负责日常串口基线；专用VM策略集中在 `setup_dedicated_serial_vm.sh`。
- 新建VM使用加密SSH私钥和需要密码的sudo策略，运行资料保存在忽略目录。

## 验证结果

阶段1收口的30秒实板窗口接收29条合法记录，序号连续，解析错误0。原始阶段1公开证据保留于[收口记录](../evidence/stage1-closeout-2026-11-11/REPORT.md)。后续阶段在同一路线上完成最终8小时集成验收。

## 复现方式

Ubuntu下先检查端口占用，再配置和检查环境：

```bash
fuser /dev/ttyS1
bash scripts/linux/configure_serial.sh /dev/ttyS1
bash scripts/linux/check_environment.sh serial /dev/ttyS1
python3 -m unittest discover -s tests -p 'test_serial_capture.py' -v
```

Windows已有私有SSH配置时，使用 `scripts/windows/Connect-Ubuntu.ps1` 进入VM，使用 `Sync-Ubuntu.ps1`同步开发快照。新建VM的准备步骤见[公开材料说明](PUBLICATION.md)。
