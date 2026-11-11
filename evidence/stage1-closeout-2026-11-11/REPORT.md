> 公开脱敏副本：原始证据留存本地；设备标识、本机路径及无关环境信息已省略。原始文件哈希不用于核验脱敏后的副本。

# 阶段 1 收口实际结果

日期：2026-11-11，时区 Asia/Taipei。状态：本次阶段 1 收口完成，保留短窗口和缓冲异常的解释边界。

## 实施结果

- 先建立现状初始本地提交 `d7e599324b1e62973c8dcc543b547e37e4debcee`，再实施收口；收口提交可用 `git log -1` 查看。无推送、无新远程仓库。
- `check_environment.sh` 只读并要求明确选择 serial、storage、mqtt。必需条件失败返回 1，非法范围返回 2；mqtt 不再把 ss 的空结果当成成功。
- 一次性专用 VM 的 ModemManager 策略单列为显式 `setup_dedicated_serial_vm.sh --dedicated-vm`；此次无需重跑，已有 masked 状态保持。日常串口配置不调用 sudo，明确设置完整 115200、8N1、raw、无软硬件流控，包括关闭 IEXTEN/ECHONL。
- 依赖只由分范围安装脚本维护，cloud-init 负责账户与基础目录。取消安装时自动换源；换源入口单独显式调用。补装 sqlite3 和 libsqlite3-dev，连带更新 libsqlite3-0；apt 源前后 SHA256 均为 `56c7e171dabd40d150f69dd266630822bd45401aa2f2b43d34048d5a616834b6`，见 storage-install.txt。
- 新 VM 创建前核对生成的 SSH Port 与 NAT 转发参数；用 2223 对当前配置 2222 的不一致调用验证拒绝，未创建或启动第二台 VM，未重建现有 VM。
- 采集退出码固定为成功且有候选 0、运行/链路错误 1、正常无候选 2、SIGINT 130、SIGTERM 143。中断保留部分记录与摘要；HUP 与 POLLIN 同时出现时先有界读取尾部。修复了 PTY 断开时 termios.error 导致最终摘要丢失的清理路径。
- Windows 为统一源码入口，tar＋SSH 同步生成独立快照；只传允许的源码、脚本、测试和配置，不传历史证据、密钥、虚拟磁盘或构建产物。验证文件清单、传输内容及落盘 SHA256 后更新 current。旧手动目录与 project/evidence 不作清空目标。

## 已通过的验证

- Ubuntu 上 18/18 项 Python 测试通过，无跳过；原有 5 项保留，新增模拟就绪/故障分支、无输入、打开失败、两种信号、真实 PTY 断开、POLLIN+HUP 尾部、日常无需 sudo、快照哈希与历史隔离测试。见 tests.txt。模拟故障使用 PATH 下的临时命令，没有停真实服务或改系统设备权限。
- 5 个 Linux Shell 脚本 `bash -n` 通过；4 个 PowerShell 脚本语法解析通过。端口不一致拒绝见 seed-port-mismatch.txt。
- serial、storage、mqtt 三范围实际 READY；串口普通用户可读，ModemManager masked，serial-getty@ttyS1 inactive/disabled。Mosquitto 服务与 127.0.0.1/::1:1883 监听均通过，见 environment-after.txt。补装前的 storage 缺失结果如实保留在 environment-before.txt，不追溯否定旧串口验收。
- GCC 13.3.0 以 `-Wall -Wextra -Werror` 编译并运行 tests/dev_dependency_probe.c，实际链接 SQLite 3.45.1 与 libmosquitto 2.0.18。探针只查询版本，无数据库写入、MQTT 流量或网关业务，见 dependency-build.txt。
- 已验证 21 个源码文件的清单和 SHA256；构建测试快照为 `20261111T114532Z-ffe00fb1`，源码清单 SHA256 为 `662b4fb168071307159ef4a54c61d9d4d817f5d1f1aa1fb8cb55309048a0c6be`。build-source.json 与同步回执记录 Git 基线、dirty 状态和工具版本；构建目录 SOURCE.json 与源码目录相同。开发中同步为 `git_dirty=true`，确切版本以文件哈希为准，不把基线提交当成全部修改已提交。
- Windows 旧证据 35 个文件、Ubuntu 原有证据 12 个文件 SHA256 均未变；三个新窗口复制回 Windows 的 12 个原始产物逐文件匹配。Windows 旧证据目录相对初始提交无内容差异，见 integrity.json。新证据目录的 .gitattributes 关闭文本规范化，保留采集原字节。

