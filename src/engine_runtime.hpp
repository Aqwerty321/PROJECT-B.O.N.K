// ---------------------------------------------------------------------------
// engine_runtime.hpp — runtime boundary for CASCADE command execution
// ---------------------------------------------------------------------------
#pragma once

#include "maneuver_common.hpp"
#include "maneuver_recovery_planner.hpp"
#include "sim_clock.hpp"
#include "simulation_engine.hpp"
#include "state_store.hpp"
#include "telemetry.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cascade {

struct TelemetryCommandResult {
    bool ok = false;
    int http_status = 200;
    std::string error_code;
    std::string error_message;
    int processed_count = 0;
    std::uint64_t active_cdm_warnings = 0;
};

struct ScheduleCommandResult {
    bool ok = false;
    int http_status = 202;
    std::string error_code;
    std::string error_message;
    double projected_mass_remaining_kg = 0.0;
};

struct StepCommandResult {
    bool ok = false;
    int http_status = 200;
    std::string error_code;
    std::string error_message;
    std::string new_timestamp;
    std::uint64_t collisions_detected = 0;
    std::uint64_t maneuvers_executed = 0;
};

struct RuntimeHttpPolicy {
    int schedule_success_status = 202;
    std::int64_t max_step_seconds = 86400;
};

struct PropagationStats {
    std::uint64_t fast_last_tick = 0;
    std::uint64_t rk4_last_tick = 0;
    std::uint64_t escalated_last_tick = 0;
    std::uint64_t narrow_pairs_last_tick = 0;
    std::uint64_t collisions_last_tick = 0;
    std::uint64_t maneuvers_last_tick = 0;
    std::uint64_t narrow_refined_pairs_last_tick = 0;
    std::uint64_t narrow_refine_cleared_last_tick = 0;
    std::uint64_t narrow_refine_fail_open_last_tick = 0;
    std::uint64_t narrow_full_refined_pairs_last_tick = 0;
    std::uint64_t narrow_full_refine_cleared_last_tick = 0;
    std::uint64_t narrow_full_refine_fail_open_last_tick = 0;
    std::uint64_t narrow_full_refine_budget_allocated_last_tick = 0;
    std::uint64_t narrow_full_refine_budget_exhausted_last_tick = 0;
    std::uint64_t narrow_uncertainty_promoted_pairs_last_tick = 0;
    std::uint64_t narrow_plane_phase_evaluated_pairs_last_tick = 0;
    std::uint64_t narrow_plane_phase_shadow_rejected_pairs_last_tick = 0;
    std::uint64_t narrow_plane_phase_hard_rejected_pairs_last_tick = 0;
    std::uint64_t narrow_plane_phase_fail_open_pairs_last_tick = 0;
    std::uint64_t auto_planned_last_tick = 0;
    std::uint64_t recovery_pending_marked_last_tick = 0;
    std::uint64_t recovery_planned_last_tick = 0;
    std::uint64_t recovery_deferred_last_tick = 0;
    std::uint64_t recovery_completed_last_tick = 0;
    std::uint64_t graveyard_planned_last_tick = 0;
    std::uint64_t graveyard_deferred_last_tick = 0;
    std::uint64_t graveyard_completed_last_tick = 0;
    std::uint64_t upload_missed_last_tick = 0;
    std::uint64_t stationkeeping_outside_box_last_tick = 0;
    double stationkeeping_uptime_penalty_mean_last_tick = 0.0;
    double stationkeeping_slot_radius_error_mean_km_last_tick = 0.0;
    double stationkeeping_slot_radius_error_max_km_last_tick = 0.0;
    double recovery_slot_error_mean_last_tick = 0.0;
    double recovery_slot_error_max_last_tick = 0.0;

    std::uint64_t broad_pairs_last_tick = 0;
    std::uint64_t broad_candidates_last_tick = 0;
    std::uint64_t broad_overlap_pass_last_tick = 0;
    std::uint64_t broad_dcriterion_rejected_last_tick = 0;
    std::uint64_t broad_dcriterion_shadow_rejected_last_tick = 0;
    std::uint64_t broad_fail_open_objects_last_tick = 0;
    std::uint64_t broad_fail_open_satellites_last_tick = 0;
    double broad_shell_margin_km_last_tick = 0.0;
    bool broad_dcriterion_enabled_last_tick = false;
    double broad_a_bin_width_km_last_tick = 0.0;
    int broad_band_neighbor_bins_last_tick = 0;

