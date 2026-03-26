# CASCADE — Autonomous Orbital Collision Avoidance System

> **Built for National Space Hackathon 2026 · IIT Delhi**

---

**The Kessler Cascade is not theoretical anymore — it is a timing problem.**
One untracked fragment at 7.8 km/s hits one Starlink-class bus. That bus sprays 2,000 new trackable pieces. Each piece hits two more. Within a decade, Low Earth Orbit is a closed graveyard. The window to automate collision avoidance is now.

CASCADE is a production-feeling, deterministic **Autonomous Constellation Manager** that ingests live ECI telemetry, eliminates O(N²) conjunction screening through multi-tier spatial filtering, schedules RTN-frame burns under fuel and cooldown constraints, and autonomously recovers satellites to their orbital slots — all without a human in the loop.

---

## Performance at a Glance

| Metric | Value |
|---|---|
| Tick latency — 50 sats / 10,000 debris | **13.3 ms** (mean) |
| Tick latency — 100 sats / 20,000 debris | **72.6 ms** (mean) |
| False negatives across all 6 test families | **0** |
| Adaptive propagator position error (86,400 s) | **< 0.061 km** max |
| Real catalog objects ingested (Space-Track LEO) | **27,191** |
| Safety gate tests passing | **8 / 8** |
| OpenMP narrowphase speedup (100 sat / 20K deb) | **−31.5%** |
| NSGA-II tuner parameters | **69** (3 tiers, 3 objectives) |

---

## Evaluation Criteria — How We Score

| Criterion | Weight | What CASCADE does |
|---|---|---|
| **Safety Score** | 25% | Analytical MOID (Gronchi 2005 quartic solver) + TCA micro-refinement + fail-open design: any ambiguous pair is escalated, never silently dropped |
| **Fuel Efficiency** | 20% | CW/ZEM-equivalent single-burn recovery solver; RTN-frame evasion burns sized by Tsiolkovsky relation; NSGA-II tuner optimizes the fuel/safety/speed Pareto front |
| **Constellation Uptime** | 15% | Every evasion auto-triggers a recovery burn targeting the nominal orbital slot; slot-error convergence gate (Δslot ≤ 0.08) passes deterministically |
| **Algorithmic Speed** | 15% | O(N log N) broad phase (SMA binning + D-criterion); OpenMP-parallelized narrow sweep; adaptive RK4 probe only where needed |
| **UI/UX** | 15% | REST snapshot API (`/api/visualization/snapshot`) delivers geodetic lat/lon/alt + debris tuple arrays for 60 FPS Canvas/WebGL render |
| **Code Quality** | 10% | 8/8 CTest gates; 69 constants env-overridable; structured logging with per-phase timing breakdowns |

---

## Quick Start (Docker — three commands)

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
curl -s http://localhost:8000/api/status | python3 -m json.tool
```

Expected startup output:

```
CASCADE (Project BONK) SYSTEM ONLINE
Boost version : 1_74
Starting HTTP server on 0.0.0.0:8000 ...
```

Smoke-test all three required endpoints:

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

---

## The Pipeline

```
Telemetry (ECI state vectors)
        |
        v
  [ State Store ]  — lock-free upsert, type-conflict detection
        |
        v
  [ Propagation ]  — adaptive RK4+J2; fast-lane Kepler for circular orbits
        |           max position error: 0.061 km at 86,400 s
        v
  [ Broad Phase ]  — O(N log N) semi-major axis + inclination binning
        |           D-criterion filter; fail-open for high-e orbits
        v
  [ Narrow Phase ] — per-pair analytical MOID (Gronchi 2005 quartic solver)
        |           TCA refinement → RK4 micro-window → budgeted full-window
        |           OpenMP-parallelized; every ambiguous pair escalated (fail-open)
        v
  [ CDM Engine ]   — 24-hour conjunction horizon; TCA + miss distance
        |
        v
  [ COLA Planner ] — RTN-frame evasion ΔV; Tsiolkovsky fuel deduction;
        |           600 s cooldown enforcement; LOS/blackout validation
        v
  [ Recovery Planner ] — CW/ZEM slot-targeting burn; element-delta correction
        |                (a, e, i, Ω); slot-error convergence gate
        v
  [ Maneuver Queue ]   — priority-ordered execution on /api/simulate/step
        |
        v
  REST API (port 8000)  — telemetry · maneuver · step · snapshot · status
