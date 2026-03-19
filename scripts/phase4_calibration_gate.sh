#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SUMMARY_JSON="${2:-$BUILD_DIR/phase4_calibration_gate_summary.json}"

if [[ ! "$BUILD_DIR" = /* ]]; then
  BUILD_DIR="$ROOT_DIR/${BUILD_DIR#./}"
fi
if [[ ! "$SUMMARY_JSON" = /* ]]; then
  SUMMARY_JSON="$ROOT_DIR/${SUMMARY_JSON#./}"
fi

mkdir -p "$(dirname "$SUMMARY_JSON")"

declare -A STEP_RESULT
STEP_ORDER=(
  recovery_sweep_determinism
  narrow_phase_uncertainty_observability
  phase2_regression
  broad_phase_sanity
  narrow_phase_false_negative
  maneuver_ops_invariants
  recovery_slot
  recovery_planner_invariants
  api_contract
  ctest
)

for key in "${STEP_ORDER[@]}"; do
  STEP_RESULT["$key"]="NOT_RUN"
done

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

write_summary() {
  local overall="$1"
  local failed_step="$2"

  {
    printf '{\n'
    printf '  "overall":"%s",\n' "$overall"
    printf '  "failed_step":"%s",\n' "$failed_step"
    printf '  "build_dir":"%s",\n' "$BUILD_DIR"
    printf '  "steps":{\n'

    local idx=0
    local total="${#STEP_ORDER[@]}"
    for key in "${STEP_ORDER[@]}"; do
      idx=$((idx + 1))
      local comma=","
      if [[ "$idx" -eq "$total" ]]; then
        comma=""
      fi
      printf '    "%s":"%s"%s\n' "$key" "${STEP_RESULT[$key]}" "$comma"
    done

    printf '  }\n'
    printf '}\n'
  } > "$SUMMARY_JSON"
}

run_step() {
  local key="$1"
  shift

  local log_file="$TMP_DIR/${key}.log"
  printf 'phase4_calibration_gate_step=%s\n' "$key"
  if "$@" >"$log_file" 2>&1; then
    cat "$log_file"
    STEP_RESULT["$key"]="PASS"
    return 0
  fi

  cat "$log_file"
  STEP_RESULT["$key"]="FAIL"
  write_summary "FAIL" "$key"
  printf 'phase4_calibration_gate_result=FAIL\n'
  printf 'phase4_calibration_gate_failed_step=%s\n' "$key"
  printf 'phase4_calibration_gate_summary_json=%s\n' "$SUMMARY_JSON"
  return 1
}

run_step recovery_sweep_determinism \
  "$ROOT_DIR/scripts/recovery_sweep_determinism_gate.sh" \
  "$BUILD_DIR" \
  3 \
  24 \
  0.08 \
  1.10

run_step narrow_phase_uncertainty_observability \
  "$ROOT_DIR/scripts/narrow_phase_uncertainty_observability_gate.sh" \
  "$BUILD_DIR"

run_step phase2_regression "$ROOT_DIR/scripts/phase2_regression_gate.sh" "$BUILD_DIR"
run_step broad_phase_sanity "$ROOT_DIR/scripts/broad_phase_sanity_gate.sh" "$BUILD_DIR"
run_step narrow_phase_false_negative "$ROOT_DIR/scripts/narrow_phase_false_negative_gate.sh" "$BUILD_DIR"
run_step maneuver_ops_invariants "$ROOT_DIR/scripts/maneuver_ops_invariants_gate.sh" "$BUILD_DIR"
run_step recovery_slot "$ROOT_DIR/scripts/recovery_slot_gate.sh" "$BUILD_DIR"
run_step recovery_planner_invariants "$ROOT_DIR/scripts/recovery_planner_invariants_gate.sh" "$BUILD_DIR"
run_step api_contract "$ROOT_DIR/scripts/api_contract_gate.sh" "$BUILD_DIR"
run_step ctest ctest --test-dir "$BUILD_DIR" --output-on-failure

write_summary "PASS" ""
printf 'phase4_calibration_gate_result=PASS\n'
printf 'phase4_calibration_gate_summary_json=%s\n' "$SUMMARY_JSON"
printf 'phase4 calibration gate: PASS\n'
