#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SCENARIOS="${2:-24}"
MARGIN="${3:-0.08}"
FUEL_RATIO_CAP="${4:-1.10}"

ARG5="${5:-}"
ARG6="${6:-}"

if [[ "$ARG5" == "strict" || "$ARG5" == "strict-expanded" ]]; then
  PROFILE="$ARG5"
  JSON_OUT="${ARG6:-}"
else
  PROFILE="${ARG6:-strict}"
  JSON_OUT="${ARG5:-}"
fi

if [[ "$PROFILE" != "strict" && "$PROFILE" != "strict-expanded" ]]; then
  echo "invalid profile: $PROFILE (expected strict or strict-expanded)" >&2
  exit 2
fi

if [[ -z "$JSON_OUT" ]]; then
  if [[ "$PROFILE" == "strict-expanded" ]]; then
    JSON_OUT="$BUILD_DIR/recovery_slot_sweep_strict_expanded.json"
  else
    JSON_OUT="$BUILD_DIR/recovery_slot_sweep_strict.json"
  fi
fi

cmake --build "$BUILD_DIR" --target recovery_slot_gate
"$BUILD_DIR/recovery_slot_gate" \
  --sweep \
  --profile "$PROFILE" \
  --scenarios "$SCENARIOS" \
  --margin "$MARGIN" \
  --fuel-ratio-cap "$FUEL_RATIO_CAP" \
  --json-out "$JSON_OUT"

echo "recovery slot sweep: PASS"
echo "recovery slot sweep profile: $PROFILE"
echo "recovery slot sweep artifact: $JSON_OUT"
