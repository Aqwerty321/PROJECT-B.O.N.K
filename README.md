<div align="center">

# CASCADE

### **C**ollision **A**voidance **S**ystem for **C**onstellation **A**utomation, **D**etection & **E**vasion

*National Space Hackathon 2026 · IIT Delhi*

**Team Space_Invaders**

</div>

---

Low Earth Orbit hosts over 27,000 tracked objects in an increasingly crowded shell, and every mega-constellation launch raises the probability of a cascading Kessler event. CASCADE is a fully autonomous, deterministic constellation manager that ingests high-frequency ECI J2000 telemetry, screens conjunctions through a mathematically conservative multi-tier filter cascade with a **provable zero-false-negative guarantee**, and autonomously plans evasion and recovery burns under realistic propulsion, cooldown, and line-of-sight constraints across a six-station global ground network.

Under the reference scenario (50 satellites, 10,000 debris), the C++20 engine completes a full simulation tick — propagation, screening, CDM generation, maneuver planning, and recovery scheduling — in **13.3 ms** mean wall-clock time. Validated against a live Space-Track OMM catalog of **27,191 LEO objects**, CASCADE maintains zero missed conjunctions, sub-kilometer station-keeping accuracy, and Tsiolkovsky-consistent fuel accounting across all test families.

---

## Performance at a Glance

| Metric | Value |
|---|---|
| Tick latency — 50 sats / 10,000 debris | **13.3 ms** mean |
| Tick latency — 100 sats / 20,000 debris | **72.6 ms** mean |
| Tick latency — 27,191 real LEO objects | **2.1 s** mean |
| False negatives across all test families | **0** |
| Propagator position error at 86,400 s | **< 0.061 km** max |
| Real catalog objects ingested (Space-Track) | **27,191** |
| CTest safety gates passing | **8 / 8** |
| OpenMP narrow-phase speedup (2× scale) | **−31.5%** |
| NSGA-II tuner parameters | **69** across 3 tiers |

---

## Evaluation Criteria Alignment

| Criterion | Weight | CASCADE Evidence |
|---|---|---|
| **Safety Score** | 25% | Zero false negatives (8 gates), fail-open escalation, analytical MOID (Gronchi 2005), conservative 120 m screening envelope |
| **Fuel Efficiency** | 20% | Tsiolkovsky accounting, CW/ZEM recovery solver, per-burn ΔV tracking, NSGA-II Pareto optimization |
| **Constellation Uptime** | 15% | 10 km slot box, J2-secular slot propagation, recovery convergence gate (Δslot ≤ 0.08) |
| **Algorithmic Speed** | 15% | O(N log N) broad phase, OpenMP-parallelized narrow sweep, adaptive RK4 probe-then-escalate |
| **UI/UX & Visualization** | 15% | 7-page mission console, ground track with trails, bullseye threat view, Gantt timeline, 60 FPS Canvas/WebGL |
| **Code Quality** | 10% | 8 CTest gates enforced in CI + Docker build, 69 env-overridable constants, structured per-phase timing, debug APIs |

---

## Quick Start

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
curl -s http://localhost:8000/api/status | python3 -m json.tool
```

The engine starts and the frontend is served at `http://localhost:8000`.

Smoke-test the three required endpoints:

```bash
# Ingest telemetry
curl -s -X POST http://localhost:8000/api/telemetry \
  -H 'Content-Type: application/json' \
  -d '{"timestamp":"2026-03-12T08:00:00.000Z","objects":[
        {"id":"SAT-01","type":"SATELLITE",
         "r":{"x":6778,"y":0,"z":0},"v":{"x":0,"y":7.78,"z":0}}
      ]}' | python3 -m json.tool

# Advance simulation 60 seconds
curl -s -X POST http://localhost:8000/api/simulate/step \
  -H 'Content-Type: application/json' \
  -d '{"step_seconds":60}' | python3 -m json.tool

# Pull visualization snapshot
curl -s http://localhost:8000/api/visualization/snapshot | python3 -m json.tool
```

Full automated verification:

```bash
./scripts/docker_api_smoke.sh   # end-to-end Docker lifecycle test
./scripts/run_ready_demo.sh     # full demo readiness check
```