    std::uint64_t fast_total = 0;
    std::uint64_t rk4_total = 0;
    std::uint64_t escalated_total = 0;
    std::uint64_t narrow_pairs_total = 0;
    std::uint64_t collisions_total = 0;
    std::uint64_t maneuvers_total = 0;
    std::uint64_t narrow_refined_pairs_total = 0;
    std::uint64_t narrow_refine_cleared_total = 0;
    std::uint64_t narrow_refine_fail_open_total = 0;
    std::uint64_t narrow_full_refined_pairs_total = 0;
    std::uint64_t narrow_full_refine_cleared_total = 0;
    std::uint64_t narrow_full_refine_fail_open_total = 0;
    std::uint64_t narrow_full_refine_budget_allocated_total = 0;
    std::uint64_t narrow_full_refine_budget_exhausted_total = 0;
    std::uint64_t narrow_uncertainty_promoted_pairs_total = 0;
    std::uint64_t narrow_plane_phase_evaluated_pairs_total = 0;
    std::uint64_t narrow_plane_phase_shadow_rejected_pairs_total = 0;
    std::uint64_t narrow_plane_phase_hard_rejected_pairs_total = 0;
    std::uint64_t narrow_plane_phase_fail_open_pairs_total = 0;
    std::uint64_t auto_planned_total = 0;
    std::uint64_t recovery_pending_marked_total = 0;
    std::uint64_t recovery_planned_total = 0;
    std::uint64_t recovery_deferred_total = 0;
    std::uint64_t recovery_completed_total = 0;
    std::uint64_t graveyard_planned_total = 0;
    std::uint64_t graveyard_deferred_total = 0;
    std::uint64_t graveyard_completed_total = 0;
    std::uint64_t upload_missed_total = 0;
    std::uint64_t stationkeeping_outside_box_total = 0;
    double stationkeeping_uptime_penalty_sum_total = 0.0;
    std::uint64_t stationkeeping_uptime_penalty_samples_total = 0;
    double stationkeeping_slot_radius_error_sum_total = 0.0;
    std::uint64_t stationkeeping_slot_radius_error_samples_total = 0;
    double stationkeeping_slot_radius_error_max_total = 0.0;
    double recovery_slot_error_sum_total = 0.0;
    std::uint64_t recovery_slot_error_samples_total = 0;
    double recovery_slot_error_max_total = 0.0;

    std::uint64_t broad_pairs_total = 0;
    std::uint64_t broad_candidates_total = 0;
    std::uint64_t broad_overlap_pass_total = 0;
    std::uint64_t broad_dcriterion_rejected_total = 0;
    std::uint64_t broad_dcriterion_shadow_rejected_total = 0;
    std::uint64_t broad_fail_open_objects_total = 0;
    std::uint64_t broad_fail_open_satellites_total = 0;
};

class EngineRuntime {
public:
    EngineRuntime();
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    TelemetryCommandResult ingest_telemetry(const TelemetryParseResult& parsed,
                                            std::string_view source_id);

    ScheduleCommandResult schedule_maneuver(std::string_view satellite_id,
                                            std::vector<ScheduledBurn> burns);

    StepCommandResult simulate_step(std::int64_t step_seconds);

    std::string snapshot_json() const;
    std::string status_json(bool include_details) const;
    std::string conflicts_json() const;
    std::string propagation_json() const;

private:
    struct TelemetryCommand {
        TelemetryParseResult parsed;
        std::string source_id;
        std::chrono::steady_clock::time_point enqueued_at{};
        std::promise<TelemetryCommandResult> completion;
    };

    struct ScheduleCommand {
        std::string satellite_id;
        std::vector<ScheduledBurn> burns;
        std::chrono::steady_clock::time_point enqueued_at{};
        std::promise<ScheduleCommandResult> completion;
    };

