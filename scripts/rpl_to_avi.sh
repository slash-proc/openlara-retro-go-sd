#!/usr/bin/env bash
# Thin wrapper around scripts/rpl_to_avi.py (kept for shell users).
# Usage:
#   ./scripts/rpl_to_avi.sh /path/to/FMV sd_assets/openlara/fmv
#   ./scripts/rpl_to_avi.sh FMV/CAFE.RPL sd_assets/openlara/fmv/CAFE.AVI
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ $# -eq 2 ]]; then
  exec python3 "$ROOT/scripts/rpl_to_avi.py" "$1" -o "$2"
fi
exec python3 "$ROOT/scripts/rpl_to_avi.py" "$@"
