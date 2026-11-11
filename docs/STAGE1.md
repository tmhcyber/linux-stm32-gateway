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

首次启动由 cloud-init 创建 ubuntu 用户、安装依赖并加入 dialout 组。SSH 私钥仅保存在 `.local/keys/`。网络安装完成以 `sudo cloud-init status --wait` 和实际依赖检查为准，不以 SSH 能连接作为完成标准。

```bash
sudo cloud-init status --wait
bash ~/project/scripts/linux/install_dependencies.sh
bash ~/project/scripts/linux/check_environment.sh
```

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

这台专用虚拟机在停止 ModemManager、保持 raw 115200 后获得了干净的 30 秒窗口。已在虚拟机内持久屏蔽 ModemManager，避免探测干扰；当前没有额外开启该服务来复现，不能把受控调整等同于完整根因证明。每次新启动虚拟机后先配置串口：

```bash
bash ~/project/scripts/linux/configure_serial.sh /dev/ttyS1
```

依赖安装脚本使用本轮已检查可达的中科大 Ubuntu 镜像，保留原 apt 源备份，包签名仍由 Ubuntu 官方 keyring 校验。

## 采集原始数据

配置：115200、8N1、无流控；只读取，不发送应用命令。新连接先丢弃到首个 CRLF，再保存完整帧。所有原始字节仍保存在 raw.bin 中。

```bash
python3 ~/project/scripts/linux/capture_serial.py \
  --port /dev/ttyS1 \
  --duration 30 --discard-buffered \
  --output ~/project/evidence/serial-first
```

每次用新的输出目录。`--discard-buffered` 在观测前清空已积压的 tty 输入，并在 summary 中注明；本轮首个未清空窗口已原样保留，不能把启动积压、截断或序号倒退隐藏掉。脚本输出 raw.bin、complete-lines.txt、summary.json。v1_candidate_lines 只表示形似 v1 的完整行，不代表完整语义校验通过。完整验证可使用原工程 Tools/pc_uplink_receiver.py 对同一份记录进行对照。

本阶段通过条件：Ubuntu 已启动、开发依赖可用、宿主串口映射已工作、普通用户可读；保存真实终端输出，人工或原接收器核对字段格式。USB 直通单列为未验证，不影响通过当前映射路线完成 Linux 用户态串口接收。无硬件时 PTY 只能验证工具，实板项明确待完成。
