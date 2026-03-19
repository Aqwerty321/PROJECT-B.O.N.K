#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"

# Compact deterministic fixture profile for CI/runtime safety checks.
SATELLITES="${2:-24}"
DEBRIS="${3:-3000}"
TICKS="${4:-3}"
STEP_S="${5:-60}"

HIGH_REL_SPEED_KM_S="${6:-2.0}"
HIGH_REL_SPEED_EXTRA_BAND_KM="${7:-0.50}"
FULL_REFINE_BUDGET_BASE="${8:-96}"
FULL_REFINE_BUDGET_MIN="${9:-16}"
FULL_REFINE_BUDGET_MAX="${10:-320}"
FULL_REFINE_BAND_KM="${11:-0.35}"
REFINE_BAND_KM="${12:-0.14}"
TCA_GUARD_KM="${13:-0.03}"
FULL_REFINE_SAMPLES="${14:-24}"
FULL_REFINE_SUBSTEP_S="${15:-0.7}"
MICRO_REFINE_MAX_STEP_S="${16:-3.0}"
FIXTURE_PAIRS_PER_SAT="${17:-4}"
FIXTURE_REL_SPEED_KM_S="${18:-3.0}"
FIXTURE_OFFSET_KM="${19:-0.42}"
FIXTURE_OFFSET_JITTER_KM="${20:-0.01}"
MOID_SHADOW="${21:-1}"
MOID_FILTER="${22:-0}"
MOID_SAMPLES="${23:-24}"
MOID_REJECT_THRESHOLD_KM="${24:-2.0}"
MOID_MAX_E="${25:-0.2}"

MIN_PROMOTED_TOTAL="${26:-1}"
MIN_FULL_REFINED_TOTAL="${27:-1}"
MIN_PLANE_PHASE_EVALUATED_TOTAL="${28:-1}"
MIN_PLANE_PHASE_SHADOW_REJECTED_TOTAL="${29:-0}"
MIN_MOID_EVALUATED_TOTAL="${30:-1}"
MIN_MOID_SHADOW_REJECTED_TOTAL="${31:-1}"

PROBE_LOG="$(mktemp)"
trap 'rm -f "$PROBE_LOG"' EXIT

"$ROOT_DIR/scripts/narrow_phase_calibration_probe.sh" \
  "$BUILD_DIR" \
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
  "$MICRO_REFINE_MAX_STEP_S" \
  "$FIXTURE_PAIRS_PER_SAT" \
  "$FIXTURE_REL_SPEED_KM_S" \
  "$FIXTURE_OFFSET_KM" \
  "$FIXTURE_OFFSET_JITTER_KM" \
  "$MOID_SHADOW" \
  "$MOID_FILTER" \
  "$MOID_SAMPLES" \
  "$MOID_REJECT_THRESHOLD_KM" \
  "$MOID_MAX_E" | tee "$PROBE_LOG"

PROMOTED_TOTAL="$(python3 - <<'PY' "$PROBE_LOG"
import re, sys
txt=open(sys.argv[1], 'r', encoding='utf-8').read()
m=re.search(r'^narrow_uncertainty_promoted_pairs_total=(\d+)$', txt, re.M)
print(m.group(1) if m else "")
PY
)"

FULL_REFINED_TOTAL="$(python3 - <<'PY' "$PROBE_LOG"
import re, sys
txt=open(sys.argv[1], 'r', encoding='utf-8').read()
m=re.search(r'^narrow_full_refined_pairs_total=(\d+)$', txt, re.M)
print(m.group(1) if m else "")
PY
)"

PLANE_PHASE_EVALUATED_TOTAL="$(python3 - <<'PY' "$PROBE_LOG"
import re, sys
txt=open(sys.argv[1], 'r', encoding='utf-8').read()
m=re.search(r'^narrow_plane_phase_evaluated_pairs_total=(\d+)$', txt, re.M)
print(m.group(1) if m else "")
PY
)"

PLANE_PHASE_SHADOW_REJECTED_TOTAL="$(python3 - <<'PY' "$PROBE_LOG"
import re, sys
txt=open(sys.argv[1], 'r', encoding='utf-8').read()
m=re.search(r'^narrow_plane_phase_shadow_rejected_pairs_total=(\d+)$', txt, re.M)
print(m.group(1) if m else "")
PY
)"

MOID_EVALUATED_TOTAL="$(python3 - <<'PY' "$PROBE_LOG"
import re, sys
txt=open(sys.argv[1], 'r', encoding='utf-8').read()
m=re.search(r'^narrow_moid_evaluated_pairs_total=(\d+)$', txt, re.M)
print(m.group(1) if m else "")
PY
)"