```

---

## API Reference

All endpoints on port **8000**, bound to `0.0.0.0`.

| Method | Endpoint | Status | Description |
|---|---|---|---|
| `POST` | `/api/telemetry` | 200 | Ingest ECI state vectors (bulk) |
| `POST` | `/api/maneuver/schedule` | 202 | Schedule evasion / recovery burns |
| `POST` | `/api/simulate/step` | 200 | Advance simulation by N seconds |
| `GET` | `/api/visualization/snapshot` | 200 | Geodetic snapshot for frontend |
| `GET` | `/api/status` | 200 | Engine health, tick count, CDM warnings |
| `GET` | `/api/status?details=1` | 200 | Full internal diagnostics (phase timing, gate evidence, queue depth) |
| `GET` | `/api/debug/conflicts` | 200 | Type-conflict ring buffer |
| `GET` | `/api/debug/propagation` | 200 | Fast-lane / RK4 usage counters |
| `GET` | `/api/debug/conjunctions` | 200 | Historical / predictive conjunction debug feed |

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
{"status":"ACK","processed_count":1,"active_cdm_warnings":3}
```

### Maneuver Scheduling

Validates: ΔV ≤ 15 m/s, 600 s cooldown, sufficient fuel (Tsiolkovsky), ground-station LOS.

```http
POST /api/maneuver/schedule
Content-Type: application/json

{
  "satelliteId": "SAT-Alpha-04",
  "maneuver_sequence": [
    {"burn_id":"EVASION_1",  "burnTime":"2026-03-12T14:15:30.000Z",
     "deltaV_vector":{"x":0.002,"y":0.015,"z":-0.001}},
    {"burn_id":"RECOVERY_1", "burnTime":"2026-03-12T15:45:30.000Z",
     "deltaV_vector":{"x":-0.0019,"y":-0.014,"z":0.001}}
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

### Conjunction Debug Streams

`/api/debug/conjunctions` supports an additive `source` query for local/debug use:

```bash
curl -s 'http://localhost:8000/api/debug/conjunctions?source=history'
curl -s 'http://localhost:8000/api/debug/conjunctions?source=predicted'
curl -s 'http://localhost:8000/api/debug/conjunctions?source=combined'
```

- `history` = narrow-phase watch history captured during steps
- `predicted` = persisted 24-hour predictive CDM scan results
- `combined` = both streams together

---

## Technical Depth

### Propagation

- **Adaptive RK4 + J2 perturbation** for all objects each tick.
- **Fast-lane Kepler** short-circuits circular, low-eccentricity orbits to skip
  expensive numerical integration where J2 drift is negligible.
- **Probe-and-escalate**: a cheap adaptive step probes first; if position/velocity
  error exceeds threshold, the pair is promoted to full RK4.
- Position error at 86,400 s: mean **0.000020 km**, max **0.061 km** (p95 = 0 km).

### Broad Phase — O(N log N)

- Objects binned by semi-major axis (width = 500 km) and inclination band.
- Neighbor-bin search limits candidate pairs to geometrically plausible
  encounters only; band width tunable at runtime.
- **D-criterion filter** (Drummond 1981) rejects co-planar non-crossing pairs
  before expensive narrow-phase evaluation.
- High-eccentricity objects (e > 0.2) bypass the filter and fail-open —
  safety over performance.

### Narrow Phase — Analytical MOID

The narrow phase implements three progressively more expensive filters,
each failing open on any numerical uncertainty:

1. **Plane/phase angular screening** — cheap geometry precheck.
2. **MOID evaluation** — full analytical Minimum Orbit Intersection Distance
   via the **Gronchi (2005) degree-8 resultant polynomial** (quartic in cos E),
   not just sampled proxies. This is the same formulation used in ESA's DRAMA
   tool suite.
3. **TCA refinement** — near-threshold pairs get an RK4 micro-window pass
   around the predicted TCA.
4. **Budgeted full-window scan** — ultra-near pairs get a budgeted sampled
   full-window RK4 pass before final classification.

Fail-open policy: **any step that cannot complete a numeric check escalates
the pair to detected** rather than silently dropping it. False negatives are
treated as a system failure.

### COLA & Recovery

- **Evasion**: prograde/retrograde RTN burn sized to push the satellite ≥ 100 m
  standoff; respects 15 m/s per-burn hard cap and 600 s thermal cooldown.
- **Tsiolkovsky tracking**: every burn deducts `Δm = m_current · (1 − e^(−|Δv|/(Isp·g0)))`
  from the live propellant budget; dry mass 500 kg, Isp 300 s.
- **Recovery burn**: CW/ZEM-equivalent single-burn targeting nominal orbital
  slot (Δa, Δe, Δi, ΔΩ element delta correction); slot-error convergence gate
  passes at Δslot ≤ 0.08, deterministically reproducible across 3 independent runs.
- **Graveyard**: if fuel drops to critical threshold, a graveyard deorbit is
  autonomously scheduled before the satellite is maneuvering-incapable.
- **LOS enforcement**: burns are only uplinked when the satellite has line-of-sight
  to at least one of the six PS-defined ground stations (elevation mask respected).
  Conjunctions predicted over blackout zones are handled by pre-uploading the
  sequence before the last LOS window closes.

### NSGA-II Multi-Objective Tuner

A Julia 1.10 / jluna bridge runs a full NSGA-II optimizer (Deb 2002) over
**69 tunable parameters** across all three engine tiers:

- **3 objectives** (all minimized): `safety_risk`, `fuel_cost`, `compute_cost`
- **Feasibility-first** constraint handling — any candidate with `safety_risk > 0`
  is dominated by all safe candidates regardless of other objectives
- **SBX crossover** + **polynomial mutation** with per-parameter integer rounding
- Verified end-to-end: 20 pop × 5 generations → best tick **13.3 ms**
  (vs 23 ms default) with zero safety violations

```bash
# Build with Julia bridge
cmake -S . -B build_julia -DPROJECTBONK_ENABLE_JULIA_RUNTIME=ON
cmake --build build_julia --target nsga2_jluna_bridge -j$(nproc)

