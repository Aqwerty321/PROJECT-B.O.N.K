// ---------------------------------------------------------------------------
// simulation_engine.hpp — phase 3 simulation tick engine
// ---------------------------------------------------------------------------
#pragma once

#include "state_store.hpp"
#include "sim_clock.hpp"

#include <cstdint>

namespace cascade {

struct StepRunStats {
    std::uint64_t propagated_objects = 0;
    std::uint64_t failed_objects = 0;
    std::uint64_t used_fast = 0;
    std::uint64_t used_rk4 = 0;
    std::uint64_t escalated_after_probe = 0;

    // Broad-phase (conservative shell overlap)
    std::uint64_t broad_pairs_considered = 0;
    std::uint64_t broad_candidates = 0;
    std::uint64_t broad_shell_overlap_pass = 0;
    std::uint64_t broad_dcriterion_rejected = 0;
    std::uint64_t broad_fail_open_objects = 0;
    std::uint64_t broad_fail_open_satellites = 0;
    double broad_shell_margin_km = 0.0;

    double target_epoch_s = 0.0;
};

// Runs one simulation step with adaptive propagation.
// Returns true when clock was initialized and step executed.
bool run_simulation_step(StateStore& store,
                         SimClock& clock,
                         double step_seconds,
                         StepRunStats& out) noexcept;

} // namespace cascade
