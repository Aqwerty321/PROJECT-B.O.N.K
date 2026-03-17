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
    double target_epoch_s = 0.0;
};

// Runs one simulation step with adaptive propagation.
// Returns true when clock was initialized and step executed.
bool run_simulation_step(StateStore& store,
                         SimClock& clock,
                         double step_seconds,
                         StepRunStats& out) noexcept;

} // namespace cascade
