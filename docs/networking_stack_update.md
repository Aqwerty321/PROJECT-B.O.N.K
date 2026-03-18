# CASCADE Networking Stack Update Tracker

Date: 2026-03-18

This tracker keeps the networking and runtime-synchronization migration in one
place so the full scope can be delivered reliably after current math-hardening
work.

## Current baseline

- HTTP stack: `cpp-httplib` in-process server on `:8000`.
- Runtime and HTTP currently remain in one process and one source unit
  (`main.cpp`) with lock-based coordination.
- New math and safety coverage now include:
  - stricter near-threshold narrow-phase refinement and fail-open under budget
    exhaustion,
  - false-negative regression gate (`narrow_phase_false_negative_gate`),
  - maneuver/upload/graveyard invariants regression gate
    (`maneuver_ops_invariants_gate`),
  - station-keeping error/penalty metrics,
  - upload-window scheduling with 10s latency checks,
  - graveyard planning/execution path at EOL threshold.

## Implementation progress snapshot

- N1 complete:
  - `EngineRuntime` owns mutable simulation state and command execution.
  - endpoint handlers call runtime methods, not core state directly.
- N2 complete:
  - typed command queue for telemetry/schedule/step with `promise/future`
    synchronization.
  - single runtime worker serializes mutating commands.
  - bounded command wait added (`RUNTIME_BUSY` on timeout) and queue metrics
    exposed in `GET /api/status?details=1`.
- N3 complete:
  - routes moved to `src/http/api_server.*`.
  - request parsing moved to `src/http/request_parsers.*`.
  - response serialization moved to `src/http/response_builders.*`.
  - API contract regression harness added (`tools/api_contract_gate.cpp`,
    `scripts/api_contract_gate.sh`) and wired into CTest/CI.
- N4 in progress:
  - immutable published snapshots now back `snapshot/conflicts/propagation`
    GET paths for lock-light reads.
  - queue depth/enqueue/complete/reject/timeout counters exposed in status
    details for operational visibility.
  - queue backpressure guardrail added via `PROJECTBONK_MAX_COMMAND_QUEUE_DEPTH`
    with explicit `RUNTIME_BUSY` reject path when full.
  - API contract gate now stress-checks queue pressure and verifies backpressure
    evidence (`RUNTIME_BUSY` or timeout/reject metrics).
  - local split CORS controls are now supported via env (`PROJECTBONK_CORS_*`)
    with `OPTIONS /api/*` preflight handling and CORS headers on API responses.
    `PROJECTBONK_CORS_ALLOW_ORIGIN` supports comma-separated allowlists and the
    server echoes only matching origins (`Vary: Origin` enabled).
  - API contract gate now validates CORS preflight/allow-origin behavior in
    addition to queue-pressure checks.
  - queue latency observability added to `GET /api/status?details=1` as
    per-command (`telemetry/schedule/step`) queue-wait and execution metrics.
  - README now documents local split dev topology (`5173` + `8000`) and
    reverse-proxy `/api` pass-through deployment.

## Scope to carry into networking refactor

These are required to preserve behavior during runtime/http separation:

1. Preserve all PS endpoint schemas and status codes.
2. Preserve conservative fail-open collision behavior and all current gates.
3. Preserve upload-window semantics (latency + LOS + cooldown) for manual and
   auto-planned burns.
4. Preserve station-keeping metrics and graveyard lifecycle counters in debug/
   details views.

## Planned networking phases

### Phase N1: Runtime boundary extraction

- Create `EngineRuntime` owning mutable simulation state and command execution.
- Keep `cpp-httplib`; only move state access behind runtime methods.
- Add deterministic command result structs for telemetry/schedule/step.

### Phase N2: Compute-thread command queue

- Introduce typed command queue (`telemetry`, `schedule`, `step`) with
  `promise/future` completion for synchronous endpoints.
- Ensure state writes happen only on compute thread.
- Keep GET paths lock-safe; migrate to published immutable snapshots.

### Phase N3: HTTP module split

- Move route registration and payload parsing into `src/http/api_server.*`.
- Keep endpoint behavior unchanged.
- Keep runtime logic outside HTTP handlers.

### Phase N4: Frontend/backend dev split + demo packaging

- Dev topology: frontend on `5173`, backend on `8000`.
- Demo/prod topology: one container via `nginx` reverse proxy or static asset
  serving path, while preserving `/api` contract.

## Regression checklist for networking phases

Run on each networking PR:

- `./scripts/phase2_regression_gate.sh`
- `./scripts/broad_phase_sanity_gate.sh`
- `./scripts/narrow_phase_false_negative_gate.sh`
- `./scripts/maneuver_ops_invariants_gate.sh`
- `./scripts/recovery_slot_gate.sh`
- `./scripts/recovery_planner_invariants_gate.sh`
- `./scripts/api_contract_gate.sh`
- `ctest --test-dir build --output-on-failure`

## Open risks to monitor

- Request-thread timeouts if queued command scheduling is not bounded.
- Snapshot staleness under high step load.
- Behavior drift in schedule validation when moving code from handlers.
- Any API schema drift (PS endpoints are strict contract).

## Next concrete tasks

1. Keep validating queue timeout/depth defaults under higher synthetic load to
   tune `PROJECTBONK_MAX_COMMAND_QUEUE_DEPTH` for CI and demo environments.
2. Optional: export status `details` latency counters to external dashboards
   (Prometheus sidecar/log pipeline) without changing API contract payloads.
3. Continue preserving PS response-key/status compatibility as future network
   packaging changes land.
