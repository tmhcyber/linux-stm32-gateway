#!/usr/bin/env bash
set -euo pipefail
if [[ ${1:-} != --dedicated-vm ]]; then
  echo "Usage: bash $0 --dedicated-vm" >&2
  echo 'Only for the dedicated serial VM without cellular-modem workloads.' >&2
  exit 2
fi
# One-time opt-in policy, separate from unprivileged daily operation.
sudo systemctl mask --now ModemManager.service
