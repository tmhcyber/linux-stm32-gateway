#!/usr/bin/env bash
set -euo pipefail
. /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 24.04 ]]; then
  echo 'This bootstrap targets Ubuntu 24.04 only.' >&2
  exit 1
fi

# This new VM's original archive connection was interrupted. Use the reachable
# USTC Ubuntu mirror; package authenticity still uses Ubuntu's archive keyring.
sources=/etc/apt/sources.list.d/ubuntu.sources
if [[ ! -e "$sources.stage1-backup" ]]; then
  sudo cp "$sources" "$sources.stage1-backup"
fi
sudo sed -i \
  -e 's|http://archive.ubuntu.com/ubuntu|https://mirrors.ustc.edu.cn/ubuntu|g' \
  -e 's|http://security.ubuntu.com/ubuntu|https://mirrors.ustc.edu.cn/ubuntu|g' \
  "$sources"
sudo dpkg --configure -a
sudo apt-get -o Acquire::Retries=2 -o Acquire::http::Timeout=30 \
  -o Acquire::https::Timeout=30 update -qq
sudo env DEBIAN_FRONTEND=noninteractive apt-get -y --no-install-recommends \
  -o Acquire::Retries=2 -o Acquire::https::Timeout=30 install \
  build-essential gdb git pkg-config python3 usbutils \
  mosquitto mosquitto-clients libmosquitto-dev
