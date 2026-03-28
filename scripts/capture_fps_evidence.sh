#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
API_BASE="${API_BASE:-http://localhost:8000}"
OUTPUT_DIR="${FPS_EVIDENCE_OUTPUT_DIR:-$ROOT_DIR/test-results/fps-evidence}"
PREPARE_SCENE="${PREPARE_SCENE:-1}"

mkdir -p "$OUTPUT_DIR"

port_from_api_base() {
  local base="$1"
  local tail="${base##*:}"
  if [[ "$tail" =~ ^[0-9]+$ ]]; then
    printf '%s\n' "$tail"
  else
    printf '8000\n'
  fi
}

if [[ "$PREPARE_SCENE" == "1" ]]; then
  export PROJECTBONK_PORT="$(port_from_api_base "$API_BASE")"
  export API_BASE
  echo "[fps-evidence] preparing a dense demo scene via run_ready_demo.sh"
  "$ROOT_DIR/scripts/run_ready_demo.sh"
fi

if ! curl -sf "$API_BASE/api/status" >/dev/null 2>&1; then
  echo "[fps-evidence] backend is not reachable at $API_BASE" >&2
  exit 1
fi

if [[ ! -d "$ROOT_DIR/node_modules/@playwright/test" ]]; then
  echo "[fps-evidence] installing root Playwright dependencies"
  npm ci --prefix "$ROOT_DIR"
fi

echo "[fps-evidence] ensuring Chromium is installed"
npx --prefix "$ROOT_DIR" playwright install chromium >/dev/null

echo "[fps-evidence] capturing frame-rate and payload evidence"
FPS_EVIDENCE_OUTPUT_DIR="$OUTPUT_DIR" \
FPS_EVIDENCE_BASE_URL="$API_BASE" \
npx --prefix "$ROOT_DIR" playwright test "$ROOT_DIR/tests/fps-evidence.spec.js" --reporter=line

echo "[fps-evidence] artifacts written to $OUTPUT_DIR"