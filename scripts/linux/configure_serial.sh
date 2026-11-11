#!/usr/bin/env bash
set -euo pipefail
port=${1:-/dev/ttyS1}
if [[ ! -c "$port" ]]; then
  echo "Serial character device not found: $port" >&2
  exit 1
fi

# This dedicated development VM has no cellular-modem workload. Its serial
# capture became clean after stopping ModemManager and preserving raw 115200.
# Do not apply this service policy to an unrelated Linux workstation.
if systemctl cat ModemManager.service >/dev/null 2>&1; then
  sudo systemctl unmask --runtime ModemManager.service
  sudo systemctl mask --now ModemManager.service
fi
stty -F "$port" 115200 raw -echo -ixon -ixoff -crtscts -hupcl clocal
stty -F "$port" -a
