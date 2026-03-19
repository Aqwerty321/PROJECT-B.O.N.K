# CASCADE Phase 4 Safety Calibration Notes

Date: 2026-03-18

## Goal

Phase 4 calibrates recovery and conjunction safety controls using deterministic
evidence while preserving PS endpoint contracts and fail-open behavior.

## Scope in this phase

1. Recovery planner calibration with runtime-configurable gains and bounded
   per-burn request ratio.
2. Narrow-phase fidelity hardening with uncertainty promotion telemetry.
3. Broad-phase D-criterion shadow evidence rollout before any runtime hard gate.

## Runtime calibration knobs

### Recovery (`PROJECTBONK_RECOVERY_*`)

- `PROJECTBONK_RECOVERY_SCALE_T`
- `PROJECTBONK_RECOVERY_SCALE_R`
- `PROJECTBONK_RECOVERY_RADIAL_SHARE`
- `PROJECTBONK_RECOVERY_SCALE_N`
- `PROJECTBONK_RECOVERY_FALLBACK_NORM_KM_S`
- `PROJECTBONK_RECOVERY_MAX_REQUEST_RATIO`

Safety intent:

- keep defaults conservative and backward-compatible,
- prevent over-correction against remaining recovery request via
  `MAX_REQUEST_RATIO`,
- allow sweep-first tuning before runtime promotion.

### Narrow-phase (`PROJECTBONK_NARROW_*`)

- `PROJECTBONK_NARROW_TCA_GUARD_KM`
- `PROJECTBONK_NARROW_REFINE_BAND_KM`
- `PROJECTBONK_NARROW_FULL_REFINE_BAND_KM`
- `PROJECTBONK_NARROW_HIGH_REL_SPEED_KM_S`
- `PROJECTBONK_NARROW_HIGH_REL_SPEED_EXTRA_BAND_KM`
- `PROJECTBONK_NARROW_FULL_REFINE_BUDGET_BASE`
- `PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MIN`
- `PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MAX`
- `PROJECTBONK_NARROW_FULL_REFINE_SAMPLES`
- `PROJECTBONK_NARROW_FULL_REFINE_SUBSTEP_S`
- `PROJECTBONK_NARROW_MICRO_REFINE_MAX_STEP_S`

Safety intent:

- preserve fail-open classification when refinement is uncertain or budget is
  exhausted,
- explicitly count uncertainty-promoted pairs and budget pressure.

### Broad-phase D-criterion (`PROJECTBONK_BROAD_*`)

- `PROJECTBONK_BROAD_DCRITERION_ENABLE` (default `0`)
- `PROJECTBONK_BROAD_DCRITERION_SHADOW` (default `1`)
- `PROJECTBONK_BROAD_DCRITERION_THRESHOLD`
- existing shell/band controls remain configurable.

Safety intent:

- collect rejection evidence in shadow mode before enabling filtering.

## Observability additions

- `/api/debug/propagation` and `GET /api/status?details=1` include:
  - `narrow_uncertainty_promoted_pairs`
  - `broad_dcriterion_shadow_rejected`
  - active recovery/narrow/broad calibration config values

## Sweep evidence snapshot

Strict sweep command:

```bash
./scripts/recovery_slot_sweep.sh ./build 24 0.08
```

Strict-expanded sweep command:

```bash
./scripts/recovery_slot_sweep.sh ./build 24 0.08 1.10 strict-expanded
```

Current deterministic outcome (with request-ratio candidate expansion):

- strict profile: PASS
- strict-expanded profile: PASS
- selected candidate: `grid_t1.2_r0.8_n0.8`
- selected evidence (strict):
  - mean delta slot error: `-0.074717`
  - mean fuel used: `0.316844 kg`
  - fuel ratio to default: `1.00597`

Repeated-run stability snapshot:

- strict runs (`run1`, `run2`, `run3`): same selected candidate
  `grid_t1.2_r0.8_n0.8`
- strict-expanded runs (`run1`, `run2`): same selected candidate
  `grid_t1.2_r0.8_n0.8`

Default-alignment note:

- runtime recovery defaults now align with calibrated sweep-safe baseline:
  - `fallback_norm_km_s = 1e-4`
  - `max_request_ratio = 0.05`

Determinism gate helper:

- `./scripts/recovery_sweep_determinism_gate.sh ./build 3 24 0.08 1.10`
- verifies both `strict` and `strict-expanded` profiles are PASS and choose the
  same selected candidate across repeated runs

