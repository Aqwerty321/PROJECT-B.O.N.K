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
- tune adaptive full-window refinement budget policy using mixed load scenarios
- tune slot-targeted recovery gains using scenario-based acceptance sweeps

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
- `recovery_slot_gate` (slot-targeted recovery acceptance)
- `recovery_planner_invariants_gate` (collision-pressure/fuel-floor planner invariants)
- CI workflow runs all four gates on push/PR
- CI config keeps Julia runtime bridge disabled
  (`PROJECTBONK_ENABLE_JULIA_RUNTIME=OFF`) so hard safety gates are not coupled
  to external jluna/Julia compatibility drift
- runtime broad-phase keeps D-criterion disabled by default until Phase 4
  narrow-phase integration proves no-FN behavior end-to-end

Latest gate snapshot:

- adaptive gate: PASS on both configured sweeps
- broad-phase sanity gate: PASS (`missing_vs_shell_baseline_total = 0`)
- recovery slot gate: PASS (recovery path not worse than baseline in synthetic
  acceptance scenario)
- recovery planner invariants gate: PASS (no recovery scheduling under
  persistent collision pressure or fuel-floor violation)
- recovery sweep helper supports `strict` (default) and `strict-expanded`
  profiles; strict defaults remain (`scenarios=24`, `margin=0.08`,
  `fuel_ratio_cap=1.10`) and artifacts are emitted at
  `build/recovery_slot_sweep_strict.json` or
  `build/recovery_slot_sweep_strict_expanded.json` by profile
- latest strict sweep snapshot: deterministic `selection.status=FAIL` with
  explicit reason `no candidate met strict scenario + fuel-ratio criteria`

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
- narrow-phase now includes a targeted RK4 micro-window refinement for near-
  threshold pairs, with fail-open behavior if refinement integration fails
- narrow-phase now includes a budgeted full-window RK4 sample refinement for
  ultra-near-threshold pairs, with fail-open and budget-exhaustion counters
- full-window refinement budget is now adaptive per tick based on candidate
  load, step size, and propagation failure pressure
- `/api/status` now supports optional details mode (`?details=1`) exposing
  internal metrics without changing default PS response schema
- auto-COLA now records a pending recovery request and the runtime attempts a
  cooldown/LOS/fuel-safe recovery burn plan each tick
- status details/debug endpoints now expose recovery planner counters
  (`recovery_planned`, `recovery_deferred`, `recovery_completed`)
- recovery planner now computes slot-targeted correction impulses from current
  orbital element deltas (a/e/i/RAAN) with conservative per-burn capping
- `tools/recovery_slot_gate.cpp` now supports parameterized multi-scenario
  acceptance checks and deterministic sweep mode for offline gain tuning,
  including profile-based candidate sets (`strict`, `strict-expanded`),
  candidate ranking evidence, and JSON artifact output

Current status note:

- station-geometry LOS windows are currently static point checks (no latency/
  upload-window planner yet)
- TCA-window narrow-phase is conservative and currently linearized; high-
  fidelity RK4 conjunction window solver remains pending (current full-window
  pass is budgeted and only applies to ultra-near-threshold pairs)
- adaptive budget telemetry is exposed via debug counters to monitor safety/
  performance tradeoff over time
- recovery planner may defer when conjunction pressure persists; slot-targeted
  gain tuning and
  mission-box objective shaping remain pending
- strict sweep results are evidence-only in this milestone; runtime gain
  promotion remains deferred
- runtime gain promotion only proceeds in a separate commit once deterministic
  repeated sweeps select the same strict-passing candidate

## Phase 4 safety calibration kickoff (2026-03-18)

This branch starts safety-first calibration work while preserving PS contracts
and mandatory gates.

- recovery planner gains are now runtime-configurable via env:
  - `PROJECTBONK_RECOVERY_SCALE_T`
  - `PROJECTBONK_RECOVERY_SCALE_R`
  - `PROJECTBONK_RECOVERY_RADIAL_SHARE`
  - `PROJECTBONK_RECOVERY_SCALE_N`
  - `PROJECTBONK_RECOVERY_FALLBACK_NORM_KM_S`
- narrow-phase hardening adds configurable thresholds/budgets and uncertainty
  promotion for high-relative-speed near-threshold pairs:
  - `PROJECTBONK_NARROW_TCA_GUARD_KM`
  - `PROJECTBONK_NARROW_REFINE_BAND_KM`
  - `PROJECTBONK_NARROW_FULL_REFINE_BAND_KM`
  - `PROJECTBONK_NARROW_HIGH_REL_SPEED_KM_S`
  - `PROJECTBONK_NARROW_HIGH_REL_SPEED_EXTRA_BAND_KM`
  - `PROJECTBONK_NARROW_FULL_REFINE_BUDGET_BASE`
  - `PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MIN`
  - `PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MAX`
  - `PROJECTBONK_NARROW_FULL_REFINE_SAMPLES`
  - `PROJECTBONK_NARROW_FULL_REFINE_SUBSTEP_S`
  - `PROJECTBONK_NARROW_MICRO_REFINE_MAX_STEP_S`
- broad-phase D-criterion rollout now supports shadow-only evidence mode in
  runtime and tooling:
  - `PROJECTBONK_BROAD_DCRITERION_ENABLE` (default `0`)
  - `PROJECTBONK_BROAD_DCRITERION_SHADOW` (default `1`)
  - debug/status counters include `broad_dcriterion_shadow_rejected`

Safety-gate snapshot on this kickoff branch:

- `phase2_regression_gate`: PASS
- `broad_phase_sanity_gate`: PASS
- `narrow_phase_false_negative_gate`: PASS
- `maneuver_ops_invariants_gate`: PASS
- `recovery_slot_gate`: PASS
- `recovery_planner_invariants_gate`: PASS
- `api_contract_gate`: PASS
- `ctest --test-dir build --output-on-failure`: PASS (8/8)

Latest tuner snapshot (`240 50 10000 3 2`):

- strict-zero-risk: enabled
- disqualified non-zero-risk candidates: `43`
- safe population: `197`
- pareto set size: `3`
