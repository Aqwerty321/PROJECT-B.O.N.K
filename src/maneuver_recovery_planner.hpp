// ---------------------------------------------------------------------------
// maneuver_recovery_planner.hpp — slot-targeted recovery planner helper
// ---------------------------------------------------------------------------
#pragma once

#include "maneuver_common.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cascade {

enum class RecoverySolverMode : std::uint8_t {
    HEURISTIC = 0,
    CW_ZEM_EQUIVALENT = 1,
};

struct RecoveryPlannerConfig {
    // Slot-targeted proportional correction gains.
    double scale_t = 6e-5;
    double scale_r = 2e-3;
    double radial_share = 0.5;
    double scale_n = 6e-3;

    // If computed slot correction is too small, use remaining dv request.
    double fallback_norm_km_s = 1e-4;

    // Prevent over-correction beyond remaining requested recovery budget.
    // 1.0 means do not command more norm than remaining request.
    double max_request_ratio = 0.05;

    // Recovery DV solver selection. CW solver is default — validated in P0.
    RecoverySolverMode solver_mode = RecoverySolverMode::CW_ZEM_EQUIVALENT;
};

RecoveryPlannerConfig recovery_planner_config_from_env();

Vec3 compute_slot_target_recovery_dv(const StateStore& store,
                                     std::size_t sat_idx,
                                     const RecoveryRequest& req,
                                     std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                                     const RecoveryPlannerConfig& cfg = RecoveryPlannerConfig{}) noexcept;

RecoveryPlanStats plan_recovery_burns(StateStore& store,
                                      double current_epoch_s,
                                       std::uint64_t tick_id,
                                       std::vector<ScheduledBurn>& burn_queue,
                                       std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                      std::unordered_map<std::string, RecoveryRequest>& recovery_requests_by_sat,
                                      std::unordered_map<std::string, bool>& graveyard_requested_by_sat,
                                      std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                                      double auto_upload_horizon_s,
                                      const RecoveryPlannerConfig& cfg = RecoveryPlannerConfig{}) noexcept;

} // namespace cascade
