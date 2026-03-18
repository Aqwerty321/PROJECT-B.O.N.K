#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

cmake --build "$BUILD_DIR" --target recovery_planner_invariants
"$BUILD_DIR/recovery_planner_invariants"

echo "recovery planner invariants gate: PASS"