## 实板结果

本轮重新枚举 CH340 为 COM11，Status OK，见 host-serial.json。路线仍为 STM32 USART1 → 板载 CH340 → Windows COM11 → VirtualBox UART2 → Ubuntu /dev/ttyS1。所有采集以 uid 1000 的 ubuntu 用户运行，没有 sudo、没有业务发送、没有修改固件。

| 窗口 | Ubuntu 时刻（Asia/Taipei） | 原始字节 | 语义合法记录 | seq | 解释 |
| --- | --- | --- | --- | --- | --- |
| serial-30s | 19:39:16～19:39:46，30.091 秒 | 61485 | 163，0 解析错误 | 9184～9346，步长 1 | 设备 value_ms 跨 162 秒，存在缓冲突发/时间跨度异常；不作为 30 秒实时速率通过 |
| settling-20s | 19:41:30～19:41:50，20.172 秒 | 7520 | 19，0 解析错误 | 9452～9470，步长 1 | 后续连续观测的消化窗口，原件保留 |
| serial-followup-30s | 19:41:50～19:42:20，30.041 秒 | 11280 | 29，0 解析错误 | 9472～9500，步长 1 | 新的干净短窗口，设备 value_ms 跨 28 秒 |

三窗口均非 ASCII 0、超长 0、链路错误 0、退出码 0；各自启动同步丢弃 1 条。窗口之间有清空 tty 与主动同步，不能把跨窗口序号空隙计作窗口内部丢帧或拼成连续长期测试。tcflush 仅清空当时 Linux tty 可见输入，不保证 Windows/VirtualBox 各级队列全空；首窗口异常的具体缓冲层未定位。

语义核对只读复用 STM32 工程 Tools/pc_uplink_receiver.py，SHA256 `90b8ea84d3f679fbd75bf6d93f2ba91eade520b0263a5af433875c4e911aa231`，每个窗口有 semantic-validation.json。全部点为 VALID+OK；本轮未出现 NOT_AVAILABLE+NACK。原接收器对该启动组合的误拒收限制仍保留，不能据此声称完整协议已经修正。采集器本身仍只做组帧和 v1 前缀检查。

真实采集来自 `20261111T113757Z-85e55c30` 的开发快照，每个窗口附当时 SOURCE.json；随后只加强了日常 stty 配置、落盘哈希核对、依赖探针和文档，采集器内容相同。最终提交后可再次同步得到 `git_dirty=false`、指向收口提交的回执，保存在 .local/sync，避免证据文档自引用提交哈希。

## 未完成与限制

- 1 小时、8 小时与长期稳定性测试未做，属于后续里程碑。首窗口的突发积压限制如上保留。
- Ubuntu 看到模拟 UART，未验证 Linux 原生 CH340 USB 接入。未启用 OHCI/EHCI/xHCI、未调整 Windows 驱动、未改 UsbDk/hrdevmon，未把旧蓝屏解释为单一驱动根因或永久修复。
- 没有重建现有 VM，也未实际新建 VM 验收新 cloud-init 流程；已完成静态检查和端口不一致的安全拒绝验证。
- 没有 C 网关、SQLite 业务、MQTT 业务或 systemd 应用服务；构建目录只含开发依赖探针和来源信息。未修改简历、未购买硬件。

后续入口：[阶段 1 操作说明](../../docs/STAGE1.md)。先从固定来源快照和当前映射串口开始阶段 2 的 C 协议与业务开发；网关自行配置 termios，不依赖日常 stty。