MOID_SHADOW_REJECTED_TOTAL="$(python3 - <<'PY' "$PROBE_LOG"
import re, sys
txt=open(sys.argv[1], 'r', encoding='utf-8').read()
m=re.search(r'^narrow_moid_shadow_rejected_pairs_total=(\d+)$', txt, re.M)
print(m.group(1) if m else "")
PY
)"

if [[ -z "$PROMOTED_TOTAL" || -z "$FULL_REFINED_TOTAL" || -z "$PLANE_PHASE_EVALUATED_TOTAL" || -z "$PLANE_PHASE_SHADOW_REJECTED_TOTAL" || -z "$MOID_EVALUATED_TOTAL" || -z "$MOID_SHADOW_REJECTED_TOTAL" ]]; then
  echo "narrow-phase uncertainty observability gate: FAIL" >&2
  echo "reason=missing_probe_metrics" >&2
  exit 1
fi

if (( PROMOTED_TOTAL < MIN_PROMOTED_TOTAL )); then
  echo "narrow-phase uncertainty observability gate: FAIL" >&2
  echo "reason=promoted_pairs_below_threshold" >&2
  echo "promoted_total=$PROMOTED_TOTAL min_required=$MIN_PROMOTED_TOTAL" >&2
  exit 1
fi

if (( FULL_REFINED_TOTAL < MIN_FULL_REFINED_TOTAL )); then
  echo "narrow-phase uncertainty observability gate: FAIL" >&2
  echo "reason=full_refined_pairs_below_threshold" >&2
  echo "full_refined_total=$FULL_REFINED_TOTAL min_required=$MIN_FULL_REFINED_TOTAL" >&2
  exit 1
fi

if (( PLANE_PHASE_EVALUATED_TOTAL < MIN_PLANE_PHASE_EVALUATED_TOTAL )); then
  echo "narrow-phase uncertainty observability gate: FAIL" >&2
  echo "reason=plane_phase_evaluated_below_threshold" >&2
  echo "plane_phase_evaluated_total=$PLANE_PHASE_EVALUATED_TOTAL min_required=$MIN_PLANE_PHASE_EVALUATED_TOTAL" >&2
  exit 1
fi

if (( PLANE_PHASE_SHADOW_REJECTED_TOTAL < MIN_PLANE_PHASE_SHADOW_REJECTED_TOTAL )); then
  echo "narrow-phase uncertainty observability gate: FAIL" >&2
  echo "reason=plane_phase_shadow_rejected_below_threshold" >&2
  echo "plane_phase_shadow_rejected_total=$PLANE_PHASE_SHADOW_REJECTED_TOTAL min_required=$MIN_PLANE_PHASE_SHADOW_REJECTED_TOTAL" >&2
  exit 1
fi

if (( MOID_EVALUATED_TOTAL < MIN_MOID_EVALUATED_TOTAL )); then
  echo "narrow-phase uncertainty observability gate: FAIL" >&2
  echo "reason=moid_evaluated_below_threshold" >&2
  echo "moid_evaluated_total=$MOID_EVALUATED_TOTAL min_required=$MIN_MOID_EVALUATED_TOTAL" >&2
  exit 1
fi

if (( MOID_SHADOW_REJECTED_TOTAL < MIN_MOID_SHADOW_REJECTED_TOTAL )); then
  echo "narrow-phase uncertainty observability gate: FAIL" >&2
  echo "reason=moid_shadow_rejected_below_threshold" >&2
  echo "moid_shadow_rejected_total=$MOID_SHADOW_REJECTED_TOTAL min_required=$MIN_MOID_SHADOW_REJECTED_TOTAL" >&2
  exit 1
fi

python3 - <<'PY' "$PROBE_LOG"
import re
import sys

txt = open(sys.argv[1], 'r', encoding='utf-8').read()
metrics = {}
for m in re.finditer(r'^([a-z0-9_]+)=(\d+)$', txt, re.M):
  metrics[m.group(1)] = int(m.group(2))

expected_reason_keys = {
  "narrow_plane_phase_reject_reason_plane_angle_total",
  "narrow_plane_phase_reject_reason_phase_angle_total",
  "narrow_plane_phase_fail_open_reason_elements_invalid_total",
  "narrow_plane_phase_fail_open_reason_eccentricity_guard_total",
  "narrow_plane_phase_fail_open_reason_non_finite_state_total",
  "narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total",
  "narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total",
  "narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total",
  "narrow_plane_phase_fail_open_reason_uncertainty_override_total",
  "narrow_moid_reject_reason_distance_threshold_total",
  "narrow_moid_fail_open_reason_elements_invalid_total",
  "narrow_moid_fail_open_reason_eccentricity_guard_total",
  "narrow_moid_fail_open_reason_non_finite_state_total",
  "narrow_moid_fail_open_reason_sampling_failure_total",
  "narrow_moid_fail_open_reason_uncertainty_override_total",
  "narrow_refine_fail_open_reason_rk4_failure_total",
  "narrow_full_refine_fail_open_reason_rk4_failure_total",
  "narrow_full_refine_fail_open_reason_budget_exhausted_total",
}

