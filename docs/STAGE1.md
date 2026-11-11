# 阶段 1：Ubuntu 环境与板载串口

范围：建立 Ubuntu 虚拟机、准备开发依赖、接入板载 CH340 输出、以普通用户保存真实串口数据。当前实际结果见 evidence 中的 REPORT。

2026-11-11 执行中发生宿主蓝屏，转储调用栈包含 VBoxUSBMon、UsbDk 等模块。当前改用 **Windows COM → 虚拟 UART → Ubuntu ttyS1**，关闭虚拟机全部 USB 控制器；原计划的原生 USB 直通暂不使用。详见 [崩溃记录](../evidence/stage1-2026-11-11/CRASH.md)。

## 环境选择

Ubuntu 24.04 LTS 官方 cloud image，VirtualBox，2 CPU、4096 MB 内存、32 GiB 动态磁盘。使用 Ubuntu Server，通过 SSH 学习命令行。NAT 只将虚拟机 SSH 转发到 Windows 的 127.0.0.1:2222。虚拟磁盘和种子 ISO 均在 `.local/`，不会入库。

VirtualBox 来源：https://download.virtualbox.org/virtualbox/ 。Ubuntu 来源：https://cloud-images.ubuntu.com/noble/current/ 。执行时从各自官方 SHA256SUMS 校验下载；VirtualBox 额外检查 Oracle 的 Windows 签名。此机器安装仅保留 VBoxApplication，移除新装的 VBoxUSB；不修改原有 USB 过滤驱动。

## 建立或启动环境

Windows：先安装已校验的 VirtualBox，首次安装驱动需要 Windows 管理员 UAC。准备 `.local/python-deps` 中的 pycdlib 后执行：

```powershell
python -m pip install --target .local/python-deps pycdlib
python scripts/windows/prepare_seed.py
./scripts/windows/New-UbuntuVm.ps1 -HostSerialPort COM11
./scripts/windows/Connect-Ubuntu.ps1
```

新建环境由 cloud-init 创建 ubuntu 用户、加入 dialout 组、建立基础目录；它不再维护第二份开发包清单。SSH 私钥仅保存在 `.local/keys/`。`prepare_seed.py --port` 必须与 `New-UbuntuVm.ps1 -SshPort` 相同；创建脚本会在任何 VM 创建操作之前核对生成的 SSH 配置。现有 VM 不需要重建，也不要重跑 seed 去替换现有身份。

首次 SSH 可连接后，在 Windows 同步完整源码：

```powershell
./scripts/windows/Sync-Ubuntu.ps1
./scripts/windows/Connect-Ubuntu.ps1
```

```bash
sudo cloud-init status --wait
cd ~/project/current
bash scripts/linux/install_dependencies.sh serial
bash scripts/linux/check_environment.sh serial /dev/ttyS1
# 开始 SQLite 或 MQTT 开发前，分别安装并验收对应范围：
bash scripts/linux/install_dependencies.sh storage
bash scripts/linux/check_environment.sh storage
bash scripts/linux/install_dependencies.sh mqtt
bash scripts/linux/check_environment.sh mqtt
```

安装入口固定为 `install_dependencies.sh {serial|storage|mqtt}`，通用开发工具随各范围安装。storage 包含 sqlite3、libsqlite3-dev；mqtt 包含 broker、CLI 和 libmosquitto-dev。检查脚本只读，失败明确打印 `FAIL:` 并返回 1；未指定合法范围返回 2。serial 检查字符设备、当前用户可读权限、对应 serial-getty 与 ModemManager 是否活动；storage 检查工具与 SQLite pkg-config；mqtt 检查客户端开发库、broker 服务及实际 127.0.0.1/::1:1883 监听。枚举不等于就绪，服务 active 也不等于已有监听。

依赖检查通过仅表示当前开发条件就绪，不能替代后续业务功能测试。缺少未来阶段的开发依赖不追溯否定旧串口窗口。

COM11 是本轮核实的实际端口，重新插线后仍需核对。现有虚拟机启动与关机：

```powershell
& 'C:/Program Files/Oracle/VirtualBox/VBoxManage.exe' startvm linux-stm32 --type headless
./scripts/windows/Connect-Ubuntu.ps1 sudo poweroff
```

## 通过宿主串口映射接入 CH340

沿用现有 STM32 正常固件、供电和跳帽。连接板载 Mini USB“USB转串口”接口，关闭占用该端口的 Windows 工具。该接口与 DAPLink 调试接口不同。不要为本阶段修改线路或固件。

虚拟机关机后，使用已有的 Windows CH340 串口驱动映射到第二个虚拟 UART：

```powershell
./scripts/windows/Set-HostSerial.ps1 -Port COM11
& 'C:/Program Files/Oracle/VirtualBox/VBoxManage.exe' startvm linux-stm32 --type headless
```

脚本校验指定端口确实属于 VID 1A86 / PID 7523 的 CH340，并关闭虚拟机 USB 控制器。Windows COM11 → VirtualBox UART2（I/O 0x2F8、IRQ3）→ Linux /dev/ttyS1。UART1 继续用于 Ubuntu 控制台日志。

虚拟机运行时会占用宿主 COM11，其他 Windows 串口工具不能同时打开。Ubuntu 内看到的是模拟 16550 UART，不能将这一结果写成已验证 Linux CH340 USB 驱动或原生 USB 直通。

Ubuntu：

```bash
ls -l /dev/ttyS1
id
test -r /dev/ttyS1 && echo readable
```

核实 ttyS1 的串口配置、dialout 组和可读权限。当前映射下 lsusb 中不应出现 CH340。重新插线导致 COM 号变化时，应在关机状态重跑映射脚本；不自动占用其他串口。

