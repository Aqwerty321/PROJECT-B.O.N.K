#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="${PROJECTBONK_DOCKER_SMOKE_IMAGE:-cascade:smoke-local}"
HOST_PORT="${PROJECTBONK_DOCKER_SMOKE_PORT:-18080}"
CONTAINER_NAME="projectbonk-docker-smoke-${HOST_PORT}"
API_BASE="http://127.0.0.1:${HOST_PORT}"

port_has_listener() {
  local port="$1"
  if (: < "/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
    return 0
  fi
  return 1
}

if [[ -z "${PROJECTBONK_DOCKER_SMOKE_PORT:-}" ]] && port_has_listener "$HOST_PORT"; then
  for candidate in 18081 18082 18083 18084 18085 18086 18087 18088 18089; do
    if ! port_has_listener "$candidate"; then
      HOST_PORT="$candidate"
      API_BASE="http://127.0.0.1:${HOST_PORT}"
      CONTAINER_NAME="projectbonk-docker-smoke-${HOST_PORT}"
      break
    fi
  done
fi

cleanup() {
  local exit_code="$1"
  if docker ps -a --format '{{.Names}}' | grep -Fx "$CONTAINER_NAME" >/dev/null 2>&1; then
    if [[ "$exit_code" -ne 0 ]]; then
      echo "[docker-smoke] container logs"
      docker logs "$CONTAINER_NAME" || true
    fi
    docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
}
trap 'cleanup $?' EXIT

echo "[docker-smoke] building image $IMAGE_TAG"
docker build -t "$IMAGE_TAG" "$ROOT_DIR"

echo "[docker-smoke] starting container on host port $HOST_PORT"
docker run -d --name "$CONTAINER_NAME" -p "${HOST_PORT}:8000" "$IMAGE_TAG" >/dev/null

ready=0
for _ in $(seq 1 120); do
  if curl -fsS "$API_BASE/api/status" >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 1
done

if [[ "$ready" -ne 1 ]]; then
  echo "[docker-smoke] FAIL: container did not become ready"
  exit 1
fi

status_json="$(curl -fsS "$API_BASE/api/status")"
printf '%s' "$status_json" | python3 -c 'import json,sys; data=json.load(sys.stdin); assert data.get("status"), data; assert "tick_count" in data, data; assert "object_count" in data, data'

telemetry_payload='{"timestamp":"2026-03-12T08:00:00.000Z","objects":[{"id":"SAT-SMOKE-01","type":"SATELLITE","r":{"x":6778.0,"y":0.0,"z":0.0},"v":{"x":0.0,"y":7.78,"z":0.0}}]}'
telemetry_json="$(curl -fsS -X POST "$API_BASE/api/telemetry" -H 'Content-Type: application/json' -d "$telemetry_payload")"
printf '%s' "$telemetry_json" | python3 -c 'import json,sys; data=json.load(sys.stdin); assert data.get("status") == "ACK", data; assert data.get("processed_count") == 1, data'

step_json="$(curl -fsS -X POST "$API_BASE/api/simulate/step" -H 'Content-Type: application/json' -d '{"step_seconds":60}')"
printf '%s' "$step_json" | python3 -c 'import json,sys; data=json.load(sys.stdin); assert data.get("status") == "STEP_COMPLETE", data'

snapshot_json="$(curl -fsS "$API_BASE/api/visualization/snapshot")"
printf '%s' "$snapshot_json" | python3 -c 'import json,sys; data=json.load(sys.stdin); sats=data.get("satellites", []); assert data.get("timestamp"), data; assert len(sats) >= 1, data'

echo "[docker-smoke] PASS: Docker boot, telemetry, simulate/step, and snapshot endpoints verified on $API_BASE"