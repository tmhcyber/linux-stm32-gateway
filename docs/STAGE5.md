# 阶段5：本地查询与systemd运维

## 目标

为持续运行的网关提供状态、最新值和历史查询，并以固定版本和普通用户服务管理部署。

## 实现

`gatewayctl`通过AF_UNIX连接网关；主线程以非阻塞poll管理客户端，查询由原存储线程执行，eventfd唤醒结果处理。部署采用专用 `stm32gw`用户、固定release、独立配置和状态目录。

## 关键设计与接口

```bash
run=$(mktemp -d)
build/gateway --port /dev/ttyS1 --continuous --output "$run/data" \
  --db "$run/history.sqlite" --socket "$run/control.sock" --mqtt
# 另一个终端使用上面实际生成的run路径：
build/gatewayctl --socket /path/to/control.sock status
build/gatewayctl --socket /path/to/control.sock latest
build/gatewayctl --socket /path/to/control.sock history \
  --from 2026-11-14T00:00:00Z --until 2026-11-15T00:00:00Z --limit 100
```

协议v1为ASCII请求行和单行JSON响应，所有响应包含 `protocol:1`、`code`：

```text
1 status
1 latest
1 history FROM_UTC UNTIL_UTC LIMIT CURSOR
```

history使用严格UTC秒格式，上下界包含，limit为1—1000，首次cursor为 `-`。返回 `records/count/more/next_cursor`；按 `(utc, run_id, rx_id)`稳定升序分页。每次请求读取自己的已提交视图，多页查询期间可有新记录或保留清理。

客户端退出码：0成功，2参数/请求错误，3服务不可用，4无数据，5超时，6查询不可用/忙/失败，7协议或I/O错误。默认客户端总期限6秒，可设置0.1—30秒。

最多8客户端，查询与结果最多8项未领取，请求正文512字节，响应1600KiB；服务端请求期限5秒。存储线程每轮最多执行一个历史请求，采用50ms执行预算和25ms busy等待。请求ID隔离断开与复用fd，慢客户端由主循环预算隔离。

## 服务部署

先按[测试指南](TESTING.md)准备并测试独立源码快照，在该快照目录中审查配置并安装：

```bash
build="$HOME/project/build/$(python3 -c 'import json; print(json.load(open("SOURCE.json"))["snapshot"])')"
sudo python3 scripts/linux/install_service.py install --build "$build"
sudo cat /etc/stm32-gateway.toml
sudo systemctl cat stm32-gateway.service
fuser /dev/ttyS1
bash scripts/linux/configure_serial.sh /dev/ttyS1
sudo systemctl start stm32-gateway.service
sudo systemctl status stm32-gateway.service --no-pager
sudo journalctl -u stm32-gateway.service -f
```

安装器要求SOURCE/BUILD来源清单，按[测试指南](TESTING.md)的快照流程准备；普通 `make`用于快速体验，正式安装采用已验证快照。安装动作完成后显式启动服务。

release在 `/opt/stm32-gateway/releases/`；配置为 `/etc/stm32-gateway.toml`；状态为 `/var/lib/stm32-gateway`；socket为 `/run/stm32-gateway/control.sock`，权限0600。配置中的端口、状态和socket均可按部署环境设置。

`Restart=on-failure`，退避5秒、120秒最多5次启动；正常stop受控退出。回滚命令为 `install_service.py rollback --release RELEASE_ID`，数据库、配置与运行证据保留。每次启动验证固定源码及二进制哈希。

## 验证结果

阶段5普通、ASan/UBSan各75项，TSan存储20、MQTT9、IPC11项通过。[软件结果](../evidence/results/stage5-software.json)覆盖压力、慢客户端、分页、权限和生命周期；普通用户部署、受控子进程故障重启及回滚另行验证。

本阶段持续运行要求在阶段6最终集成版完成：连续8小时、1,437项历史返回核对通过。[结项记录](STAGE5_CLOSEOUT.md)说明版本关系。

## 复现方式

运行 `make test` / `make sanitize`覆盖IPC和存储回归；完整TSan与正式快照准备见[测试指南](TESTING.md)。
