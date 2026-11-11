#!/usr/bin/env bash
# Read-only: enumeration never substitutes for requirements.
set -uo pipefail
scope=${1:-}
port=${2:-/dev/ttyS1}
failures=0
fail() { echo "FAIL: $*" >&2; failures=$((failures + 1)); }
need() { command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"; }
case "$scope" in
  serial)
    need python3; need stty; need systemctl
    [[ -c "$port" ]] || fail "not a character device: $port"
    [[ -r "$port" ]] || fail "current user cannot read: $port"
    for unit in "serial-getty@$(basename "$port").service" ModemManager.service; do
      state=$(systemctl is-active "$unit" 2>/dev/null) || :
      case "$state" in
        inactive|failed|unknown) echo "INFO: $unit $state" ;;
        active|activating|reloading) fail "$unit is $state (may own/probe serial port)" ;;
        *) fail "cannot determine $unit state: $state" ;;
      esac
    done
    echo 'INFO: device enumeration (auxiliary only)'
    ls -l "$port" 2>/dev/null || :
    ;;
  storage)
    for cmd in gcc make pkg-config sqlite3; do need "$cmd"; done
    pkg-config --exists sqlite3 2>/dev/null || fail 'SQLite development dependency unavailable (libsqlite3-dev/pkg-config)'
    ;;
  mqtt)
    for cmd in gcc make pkg-config mosquitto_pub mosquitto_sub systemctl ss; do need "$cmd"; done
    pkg-config --exists libmosquitto 2>/dev/null || fail 'libmosquitto development dependency unavailable'
    systemctl is-active --quiet mosquitto.service || fail 'mosquitto.service is not active'
    listeners=$(ss -H -ltn '( sport = :1883 )' 2>/dev/null) || fail 'cannot inspect TCP listeners'
    if ! awk '$4 == "127.0.0.1:1883" || $4 == "[::1]:1883" {found=1} END {exit !found}' <<< "${listeners:-}"; then
      fail 'no loopback TCP listener on port 1883'
    fi
    ;;
  *) echo "Usage: bash $0 {serial [device]|storage|mqtt}" >&2; exit 2 ;;
esac
if (( failures )); then
  echo "FAILED: $scope ($failures requirements)" >&2
  exit 1
fi
echo "READY: $scope"
