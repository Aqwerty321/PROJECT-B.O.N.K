#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
API_BASE="${API_BASE:-http://localhost:8000}"
DATA_FILE="${DATA_FILE:-$ROOT_DIR/3le_data.txt}"

run_step() {
  local seconds="$1"
  curl -s -X POST "$API_BASE/api/simulate/step" \
    -H 'Content-Type: application/json' \
    -d "{\"step_seconds\":$seconds}"
  printf '\n'
}

printf '[ready-demo] restarting docker services\n'
docker compose down
docker compose up -d

printf '[ready-demo] replaying catalog-backed scene from %s\n' "$DATA_FILE"
python3 "$ROOT_DIR/scripts/replay_data_catalog.py" \
  --data "$DATA_FILE" \
  --api-base "$API_BASE" \
  --satellite-mode catalog \
  --operator-sats 10

printf '[ready-demo] injecting calibrated critical for SAT-67060\n'
python3 "$ROOT_DIR/scripts/inject_synthetic_encounter.py" \
  --api-base "$API_BASE" \
  --mode single \
  --target SAT-67060 \
  --miss-km 0.008 \
  --count 1 \
  --encounter-hours 0.18 \
  --future-model hcw \
  --debris-start-id 97001 \
  --seed 7

printf '[ready-demo] injecting calibrated critical for SAT-67061\n'
python3 "$ROOT_DIR/scripts/inject_synthetic_encounter.py" \
  --api-base "$API_BASE" \
  --mode single \
  --target SAT-67061 \
  --miss-km 0.008 \
  --count 1 \
  --encounter-hours 0.18 \
  --future-model hcw \
  --debris-start-id 97101 \
  --seed 7

printf '[ready-demo] stepping sim: 60s\n'
run_step 60
printf '[ready-demo] stepping sim: 20s\n'
run_step 20
printf '[ready-demo] stepping sim: 60s\n'
run_step 60
printf '[ready-demo] stepping sim: 60s\n'
run_step 60
printf '[ready-demo] stepping sim: 27s\n'
run_step 27

printf '[ready-demo] readiness check\n'
python3 "$ROOT_DIR/scripts/check_demo_readiness.py" --api-base "$API_BASE"

printf '[ready-demo] confirmed avoids\n'
curl -s "$API_BASE/api/debug/burns" | python3 -c '
import json, sys
data = json.load(sys.stdin)
rows = [
    b for b in data.get("executed", [])
    if b.get("collision_avoided")
    and b.get("trigger_debris_id") in {"DEB-SYNTH-97001", "DEB-SYNTH-97101"}
]
print(json.dumps(rows, indent=2))
if len(rows) < 2:
    raise SystemExit(1)
'

printf '[ready-demo] complete\n'
