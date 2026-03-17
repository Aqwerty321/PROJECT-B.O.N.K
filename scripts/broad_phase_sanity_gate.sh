#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

cmake --build "$BUILD_DIR" --target broad_phase_validation -j"$(nproc)"
"$BUILD_DIR/broad_phase_validation" 3 50 10000 1

echo "broad-phase sanity gate: PASS"
