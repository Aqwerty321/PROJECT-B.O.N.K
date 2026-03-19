# CASCADE Handoff Runbook

Date: 2026-03-18

## Current status snapshot

- Phase 1 complete: state store + telemetry + clock + API wiring
- Phase 2/2.1 complete: adaptive propagation, strict regression gate, diagnostics
- Phase 3 started: simulation engine extraction, conservative broad-phase module
- Safety policy: **false negatives are unacceptable**; fail-open when uncertain

## Module map

- `main.cpp`
  - HTTP API surface
  - debug aggregation endpoint (`/api/debug/propagation`)
  - maneuver queue validation/execution (fuel + cooldown + due-burn apply)
- `src/state_store.*`
  - SoA runtime state
  - orbital-element cache
  - conflict ring buffer
- `src/telemetry.*`
  - strict payload parse and ingest
  - stale timestamp policy
- `src/orbit_math.*`
  - ECI <-> Kepler conversion
  - Kepler solver + J2 secular helpers
- `src/propagator.*`
  - adaptive fast/RK4 propagation policy
- `src/simulation_engine.*`
  - tick orchestration for `/api/simulate/step`
- `src/broad_phase.*`
  - conservative candidate generation
  - band indexing + shell overlap + fail-open D-criterion gate

## Tooling map

- `tools/phase2_regression.cpp`
  - adaptive propagation quality checks
  - supports `--strict-adaptive`
- `scripts/phase2_regression_gate.sh`
  - hard-fail gate wrapper
- `tools/broad_phase_validation.cpp`
  - shell-baseline safety validation for indexed broad-phase
- `scripts/broad_phase_sanity_gate.sh`
  - hard-fail broad-phase gate wrapper
- `tools/phase3_tick_benchmark.cpp`
  - synthetic tick latency benchmark
- `tuner/offline_multiobjective_tuner.cpp`
  - offline broad-phase parameter exploration (separate from runtime)
- `tools/maneuver_ops_invariants_gate.cpp`
  - focused maneuver/upload/graveyard invariant checks
- `scripts/maneuver_ops_invariants_gate.sh`
  - hard-fail maneuver ops invariant gate wrapper

### Dev debug toolchain (installed on PATH)

- `gdb`, `lldb`, `valgrind`, `perf`, `strace`, `ltrace`, `rr`, `heaptrack`
- static checks: `cppcheck`, `clang-tidy`, `clang-format`
- coverage/build helpers: `lcov`, `gcov`, `ninja`, `ctest`

## Runbook

### Build core targets

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROJECTBONK_ENABLE_JULIA_RUNTIME=OFF
cmake --build build --target ProjectBONK
cmake --build build --target phase2_regression_gate
cmake --build build --target broad_phase_sanity_gate
cmake --build build --target recovery_slot_acceptance_gate
cmake --build build --target recovery_planner_invariants_gate
cmake --build build --target maneuver_ops_invariants_regression_gate
cmake --build build --target phase3_tick_benchmark
cmake --build build --target offline_multiobjective_tuner
```

Note: this environment intermittently reports "No rule to make target" on some
multi-target invocations even when targets exist; separate target builds are
used in runbooks and CI for reliability.

### Mandatory safety gate

```bash
cmake --build build --target phase2_regression_gate
# or
./scripts/phase2_regression_gate.sh
```

### Mandatory broad-phase sanity gate

```bash
cmake --build build --target broad_phase_sanity_gate
# or
./scripts/broad_phase_sanity_gate.sh
```

### Mandatory narrow-phase false-negative gate

```bash
cmake --build build --target narrow_phase_false_negative_regression_gate
# or
./scripts/narrow_phase_false_negative_gate.sh ./build 10 8 120
```

### Mandatory recovery slot gate

```bash
cmake --build build --target recovery_slot_acceptance_gate
# or
./scripts/recovery_slot_gate.sh
```

### Mandatory recovery planner invariants gate

```bash
cmake --build build --target recovery_planner_invariants_gate
# or
./scripts/recovery_planner_invariants_gate.sh
```

### Mandatory maneuver ops invariants gate

```bash
cmake --build build --target maneuver_ops_invariants_regression_gate
# or
./scripts/maneuver_ops_invariants_gate.sh
```

### CTest gate suite

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
ctest --test-dir build --output-on-failure
```

