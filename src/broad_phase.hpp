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

    // Orbital band indexing (conservative settings)
    double a_bin_width_km = 500.0;
    double i_bin_width_rad = 0.3490658503988659; // 20 deg
    int band_neighbor_bins = 2;
    bool enable_i_neighbor_filter = false;

    // Objects above this eccentricity are routed to fail-open path.
    double high_e_fail_open = 0.2;

    // Conservative D-criterion gate (only used when both elements are valid
    // and below high_e_fail_open). Disabled by default in runtime until
    // narrow-phase integration is complete.
    bool enable_dcriterion = false;
    bool shadow_dcriterion = true;
    double dcriterion_threshold = 2.0;
};

struct BroadPhasePair {
    std::uint32_t sat_idx = 0;
    std::uint32_t obj_idx = 0;
};

struct BroadPhaseResult {
    std::vector<BroadPhasePair> candidates;
    std::uint64_t pairs_considered = 0;
    std::uint64_t pairs_after_band_index = 0;
    std::uint64_t shell_overlap_pass = 0;
    std::uint64_t dcriterion_rejected = 0;
    std::uint64_t dcriterion_shadow_rejected = 0;
    std::uint64_t fail_open_objects = 0;
    std::uint64_t fail_open_satellites = 0;
    double shell_margin_km = 0.0;
};

BroadPhaseResult generate_broad_phase_candidates(const StateStore& store,
                                                 const BroadPhaseConfig& cfg = BroadPhaseConfig{});

} // namespace cascade
