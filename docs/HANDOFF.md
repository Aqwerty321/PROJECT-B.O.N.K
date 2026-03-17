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

## Runbook

### Build core targets

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ProjectBONK phase2_regression phase2_regression_gate broad_phase_validation broad_phase_sanity_gate phase3_tick_benchmark offline_multiobjective_tuner -j"$(nproc)"
```

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

### Tick benchmark (scale target)

```bash
./build/phase3_tick_benchmark 50 10000 5 10 30
```

### Offline tuner

```bash
# args: samples satellites debris train_scenarios eval_scenarios
./build/offline_multiobjective_tuner 240 50 10000 3 2
```

## Phase 4 readiness checklist

- [ ] adaptive regression gate stays green (`strict_adaptive`)
- [ ] broad-phase counters exposed and stable under smoke scenarios
- [x] broad-phase validation harness confirms indexed path does not miss shell baseline pairs
- [ ] candidate reduction is measurable without violating safety policy
- [ ] handoff docs updated with exact commands/results used for acceptance

## Known open items before full Phase 4 integration

- narrow-phase confirmation pipeline not yet wired to broad-phase candidate list
- D-criterion is intentionally conservative; tune only through offline path
- CI now enforces both adaptive regression gate and broad-phase sanity gate
