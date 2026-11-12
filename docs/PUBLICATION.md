# 公开副本与私有原件

此仓库公开的是脱敏副本。原始证据和完整本地开发历史保留在本机的忽略目录备份及原仓库中。

- 省略无关蓝牙设备、设备地址、枚举实例唯一部分、个人目录、磁盘余量、崩溃报告标识和内核地址。
- 保留 CH340 VID/PID、串口路线、必要软件版本、采集计数和解释边界。
- raw.bin 与 complete-lines.txt 保留原字节。JSON、环境清单和报告中的 publication_notice/引用说明标记脱敏；历史 SHA256 证明的是私有原件，不能拿来核验已脱敏的公开 JSON 或源码。
- 原有两个阶段 1 提交已经用脱敏内容重建，提交哈希因此变化；随后增加独立的公开安全修复提交。没有把后续阶段提前公开。

## 发布前检查

```powershell
python scripts/windows/check_public_history.py --ref HEAD
python scripts/windows/check_public_history.py --install-hook
```

检查器遍历拟推送提交的全部祖先，包含旧版文件；不会仅检查最新文件。命中私有产物或机器标识时拒绝推送，安装时不覆盖已有 hook。Git hook 是本机防误操作措施，可以被 --no-verify 绕过，不能替代 GitHub 的服务端权限控制。

保留原始证据的本地开发分支属于私有历史，不能直接推送到这个公开仓库。发布后续阶段时，先在隔离目录基于公开 main 整理对应阶段的代码、测试和脱敏证据，再用检查器验证全部历史。不要 merge/cherry-pick 会重新带入旧证据的完整树，也不要使用 git push --all。

若修改采集证据的公开副本，要保留明确的脱敏说明和私有原件；不要把脱敏副本冒充原始记录。先检查、后推送，避免再次公开后才清理。

## 新建 VM 的认证默认值

prepare_seed.py 只接受带口令的 OpenSSH 私钥，并要求私有的 SHA-512 crypt sudo 密码哈希文件。新 VM 的 sudo 要求密码，SSH 密码登录与 root SSH 登录继续禁用；SSH 转发继续只绑定 127.0.0.1。

在本机交互执行 openssl passwd -6，按提示输入新的 sudo 密码，将输出哈希保存在 .local/keys/ubuntu_sudo.sha512。不要把明文密码放进命令行参数或 Git。若已有专用于新 VM 的无口令 SSH 私钥，先在本地交互执行 ssh-keygen -p -f .local/keys/ubuntu_ed25519 设置口令；新建密钥时脚本也会交互询问口令。私钥口令可通过 SSH agent 在当前登录会话中缓存。

这些默认值针对未来新建 VM。现有开发 VM 的账户、私钥和免密自动化按用户选择保持运行；本次没有重建 VM 或修改其登录凭据。不要为应用此文档而对现有 VM 重跑 seed。
