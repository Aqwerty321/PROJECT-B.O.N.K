# Implementation Plan — PROJECT-B.O.N.K Backend Optimization

Generated: 2026-03-20
Branch: main (HEAD fa02062)
Status: APPROVED — executing

---

## Phase H — Housekeeping
- [x] Add `data.txt`, `tle2025.txt`, `ps.txt` to `.gitignore`

## Phase P0 — Critical Correctness (Fuel Efficiency: 20% weight)

### P0.1: Replace simplified CW/ZEM with true CW state-transition matrix
- **File**: `src/maneuver_recovery_planner.cpp`
- **Issue**: CW/ZEM formulas use simplified linear terms (`2/horizon_s * dr`) instead of the actual CW state-transition matrix inverse with `sin(nτ)`, `cos(nτ)` terms. Produces suboptimal delta-v.
- **Fix**: Implement true CW state-transition matrix inverse for the recovery solver. The CW equations in the Hill/Clohessy-Wiltshire frame give:
  ```
  Φ(τ) = [sin(nτ)/n terms, (1-cos(nτ))/n terms, ...]
  Δv = Φ_rv^{-1} * (r_target - Φ_rr * r_current - Φ_rv * v_current_rel)
  ```

### P0.2: Fix mean motion to use `a` instead of `r`
- **File**: `src/maneuver_recovery_planner.cpp:159`
- **Issue**: `n = sqrt(mu / r^3)` uses instantaneous radius instead of semi-major axis. Causes oscillating burn commands on eccentric orbits.
- **Fix**: Use `n = sqrt(mu / a^3)` where `a` is from orbital elements.

### P0.3: Fix T-hat to use `n×r` instead of `v/|v|`
- **File**: `src/maneuver_recovery_planner.cpp` (both heuristic and CW solvers)
- **Issue**: Velocity direction diverges from true tangential on eccentric orbits.
- **Fix**: Compute `t_hat = normalize(cross(h, r))` where `h = cross(r, v)`.

## Phase P1 — Performance (Algorithmic Speed: 15% weight)

### P1.1: Replace `std::pow` with direct multiplication in hot paths
- **Files**: `propagator.cpp:61`, `orbit_math.cpp:160,168,176`
- **Issue**: `std::pow(rmag, 5.0)` in J2 acceleration is 10-50x slower than `r2*r2*rmag`.
- **Fix**: Replace all `std::pow(x, N)` with explicit multiplication chains.

### P1.2: Precompute shared J2 secular rate terms
- **File**: `orbit_math.cpp`
- **Issue**: All 3 secular rate functions independently compute `a^3.5` and `cos(i)`.
- **Fix**: Factor out shared terms: precompute `cos_i`, `sin_i`, `p2 = a*a`, `p3 = p2*a`, `sqrt_p3 = sqrt(p3)`, etc.

### P1.3: Increase MOID samples or implement analytical MOID
- **File**: `simulation_engine.cpp`
- **Issue**: 24 samples = 15° resolution → 1780 km arc gap at LEO altitudes, making the MOID gate effectively a no-op.
- **Fix**: Increase to 72 samples (5° resolution) as minimum. Consider Gronchi's quartic for analytical MOID if time permits.

## Phase P2 — Safety Hardening (Safety Score: 25% weight)

### P2.1: Add linear TCA interpolation between RK4 sub-steps
- **File**: `simulation_engine.cpp`
- **Issue**: Full-window RK4 only checks distances at sub-step boundaries. A close approach between steps could be missed.
- **Fix**: After each RK4 sub-step, compute linear TCA between consecutive positions and check minimum distance.

### P2.2: Add fuel check in graveyard burn planning
- **File**: `src/maneuver_common.cpp`
- **Issue**: No explicit fuel guard — could schedule an impossible graveyard burn.
- **Fix**: Check `fuel_kg >= required_fuel_kg` before scheduling graveyard burn.

### P2.3: Enable CORS in Dockerfile
- **File**: `Dockerfile`
- **Issue**: CORS disabled by default — blocks browser-based frontend.
- **Fix**: Set `PROJECTBONK_CORS_ENABLE=1` and `PROJECTBONK_CORS_ALLOW_ORIGIN=*` in Dockerfile ENV.

## Phase P3 — Station-Keeping Quality (Constellation Uptime: 15% weight)

### P3.1: Add mean anomaly to slot error score
- **File**: `src/maneuver_common.cpp`
- **Issue**: Slot error score omits mean anomaly — no along-track drift detection.
- **Fix**: Add `|dM| / normalization` term to the slot error score computation.

### P3.2: Fix slot reference bootstrapping
- **File**: `src/maneuver_recovery_planner.cpp`
- **Issue**: After restart, displaced position becomes target slot — drift accumulates.
- **Fix**: Store initial slot reference at first telemetry ingestion and persist it, rather than bootstrapping from current state.

## Phase 5 — Offline ML Parameter Tuning

### 5.1: OMM ingester for real data
- Parse `data.txt` (33MB single-line JSON array of OMM records)
- Extract Keplerian elements or TLE lines, populate StateStore with real objects
- Replace synthetic `seed_store()` in tuner

