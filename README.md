# Linux＋STM32 数据采集与 MQTT 网关

复用现有 STM32F407 温湿度终端，使用 Linux C 和 libmosquitto 完成串口到 MQTT 的数据上报。

当前分支：`codex/stage1-environment-serial`。阶段 1 已按宿主串口映射路线完成：Ubuntu 开发依赖就绪，30 秒窗口解析 29 条合法记录、0 错误、序号连续。

- [阶段 1 操作说明](docs/STAGE1.md)
- [阶段 1 实际进度与证据](evidence/stage1-2026-11-11/REPORT.md)
- 原始总体方案：`<PLANNING_WORKSPACE>\docs\LINUX_STM32_MQTT_GATEWAY_PLAN.md`

Windows 中用 PowerShell 管理虚拟机；Ubuntu 中以普通用户从 `/dev/ttyS1` 读取映射的板载 CH340 输出。蓝屏后已移除 VirtualBox USB 直通驱动，当前不使用 USB 设备直通。虚拟机、下载文件和 SSH 私钥位于忽略目录 `.local/`。

进入当前正在运行的 Ubuntu：

```powershell
./scripts/windows/Connect-Ubuntu.ps1
```

后续阶段才实现 C 解析器与 MQTT 网关；串口原始数据采集不等同于网关开发或 Linux 内核驱动开发。
