#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

cmake --build "$BUILD_DIR" --target recovery_slot_gate
"$BUILD_DIR/recovery_slot_gate"

echo "recovery slot gate: PASS"
