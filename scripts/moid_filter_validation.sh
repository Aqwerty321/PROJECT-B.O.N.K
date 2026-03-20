#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SUMMARY_JSON="${2:-$BUILD_DIR/moid_filter_validation_summary.json}"
SCENARIOS="${3:-13}"
SATELLITES="${4:-10}"
DEBRIS="${5:-160}"
MOID_THRESHOLD_STRESS_KM="${MOID_THRESHOLD_STRESS_KM:-0.05}"

if [[ ! "$BUILD_DIR" = /* ]]; then
  BUILD_DIR="$ROOT_DIR/${BUILD_DIR#./}"
fi
if [[ ! "$SUMMARY_JSON" = /* ]]; then
  SUMMARY_JSON="$ROOT_DIR/${SUMMARY_JSON#./}"
fi

mkdir -p "$(dirname "$SUMMARY_JSON")"

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

PROXY_LOG="$TMP_DIR/canary_proxy.log"
HF_LOG="$TMP_DIR/canary_hf.log"
OBS_HF_LOG="$TMP_DIR/obs_hf.log"
PROXY_JSON="$TMP_DIR/canary_proxy_summary.json"
HF_JSON="$TMP_DIR/canary_hf_summary.json"

echo "moid_filter_validation_step=build_target"
cmake --build "$BUILD_DIR" --target narrow_phase_false_negative_gate narrow_phase_calibration_probe

echo "moid_filter_validation_step=proxy_canary"
PROJECTBONK_NARROW_MOID_MODE=proxy \
PROJECTBONK_NARROW_PLANE_PHASE_SHADOW=1 \
PROJECTBONK_NARROW_PLANE_PHASE_FILTER=1 \
PROJECTBONK_NARROW_MOID_SHADOW=1 \
PROJECTBONK_NARROW_MOID_FILTER=1 \
PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM="$MOID_THRESHOLD_STRESS_KM" \
bash "$ROOT_DIR/scripts/narrow_phase_false_negative_canary.sh" \
  "$BUILD_DIR" \
  "$PROXY_JSON" \
  "$SCENARIOS" \
  "$SATELLITES" \
  "$DEBRIS" | tee "$PROXY_LOG"

echo "moid_filter_validation_step=hf_canary"
PROJECTBONK_NARROW_MOID_MODE=hf \
PROJECTBONK_NARROW_PLANE_PHASE_SHADOW=1 \
PROJECTBONK_NARROW_PLANE_PHASE_FILTER=1 \
PROJECTBONK_NARROW_MOID_SHADOW=1 \
PROJECTBONK_NARROW_MOID_FILTER=1 \
PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM="$MOID_THRESHOLD_STRESS_KM" \
bash "$ROOT_DIR/scripts/narrow_phase_false_negative_canary.sh" \
  "$BUILD_DIR" \
  "$HF_JSON" \
  "$SCENARIOS" \
  "$SATELLITES" \
  "$DEBRIS" | tee "$HF_LOG"

echo "moid_filter_validation_step=hf_observability"
PROJECTBONK_NARROW_MOID_MODE=hf \
"$ROOT_DIR/scripts/narrow_phase_uncertainty_observability_gate.sh" \
  "$BUILD_DIR" | tee "$OBS_HF_LOG"

python3 - <<'PY' "$PROXY_JSON" "$HF_JSON" "$OBS_HF_LOG" "$SUMMARY_JSON" "$SCENARIOS" "$SATELLITES" "$DEBRIS" "$MOID_THRESHOLD_STRESS_KM"
import json
import re
import sys

proxy_json_path, hf_json_path, obs_hf_log_path, summary_path, scenarios, sats, debris, moid_threshold_stress = sys.argv[1:9]

proxy = json.load(open(proxy_json_path, "r", encoding="utf-8"))
hf = json.load(open(hf_json_path, "r", encoding="utf-8"))
obs_txt = open(obs_hf_log_path, "r", encoding="utf-8").read()

def metric(txt: str, key: str) -> int:
    m = re.search(rf'^{re.escape(key)}=(\d+)$', txt, re.M)
    if not m:
        raise SystemExit(f"missing_metric={key}")
    return int(m.group(1))

def metric_str(txt: str, key: str) -> str:
    m = re.search(rf'^{re.escape(key)}=(\S+)$', txt, re.M)
    if not m:
        raise SystemExit(f"missing_metric={key}")
    return m.group(1)

obs = {
    "narrow_moid_evaluated_pairs_total": metric(obs_txt, "narrow_moid_evaluated_pairs_total"),
    "narrow_moid_shadow_rejected_pairs_total": metric(obs_txt, "narrow_moid_shadow_rejected_pairs_total"),
    "narrow_moid_fail_open_reason_hf_placeholder_total": metric(obs_txt, "narrow_moid_fail_open_reason_hf_placeholder_total"),
    "reason_codes_strict_enum_validation": metric_str(obs_txt, "reason_codes_strict_enum_validation"),
    "narrow_phase_uncertainty_observability_gate_result": metric_str(obs_txt, "narrow_phase_uncertainty_observability_gate_result"),
}

status = "PASS"
if proxy.get("status") != "PASS" or hf.get("status") != "PASS":
    status = "FAIL"
if proxy.get("metrics", {}).get("false_negative_sats_total", 1) != 0:
    status = "FAIL"
if hf.get("metrics", {}).get("false_negative_sats_total", 1) != 0:
    status = "FAIL"
if obs["reason_codes_strict_enum_validation"] != "PASS":
    status = "FAIL"
if obs["narrow_phase_uncertainty_observability_gate_result"] != "PASS":
    status = "FAIL"

payload = {
    "schema_version": 1,
    "status": status,
    "profile": {
        "scenarios": int(scenarios),
        "satellites": int(sats),
        "debris": int(debris),
        "moid_threshold_stress_km": float(moid_threshold_stress),
    },
    "proxy_canary": {
        "status": proxy.get("status"),
        "false_negative_sats_total": proxy.get("metrics", {}).get("false_negative_sats_total"),
        "reference_collision_sats_total": proxy.get("metrics", {}).get("reference_collision_sats_total"),
        "production_collision_sats_total": proxy.get("metrics", {}).get("production_collision_sats_total"),
        "narrow_moid_hard_rejected_pairs_total": proxy.get("metrics", {}).get("narrow_moid_hard_rejected_pairs_total"),
        "narrow_moid_fail_open_pairs_total": proxy.get("metrics", {}).get("narrow_moid_fail_open_pairs_total"),
    },
    "hf_canary": {
        "status": hf.get("status"),
        "false_negative_sats_total": hf.get("metrics", {}).get("false_negative_sats_total"),
        "reference_collision_sats_total": hf.get("metrics", {}).get("reference_collision_sats_total"),
        "production_collision_sats_total": hf.get("metrics", {}).get("production_collision_sats_total"),
        "narrow_moid_hard_rejected_pairs_total": hf.get("metrics", {}).get("narrow_moid_hard_rejected_pairs_total"),
        "narrow_moid_fail_open_pairs_total": hf.get("metrics", {}).get("narrow_moid_fail_open_pairs_total"),
    },
    "hf_observability": obs,
    "coverage": {
        "proxy_moid_hard_reject_exercised": (proxy.get("metrics", {}).get("narrow_moid_hard_rejected_pairs_total", 0) > 0),
        "hf_moid_hard_reject_exercised": (hf.get("metrics", {}).get("narrow_moid_hard_rejected_pairs_total", 0) > 0),
    },
}

json.dump(payload, open(summary_path, "w", encoding="utf-8"), indent=2)
open(summary_path, "a", encoding="utf-8").write("\n")
print(f"moid_filter_validation_status={status}")
print(f"moid_filter_validation_summary_json={summary_path}")

if status != "PASS":
    raise SystemExit(1)
PY

echo "moid filter validation: PASS"