### Tick benchmark (scale target)

```bash
./build/phase3_tick_benchmark 50 10000 5 10 30
```

### Offline tuner

```bash
# args: samples satellites debris train_scenarios eval_scenarios
./build/offline_multiobjective_tuner 240 50 10000 3 2
```

### Optional recovery sweep helper

```bash
# strict profile (default)
./scripts/recovery_slot_sweep.sh ./build 24 0.08

# strict-expanded profile (deeper deterministic candidate set)
./scripts/recovery_slot_sweep.sh ./build 24 0.08 1.10 strict-expanded

# deterministic repeat selection gate
./scripts/recovery_sweep_determinism_gate.sh ./build 3 24 0.08 1.10

# phase4 aggregate calibration gate (determinism + observability + safety suite)
./scripts/phase4_calibration_gate.sh ./build
```

Default sweep artifacts by profile:

- `strict`: `build/recovery_slot_sweep_strict.json`
- `strict-expanded`: `build/recovery_slot_sweep_strict_expanded.json`

Strict sweep interpretation:

- selected candidate is ranked by: lowest mean delta slot error, then lowest
  mean fuel use, then lexical candidate name
- this output is advisory for offline tuning; runtime gains remain unchanged in
  this milestone
- runtime gain promotion requires a separate commit and deterministic repeated
  runs selecting the same strict-passing candidate

## PS completeness matrix

| Endpoint | PS requires | Current status | Gap | Next task |
|---|---|---|---|---|
| `POST /api/telemetry` | strict ingest + ACK counters | implemented | no payload-level quality metrics in response | keep response PS-clean, expose extra diagnostics only in debug |
| `POST /api/simulate/step` | timestamp advance + collision/maneuver counts | partial | slot-targeted recovery planner exists; gain tuning and mission-box objective shaping remain | calibrate recovery gains against station-keeping scenarios |
| `POST /api/maneuver/schedule` | validation incl LOS/fuel/cooldown | implemented | upload station/epoch metadata not returned in PS response body | keep PS response shape; expose upload metadata only via debug endpoints if required |
| `GET /api/visualization/snapshot` | geodetic satellite/debris visualization fields | implemented | no uncertainty/quality fields yet | add optional confidence metadata in debug path |
| `GET /api/status` | health/tick/object counters | implemented | default schema remains PS-clean; details mode is non-PS extension | tune/expand optional details mode as needed |

## Phase 4 readiness checklist

- [x] adaptive regression gate stays green (`strict_adaptive`)
- [x] broad-phase counters exposed and stable under smoke scenarios
- [x] broad-phase validation harness confirms indexed path does not miss shell baseline pairs
- [x] candidate reduction is measurable without violating safety policy
- [x] handoff docs updated with exact commands/results used for acceptance

## Known open items before full Phase 4 integration

- recovery planner gains are now env-configurable for safety-first calibration
  (`PROJECTBONK_RECOVERY_*`) while defaults preserve prior behavior
- recovery planner logic is now isolated in `src/maneuver_recovery_planner.*` for focused tuning
- shared maneuver/upload/graveyard operations are extracted in `src/maneuver_common.*`
- broad-phase D-criterion remains runtime-disabled by default; shadow evidence is
  now exposed via debug/status counters for calibration rollout
- CI now enforces adaptive regression, broad-phase sanity, narrow-phase false-negative,
  recovery slot, recovery planner invariants, and maneuver ops invariants gates
- CI keeps Julia runtime bridge disabled (`PROJECTBONK_ENABLE_JULIA_RUNTIME=OFF`) to keep hard gates deterministic and avoid jluna/Julia version drift

## Phase 4 safety calibration kickoff (in progress)

- Recovery planner calibration path:
  - runtime now loads planner gains from env with guarded ranges
  - runtime now supports recovery request-ratio cap (`PROJECTBONK_RECOVERY_MAX_REQUEST_RATIO`)
  - debug propagation config now reports active recovery gain values
