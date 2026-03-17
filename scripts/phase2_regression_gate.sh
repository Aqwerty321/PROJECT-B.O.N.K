#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

cmake --build "$BUILD_DIR" --target phase2_regression -j"$(nproc)"

"$BUILD_DIR/phase2_regression" 3000 300 86400 --strict-adaptive
"$BUILD_DIR/phase2_regression" 3000 1 120 --strict-adaptive

echo "phase2 regression gate: PASS"
