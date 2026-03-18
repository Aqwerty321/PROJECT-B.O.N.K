// ---------------------------------------------------------------------------
// simulation_engine.hpp — phase 3 simulation tick engine
// ---------------------------------------------------------------------------
#pragma once

#include "state_store.hpp"
#include "sim_clock.hpp"
#include "broad_phase.hpp"

#include <cstdint>
#include <vector>

namespace cascade {

struct NarrowPhaseConfig {
    // Conservative linear-TCA screening guard.
    double tca_guard_km = 0.02;

    // Near-threshold RK4 micro-refinement band above screening threshold.
    double refine_band_km = 0.10;

    // Full-window RK4 sampled refinement band above screening threshold.
    double full_refine_band_km = 0.20;

    // Additional uncertainty band for high-relative-speed pairs.
    double high_rel_speed_km_s = 8.0;
    double high_rel_speed_extra_band_km = 0.10;

    // Full-window sampled refinement controls.
    std::uint64_t full_refine_budget_base = 64;
    std::uint64_t full_refine_budget_min = 8;
    std::uint64_t full_refine_budget_max = 192;
    std::uint32_t full_refine_samples = 16;
    double full_refine_substep_s = 1.0;
    double micro_refine_max_step_s = 5.0;
};

struct StepRunStats {
    std::uint64_t propagated_objects = 0;
    std::uint64_t failed_objects = 0;
    std::uint64_t used_fast = 0;
    std::uint64_t used_rk4 = 0;
    std::uint64_t escalated_after_probe = 0;

    // Narrow-phase collision sweep (satellite vs debris at target epoch)
    std::uint64_t narrow_pairs_checked = 0;
    std::uint64_t collisions_detected = 0;
    std::uint64_t maneuvers_executed = 0;
    std::uint64_t narrow_refined_pairs = 0;
    std::uint64_t narrow_refine_cleared = 0;
    std::uint64_t narrow_refine_fail_open = 0;
    std::uint64_t narrow_full_refined_pairs = 0;
    std::uint64_t narrow_full_refine_cleared = 0;
    std::uint64_t narrow_full_refine_fail_open = 0;
    std::uint64_t narrow_full_refine_budget_allocated = 0;
    std::uint64_t narrow_full_refine_budget_exhausted = 0;
    std::uint64_t narrow_uncertainty_promoted_pairs = 0;
    std::vector<std::uint32_t> collision_sat_indices;

    // Broad-phase (conservative shell overlap)
    std::uint64_t broad_pairs_considered = 0;
    std::uint64_t broad_candidates = 0;
    std::uint64_t broad_shell_overlap_pass = 0;
    std::uint64_t broad_dcriterion_rejected = 0;
    std::uint64_t broad_dcriterion_shadow_rejected = 0;
    std::uint64_t broad_fail_open_objects = 0;
    std::uint64_t broad_fail_open_satellites = 0;
    double broad_shell_margin_km = 0.0;
    bool broad_dcriterion_enabled = false;
    double broad_a_bin_width_km = 0.0;
    int broad_band_neighbor_bins = 0;

    double target_epoch_s = 0.0;
};

struct StepRunConfig {
    BroadPhaseConfig broad_phase{};
    NarrowPhaseConfig narrow_phase{};
};

// Runs one simulation step with adaptive propagation.
// Returns true when clock was initialized and step executed.
bool run_simulation_step(StateStore& store,
                         SimClock& clock,
                         double step_seconds,
                         StepRunStats& out,
                         const StepRunConfig& cfg = StepRunConfig{}) noexcept;

} // namespace cascade
