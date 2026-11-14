# 构建、测试与复现

## 依赖与快速回归

Ubuntu 24.04、GCC 13、Python 3.12、SQLite 3.45、libmosquitto 2.0。安装命令见[首页](../README.md)。

```bash
make test
make sanitize
python3 scripts/windows/check_publication.py
python3 scripts/windows/check_public_history.py --ref HEAD
```

测试使用临时数据库、私有PTY与回环broker。普通和ASan/UBSan各执行C边界检查及Python全回归；公开版本同时运行新增的安全回归。历史阶段6的83项和公开版当前统计分别保存在[验收索引](../evidence/README.md)与[发布复验](../evidence/PUBLIC_RELEASE.md)。

## 准备独立源码快照

需要完整诊断验收、TSan或服务安装时，先为本次复现建立独立快照。输出目录必须位于仓库外且尚不存在：

```bash
snapshot="$HOME/gateway-public-$(date -u +%Y%m%dT%H%M%SZ)"
python3 scripts/linux/prepare_public_snapshot.py --output "$snapshot"
cd "$snapshot"
build="$HOME/project/build/$(python3 -c 'import json; print(json.load(open("SOURCE.json"))["snapshot"])')"
make BUILD="$build" test
```

准备工具复制应用、测试、配置、示例和冻结MCU参考，生成SOURCE.json及独立构建目录的同一清单。快照用于构建与验证，原仓库保持可编辑。

## 完整诊断与并发验收

在上面的快照目录中执行：

```bash
mkdir -p "$HOME/project/evidence"
python3 scripts/linux/validate_stage6.py \
  --mcu-reference "$PWD/fixtures/mcu-stage6" \
  --output "$HOME/project/evidence/public-stage6-$(date -u +%Y%m%dT%H%M%SZ)"
```

入口验证源文件哈希、MCU实际C宏0/1向量、普通/ASan全回归及TSan存储/MQTT/IPC/诊断专项。TSan通过 `setarch x86_64 -R`仅调整私有测试进程族；验证器以 `sudo -n cat /proc/sys/vm/mmap_rnd_bits`读取配置，需要该只读命令可执行。全局ASLR前后值写入结果。

单独运行TSan：

```bash
python3 scripts/linux/validate_tsan.py --build "$build" --diagnostics \
  --output "$HOME/project/evidence/public-tsan-$(date -u +%Y%m%dT%H%M%SZ)"
```

## 服务安装与实板运行

在测试通过的源码快照中，使用 `sudo python3 scripts/linux/install_service.py install --build "$build"`准备固定release，然后按[阶段5](STAGE5.md)核对配置并显式启动。

实板要求匹配v1遥测、显式开启d=1诊断的MCU固件，以及115200/8N1/raw串口。历史冻结版本与实板结果见[阶段6验收](STAGE6_FINAL.md)。更换固件或部署配置时，记录新的版本与哈希后开展对应验收。

## 历史阶段工具

`validate_stage2.py`、`validate_stage3*.py`和阶段4/5脚本保留相应阶段的调试及验收方法，其中部分参数、编译集合依赖当时版本。当前最终源码的统一复现入口为 `make test`、`make sanitize`及 `validate_stage6.py`。
