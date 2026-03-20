# CASCADE Backend Audit and Next Steps

Date: 2026-03-19
Branch: `phase4-safety-calibration`

This document is the canonical backend audit and execution plan before frontend
work. It maps current implementation to `PS.md` and `ARCHITECTURE.md`, calls
out safety/performance risks, and defines a measurable plan with acceptance
criteria.

## 1) Verified baseline (as of this audit)

- Aggregate phase 4 gate passes end-to-end:
  - command: `cmake --build build --target phase4_calibration_gate`
  - artifact: `build/phase4_calibration_gate_summary.json`
  - result: overall `PASS`
- Hard gates currently green in local run:
  - `phase2_regression_gate`
  - `broad_phase_sanity_gate`
  - `narrow_phase_false_negative_gate`
  - `maneuver_ops_invariants_gate`
  - `recovery_slot_gate`
  - `recovery_planner_invariants_gate`
  - `api_contract_gate`
  - `ctest` suite (`8/8`)
- Recovery calibration determinism evidence remains stable:
  - strict + strict-expanded repeated sweeps select
    `grid_t1.2_r0.8_n0.8`
- Phase 4 observability evidence is non-zero under fixture-heavy probe:
  - uncertainty promoted pairs > 0
  - full-refined pairs > 0

## 2) Audit changes applied during this pass

- Fixed CMake target-definition ordering for robust dependency resolution:
  - moved `narrow_phase_uncertainty_observability_gate` target below
    `narrow_phase_calibration_probe` target definition in `CMakeLists.txt`
- Fixed Docker build reproducibility:
  - `Dockerfile` now copies `src/`, `tools/`, `tuner/`, and `scripts/`
    before final build stage
  - `docker build -t cascade:audit-local .` now succeeds
- Updated stale documentation references for selected recovery candidate:
  - `docs/HANDOFF.md` and `docs/PHASE 3.md` now align with
    `grid_t1.2_r0.8_n0.8`
- Added runbook pointer from handoff to this plan:
  - `docs/HANDOFF.md` now links `docs/next-steps.md`
- Expanded deterministic no-FN gate scenario families:
  - `tools/narrow_phase_false_negative_gate.cpp` now covers baseline/high-e/
    coorbital/crossing + long-step windows
  - `scripts/narrow_phase_false_negative_gate.sh` now accepts optional
    scenario/size args and defaults to expanded coverage
- Externalized ground-station dataset source:
  - added `docs/groundstations.csv`
  - runtime now loads from CSV (or env override
    `PROJECTBONK_GROUND_STATIONS_CSV`) with safe fallback to builtin defaults
  - invariants gate now validates catalog count/IDs and reports source

Current status after these updates:

- `phase4_calibration_gate`: PASS
- `maneuver_ops_invariants_gate`: PASS (including ground-station catalog check)
- `narrow_phase_false_negative_gate`: PASS with expanded deterministic families
- `api_contract_gate`: PASS
- Docker build/smoke: PASS

## 3) PS conformance matrix (`PS.md` vs current backend)

Legend: `DONE`, `PARTIAL`, `MISSING`, `RISK`

| PS area | Status | Evidence | Gap / risk | Required follow-up |
|---|---|---|---|---|
| `POST /api/telemetry` request/ACK shape | DONE | parser + ACK builders + contract gate | none for core schema | keep schema lock in contract gate |
| `POST /api/maneuver/schedule` request/validation body shape | DONE | parser + cooldown/fuel/LOS checks + contract gate | response status currently `202` while PS example omits explicit HTTP code | confirm grader expectation for status code policy |
| `POST /api/simulate/step` request/ACK shape | DONE | parser + runtime step + contract gate | no explicit upper bound on `step_seconds` may stress queue under abuse | add optional max-step guard in P0 hardening |
| `GET /api/visualization/snapshot` shape | DONE | snapshot builder includes timestamp/sat/debris keys | none for required keys | preserve schema in contract gate |
| `GET /api/status` health shape | DONE | status builder + contract gate | details mode is non-PS extension (acceptable) | keep default response PS-clean |
| ECI state usage and units | DONE | state store + telemetry/parser conventions | none | keep regression checks |
| J2 perturbation support | DONE | `orbit_math` + `propagator` + RK4 path | no atmospheric drag model (architecture marks drag as optional) | treat as PARTIAL only if drag becomes mandatory |
| 100 m conjunction threshold policy | PARTIAL | threshold constant present (`0.100 km`) | default narrow guard (`+0.02 km`) makes runtime conservative (fewer FN, more FP) | explicitly document and test this policy to avoid confusion |
| Fuel depletion via Tsiolkovsky | DONE | `propellant_used_kg` + schedule/exec checks | none in core equation use | add invariant tests with edge masses |
| 15 m/s burn cap + 600s cooldown | DONE | parser cap + schedule cooldown + queue cooldown | no burn-combination policy yet | add combined-burn strategy only if needed |
| 5% fuel EOL/graveyard behavior | DONE | fuel floor checks + graveyard planning/execution | graveyard transfer is heuristic (single impulse policy) | add stricter graveyard acceptance scenarios |
| Station-keeping 10 km box and penalty | PARTIAL | box radius and exponential penalty metrics present | penalty is observed metric, not yet used in optimization objective | define policy for objective coupling |
| Communication latency + LOS | PARTIAL | 10s latency + LOS window planning | planner remains simplified for blackout/upload optimization | continue scheduler fidelity upgrades |
| Deployment requirement (`Dockerfile`, Ubuntu, port 8000) | DONE | root Dockerfile, `ubuntu:22.04`, `EXPOSE 8000` | previously broken image due missing source copy, now fixed | keep image build in CI |
| Frontend visualization modules | MISSING (deferred intentionally) | no frontend directory currently | not started by design per backend-first instruction | start only after backend exit criteria |

