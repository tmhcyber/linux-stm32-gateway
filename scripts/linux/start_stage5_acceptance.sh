#!/usr/bin/env bash
# Manual next-day entry: one NEW continuous 8-hour window, sampled every 60s.
set -euo pipefail
if [[ $# -ne 1 || "$1" != /* ]]; then
  echo "Usage: bash scripts/linux/start_stage5_acceptance.sh /absolute/new/evidence-directory" >&2
  exit 2
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
release=/opt/stm32-gateway/releases/20261112T074342Z-8bb665a4
if [[ -e "$1" || ! -d "$(dirname -- "$1")" ]]; then
  echo "Output must be new and its parent must exist: $1" >&2
  exit 2
fi
state=$(systemctl is-active stm32-gateway.service || true)
if [[ "$state" != inactive ]]; then
  echo "Inspect service before starting a new window: $state" >&2
  exit 1
fi
if sudo -n fuser -v /dev/ttyS1; then
  echo "Serial port is occupied; inspect the owner. No process was stopped." >&2
  exit 1
else
  result=$?
  if [[ $result -ne 1 ]]; then exit "$result"; fi
fi
bash "$root/scripts/linux/configure_serial.sh" /dev/ttyS1
exec sudo -n python3 "$root/scripts/linux/accept_stage5_board.py" \
  --release "$release" --output "$1" --seconds 28800
