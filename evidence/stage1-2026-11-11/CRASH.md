> 公开脱敏副本：原始证据留存本地；设备标识、本机路径及无关环境信息已省略。原始文件哈希不用于核验脱敏后的副本。

# 本轮宿主蓝屏与接入方式调整

2026-11-11 15:52 左右，Windows 蓝屏重启。用户确认是在本次执行期间发生。

## 已确认

- 系统事件 1001：BugCheck 0xCA，参数 1 为 0x2。微软定义为 PNP 检测到无效设备对象（Invalid PDO）。
- 小型转储：`<LOCAL_CRASH_DUMP>`；经用户允许，仅复制到忽略目录 `.local/crash-2026-11-11/`，没有上传。
- 使用微软签名的 CDB 和本机系统映像离线展开调用栈，可见 BugCheck 上方连续的 VBoxUSBMon 模块调用；同一栈还包含 UsbDk、hrdevmon、UsbHub3、Wdf01000。
- 本次刚安装的 VirtualBox 是 7.2.22，其 USB 监控驱动当时已加载。现场此前尚未执行 CH340 的 usbattach。
- 重启后 CH340 在 Windows 枚举为 COM11，驱动 3.9.2024.9。Windows 侧 30 秒读取：29 条合法 v1 记录，解析错误 0，seq 437～465。

## 解释边界

证据指向 USB 枚举/过滤驱动的交互路径。不能把当前栈简单等同为 STM32 固件问题，也不能仅凭模块名确定双方哪一个具体源代码缺陷。离线只使用导出符号和映像展开，函数名加大偏移不作为精确内部函数定位。

没有重新插拔 USB 来复现蓝屏，没有删除、禁用原有 UsbDk/hrdevmon。原计划的 USB 直通暂不继续。

## 调整

- 在虚拟机关闭时，禁用 OHCI/EHCI/xHCI。
- 移除本轮新安装的 VirtualBox USB 直通功能，保留虚拟机主体和 Ubuntu 磁盘；实际移除结果见 REPORT。
- 改为 VirtualBox 的 Host Device 串口映射：Windows COM11 → 虚拟 UART2 → Ubuntu /dev/ttyS1。
- 该路线使用宿主已有的 CH340 串口驱动，不向 Ubuntu 捕获 USB 设备。成功接收后仅证明串口映射路线可用，不证明原 USB 直通缺陷已修复。

参考：[微软 BugCheck 0xCA 参数说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0xca--pnp-detected-fatal-error)；[Oracle VirtualBox 串口与 Host Device 说明](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/working-with-vms.html)。