found_reason_keys = {
  k for k in metrics.keys()
  if "_reason_" in k and k.endswith("_total")
}

missing = sorted(expected_reason_keys - found_reason_keys)
unexpected = sorted(found_reason_keys - expected_reason_keys)
if missing or unexpected:
  print("narrow-phase uncertainty observability gate: FAIL", file=sys.stderr)
  if missing:
    print("reason=reason_code_keys_missing", file=sys.stderr)
    print("missing_keys=" + ",".join(missing), file=sys.stderr)
  if unexpected:
    print("reason=reason_code_keys_unexpected", file=sys.stderr)
    print("unexpected_keys=" + ",".join(unexpected), file=sys.stderr)
  raise SystemExit(1)

def need(name: str) -> int:
  if name not in metrics:
    print("narrow-phase uncertainty observability gate: FAIL", file=sys.stderr)
    print("reason=missing_reason_metric", file=sys.stderr)
    print(f"missing_metric={name}", file=sys.stderr)
    raise SystemExit(1)
  return metrics[name]

plane_fail_open_sum = (
  need("narrow_plane_phase_fail_open_reason_elements_invalid_total")
  + need("narrow_plane_phase_fail_open_reason_eccentricity_guard_total")
  + need("narrow_plane_phase_fail_open_reason_non_finite_state_total")
  + need("narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total")
  + need("narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total")
  + need("narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total")
  + need("narrow_plane_phase_fail_open_reason_uncertainty_override_total")
)

moid_fail_open_sum = (
  need("narrow_moid_fail_open_reason_elements_invalid_total")
  + need("narrow_moid_fail_open_reason_eccentricity_guard_total")
  + need("narrow_moid_fail_open_reason_non_finite_state_total")
  + need("narrow_moid_fail_open_reason_sampling_failure_total")
  + need("narrow_moid_fail_open_reason_uncertainty_override_total")
)

refine_fail_open_sum = need("narrow_refine_fail_open_reason_rk4_failure_total")
full_refine_fail_open_sum = (
  need("narrow_full_refine_fail_open_reason_rk4_failure_total")
  + need("narrow_full_refine_fail_open_reason_budget_exhausted_total")
)

if plane_fail_open_sum != need("narrow_plane_phase_fail_open_pairs_total"):
  print("narrow-phase uncertainty observability gate: FAIL", file=sys.stderr)
  print("reason=plane_phase_fail_open_reason_sum_mismatch", file=sys.stderr)
  raise SystemExit(1)

if moid_fail_open_sum != need("narrow_moid_fail_open_pairs_total"):
  print("narrow-phase uncertainty observability gate: FAIL", file=sys.stderr)
  print("reason=moid_fail_open_reason_sum_mismatch", file=sys.stderr)
  raise SystemExit(1)

if refine_fail_open_sum != need("narrow_refine_fail_open_total"):
  print("narrow-phase uncertainty observability gate: FAIL", file=sys.stderr)
  print("reason=refine_fail_open_reason_sum_mismatch", file=sys.stderr)
  raise SystemExit(1)

if full_refine_fail_open_sum != need("narrow_full_refine_fail_open_total"):
  print("narrow-phase uncertainty observability gate: FAIL", file=sys.stderr)
  print("reason=full_refine_fail_open_reason_sum_mismatch", file=sys.stderr)
  raise SystemExit(1)
PY

echo "narrow_uncertainty_promoted_pairs_total=$PROMOTED_TOTAL"
echo "narrow_full_refined_pairs_total=$FULL_REFINED_TOTAL"
echo "narrow_plane_phase_evaluated_pairs_total=$PLANE_PHASE_EVALUATED_TOTAL"
echo "narrow_plane_phase_shadow_rejected_pairs_total=$PLANE_PHASE_SHADOW_REJECTED_TOTAL"
echo "narrow_moid_evaluated_pairs_total=$MOID_EVALUATED_TOTAL"
echo "narrow_moid_shadow_rejected_pairs_total=$MOID_SHADOW_REJECTED_TOTAL"
echo "reason_codes_strict_enum_validation=PASS"
echo "narrow_phase_uncertainty_observability_gate_result=PASS"
echo "narrow-phase uncertainty observability gate: PASS"