---

## Design Philosophy

Three principles guided every architectural decision:

1. **Mathematical safety guarantees.** A single missed conjunction can trigger a catastrophic debris cascade. Every uncertain or computationally degraded object pair is *escalated* to a more expensive solver, never silently dropped — producing a provable zero-false-negative property across the entire filter cascade.

2. **Deterministic, auditable execution.** Identical inputs always produce the same maneuver sequence, the same CDM records, and the same fuel expenditure. This determinism is enforced by 8 standalone CTest gate binaries that run on every commit and inside the Docker build itself.

3. **Operational realism.** The engine respects the physical constraints of real spacecraft operations: impulsive-burn propulsion with Tsiolkovsky fuel accounting, 600 s thruster cooldown, 15 m/s per-burn thrust limits, line-of-sight validation against a geographically distributed ground network, and a 10 s signal-latency budget that forces proactive burn scheduling.

---

## System Architecture

CASCADE is a single-process C++20 engine with a minimal dependency footprint: `cpp-httplib` v0.15.3 for the REST server, `simdjson` v3.9.4 for streaming JSON ingestion, and Boost headers for version verification — all managed through CMake FetchContent with no system-level requirements beyond GCC 12 and `libgomp`.

### Tick Pipeline

```
POST /api/telemetry (ECI J2000 state vectors)
        │
        ▼
  ┌─────────────┐
  │ State Store  │  SoA layout, lock-free upsert, type-conflict detection
  └──────┬──────┘
         ▼
  ┌─────────────┐
  │ Propagation  │  Adaptive RK4+J2 / fast-lane Kepler (OpenMP)
  └──────┬──────┘  max error: 0.061 km at 86,400 s
         ▼
  ┌─────────────┐
  │ Broad Phase  │  O(N log N) SMA+inclination bins, shell overlap, D-criterion
  └──────┬──────┘  fail-open for high-e / invalid elements
         ▼
  ┌─────────────┐
  │ Narrow Phase │  MOID → TCA → RK4 micro-refine → full-window refine
  └──────┬──────┘  OpenMP-parallel, every ambiguous pair escalated
         ▼
  ┌─────────────┐
  │ CDM Engine   │  24-hour predictive scan, severity classification
  └──────┬──────┘
         ▼
  ┌─────────────┐
  │ COLA Planner │  RTN evasion ΔV, Tsiolkovsky fuel, cooldown, LOS check
  └──────┬──────┘
         ▼
  ┌─────────────┐
  │  Recovery    │  CW/ZEM slot-targeting burn, element-delta correction
  └──────┬──────┘  slot-error convergence gate (Δslot ≤ 0.08)
         ▼
  REST API + Frontend (port 8000)
```

### Concurrency Model

Mutating operations (telemetry ingestion, simulation steps, maneuver scheduling) are serialized through a command-queue backed by `std::future`/`std::promise` pairs. Read-only endpoints (`GET /api/visualization/snapshot`, `GET /api/status`) operate concurrently via atomically published view pointers. Within a tick, propagation and narrow-phase refinement are parallelized with OpenMP.

---

## Engineering Rationale

### Prototyping with Julia, Deploying in Pure C++

Julia's scientific computing ecosystem made it the natural choice for prototyping the NSGA-II parameter optimizer (69 parameters, 3 objectives) via the `jluna` C++/Julia bridge. Once the tuner proved the parameter space was safely explorable, we deployed in **pure C++20** — the Julia runtime adds ~200 MB to the Docker image plus 4–8 s of JIT warm-up, unacceptable for a latency-sensitive submission. The tuner remains a first-class offline tool; its outputs became the production defaults, and all 69 parameters are overridable at runtime via `PROJECTBONK_*` environment variables.

### Staged Filter Activation via Shadow Deployment

Every new screening filter is introduced in three stages: **shadow mode** (evaluate but never reject), **calibration** (accumulate statistics against RK4 ground truth), and **activation** (promote to hard-reject after demonstrating zero false negatives). This pattern is applied to the D-criterion gate, plane/phase angular gate, and MOID proxy — guaranteeing the zero-false-negative invariant at every point in the development timeline.

### Clohessy–Wiltshire Recovery with Heuristic Safeguard