这台专用虚拟机在停止 ModemManager、保持 raw 115200 后获得了干净的旧 30 秒窗口。已持久屏蔽 ModemManager；不能把关联观察等同于完整根因证明。只在未来新建且没有蜂窝调制解调器用途的专用虚拟机中显式执行一次：

```bash
bash ~/project/current/scripts/linux/setup_dedicated_serial_vm.sh --dedicated-vm
```

这个入口需要 sudo，只处理 ModemManager 策略；当前 VM 已 masked，不必重复执行。不在其他 Linux 主机泛化屏蔽该服务。对应 serial-getty 必须 inactive；当前 ttyS1 disabled，Ubuntu 控制台继续使用 ttyS0。日常操作以普通 ubuntu 用户执行，不调用 sudo、不改服务：

```bash
bash ~/project/current/scripts/linux/configure_serial.sh /dev/ttyS1
```

日常脚本完整设置 115200、8N1、raw、无软硬件流控。Python 采集器会自己用 termios 配置并在结束时恢复原属性；后续 C 网关也必须自己配置 termios，不依赖事先运行 stty。

依赖安装保留当前 apt 源。本机已工作的中科大镜像继续保留；只有明确决定换源时单独执行 `bash scripts/linux/set_apt_mirror.sh --ustc`，会保留原源备份，包签名仍由 Ubuntu keyring 校验。此次没有重新换源。

## 采集原始数据

配置：115200、8N1、无流控；只读取，不发送应用命令。新连接先丢弃到首个 CRLF，再保存完整帧。所有原始字节仍保存在 raw.bin 中。

```bash
python3 ~/project/current/scripts/linux/capture_serial.py \
  --port /dev/ttyS1 \
  --duration 30 --discard-buffered \
  --output ~/project/evidence/serial-first
```

每次用新的输出目录。`--discard-buffered` 在观测前清空已积压的 tty 输入，并在 summary 中注明；本轮首个未清空窗口已原样保留，不能把启动积压、截断或序号倒退隐藏掉。脚本输出 raw.bin、complete-lines.txt、summary.json。v1_candidate_lines 只表示形似 v1 的完整行，不代表完整语义校验通过。完整验证可使用原工程 Tools/pc_uplink_receiver.py 对同一份记录进行对照。

`tcflush` 只能清空 Linux tty 当时可见的队列，不能保证清空 Windows/VirtualBox 的其他缓冲。核对设备时间、序号和窗口时长，不能仅凭计数连续就宣称实时传输。原接收器对启动时 NOT_AVAILABLE+NACK 存在误拒收；遇到该组合应保留原帧并单列解析器限制，完整协议修正在阶段 2。

采集退出约定：完整结束且有候选帧为 0；运行/链路错误为 1；完整结束无候选帧为 2；SIGINT 为 130；SIGTERM 为 143。中断保存已读的原始字节、完整行和摘要，新增 signal、exit_code、link_ended 字段。POLLIN 与 HUP 同时出现时先有界读取可用尾部（最多 256 KiB），再报告链路错误。输出目录已存在时拒绝覆盖，返回 1。CLI 参数错误仍由 argparse 返回 2。

## 源码同步与构建来源

在 Windows 编辑，运行 `./scripts/windows/Sync-Ubuntu.ps1`。同步只选择 Git 已跟踪或未被忽略的源文件：scripts、tests、docs、src、include、config 中的指定源文件扩展名，以及 README、Makefile 等顶层配置；排除 .local、密钥、磁盘、evidence、build、缓存。新增类型需明确修改白名单；不要把秘密写入允许同步的源码或配置。

每次 tar＋SSH 同步都建立新的 `~/project/source/<快照>/`，不清空整个 project，不删除旧手动 scripts/tests，不覆盖旧 evidence。传输前记录本地 Git commit、完整未提交状态、逐文件实际字节 SHA256、清单 SHA256 和工具版本；接收端检查清单、路径、SHA256，并重新读取落盘文件验证。成功后更新 current 符号链接，构建目录 `~/project/build/<快照>/` 存放同一 SOURCE.json 与含 Linux 工具版本的 RECEIPT.json。本地回执在 `.local/sync/<快照>/`。

允许同步未提交修改：此时 `git_dirty=true`，确切源码以文件 SHA256 为准，不能只引用基线 commit。证据可复制对应快照的 SOURCE.json，后续编译输出写入独立 build 目录。不要在 Ubuntu 的快照中直接维护另一份源码。

```bash
cd ~/project/current
python3 -m unittest discover -s tests -v
for script in scripts/linux/*.sh; do bash -n "$script" || break; done
# 仅验证开发头文件和链接库，无业务、无数据库写入、无 MQTT 流量：
snapshot=$(python3 -c 'import json; print(json.load(open("SOURCE.json"))["snapshot"])')
build="$HOME/project/build/$snapshot"
gcc -Wall -Wextra -Werror tests/dev_dependency_probe.c \
  $(pkg-config --cflags --libs sqlite3 libmosquitto) -o "$build/dependency-probe"
"$build/dependency-probe"
```

## 收口验收与边界

本次实际结果见 [收口报告](../evidence/stage1-closeout-2026-11-11/REPORT.md)。回归通过不等于硬件长期稳定；1 小时、8 小时验证留给后续里程碑。尚未实现 C 网关、SQLite 业务、MQTT 业务或 systemd 应用服务；本阶段不改固件、不调整 Windows 驱动、不恢复 USB 直通。

本阶段通过条件：Ubuntu 已启动、开发依赖可用、宿主串口映射已工作、普通用户可读；保存真实终端输出，人工或原接收器核对字段格式。USB 直通单列为未验证，不影响通过当前映射路线完成 Linux 用户态串口接收。无硬件时 PTY 只能验证工具，实板项明确待完成。
