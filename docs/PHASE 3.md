# CASCADE Phase 3 Implementation Notes (Start)

Date: 2026-03-17

## Goal

Phase 3 wires a dedicated simulation tick engine so `POST /api/simulate/step`
is no longer coupled to endpoint handler logic and can be benchmarked/reused.

## Delivered in this start commit

- Added `src/simulation_engine.hpp`
- Added `src/simulation_engine.cpp`
- Added `src/broad_phase.hpp`
- Added `src/broad_phase.cpp`
- Added `tuner/offline_multiobjective_tuner.cpp` (offline scaffold)
- Added `tools/broad_phase_validation.cpp`
- Added `scripts/broad_phase_sanity_gate.sh`
- Refactored `main.cpp` simulate endpoint to call `run_simulation_step(...)`
- Added synthetic benchmark `tools/phase3_tick_benchmark.cpp`
- Updated `CMakeLists.txt` with:
  - `src/simulation_engine.cpp` in `ProjectBONK`
  - `src/broad_phase.cpp` in `ProjectBONK`
  - new target `phase3_tick_benchmark`

## Why this matters

- Centralizes simulation stepping in one module (clean boundary for Phase 3+)
- Makes propagation behavior testable outside HTTP stack
- Enables direct runtime/perf measurement at hackathon scale
- Introduces conservative broad-phase candidate generation with explicit
  false-negative-averse shell overlap policy

## Benchmark snapshots

### Default synthetic load

```bash
./build/phase3_tick_benchmark
```

Observed:

- objects: `10050` (`50` satellites + `10000` debris)
- step: `30s`
- mean tick: `~6.9 ms`
- median tick: `~6.7 ms`
- p95 tick: `~7.4 ms`
- broad-phase counters available from engine stats (`pairs considered`,
  `shell-overlap candidates`)
- narrow-phase now consumes broad-phase candidates and populates
  `collisions_detected` in step responses

### Stress step

```bash
./build/phase3_tick_benchmark 50 10000 5 10 3600
```

Observed:

- step: `3600s`
- median tick: `~91.5 ms`
- p95 tick: `~92.6 ms`

## Next Phase 3 tasks

- integrate per-tick arena allocation strategy
- expose structured tick stats in `/api/status` (without breaking PS schema)
- map collision detections to maneuver executor outputs
- move from instantaneous threshold checks to TCA-window narrow-phase

## Offline tuner scaffold

`offline_multiobjective_tuner` is intentionally separate from runtime path.

- uses deterministic train/eval scenario split
- strict safety rule: disqualify any candidate with non-zero risk proxy on
  train or eval
- reports Pareto candidates minimizing safe compute objectives:
  - pairs after band indexing
  - candidate count

## Safety gates

- `phase2_regression_gate` (strict adaptive propagation)
- `broad_phase_sanity_gate` (indexed broad-phase vs shell baseline)
- CI workflow runs both gates on push/PR
- runtime broad-phase keeps D-criterion disabled by default until Phase 4
  narrow-phase integration proves no-FN behavior end-to-end

Latest gate snapshot:

- adaptive gate: PASS on both configured sweeps
- broad-phase sanity gate: PASS (`missing_vs_shell_baseline_total = 0`)

Latest integration snapshot:

- `/api/simulate/step` now reports non-placeholder `collisions_detected`
  from narrow-phase candidate checks (smoke scenario with co-located sat/debris:
  `collisions_detected=1`)
- `POST /api/maneuver/schedule` now validates cooldown and fuel projection,
  queues accepted burns, and `maneuvers_executed` increments once due burns
  execute on `simulate/step`
- `/api/debug/propagation` now includes narrow-phase counters:
  `narrow_pairs_checked`, `collisions_detected`, `maneuvers_executed`
- `/api/visualization/snapshot` now emits geodetic `lat/lon/alt` derived via
  ECI -> ECEF -> WGS-84 conversion path
- maneuver scheduling now performs a static ground-station LOS check at each
  burn time using provided PS station set and min-elevation limits
- `simulate/step` now includes a conservative auto-COLA hook that can queue
  and execute a minimal prograde impulse for satellites flagged in collision
- narrow-phase now evaluates a conservative short-horizon TCA window using
  endpoint-relative linear approximations plus safety guard margin

Current status note:

- LOS validation is currently a conservative burn-time-in-future proxy;
- station-geometry LOS windows are currently static point checks (no latency/
  upload-window planner yet)
- TCA-window narrow-phase is conservative and currently linearized; high-
  fidelity RK4 conjunction window solver remains pending
- auto-COLA currently uses fixed small impulse and does not perform recovery
  planning yet

Latest tuner snapshot (`240 50 10000 3 2`):

- strict-zero-risk: enabled
- disqualified non-zero-risk candidates: `43`
- safe population: `197`
- pareto set size: `3`