Recovery burns are computed via CW state-transition matrix inversion blended with velocity-error damping (α = 0.70 position, β = 0.30 velocity, NSGA-II tuned). The proportional controller is retained as an automatic fallback for near-singular CW regimes. Recovery budgets are capped at 5% of remaining ΔV per request.

### Provably Safe Fail-Open Escalation

When propagation fails for an object, the engine bypasses all cheap filters and screens it against the *entire* debris catalog in the full narrow-phase pipeline. This is computationally expensive — O(M) narrow-phase evaluations — but propagation failures affect < 0.1% of objects per tick, and the safety guarantee is absolute.

### Multi-Mode MOID Screening

Minimum Orbit Intersection Distance is computed via three solver modes:

- **PROXY**: 72 × 72 true-anomaly grid (5,184 evaluations/pair), robust to all orbit types
- **HF**: Coarse global sampling + 3 local refinement iterations with pre-computed satellite positions
- **ANALYTICAL**: Full Gronchi 2005 ellipse-to-ellipse computation for well-conditioned pairs

The 2.0 km reject threshold ensures only genuinely non-intersecting pairs are discarded.

---

## Numerical Methods

### Orbital Propagation — Adaptive Three-Mode

| Mode | Criteria | Method |
|---|---|---|
| **Fast-lane** | e ≤ 0.003, perigee ≥ 650 km, Δt ≤ 45 s | J2 secular rates on mean elements |
| **Probe-then-escalate** | Outside fast-lane | J2 secular + coarse RK4 probe; escalate if disagreement > 0.5 km |
| **Full RK4+J2** | Low perigee (< 350 km), high-e (≥ 0.98), long Δt | Classical RK4, max substep 60 s, NaN guards |

Position error at 86,400 s: mean **0.000020 km**, max **0.061 km** — three orders of magnitude below the 100 m collision threshold.

### Broad Phase — O(N log N)

1. **SMA–Inclination binning**: Objects binned by semi-major axis (500 km) and inclination (20°). Flat sorted arrays with binary search; neighbor ±2 bins.
2. **Shell overlap test**: Perigee/apogee radial bounds with 50 km margin. Invalid elements get ±200 km envelope (intentional over-approximation).
3. **D-criterion gate**: Drummond proximity metric with reject threshold D > 2.0 (shadow-deployed).

Objects with invalid elements, e > 0.2, or excessive radial span **fail open** directly to the narrow phase.

### Narrow Phase — Multi-Gate Pipeline

1. **Plane/phase angular gate** — orbital plane and mean-anomaly phase separation screening; high-e objects bypass (fail-open)
2. **MOID proxy** — multi-mode solver, 2.0 km reject threshold
3. **Linear TCA approximation** — four simultaneous segment estimates over the step interval
4. **RK4 micro-refinement** — pairs within 0.10 km of threshold get 5 s substep RK4
5. **Full-window RK4 scan** — pairs within 0.20 km get budgeted OpenMP-parallel refinement (16 samples, 1 s substep)

Hypervelocity encounters (> 8 km/s) receive +0.10 km margin, giving an effective screening distance of **0.120 km**.

### 24-Hour Predictive CDM Generation

After each tick, a forward-looking scan propagates broad-phase pairs via RK4+J2 in 600 s substeps over a configurable horizon (default 86,400 s). Up to 1,024 records retained per scan, sorted by severity.

| Severity | Condition | Action |
|---|---|---|
| CRITICAL | d < 100 m | Auto-COLA triggered |
| WARNING | d < 1.0 km | Monitor |
| WATCH | d < 5.0 km | Log |

### Autonomous Maneuver Planning

**COLA planner**: Discrete 160-candidate search (4 magnitudes × 4 timings × 10 RTN directions). Each candidate is fully RK4-propagated and screened. Candidates ranked by: safety class → |ΔV| → normal component → slot error → miss distance. Max 3 CRITICAL CDMs evaluated per satellite per tick.

**Fuel accounting**: Tsiolkovsky relation with I_sp = 300 s, dry mass 500 kg, initial propellant 50 kg. A 5% fuel guard (2.5 kg) triggers automatic graveyard deorbit.

