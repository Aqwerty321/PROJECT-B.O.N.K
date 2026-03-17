# CASCADE — Architecture & Dependency Guide

> Short pitch (one line):
> **CASCADE** is a high-performance Autonomous Orbital Collision Avoidance System for constellation operators — a production-feeling, deterministic pipeline that ingests telemetry, screens for conjunctions under arbitrary timestep jumps, schedules safe burns under fuel/thermal constraints, and visualizes cascade risk and TCA rings on a slick mission console.

This document explains **what** to build, **why** each dependency exists, and **how** components fit together so any developer, judge, or reviewer immediately understands the stack, constraints, and purpose of every library/service.

> **Note**: The authoritative API schemas and grader-facing contracts live in
> `PS.md` (the problem statement).  This document is an internal guide and
> may be updated freely as the project evolves.

---

# Table of contents

1. Goals & non-goals
2. High-level architecture
3. Data & API contracts (essential formats)
4. Components, responsibilities, and flow
5. Required platform, build, runtime, and CI environment
6. Dependencies (exact role + recommended versions / alternatives)
7. Important algorithms / numerical requirements & edge cases
8. Performance, capacity & test targets
9. Development roadmap / milestone checklist
10. Deliverables (what to include in repo & demo)
11. Operational notes, risks, and mitigation

---

# 1. Goals & non-goals

**Primary goals**

* Correctly detect conjunctions (≤ 0.100 km) across arbitrary `step_seconds` (seconds → days) with **zero false negatives** in tests.
* Minimize expensive checks (MOID / RK4) through multi-stage filtering: orbital-element filters, D-criterion, concentric shells, phase/torus screening.
* Provide deterministic maneuver scheduling that respects constraints: max Δv (15 m/s per burn), 600s cooldown, fuel limits, mandatory graveyard rules.
* Deliver a compelling frontend demo (slick CASCADE dashboard) with boot animation, rotor indicator, motion trails, TCA ripple rings, and cascade propagation simulation.

**Non-goals**

* Not attempting a research-grade long-term propagator (multi-year perturbation modeling). Keep to J2 + drag heuristics for days-level fidelity.
* No runtime machine learning in the production engine (ML allowed offline for parameter tuning only, in C++ if used).
* Not building a distributed microservice cluster — single-container deterministic engine for the hackathon (scales to many objects with efficient design).

---

# 2. High-level architecture

1. **Ingest API (C++ engine)** — REST endpoint(s) that accept telemetry and commands. Parse JSON → upsert object states. (Port 8000)
2. **State store** — in-process SoA memory (primary). Optional: lightweight embedded DB (ObjectBox / RocksDB / LMDB) for persistence and query acceleration.
3. **Adaptive Propagation Layer** (C++ / Julia hybrid):

   * Cheap: J2 secular updates + Kepler propagation for many objects.
   * Expensive: RK4 with J2 (and optional drag) for critical narrow-phase propagation.
   * Use `jluna` to call Julia kernels for heavy linear algebra / ODEs if chosen.
4. **Screening & Collision Pipeline** (C++ main loop):

   * Orbital element conversion → D-criterion → concentric shells (r_min/r_max) → plane intersection → phase/torus check → analytic TCA → MOID → RK4 final confirmation.
5. **Maneuver Brain** (C++ / Julia): CW/ZEM solvers, RTN transformation, Δv calculation, fuel accounting (Tsiolkovsky), & cooldown logic.
6. **Scheduler** — queue + ground-station LOS check (ground_stations.csv embedded in PS), offline upload window handling.
7. **Tuner (offline)** — C++ Monte-Carlo harness + libCMA-ES / simple evolution to tune parameters (no Python in production).
8. **Frontend** — React + Tailwind + deck.gl + three.js for map + custom CSS/SVG animations (MICR font header, rotor indicator, TCA rings, trails, cascade graph).
9. **CI / Docker** — single Dockerfile (ubuntu:22.04) that builds engine and frontend for deterministic grading.

Simplified data flow: POST telemetry → upsert ECI states → element conversion & J2-secular update for `t0+Δt` → cascade of filters → narrow-phase solves → maneuver scheduling via POST /api/maneuver/schedule.

