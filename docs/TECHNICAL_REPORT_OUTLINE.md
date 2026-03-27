# Technical Report Outline

Date: 2026-03-27
Purpose: PDF-ready structure for the final submission report. This is written to be easy to convert into LaTeX or a short conference-style PDF without rediscovering what belongs in each section.

## Target format

- Length: 4 to 6 pages plus appendix if needed
- Style: short systems paper, figure-heavy, minimal prose padding
- Output: PDF
- Primary source docs:
  - `ARCHITECTURE.md`
  - `docs/PS_EVIDENCE_INDEX.md`
  - `docs/STRICT_REAL_DATA_DEMO_PLAN.md`
  - `README.md`

## Title block

- Title: `CASCADE: Autonomous Orbital Collision Avoidance and Constellation Management for Congested LEO`
- Subtitle: `National Space Hackathon 2026 Submission`
- Team line: team name, members, contact email, GitHub repository URL

## Abstract

Write one compact paragraph covering:

- the problem: autonomous conjunction management for large LEO constellations
- the system: telemetry ingestion, predictive CDM scan, auto-COLA, recovery, and frontend mission console
- the differentiator: conservative safety policy with deterministic runtime and judge-ready deployment
- the outcome: Dockerized port-8000 runtime, real-data demo path, and live operational dashboard

## 1. Problem and design goals

State the judge-facing goals in PS.md terms.

- high-frequency ECI telemetry ingestion
- 24-hour predictive conjunction assessment
- autonomous collision avoidance and recovery
- fuel-aware scheduling under cooldown and LOS constraints
- real-time operational visualization

Recommended figure:

- one system overview block diagram from telemetry to dashboard

## 2. System architecture

Summarize the runtime topology.

- C++ engine and in-process state store
- HTTP API boundary on port 8000
- propagation, broad phase, narrow phase, maneuver planner, scheduler, dashboard
- Docker packaging and CI verification

Recommended table:

- module, responsibility, key files, runtime cost profile

## 3. Numerical methods and safety policy

This is the technical core.

### 3.1 State representation and propagation

- ECI state vectors
- J2-aware propagation
- adaptive propagation and timestep behavior

### 3.2 Conjunction filtering pipeline

- broad phase candidate reduction
- narrow phase escalation
- predictive conjunction persistence and severity tiers
- fail-open or conservative classification policy

### 3.3 Maneuver planning

- RTN-frame burn construction
- fuel accounting and cooldown enforcement
- upload/LOS constraints
- recovery and slot-integrity handling

Recommended equations:

- J2 acceleration
- collision threshold at 0.100 km
- Tsiolkovsky fuel relation

Recommended figure:

- one pipeline chart showing broad phase to burn queue

## 4. Frontend mission console

Describe the judge-facing cockpit.

- Ground Track page with real-time markers, 90-minute trail, 90-minute forecast, and live solar terminator
- Threat page with bullseye and severity filtering
- Burn timeline with blackout and friction markers
- Fleet/evasion/scorecard views for PS Section 7 criteria

Recommended figure set:

- Ground Track screenshot
- Threat/Bullseye screenshot
- Burn timeline screenshot
- Scorecard screenshot

## 5. Validation and evidence

Organize by PS scoring category.

### 5.1 Safety

- gate results
- ready-demo path
- confirmed avoids and conjunction tracking

### 5.2 Fuel efficiency and uptime

- burn summary metrics
- slot-integrity metrics
- avoidance-fuel versus total-fuel discussion

### 5.3 Speed and deployment

- command-latency or benchmark evidence
- Docker/API smoke gate
- CI summary artifacts

Recommended table:

- criterion, evidence source, command or route, result snapshot

## 6. Deployment and deliverables

- root Dockerfile with `ubuntu:22.04`
- binds `0.0.0.0:8000`
- release verification command set
- repo links, video plan, and report artifact

Recommended callout box:

- exact clone/build/run/smoke commands

## 7. Limitations and next steps

Be explicit and brief.

- finalize the strongest dense-scene FPS proof
- package the final narrated demo video
- continue tightening frontend visualization polish around the judge story

## Appendix A. Command appendix

Include the exact commands worth pasting into the report appendix.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ProjectBONK
ctest --test-dir build --output-on-failure
./scripts/api_contract_gate.sh ./build
./scripts/docker_api_smoke.sh
./scripts/run_ready_demo.sh
cd frontend && npm run build
```

## Appendix B. Figure checklist

- architecture diagram
- Ground Track with trail, forecast, terminator
- bullseye threat view
- maneuver timeline
- scorecard view
- ready-demo confirmation output

## Appendix C. Writing notes

- Prefer concrete metrics over adjectives.
- Use PS.md language when possible so judges can map claims directly to the rubric.
- Every claim in the report should point back to either a runtime route, a script, a gate, or a screenshot.