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
    enum class MoidMode : std::uint8_t {
        PROXY = 0,
        HF = 1,
    };

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

    // Conservative pre-refine gate (plane/phase) rollout.
    bool plane_phase_shadow = true;
    bool plane_phase_filter = false;
    double plane_angle_threshold_rad = 1.3089969389957472; // 75 deg
    double phase_angle_threshold_rad = 2.6179938779914944; // 150 deg
    double phase_max_e = 0.2;

    // MOID proxy rollout (sampled, shadow-first).
    MoidMode moid_mode = MoidMode::PROXY;
    bool moid_shadow = true;
    bool moid_filter = false;
    std::uint32_t moid_samples = 72;
    double moid_reject_threshold_km = 2.0;
    double moid_max_e = 0.2;
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
    std::uint64_t narrow_plane_phase_evaluated_pairs = 0;
    std::uint64_t narrow_plane_phase_shadow_rejected_pairs = 0;
    std::uint64_t narrow_plane_phase_hard_rejected_pairs = 0;
    std::uint64_t narrow_plane_phase_fail_open_pairs = 0;
    std::uint64_t narrow_plane_phase_reject_reason_plane_angle_total = 0;
    std::uint64_t narrow_plane_phase_reject_reason_phase_angle_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_reason_elements_invalid_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_reason_eccentricity_guard_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_reason_non_finite_state_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_reason_uncertainty_override_total = 0;
    std::uint64_t narrow_moid_evaluated_pairs = 0;
    std::uint64_t narrow_moid_shadow_rejected_pairs = 0;
    std::uint64_t narrow_moid_hard_rejected_pairs = 0;
    std::uint64_t narrow_moid_fail_open_pairs = 0;
    std::uint64_t narrow_moid_reject_reason_distance_threshold_total = 0;
    std::uint64_t narrow_moid_fail_open_reason_elements_invalid_total = 0;
    std::uint64_t narrow_moid_fail_open_reason_eccentricity_guard_total = 0;
    std::uint64_t narrow_moid_fail_open_reason_non_finite_state_total = 0;
    std::uint64_t narrow_moid_fail_open_reason_sampling_failure_total = 0;
    std::uint64_t narrow_moid_fail_open_reason_hf_placeholder_total = 0;
    std::uint64_t narrow_moid_fail_open_reason_uncertainty_override_total = 0;
    std::uint64_t narrow_refine_fail_open_reason_rk4_failure_total = 0;
    std::uint64_t narrow_full_refine_fail_open_reason_rk4_failure_total = 0;
    std::uint64_t narrow_full_refine_fail_open_reason_budget_exhausted_total = 0;
    std::uint64_t narrow_fail_open_allpairs = 0;
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
