# 公开材料与复现说明

## 仓库内容

公开版包含阶段1—6应用、测试、配置、部署工具、协议样例和精选验收结果。原始长测流、完整历史日志、私有数据库及硬件转储保存在本地归档。

[MANIFEST](../evidence/MANIFEST.json)逐项记录精选证据的来源路径、原件SHA256、公开文件SHA256及处理类型：`original`为逐字节原件，`redacted_copy`为移除个人路径或机器标识的副本，派生样例列出输入。旧阶段1材料沿用原有脱敏标记。

[SOURCE_CODE](../evidence/SOURCE_CODE.json)提供最终应用/测试输入的原件哈希。原开发提交值作为来源标识，源码通过当前公开目录访问。公开元数据的SHA256用于校验公开副本，历史哈希用于对应原归档。

## 发布检查

```bash
python3 scripts/windows/check_publication.py
python3 scripts/windows/check_public_history.py --ref HEAD
```

完整祖先历史检查覆盖私有产物、凭据及个人设备标识。本地pre-push hook运行同一检查器；阶段2—6通过公开main上的新提交发布。

## VM与认证

Windows辅助脚本面向已有VirtualBox环境；`.local/`保存VM、密钥和SSH配置。新建VM时，`prepare_seed.py`要求加密OpenSSH私钥及私有SHA-512 crypt sudo密码哈希文件；sudo需要密码，SSH仅允许密钥登录，转发绑定127.0.0.1。

首次准备可在本机交互运行 `openssl passwd -6`，将生成值保存到 `.local/keys/ubuntu_sudo.sha512`，然后执行 `New-UbuntuVm.ps1`。已有私钥可通过 `ssh-keygen -p`设置口令，SSH agent可缓存本机会话口令。既有开发VM的运行配置由本地环境管理。