**Constraints**: 600 s cooldown, 15 m/s thrust cap, LOS validation against 6-station ground network with 10 s signal latency, blackout-aware pre-upload scheduling.

### Ground Station Network

| Station | Location | Min Elevation |
|---|---|---|
| ISTRAC Bengaluru | 13.0°N, 77.5°E | 5° |
| Svalbard Satellite | 78.2°N, 15.4°E | 5° |
| Goldstone Tracking | 35.4°N, 116.9°W | 10° |
| Punta Arenas | 53.2°S, 70.9°W | 5° |
| IIT Delhi Ground Node | 28.5°N, 77.2°E | 15° |
| McMurdo Station | 77.8°S, 166.7°E | 5° |

Loaded from CSV at runtime (`PROJECTBONK_GROUND_STATIONS_CSV`) with compiled-in fallback.

---

## Mission Console Frontend

React 18 / TypeScript SPA built with Vite, rendered via Canvas 2D and Three.js WebGL. Polls the engine at 1–2 Hz. Seven purpose-built operational views:

| View | Route | Purpose |
|---|---|---|
| **Command Center** | `#/command` | Fleet posture summary — satellite status counts, active CDMs, recent burns, engine health |
| **Ground Track** | `#/track` | Mercator canvas with 90-min historical trails + forecast arcs, solar terminator, station footprints |
| **Threat Assessment** | `#/threat` | Polar bullseye plot — radial = time-to-TCA, angular = approach vector, color = CDM severity |
| **Burn Operations** | `#/burn-ops` | Gantt maneuver timeline with blackout overlays, cooldown markers, per-satellite fuel gauges |
| **Evasion** | `#/evasion` | Evasion-specific operational view |
| **Fleet Status** | `#/fleet-status` | Fuel heatmaps, ΔV vs collisions-avoided, station-keeping drift, satellite availability |
| **Scorecard** | `#/scorecard` | PS §7 evaluation criteria mapped to live engine metrics with weighted readiness percentages |

The snapshot endpoint delivers debris as flattened `[id, lat, lon, alt_km]` tuples to minimize payload size. Canvas 2D renders ground-track and Gantt views at 60 FPS with 10,000+ debris markers. The frontend is statically built, bundled into the Docker image, and served by `cpp-httplib` — no separate web server required.

---

## API Reference

All endpoints on port **8000**, bound to `0.0.0.0`.

| Method | Endpoint | Description |
|---|---|---|
| `POST` | `/api/telemetry` | Batch ECI state vector ingestion |
| `POST` | `/api/maneuver/schedule` | Evasion / recovery burn submission |
| `POST` | `/api/simulate/step` | Advance simulation by Δt seconds |
| `GET` | `/api/visualization/snapshot` | Geodetic snapshot for frontend |
| `GET` | `/api/status` | Engine health, diagnostics, filter stats |
| `GET` | `/api/status?details=1` | Full internal diagnostics |
| `GET` | `/api/debug/conflicts` | Type-conflict ring buffer |
| `GET` | `/api/debug/propagation` | Fast-lane / RK4 usage counters |
| `GET` | `/api/debug/conjunctions` | Historical / predictive conjunction feed |

### Telemetry Ingestion

```http
POST /api/telemetry
Content-Type: application/json

{
  "timestamp": "2026-03-12T08:00:00.000Z",
  "objects": [
    {
      "id": "DEB-99421",
      "type": "DEBRIS",
      "r": {"x": 4500.2, "y": -2100.5, "z": 4800.1},
      "v": {"x": -1.25,  "y": 6.84,    "z": 3.12}
    }
  ]
}
```

```json
{"status": "ACK", "processed_count": 1, "active_cdm_warnings": 3}
```

### Maneuver Scheduling

Validates: ΔV ≤ 15 m/s, 600 s cooldown, sufficient fuel (Tsiolkovsky), ground-station LOS.

```http
POST /api/maneuver/schedule
Content-Type: application/json

{
  "satelliteId": "SAT-Alpha-04",
  "maneuver_sequence": [
    {"burn_id": "EVASION_1",  "burnTime": "2026-03-12T14:15:30.000Z",
     "deltaV_vector": {"x": 0.002, "y": 0.015, "z": -0.001}},
    {"burn_id": "RECOVERY_1", "burnTime": "2026-03-12T15:45:30.000Z",
     "deltaV_vector": {"x": -0.0019, "y": -0.014, "z": 0.001}}
  ]
}
```

