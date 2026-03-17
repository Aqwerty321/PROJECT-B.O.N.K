// ---------------------------------------------------------------------------
// broad_phase.hpp — conservative broad-phase candidate generation
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <vector>

namespace cascade {

class StateStore;

struct BroadPhaseConfig {
    // Conservative expansion around [rp, ra] shells. Larger values reduce
    // false-negative risk at the cost of extra narrow-phase work.
    double shell_margin_km = 50.0;

    // Used when orbital elements are invalid and only instantaneous radius is
    // available. This intentionally over-approximates to protect against
    // false negatives.
    double invalid_shell_pad_km = 200.0;
};

struct BroadPhasePair {
    std::uint32_t sat_idx = 0;
    std::uint32_t obj_idx = 0;
};

struct BroadPhaseResult {
    std::vector<BroadPhasePair> candidates;
    std::uint64_t pairs_considered = 0;
    std::uint64_t shell_overlap_pass = 0;
    double shell_margin_km = 0.0;
};

BroadPhaseResult generate_broad_phase_candidates(const StateStore& store,
                                                 const BroadPhaseConfig& cfg = BroadPhaseConfig{});

} // namespace cascade