# Run tuner (pop=80, gens=50, 50 sats, 10K debris)
./build_julia/nsga2_jluna_bridge 80 50 50 10000 10 30
# Writes: tuner/tuned_params.env
```

---

## Benchmarks

All benchmarks on a 24-core dev machine, Release build, OpenMP enabled.

### Tick Latency by Phase (50 sats / 10K debris / 30 s step)

| Phase | Time (ms) | % of tick |
|---|---|---|
| Propagation | 3.6 | 13.5% |
| Broad phase | 4.4 | 16.7% |
| Narrow precomp | 0.7 | 2.7% |
| Narrow sweep | 14.4 | 54.5% |

### OpenMP Narrowphase Speedup

| Scenario | Before | After | Delta |
|---|---|---|---|
| 50 sat / 10K deb — tick mean | 25.5 ms | 23.0 ms | **−9.8%** |
| 50 sat / 10K deb — narrow sweep | 17.4 ms | 14.4 ms | **−17.2%** |
| 100 sat / 20K deb — tick mean | 91.8 ms | 72.6 ms | **−20.9%** |
| 100 sat / 20K deb — narrow sweep | ~68 ms | 46.6 ms | **−31.5%** |

### Real Catalog Run (Space-Track OMM, LEO only)

| Scenario | tick mean | broad phase | narrow sweep | collisions |
|---|---|---|---|---|
| 50 sats + 10K debris (real data) | 17.0 ms | — | — | — |
| Full 27,191 LEO objects | **2,146 ms** | 698 ms | 1,425 ms | 10 detected |

---

## Build from Source (Ubuntu 22.04)

```bash
sudo apt-get install -y build-essential cmake git curl wget libboost-all-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROJECTBONK_ENABLE_JULIA_RUNTIME=OFF
cmake --build build -j$(nproc)
./build/ProjectBONK
```

### Run the Safety Suite

```bash
ctest --test-dir build --output-on-failure
# 8/8 tests pass:
#   phase2_regression_gate_test
#   phase2_regression_gate_short_step_test
#   broad_phase_sanity_gate_test
#   narrow_phase_false_negative_gate_test     <- 0 FN across all 6 families
#   recovery_slot_gate_test
#   recovery_planner_invariants_gate_test
#   maneuver_ops_invariants_gate_test
#   api_contract_gate_test
```

### Run the Tick Benchmark

```bash
cmake --build build --target phase3_tick_benchmark -j$(nproc)
./build/phase3_tick_benchmark 50 10000 5 10 30
```

---

## Runtime Configuration

All 69 engine constants are overridable via environment variables at startup —
no recompile required. Key variables:

| Variable | Default | Description |
|---|---|---|
| `PROJECTBONK_RECOVERY_SOLVER_MODE` | `CW_ZEM_EQUIVALENT` | Recovery solver (`HEURISTIC` or `CW_ZEM_EQUIVALENT`) |
| `PROJECTBONK_BROAD_SHELL_MARGIN_KM` | `50.0` | Broad-phase altitude shell margin |
| `PROJECTBONK_NARROW_TCA_GUARD_KM` | `0.02` | TCA refinement guard radius |
| `PROJECTBONK_CDM_HORIZON_S` | `86400` | 24-hour CDM prediction horizon |
| `PROJECTBONK_AUTO_DV_KM_S` | `0.001` | Auto-COLA impulse magnitude |
| `PROJECTBONK_CORS_ENABLE` | `false` | Enable CORS (dev frontend split) |
| `PROJECTBONK_MAX_STEP_SECONDS` | `86400` | Max allowed step_seconds |

Full variable reference: `docs/implementation-plan.md`.

---

## Frontend Development

Backend on port `8000`. For a local frontend dev server on `5173`:

```bash
PROJECTBONK_CORS_ENABLE=true \
PROJECTBONK_CORS_ALLOW_ORIGIN=http://localhost:5173 \
./build/ProjectBONK
```

The `/api/visualization/snapshot` response is shaped for high-density rendering:
satellites as full objects, debris as flattened `[id, lat, lon, alt_km]` tuples
to minimize payload size at 50+ sats / 10K+ debris.

---

## Deployment

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
```

