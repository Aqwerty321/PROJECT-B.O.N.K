#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
RUNS="${2:-3}"
SCENARIOS="${3:-24}"
MARGIN="${4:-0.08}"
FUEL_RATIO_CAP="${5:-1.10}"

if [[ "$RUNS" -lt 2 ]]; then
  echo "determinism gate requires at least 2 runs" >&2
  exit 2
fi

cmake --build "$BUILD_DIR" --target recovery_slot_gate

strict_ref=""
expanded_ref=""

for i in $(seq 1 "$RUNS"); do
  strict_json="$BUILD_DIR/recovery_slot_sweep_strict_determinism_run${i}.json"
  expanded_json="$BUILD_DIR/recovery_slot_sweep_strict_expanded_determinism_run${i}.json"

  "$ROOT_DIR/scripts/recovery_slot_sweep.sh" "$BUILD_DIR" "$SCENARIOS" "$MARGIN" "$FUEL_RATIO_CAP" "$strict_json" strict >/dev/null
  "$ROOT_DIR/scripts/recovery_slot_sweep.sh" "$BUILD_DIR" "$SCENARIOS" "$MARGIN" "$FUEL_RATIO_CAP" "$expanded_json" strict-expanded >/dev/null

  strict_sel="$(python3 - <<'PY' "$strict_json"
import json, sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    d=json.load(f)
print(d['selection']['status'])
print(d['selection']['selected_candidate'])
PY
)"
  strict_status="$(printf '%s\n' "$strict_sel" | sed -n '1p')"
  strict_candidate="$(printf '%s\n' "$strict_sel" | sed -n '2p')"

  expanded_sel="$(python3 - <<'PY' "$expanded_json"
import json, sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    d=json.load(f)
print(d['selection']['status'])
print(d['selection']['selected_candidate'])
PY
)"
  expanded_status="$(printf '%s\n' "$expanded_sel" | sed -n '1p')"
  expanded_candidate="$(printf '%s\n' "$expanded_sel" | sed -n '2p')"

  if [[ "$strict_status" != "PASS" ]]; then
    echo "recovery sweep determinism gate: FAIL" >&2
    echo "reason=strict_profile_not_pass_run_${i}" >&2
    exit 1
  fi
  if [[ "$expanded_status" != "PASS" ]]; then
    echo "recovery sweep determinism gate: FAIL" >&2
    echo "reason=strict_expanded_profile_not_pass_run_${i}" >&2
    exit 1
  fi

  if [[ -z "$strict_ref" ]]; then
    strict_ref="$strict_candidate"
  elif [[ "$strict_candidate" != "$strict_ref" ]]; then
    echo "recovery sweep determinism gate: FAIL" >&2
    echo "reason=strict_selected_candidate_mismatch" >&2
    echo "expected=$strict_ref actual=$strict_candidate run=$i" >&2
    exit 1
  fi

  if [[ -z "$expanded_ref" ]]; then
    expanded_ref="$expanded_candidate"
  elif [[ "$expanded_candidate" != "$expanded_ref" ]]; then
    echo "recovery sweep determinism gate: FAIL" >&2
    echo "reason=strict_expanded_selected_candidate_mismatch" >&2
    echo "expected=$expanded_ref actual=$expanded_candidate run=$i" >&2
    exit 1
  fi

  echo "run_${i}_strict_selected_candidate=$strict_candidate"
  echo "run_${i}_strict_expanded_selected_candidate=$expanded_candidate"
done

echo "strict_selected_candidate_reference=$strict_ref"
echo "strict_expanded_selected_candidate_reference=$expanded_ref"
echo "recovery_sweep_determinism_gate_result=PASS"