- Narrow-phase fidelity hardening:
  - narrow thresholds/budgets are now runtime-configurable (`PROJECTBONK_NARROW_*`)
  - high-relative-speed near-threshold pairs are uncertainty-promoted into full-window refinement
  - promoted-pair evidence is exposed in debug/status counters
- Broad-phase D-criterion rollout:
  - `enable_dcriterion` remains opt-in; `shadow_dcriterion` defaults on
  - shadow rejections are counted and exposed without filtering candidates when disabled

## Latest acceptance outputs

- `phase2_regression_gate`: PASS (both sweeps in strict adaptive mode)
- `broad_phase_sanity_gate`: PASS (`missing_vs_shell_baseline_total=0`,
  `dcriterion_rejected_total=0`)
- broad-phase shadow evidence is available (`dcriterion_shadow_rejected_total`)
- `narrow_phase_false_negative_gate`: PASS (`false_negative_sats_total=0`)
- `recovery_slot_gate`: PASS (`recovery_slot_gate_result=PASS`)
- `recovery_planner_invariants_gate`: PASS
- `maneuver_ops_invariants_gate`: PASS (`blackout_upload_execution=PASS`,
  `upload_prune=PASS`, `graveyard_offline_transition=PASS`)
- strict recovery sweep (`./scripts/recovery_slot_sweep.sh ./build 24 0.08`):
  writes `build/recovery_slot_sweep_strict.json`; latest strict run reports
  `selection.status=PASS` with selected candidate
  `grid_t1.2_r0.8_n0.8`
- repeated strict and strict-expanded sweeps selected the same candidate in this
  branch snapshot (see `docs/PHASE 4.md` artifact list)
- `phase3_tick_benchmark 50 10000 30`:
  mean `13.291 ms`, median `13.271 ms`, p95 `13.538 ms`
- `offline_multiobjective_tuner 240 50 10000 3 2`:
  strict-zero-risk enabled, disqualified `43`, safe population `197`, pareto
  set `3`
- API smoke (co-located SAT/DEB object pair with `step_seconds=1`):
  `collisions_detected=1`, `maneuvers_executed=1`
- API smoke (schedule validation):
  `COOLDOWN_VIOLATION` for <600s spacing,
  `UPLOAD_WINDOW_UNAVAILABLE` for no valid pre-burn upload windows
- API smoke (geodetic snapshot):
  populated `lat/lon/alt` fields from ECI->ECEF->WGS84 conversion
- API smoke (auto planning):
  collision on `simulate/step` auto-queues/executes conservative burn,
  debug shows `auto_planned_maneuvers=1`
- Narrow-phase behavior note:
  switched from endpoint-only distance check to conservative short-horizon
  TCA-window approximation with guard margin
- Refinement behavior note:
  near-threshold pairs are re-evaluated with RK4 micro-window refinement;
  refinement failures are fail-open and counted in debug counters
- Full-window refinement note:
  ultra-near-threshold pairs can trigger budgeted sampled RK4 window
  refinement; budget exhaustion is explicitly counted
- Adaptive budget note:
  per-tick budget allocation now scales with candidate load, step length,
  and propagation-failure pressure (reported in debug counters)
- Status endpoint note:
  `GET /api/status?details=1` now returns optional internal metrics (queue,
  refinement, broad-phase load) while default response remains PS-compatible
- Recovery planner note:
  auto-COLA burns create recovery requests; planner computes slot-targeted
  correction burns under cooldown/LOS/fuel guards and reports counters
- Communication/blackout note:
  command upload is validated against pre-burn windows with 10 s latency;
  burns can execute through blackout if uplink occurred before the latency cut
- Ground-station dataset note:
  runtime now loads stations from `docs/groundstations.csv` (or
  `PROJECTBONK_GROUND_STATIONS_CSV` when set); if unavailable it falls back to
  builtin PS-equivalent defaults
- Graveyard note:
  satellites at/under EOL fuel are reserved for graveyard transfer; successful
  graveyard execution transitions satellite status to `OFFLINE`
- Recovery smoke note:
  recovery plans are deferred while pending burns/collision pressure persist,
  and complete once a safe scheduling window is available

## Backend audit roadmap

- Detailed backend conformance audit and prioritized hardening plan are tracked in
  `docs/next-steps.md`
