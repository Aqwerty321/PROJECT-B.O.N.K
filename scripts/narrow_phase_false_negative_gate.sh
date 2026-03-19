#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

SCENARIOS="${2:-10}"
SATELLITES="${3:-8}"
DEBRIS="${4:-120}"

cmake --build "$BUILD_DIR" --target narrow_phase_false_negative_gate
"$BUILD_DIR/narrow_phase_false_negative_gate" "$SCENARIOS" "$SATELLITES" "$DEBRIS"

echo "narrow-phase false-negative gate: PASS"
