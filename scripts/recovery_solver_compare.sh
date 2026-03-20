#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SUMMARY_JSON="${2:-$BUILD_DIR/recovery_solver_compare_summary.json}"
SCENARIOS="${3:-24}"
MARGIN="${4:-0.08}"
FUEL_RATIO_CAP="${5:-1.10}"
PROFILE="${6:-strict}"

if [[ "$PROFILE" != "strict" && "$PROFILE" != "strict-expanded" ]]; then
  echo "invalid profile: $PROFILE (expected strict or strict-expanded)" >&2
  exit 2
fi

if [[ ! "$BUILD_DIR" = /* ]]; then
  BUILD_DIR="$ROOT_DIR/${BUILD_DIR#./}"
fi
if [[ ! "$SUMMARY_JSON" = /* ]]; then
  SUMMARY_JSON="$ROOT_DIR/${SUMMARY_JSON#./}"
fi

mkdir -p "$(dirname "$SUMMARY_JSON")"

HEUR_JSON="$BUILD_DIR/recovery_slot_sweep_${PROFILE}_heuristic.json"
CW_JSON="$BUILD_DIR/recovery_slot_sweep_${PROFILE}_cw_zem.json"

echo "recovery_solver_compare_step=heuristic_sweep"
heur_exit=0
set +e
PROJECTBONK_RECOVERY_SOLVER_MODE=heuristic \
"$ROOT_DIR/scripts/recovery_slot_sweep.sh" \
  "$BUILD_DIR" \
  "$SCENARIOS" \
  "$MARGIN" \
  "$FUEL_RATIO_CAP" \
  "$HEUR_JSON" \
  "$PROFILE"
heur_exit=$?
set -e

echo "recovery_solver_compare_step=cw_zem_sweep"
cw_exit=0
set +e
PROJECTBONK_RECOVERY_SOLVER_MODE=cw_zem \
"$ROOT_DIR/scripts/recovery_slot_sweep.sh" \
  "$BUILD_DIR" \
  "$SCENARIOS" \
  "$MARGIN" \
  "$FUEL_RATIO_CAP" \
  "$CW_JSON" \
  "$PROFILE"
cw_exit=$?
set -e

python3 - <<'PY' "$HEUR_JSON" "$CW_JSON" "$SUMMARY_JSON" "$SCENARIOS" "$MARGIN" "$FUEL_RATIO_CAP" "$PROFILE" "$heur_exit" "$cw_exit"
import json
import sys

heur_path, cw_path, out_path, scenarios, margin, fuel_ratio_cap, profile, heur_exit, cw_exit = sys.argv[1:10]
heur = json.load(open(heur_path, "r", encoding="utf-8"))
cw = json.load(open(cw_path, "r", encoding="utf-8"))

def selection_summary(doc):
    sel = doc.get("selection", {})
    return {
        "status": sel.get("status"),
        "selected_candidate": sel.get("selected_candidate"),
        "selected_mean_delta_slot_error": sel.get("selected_mean_delta_slot_error"),
        "selected_mean_fuel_used_kg": sel.get("selected_mean_fuel_used_kg"),
        "selected_fuel_ratio_to_default": sel.get("selected_fuel_ratio_to_default"),
    }

heur_sel = selection_summary(heur)
cw_sel = selection_summary(cw)

heur_delta = float(heur_sel.get("selected_mean_delta_slot_error", 0.0) or 0.0)
cw_delta = float(cw_sel.get("selected_mean_delta_slot_error", 0.0) or 0.0)
heur_fuel = float(heur_sel.get("selected_mean_fuel_used_kg", 0.0) or 0.0)
cw_fuel = float(cw_sel.get("selected_mean_fuel_used_kg", 0.0) or 0.0)

payload = {
    "schema_version": 1,
  "status": "PASS" if heur_sel.get("status") == "PASS" and cw_sel.get("status") == "PASS" else "FAIL",
    "profile": {
        "name": profile,
        "scenarios": int(scenarios),
        "margin": float(margin),
        "fuel_ratio_cap": float(fuel_ratio_cap),
    },
  "command_exit_codes": {
    "heuristic_sweep": int(heur_exit),
    "cw_zem_sweep": int(cw_exit),
  },
    "heuristic": {
        "artifact": heur_path,
        **heur_sel,
    },
    "cw_zem": {
        "artifact": cw_path,
        **cw_sel,
    },
    "deltas_cw_zem_minus_heuristic": {
        "selected_mean_delta_slot_error": cw_delta - heur_delta,
        "selected_mean_fuel_used_kg": cw_fuel - heur_fuel,
    },
}

with open(out_path, "w", encoding="utf-8") as f:
    json.dump(payload, f, indent=2)
    f.write("\n")

print(f"recovery_solver_compare_status={payload['status']}")
print(f"recovery_solver_compare_summary_json={out_path}")

if payload["status"] != "PASS":
    raise SystemExit(1)
PY

echo "recovery solver compare: PASS"