## 4) Architecture uptake matrix (`ARCHITECTURE.md` vs implementation)

| Architecture element | Status | Notes |
|---|---|---|
| In-process SoA state store | DONE | `StateStore` is central runtime source of truth |
| HTTP boundary and modular handlers | DONE | `src/http/*` + `EngineRuntime` command boundary |
| Adaptive propagation (fast + RK4) | DONE | fast J2+Kepler path + RK4 fallback/probe escalation |
| Broad-phase shell/indexing + fail-open policy | DONE | shell overlap + band indexing + fail-open behavior |
| D-criterion rollout with shadow mode | PARTIAL | shadow and opt-in gate exist; runtime defaults keep hard filter disabled |
| Narrow-phase analytic TCA + RK4 refinements | PARTIAL | conservative linearized TCA + micro/full RK4 refinement implemented |
| Plane intersection / phase-torus gate | PARTIAL | shadow-first plane/phase gate scaffolding implemented; hard reject remains opt-in |
| MOID solver stage | PARTIAL | MOID-proxy shadow stage implemented; full HF evaluator remains pending |
| Maneuver brain CW/ZEM solver | MISSING | current planner is heuristic slot-delta based |
| Scheduler with blackout/upload semantics | PARTIAL | LOS + latency upload planning exists; static stations and simplified planner |
| Offline tuner path | DONE | deterministic offline tuner scaffold and sweep tooling |
| Frontend mission console | MISSING (deferred) | intentionally deferred |
| CI deterministic safety gate stack | DONE | aggregate phase4 gate and law-assertions gate are wired in CI with artifacts |

## 5) Critical gaps and risk register

### P0 risk (blockers before frontend)

1. False-negative evidence is strong but still synthetic-limited.
   - Risk: blind spots for adversarial geometry/time windows not represented.
2. Maneuver planner is heuristic, not CW/ZEM or equivalent validated solver.
   - Risk: may meet safety but underperform fuel/uptime objectives.
3. Full HF MOID evaluator is still pending behind placeholder fail-open mode.
  - Risk: fidelity/performance ceiling for narrow-phase decisions.

### P1 risk (important, not immediate blocker)

1. Broad-phase indexing currently uses only `a` bin key path in practice.
   - `i` bin utility exists but is not used in key population/lookup.
   - Risk: avoidable candidate volume and performance inefficiency.
2. Broad-phase candidate list includes non-debris objects, filtered later.
   - Risk: wasted narrow-phase scanning overhead.
3. API status code interpretation for successful schedule remains an external
   contract ambiguity.
   - Risk: grader mismatch if strict HTTP code expectations differ.

### P2 risk (cleanup and maintainability)

1. Historical phase docs can drift from latest calibration evidence.
2. Unit test surface is narrower than gate harness surface.
3. Additional observability could be exported for operations dashboards.

## 6) Execution plan (detailed, prioritized)

## P0 - Backend hardening completion (frontend blocked)

### P0.1 [COMPLETE] Wire aggregate phase4 gate into CI

- Why:
  - enforce one authoritative end-to-end safety/calibration gate.
