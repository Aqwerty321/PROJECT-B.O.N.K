# CASCADE Phase 3 Implementation Notes (Start)

Date: 2026-03-17

## Goal

Phase 3 wires a dedicated simulation tick engine so `POST /api/simulate/step`
is no longer coupled to endpoint handler logic and can be benchmarked/reused.

## Delivered in this start commit

- Added `src/simulation_engine.hpp`
- Added `src/simulation_engine.cpp`
- Refactored `main.cpp` simulate endpoint to call `run_simulation_step(...)`
- Added synthetic benchmark `tools/phase3_tick_benchmark.cpp`
- Updated `CMakeLists.txt` with:
  - `src/simulation_engine.cpp` in `ProjectBONK`
  - new target `phase3_tick_benchmark`

## Why this matters

- Centralizes simulation stepping in one module (clean boundary for Phase 3+)
- Makes propagation behavior testable outside HTTP stack
- Enables direct runtime/perf measurement at hackathon scale

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
- prepare broad-phase candidate generation module for Phase 4 handoff
