#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SCENARIOS="${2:-6}"
MARGIN="${3:-0.1}"

cmake --build "$BUILD_DIR" --target recovery_slot_gate
"$BUILD_DIR/recovery_slot_gate" --sweep --scenarios "$SCENARIOS" --margin "$MARGIN"

echo "recovery slot sweep: PASS"