- Deliverables:
  - `.github/workflows/ci.yml` runs `phase4_calibration_gate`
  - `build/phase4_calibration_gate_summary.json` uploaded as CI artifact
- Acceptance:
  - CI fails if any phase4 step fails
  - summary artifact available on CI runs
- Verify:
  - `cmake --build build --target phase4_calibration_gate`

### P0.2 Strengthen no-false-negative evidence bank

- Why:
  - current gate scenarios are deterministic but modest in diversity.
- Deliverables:
  - expand narrow-phase FN harness scenarios across:
    - co-orbital low-relative-speed passes
    - high-relative-speed near-threshold passes
    - long-step windows (`3600`, `21600`, `86400`)
    - high-e and perigee-low stress cases
  - include deterministic seeds and artifact output
- Acceptance:
  - expanded gate remains PASS with `false_negative_sats_total=0`
  - reference collisions observed in each scenario family
- Verify:
  - `./scripts/narrow_phase_false_negative_gate.sh ./build`

### P0.3 [COMPLETE] Data-source correctness for ground stations

- Why:
  - PS provides dataset; hardcoded list is fragile.
- Deliverables:
  - station loader from CSV tracked in repo docs/data path
  - deterministic fallback when file unavailable
  - invariants gate checks catalog source/count/IDs
- Acceptance:
  - schedule/LOS behavior unchanged under default dataset
  - station data source reported by invariants output
- Verify:
  - `./scripts/maneuver_ops_invariants_gate.sh ./build`
  - `./scripts/api_contract_gate.sh ./build`

### P0.4 Clarify and lock collision-threshold policy

- Why:
  - runtime currently uses threshold+guard in conservative classification.
- Deliverables:
  - explicitly document safety rationale and effective threshold in status debug
  - add contract-safe metric key in details mode for configured guard
  - add test asserting guard does not produce false negatives when set to `0`
- Acceptance:
  - policy is documented and test-backed
  - no PS payload drift in default endpoints
- Verify:
  - `./scripts/narrow_phase_false_negative_gate.sh ./build`
  - `./scripts/api_contract_gate.sh ./build`

### P0.5 Contract guardrail review and decision log

- Why:
  - avoid grader-facing ambiguity and accidental schema drift.
- Deliverables:
  - freeze explicit contract notes for:
    - schedule success HTTP code
    - optional details fields
    - accepted error-code taxonomy
  - extend `api_contract_gate` checks only where PS-safe
- Acceptance:
  - all core PS endpoint keys/status semantics validated in one gate
- Verify:
  - `./scripts/api_contract_gate.sh ./build`

Status: partial progress completed

- `api_contract_gate` now also asserts:
  - pre-telemetry `simulate/step` deterministic rejection (`CLOCK_UNINITIALIZED`),
  - malformed telemetry deterministic rejection (`MALFORMED_JSON`),
  - unknown satellite schedule rejection (`SATELLITE_NOT_FOUND`),
  - default `GET /api/status` remains PS-clean (no `internal_metrics` leak),
  - `?verbose=1` parity with `?details=1` details behavior.
- details mode now exposes explicit threshold semantics:
  - `collision_threshold_km`
  - `narrow_tca_guard_km`
  - `effective_collision_threshold_km`
- schedule success expected HTTP status is now explicitly gate-configurable via
  `PROJECTBONK_API_CONTRACT_SCHEDULE_SUCCESS_STATUS` (default `202`) for
  compatibility checks without changing runtime defaults.
- runtime now also supports controlled policy overrides:
  - `PROJECTBONK_SCHEDULE_SUCCESS_STATUS` (default `202`, range `200..299`)
  - `PROJECTBONK_MAX_STEP_SECONDS` (default `86400`)
  and details mode exposes these active values in `GET /api/status?details=1`.
- `api_contract_gate` now defaults its expected schedule success status to
  runtime `PROJECTBONK_SCHEDULE_SUCCESS_STATUS` when present; explicit gate
  override `PROJECTBONK_API_CONTRACT_SCHEDULE_SUCCESS_STATUS` still takes
  precedence for external compatibility probes.

### P0.6 Documentation consistency sweep

- Why:
  - stale docs increase operational mistakes and handoff risk.
- Deliverables:
  - synchronize `README.md`, `docs/HANDOFF.md`, phase docs, and this plan
  - include latest calibrated candidate and gate commands in one canonical place
- Acceptance:
  - no conflicting candidate/value claims across docs
- Verify:
  - grep checks over docs for stale candidate IDs

## P1 - Capability closure and performance efficiency

