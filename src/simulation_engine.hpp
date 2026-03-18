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
    std::vector<std::uint32_t> collision_sat_indices;

    // Broad-phase (conservative shell overlap)
    std::uint64_t broad_pairs_considered = 0;
    std::uint64_t broad_candidates = 0;
    std::uint64_t broad_shell_overlap_pass = 0;
    std::uint64_t broad_dcriterion_rejected = 0;
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
};

// Runs one simulation step with adaptive propagation.
// Returns true when clock was initialized and step executed.
bool run_simulation_step(StateStore& store,
                         SimClock& clock,
                         double step_seconds,
                         StepRunStats& out,
                         const StepRunConfig& cfg = StepRunConfig{}) noexcept;

} // namespace cascade
