#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUMMARY_JSON="${1:-$ROOT_DIR/build/phase4_calibration_gate_summary.json}"
EXPECTED_SCHEMA_VERSION="${2:-2}"

if [[ ! "$SUMMARY_JSON" = /* ]]; then
  SUMMARY_JSON="$ROOT_DIR/${SUMMARY_JSON#./}"
fi

if [[ ! -f "$SUMMARY_JSON" ]]; then
  echo "phase4_law_assertions_gate_result=FAIL" >&2
  echo "reason=summary_json_missing" >&2
  echo "summary_json=$SUMMARY_JSON" >&2
  exit 1
fi

python3 - <<'PY' "$SUMMARY_JSON" "$EXPECTED_SCHEMA_VERSION"
import json
import sys

summary_path = sys.argv[1]
expected_schema_version = str(sys.argv[2])

try:
    with open(summary_path, "r", encoding="utf-8") as f:
        payload = json.load(f)
except Exception:
    print("phase4_law_assertions_gate_result=FAIL", file=sys.stderr)
    print("reason=summary_json_parse_failure", file=sys.stderr)
    raise

laws = payload.get("law_assertions")
if not isinstance(laws, dict):
    print("phase4_law_assertions_gate_result=FAIL", file=sys.stderr)
    print("reason=law_assertions_missing", file=sys.stderr)
    raise SystemExit(1)

required_status_paths = [
    ("law1_no_false_negatives", "status"),
    ("law2_uncertainty_fail_open", "status"),
    ("law3_transitions_observable", "status"),
    ("law4_no_silent_fallback", "status"),
    ("law4_no_silent_fallback", "strict_reason_codes"),
]

for section, field in required_status_paths:
    section_obj = laws.get(section)
    if not isinstance(section_obj, dict):
        print("phase4_law_assertions_gate_result=FAIL", file=sys.stderr)
        print(f"reason=missing_law_section_{section}", file=sys.stderr)
        raise SystemExit(1)
    value = str(section_obj.get(field, ""))
    if value != "PASS":
        print("phase4_law_assertions_gate_result=FAIL", file=sys.stderr)
        print(f"reason=law_assertion_not_pass_{section}_{field}", file=sys.stderr)
        print(f"observed={value}", file=sys.stderr)
        raise SystemExit(1)

schema_value = str(
    laws.get("law4_no_silent_fallback", {}).get("reason_codes_schema_version", "")
)
if schema_value != expected_schema_version:
    print("phase4_law_assertions_gate_result=FAIL", file=sys.stderr)
    print("reason=reason_codes_schema_version_mismatch", file=sys.stderr)
    print(f"observed={schema_value}", file=sys.stderr)
    print(f"expected={expected_schema_version}", file=sys.stderr)
    raise SystemExit(1)

fn_total = str(laws.get("law1_no_false_negatives", {}).get("false_negative_sats_total", ""))
if fn_total != "0":
    print("phase4_law_assertions_gate_result=FAIL", file=sys.stderr)
    print("reason=false_negative_total_nonzero", file=sys.stderr)
    print(f"observed={fn_total}", file=sys.stderr)
    raise SystemExit(1)

print("phase4_law_assertions_gate_result=PASS")
print(f"summary_json={summary_path}")
print(f"reason_codes_schema_version={schema_value}")
PY

echo "phase4 law assertions gate: PASS"