### P1.1 Broad-phase efficiency correctness

- Deliverables:
  - integrate inclination bin into active indexing key path
  - avoid adding non-debris candidates into narrow-phase candidate list
- Acceptance:
  - broad-phase candidate count decreases on benchmark loads
  - broad-phase sanity and FN gates remain PASS
- Verify:
  - `./scripts/broad_phase_sanity_gate.sh ./build`
  - `./scripts/narrow_phase_false_negative_gate.sh ./build`
  - `./build/phase3_tick_benchmark 50 10000 5 10 30`

Status update (current branch):

- broad-phase indexed path now avoids full debris scans by iterating a
  per-satellite selected candidate list built from fail-open debris + indexed
  bands
- optional inclination-neighbor filtering toggle added:
  `PROJECTBONK_BROAD_I_NEIGHBOR_FILTER` (default `0`, explicit opt-in)
- safety validation results after this change set:
  - `broad_phase_sanity_gate`: PASS (`missing_vs_shell_baseline_total=0`)
  - `narrow_phase_false_negative_gate`: PASS (`false_negative_sats_total=0`)
- benchmark evidence (3 runs each, `phase3_tick_benchmark 50 10000 10 30 30`):
  - filter `0`: mean tick 16.919 ms, mean p95 18.127 ms,
    broad candidates 8,733,275
  - filter `1`: mean tick 14.470 ms, mean p95 15.907 ms,
    broad candidates 6,697,647
  - net impact: about 14.5% lower mean tick latency and about 23.3% fewer
    broad candidates with gate parity preserved
  - promotion check outcome: do not flip default yet; a strict default-on run of
    `broad_phase_sanity_gate` reported `missing_vs_shell_baseline_total=83158`
    (`indexed_pairs_total=1229360` vs `baseline_shell_pairs_total=1312518`)

### P1.2 Narrow-phase architecture completion path

- Deliverables:
  - add plane-intersection and phase-gating stage(s) before expensive refine
    (shadow-first rollout; hard reject remains opt-in until evidence stabilizes)
  - evaluate MOID stage feasibility (full solver or conservative approximation)
- Acceptance:
  - reduced narrow pairs/compute on benchmark scenarios
  - zero false negatives preserved under gate suite
- Verify:
  - phase4 aggregate gate + benchmark comparisons

Status update (current branch):

- Introduced narrow plane/phase pre-refine gate scaffolding in shadow-first mode:
  - `PROJECTBONK_NARROW_PLANE_PHASE_SHADOW` (default `1`)
  - `PROJECTBONK_NARROW_PLANE_PHASE_FILTER` (default `0`)
  - `PROJECTBONK_NARROW_PLANE_ANGLE_THRESHOLD_RAD`
  - `PROJECTBONK_NARROW_PHASE_ANGLE_THRESHOLD_RAD`
  - `PROJECTBONK_NARROW_PHASE_MAX_E`
- Added sampled MOID-proxy shadow stage scaffolding (still shadow-first):
  - `PROJECTBONK_NARROW_MOID_SHADOW` (default `1`)
  - `PROJECTBONK_NARROW_MOID_FILTER` (default `0`)
  - `PROJECTBONK_NARROW_MOID_SAMPLES`
  - `PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM`
  - `PROJECTBONK_NARROW_MOID_MAX_E`
- Implemented `PROJECTBONK_NARROW_MOID_MODE=hf` evaluator path:
  - replaces placeholder with two-stage sampled evaluation (coarse global
    search + local refinement)
  - preserves fail-open behavior for invalid/non-finite/sampling-failure cases
  - compatibility reason key `narrow_moid_fail_open_reason_hf_placeholder` is
    retained and expected to remain `0`
- Added MOID validation matrix script and stress fixtures:
  - `scripts/moid_filter_validation.sh` runs proxy/hf hard-filter canary + hf
    observability and emits a single summary artifact
  - false-negative harness now includes MOID stress families:
    `moid_threshold_edge`, `moid_high_e_guard`, `moid_stale_epoch`
  - MOID counters (`evaluated`, `shadow_rejected`, `hard_rejected`) are now
    exported in gate output/canary summaries
- Coverage closure evidence (full stress profile `13/10/160`):
  - `build/moid_filter_validation_summary.json`: PASS
  - `proxy_moid_hard_reject_exercised=true`
  - `hf_moid_hard_reject_exercised=true`
  - `false_negative_sats_total=0` in proxy/hf canary
  - `narrow_moid_fail_open_reason_hf_placeholder_total=0` in hf observability
