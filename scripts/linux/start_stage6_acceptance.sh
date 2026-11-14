#!/usr/bin/env bash
# Manual entry copied into the fixed release. Never schedules or auto-starts a run.
set -euo pipefail
if [[ $# -ne 1 || "$1" != /* ]]; then
  echo 'Usage: bash /opt/stm32-gateway/releases/RELEASE/acceptance/start_stage6_acceptance.sh /absolute/new/output' >&2
  exit 2
fi
if [[ $(TZ=Asia/Taipei date +%F) < 2026-11-14 ]]; then
  echo 'The requested eight-hour window is deferred to 2026-11-14 (Asia/Taipei).' >&2
  exit 2
fi
release=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
if [[ "$release" != /opt/stm32-gateway/releases/* || ! -f "$release/FINAL.json" ]]; then
  echo 'Use the entry inside the pinned installed release.' >&2
  exit 2
fi
exec sudo -n python3 "$release/acceptance/accept_stage6_final.py" \
  --release "$release" --output "$1" --seconds 28800
