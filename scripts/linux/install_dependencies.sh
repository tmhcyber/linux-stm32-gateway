#!/usr/bin/env bash
set -euo pipefail
. /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 24.04 ]]; then
  echo 'This bootstrap targets Ubuntu 24.04 only.' >&2
  exit 1
fi

case ${1:-} in
  serial) packages=(python3 usbutils) ;;
  storage) packages=(sqlite3 libsqlite3-dev) ;;
  mqtt) packages=(mosquitto mosquitto-clients libmosquitto-dev) ;;
  *) echo "Usage: bash $0 {serial|storage|mqtt}" >&2; exit 2 ;;
esac
# Preserve the administrator's apt sources. Package repair is a separate action.
sudo apt-get -o Acquire::Retries=2 -o Acquire::http::Timeout=30 \
  -o Acquire::https::Timeout=30 update -qq
sudo env DEBIAN_FRONTEND=noninteractive apt-get -y --no-install-recommends \
  -o Acquire::Retries=2 -o Acquire::https::Timeout=30 install \
  build-essential gdb git pkg-config "${packages[@]}"