    struct StepCommand {
        std::int64_t step_seconds = 0;
        std::chrono::steady_clock::time_point enqueued_at{};
        std::promise<StepCommandResult> completion;
    };

    struct CommandLatencyAtomicStats {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> queue_wait_us_total{0};
        std::atomic<std::uint64_t> queue_wait_us_max{0};
        std::atomic<std::uint64_t> queue_wait_us_last{0};
        std::atomic<std::uint64_t> execution_us_total{0};
        std::atomic<std::uint64_t> execution_us_max{0};
        std::atomic<std::uint64_t> execution_us_last{0};
    };

    using RuntimeCommand = std::variant<TelemetryCommand, ScheduleCommand, StepCommand>;

    void worker_loop();

    TelemetryCommandResult execute_ingest_telemetry(const TelemetryParseResult& parsed,
                                                    std::string_view source_id);

    ScheduleCommandResult execute_schedule_maneuver(std::string_view satellite_id,
                                                    std::vector<ScheduledBurn> burns);

    StepCommandResult execute_simulate_step(std::int64_t step_seconds);

    void observe_queue_depth(std::size_t depth) noexcept;
    void observe_latency_max(std::atomic<std::uint64_t>& metric,
                             std::uint64_t value) noexcept;
    void record_command_latency(CommandLatencyAtomicStats& stats,
                                std::uint64_t queue_wait_us,
                                std::uint64_t execution_us) noexcept;
    void publish_read_views();

    std::uint64_t enforce_stationkeeping_recovery(double epoch_s,
                                                  std::uint64_t tick_id,
                                                  double& slot_error_sum_tick,
                                                  double& slot_error_max_tick,
                                                  std::uint64_t& slot_error_samples_tick,
                                                  double& slot_radius_err_sum_tick,
                                                  double& slot_radius_err_max_tick,
                                                  std::uint64_t& slot_radius_err_samples_tick,
                                                  std::uint64_t& stationkeeping_outside_box_tick,
                                                  double& stationkeeping_uptime_penalty_sum_tick,
                                                  std::uint64_t& stationkeeping_uptime_penalty_samples_tick);

    struct PublishedReadViews {
        std::string snapshot_json;
        std::string conflicts_json;
        std::string propagation_json;
    };

    mutable std::shared_mutex mutex_;
    std::mutex command_queue_mutex_;
    std::condition_variable command_queue_cv_;
    std::deque<RuntimeCommand> command_queue_;
    bool stop_worker_ = false;
    std::thread worker_;

    std::atomic<std::uint64_t> queue_depth_current_{0};
    std::atomic<std::uint64_t> queue_depth_max_{0};
    std::atomic<std::uint64_t> queue_enqueued_total_{0};
    std::atomic<std::uint64_t> queue_completed_total_{0};
    std::atomic<std::uint64_t> queue_rejected_total_{0};
    std::atomic<std::uint64_t> queue_timeout_total_{0};
    CommandLatencyAtomicStats telemetry_latency_{};
    CommandLatencyAtomicStats schedule_latency_{};
    CommandLatencyAtomicStats step_latency_{};
    std::uint64_t max_queue_depth_ = 1024;

    std::shared_ptr<const PublishedReadViews> published_views_;

    StateStore store_;
    SimClock clock_;
    std::atomic<std::int64_t> tick_count_{0};
    StepRunConfig step_cfg_{};
    RecoveryPlannerConfig recovery_cfg_{};
    RuntimeHttpPolicy http_policy_{};
    PropagationStats prop_stats_{};

    std::vector<ScheduledBurn> burn_queue_;
    std::unordered_map<std::string, double> last_burn_epoch_by_sat_;
    std::unordered_map<std::string, RecoveryRequest> recovery_requests_by_sat_;
    std::unordered_map<std::string, SlotReference> slot_reference_by_sat_;
    std::unordered_map<std::string, bool> graveyard_completed_by_sat_;
    std::unordered_map<std::string, bool> graveyard_requested_by_sat_;
};

} // namespace cascade
