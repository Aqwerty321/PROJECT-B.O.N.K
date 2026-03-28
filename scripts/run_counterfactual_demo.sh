#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_TAG="${IMAGE_TAG:-cascade:local}"
DATA_FILE="${DATA_FILE:-$ROOT_DIR/3le_data.txt}"
WITHOUT_PORT="${WITHOUT_PORT:-8011}"
WITH_PORT="${WITH_PORT:-8012}"
REPORT_DIR="${REPORT_DIR:-$ROOT_DIR/build/counterfactual-demo}"

mkdir -p "$REPORT_DIR"

cleanup_container() {
  local name="$1"
  docker rm -f "$name" >/dev/null 2>&1 || true
}

wait_ready() {
  local base="$1"
  for _ in $(seq 1 120); do
    if curl -sf "$base/api/status" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  echo "[counterfactual] backend did not become ready at $base" >&2
  return 1
}

run_steps() {
  local base="$1"
  for seconds in 60 20 60 60 27; do
    curl -s -X POST "$base/api/simulate/step" \
      -H 'Content-Type: application/json' \
      -d "{\"step_seconds\":$seconds}" >/dev/null
  done
}

collect_report() {
  local label="$1"
  local base="$2"
  local out_json="$3"

  python3 - "$label" "$base" "$out_json" <<'PY'
import json
import math
import sys
import urllib.request

label, base, out_json = sys.argv[1:4]

def get_json(path: str):
    with urllib.request.urlopen(f"{base}{path}", timeout=20) as resp:
        return json.loads(resp.read().decode())

burns = get_json('/api/debug/burns')
status = get_json('/api/status?details=1')
predicted = get_json('/api/debug/conjunctions?source=predicted')

executed = burns.get('executed', []) if isinstance(burns, dict) else []
pending = burns.get('pending', []) if isinstance(burns, dict) else []
dropped = burns.get('dropped', []) if isinstance(burns, dict) else []
summary = burns.get('summary', {}) if isinstance(burns, dict) else {}
metrics = status.get('internal_metrics', {}) if isinstance(status, dict) else {}
predicted_events = predicted.get('conjunctions', []) if isinstance(predicted, dict) else []

tracked = [burn for burn in executed if burn.get('scheduled_from_predictive_cdm') and burn.get('trigger_debris_id')]
evaluated = [burn for burn in tracked if burn.get('mitigation_evaluated')]
avoided = [burn for burn in evaluated if burn.get('collision_avoided')]
critical_predicted = [event for event in predicted_events if str(event.get('severity') or '') == 'critical']

if summary.get('collisions_avoided', 0) >= 2:
    verdict = 'ready'
elif summary.get('collisions_avoided', 0) >= 1 or len(evaluated) >= 2:
    verdict = 'usable'
else:
    verdict = 'weak'

payload = {
    'label': label,
    'verdict': verdict,
    'tick_count': status.get('tick_count'),
    'object_count': status.get('object_count'),
    'predictive_conjunction_count': len(predicted_events),
    'predictive_critical_count': len(critical_predicted),
    'executed_burns': len(executed),
    'pending_burns': len(pending),
    'dropped_burns': len(dropped),
    'tracked_avoidance_burns': len(tracked),
    'evaluated_avoidance_burns': len(evaluated),
    'successful_avoids': len(avoided),
    'fleet_collisions_avoided': summary.get('collisions_avoided', 0),
    'fuel_consumed_kg': summary.get('fuel_consumed_kg', 0.0),
    'avoidance_fuel_consumed_kg': summary.get('avoidance_fuel_consumed_kg', 0.0),
    'active_cdm_warnings': metrics.get('active_cdm_warnings'),
  }

with open(out_json, 'w', encoding='utf-8') as handle:
    json.dump(payload, handle, indent=2)
PY
}

run_case() {
  local label="$1"
  local port="$2"
  local min_lead="$3"
  local container="cascade-counterfactual-$label"
  local base="http://localhost:$port"
  local out_json="$REPORT_DIR/$label.json"

  echo "[counterfactual] running case '$label' on $base"
  cleanup_container "$container"

  local -a env_args=(-e PROJECTBONK_CORS_ENABLE=1)
  if [[ -n "$min_lead" ]]; then
    env_args+=(-e "PROJECTBONK_AUTO_COLA_MIN_LEAD_S=$min_lead")
  fi

  docker run -d --rm \
    --name "$container" \
    -p "$port:8000" \
    "${env_args[@]}" \
    "$IMAGE_TAG" >/dev/null

  wait_ready "$base"

  python3 "$ROOT_DIR/scripts/replay_data_catalog.py" \
    --data "$DATA_FILE" \
    --api-base "$base" \
    --satellite-mode catalog \
    --operator-sats 10 >/dev/null

  python3 "$ROOT_DIR/scripts/inject_synthetic_encounter.py" \
    --api-base "$base" \
    --mode single \
    --target SAT-67060 \
    --miss-km 0.008 \
    --count 1 \
    --encounter-hours 0.18 \
    --future-model hcw \
    --debris-start-id 97001 \
    --seed 7 >/dev/null

  python3 "$ROOT_DIR/scripts/inject_synthetic_encounter.py" \
    --api-base "$base" \
    --mode single \
    --target SAT-67061 \
    --miss-km 0.008 \
    --count 1 \
    --encounter-hours 0.18 \
    --future-model hcw \
    --debris-start-id 97101 \
    --seed 7 >/dev/null

  run_steps "$base"
  collect_report "$label" "$base" "$out_json"
  cleanup_container "$container"
}

if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  echo "[counterfactual] docker image '$IMAGE_TAG' missing, building it now"
  docker build -t "$IMAGE_TAG" "$ROOT_DIR"
fi

run_case without-cascade "$WITHOUT_PORT" 3600
run_case with-cascade "$WITH_PORT" ""

python3 - "$REPORT_DIR/without-cascade.json" "$REPORT_DIR/with-cascade.json" <<'PY'
import json
import sys

without_path, with_path = sys.argv[1:3]

with open(without_path, 'r', encoding='utf-8') as handle:
    without_case = json.load(handle)
with open(with_path, 'r', encoding='utf-8') as handle:
    with_case = json.load(handle)

avoid_delta = with_case['fleet_collisions_avoided'] - without_case['fleet_collisions_avoided']
executed_delta = with_case['executed_burns'] - without_case['executed_burns']

print('[counterfactual-demo]')
print('| Case | Verdict | Avoided | Executed Burns | Predictive Critical | Fuel Consumed (kg) |')
print('|---|---|---:|---:|---:|---:|')
for row in (without_case, with_case):
    print(f"| {row['label']} | {row['verdict']} | {row['fleet_collisions_avoided']} | {row['executed_burns']} | {row['predictive_critical_count']} | {row['fuel_consumed_kg']:.3f} |")

print()
print(f"avoid_delta={avoid_delta}")
print(f"executed_burn_delta={executed_delta}")
print(f"reports={without_path},{with_path}")

if avoid_delta <= 0:
    raise SystemExit('expected with-cascade case to outperform the suppressed case')
PY