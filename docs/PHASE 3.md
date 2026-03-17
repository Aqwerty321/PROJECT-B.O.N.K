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
- median tick: `~4.0 ms`
- p95 tick: `~4.3 ms`
- broad-phase counters available from engine stats (`pairs considered`,
  `shell-overlap candidates`)

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
- add orbital-band indexing and D-criterion stage before shell overlap
- connect broad-phase candidate list into Phase 4 narrow-phase pipeline

## Offline tuner scaffold

`offline_multiobjective_tuner` is intentionally separate from runtime path.

- explores broad-phase config space
- reports Pareto candidates minimizing:
  - D-criterion rejection proxy (risk)
  - pairs after band indexing (compute)
  - candidate count (compute)
