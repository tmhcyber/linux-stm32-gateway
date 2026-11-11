# Linux＋STM32 数据采集与 MQTT 网关

复用现有 STM32F407 温湿度终端，使用 Linux C 和 libmosquitto 完成串口到 MQTT 的数据上报。

当前分支：`codex/stage1-environment-serial`。阶段 1 使用宿主串口映射路线；环境、采集退出语义与源码追溯已收口。真实短窗口结果与边界见本次报告，不代表长期稳定性。

- [阶段 1 操作说明](docs/STAGE1.md)
- [阶段 1 实际进度与证据](evidence/stage1-2026-11-11/REPORT.md)
- [阶段 1 收口结果](evidence/stage1-closeout-2026-11-11/REPORT.md)
- 原始总体方案：`<PLANNING_WORKSPACE>\docs\LINUX_STM32_MQTT_GATEWAY_PLAN.md`

Windows 中用 PowerShell 管理虚拟机；Ubuntu 中以普通用户从 `/dev/ttyS1` 读取映射的板载 CH340 输出。蓝屏后已移除 VirtualBox USB 直通驱动，当前不使用 USB 设备直通。虚拟机、下载文件和 SSH 私钥位于忽略目录 `.local/`。

进入当前正在运行的 Ubuntu：

```powershell
./scripts/windows/Connect-Ubuntu.ps1
```

Windows 是统一源码入口。修改后运行 `./scripts/windows/Sync-Ubuntu.ps1`，以 tar＋SSH 生成带 SHA256 清单的独立快照。Ubuntu 的 `~/project/current` 指向最近同步的源码；`~/project/build/<快照>/` 保存构建及来源信息，`~/project/evidence/` 保存采集，不参与同步覆盖。

```bash
cd ~/project/current
bash scripts/linux/check_environment.sh serial /dev/ttyS1
bash scripts/linux/check_environment.sh storage
bash scripts/linux/check_environment.sh mqtt
python3 -m unittest discover -s tests -v
```

后续阶段才实现 C 解析器与 MQTT 网关；串口原始数据采集不等同于网关开发或 Linux 内核驱动开发。
