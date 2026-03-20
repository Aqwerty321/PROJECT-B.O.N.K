#!/usr/bin/env bash
# hard_filter_canary_gate.sh
#
# Combined hard-filter canary that runs the FN gate with both plane-phase
# and MOID hard filters ON at multiple threshold profiles:
#   1. Production thresholds (the values that would ship if defaults flipped)
#   2. Stress thresholds   (tightened to push rejection rates higher)
#
# Tests both proxy and HF MOID modes at each profile.
#
# Exit 0 iff ALL runs show zero false negatives.  Emits a single JSON
# evidence file suitable for documenting promotion readiness.
#
# Usage:
#   bash scripts/hard_filter_canary_gate.sh [BUILD_DIR] [SUMMARY_JSON] [SCENARIOS] [SATS] [DEBRIS]
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SUMMARY_JSON="${2:-$BUILD_DIR/hard_filter_canary_gate_summary.json}"
SCENARIOS="${3:-12}"
SATELLITES="${4:-10}"
DEBRIS="${5:-160}"

if [[ ! "$BUILD_DIR" = /* ]]; then
  BUILD_DIR="$ROOT_DIR/${BUILD_DIR#./}"
fi
if [[ ! "$SUMMARY_JSON" = /* ]]; then
  SUMMARY_JSON="$ROOT_DIR/${SUMMARY_JSON#./}"
fi

mkdir -p "$(dirname "$SUMMARY_JSON")"

TMP_DIR="$(mktemp -d)"
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT

# ---------- build ----------------------------------------------------------
echo "hard_filter_canary_gate_step=build"
cmake --build "$BUILD_DIR" --target narrow_phase_false_negative_gate

# ---------- helper to run one profile -------------------------------------
# run_profile <label> <moid_mode> <moid_threshold_km> <out_json>
run_profile() {
  local label="$1" moid_mode="$2" threshold_km="$3" out_json="$4"
  local logfile="$TMP_DIR/${label}.log"

  echo "hard_filter_canary_gate_step=run_$label"

  PROJECTBONK_NARROW_PLANE_PHASE_SHADOW=1 \
  PROJECTBONK_NARROW_PLANE_PHASE_FILTER=1 \
  PROJECTBONK_NARROW_MOID_SHADOW=1 \
  PROJECTBONK_NARROW_MOID_FILTER=1 \
  PROJECTBONK_NARROW_MOID_MODE="$moid_mode" \
  PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM="$threshold_km" \
  "$BUILD_DIR/narrow_phase_false_negative_gate" "$SCENARIOS" "$SATELLITES" "$DEBRIS" \
    | tee "$logfile"

  # Parse structured metrics from gate output into JSON
  python3 - <<'PY' "$logfile" "$out_json" "$label" "$moid_mode" "$threshold_km"
import json, re, sys

log_path, out_path, label, moid_mode, threshold_km = sys.argv[1:6]
text = open(log_path, "r", encoding="utf-8").read()

def metric_int(key):
    m = re.search(rf'^{re.escape(key)}=(\d+)$', text, re.M)
    return int(m.group(1)) if m else None

def metric_str(key):
    m = re.search(rf'^{re.escape(key)}=(\S+)$', text, re.M)
    return m.group(1) if m else None

family_metrics = {}
for m in re.finditer(
    r'^family_([a-z0-9_]+)_(scenarios|reference_collision_sats_total'
    r'|production_collision_sats_total|false_negative_sats_total)=(\d+)$',
    text, re.M
):
    fam = m.group(1)
    if fam not in family_metrics:
        family_metrics[fam] = {}
    family_metrics[fam][m.group(2)] = int(m.group(3))

fn = metric_int("false_negative_sats_total")
gate_result = metric_str("narrow_phase_false_negative_gate_result")

payload = {
    "label": label,
    "moid_mode": moid_mode,
    "moid_reject_threshold_km": float(threshold_km),
    "gate_result": gate_result,
    "false_negative_sats_total": fn,
    "reference_collision_sats_total": metric_int("reference_collision_sats_total"),
    "production_collision_sats_total": metric_int("production_collision_sats_total"),
    "narrow_plane_phase_hard_rejected_pairs_total": metric_int("narrow_plane_phase_hard_rejected_pairs_total"),
    "narrow_plane_phase_fail_open_pairs_total": metric_int("narrow_plane_phase_fail_open_pairs_total"),
    "narrow_moid_evaluated_pairs_total": metric_int("narrow_moid_evaluated_pairs_total"),
    "narrow_moid_shadow_rejected_pairs_total": metric_int("narrow_moid_shadow_rejected_pairs_total"),
    "narrow_moid_hard_rejected_pairs_total": metric_int("narrow_moid_hard_rejected_pairs_total"),
    "narrow_moid_fail_open_pairs_total": metric_int("narrow_moid_fail_open_pairs_total"),
    "narrow_uncertainty_promoted_pairs_total": metric_int("narrow_uncertainty_promoted_pairs_total"),
    "families": family_metrics,
    "status": "PASS" if (gate_result == "PASS" and fn is not None and fn == 0) else "FAIL",
}

with open(out_path, "w", encoding="utf-8") as f:
    json.dump(payload, f, indent=2)
    f.write("\n")

if payload["status"] != "PASS":
    print(f"hard_filter_canary_profile_{label}=FAIL", file=sys.stderr)
    sys.exit(1)
print(f"hard_filter_canary_profile_{label}=PASS")
PY
}

# ---------- run profiles ---------------------------------------------------
# Production thresholds (what would ship if defaults flipped to ON)
run_profile "prod_proxy" "proxy" "2.0"   "$TMP_DIR/prod_proxy.json"
run_profile "prod_hf"    "hf"    "2.0"   "$TMP_DIR/prod_hf.json"

# Stress thresholds (tightened to exercise rejection paths harder)
run_profile "stress_proxy" "proxy" "0.05" "$TMP_DIR/stress_proxy.json"
run_profile "stress_hf"    "hf"    "0.05" "$TMP_DIR/stress_hf.json"

# ---------- aggregate -------------------------------------------------------
echo "hard_filter_canary_gate_step=aggregate"

python3 - <<'PY' "$TMP_DIR" "$SUMMARY_JSON" "$SCENARIOS" "$SATELLITES" "$DEBRIS"
import json, sys, os

tmp_dir, summary_path, scenarios, sats, debris = sys.argv[1:6]

profiles = []
all_pass = True
for name in ["prod_proxy", "prod_hf", "stress_proxy", "stress_hf"]:
    path = os.path.join(tmp_dir, f"{name}.json")
    p = json.load(open(path, "r", encoding="utf-8"))
    profiles.append(p)
    if p["status"] != "PASS":
        all_pass = False

# Check coverage: hard-reject paths must have been exercised at stress level
stress_profiles = [p for p in profiles if "stress" in p["label"]]
hard_reject_exercised = all(
    (p.get("narrow_moid_hard_rejected_pairs_total") or 0) > 0
    for p in stress_profiles
)
plane_reject_exercised = all(
    (p.get("narrow_plane_phase_hard_rejected_pairs_total") or 0) > 0
    for p in profiles
)

payload = {
    "schema_version": 1,
    "status": "PASS" if all_pass else "FAIL",
    "test_config": {
        "scenarios": int(scenarios),
        "satellites": int(sats),
        "debris": int(debris),
    },
    "profiles": {p["label"]: p for p in profiles},
    "coverage": {
        "stress_moid_hard_reject_exercised": hard_reject_exercised,
        "plane_phase_hard_reject_exercised": plane_reject_exercised,
        "profiles_tested": len(profiles),
        "profiles_passed": sum(1 for p in profiles if p["status"] == "PASS"),
    },
    "evidence_summary": {
        "total_false_negatives": sum(p.get("false_negative_sats_total") or 0 for p in profiles),
        "total_reference_collisions": sum(p.get("reference_collision_sats_total") or 0 for p in profiles),
        "total_production_collisions": sum(p.get("production_collision_sats_total") or 0 for p in profiles),
    },
}

with open(summary_path, "w", encoding="utf-8") as f:
    json.dump(payload, f, indent=2)
    f.write("\n")

print(f"hard_filter_canary_gate_status={payload['status']}")
print(f"hard_filter_canary_gate_summary_json={summary_path}")
print(f"hard_filter_canary_gate_profiles_tested={len(profiles)}")
print(f"hard_filter_canary_gate_profiles_passed={payload['coverage']['profiles_passed']}")
print(f"hard_filter_canary_gate_total_fn={payload['evidence_summary']['total_false_negatives']}")
print(f"hard_filter_canary_gate_stress_moid_reject_exercised={hard_reject_exercised}")
print(f"hard_filter_canary_gate_plane_reject_exercised={plane_reject_exercised}")

if payload["status"] != "PASS":
    print("hard_filter_canary_gate=FAIL", file=sys.stderr)
    raise SystemExit(1)

print("hard_filter_canary_gate=PASS")
PY

echo "hard-filter canary gate: PASS"