```json
{
  "status": "SCHEDULED",
  "validation": {
    "ground_station_los": true,
    "sufficient_fuel": true,
    "projected_mass_remaining_kg": 548.12
  }
}
```

### Simulation Step

```http
POST /api/simulate/step
Content-Type: application/json

{"step_seconds": 3600}
```

```json
{
  "status": "STEP_COMPLETE",
  "new_timestamp": "2026-03-12T09:00:00.000Z",
  "collisions_detected": 0,
  "maneuvers_executed": 2
}
```

---

## Validation & Evidence

### Safety Gate Suite

8 standalone CTest gates, each a compiled C++ binary. Every gate runs in CI on every push and inside the Docker build — if any gate fails, the image cannot be built.

| Gate | Enforced Invariant |
|---|---|
| `api_contract_gate` | All 5 endpoints conform to PS schema |
| `broad_phase_validation` | Shell filter ⊇ brute-force baseline |
| `narrow_phase_false_negative_gate` | Zero missed collisions vs RK4 reference |
| `narrow_phase_calibration_probe` | Screening thresholds within tolerance |
| `phase2_regression` | Propagator drift < 0.061 km at 86,400 s |
| `maneuver_ops_invariants` | Cooldown, fuel, thrust-cap enforcement |
| `recovery_planner_invariants` | CW/ZEM convergence, slot error ≤ 0.08 |
| `recovery_slot_gate` | Deterministic slot recovery across 3 runs |

```bash
ctest --test-dir build --output-on-failure   # 8/8 pass
```

### Real-Data Validation

The scenario generator (`tools/real_data_scenario_gen`) ingested a live Space-Track OMM catalog (34 MB, 30,000+ objects):

- **27,191** LEO objects loaded in < 1 s with zero conversion failures
- 10 conjunctions detected at full 27K scale (2.1 s/tick)
- All broad-phase filters correctly preserved confirmed narrow-phase pairs
- Zero false negatives across the full catalog run

### Hot-Path Optimizations

Three profiling-driven optimizations:

1. **`std::pow(rmag, 5.0)` → explicit multiply** (`r2*r2*rmag`) — 10–50× faster per-object J2 acceleration
2. **J2 secular-rate subexpression factoring** — precomputed `cos i`, `sin i`, `a^3.5` struct eliminates redundant transcendentals
3. **MOID grid 24 → 72 samples** — 15° spacing left ~1,780 km arc gaps at LEO, rendering the filter inert

---

## Benchmarks

All benchmarks on a 24-core machine, Release build, OpenMP enabled.

### Tick Latency by Phase (50 sats / 10K debris / 30 s step)

| Phase | Time (ms) | % of tick |
|---|---|---|
| Propagation | 3.6 | 13.5% |
| Broad phase | 4.4 | 16.7% |
| Narrow precomp | 0.7 | 2.7% |
| Narrow sweep | 14.4 | 54.5% |

### Scaling

| Scenario | Mean Tick | Notes |
|---|---|---|
| 50 sats / 10K debris | 13.3 ms | Baseline |
| 100 sats / 20K debris | 72.6 ms | 2× stress |
| 27,191 LEO objects (real) | 2.1 s | Full Space-Track |

### Benchmark Tooling

```bash
# Deterministic synthetic benchmark
./build-benchmark-release/phase3_tick_benchmark 50 10000 10 40 30 20260317 1773292800

# Baseline vs current comparison (synthetic)
python scripts/benchmark_compare.py --profile long --report-md docs/benchmarks

# Real-catalog comparison
python scripts/benchmark_compare.py --profile real-smoke --report-md docs/benchmarks

# Repeated-run variance measurement
python scripts/benchmark_envelope.py --profile real-long --runs 5 --report-md docs/benchmarks
```

Benchmark compare checks deterministic output equivalence via FNV-1a fingerprinting. CI runs a smoke comparison on every push; a separate workflow supports scheduled/manual longer runs. Full workflow: `docs/BENCHMARK_WORKFLOW.md`.