---

# 3. Data & API contracts

> **Canonical source**: `PS.md` (the hackathon problem statement).
> The schemas below mirror PS.md for quick reference.  If there is any
> discrepancy, PS.md wins.

## POST /api/telemetry

Request JSON:

```json
{
  "timestamp": "2026-03-12T08:00:00.000Z",
  "objects": [
    {
      "id": "DEB-99421",
      "type": "DEBRIS",
      "r": {"x": 4500.2, "y": -2100.5, "z": 4800.1},
      "v": {"x": 1.25, "y": 6.84, "z": 3.12}
    }
  ]
}
```

Response:

```json
{
  "status": "ACK",
  "processed_count": 1,
  "active_cdm_warnings": 3
}
```

* All positions/velocities in ECI (km, km/s).

## POST /api/maneuver/schedule

Request JSON:

```json
{
  "satelliteId": "SAT-Alpha-04",
  "maneuver_sequence": [
    {
      "burn_id": "EVASION_BURN_1",
      "burnTime": "2026-03-12T14:15:30.000Z",
      "deltaV_vector": {"x": 0.002, "y": 0.015, "z": -0.001}
    },
    {
      "burn_id": "RECOVERY_BURN_1",
      "burnTime": "2026-03-12T15:45:30.000Z",
      "deltaV_vector": {"x": -0.0019, "y": -0.014, "z": 0.001}
    }
  ]
}
```

Response:

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

Engine must validate constraints (fuel, cooldown, LOS) before acceptance.

## POST /api/simulate/step

Request JSON:

```json
{
  "step_seconds": 3600
}
```

Response:

```json
{
  "status": "STEP_COMPLETE",
  "new_timestamp": "2026-03-12T09:00:00.000Z",
  "collisions_detected": 0,
  "maneuvers_executed": 2
}
```

## GET /api/visualization/snapshot

Response:

```json
{
  "timestamp": "2026-03-12T08:00:00.000Z",
  "satellites": [
    {
      "id": "SAT-Alpha-04",
      "lat": 28.545,
      "lon": 77.192,
      "fuel_kg": 48.5,
      "status": "NOMINAL"
    }
  ],
  "debris_cloud": [
    ["DEB-99421", 12.42, 45.21, 400.5],
    ["DEB-99422", 12.55, -45.10, 401.2]
  ]
}
```

## GET /api/status

Return engine health, latest tick, queue lengths.

---

# 4. Components, responsibilities & flow

Keep components small and deterministic. Responsibilities:

* **HTTP Front** (`cpp-httplib`): parse JSON, validate payload size, upsert into State Store.
* **State Store** (SoA arrays, indexed by ID): stores ECI at `t0`, precomputed orbital elements, r_min/r_max, last burn, fuel, flags.
* **Element conversion module**: ECI ↔ Keplerian (a,e,i,Ω,ω,M), compute p,rp,ra, h-vector.
* **Adaptive propagator**: given Δt and object metadata choose J2-secular / Kepler / RK4; update Ω,ω,M (J2 rates) and optionally a,e for drag or burns.
* **Broad-phase filters**:

  * Orbital band indexing (a-bin, i-bin)
  * D-criterion (Southworth-Hawkins or Drummond)
  * Concentric shell overlap (r_min,r_max ± margin)
* **Narrow-phase filters**:

  * Plane intersection (h-vector angle)
  * Phase alignment solver / torus check (fast linear congruences)
  * Analytic TCA (swept-sphere / relative motion)
  * MOID solver (ellipse-to-ellipse distance)
* **Final propagator**: RK4 with J2 (+drag if `rp < 350km`) around TCA window; produce final miss distance.
* **Maneuver Brain**: CW + ZEM-based minimum Δv; apply RTN→ECI; do fuel check & cooldown shadowing; simulate "ghost" recovery trajectory to ensure no recovery-collision.
* **Scheduler / Comms**: LOS check vs ground_stations.csv; schedule upload windows; include 10s signal latency.
* **Frontend**: maps, TCA ripples, trails, cascade graph, boot animation; connect to engine GET endpoints for state.
* **Tuner**: standalone C++ harness for parameter search using Monte-Carlo sampling and constrained optimization (CMA-ES). Produces constants used by engine.

