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

"$BUILD_DIR/ProjectBONK" >/tmp/projectbonk_api_contract_server.log 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 80); do
  if "$BUILD_DIR/api_contract_gate" 127.0.0.1 "$PORT" >/tmp/projectbonk_api_contract_probe.log 2>&1; then
    cat /tmp/projectbonk_api_contract_probe.log
    echo "api contract gate: PASS"
    exit 0
  fi
  sleep 0.1
done

"$BUILD_DIR/api_contract_gate" 127.0.0.1 "$PORT"
