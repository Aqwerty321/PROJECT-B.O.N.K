# CASCADE Handoff Runbook

Date: 2026-03-17

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

### Dev debug toolchain (installed on PATH)

- `gdb`, `lldb`, `valgrind`, `perf`, `strace`, `ltrace`, `rr`, `heaptrack`
- static checks: `cppcheck`, `clang-tidy`, `clang-format`
- coverage/build helpers: `lcov`, `gcov`, `ninja`, `ctest`

## Runbook

### Build core targets

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ProjectBONK
cmake --build build --target phase2_regression_gate
cmake --build build --target broad_phase_sanity_gate
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

## PS completeness matrix

| Endpoint | PS requires | Current status | Gap | Next task |
|---|---|---|---|---|
| `POST /api/telemetry` | strict ingest + ACK counters | implemented | no payload-level quality metrics in response | keep response PS-clean, expose extra diagnostics only in debug |
| `POST /api/simulate/step` | timestamp advance + collision/maneuver counts | partial | TCA window is conservative linearized approximation, not high-fidelity RK4 window solve yet | add high-fidelity conjunction window refinement |
| `POST /api/maneuver/schedule` | validation incl LOS/fuel/cooldown | partial | static LOS-at-burn-time check added (no latency/window planner yet) | integrate scheduler + station visibility window model |
| `GET /api/visualization/snapshot` | geodetic satellite/debris visualization fields | implemented | no uncertainty/quality fields yet | add optional confidence metadata in debug path |
| `GET /api/status` | health/tick/object counters | implemented | no queue/narrow-phase stats yet | add internal metrics expansion without schema drift |

## Phase 4 readiness checklist

- [x] adaptive regression gate stays green (`strict_adaptive`)
- [x] broad-phase counters exposed and stable under smoke scenarios
- [x] broad-phase validation harness confirms indexed path does not miss shell baseline pairs
- [x] candidate reduction is measurable without violating safety policy
- [x] handoff docs updated with exact commands/results used for acceptance

## Known open items before full Phase 4 integration

- automatic recovery planner not wired; current auto-COLA is minimal prograde impulse
- D-criterion is intentionally conservative; tune only through offline path
- CI now enforces both adaptive regression gate and broad-phase sanity gate

## Latest acceptance outputs

- `phase2_regression_gate`: PASS (both sweeps in strict adaptive mode)
- `broad_phase_sanity_gate`: PASS (`missing_vs_shell_baseline_total=0`,
  `dcriterion_rejected_total=0`)
- `phase3_tick_benchmark 50 10000 30`:
  mean `12.533 ms`, median `12.677 ms`, p95 `13.236 ms`
- `offline_multiobjective_tuner 240 50 10000 3 2`:
  strict-zero-risk enabled, disqualified `43`, safe population `197`, pareto
  set `3`
- API smoke (co-located SAT/DEB object pair with `step_seconds=1`):
  `collisions_detected=1`, `maneuvers_executed=1`
- API smoke (schedule validation):
  `COOLDOWN_VIOLATION` for <600s spacing,
  `GROUND_STATION_LOS_UNAVAILABLE` for no-LOS burn windows
- API smoke (geodetic snapshot):
  populated `lat/lon/alt` fields from ECI->ECEF->WGS84 conversion
- API smoke (auto planning):
  collision on `simulate/step` auto-queues/executes conservative burn,
  debug shows `auto_planned_maneuvers=1`
- Narrow-phase behavior note:
  switched from endpoint-only distance check to conservative short-horizon
  TCA-window approximation with guard margin
