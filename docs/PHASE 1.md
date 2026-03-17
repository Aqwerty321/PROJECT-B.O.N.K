# CASCADE Phase 1 Implementation Report

Date: 2026-03-17

## Scope completed

Phase 1 focused on building the backend foundation required by PS.md:

- In-memory SoA state store for telemetry objects
- Real telemetry ingestion via `simdjson` On-Demand
- Simulation clock with ISO-8601 handling
- API handlers wired to runtime state (instead of fixed stubs)
- Build integration for modular `src/` layout

## Files added

- `src/types.hpp`
- `src/json_util.hpp`
- `src/sim_clock.hpp`
- `src/sim_clock.cpp`
- `src/state_store.hpp`
- `src/state_store.cpp`
- `src/telemetry.hpp`
- `src/telemetry.cpp`

## Files updated

- `main.cpp`
- `CMakeLists.txt`

## Core architecture implemented

### 1) State management (SoA)

`StateStore` uses contiguous vectors for object state:

- Position ECI (km): `rx, ry, rz`
- Velocity ECI (km/s): `vx, vy, vz`
- Metadata: `id`, `type`
- Satellite attributes: `fuel_kg`, `mass_kg`, `status`
- ID index: `unordered_map<string, size_t>`

Pre-allocation is enabled for scale readiness:

- `DEFAULT_CAPACITY = 10,100`

Ingestion behavior:

- New ID: append to SoA arrays
- Existing ID: in-place overwrite of ECI state

### 2) Clock and time handling

`SimClock` stores simulation time as Unix epoch seconds (`double`) and supports:

- `set_from_iso(...)`
- `advance(step_seconds)`
- `to_iso()`
- wall-clock `uptime_s()` for status endpoint

### 3) Telemetry parser

`parse_telemetry(...)` in `src/telemetry.cpp`:

- Parses request body using `simdjson::ondemand`
- Reads batch timestamp and object list
- Extracts `id`, `type`, `r.{x,y,z}`, `v.{x,y,z}`
- Upserts into `StateStore`
- Seeds clock from first telemetry batch

## API behavior after Phase 1

### `POST /api/telemetry`

- Parses and stores real objects
- Returns:
  - `status: ACK`
  - `processed_count` (real)
  - `active_cdm_warnings: 0` (placeholder until screening phase)

### `POST /api/simulate/step`

- Parses `step_seconds`
- Advances simulation clock
- Increments tick counter
- Returns real `new_timestamp`

### `GET /api/status`

- Returns runtime values:
  - `uptime_s`
  - `tick_count`
  - `object_count`

### `GET /api/visualization/snapshot`

- Returns objects from in-memory state
- Satellites include `fuel_kg` and `status`
- `lat/lon/alt` placeholders are currently `0.0`

### `POST /api/maneuver/schedule`

- Kept as phase-stub behavior for scheduling logic
- Reads `satelliteId` and reports current projected mass from state when available

## Build system updates

`CMakeLists.txt` now compiles modular sources:

- `src/state_store.cpp`
- `src/telemetry.cpp`
- `src/sim_clock.cpp`

And includes:

- `target_include_directories(ProjectBONK PRIVATE ${CMAKE_SOURCE_DIR}/src)`

## Validation run (local)

Build:

```bash
cmake --build build --target ProjectBONK -j$(nproc)
```

Smoke-test results validated:

- `GET /api/status` before telemetry -> `object_count = 0`
- Telemetry ingest of 3 objects -> `processed_count = 3`
- `GET /api/status` after telemetry -> `object_count = 3`
- `POST /api/simulate/step` with `3600` seconds -> timestamp advanced by 1 hour
- `GET /api/visualization/snapshot` returned 2 satellites and 1 debris item from state

## Known limitations intentionally deferred

- Strict request validation/error schema hardening (planned next)
- Orbital elements cache and propagation engine (Phase 2)
- Collision screening / CDM generation (Phase 4)
- Maneuver optimizer and constraint enforcement (Phase 5)
- ECI->geodetic conversion for visualization (Phase 6)

## Phase 2 entry point

Next phase begins with:

- API hardening and deterministic error responses
- ECI <-> Kepler conversion module
- J2-secular + Kepler fast propagation
- RK4 guarded fallback implementation
- Integration into `POST /api/simulate/step`
