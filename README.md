<div align="center">

# C . A . S . C . A . D . E

### Collision Avoidance System for Constellation Automation, Detection & Evasion

_National Space Hackathon 2026 · IIT Delhi_

</div>

CASCADE is an Autonomous Constellation Manager built for the National Space Hackathon 2026 problem statement. It ingests high-frequency ECI telemetry, predicts conjunctions, autonomously schedules evasion and recovery burns, tracks propellant usage, and serves a frontend mission console from the same Dockerized runtime.

## Submission Checklist

- Root `Dockerfile` uses `ubuntu:22.04`
- Runtime binds to `0.0.0.0:8000`
- Required APIs are exposed on port `8000`
- Frontend is served from the backend runtime
- Technical report: `docs/report.pdf`
- Technical report source: `docs/report.tex`

## Quick Start

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
curl -s http://localhost:8000/api/status | python3 -m json.tool
```

The application will be available at `http://localhost:8000`.

## Required API Surface

The project exposes the PS-required endpoints on port `8000`:

- `POST /api/telemetry`
- `POST /api/maneuver/schedule`
- `POST /api/simulate/step`
- `GET /api/visualization/snapshot`

Useful runtime/status endpoints:

- `GET /api/status`
- `GET /api/debug/burns`

## Frontend Modules

The frontend includes the required visualization modules from the problem statement:

- `#/track` — Ground Track map with trails and forecast arcs
- `#/threat` — Conjunction bullseye plot
- `#/burn-ops` — Maneuver timeline / scheduler
- `#/evasion` — Fuel-to-mitigation efficiency view
- `#/fleet-status` — Fleet-wide telemetry and resource posture
- `#/command` — overall operational summary

## Demo Path

The canonical end-to-end demo path is:

```bash
./scripts/run_ready_demo.sh
```

This script:

1. Starts the Dockerized system
2. Replays `3le_data.txt`
3. Injects calibrated critical encounters
4. Advances simulation time
5. Verifies autonomous avoidance with `scripts/check_demo_readiness.py`

## Verification

Quick Docker/API smoke test:

```bash
./scripts/docker_api_smoke.sh
```

Core backend safety suite:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROJECTBONK_ENABLE_JULIA_RUNTIME=OFF
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Repo Layout

```text
.
├── Dockerfile
├── docker-compose.yml
├── CMakeLists.txt
├── main.cpp
├── src/
├── frontend/
├── tools/
├── scripts/
├── docs/
│   ├── groundstations.csv
│   ├── report.pdf
│   └── report.tex
├── 3le_data.txt
└── PS.md
```

## Notes

- `3le_data.txt` is committed as a normal git file at the repo root for deterministic demos.
- `data.txt`, `train_data.csv`, and `test_data.csv` are intentionally left out of git and may exist only as local files.
- `docs/groundstations.csv` is included in the runtime image for LOS validation.
- The runtime image serves both the backend APIs and the built frontend.

## License

See `LICENSE`.

## Note ;)

`Project BONK` was the original internal hackathon codename used while the backend, binary name, and environment-variable surface were still moving quickly. The project was later renamed to `CASCADE` because the final system is about chained collision response: detect a threat, evaluate it, avoid it, recover the slot, and keep the constellation operating through that whole cascade of decisions. Some legacy identifiers such as `ProjectBONK` still remain in the repo and runtime settings to preserve compatibility and avoid risky late-stage renames.
