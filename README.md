# C.A.S.C.A.D.E

## Collision Avoidance System for Constellation Analysis and Debris Elimination

High-performance C++20 backend with an optional Julia/jluna runtime bridge for
autonomous orbital debris avoidance.  Built for the
**National Space Hackathon 2026** (IIT Delhi).

### Stack

| Layer | Technology |
|---|---|
| Language | C++20 (engine), optional Julia 1.10.x bridge via `jluna` |
| HTTP | `cpp-httplib` (header-only) |
| JSON parse | `simdjson` v3.9.4 (FetchContent) |
| Linear algebra | Boost headers, Eigen (planned) |
| Build | CMake >= 3.22, GCC 12, Docker (`ubuntu:22.04`) |

---

## Quick Start (Docker) -- recommended

### 1. Build image

```bash
docker build -t cascade:local .
```

### 2. Run container

```bash
docker run --rm -p 8000:8000 cascade:local
```

Expected output:

```text
CASCADE (Project BONK) SYSTEM ONLINE
Boost version : 1_74
Starting HTTP server on 0.0.0.0:8000 ...
```

### 3. Smoke test

```bash
curl -s http://localhost:8000/api/status | python3 -m json.tool
curl -s -X POST http://localhost:8000/api/telemetry -H 'Content-Type: application/json' \
     -d '{"timestamp":"2026-03-12T08:00:00.000Z","objects":[]}' | python3 -m json.tool
```

---

## API Endpoints

All endpoints are served on port **8000** (bound to `0.0.0.0`).
Response schemas follow `PS.md`.

| Method | Path | Status | Description |
|---|---|---|---|
| `POST` | `/api/telemetry` | 200 | Ingest ECI state vectors |
| `POST` | `/api/maneuver/schedule` | 202 | Schedule evasion/recovery burns |
| `POST` | `/api/simulate/step` | 200 | Advance simulation by N seconds |
| `GET`  | `/api/visualization/snapshot` | 200 | Snapshot for frontend rendering |
| `GET`  | `/api/status` | 200 | Engine health & tick count |
| `GET`  | `/api/debug/conflicts` | 200 | Type-conflict diagnostics ring buffer |
| `GET`  | `/api/debug/propagation` | 200 | Adaptive propagation usage counters |

---

## Frontend/Backend Split (Dev)

Backend remains on port `8000`. For local split development with a frontend dev
server on `5173`, enable CORS on the backend process:

```bash
PROJECTBONK_CORS_ENABLE=true \
PROJECTBONK_CORS_ALLOW_ORIGIN=http://localhost:5173 \
./build/ProjectBONK
```

Optional CORS controls:

- `PROJECTBONK_CORS_ALLOW_CREDENTIALS=true|false` (default: `false`)
- `PROJECTBONK_CORS_ALLOW_ORIGIN` supports comma-separated origins (default
  when enabled: `http://localhost:5173`)
- `PROJECTBONK_CORS_ALLOW_METHODS` (default: `GET, POST, OPTIONS`)
- `PROJECTBONK_CORS_ALLOW_HEADERS` (default: `Content-Type, X-Source-Id`)

The backend echoes `Access-Control-Allow-Origin` only for matching allowed
origins and sets `Vary: Origin` for cache safety.

Example allowlist:

```bash
PROJECTBONK_CORS_ENABLE=true \
PROJECTBONK_CORS_ALLOW_ORIGIN="http://localhost:5173,https://demo.example.com" \
./build/ProjectBONK
```

CORS is disabled by default. API schemas and status/error contracts remain
unchanged (see `PS.md`).

---

## Reverse Proxy Deployment (Demo/Prod)

For a single-origin deployment, serve frontend static files and proxy `/api/*`
to `ProjectBONK` on `127.0.0.1:8000`.

Minimal nginx example:

```nginx
server {
    listen 80;
    server_name _;

    root /srv/cascade-frontend;
    index index.html;

    location /api/ {
        proxy_pass http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    location / {
        try_files $uri $uri/ /index.html;
    }
}
```

Keep `/api` path and response payloads unchanged to preserve PS contract
compatibility.

---

## Local Build (Ubuntu 22.04)

Docker is the recommended path. Local builds work on Linux with Julia runtime
disabled by default (`-DPROJECTBONK_ENABLE_JULIA_RUNTIME=OFF`).

