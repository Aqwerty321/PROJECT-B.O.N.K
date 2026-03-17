# C.A.S.C.A.D.E

## Collision Avoidance System for Constellation Analysis and Debris Elimination

High-performance C++20/Julia hybrid engine for autonomous orbital debris
avoidance.  Built for the **National Space Hackathon 2026** (IIT Delhi).

### Stack

| Layer | Technology |
|---|---|
| Language | C++20 (engine), Julia 1.10.0 (numerics via `jluna`) |
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

## Local Build (Ubuntu 22.04)

Docker is the recommended path.  Local builds work on Linux and will
auto-download Julia 1.10.0 if it is not already on `PATH`.

### 1. Install toolchain and base libraries

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake gcc-12 g++-12 git curl wget libboost-all-dev
```

### 2. Configure and build

```bash
rm -rf build
CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 cmake -S . -B build
cmake --build build -j"$(nproc)"
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

CMake will automatically:

- Fetch `simdjson` v3.9.4 and `cpp-httplib` v0.15.3 via FetchContent.
- Download Julia 1.10.0 into `build/_deps/julia` when `julia` is not on
  `PATH` (Linux only).
- Fetch and build `jluna` v1.0.1 if no installed package is found.

Override paths if you have local installations:

```bash
cmake -S . -B build -DJULIA_BINDIR=/path/to/julia/bin -DJLUNA_DIR=/path/to/jluna
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
