# PS Alignment Improvement Plan

Date: 2026-03-27
Status: Active execution plan

This document maps the current repository state to the judge-facing requirements in PS.md and defines the next improvements that materially increase submission quality.

## Current verified baseline

- Backend builds cleanly with CMake and the main target compiles.
- Frontend production build passes.
- Auto-COLA now has a reproducible local ready-path with two confirmed credited avoids using the strict catalog-backed demo flow.
- The dashboard now surfaces slot-integrity and propagation-health signals that were previously only visible in backend metrics.
- A counterfactual A/B demo path now exists to compare the same encounter set with auto-intervention suppressed versus enabled.
- A frozen 5-minute storyboard now exists for the live judging flow.
- Frontend FPS evidence capture is scripted so dense-payload proof can be regenerated on demand.
- Root deployment requirements from PS Section 8 are already present:
  - root Dockerfile exists
  - runtime image is ubuntu:22.04
  - the server binds on 0.0.0.0 and defaults to port 8000

## PS.md conformance snapshot

| PS area | Status | Evidence | Remaining gap |
|---|---|---|---|
| Sections 3-5 backend physics and maneuver logic | Strong | J2 propagation, predictive CDM scan, auto-COLA, recovery, fuel and cooldown gates | keep extending real-data validation evidence |
| Section 4 required APIs | Strong | telemetry, schedule, step, snapshot, status all implemented | keep contract gate green in isolated and CI environments |
| Section 6.1 performance constraints | Partial | current frontend uses Three.js and compact snapshot payloads; `tests/fps-evidence.spec.js` and `scripts/capture_fps_evidence.sh` now capture frame-rate evidence | still needs a fresh recorded artifact from the final dense run |
| Section 6.2 required visualization modules | Partial | ground track, bullseye, fuel/resource surfaces, maneuver timeline, scorecard, and burn explainability are now present | remaining work is judge-facing polish and final screenshot/video capture, not missing modules |
| Section 7 evaluation criteria reporting | Strong | `#/scorecard` rolls up safety, fuel, uptime, speed, visualization, and logging evidence from live runtime signals | still package screenshots/video around the scorecard for the final deliverable |
| Section 8 deployment | Strong | Dockerfile and compose setup present | add one-command smoke proof into CI or release checklist |
| Section 9 deliverables | Partial | repo, runtime, storyboard, evidence index, and counterfactual demo guide are now formalized | technical report packaging and recorded final video still need to be frozen |

## Priority order

## P0 - Submission safety

These items directly reduce judge risk and should land before aesthetic or stretch work.

1. Lock the contract and deployment path.
   - Keep api_contract_gate runnable on an isolated port.
   - Add a CI or release-smoke step that proves Docker boot plus /api/status, /api/telemetry, /api/simulate/step, and /api/visualization/snapshot.
   - Acceptance: a fresh machine can clone, build, start, and hit all required APIs without manual fixes.

2. Preserve the reproducible ready demo.
   - Keep scripts/run_ready_demo.sh and scripts/check_demo_readiness.py as the canonical demo path.
   - Record expected outputs and failure signatures in docs/STRICT_REAL_DATA_DEMO_PLAN.md.
   - Acceptance: local run reaches verdict=ready and confirms two credited avoids on the documented path.

3. Publish a single judge-facing evidence index.
   - Add one short doc that links the exact commands, artifacts, and screenshots for each PS scoring category.
   - Acceptance: a reviewer can find safety, fuel, uptime, speed, UI, and logging evidence in under two minutes.

## P1 - Frontend PS closure

These items close the remaining Section 6 gaps.

1. Ground Track completion.
   - Ensure the map shows the last 90 minutes, next 90 minutes, and the day-night terminator cleanly at judge scale.
   - Acceptance: selected satellite history and forecast remain legible with full constellation rendering.

2. Bullseye completion.
   - Make TCA radius, relative approach angle, and severity colors explicit in labels and legend.
   - Acceptance: a reviewer can identify critical versus warning threats without reading backend JSON.

3. Maneuver timeline completion.
   - Show burn blocks, cooldown windows, blackout overlaps, dropped uploads, and execution state with unambiguous colors.
   - Acceptance: command conflicts and missed upload windows are visible from the timeline alone.

4. Fleet heatmap and scorecard unification.
   - Merge fuel posture, slot integrity, collisions avoided, and delta-v cost into a single operations panel.
   - Acceptance: the frontend visibly answers the Section 7 metrics without requiring debug endpoints.

## P2 - Deliverables packaging

These items improve judging efficiency and submission completeness.

1. Technical report pack.
   - Convert the backend architecture, gate evidence, and algorithm rationale into a short PDF-ready outline.
   - Include propagation model, broad/narrow phase strategy, maneuver logic, and deployment architecture.

2. Demo script and storyboard.
   - Freeze a sub-5-minute demo sequence: problem framing, live replay, credited avoid proof, dashboard walk-through, and Docker proof.
   - Prepare a fallback cut using recorded outputs from the ready-demo path.

3. Release checklist.
   - Verify repo visibility, Docker build, frontend bundle presence, ground-station dataset inclusion, and docs links.

## Suggested immediate next rehearsals

- Run `./scripts/run_counterfactual_demo.sh` and archive the comparison output.
- Run `./scripts/capture_fps_evidence.sh` after the ready-demo path and keep the screenshot + JSON artifact.
- Rehearse `docs/DEMO_STORYBOARD.md` until the live flow is comfortably under 5 minutes.