#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SCENARIOS="${2:-24}"
MARGIN="${3:-0.08}"
FUEL_RATIO_CAP="${4:-1.10}"
JSON_OUT="${5:-$BUILD_DIR/recovery_slot_sweep_strict.json}"

cmake --build "$BUILD_DIR" --target recovery_slot_gate
"$BUILD_DIR/recovery_slot_gate" \
  --sweep \
  --scenarios "$SCENARIOS" \
  --margin "$MARGIN" \
  --fuel-ratio-cap "$FUEL_RATIO_CAP" \
  --json-out "$JSON_OUT"

echo "recovery slot sweep: PASS"
echo "recovery slot sweep artifact: $JSON_OUT"