### 1. Install toolchain and base libraries

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake gcc-12 g++-12 git curl wget libboost-all-dev
```

### 2. Configure and build

```bash
rm -rf build
CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 cmake -S . -B build \
  -DPROJECTBONK_ENABLE_JULIA_RUNTIME=OFF
cmake --build build -j"$(nproc)"
```

Optional: enable Julia runtime bridge for `ProjectBONK`.

```bash
CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 cmake -S . -B build \
  -DPROJECTBONK_ENABLE_JULIA_RUNTIME=ON
cmake --build build --target ProjectBONK -j"$(nproc)"
```

Optional: build Phase 2 regression harness.

```bash
cmake --build build --target phase2_regression -j"$(nproc)"
./build/phase2_regression 3000
```

Hard-fail adaptive regression gate:

```bash
cmake --build build --target phase2_regression_gate
# or
./scripts/phase2_regression_gate.sh
```

Hard-fail broad-phase sanity gate:

```bash
cmake --build build --target broad_phase_sanity_gate
# or
./scripts/broad_phase_sanity_gate.sh
```

Hard-fail recovery slot acceptance gate:

```bash
cmake --build build --target recovery_slot_acceptance_gate
# or
./scripts/recovery_slot_gate.sh
```

Hard-fail recovery planner invariants gate:

```bash
cmake --build build --target recovery_planner_invariants_gate
# or
./scripts/recovery_planner_invariants_gate.sh
```

Maneuver schedule endpoint currently enforces:

- per-burn delta-V limit (`<= 15 m/s`)
- 600s cooldown within sequence and against last executed burn per satellite
- projected fuel/mass check via Tsiolkovsky relation
- static LOS-at-burn-time check against PS ground station set and minimum
  elevation limits
- burn queue execution on `POST /api/simulate/step` with
  `maneuvers_executed` reported in response

Simulation step currently includes a conservative auto-COLA hook: when
collision detections are present, eligible satellites may receive a small
prograde burn (subject to LOS/cooldown/fuel safety checks).

Executed auto-COLA burns now generate pending recovery requests; runtime then
attempts recovery burns under the same cooldown/LOS/fuel safety constraints.

Recovery burns are now slot-targeted (heuristic element-delta correction using
semi-major axis/eccentricity/inclination/RAAN deltas), then capped to burn
limits.

Recovery planner calibration can be configured at runtime with:

- `PROJECTBONK_RECOVERY_SCALE_T`
- `PROJECTBONK_RECOVERY_SCALE_R`
- `PROJECTBONK_RECOVERY_RADIAL_SHARE`
- `PROJECTBONK_RECOVERY_SCALE_N`
- `PROJECTBONK_RECOVERY_FALLBACK_NORM_KM_S`
- `PROJECTBONK_RECOVERY_MAX_REQUEST_RATIO`

`PROJECTBONK_RECOVERY_MAX_REQUEST_RATIO` bounds per-burn commanded recovery
norm relative to remaining requested recovery budget.

Recovery planner counters are visible in debug/status details outputs
(`recovery_planned`, `recovery_deferred`, `recovery_completed`).

Collision detection in `simulate/step` uses a conservative short-horizon
TCA-window approximation (not endpoint-only distance).

Near-threshold conjunction pairs are further refined with a targeted RK4
micro-window pass; refinement failures are treated fail-open for safety.

Ultra-near-threshold pairs may trigger a budgeted sampled full-window RK4
refinement pass before final collision classification.

Full-window refinement budget is adaptive per tick (candidate load, step
duration, and propagation failure pressure).

`GET /api/status` keeps PS-compatible default fields; add `?details=1` (or
`?verbose=1`) to include internal diagnostics metrics such as queue depth,
backpressure counters, and per-command queue/execution latency stats.

Visualization snapshot currently includes geodetic outputs (`lat/lon/alt`)
computed from ECI state vectors.

Optional: run CTest safety suite.

```bash
cmake -S . -B build -DBUILD_TESTING=ON
ctest --test-dir build --output-on-failure
```

Optional: build and run Phase 3 synthetic tick benchmark.

```bash
cmake --build build --target phase3_tick_benchmark -j"$(nproc)"
./build/phase3_tick_benchmark
```

Optional: run offline multi-objective tuner scaffold (separate path).

```bash
cmake --build build --target offline_multiobjective_tuner -j"$(nproc)"
# args: samples satellites debris train_scenarios eval_scenarios
./build/offline_multiobjective_tuner 240 50 10000 3 2
```

Optional: run recovery gain sweep helper (offline tuning, not runtime path).

```bash
# strict profile (default)
./scripts/recovery_slot_sweep.sh ./build 24 0.08

