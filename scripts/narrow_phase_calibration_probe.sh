#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SATELLITES="${2:-50}"
DEBRIS="${3:-10000}"
TICKS="${4:-5}"
STEP_S="${5:-30}"

# Optional narrow-phase tuning args (pass-through)
HIGH_REL_SPEED_KM_S="${6:-8.0}"
HIGH_REL_SPEED_EXTRA_BAND_KM="${7:-0.10}"
FULL_REFINE_BUDGET_BASE="${8:-64}"
FULL_REFINE_BUDGET_MIN="${9:-8}"
FULL_REFINE_BUDGET_MAX="${10:-192}"
FULL_REFINE_BAND_KM="${11:-0.20}"
REFINE_BAND_KM="${12:-0.10}"
TCA_GUARD_KM="${13:-0.02}"
FULL_REFINE_SAMPLES="${14:-16}"
FULL_REFINE_SUBSTEP_S="${15:-1.0}"
MICRO_REFINE_MAX_STEP_S="${16:-5.0}"

cmake --build "$BUILD_DIR" --target narrow_phase_calibration_probe

"$BUILD_DIR/narrow_phase_calibration_probe" \
  "$SATELLITES" \
  "$DEBRIS" \
  "$TICKS" \
  "$STEP_S" \
  "$HIGH_REL_SPEED_KM_S" \
  "$HIGH_REL_SPEED_EXTRA_BAND_KM" \
  "$FULL_REFINE_BUDGET_BASE" \
  "$FULL_REFINE_BUDGET_MIN" \
  "$FULL_REFINE_BUDGET_MAX" \
  "$FULL_REFINE_BAND_KM" \
  "$REFINE_BAND_KM" \
  "$TCA_GUARD_KM" \
  "$FULL_REFINE_SAMPLES" \
  "$FULL_REFINE_SUBSTEP_S" \
  "$MICRO_REFINE_MAX_STEP_S"

echo "narrow-phase calibration probe: PASS"
