#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
PORT="${PROJECTBONK_API_CONTRACT_PORT:-8000}"

cmake --build "$BUILD_DIR" --target ProjectBONK
cmake --build "$BUILD_DIR" --target api_contract_gate

cleanup() {
  if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

PROJECTBONK_CORS_ENABLE="${PROJECTBONK_CORS_ENABLE:-true}" \
PROJECTBONK_CORS_ALLOW_ORIGIN="${PROJECTBONK_CORS_ALLOW_ORIGIN:-http://localhost:5173}" \
PROJECTBONK_MAX_COMMAND_QUEUE_DEPTH="${PROJECTBONK_MAX_COMMAND_QUEUE_DEPTH:-1}" \
PROJECTBONK_SCHEDULE_SUCCESS_STATUS="${PROJECTBONK_SCHEDULE_SUCCESS_STATUS:-202}" \
PROJECTBONK_MAX_STEP_SECONDS="${PROJECTBONK_MAX_STEP_SECONDS:-86400}" \
"$BUILD_DIR/ProjectBONK" >/tmp/projectbonk_api_contract_server.log 2>&1 &
SERVER_PID=$!

ready=0
for _ in $(seq 1 80); do
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "api contract gate: FAIL"
    echo "server process exited before readiness"
    cat /tmp/projectbonk_api_contract_server.log
    exit 1
  fi

  if curl -fsS "http://127.0.0.1:${PORT}/api/status" >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 0.1
done

if [[ "$ready" -ne 1 ]]; then
  echo "api contract gate: FAIL"
  echo "server readiness probe timed out on /api/status"
  exit 1
fi

PROJECTBONK_SCHEDULE_SUCCESS_STATUS="${PROJECTBONK_SCHEDULE_SUCCESS_STATUS:-202}" \
PROJECTBONK_MAX_STEP_SECONDS="${PROJECTBONK_MAX_STEP_SECONDS:-86400}" \
PROJECTBONK_API_CONTRACT_SCHEDULE_SUCCESS_STATUS="${PROJECTBONK_API_CONTRACT_SCHEDULE_SUCCESS_STATUS:-${PROJECTBONK_SCHEDULE_SUCCESS_STATUS:-202}}" \
  "$BUILD_DIR/api_contract_gate" 127.0.0.1 "$PORT" >/tmp/projectbonk_api_contract_probe.log 2>&1 || {
    cat /tmp/projectbonk_api_contract_probe.log
    exit 1
  }

cat /tmp/projectbonk_api_contract_probe.log
echo "api contract gate: PASS"