- Added observability counters in debug/status details and calibration probe:
  - plane/phase and MOID-proxy evaluated/shadow-rejected/hard-rejected/
    fail-open pair totals
  - hard-rejected remains expected `0` under default shadow-first policy
- Calibrated observability probe defaults to ensure deterministic non-zero MOID
  shadow evidence while keeping FN gate at zero (fixture profile now uses
  `high_rel_speed_km_s=2.0`, `high_rel_speed_extra_band_km=0.5`,
  `fixture_rel_speed_km_s=3.0`).

### P1.3 Maneuver planner fidelity upgrade path

- Deliverables:
  - evaluate CW/ZEM-based planner prototype against current heuristic planner
  - compare fuel and slot-recovery metrics on deterministic scenario bank
- Acceptance:
  - improved or equal safety and measurable objective improvement
  - no cooldown/LOS/fuel invariant regressions
- Verify:
  - `./scripts/recovery_slot_gate.sh ./build`
  - `./scripts/recovery_planner_invariants_gate.sh ./build`
  - `./scripts/maneuver_ops_invariants_gate.sh ./build`

## P2 - Operational polish and pre-frontend staging

### P2.1 Multi-tier gate strategy (fast PR + deep nightly)

- Fast PR gate set (target <= ~10 min):
  - phase2 regression
  - broad-phase sanity
  - narrow-phase false-negative
  - API contract
  - selected invariants
- Nightly/full gate set:
  - `phase4_calibration_gate`
  - extended scenario sweeps and benchmark captures

### P2.2 Soak and reproducibility runs

- Deliverables:
  - repeated deterministic runs over fixed seed banks
  - record trend artifacts for narrow/broad/recovery counters
- Acceptance:
  - deterministic selection and no FN regressions across repeated runs

### P2.3 Frontend start gate (must satisfy all)

- Entry criteria:
  - P0 tasks complete and merged
  - CI includes aggregate phase4 gate with artifact retention
  - no open P0 backend contract/safety findings

## 7) Verification commands (canonical)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROJECTBONK_ENABLE_JULIA_RUNTIME=OFF
cmake --build build --target phase4_calibration_gate

./scripts/phase2_regression_gate.sh ./build
./scripts/broad_phase_sanity_gate.sh ./build
./scripts/narrow_phase_false_negative_gate.sh ./build
./scripts/phase1_evidence_baseline.sh ./build ./build/phase1_evidence_baseline_summary.json
./scripts/moid_filter_validation.sh ./build ./build/moid_filter_validation_summary.json
./scripts/maneuver_ops_invariants_gate.sh ./build
./scripts/recovery_slot_gate.sh ./build
./scripts/recovery_planner_invariants_gate.sh ./build
./scripts/api_contract_gate.sh ./build

ctest --test-dir build --output-on-failure
```

Docker verification:

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
```

## 8) Promotion and rollback policy (calibration defaults)

- Promotion only when all conditions are true:
  1. strict + strict-expanded deterministic sweeps repeatedly select same
     candidate,
  2. aggregate phase4 gate passes,
  3. API contract gate passes unchanged,
  4. no new P0 risks introduced.
- Rollback trigger:
  - any gate regression on promoted defaults or unexplained determinism break.
- Rollback mechanism:
  - revert defaults in dedicated commit, keep knobs env-overridable, rerun full
    phase4 gate before re-promotion.

## 9) Open decisions and recommended defaults

1. Schedule success HTTP code policy
   - Recommended default: keep `202` unless grader explicitly demands `200`.
   - Impact: changing code affects contract gate and potentially integrations.

2. MOID implementation timing
   - Recommended default: defer full MOID to P1 after P0 hardening and CI gate
     unification.
   - Impact: keeps near-term risk low while preserving safety-first posture.

3. D-criterion enablement in runtime
   - Recommended default: keep runtime hard filter disabled (`shadow` only)
     until broader FN evidence bank is complete.
   - Impact: higher compute, lower FN risk.

4. CI runtime budget
   - Recommended default: keep PR gate under ~10 minutes and run deep phase4
     aggregate checks nightly/full.
   - Impact: faster developer feedback while preserving deep safety coverage.

## 10) Definition of done for backend phase

Backend is considered ready for frontend integration when:

- all P0 items are complete,
- CI executes and enforces aggregate phase4 gate,
- `api_contract_gate` remains stable with no PS schema/status drift,
- deterministic sweep and observability artifacts are consistently green,
- docs are synchronized and no stale calibration claims remain.
