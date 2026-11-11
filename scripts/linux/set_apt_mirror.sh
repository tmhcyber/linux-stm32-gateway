#!/usr/bin/env bash
# Explicit optional maintenance; never called by dependency installation.
set -euo pipefail
if [[ ${1:-} != --ustc ]]; then
  echo "Usage: bash $0 --ustc (Ubuntu 24.04 only)" >&2; exit 2
fi
. /etc/os-release
[[ "$ID" == ubuntu && "$VERSION_ID" == 24.04 ]] || exit 1
sources=/etc/apt/sources.list.d/ubuntu.sources
if [[ ! -e "$sources.stage1-backup" ]]; then
  sudo cp "$sources" "$sources.stage1-backup"
fi
sudo sed -i \
  -e 's|http://archive.ubuntu.com/ubuntu|https://mirrors.ustc.edu.cn/ubuntu|g' \
  -e 's|http://security.ubuntu.com/ubuntu|https://mirrors.ustc.edu.cn/ubuntu|g' "$sources"