---

# 5. Platform, build, runtime, CI

**Development / Build**

* OS: Ubuntu 22.04 base (docker)
* Build: CMake >= 3.22 (recommended)
* Compiler: g++ >= 12 or clang >= 15
* Container: Dockerfile uses `ubuntu:22.04` (one container for final submission)

**Runtime**

* Single Linux container exposes port 8000
* RAM: 4–8 GB during hackathon; tests run on grading VM
* CPU: aim for multi-core (4+) during local development

**CI**

* Provide Dockerfile + `docker build -t cascade .` and `docker run -p 8000:8000 cascade`
* Include unit tests (C++ Catch2 or doctest) and a performance script to run scenario sweeps.

---

# 6. Dependencies (why used, suggested versions, alternatives)

> **Principle**: The engine is C++ native. Use Julia (`jluna`) for heavy numerics optionally — but have pure-C++ fallback. No Python in production.

## Core C++ / build

* **CMake** (>= 3.22) — build orchestrator, cross-platform.
* **fmtlib** (`fmt`) — safe formatted logging (v9+). Alternative: `spdlog` (already uses fmt).
* **spdlog** — structured, fast logging.
* **Eigen** (>= 3.4) — linear algebra for C++ (matrix ops, small vectors). *Used heavily if Julia not used.*
* **Boost (headers-only)** — version macro verification; Boost.Asio available if needed later.
* **simdjson** (v3.9.4, FetchContent) — high-performance JSON parsing for telemetry ingestion.
* **cpp-httplib** (v0.15.3, FetchContent, header-only) — minimal REST API framework.
* **Catch2** or **doctest** — unit tests.

## Numerical / propagation

* **Julia (optional)** + **jluna** — call Julia from C++ for DifferentialEquations.jl solvers, MOID, and high-perf linear algebra. Use only if comfortable; otherwise implement numeric routines in C++ (Eigen + hand-RK4).

  * Julia packages: `DifferentialEquations.jl`, `StaticArrays.jl`, `LinearAlgebra` (std), `Measurements.jl` (diagnostics). Use `DifferentialEquations` only via batched calls.
* **libcmaes** — C++ CMA-ES optimizer for tuner; alternatives: implement simple evolutionary strategy.

## Storage / state

* **In-memory SoA** — primary. Fastest and simplest for hackathon.
* **ObjectBox (optional)** — if you want ACID-ish in-memory DB. *Note*: adds complex linking in Docker; include only if you can reliably build in container.
* **RocksDB / LMDB / SQLite** — alternatives if persistence desired.

## Math & helper libs

* **Eigen** (if not using Julia)
* **Boost.Random** or `std::mt19937` for Monte Carlo sampling
* **libcmaes** for tuner (optional)
* **OpenMP** or thread-pool for parallelizing pair checks

## Frontend

* **Node.js (>=18)**, **npm/yarn**
* **React** (v18) — main SPA
* **Tailwind CSS** — styling quick and consistent
* **deck.gl** — 2D map layers (Mercator ground tracks)
* **three.js** — 3D polar “bullseye” and cascade graph
* **react-three-fiber** (optional) — React bindings for three.js
* **MICR Extended** (font) — heading; include fallback
* **mapbox-gl** or **leaflet** — basemap (choose licensing-friendly option)
* **esbuild / Vite** — dev build tooling for snappy dev server

## DevOps / CI tooling

* **Docker**: base image `ubuntu:22.04`
* **GitHub Actions** (or local scripts) for build/test and lint.
* **gdb / valgrind** (local) for debug.

---

# 7. Important algorithms, formulas & edge-case rules (must implement)

**Always include unit tests for each of these behaviors.**

## Orbital math / Propagation basics