### 5.2: Expand evaluation metrics
- fuel_efficiency_proxy (recovery burn dv magnitude)
- slot_convergence_proxy (slot error after N ticks)
- safety_proxy (missed conjunctions vs ground truth)

### 5.3: Ground truth generation
- All-pairs brute-force conjunction check on ~500-object subset
- Generate known conjunction truth sets as safety constraint

### 5.4: NSGA-II optimizer
- Lightweight multi-objective optimizer (~200 lines C++, no deps)
- Objectives: minimize compute cost, minimize fuel, maximize slot convergence
- Hard constraint: zero false negatives on truth set

### 5.5: Parameter export
- Output optimal parameters as `.env` file or env-var commands
- Loadable at runtime via existing env-var override system

### 5.6: Validation
- Run full gate suite with tuned parameters
- Confirm zero regressions

---

## Tunable Parameter Catalog

### Broad Phase (7 params, all env-overridable)
| Parameter | Default | Env Var |
|-----------|---------|---------|
| shell_margin_km | 50.0 | PROJECTBONK_BROAD_SHELL_MARGIN_KM |
| invalid_shell_pad_km | 200.0 | PROJECTBONK_BROAD_INVALID_SHELL_PAD_KM |
| a_bin_width_km | 500.0 | PROJECTBONK_BROAD_A_BIN_WIDTH_KM |
| i_bin_width_rad | 0.349 (20°) | PROJECTBONK_BROAD_I_BIN_WIDTH_RAD |
| band_neighbor_bins | 2 | PROJECTBONK_BROAD_BAND_NEIGHBOR_BINS |
| high_e_fail_open | 0.2 | PROJECTBONK_BROAD_HIGH_E_FAIL_OPEN |
| dcriterion_threshold | 2.0 | PROJECTBONK_BROAD_DCRITERION_THRESHOLD |

### Narrow Phase (17 params, all env-overridable)
| Parameter | Default | Env Var |
|-----------|---------|---------|
| tca_guard_km | 0.02 | PROJECTBONK_NARROW_TCA_GUARD_KM |
| refine_band_km | 0.10 | PROJECTBONK_NARROW_REFINE_BAND_KM |
| full_refine_band_km | 0.20 | PROJECTBONK_NARROW_FULL_REFINE_BAND_KM |
| high_rel_speed_km_s | 8.0 | PROJECTBONK_NARROW_HIGH_REL_SPEED_KM_S |
| high_rel_speed_extra_band_km | 0.10 | PROJECTBONK_NARROW_HIGH_REL_SPEED_EXTRA_BAND_KM |
| full_refine_budget_base | 64 | PROJECTBONK_NARROW_FULL_REFINE_BUDGET_BASE |
| full_refine_budget_min | 8 | PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MIN |
| full_refine_budget_max | 192 | PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MAX |
| full_refine_samples | 16 | PROJECTBONK_NARROW_FULL_REFINE_SAMPLES |
| full_refine_substep_s | 1.0 | PROJECTBONK_NARROW_FULL_REFINE_SUBSTEP_S |
| micro_refine_max_step_s | 5.0 | PROJECTBONK_NARROW_MICRO_REFINE_MAX_STEP_S |
| plane_angle_threshold_rad | 1.309 (75°) | PROJECTBONK_NARROW_PLANE_ANGLE_THRESHOLD_RAD |
| phase_angle_threshold_rad | 2.618 (150°) | PROJECTBONK_NARROW_PHASE_ANGLE_THRESHOLD_RAD |
| phase_max_e | 0.2 | PROJECTBONK_NARROW_PHASE_MAX_E |
| moid_samples | 24 | PROJECTBONK_NARROW_MOID_SAMPLES |
| moid_reject_threshold_km | 2.0 | PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM |
| moid_max_e | 0.2 | PROJECTBONK_NARROW_MOID_MAX_E |

### Recovery Planner (7 params, all env-overridable)
| Parameter | Default | Env Var |
|-----------|---------|---------|
| scale_t | 6e-5 | PROJECTBONK_RECOVERY_SCALE_T |
| scale_r | 2e-3 | PROJECTBONK_RECOVERY_SCALE_R |
| radial_share | 0.5 | PROJECTBONK_RECOVERY_RADIAL_SHARE |
| scale_n | 6e-3 | PROJECTBONK_RECOVERY_SCALE_N |
| fallback_norm_km_s | 1e-4 | PROJECTBONK_RECOVERY_FALLBACK_NORM_KM_S |
| max_request_ratio | 0.05 | PROJECTBONK_RECOVERY_MAX_REQUEST_RATIO |
| solver_mode | HEURISTIC | PROJECTBONK_RECOVERY_SOLVER_MODE |

### Non-overridable inline constants (need code changes to tune)
- Propagator fast-lane thresholds (6 params in propagator.cpp:14-24)
- Full-refine budget tiers (8 values in simulation_engine.cpp:683-696)
- CW/ZEM horizon/gains (5 values in maneuver_recovery_planner.cpp:161-200)
- Slot error normalization (4 values in maneuver_common.cpp:402)
- MOID HF refinement params (4 values in simulation_engine.cpp:244-321)
