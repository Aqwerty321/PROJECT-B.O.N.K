# PS Evidence Index

Date: 2026-03-27
Purpose: one place to find the repo-native proof paths for each PS.md evaluation area and required deliverable.

## Section 7 score criteria

### Safety Score (25%)

- Live frontend view: `#/threat`, `#/burn-ops`, `#/evasion`
- Runtime evidence:
  - `GET /api/debug/burns`
  - `GET /api/debug/conjunctions?source=predicted`
  - `GET /api/status?details=1`
- Reproducible local demo:
  - `scripts/run_ready_demo.sh`
  - `scripts/check_demo_readiness.py`
- Counterfactual proof:
  - `scripts/run_counterfactual_demo.sh`
  - `docs/COUNTERFACTUAL_DEMO.md`
- Backend gates:
  - `narrow_phase_false_negative_gate`
  - `api_contract_gate`

### Fuel Efficiency (20%)

- Live frontend view: `#/evasion`, `#/fleet-status`
- Runtime evidence:
  - burn summary `fuel_consumed_kg`
  - burn summary `avoidance_fuel_consumed_kg`
  - per-satellite burn stats from `GET /api/debug/burns`
- Supporting proof:
  - ready-demo avoids with mitigation tracking

### Constellation Uptime (15%)

- Live frontend view: `#/fleet-status`, `#/command`
- Runtime evidence:
  - `status.internal_metrics.propagation_last_tick.stationkeeping_outside_box`
  - `status.internal_metrics.propagation_last_tick.stationkeeping_slot_radius_error_max_km`
  - satellite status counts from `GET /api/visualization/snapshot`

### Algorithmic Speed (15%)

- Live frontend view: `#/evasion`, `#/fleet-status`
- Runtime evidence:
  - `status.internal_metrics.command_latency_us`
  - object count from `GET /api/status?details=1`
- Batch evidence:
  - `phase3_tick_benchmark`
  - `broad_phase_validation`

### UI/UX & Visualization (15%)

- Live frontend views:
  - `#/command`
  - `#/track`
  - `#/threat`
  - `#/burn-ops`
  - `#/fleet-status`
- PS module mapping:
  - Ground track: `#/track`
  - Bullseye: `#/threat`
  - Resource heatmaps and fleet posture: `#/fleet-status`, `#/evasion`
  - Maneuver timeline: `#/burn-ops`
- Capture workflow:
  - `scripts/capture_fps_evidence.sh`
  - `npm run evidence:fps`
  - `tests/fps-evidence.spec.js`

### Code Quality & Logging (10%)

- Runtime evidence:
  - `GET /api/status?details=1`
  - `GET /api/debug/burns`
- Verification commands:
  - `cmake --build build --target ProjectBONK`
  - `ctest --test-dir build --output-on-failure`
  - `cd frontend && npm run build`

### Technical report packaging

- PDF-ready outline:
  - `docs/TECHNICAL_REPORT_OUTLINE.md`
- Architecture source:
  - `ARCHITECTURE.md`
- Evidence source:
  - `docs/PS_EVIDENCE_INDEX.md`
- Demo storyboard:
  - `docs/DEMO_STORYBOARD.md`
- Counterfactual narrative:
  - `docs/COUNTERFACTUAL_DEMO.md`

## Section 8 deployment requirements

- Dockerfile: root `Dockerfile`
- Base image: `ubuntu:22.04`
- Port exposure: `8000`
- Local smoke:
  - `docker build -t cascade:local .`
  - `docker run --rm -p 8000:8000 cascade:local`
  - `curl -s http://localhost:8000/api/status`
  - `./scripts/docker_api_smoke.sh`

## Section 9 deliverables checklist

- Public GitHub repo: repository root
- Docker environment: root `Dockerfile`
- Technical report: still needs packaging into a PDF-ready artifact
- Video demonstration: use `docs/DEMO_STORYBOARD.md` as the frozen speaking plan, then record the final run
- Counterfactual proof path: `scripts/run_counterfactual_demo.sh`
- FPS/UI evidence path: `scripts/capture_fps_evidence.sh`