# strict-expanded profile (deeper deterministic candidate set)
./scripts/recovery_slot_sweep.sh ./build 24 0.08 1.10 strict-expanded

# optional explicit artifact path with profile
./scripts/recovery_slot_sweep.sh ./build 24 0.08 1.10 \
  ./build/recovery_slot_sweep_custom.json strict-expanded
```

`recovery_slot_gate --sweep` supports profiles:

- `strict` (default): baseline reproducibility candidate set
- `strict-expanded`: `strict` candidates plus deterministic low-fallback grid
  and fallback-sensitivity variants

Strict sweep criteria:

- every scenario must satisfy `delta_slot_error <= 0.08`
- every scenario must execute at least one recovery burn
- candidate mean fuel must satisfy
  `mean_fuel_used_kg <= default_candidate_mean_fuel_used_kg * 1.10`

Current deterministic sweep snapshot in this branch:

- strict and strict-expanded profiles select `fallback_1e-4_ratio_0.05`
- see `docs/PHASE 4.md` for exact evidence and artifact paths

Promotion policy for this milestone:

- sweep only produces evidence and recommended candidate
- runtime recovery gains stay unchanged until a separate promotion decision
- promote runtime gains only in a separate commit after deterministic repeated
  sweep runs confirm the same strict-passing candidate

CMake will automatically:

- Fetch `simdjson` v3.9.4 and `cpp-httplib` v0.15.3 via FetchContent.
- When `PROJECTBONK_ENABLE_JULIA_RUNTIME=ON`:
  - download Julia 1.10.0 into `build/_deps/julia` when `julia` is not on
    `PATH` (Linux only).
  - fetch and build `jluna` v1.0.1 if no installed package is found.

Override paths if you have local installations:

```bash
cmake -S . -B build \
  -DPROJECTBONK_ENABLE_JULIA_RUNTIME=ON \
  -DJULIA_BINDIR=/path/to/julia/bin \
  -DJLUNA_DIR=/path/to/jluna
```

### 3. Run

```bash
./build/ProjectBONK
```

---

## Docker Layer Caching

The Dockerfile is structured for fast incremental rebuilds:

1. **apt + Julia install** — cached unless Dockerfile base changes.
2. **`COPY CMakeLists.txt` + `cmake -DPROJECTBONK_PREFETCH_ONLY=ON`** —
   fetches `jluna`, `simdjson`, and `cpp-httplib`.  Cached as long as
   `CMakeLists.txt` does not change.
3. **`COPY main.cpp` (and future `src/`)** — only this layer and the
   final build re-run when C++ source files change.

---

## Project Layout

```
.
├── CMakeLists.txt          # Build system (C++20, FetchContent deps)
├── Dockerfile              # Single-container build & run
├── main.cpp                # API server entry point
├── src/                    # Engine modules (state, telemetry, orbit math, propagation)
├── tools/                  # Local dev tools (regression/benchmark/validation)
├── tuner/                  # Offline optimization scaffold (not runtime path)
├── scripts/                # Safety gate runner scripts
├── docs/                   # Phase reports and implementation notes
├── PS.md                   # Authoritative problem statement (IIT Delhi)
├── ARCHITECTURE.md         # Internal architecture & dependency guide
├── README.md               # This file
└── .dockerignore
```

---

## Notes

- `main.cpp` calls `jluna::initialize()` at startup.
- If `jluna` is not installed, CMake fetches `v1.0.1` automatically during
  configure.
- `PROJECTBONK_FORCE_LOCAL_INSTALL_PREFIX=ON` avoids root-permission linker
  output paths by default.
- `.dockerignore` excludes host build artifacts (including the `.src` jluna
  symlink) to prevent CMake cache conflicts in container builds.
- If you see CMake cache path mismatch errors, remove the local `build/`
  directory and rebuild.
- `libsimdjson-dev` is **not** required in apt; simdjson is fetched by CMake
  for version consistency.
