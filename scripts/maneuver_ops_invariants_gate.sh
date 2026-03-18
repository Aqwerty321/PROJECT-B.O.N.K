#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

cmake --build "$BUILD_DIR" --target maneuver_ops_invariants_gate
"$BUILD_DIR/maneuver_ops_invariants_gate"

echo "maneuver ops invariants gate: PASS"
