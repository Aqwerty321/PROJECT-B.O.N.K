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

Vec3 compute_slot_target_recovery_dv(const StateStore& store,
                                     std::size_t sat_idx,
                                     const RecoveryRequest& req,
                                     std::unordered_map<std::string, SlotReference>& slot_reference_by_sat) noexcept;

RecoveryPlanStats plan_recovery_burns(StateStore& store,
                                      double current_epoch_s,
                                      std::uint64_t tick_id,
                                      std::vector<ScheduledBurn>& burn_queue,
                                      std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                      std::unordered_map<std::string, RecoveryRequest>& recovery_requests_by_sat,
                                      std::unordered_map<std::string, bool>& graveyard_requested_by_sat,
                                      std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                                      double auto_upload_horizon_s) noexcept;

} // namespace cascade
