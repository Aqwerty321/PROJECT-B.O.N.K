#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

cmake --build "$BUILD_DIR" --target narrow_phase_false_negative_gate
"$BUILD_DIR/narrow_phase_false_negative_gate" 6 6 80

echo "narrow-phase false-negative gate: PASS"
