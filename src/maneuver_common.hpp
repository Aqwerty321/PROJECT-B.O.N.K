// ---------------------------------------------------------------------------
// maneuver_common.hpp — shared maneuver/ops helpers
// ---------------------------------------------------------------------------
#pragma once

#include "state_store.hpp"
#include "types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cascade {

struct ScheduledBurn {
    std::string id;
    std::string satellite_id;
    std::string upload_station_id;
    double upload_epoch_s = 0.0;
    double burn_epoch_s = 0.0;
    Vec3 delta_v_km_s{};
    double delta_v_norm_km_s = 0.0;
    bool auto_generated = false;
    bool recovery_burn = false;
    bool graveyard_burn = false;
};

struct ManeuverExecStats {
    std::uint64_t executed = 0;
    std::uint64_t recovery_pending_marked = 0;
    std::uint64_t recovery_completed = 0;
    std::uint64_t graveyard_completed = 0;
    std::uint64_t upload_missed = 0;
};

struct RecoveryPlanStats {
    std::uint64_t planned = 0;
    std::uint64_t deferred = 0;
};

struct GraveyardPlanStats {
    std::uint64_t planned = 0;
    std::uint64_t deferred = 0;
};

struct RecoveryRequest {
    Vec3 remaining_delta_v_km_s{};
    double earliest_epoch_s = 0.0;
};

struct SlotReference {
    OrbitalElements elements{};
    double reference_epoch_s = 0.0;
    bool bootstrapped_from_telemetry = false; // true if set at first telemetry ingestion
};

struct GroundStation {
    std::string id;
    double lat_deg;
    double lon_deg;
    double alt_km;
    double min_el_deg;
};

std::size_t active_ground_station_count() noexcept;
bool active_ground_station_has_id(std::string_view station_id) noexcept;
std::string active_ground_station_source();

inline constexpr double SIGNAL_LATENCY_S = 10.0;
inline constexpr double STATIONKEEPING_BOX_RADIUS_KM = 10.0;
inline constexpr double UPLOAD_SCAN_STEP_S = 20.0;
inline constexpr double AUTO_UPLOAD_HORIZON_S = 1800.0;
inline constexpr double GRAVEYARD_TARGET_DV_KM_S = 0.003;

bool get_current_elements(const StateStore& store,
                          std::size_t idx,
                          OrbitalElements& out) noexcept;

OrbitalElements derive_slot_elements_if_needed(const StateStore& store,
                                                std::size_t sat_idx,
                                                std::unordered_map<std::string, SlotReference>& slot_reference_by_sat) noexcept;

bool slot_elements_at_epoch(const std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                            const std::string& sat_id,
                            double epoch_s,
                            OrbitalElements& out) noexcept;

bool slot_radius_error_km_at_epoch(const StateStore& store,
                                   std::size_t sat_idx,
                                   double epoch_s,
                                   const std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                                   double& out_error_km) noexcept;

double slot_error_score(const OrbitalElements& slot,
                        const OrbitalElements& cur) noexcept;

double dv_norm_km_s(const Vec3& dv) noexcept;

double propellant_used_kg(double mass_kg,
                          double delta_v_km_s) noexcept;

bool has_ground_station_los(const Vec3& sat_eci_km,
                            double epoch_s) noexcept;

bool predict_satellite_eci_at_epoch(const StateStore& store,
                                    std::size_t sat_idx,
                                    double epoch_s,
                                    Vec3& out_sat_eci_km) noexcept;

bool has_ground_station_los_for_sat_epoch(const StateStore& store,
                                          std::size_t sat_idx,
                                          double epoch_s,
                                          std::string* station_id_out = nullptr) noexcept;

bool find_latest_upload_slot_epoch(const StateStore& store,
                                   std::size_t sat_idx,
                                   double now_epoch_s,
                                   double burn_epoch_s,
                                   double& out_upload_epoch_s,
                                   std::string& out_station_id) noexcept;

bool find_earliest_upload_slot_epoch(const StateStore& store,
                                     std::size_t sat_idx,
                                     double start_epoch_s,
                                     double end_epoch_s,
                                     double& out_upload_epoch_s,
                                     std::string& out_station_id) noexcept;

bool compute_upload_plan_for_burn(const StateStore& store,
                                  std::size_t sat_idx,
                                  double now_epoch_s,
                                  double burn_epoch_s,
                                  double& out_upload_epoch_s,
                                  std::string& out_station_id) noexcept;

bool choose_burn_epoch_with_upload(const StateStore& store,
                                   const std::vector<ScheduledBurn>& burn_queue,
                                   const std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                   std::size_t sat_idx,
                                   double now_epoch_s,
                                   double earliest_burn_epoch_s,
                                   double latest_burn_epoch_s,
                                   double& out_burn_epoch_s,
                                   double& out_upload_epoch_s,
                                   std::string& out_station_id) noexcept;

bool upload_window_ready_for_execution(const ScheduledBurn& burn,
                                       double current_epoch_s) noexcept;

bool has_pending_burn_in_cooldown_window(const std::vector<ScheduledBurn>& burn_queue,
                                         const std::string& sat_id,
                                         double epoch_s) noexcept;

bool has_any_pending_burn(const std::vector<ScheduledBurn>& burn_queue,
                          const std::string& sat_id) noexcept;

bool should_request_graveyard(const StateStore& store,
                              std::size_t sat_idx,
                              const std::unordered_map<std::string, bool>& graveyard_completed_by_sat) noexcept;

GraveyardPlanStats plan_graveyard_burns(StateStore& store,
                                        double current_epoch_s,
                                        std::uint64_t tick_id,
                                        std::vector<ScheduledBurn>& burn_queue,
                                        std::unordered_map<std::string, bool>& graveyard_requested_by_sat,
                                        const std::unordered_map<std::string, bool>& graveyard_completed_by_sat);

void accumulate_recovery_request(const ScheduledBurn& burn,
                                 double current_epoch_s,
                                 std::unordered_map<std::string, RecoveryRequest>& recovery_requests_by_sat) noexcept;

ManeuverExecStats execute_due_maneuvers(StateStore& store,
                                        double current_epoch_s,
                                        std::vector<ScheduledBurn>& burn_queue,
                                        std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                        std::unordered_map<std::string, RecoveryRequest>& recovery_requests_by_sat,
                                        std::unordered_map<std::string, bool>& graveyard_requested_by_sat,
                                        std::unordered_map<std::string, bool>& graveyard_completed_by_sat);

void validate_pending_upload_windows(StateStore& store,
                                     double current_epoch_s,
                                     std::vector<ScheduledBurn>& burn_queue,
                                     std::uint64_t& upload_missed);

} // namespace cascade
