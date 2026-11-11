#!/usr/bin/env bash
set -euo pipefail
port=${1:-/dev/ttyS1}
if [[ ! -c "$port" ]]; then
  echo "Serial character device not found: $port" >&2
  exit 1
fi

# Daily operation: no privilege escalation or service changes.
stty -F "$port" 115200 raw cs8 -parenb -parodd -cstopb cread \
  -echo -echonl -iexten -ixon -ixoff -ixany -crtscts -hupcl clocal
stty -F "$port" -a
