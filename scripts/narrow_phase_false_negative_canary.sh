#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SUMMARY_JSON="${2:-$BUILD_DIR/narrow_phase_false_negative_canary_summary.json}"
SCENARIOS="${3:-10}"
SATELLITES="${4:-8}"
DEBRIS="${5:-120}"

if [[ ! "$BUILD_DIR" = /* ]]; then
  BUILD_DIR="$ROOT_DIR/${BUILD_DIR#./}"
fi
if [[ ! "$SUMMARY_JSON" = /* ]]; then
  SUMMARY_JSON="$ROOT_DIR/${SUMMARY_JSON#./}"
fi

mkdir -p "$(dirname "$SUMMARY_JSON")"

LOG_FILE="$(mktemp)"
trap 'rm -f "$LOG_FILE"' EXIT

cmake --build "$BUILD_DIR" --target narrow_phase_false_negative_gate

PROJECTBONK_NARROW_PLANE_PHASE_SHADOW=1 \
PROJECTBONK_NARROW_PLANE_PHASE_FILTER=1 \
PROJECTBONK_NARROW_MOID_SHADOW=1 \
PROJECTBONK_NARROW_MOID_FILTER=1 \
"$BUILD_DIR/narrow_phase_false_negative_gate" "$SCENARIOS" "$SATELLITES" "$DEBRIS" | tee "$LOG_FILE"

python3 - <<'PY' "$LOG_FILE" "$SUMMARY_JSON"
import json
import re
import sys

log_path = sys.argv[1]
summary_path = sys.argv[2]

text = open(log_path, "r", encoding="utf-8").read()

metrics = {}
for m in re.finditer(r'^([a-z0-9_]+)=(\S+)$', text, re.M):
    metrics[m.group(1)] = m.group(2)

family_metrics = {}
for m in re.finditer(r'^family_([a-z0-9_]+)_(scenarios|reference_collision_sats_total|production_collision_sats_total|false_negative_sats_total)=(\d+)$', text, re.M):
    family = m.group(1)
    key = m.group(2)
    val = int(m.group(3))
    if family not in family_metrics:
        family_metrics[family] = {}
    family_metrics[family][key] = val

required = [
    "false_negative_sats_total",
    "reference_collision_sats_total",
    "production_collision_sats_total",
    "narrow_uncertainty_promoted_pairs_total",
    "narrow_plane_phase_hard_rejected_pairs_total",
    "narrow_plane_phase_fail_open_pairs_total",
    "narrow_moid_evaluated_pairs_total",
    "narrow_moid_shadow_rejected_pairs_total",
    "narrow_moid_hard_rejected_pairs_total",
    "narrow_moid_fail_open_pairs_total",
    "narrow_phase_false_negative_gate_result",
]
missing = [k for k in required if k not in metrics]
if missing:
    print("narrow_phase_false_negative_canary_result=FAIL", file=sys.stderr)
    print("reason=missing_required_metrics", file=sys.stderr)
    print("missing_keys=" + ",".join(missing), file=sys.stderr)
    raise SystemExit(1)

result = metrics["narrow_phase_false_negative_gate_result"]
fn_total = int(metrics["false_negative_sats_total"])

payload = {
    "schema_version": 2,
    "status": "PASS" if (result == "PASS" and fn_total == 0) else "FAIL",
    "mode": "hard_filter_canary",
    "env": {
        "PROJECTBONK_NARROW_PLANE_PHASE_SHADOW": 1,
        "PROJECTBONK_NARROW_PLANE_PHASE_FILTER": 1,
        "PROJECTBONK_NARROW_MOID_SHADOW": 1,
        "PROJECTBONK_NARROW_MOID_FILTER": 1,
    },
    "metrics": {
        "false_negative_sats_total": fn_total,
        "reference_collision_sats_total": int(metrics["reference_collision_sats_total"]),
        "production_collision_sats_total": int(metrics["production_collision_sats_total"]),
        "narrow_uncertainty_promoted_pairs_total": int(metrics["narrow_uncertainty_promoted_pairs_total"]),
        "narrow_plane_phase_hard_rejected_pairs_total": int(metrics["narrow_plane_phase_hard_rejected_pairs_total"]),
        "narrow_plane_phase_fail_open_pairs_total": int(metrics["narrow_plane_phase_fail_open_pairs_total"]),
        "narrow_moid_evaluated_pairs_total": int(metrics["narrow_moid_evaluated_pairs_total"]),
        "narrow_moid_shadow_rejected_pairs_total": int(metrics["narrow_moid_shadow_rejected_pairs_total"]),
        "narrow_moid_hard_rejected_pairs_total": int(metrics["narrow_moid_hard_rejected_pairs_total"]),
        "narrow_moid_fail_open_pairs_total": int(metrics["narrow_moid_fail_open_pairs_total"]),
    },
    "families": family_metrics,
}

with open(summary_path, "w", encoding="utf-8") as f:
    json.dump(payload, f, indent=2)
    f.write("\n")

if payload["status"] != "PASS":
    print("narrow_phase_false_negative_canary_result=FAIL", file=sys.stderr)
    print("reason=canary_false_negative_or_gate_failure", file=sys.stderr)
    print(f"summary_json={summary_path}", file=sys.stderr)
    raise SystemExit(1)

print("narrow_phase_false_negative_canary_result=PASS")
print(f"summary_json={summary_path}")
PY

echo "narrow-phase false-negative canary: PASS"