* ECI ↔ Kepler conversion (a, e, i, Ω, ω, M). Use robust algorithms; watch singularities (near circular, near-equatorial).
* J2 secular rates for Ω̇, ω̇, Ṁ (closed-form) — apply for cheap propagation over hours/days.
* Kepler propagator (solves Kepler's equation for M → E → ν).
* RK4 integrator for equations with J2 perturbation; Δt substepping when used.

## Filtering pipeline (guarantees)

* **Concentric shell overlap**: r_min = a(1−e); r_max = a(1+e). Overlap test must expand by 0.100 km + safety margin. Demonstrate mathematically zero false negatives for shell test (when r_min and r_max are correct for time window).
* **D-criterion** (Southworth–Hawkins or Drummond) used to discard unrelated orbits cheaply.
* **Phase/torus check**: linear congruences in angle-space to reject pairs that cannot arrive simultaneously.
* **Analytic TCA**: relative motion closed-form for small-window passes (project relative velocity).
* **MOID**: ellipse-to-ellipse minimum distance; use robust solver (Julia if available or implement numeric solver in C++).

## Maneuver & constraints

* **Clohessy–Wiltshire (CW)** and **Zero-Effort-Miss (ZEM)** to compute minimal Δv for impulsive burns.
* **Thrust limits and fuel**: implement Tsiolkovsky equation to reduce mass after burns.
* **600s cooldown**: manager must cluster maneuvers falling in cooldown to combine burns if safe and fuel-permitting.
* **5% fuel end-of-life guard**: if a burn reduces projected fuel below 5% (2.5 kg for 50 kg initial) abort evasion and schedule graveyard transfer per PS rules.
* **Ghost recovery simulation**: simulate recovery trajectory and re-run pipeline to avoid recovery-collision loops.

## Edge-case handling (explicit)

1. **Co-orbital / Δv = 0** — handle division-by-zero when computing TCA; add epsilon checks and fallback static-distance logic.
2. **Large Δt (e.g., 3600 s, 86400 s)** — run J2 secular projection + phase checks; escalate to MOID if geometric proximity becomes uncertain.
3. **Drag-sensitive perigee (<350km)** — mark as high-risk; either propagate with drag or conservatively keep them as candidates.
4. **Maneuver grouping in cooldown** — when multiple conjunctions fall within 600s, compute a combined Δv that avoids all, or prioritize per mission policy.
5. **Ground station blackouts** — implement last-available upload slot calculation (account for 10s latency) and queuing semantics.

---

# 8. Performance, capacity & testing targets

**Target scale (hackathon)**

* 50 satellites + 10,000 debris at tick-time: full end-to-end response ≤ 100 ms median (goal) under normal loads; 99th percentile ≤ 500 ms. (These numbers are achievable with filtering cascade.)

**Profiling & tests**

* Unit tests for each math module (ECI/Kepler, J2 rates, Kepler solver).
* Regression test against a high-fidelity reference (small RK4 integrator) for a set of adversarial scenarios.
* Performance harness:

  * Synthetic runner: 10k debris, 50 sats, run pipeline for 100 random Δt values (1–86,400s).
  * Measure false negatives vs full RK4 propagate reference for each scenario.
* Tuner tests: run Monte-Carlo sampling to tune margins on a synthetic/adversarial bank (C++ harness; parallelized).
* CI smoke: `docker build` plus `./run_smoke.sh` which runs a 2-second service sanity check and returns.

**Hardware estimate for dev**

* 4-core dev CPU; final demonstration machine ideally 8-core with 16+ GB to run frontend + engine in container smoothly.

---

# 9. Development roadmap (4-week aggressive plan)

**Week 0 (today):** finalize architecture & repo layout, Dockerfile scaffold, CMake skeleton, create sample telemetry JSON from PS, commit README.

**Week 1:** core ingestion, state SoA, ECI→Kepler, J2 secular updates & Kepler propagator, basic filter cascade (shell + D-criterion), minimal REST endpoints, simple console logs.

**Week 2:** implement phase checks, analytic TCA, MOID approximator or Julia bridge for MOID, maneuver math (CW/ZEM), fuel/cooldown logic, scheduler + LOS check.

**Week 3:** RK4 narrow-phase, "ghost" recovery simulations, safety edge-case unit tests, build the C++ tuner harness and run initial Monte-Carlo to pick conservative margins.

**Week 4:** frontend dashboard (boot animation, rotor, trails, TCA rings, cascade graph) + integration, demo scenarios, stress/performance tuning, polish README + docs, produce final Docker image and presentation materials.

**Deliver each Friday** with: working binary, integration test, short demo video or GIF, README update.

---

# 10. Repo contents & deliverables (what judges should see)

* `README.md` — short pitch, architecture diagram, build & run steps, demo instructions.
* `Dockerfile` — builds engine + optionally frontend; `docker build .` → `docker run -p8000:8000 cascade`.
* `CMakeLists.txt` + `src/` — engine code (ECI, propagator, filters, scheduler, web).
* `tuner/` — C++ tuner harness (optional separate Docker target).
* `frontend/` — React app with build script and sample state playback mode.
* `docs/` — architecture diagram, algorithm references, ground_stations.csv, sample telemetry files.
* `tests/` — unit tests + performance harness + adversarial scenarios.
* `demo/` — short GIFs and a small script to run demo sequences.
* `LICENSE` + `CONTRIBUTING.md` (brief).

---

# 11. Operational notes, risks & mitigations

**Risk: linking jluna + Julia in Docker breaks on grading VM**
Mitigation: provide pure C++ fallbacks (Eigen + hand-RK4) and a compile-time switch; ensure Dockerfile builds both paths; default to pure-C++ build for grading.

**Risk: heavy external DB (ObjectBox) complicates build**
Mitigation: use in-memory SoA state first. Only add ObjectBox as optional if you can test Docker build thoroughly. Provide two docker targets: `cascade:light` (no ObjectBox) and `cascade:db` (with DB).

**Risk: numerical edge cases cause NaN/inf**
Mitigation: epsilon guards, assert checks, unit tests on edge orbits (circular, equatorial, co-orbital).

**Risk: UI looks gimmicky**
Mitigation: follow the “terminal but organic” rules: restrained animation, mission palette (white + electric blue + amber/red), rotor oscillation, short boot, motion trails, TCA rings; make a polished 90–120s demo route to lead judges.

---

# Quick reference: recommended stack summary (compact)

* **Language**: C++20 (engine, tuner), optional Julia 1.10.0 (numerics) via `jluna`
* **Build**: CMake >= 3.22, GCC 12, Docker (ubuntu:22.04)
* **HTTP**: `cpp-httplib` v0.15.3 (header-only, FetchContent)
* **JSON**: `simdjson` v3.9.4 (FetchContent); raw strings for serialisation (nlohmann/json later if needed)
* **Linear algebra**: Eigen (or Julia `StaticArrays` if used)
* **Parallelism**: OpenMP / thread-pool for pair checks
* **Tuner**: C++ Monte-Carlo + libcmaes (no Python)
* **Frontend**: React + Tailwind + deck.gl + three.js + MICR Extended font
* **Testing**: Catch2 / doctest; performance harness in C++
* **Storage**: in-memory SoA; optional ObjectBox / RocksDB if time permits

---

# Appendix — Minimum commands (how reviewers should run)

1. Build (local)

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
# open frontend at http://localhost:3000 (or through built-in static server)
```

2. Smoke test

```bash
curl -X POST http://localhost:8000/api/telemetry -d @demo/sample_telemetry.json
curl http://localhost:8000/api/status
```

3. Run demo script

```bash
./demo/run_demo.sh   # plays a pre-recorded scenario and triggers the frontend demo
```

(Include `demo/` and `scripts/` in repo; make scripts idempotent & failing-fast for CI.)

---

# Final brutally honest checklist (what will win)

* ✅ **Solid, deterministic core** (C++ SoA + conservative physics + unit tests).
* ✅ **Zero false negatives** on adversarial testbank — this is non-negotiable.
* ✅ **Show a real demo**: CASCADE boots (box+rotor), shows trails + TCA rings, runs cascade simulation, and issues a maneuver that removes an imminent collision. Judges remember visuals.
* ✅ **Ship a simple build** (single Docker command) that reliably compiles without internet magic. No optional exotic libs required to run the basic demo.
* ✅ **Document everything**: how to build, run, explain trade-offs in README.

---
