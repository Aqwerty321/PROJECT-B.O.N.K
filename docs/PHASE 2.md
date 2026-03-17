# CASCADE Phase 2 Implementation Report

Date: 2026-03-17

## Scope completed

Phase 2 delivered API hardening, conflict observability, orbital mechanics core,
and adaptive propagation wiring in `POST /api/simulate/step`.

## Architectural decisions implemented

- ECI state remains authoritative for runtime storage and endpoint output.
- Derived orbital elements are persisted in SoA as a cached representation.
- Telemetry policy:
  - reject malformed payloads
  - reject stale telemetry (`timestamp < sim_clock`) with HTTP `409`
  - accept newer telemetry without auto-advancing simulation clock
- Type conflict policy:
  - reject object updates where canonical type differs from incoming type
  - never silently mutate object type
  - append audit record to bounded in-memory ring buffer
- Propagation strategy:
  - adaptive mode with guarded RK4 fallback
  - default thresholds: `step_seconds > 21600`, `perigee_alt_km < 350`, or `e >= 0.98`
  - added runtime probe for low-threshold cases:
    - run fast prediction and coarse RK4 probe
    - escalate object to RK4 if probe drift exceeds
      - 0.5 km in position OR
      - 0.5 m/s in velocity
  - added low-risk fast lane:
    - if `dt <= 30s`, `e <= 0.02`, and perigee altitude `>= 500 km`,
      use fast propagation directly
    - extended lane for ultra-low-risk profiles:
      - `dt <= 45s`, `e <= 0.003`, perigee altitude `>= 650 km`
    - fallback to RK4 if fast propagation fails
- Step-before-telemetry is blocked with deterministic error response.

## Files added

- `src/orbit_math.hpp`
- `src/orbit_math.cpp`
- `src/propagator.hpp`
- `src/propagator.cpp`
- `tools/phase2_regression.cpp`

## Files updated

- `main.cpp`
- `src/telemetry.hpp`
- `src/telemetry.cpp`
- `src/state_store.hpp`
- `src/state_store.cpp`
- `src/json_util.hpp`
- `src/sim_clock.hpp`
- `src/sim_clock.cpp`
- `src/types.hpp`
- `CMakeLists.txt`

## New API behavior

### Error response schema

All validation failures now use:

```json
{"status":"ERROR","code":"...","message":"..."}
```

### `POST /api/telemetry`

- parse/validate outside lock
- commit under lock
- malformed JSON -> `400 MALFORMED_JSON`
- stale telemetry -> `409 STALE_TELEMETRY`
- partial object validity is supported:
  - invalid objects are skipped
  - valid objects are ingested
  - response remains PS-compatible:

```json
{"status":"ACK","processed_count":N,"active_cdm_warnings":0}
```

### `POST /api/simulate/step`

- validates `step_seconds` is present and `> 0`
- blocks until telemetry initializes simulation clock
- propagates each object using adaptive mode
- best-effort semantics:
  - propagation failures are counted internally
  - endpoint keeps PS response shape

### `POST /api/maneuver/schedule`

- request shape validation added (`satelliteId`, `maneuver_sequence`)
- unknown satellite now returns `404 SATELLITE_NOT_FOUND`

### `GET /api/debug/conflicts`

New diagnostics endpoint exposing:

- total conflict count
- ring size
- per-source counters
- recent conflict records

### `GET /api/debug/propagation`

New diagnostics endpoint exposing propagation runtime mix:

- last-tick counts for adaptive-fast / RK4 / probe-escalated objects
- cumulative counts for adaptive-fast / RK4 / probe-escalated objects
- failed object counts (last tick and total)
- active adaptive threshold config values (mode, fast-lane, and probe gates)

## StateStore enhancements

### Orbital-element cache (SoA)

Stored per object:

- `a, e, i, raan, argp, M, n, p, rp, ra`
- `elements_valid` flag
- per-object `telemetry_epoch_s`

### Conflict audit ring

- fixed capacity: `4096`
- FIFO overwrite semantics at capacity
- records include:
  - object ID
  - stored and incoming type
  - telemetry timestamp
  - ingestion timestamp
  - source ID
  - reason

### Map lookup optimization

- switched to transparent string hashing/equality to avoid temporary string
  allocations in `find` and `upsert` lookups.

## Propagation modules

### `orbit_math`

- ECI <-> classical orbital element conversion
- Kepler equation solver (elliptic)
- J2 secular rates and drift application

### `propagator`

- `propagate_fast_j2_kepler(...)`
- `propagate_rk4_j2(...)`
- `propagate_rk4_j2_substep(...)`
- `propagate_adaptive(...)`
- adaptive mode selector based on configured guarded thresholds

## Regression harness

Added `phase2_regression` executable for fast model vs RK4 comparison.

Build:

```bash
cmake -S . -B build
cmake --build build --target phase2_regression -j$(nproc)
```

Run:

```bash
./build/phase2_regression 3000
# optional: explicit dt range
./build/phase2_regression 3000 300 86400
```

Observed run output (3000 samples):

- `adaptive_used_fast = 1`
- `adaptive_used_rk4 = 2999`
- `adaptive_escalated_after_probe = 398`
- Fast-only model remains above strict target (expected for broad-phase use)
- Adaptive runtime path now passes configured accuracy gate:
  - `adaptive_pos_err_km_max = 0.060649`
  - `adaptive_vel_err_ms_max = 0.059288`
  - `adaptive_target_pos_le_1km = PASS`
  - `adaptive_target_vel_le_1ms = PASS`

Interpretation: current adaptive policy is accuracy-safe but heavily RK4-biased;
performance tuning is still required before full-scale throughput optimization.

Additional short-step sweep (`dt = 1..120s`, 3000 samples):

- `adaptive_used_fast = 666`
- `adaptive_used_rk4 = 2334`
- `adaptive_target_pos_le_1km = PASS`
- `adaptive_target_vel_le_1ms = PASS`

Interpretation: fast-lane acceptance is meaningfully higher for short operational
timesteps while retaining strict accuracy gates.

## Validation summary

Local checks run successfully:

- `cmake --build build --target ProjectBONK`
- endpoint smoke tests covering:
  - pre-telemetry simulate rejection
  - mixed valid/invalid telemetry
  - type conflict logging and diagnostics endpoint
  - stale telemetry rejection
  - normal simulation step flow
  - JSON escaping correctness in snapshot payloads

## Next steps (Phase 2.1)

- reduce RK4 utilization while preserving 1 km / 1 m/s adaptive gate
- introduce targeted fast-path improvements for low-e, low-dt objects
- add deterministic regression thresholds and perf envelopes in CI
