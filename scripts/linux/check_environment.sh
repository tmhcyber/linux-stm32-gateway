#!/usr/bin/env bash
set -euo pipefail
printf '=== OS ===\n'
cat /etc/os-release
uname -m
printf '\n=== Toolchain ===\n'
gcc --version | sed -n '1p'
make --version | sed -n '1p'
gdb --version | sed -n '1p'
python3 --version
pkg-config --modversion libmosquitto
printf '\n=== Packages ===\n'
dpkg-query -W build-essential gdb git pkg-config python3 usbutils mosquitto mosquitto-clients libmosquitto-dev
printf '\n=== Account ===\n'
id
printf '\n=== USB and serial ===\n'
lsusb || true  # Guest USB controllers are intentionally disabled on this host.
find /dev/serial -maxdepth 2 -type l -printf '%p -> %l\n' 2>/dev/null || true
find /dev -maxdepth 1 -name 'ttyUSB*' -printf '%p\n'
printf '\n=== Host serial mapping ===\n'
ls -l /dev/ttyS1 2>/dev/null || true
printf '\n=== Broker listener (observation only) ===\n'
ss -ltn '( sport = :1883 )'