Artifacts:

- `build/recovery_slot_sweep_strict.json`
- `build/recovery_slot_sweep_strict_expanded.json`
- `build/recovery_slot_sweep_strict_run1.json`
- `build/recovery_slot_sweep_strict_run2.json`
- `build/recovery_slot_sweep_strict_run3.json`
- `build/recovery_slot_sweep_strict_expanded_run1.json`
- `build/recovery_slot_sweep_strict_expanded_run2.json`

## Mandatory gate snapshot

All PASS on this calibration step:

- `./scripts/phase2_regression_gate.sh`
- `./scripts/broad_phase_sanity_gate.sh`
- `./scripts/narrow_phase_false_negative_gate.sh`
- `./scripts/maneuver_ops_invariants_gate.sh`
- `./scripts/recovery_slot_gate.sh`
- `./scripts/recovery_planner_invariants_gate.sh`
- `./scripts/api_contract_gate.sh`
- `ctest --test-dir build --output-on-failure`

Aggregate gate helper:

```bash
./scripts/phase4_calibration_gate.sh ./build
```

Build target:

```bash
cmake --build build --target phase4_calibration_gate
```

Artifact:

- `build/phase4_calibration_gate_summary.json`
  - contains per-step PASS/FAIL and overall phase4 gate result

## Narrow-phase calibration probe (phase 4)

Probe helper:

```bash
./scripts/narrow_phase_calibration_probe.sh ./build 50 10000 5 30
./scripts/narrow_phase_calibration_probe.sh ./build 50 10000 5 120 \
  5.5 0.25 96 16 320 0.35 0.14 0.03 24 0.7 3.0

# fixture-heavy profile to force uncertainty promotion / budget pressure
./scripts/narrow_phase_calibration_probe.sh ./build 50 10000 5 120 \
  5.5 0.25 96 16 320 0.35 0.14 0.03 24 0.7 3.0 4 8.5 0.42 0.01
```

Build target:

```bash
cmake --build build --target narrow_phase_calibration_probe
```

Observability gate helper:

```bash
./scripts/narrow_phase_uncertainty_observability_gate.sh ./build
```

Build target:

```bash
cmake --build build --target narrow_phase_uncertainty_observability_gate
```

Current probe evidence snapshot:

- baseline profile (`step=30s`):
  - `narrow_pairs_checked_total=2140274`
  - `narrow_uncertainty_promoted_pairs_total=0`
  - `narrow_full_refined_pairs_total=0`
  - `narrow_full_refine_budget_exhausted_total=0`
- stress profile (`step=120s`, wider bands/budget):
  - `narrow_pairs_checked_total=2140711`
  - `narrow_uncertainty_promoted_pairs_total=0`
  - `narrow_full_refined_pairs_total=0`
  - `narrow_full_refine_budget_exhausted_total=0`

Fixture-heavy evidence snapshot (targeted near-threshold high-relative-speed):

- baseline fixture profile (`step=30s`, defaults, `3` fixture pairs/sat):
  - `fixture_pairs_injected=150`
  - `narrow_pairs_checked_total=2500000`
  - `narrow_uncertainty_promoted_pairs_total=150`
  - `narrow_full_refined_pairs_total=32`
  - `narrow_full_refine_budget_exhausted_total=118`
- stress fixture profile (`step=120s`, tuned, `4` fixture pairs/sat):
  - `fixture_pairs_injected=200`
  - `narrow_pairs_checked_total=2500000`
  - `narrow_uncertainty_promoted_pairs_total=200`
  - `narrow_full_refined_pairs_total=48`
  - `narrow_full_refine_budget_exhausted_total=152`

Interpretation:

- fixture injection now exercises uncertainty-promotion and full-window budget
  pathways deterministically while keeping false-negative and contract gates
  green.

## Contract/details gate extension

- `api_contract_gate` now additionally verifies `GET /api/status?details=1`
  includes the new observability keys:
  - `narrow_uncertainty_promoted_pairs_total`
  - `broad_phase_shadow_dcriterion_rejected_total`
  - `narrow_uncertainty_promoted_pairs`
  - `broad_dcriterion_shadow_rejected`

## Promotion policy

- keep runtime defaults unchanged until separate promotion commit.
- only promote calibrated runtime defaults after repeated deterministic sweeps
  keep selecting the same strict-safe candidate and all hard gates remain green.