---

## NSGA-II Multi-Objective Tuner

A Julia 1.10 / `jluna` bridge drives a full NSGA-II optimizer (Deb 2002) over **69 parameters** across three engine tiers:

| Tier | Parameters | Scope |
|---|---|---|
| Broad phase | 9 | SMA bin width, inclination bins, shell margin, D-criterion threshold, fail-open cutoffs |
| Narrow phase | 21 | Plane/phase thresholds, MOID resolution, TCA guard, refine budgets, uncertainty promotion |
| Maneuver planner | 39 | COLA ΔV scales, RTN weights, CW horizon bounds, recovery blend, CDM scanner config |

Three objectives (all minimized): `safety_risk`, `fuel_cost`, `compute_cost`. Feasibility-first constraint handling — any candidate with `safety_risk > 0` is dominated by all safe candidates. SBX crossover + polynomial mutation.

Best run: 20 pop × 5 gen → tick **13.3 ms** (vs 23 ms default), 42% improvement, zero safety violations.

```bash
cmake -S . -B build_julia -DPROJECTBONK_ENABLE_JULIA_RUNTIME=ON
cmake --build build_julia --target nsga2_jluna_bridge -j$(nproc)
./build_julia/nsga2_jluna_bridge 80 50 50 10000 10 30
```

---

## Build from Source

```bash
# Ubuntu 22.04
sudo apt-get install -y build-essential cmake git curl wget libboost-all-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROJECTBONK_ENABLE_JULIA_RUNTIME=OFF
cmake --build build -j$(nproc)
./build/ProjectBONK
```

### Run Safety Gates

```bash
ctest --test-dir build --output-on-failure
```

### Individual Gates

```bash
./scripts/api_contract_gate.sh ./build
./scripts/broad_phase_sanity_gate.sh
./scripts/narrow_phase_false_negative_gate.sh
./scripts/recovery_planner_invariants_gate.sh
```

---

## Runtime Configuration

All 69 engine constants are overridable via environment variables — no recompile required.

| Variable | Default | Description |
|---|---|---|
| `PROJECTBONK_RECOVERY_SOLVER_MODE` | `CW_ZEM_EQUIVALENT` | Recovery solver mode |
| `PROJECTBONK_BROAD_SHELL_MARGIN_KM` | `50.0` | Broad-phase altitude shell margin |
| `PROJECTBONK_NARROW_TCA_GUARD_KM` | `0.02` | TCA refinement guard radius |
| `PROJECTBONK_CDM_HORIZON_S` | `86400` | Predictive CDM horizon |
| `PROJECTBONK_AUTO_DV_KM_S` | `0.001` | Auto-COLA impulse magnitude |
| `PROJECTBONK_CORS_ENABLE` | `false` | Enable CORS for dev frontend |
| `PROJECTBONK_MAX_STEP_SECONDS` | `86400` | Maximum allowed step_seconds |
| `PROJECTBONK_GROUND_STATIONS_CSV` | `docs/groundstations.csv` | Ground station configuration |

Full variable reference: `docs/implementation-plan.md`.

---

## Deployment

### Docker

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
```

Or with Compose:

```bash
docker compose up --build -d
```

### Docker Build Architecture

| Stage | Base | Purpose |
|---|---|---|
| **builder** | `ubuntu:22.04` | GCC-12, CMake, sccache, CTest gates |
| **frontend** | `node:24-slim` | `npm ci` + Vite production build |
| **runtime** | `ubuntu:22.04` | Minimal: `libgomp1` + binary + static assets |

Build-time optimizations: sccache compiler cache, dependency pre-fetch layer (CMake FetchContent cached), BuildKit cache mounts, CTest gates run inside the builder (image cannot be built if any gate fails). Runtime image runs as non-root user (`bonk`).

### CI Pipeline

GitHub Actions enforces a four-stage gate on every push to `main`:

1. CMake configure + build (GCC-12, Release, `-O2`)
2. CTest: all 8 safety gates with `--output-on-failure`
3. Frontend: `npm ci && npm run build` (type-checked Vite production bundle)
4. Docker API smoke gate (end-to-end container lifecycle test)

---

## Frontend Development

```bash
PROJECTBONK_CORS_ENABLE=true \
PROJECTBONK_CORS_ALLOW_ORIGIN=http://localhost:5173 \
./build/ProjectBONK
```

Frontend dev server on port 5173:

```bash
cd frontend && npm ci && npm run dev
```

---

## Local Catalog Replay

Replay the `data.txt` catalog through the telemetry API for local UI/backend testing:

```bash
python3 scripts/replay_data_catalog.py --api-base http://localhost:8000
```

Strict real-data mode (promote real payloads as operator fleet):

```bash
python3 scripts/replay_data_catalog.py \
  --api-base http://localhost:8000 \
  --satellite-mode catalog \
  --operator-sats 10
