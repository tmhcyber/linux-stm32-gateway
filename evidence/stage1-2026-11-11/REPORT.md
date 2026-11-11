> 公开脱敏副本：原始证据留存本地；设备标识、本机路径及无关环境信息已省略。原始文件哈希不用于核验脱敏后的副本。

# 阶段 1 执行记录

日期：2026-11-11。状态：**阶段 1 按宿主串口映射路线完成；原生 USB 直通不继续验收。**

已在空的 Linux Git 工作区切换到 `codex/stage1-environment-serial`；仓库尚无初始提交。

- 主机：Windows 11，Ryzen 7 8845H，32 GB 内存；硬件虚拟化已开启。
- D 盘可用空间约 200 GB，拟将虚拟机文件保存在本工作区 `.local/vms/`。
- VirtualBox 7.2.22 官方安装包：SHA256 与 Oracle 数字签名校验通过。
- Ubuntu 官方 VMDK 镜像：SHA256 校验通过。虚拟机已创建并运行，Ubuntu 24.04.5 LTS、x86_64，2 CPU、4 GB 内存、32 GiB 动态虚拟磁盘。
- Windows SSH 转发：127.0.0.1:2222 → 虚拟机 22；账户 ubuntu，使用本地 SSH 密钥，已加入 dialout 组。
- 开始时未插板，只有蓝牙串口。用户接板后核实 CH340 为 COM11。

## 开发环境

实际检查见 linux-environment.txt：GCC 13.3.0、Make 4.3、GDB 15.1、Python 3.12.3、libmosquitto 2.0.18。Mosquitto 服务监听本机 127.0.0.1 / ::1 的 1883，尚未开发 MQTT 业务程序。

首次启动在 apt 更新期间遭遇宿主蓝屏，重新启动后 cloud-init 显示 done，但包并不齐全。已单独检查实际软件包并补装，不能以 cloud-init 状态替代依赖验收。

## 蓝屏与接入调整

- Windows BugCheck 0xCA/参数 0x2；调用栈出现 VBoxUSBMon、UsbDk 和其他 USB 驱动，详见 CRASH.md。
- 已经用户处理 Windows 管理员提示，移除本轮 VirtualBox USB 功能，结果退出码 0。
- Oracle MSI 修改同时移除了 VBoxSup 核心服务，随后使用 Oracle VBoxDrvInst 仅补装核心驱动，退出码 0；虚拟机成功启动。
- 最终仅 VBoxSup 为运行中的 VirtualBox 内核驱动；VBoxUSBMon 服务不存在。没有修改原有 UsbDk/hrdevmon。
- OHCI/EHCI/xHCI 均关闭。实际路线为 COM11 → VirtualBox UART2 → Ubuntu /dev/ttyS1。不是 Linux 原生 CH340 USB 接入，也不宣称蓝屏问题已获得长期稳定性证明。

## 串口记录

| 窗口 | 实际结果 | 解释 |
| --- | --- | --- |
| windows-serial | 30 秒，29 条合法记录，0 解析错误，seq 437～465 | Windows 侧基线，不能算 Linux 验证 |
| linux-serial-first | 30 秒，74 条合法记录、1 条截断/字段错误，源序号有倒退 | 包含积压与不连续，原件保留，不作为连续性通过 |
| linux-serial-fresh | 30 秒，17 条可解析记录，原始后段有乱码、3 次超长丢弃 | 仅清空 tty 输入未解决干扰 |
| linux-serial-configured | 30 秒，5 条候选记录、3 次超长丢弃 | 仅保持 raw 115200 仍未获得干净窗口 |
| linux-serial-no-modem | 30 秒，29 条合法记录，0 解析错误、0 超长、seq 291～319 连续 | 停止 ModemManager＋保持 raw 配置后的通过窗口 |

最终通过窗口时间：2026-11-11 17:09:02～17:09:32（Asia/Taipei），11040 字节，启动同步主动舍弃一条。语义核对复用原 STM32 工程 Tools/pc_uplink_receiver.py，对应 semantic-validation.json；原始字节及完整行都已保留。

之后已在这台专用 Ubuntu 内持久屏蔽 ModemManager，串口维持 115200、8N1、raw、无流控、无回显。尚未单独恢复 ModemManager 做故障复现，因此不把关联观察写成唯一根因证明。

## 工具验证与交付边界

- Linux 工具检查 5/5 通过，含 CRLF 跨块、连帧、509 字节边界、超长恢复、有界内存及真实 PTY 接口采集；见 linux-tool-tests.txt。
- 真实串口通过窗口以普通 ubuntu 用户采集，无需 sudo 运行接收器。
- 分支中保存部署/连接/配置脚本、只读取证工具、文档和本轮证据。下载、虚拟磁盘、SSH 私钥和崩溃转储只在忽略目录 .local/，不入库。
- 尚无初始 Git 提交、未推送、未创建远程仓库；本轮不修改简历或宣称新增个人掌握能力。
- 下一阶段从官方 C MQTT 发布示例开始，再逐步接入现有串口数据。