Or with Compose:

```bash
docker compose up --build -d
```

If you already have a container running, use `--build` after frontend changes so
the baked `frontend/dist` bundle is refreshed inside the image.

Base image: `ubuntu:22.04`. Port `8000` bound to `0.0.0.0`.
Dockerfile is layered for fast incremental rebuilds (deps cached separately
from source).

### Local Catalog Replay (`data.txt`)

For local UI/backend smoke testing, you can replay the gitignored `data.txt`
catalog through the normal telemetry API without changing the judged runtime path:

```bash
python3 scripts/replay_data_catalog.py --api-base http://localhost:8000
```

For the stricter real-data route, promote real payloads directly into the operator fleet
instead of using synthetic `SAT-LOCAL-*` IDs:

```bash
python3 scripts/replay_data_catalog.py \
  --api-base http://localhost:8000 \
  --satellite-mode catalog \
  --operator-sats 10
```

To preview the selected operator fleet and replay timestamp without posting telemetry:

```bash
python3 scripts/replay_data_catalog.py --satellite-mode catalog --dry-run
```

The same replay tooling now also supports local 3LE/TLE text catalogs such as
`3le_data.txt`:

```bash
python3 scripts/replay_data_catalog.py \
  --data 3le_data.txt \
  --satellite-mode catalog \
  --operator-sats 10 \
  --dry-run
```

You can also drive replay settings from a scenario manifest template:

```bash
python3 scripts/replay_data_catalog.py \
  --manifest docs/scenarios/strict_natural_watch.example.json \
  --dry-run
```

To generate a new strict replay manifest from the local catalog:

```bash
python3 scripts/generate_strict_manifest.py \
  --output /tmp/strict_generated_manifest.json \
  --scenario-id strict_generated_test \
  --satellite-mode catalog \
  --operator-sats 6
```

To mine ranked strict replay manifests from real nearby traffic:

```bash
python3 scripts/mine_strict_scenarios.py \
  --output-dir /tmp/strict_mined \
  --scenario-prefix strict_mined \
  --top-scenarios 3 \
  --operator-sats 8 \
  --payload-candidates 12 \
  --threat-candidates 600
```

The miner now also uses `docs/groundstations.csv` for LOS-aware scoring by default,
so manifests include recommended ground stations and LOS-ready operator counts.
It also now tries to emit more diverse operator-fleet combinations instead of near-duplicate scenario sets.
It additionally tracks threat-richness signals such as near-100 km / near-250 km / near-500 km counts and balances shell/family mix when assembling operator fleets.
It now also targets likely predictive-warning shells more directly via shell-density and phase-density probing before live backend ranking begins.

You can point the same miner at `3le_data.txt` as well:

```bash
python3 scripts/mine_strict_scenarios.py \
  --data 3le_data.txt \
  --output-dir /tmp/strict_mined_3le \
  --scenario-prefix strict_mined_3le \
  --top-scenarios 3
```

To evaluate a strict manifest against a fresh local backend:

```bash
python3 scripts/evaluate_strict_manifest.py \
  --manifest /tmp/strict_generated_manifest.json \
  --api-base http://localhost:8000 \
  --extra-steps 1 \
  --extra-step-seconds 300
```

To rank multiple strict manifests by fresh backend runs:

```bash
python3 scripts/rank_strict_manifests.py \
  /tmp/strict_mined/strict_mined_01.json \
  /tmp/strict_mined/strict_mined_02.json \
  --backend-cmd ./build/ProjectBONK \
  --api-base http://localhost:8000 \
  --extra-steps 1 \
  --extra-step-seconds 300
```

The miner can now do this backend-assisted ranking inline while it writes manifests:

```bash
python3 scripts/mine_strict_scenarios.py \
  --output-dir /tmp/strict_mined_ranked \
  --scenario-prefix strict_mined_ranked \
  --top-scenarios 2 \
  --candidate-scenarios 3 \
  --backend-rank-cmd ./build/ProjectBONK \
  --backend-rank-api-base http://localhost:8000 \
  --feedback-output /tmp/strict_mined_ranked_feedback.json
```

What this does:
- promotes a small operator fleet from real payloads
- supports legacy synthetic `SAT-LOCAL-*` mode and stricter `SAT-<NORAD>` catalog mode
- supports manifest-driven replay presets for repeatable strict-route demos
- supports lightweight scenario mining that ranks real payload anchors by nearby natural traffic
- supports both OMM JSON and 3LE/TLE text catalogs in the same replay/mining flow
- supports one-shot manifest evaluation against a fresh live backend so mined presets can be ranked with real runtime outputs
- supports backend-assisted ranking across multiple manifests by restarting the backend per scenario
- incorporates ground-station LOS into mining metadata and manifest ranking
- can optionally perform backend-assisted ranking directly from the miner and emit a backend ranking summary alongside mined manifests
- can persist learned feedback weights so later mining runs bias more strongly toward backend-confirmed threat-richness signals
- writes `backend_cdm_evaluation` back into each ranked manifest so predictive-CDM mining results stay attached to the scenario artifact
- injects the rest of the filtered catalog as `DEBRIS`
- uses the existing `POST /api/telemetry` and `POST /api/simulate/step` endpoints

This replay path is intended only for local demos and UI smoke testing. It does
not add a judged API surface and does not change default runtime behavior.

---

## Project Layout

```
.
├── CMakeLists.txt              # Build system: C++20, FetchContent, Julia auto-install
├── Dockerfile                  # Single-container build & run (ubuntu:22.04)
├── main.cpp                    # Entry point: HTTP server + engine runtime
├── src/
│   ├── simulation_engine.cpp   # Master tick loop (propagate → broad → narrow → COLA)
│   ├── broad_phase.cpp         # O(N log N) SMA binning + D-criterion
│   ├── orbit_math.cpp          # Gronchi 2005 MOID, ECI↔elements, RK4+J2
│   ├── propagator.cpp          # Adaptive fast-lane / full RK4 propagator
│   ├── maneuver_recovery_planner.cpp  # CW/ZEM slot-targeting recovery
│   ├── state_store.cpp         # Lock-free state vector store
│   └── http/                   # REST API handlers
├── tuner/
│   ├── nsga2_tuner.jl          # Pure Julia NSGA-II (69 params, 3 objectives)
│   └── nsga2_jluna_bridge.cpp  # C++/Julia bridge
├── tools/                      # Regression, benchmark, and gate harnesses
├── scripts/                    # Gate runner scripts (phase2..phase4)
├── docs/                       # Implementation plan, HANDOFF, phase reports
└── PS.md                       # Authoritative problem statement (IIT Delhi)
```

---

## Deliverables Status

| Deliverable | Status |
|---|---|
| Backend + REST API (port 8000) | **Complete** |
| Docker build (`ubuntu:22.04`, port 8000) | **Complete** |
| NSGA-II parameter tuner | **Complete** |
| Safety gate suite (8/8) | **Complete** |
| Frontend — Orbital Insight dashboard | In progress |
| Technical report (LaTeX PDF) | In progress |
| Video demonstration | In progress |