```

Supports OMM JSON and 3LE/TLE text catalogs, manifest-driven replay presets, scenario mining with LOS-aware scoring, and backend-assisted ranking. See `scripts/mine_strict_scenarios.py --help` for the full mining workflow.

---

## Physical Constants

| Symbol | Description | Value |
|---|---|---|
| μ | Earth grav. parameter | 398,600.4418 km³/s² |
| R_E | Earth equatorial radius | 6,378.137 km |
| J₂ | Oblateness coefficient | 1.08263 × 10⁻³ |
| d_coll | Collision threshold | 0.100 km |
| I_sp | Specific impulse | 300 s |
| g₀ | Standard gravity | 9.80665 m/s² |
| m_dry | Satellite dry mass | 500 kg |
| m_fuel | Initial propellant | 50 kg |
| |ΔV|_max | Thrust limit | 15 m/s |
| τ_cd | Cooldown period | 600 s |
| τ_sig | Signal latency | 10 s |
| r_box | Station-keeping box | 10 km |

---

## Project Layout

```
.
├── CMakeLists.txt                  # C++20, FetchContent deps, Julia auto-install
├── Dockerfile                      # 3-stage build (ubuntu:22.04)
├── main.cpp                        # HTTP server + engine runtime entry point
├── src/
│   ├── simulation_engine.cpp       # Master tick loop: propagate → broad → narrow → COLA
│   ├── broad_phase.cpp             # O(N log N) SMA binning + D-criterion
│   ├── orbit_math.cpp              # Gronchi 2005 MOID, ECI↔elements, RK4+J2
│   ├── propagator.cpp              # Adaptive fast-lane / full RK4 propagator
│   ├── maneuver_recovery_planner.cpp  # CW/ZEM slot-targeting recovery
│   ├── state_store.cpp             # Lock-free SoA state vector store
│   ├── earth_frame.cpp             # ECI→ECEF→geodetic, GMST, LOS
│   └── http/                       # REST API handlers
├── frontend/
│   └── src/
│       └── pages/                  # 7 dashboard views (React/Canvas/WebGL)
├── tuner/
│   ├── nsga2_tuner.jl              # Pure Julia NSGA-II (69 params, 3 objectives)
│   └── nsga2_jluna_bridge.cpp      # C++/Julia bridge
├── tools/                          # Gate binaries, benchmark harnesses
├── scripts/                        # Gate runners, benchmark compare, replay, mining
├── docs/                           # Implementation plan, phase reports, groundstations.csv
└── PS.md                           # National Space Hackathon 2026 problem statement
```

---

## Development Timeline

| Phase | Milestone |
|---|---|
| **Week 1** | Core infrastructure: telemetry ingestion via simdjson, SoA state store, J2 propagator with adaptive mode selection, Kepler solver, REST API skeleton |
| **Week 2** | Screening pipeline: conservative broad phase, multi-gate narrow phase, CW/ZEM recovery solver, Tsiolkovsky fuel accounting, cooldown & thrust-cap enforcement |
| **Week 3** | Optimization & validation: shadow-gate observability, NSGA-II tuner (69 params), real-data scenario generator (27K objects), OpenMP, env-override system, 8 CTest gates |
| **Week 4** | Integration & polish: React/Canvas/WebGL dashboard (7 pages), 3-stage Docker build with sccache, CI pipeline, PS alignment, technical report |

---

## License

See [LICENSE](LICENSE).
