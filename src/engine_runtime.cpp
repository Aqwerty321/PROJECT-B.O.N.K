// ---------------------------------------------------------------------------
// engine_runtime.cpp — runtime boundary for CASCADE command execution
// ---------------------------------------------------------------------------

#include "engine_runtime.hpp"

#include "broad_phase.hpp"
#include "earth_frame.hpp"
#include "env_util.hpp"
#include "json_util.hpp"
#include "maneuver_recovery_planner.hpp"
#include "orbit_math.hpp"
#include "propagator.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cascade {

using env_util::env_double;

namespace {

// Legacy constexpr aliases removed; runtime code uses ops_*() functions directly.
constexpr std::chrono::seconds k_command_timeout{15};

std::uint64_t env_u64(std::string_view key,
                      std::uint64_t default_value,
                      std::uint64_t min_value,
                      std::uint64_t max_value) noexcept
{
    const char* raw = std::getenv(std::string(key).c_str());
    if (raw == nullptr) {
        return default_value;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(raw, &end, 10);
    if (end == nullptr || *end != '\0') {
        return default_value;
    }

    if (parsed < min_value || parsed > max_value) {
        return default_value;
    }

    return static_cast<std::uint64_t>(parsed);
}

std::int64_t env_i64(std::string_view key,
                     std::int64_t default_value,
                     std::int64_t min_value,
                     std::int64_t max_value) noexcept
{
    const char* raw = std::getenv(std::string(key).c_str());
    if (raw == nullptr) {
        return default_value;
    }

    char* end = nullptr;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (end == nullptr || *end != '\0') {
        return default_value;
    }

    if (parsed < min_value || parsed > max_value) {
        return default_value;
    }

    return static_cast<std::int64_t>(parsed);
}

int env_int(std::string_view key,
            int default_value,
            int min_value,
            int max_value) noexcept
{
    return static_cast<int>(
        env_i64(key,
                static_cast<std::int64_t>(default_value),
                static_cast<std::int64_t>(min_value),
                static_cast<std::int64_t>(max_value))
    );
}

// CDM scanner thresholds — env-overridable once on first call.
struct CdmParams {
    double horizon_s;
    double substep_s;
    double rk4_max_step_s;
};

static const CdmParams& cdm_params() noexcept
{
    static const CdmParams p{
        env_double("PROJECTBONK_CDM_HORIZON_S", 86400.0, 3600.0, 604800.0),
        env_double("PROJECTBONK_CDM_SUBSTEP_S", 600.0, 10.0, 7200.0),
        env_double("PROJECTBONK_CDM_RK4_MAX_STEP_S", 30.0, 1.0, 300.0),
    };
    return p;
}

// COLA auto-impulse magnitude — env-overridable once on first call.
static double cola_auto_dv_km_s() noexcept
{
    static const double v = env_double("PROJECTBONK_AUTO_DV_KM_S", 0.001, 1e-6, 0.01);
    return v;
}

struct CdmWarningScanResult {
    std::uint64_t warning_count = 0;
    std::vector<ConjunctionRecord> records;
};

bool conjunction_less(const ConjunctionRecord& a, const ConjunctionRecord& b) noexcept
{
    if (a.collision != b.collision) return a.collision > b.collision;
    if (a.fail_open != b.fail_open) return a.fail_open > b.fail_open;
    if (std::abs(a.miss_distance_km - b.miss_distance_km) > 1.0e-9) {
        return a.miss_distance_km < b.miss_distance_km;
    }
    if (std::abs(a.tca_epoch_s - b.tca_epoch_s) > 1.0e-6) {
        return a.tca_epoch_s < b.tca_epoch_s;
    }
    if (a.satellite_id != b.satellite_id) return a.satellite_id < b.satellite_id;
    return a.debris_id < b.debris_id;
}

void trim_sorted_conjunction_records(std::vector<ConjunctionRecord>& records,
                                     std::size_t limit)
{
    std::sort(records.begin(), records.end(), conjunction_less);
    if (records.size() > limit) {
        records.resize(limit);
    }
}

void append_conjunction_record_limited(std::vector<ConjunctionRecord>& records,
                                       ConjunctionRecord record,
                                       std::size_t limit)
{
    records.push_back(std::move(record));
    trim_sorted_conjunction_records(records, limit);
}

inline double vector_norm2(const Vec3& v) noexcept
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

template <typename BurnT>
bool is_avoidance_burn(const BurnT& burn) noexcept
{
    return !burn.recovery_burn && !burn.graveyard_burn;
}

double burn_fuel_consumed_kg(const ExecutedBurn& burn) noexcept
{
    return std::max(0.0, burn.fuel_before_kg - burn.fuel_after_kg);
}

struct PairMinSeparationScan {
    bool fail_open = false;
    double min_d2 = 0.0;
    double min_epoch_s = 0.0;
    Vec3 min_sat{};
    Vec3 min_deb{};
    Vec3 min_rel_v{};
};

PairMinSeparationScan scan_pair_minimum_separation(Vec3 sat_pos,
                                                   Vec3 sat_vel,
                                                   Vec3 deb_pos,
                                                   Vec3 deb_vel,
                                                   double base_epoch_s,
                                                   double horizon_s,
                                                   double substep_s,
                                                   double max_step_s) noexcept
{
    PairMinSeparationScan out;
    out.min_d2 = vector_norm2(Vec3{
        sat_pos.x - deb_pos.x,
        sat_pos.y - deb_pos.y,
        sat_pos.z - deb_pos.z,
    });
    out.min_epoch_s = base_epoch_s;
    out.min_sat = sat_pos;
    out.min_deb = deb_pos;
    out.min_rel_v = Vec3{
        sat_vel.x - deb_vel.x,
        sat_vel.y - deb_vel.y,
        sat_vel.z - deb_vel.z,
    };

    double elapsed_s = 0.0;
    double remaining_s = std::max(0.0, horizon_s);
    while (remaining_s > EPS_NUM) {
        const double dt = std::min(substep_s, remaining_s);
        remaining_s -= dt;

        const bool ok_sat = propagate_rk4_j2_substep(sat_pos, sat_vel, dt, max_step_s);
        const bool ok_deb = propagate_rk4_j2_substep(deb_pos, deb_vel, dt, max_step_s);
        if (!ok_sat || !ok_deb) {
            out.fail_open = true;
            break;
        }

        elapsed_s += dt;
        const double dx = sat_pos.x - deb_pos.x;
        const double dy = sat_pos.y - deb_pos.y;
        const double dz = sat_pos.z - deb_pos.z;
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < out.min_d2) {
            out.min_d2 = d2;
            out.min_epoch_s = base_epoch_s + elapsed_s;
            out.min_sat = sat_pos;
            out.min_deb = deb_pos;
            out.min_rel_v = Vec3{
                sat_vel.x - deb_vel.x,
                sat_vel.y - deb_vel.y,
                sat_vel.z - deb_vel.z,
            };
        }
    }

    return out;
}

const ConjunctionRecord* best_predictive_record_for_satellite(
    const std::vector<ConjunctionRecord>& records,
    std::string_view satellite_id) noexcept
{
    for (const auto& record : records) {
        if (record.satellite_id == satellite_id) {
            return &record;
        }
    }
    return nullptr;
}

struct MitigationEvaluationResult {
    bool tracked = false;
    bool evaluated = false;
    bool collision_avoided = false;
    double eval_epoch_s = 0.0;
    double miss_distance_km = 0.0;
    bool fail_open = false;
};

MitigationEvaluationResult evaluate_predictive_mitigation(const StateStore& store,
                                                          const ScheduledBurn& burn,
                                                          double current_epoch_s) noexcept
{
    MitigationEvaluationResult out;
    out.tracked = burn.scheduled_from_predictive_cdm && !burn.trigger_debris_id.empty();
    if (!out.tracked) {
        return out;
    }

    if (burn.trigger_fail_open || burn.trigger_tca_epoch_s <= current_epoch_s + EPS_NUM) {
        return out;
    }

    const std::size_t sat_idx = store.find(burn.satellite_id);
    const std::size_t deb_idx = store.find(burn.trigger_debris_id);
    if (sat_idx >= store.size() || deb_idx >= store.size()) {
        return out;
    }
    if (store.type(sat_idx) != ObjectType::SATELLITE || store.type(deb_idx) != ObjectType::DEBRIS) {
        return out;
    }

    const double horizon_s = std::min(
        cdm_params().horizon_s,
        std::max(cdm_params().substep_s, burn.trigger_tca_epoch_s - current_epoch_s + 3600.0)
    );
    Vec3 sat_pos{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
    Vec3 sat_vel{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};
    Vec3 deb_pos{store.rx(deb_idx), store.ry(deb_idx), store.rz(deb_idx)};
    Vec3 deb_vel{store.vx(deb_idx), store.vy(deb_idx), store.vz(deb_idx)};

    const PairMinSeparationScan scan = scan_pair_minimum_separation(
        sat_pos,
        sat_vel,
        deb_pos,
        deb_vel,
        current_epoch_s,
        horizon_s,
        cdm_params().substep_s,
        cdm_params().rk4_max_step_s
    );

    out.fail_open = scan.fail_open;
    out.eval_epoch_s = scan.min_epoch_s;
    out.miss_distance_km = scan.fail_open ? 0.0 : std::sqrt(std::max(0.0, scan.min_d2));
    out.evaluated = !scan.fail_open;
    out.collision_avoided = out.evaluated
        && burn.trigger_miss_distance_km <= COLLISION_THRESHOLD_KM + EPS_NUM
        && out.miss_distance_km > COLLISION_THRESHOLD_KM + EPS_NUM;
    return out;
}

std::uint64_t plan_collision_avoidance_burns(
    StateStore& store,
    const StepRunStats& step_stats,
    const std::vector<ConjunctionRecord>& predictive_records,
    double epoch_s,
    std::uint64_t tick_id,
    std::vector<ScheduledBurn>& burn_queue,
    std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
    std::unordered_map<std::string, bool>& graveyard_completed_by_sat,
    std::unordered_map<std::string, bool>& graveyard_requested_by_sat
)
{
    std::uint64_t planned = 0;

    for (std::uint32_t sat_idx_u32 : step_stats.collision_sat_indices) {
        const std::size_t sat_idx = static_cast<std::size_t>(sat_idx_u32);
        if (sat_idx >= store.size()) continue;
        if (store.type(sat_idx) != ObjectType::SATELLITE) continue;
        if (store.sat_status(sat_idx) == SatStatus::OFFLINE) continue;

        const auto& sat_id = store.id(sat_idx);
        if (graveyard_requested_by_sat[sat_id]) continue;
        auto grave_done_it = graveyard_completed_by_sat.find(sat_id);
        if (grave_done_it != graveyard_completed_by_sat.end() && grave_done_it->second) {
            continue;
        }

        const auto last_it = last_burn_epoch_by_sat.find(sat_id);
        if (last_it != last_burn_epoch_by_sat.end()) {
            const double dt = epoch_s - last_it->second;
            if (dt + EPS_NUM < SAT_COOLDOWN_S) {
                continue;
            }
        }

        if (cascade::has_pending_burn_in_cooldown_window(burn_queue, sat_id, epoch_s)) {
            continue;
        }

        const Vec3 r{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
        const Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};
        const double r_norm = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
        const double v_norm = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (v_norm < EPS_NUM || r_norm < EPS_NUM) {
            continue;
        }

        // Compute RTN frame unit vectors:
        //   T-hat = velocity direction (tangential / prograde)
        //   N-hat = h / |h| where h = r × v  (orbit-normal)
        //   R-hat = r / |r|  (radial, not directly used for prograde burn)
        // For auto-COLA, apply 1 m/s impulse along T-hat (prograde).
        const Vec3 t_hat{v.x / v_norm, v.y / v_norm, v.z / v_norm};

        // Cross product h = r × v for orbit-normal validation
        const double hx = r.y * v.z - r.z * v.y;
        const double hy = r.z * v.x - r.x * v.z;
        const double hz = r.x * v.y - r.y * v.x;
        const double h_norm = std::sqrt(hx * hx + hy * hy + hz * hz);
        if (h_norm < EPS_NUM) {
            // Degenerate orbit (r and v are parallel) — skip
            continue;
        }

        const double k_auto_dv_km_s = cola_auto_dv_km_s();
        // Prograde (T-hat) burn in RTN frame
        const Vec3 dv{
            t_hat.x * k_auto_dv_km_s,
            t_hat.y * k_auto_dv_km_s,
            t_hat.z * k_auto_dv_km_s
        };

        const double mass_before = store.mass_kg(sat_idx);
        const double fuel_before = store.fuel_kg(sat_idx);
        const double fuel_need = cascade::propellant_used_kg(mass_before, k_auto_dv_km_s);
        const double fuel_after = fuel_before - fuel_need;
        if (fuel_after <= SAT_FUEL_EOL_KG + EPS_NUM) {
            store.set_sat_status(sat_idx, SatStatus::FUEL_LOW);
            graveyard_requested_by_sat[sat_id] = true;
            continue;
        }

        double earliest_burn_epoch = epoch_s + ops_signal_latency_s();
        if (last_it != last_burn_epoch_by_sat.end()) {
            earliest_burn_epoch = std::max(earliest_burn_epoch, last_it->second + SAT_COOLDOWN_S);
        }

        double burn_epoch = 0.0;
        double upload_epoch = 0.0;
        std::string upload_station;
        if (!cascade::choose_burn_epoch_with_upload(
                store,
                burn_queue,
                last_burn_epoch_by_sat,
                sat_idx,
                epoch_s,
                earliest_burn_epoch,
                earliest_burn_epoch + ops_auto_upload_horizon_s(),
                burn_epoch,
                upload_epoch,
                upload_station)) {
            continue;
        }

        ScheduledBurn burn;
        burn.id = "AUTO-COLA-" + std::to_string(tick_id) + "-" + std::to_string(planned);
        burn.satellite_id = sat_id;
        burn.upload_station_id = upload_station;
        burn.upload_epoch_s = upload_epoch;
        burn.burn_epoch_s = burn_epoch;
        burn.delta_v_km_s = dv;
        burn.delta_v_norm_km_s = k_auto_dv_km_s;
        burn.auto_generated = true;
        burn.recovery_burn = false;
        burn.graveyard_burn = false;
        burn.blackout_overlap = cascade::burn_overlaps_blackout(store, sat_idx, burn_epoch);

        if (const ConjunctionRecord* trigger = best_predictive_record_for_satellite(predictive_records, sat_id)) {
            burn.scheduled_from_predictive_cdm = true;
            burn.trigger_debris_id = trigger->debris_id;
            burn.trigger_tca_epoch_s = trigger->tca_epoch_s;
            burn.trigger_miss_distance_km = trigger->miss_distance_km;
            burn.trigger_approach_speed_km_s = trigger->approach_speed_km_s;
            burn.trigger_fail_open = trigger->fail_open;
        }

        burn_queue.push_back(std::move(burn));
        ++planned;
    }

    return planned;
}

// ---------------------------------------------------------------------------
// 24-hour predictive Conjunction Data Message (CDM) scanner.
//
// After each simulation step, forward-propagates all broad-phase candidate
// pairs in coarse sub-steps (default ~600 s) over a 24-hour horizon.  Any
// pair whose minimum separation drops below COLLISION_THRESHOLD_KM is counted
// as an active CDM warning.
//
// Design notes:
//   - Uses the *current* post-step states from the StateStore (not clones).
//   - Each pair is independently propagated via RK4+J2 in a temporary copy.
//   - The broad-phase is re-used to avoid O(N²) pair enumeration.
//   - Fail-open: if propagation fails for a pair, it counts as a warning.
// ---------------------------------------------------------------------------
CdmWarningScanResult compute_cdm_warnings_24h(
    const StateStore& store,
    double base_epoch_s,
    const BroadPhaseConfig& broad_cfg) noexcept
{
    const double k_cdm_horizon_s = cdm_params().horizon_s;
    const double k_cdm_substep_s = cdm_params().substep_s;
    const double k_rk4_max_step_s = cdm_params().rk4_max_step_s;
    const double collision_threshold_sq =
        COLLISION_THRESHOLD_KM * COLLISION_THRESHOLD_KM;
    // Tiered screening: emit watch/warning records up to SCREENING_THRESHOLD_KM.
    // Only CRITICAL records (<= COLLISION_THRESHOLD_KM) drive auto-COLA.
    const double screening_threshold_sq =
        SCREENING_THRESHOLD_KM * SCREENING_THRESHOLD_KM;
    constexpr std::size_t kMaxPredictiveRecords = 1024;

    // Broad-phase candidate generation uses the *current* state snapshot.
    const BroadPhaseResult broad = generate_broad_phase_candidates(store, broad_cfg);

    CdmWarningScanResult out;
    out.records.reserve(std::min<std::size_t>(broad.candidates.size(), kMaxPredictiveRecords));

    for (const BroadPhasePair& pair : broad.candidates) {
        const std::size_t sat_idx = static_cast<std::size_t>(pair.sat_idx);
        const std::size_t obj_idx = static_cast<std::size_t>(pair.obj_idx);
        if (sat_idx >= store.size() || obj_idx >= store.size()) continue;
        if (store.type(sat_idx) != ObjectType::SATELLITE) continue;
        if (store.type(obj_idx) != ObjectType::DEBRIS) continue;

        // Take copies of current state for forward propagation.
        Vec3 rs{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
        Vec3 vs{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};
        Vec3 rd{store.rx(obj_idx), store.ry(obj_idx), store.rz(obj_idx)};
        Vec3 vd{store.vx(obj_idx), store.vy(obj_idx), store.vz(obj_idx)};

        bool critical_found = false;
        bool fail_open = false;
        double remaining_s = k_cdm_horizon_s;
        double elapsed_s = 0.0;
        double min_d2 = vector_norm2(Vec3{rs.x - rd.x, rs.y - rd.y, rs.z - rd.z});
        double min_epoch_s = base_epoch_s;
        Vec3 min_sat = rs;
        Vec3 min_deb = rd;
        Vec3 min_rel_v{vs.x - vd.x, vs.y - vd.y, vs.z - vd.z};

        while (remaining_s > EPS_NUM) {
            const double dt = std::min(k_cdm_substep_s, remaining_s);
            const bool ok_s = propagate_rk4_j2_substep(rs, vs, dt, k_rk4_max_step_s);
            const bool ok_d = propagate_rk4_j2_substep(rd, vd, dt, k_rk4_max_step_s);

            if (!ok_s || !ok_d) {
                // Fail-open: propagation failure => count as critical warning.
                critical_found = true;
                fail_open = true;
                break;
            }

            elapsed_s += dt;

            const double dx = rs.x - rd.x;
            const double dy = rs.y - rd.y;
            const double dz = rs.z - rd.z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < min_d2) {
                min_d2 = d2;
                min_epoch_s = base_epoch_s + elapsed_s;
                min_sat = rs;
                min_deb = rd;
                min_rel_v = {vs.x - vd.x, vs.y - vd.y, vs.z - vd.z};
            }

            if (d2 < collision_threshold_sq) {
                critical_found = true;
                break;
            }

            remaining_s -= dt;
        }

        // Emit tiered records: CRITICAL triggers warning_count + auto-COLA;
        // WARNING/WATCH are informational only.
        const double min_dist = fail_open ? 0.0 : std::sqrt(std::max(0.0, min_d2));
        const bool within_screening = fail_open || critical_found || (min_d2 < screening_threshold_sq);

        if (within_screening) {
            const CdmSeverity sev = fail_open ? CdmSeverity::CRITICAL
                                              : classify_miss_distance(min_dist);

            if (sev == CdmSeverity::CRITICAL) {
                ++out.warning_count;
            }

            ConjunctionRecord rec;
            rec.satellite_id = store.id(sat_idx);
            rec.debris_id = store.id(obj_idx);
            rec.tca_epoch_s = min_epoch_s;
            rec.miss_distance_km = min_dist;
            rec.approach_speed_km_s = std::sqrt(vector_norm2(min_rel_v));
            rec.sat_pos_eci_km = min_sat;
            rec.deb_pos_eci_km = min_deb;
            rec.collision = (!fail_open && min_d2 < collision_threshold_sq);
            rec.predictive = true;
            rec.fail_open = fail_open;
            rec.severity = sev;
            rec.tick_id = 0;
            append_conjunction_record_limited(out.records, std::move(rec), kMaxPredictiveRecords);
        }
    }

    return out;
}

std::string build_snapshot(const StateStore& store,
                           const SimClock&   clock)
{
    std::string out;
    out.reserve(store.satellite_count() * 128 + store.debris_count() * 56 + 128);
    const double epoch_s = clock.epoch_s();

    out += "{\"timestamp\":";
    append_json_string(out, clock.to_iso());
    out += ",\"satellites\":[";

    bool first_sat = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != ObjectType::SATELLITE) continue;
        if (!first_sat) out.push_back(',');
        first_sat = false;

        out += "{\"id\":";
        append_json_string(out, store.id(i));

        const Vec3 eci{store.rx(i), store.ry(i), store.rz(i)};
        const Vec3 ecef = eci_to_ecef(eci, epoch_s);
        double lat_deg = 0.0;
        double lon_deg = 0.0;
        double alt_km = 0.0;
        (void)ecef_to_geodetic(ecef, lat_deg, lon_deg, alt_km);

        out += ",\"lat\":";
        out += fmt_double(lat_deg, 6);
        out += ",\"lon\":";
        out += fmt_double(lon_deg, 6);
        out += ",\"fuel_kg\":";
        out += fmt_double(store.fuel_kg(i), 3);
        out += ",\"status\":";
        append_json_string(out, sat_status_str(store.sat_status(i)));
        out += '}';
    }

    out += "],\"debris_cloud\":[";
    bool first_deb = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != ObjectType::DEBRIS) continue;
        if (!first_deb) out.push_back(',');
        first_deb = false;

        out += '[';
        append_json_string(out, store.id(i));

        const Vec3 eci{store.rx(i), store.ry(i), store.rz(i)};
        const Vec3 ecef = eci_to_ecef(eci, epoch_s);
        double lat_deg = 0.0;
        double lon_deg = 0.0;
        double alt_km = 0.0;
        (void)ecef_to_geodetic(ecef, lat_deg, lon_deg, alt_km);

        out += ',';
        out += fmt_double(lat_deg, 6);
        out += ',';
        out += fmt_double(lon_deg, 6);
        out += ',';
        out += fmt_double(alt_km, 3);
        out += ']';
    }

    out += "]}";
    return out;
}

std::string build_conflicts_json(const StateStore& store)
{
    const auto history = store.conflict_history_snapshot();
    const auto by_source = store.conflicts_by_source_snapshot();

    std::string out;
    out.reserve(256 + history.size() * 200 + by_source.size() * 32);

    out += "{\"status\":\"OK\",\"total_conflicts\":";
    out += std::to_string(store.total_type_conflicts());
    out += ",\"ring_size\":";
    out += std::to_string(history.size());
    out += ",\"conflicts_by_source\":{";

    bool first_src = true;
    for (const auto& kv : by_source) {
        if (!first_src) out.push_back(',');
        first_src = false;
        append_json_string(out, kv.first);
        out.push_back(':');
        out += std::to_string(kv.second);
    }

    out += "},\"recent\":[";

    bool first = true;
    for (const auto& rec : history) {
        if (!first) out.push_back(',');
        first = false;
        out += "{\"object_id\":";
        append_json_string(out, rec.object_id);
        out += ",\"stored_type\":";
        append_json_string(out, object_type_str(rec.stored_type));
        out += ",\"incoming_type\":";
        append_json_string(out, object_type_str(rec.incoming_type));
        out += ",\"telemetry_timestamp\":";
        append_json_string(out, rec.telemetry_timestamp);
        out += ",\"ingestion_timestamp\":";
        append_json_string(out, iso8601(rec.ingestion_unix_s));
        out += ",\"source_id\":";
        append_json_string(out, rec.source_id);
        out += ",\"reason\":";
        append_json_string(out, rec.reason);
        out += '}';
    }

    out += "]}";
    return out;
}

std::string build_propagation_json(const StateStore& store,
                                   const PropagationStats& stats,
                                   const StepRunConfig& cfg,
                                   const RecoveryPlannerConfig& recovery_cfg)
{
    std::string out;
    out.reserve(320);
    out += "{\"status\":\"OK\",\"last_tick\":{";
    out += "\"adaptive_fast\":";
    out += std::to_string(stats.fast_last_tick);
    out += ",\"adaptive_rk4\":";
    out += std::to_string(stats.rk4_last_tick);
    out += ",\"escalated_after_probe\":";
    out += std::to_string(stats.escalated_last_tick);
    out += ",\"narrow_pairs_checked\":";
    out += std::to_string(stats.narrow_pairs_last_tick);
    out += ",\"collisions_detected\":";
    out += std::to_string(stats.collisions_last_tick);
    out += ",\"maneuvers_executed\":";
    out += std::to_string(stats.maneuvers_last_tick);
    out += ",\"narrow_refined_pairs\":";
    out += std::to_string(stats.narrow_refined_pairs_last_tick);
    out += ",\"narrow_refine_cleared\":";
    out += std::to_string(stats.narrow_refine_cleared_last_tick);
    out += ",\"narrow_refine_fail_open\":";
    out += std::to_string(stats.narrow_refine_fail_open_last_tick);
    out += ",\"narrow_full_refined_pairs\":";
    out += std::to_string(stats.narrow_full_refined_pairs_last_tick);
    out += ",\"narrow_full_refine_cleared\":";
    out += std::to_string(stats.narrow_full_refine_cleared_last_tick);
    out += ",\"narrow_full_refine_fail_open\":";
    out += std::to_string(stats.narrow_full_refine_fail_open_last_tick);
    out += ",\"narrow_full_refine_budget_allocated\":";
    out += std::to_string(stats.narrow_full_refine_budget_allocated_last_tick);
    out += ",\"narrow_full_refine_budget_exhausted\":";
    out += std::to_string(stats.narrow_full_refine_budget_exhausted_last_tick);
    out += ",\"narrow_uncertainty_promoted_pairs\":";
    out += std::to_string(stats.narrow_uncertainty_promoted_pairs_last_tick);
    out += ",\"narrow_plane_phase_evaluated_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_evaluated_pairs_last_tick);
    out += ",\"narrow_plane_phase_shadow_rejected_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_shadow_rejected_pairs_last_tick);
    out += ",\"narrow_plane_phase_hard_rejected_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_hard_rejected_pairs_last_tick);
    out += ",\"narrow_plane_phase_fail_open_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_pairs_last_tick);
    out += ",\"narrow_plane_phase_reject_reason_plane_angle\":";
    out += std::to_string(stats.narrow_plane_phase_reject_reason_plane_angle_last_tick);
    out += ",\"narrow_plane_phase_reject_reason_phase_angle\":";
    out += std::to_string(stats.narrow_plane_phase_reject_reason_phase_angle_last_tick);
    out += ",\"narrow_plane_phase_fail_open_reason_elements_invalid\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_elements_invalid_last_tick);
    out += ",\"narrow_plane_phase_fail_open_reason_eccentricity_guard\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_eccentricity_guard_last_tick);
    out += ",\"narrow_plane_phase_fail_open_reason_non_finite_state\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_non_finite_state_last_tick);
    out += ",\"narrow_plane_phase_fail_open_reason_angular_momentum_degenerate\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_last_tick);
    out += ",\"narrow_plane_phase_fail_open_reason_plane_angle_non_finite\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_last_tick);
    out += ",\"narrow_plane_phase_fail_open_reason_phase_angle_non_finite\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_last_tick);
    out += ",\"narrow_plane_phase_fail_open_reason_uncertainty_override\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_uncertainty_override_last_tick);
    out += ",\"narrow_moid_evaluated_pairs\":";
    out += std::to_string(stats.narrow_moid_evaluated_pairs_last_tick);
    out += ",\"narrow_moid_shadow_rejected_pairs\":";
    out += std::to_string(stats.narrow_moid_shadow_rejected_pairs_last_tick);
    out += ",\"narrow_moid_hard_rejected_pairs\":";
    out += std::to_string(stats.narrow_moid_hard_rejected_pairs_last_tick);
    out += ",\"narrow_moid_fail_open_pairs\":";
    out += std::to_string(stats.narrow_moid_fail_open_pairs_last_tick);
    out += ",\"narrow_moid_reject_reason_distance_threshold\":";
    out += std::to_string(stats.narrow_moid_reject_reason_distance_threshold_last_tick);
    out += ",\"narrow_moid_fail_open_reason_elements_invalid\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_elements_invalid_last_tick);
    out += ",\"narrow_moid_fail_open_reason_eccentricity_guard\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_eccentricity_guard_last_tick);
    out += ",\"narrow_moid_fail_open_reason_non_finite_state\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_non_finite_state_last_tick);
    out += ",\"narrow_moid_fail_open_reason_sampling_failure\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_sampling_failure_last_tick);
    out += ",\"narrow_moid_fail_open_reason_hf_placeholder\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_hf_placeholder_last_tick);
    out += ",\"narrow_moid_fail_open_reason_uncertainty_override\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_uncertainty_override_last_tick);
    out += ",\"narrow_refine_fail_open_reason_rk4_failure\":";
    out += std::to_string(stats.narrow_refine_fail_open_reason_rk4_failure_last_tick);
    out += ",\"narrow_full_refine_fail_open_reason_rk4_failure\":";
    out += std::to_string(stats.narrow_full_refine_fail_open_reason_rk4_failure_last_tick);
    out += ",\"narrow_full_refine_fail_open_reason_budget_exhausted\":";
    out += std::to_string(stats.narrow_full_refine_fail_open_reason_budget_exhausted_last_tick);
    out += ",\"auto_planned_maneuvers\":";
    out += std::to_string(stats.auto_planned_last_tick);
    out += ",\"recovery_pending_marked\":";
    out += std::to_string(stats.recovery_pending_marked_last_tick);
    out += ",\"recovery_planned\":";
    out += std::to_string(stats.recovery_planned_last_tick);
    out += ",\"recovery_deferred\":";
    out += std::to_string(stats.recovery_deferred_last_tick);
    out += ",\"recovery_completed\":";
    out += std::to_string(stats.recovery_completed_last_tick);
    out += ",\"graveyard_planned\":";
    out += std::to_string(stats.graveyard_planned_last_tick);
    out += ",\"graveyard_deferred\":";
    out += std::to_string(stats.graveyard_deferred_last_tick);
    out += ",\"graveyard_completed\":";
    out += std::to_string(stats.graveyard_completed_last_tick);
    out += ",\"upload_window_missed\":";
    out += std::to_string(stats.upload_missed_last_tick);
    out += ",\"stationkeeping_outside_box\":";
    out += std::to_string(stats.stationkeeping_outside_box_last_tick);
    out += ",\"stationkeeping_uptime_penalty_mean\":";
    out += fmt_double(stats.stationkeeping_uptime_penalty_mean_last_tick, 6);
    out += ",\"stationkeeping_slot_radius_error_mean_km\":";
    out += fmt_double(stats.stationkeeping_slot_radius_error_mean_km_last_tick, 6);
    out += ",\"stationkeeping_slot_radius_error_max_km\":";
    out += fmt_double(stats.stationkeeping_slot_radius_error_max_km_last_tick, 6);
    out += ",\"recovery_slot_error_mean\":";
    out += fmt_double(stats.recovery_slot_error_mean_last_tick, 6);
    out += ",\"recovery_slot_error_max\":";
    out += fmt_double(stats.recovery_slot_error_max_last_tick, 6);
    out += ",\"failed_objects\":";
    out += std::to_string(store.failed_last_tick());
    out += ",\"broad_pairs_considered\":";
    out += std::to_string(stats.broad_pairs_last_tick);
    out += ",\"broad_candidates\":";
    out += std::to_string(stats.broad_candidates_last_tick);
    out += ",\"broad_shell_overlap_pass\":";
    out += std::to_string(stats.broad_overlap_pass_last_tick);
    out += ",\"broad_dcriterion_rejected\":";
    out += std::to_string(stats.broad_dcriterion_rejected_last_tick);
    out += ",\"broad_dcriterion_shadow_rejected\":";
    out += std::to_string(stats.broad_dcriterion_shadow_rejected_last_tick);
    out += ",\"broad_fail_open_objects\":";
    out += std::to_string(stats.broad_fail_open_objects_last_tick);
    out += ",\"broad_fail_open_satellites\":";
    out += std::to_string(stats.broad_fail_open_satellites_last_tick);
    out += ",\"broad_shell_margin_km\":";
    out += fmt_double(stats.broad_shell_margin_km_last_tick, 3);
    out += ",\"broad_dcriterion_enabled\":";
    out += (stats.broad_dcriterion_enabled_last_tick ? "true" : "false");
    out += ",\"broad_a_bin_width_km\":";
    out += fmt_double(stats.broad_a_bin_width_km_last_tick, 3);
    out += ",\"broad_band_neighbor_bins\":";
    out += std::to_string(stats.broad_band_neighbor_bins_last_tick);
    out += "},\"totals\":{";
    out += "\"adaptive_fast\":";
    out += std::to_string(stats.fast_total);
    out += ",\"adaptive_rk4\":";
    out += std::to_string(stats.rk4_total);
    out += ",\"escalated_after_probe\":";
    out += std::to_string(stats.escalated_total);
    out += ",\"narrow_pairs_checked\":";
    out += std::to_string(stats.narrow_pairs_total);
    out += ",\"collisions_detected\":";
    out += std::to_string(stats.collisions_total);
    out += ",\"maneuvers_executed\":";
    out += std::to_string(stats.maneuvers_total);
    out += ",\"narrow_refined_pairs\":";
    out += std::to_string(stats.narrow_refined_pairs_total);
    out += ",\"narrow_refine_cleared\":";
    out += std::to_string(stats.narrow_refine_cleared_total);
    out += ",\"narrow_refine_fail_open\":";
    out += std::to_string(stats.narrow_refine_fail_open_total);
    out += ",\"narrow_full_refined_pairs\":";
    out += std::to_string(stats.narrow_full_refined_pairs_total);
    out += ",\"narrow_full_refine_cleared\":";
    out += std::to_string(stats.narrow_full_refine_cleared_total);
    out += ",\"narrow_full_refine_fail_open\":";
    out += std::to_string(stats.narrow_full_refine_fail_open_total);
    out += ",\"narrow_full_refine_budget_allocated\":";
    out += std::to_string(stats.narrow_full_refine_budget_allocated_total);
    out += ",\"narrow_full_refine_budget_exhausted\":";
    out += std::to_string(stats.narrow_full_refine_budget_exhausted_total);
    out += ",\"narrow_uncertainty_promoted_pairs\":";
    out += std::to_string(stats.narrow_uncertainty_promoted_pairs_total);
    out += ",\"narrow_plane_phase_evaluated_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_evaluated_pairs_total);
    out += ",\"narrow_plane_phase_shadow_rejected_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_shadow_rejected_pairs_total);
    out += ",\"narrow_plane_phase_hard_rejected_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_hard_rejected_pairs_total);
    out += ",\"narrow_plane_phase_fail_open_pairs\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_pairs_total);
    out += ",\"narrow_plane_phase_reject_reason_plane_angle\":";
    out += std::to_string(stats.narrow_plane_phase_reject_reason_plane_angle_total);
    out += ",\"narrow_plane_phase_reject_reason_phase_angle\":";
    out += std::to_string(stats.narrow_plane_phase_reject_reason_phase_angle_total);
    out += ",\"narrow_plane_phase_fail_open_reason_elements_invalid\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_elements_invalid_total);
    out += ",\"narrow_plane_phase_fail_open_reason_eccentricity_guard\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_eccentricity_guard_total);
    out += ",\"narrow_plane_phase_fail_open_reason_non_finite_state\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_non_finite_state_total);
    out += ",\"narrow_plane_phase_fail_open_reason_angular_momentum_degenerate\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total);
    out += ",\"narrow_plane_phase_fail_open_reason_plane_angle_non_finite\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total);
    out += ",\"narrow_plane_phase_fail_open_reason_phase_angle_non_finite\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total);
    out += ",\"narrow_plane_phase_fail_open_reason_uncertainty_override\":";
    out += std::to_string(stats.narrow_plane_phase_fail_open_reason_uncertainty_override_total);
    out += ",\"narrow_moid_evaluated_pairs\":";
    out += std::to_string(stats.narrow_moid_evaluated_pairs_total);
    out += ",\"narrow_moid_shadow_rejected_pairs\":";
    out += std::to_string(stats.narrow_moid_shadow_rejected_pairs_total);
    out += ",\"narrow_moid_hard_rejected_pairs\":";
    out += std::to_string(stats.narrow_moid_hard_rejected_pairs_total);
    out += ",\"narrow_moid_fail_open_pairs\":";
    out += std::to_string(stats.narrow_moid_fail_open_pairs_total);
    out += ",\"narrow_moid_reject_reason_distance_threshold\":";
    out += std::to_string(stats.narrow_moid_reject_reason_distance_threshold_total);
    out += ",\"narrow_moid_fail_open_reason_elements_invalid\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_elements_invalid_total);
    out += ",\"narrow_moid_fail_open_reason_eccentricity_guard\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_eccentricity_guard_total);
    out += ",\"narrow_moid_fail_open_reason_non_finite_state\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_non_finite_state_total);
    out += ",\"narrow_moid_fail_open_reason_sampling_failure\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_sampling_failure_total);
    out += ",\"narrow_moid_fail_open_reason_hf_placeholder\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_hf_placeholder_total);
    out += ",\"narrow_moid_fail_open_reason_uncertainty_override\":";
    out += std::to_string(stats.narrow_moid_fail_open_reason_uncertainty_override_total);
    out += ",\"narrow_refine_fail_open_reason_rk4_failure\":";
    out += std::to_string(stats.narrow_refine_fail_open_reason_rk4_failure_total);
    out += ",\"narrow_full_refine_fail_open_reason_rk4_failure\":";
    out += std::to_string(stats.narrow_full_refine_fail_open_reason_rk4_failure_total);
    out += ",\"narrow_full_refine_fail_open_reason_budget_exhausted\":";
    out += std::to_string(stats.narrow_full_refine_fail_open_reason_budget_exhausted_total);
    out += ",\"auto_planned_maneuvers\":";
    out += std::to_string(stats.auto_planned_total);
    out += ",\"recovery_pending_marked\":";
    out += std::to_string(stats.recovery_pending_marked_total);
    out += ",\"recovery_planned\":";
    out += std::to_string(stats.recovery_planned_total);
    out += ",\"recovery_deferred\":";
    out += std::to_string(stats.recovery_deferred_total);
    out += ",\"recovery_completed\":";
    out += std::to_string(stats.recovery_completed_total);
    out += ",\"graveyard_planned\":";
    out += std::to_string(stats.graveyard_planned_total);
    out += ",\"graveyard_deferred\":";
    out += std::to_string(stats.graveyard_deferred_total);
    out += ",\"graveyard_completed\":";
    out += std::to_string(stats.graveyard_completed_total);
    out += ",\"upload_window_missed\":";
    out += std::to_string(stats.upload_missed_total);
    out += ",\"stationkeeping_outside_box\":";
    out += std::to_string(stats.stationkeeping_outside_box_total);
    out += ",\"stationkeeping_uptime_penalty_mean\":";
    if (stats.stationkeeping_uptime_penalty_samples_total > 0) {
        out += fmt_double(
            stats.stationkeeping_uptime_penalty_sum_total
                / static_cast<double>(stats.stationkeeping_uptime_penalty_samples_total),
            6
        );
    } else {
        out += "0.000000";
    }
    out += ",\"stationkeeping_slot_radius_error_mean_km\":";
    if (stats.stationkeeping_slot_radius_error_samples_total > 0) {
        out += fmt_double(
            stats.stationkeeping_slot_radius_error_sum_total
                / static_cast<double>(stats.stationkeeping_slot_radius_error_samples_total),
            6
        );
    } else {
        out += "0.000000";
    }
    out += ",\"stationkeeping_slot_radius_error_max_km\":";
    out += fmt_double(stats.stationkeeping_slot_radius_error_max_total, 6);
    out += ",\"recovery_slot_error_mean\":";
    if (stats.recovery_slot_error_samples_total > 0) {
        out += fmt_double(
            stats.recovery_slot_error_sum_total
                / static_cast<double>(stats.recovery_slot_error_samples_total),
            6
        );
    } else {
        out += "0.000000";
    }
    out += ",\"recovery_slot_error_max\":";
    out += fmt_double(stats.recovery_slot_error_max_total, 6);
    out += ",\"failed_objects\":";
    out += std::to_string(store.failed_propagation_total());
    out += ",\"broad_pairs_considered\":";
    out += std::to_string(stats.broad_pairs_total);
    out += ",\"broad_candidates\":";
    out += std::to_string(stats.broad_candidates_total);
    out += ",\"broad_shell_overlap_pass\":";
    out += std::to_string(stats.broad_overlap_pass_total);
    out += ",\"broad_dcriterion_rejected\":";
    out += std::to_string(stats.broad_dcriterion_rejected_total);
    out += ",\"broad_dcriterion_shadow_rejected\":";
    out += std::to_string(stats.broad_dcriterion_shadow_rejected_total);
    out += ",\"broad_fail_open_objects\":";
    out += std::to_string(stats.broad_fail_open_objects_total);
    out += ",\"broad_fail_open_satellites\":";
    out += std::to_string(stats.broad_fail_open_satellites_total);
    out += "},\"config\":{";
    out += "\"mode_threshold_step_seconds\":21600,";
    out += "\"mode_threshold_perigee_alt_km\":350.0,";
    out += "\"mode_threshold_e\":0.98,";
    out += "\"fast_lane_max_dt_s\":30.0,";
    out += "\"fast_lane_max_e\":0.02,";
    out += "\"fast_lane_min_perigee_alt_km\":500.0,";
    out += "\"fast_lane_ext_max_dt_s\":45.0,";
    out += "\"fast_lane_ext_max_e\":0.003,";
    out += "\"fast_lane_ext_min_perigee_alt_km\":650.0,";
    out += "\"probe_max_step_s\":120.0,";
    out += "\"probe_pos_thresh_km\":0.5,";
    out += "\"probe_vel_thresh_ms\":0.5,";
    out += "\"narrow_tca_guard_km\":";
    out += fmt_double(cfg.narrow_phase.tca_guard_km, 3);
    out += ",\"narrow_refine_band_km\":";
    out += fmt_double(cfg.narrow_phase.refine_band_km, 3);
    out += ",\"narrow_full_refine_band_km\":";
    out += fmt_double(cfg.narrow_phase.full_refine_band_km, 3);
    out += ",\"narrow_high_rel_speed_km_s\":";
    out += fmt_double(cfg.narrow_phase.high_rel_speed_km_s, 3);
    out += ",\"narrow_high_rel_speed_extra_band_km\":";
    out += fmt_double(cfg.narrow_phase.high_rel_speed_extra_band_km, 3);
    out += ",\"narrow_full_refine_budget_base\":";
    out += std::to_string(cfg.narrow_phase.full_refine_budget_base);
    out += ",\"narrow_full_refine_budget_min\":";
    out += std::to_string(cfg.narrow_phase.full_refine_budget_min);
    out += ",\"narrow_full_refine_budget_max\":";
    out += std::to_string(cfg.narrow_phase.full_refine_budget_max);
    out += ",\"narrow_full_refine_samples\":";
    out += std::to_string(cfg.narrow_phase.full_refine_samples);
    out += ",\"narrow_full_refine_substep_s\":";
    out += fmt_double(cfg.narrow_phase.full_refine_substep_s, 3);
    out += ",\"narrow_micro_refine_max_step_s\":";
    out += fmt_double(cfg.narrow_phase.micro_refine_max_step_s, 3);
    out += ",\"narrow_plane_phase_shadow\":";
    out += (cfg.narrow_phase.plane_phase_shadow ? "true" : "false");
    out += ",\"narrow_plane_phase_filter\":";
    out += (cfg.narrow_phase.plane_phase_filter ? "true" : "false");
    out += ",\"narrow_plane_angle_threshold_rad\":";
    out += fmt_double(cfg.narrow_phase.plane_angle_threshold_rad, 6);
    out += ",\"narrow_phase_angle_threshold_rad\":";
    out += fmt_double(cfg.narrow_phase.phase_angle_threshold_rad, 6);
    out += ",\"narrow_phase_max_e\":";
    out += fmt_double(cfg.narrow_phase.phase_max_e, 3);
    out += ",\"narrow_moid_shadow\":";
    out += (cfg.narrow_phase.moid_shadow ? "true" : "false");
    out += ",\"narrow_moid_filter\":";
    out += (cfg.narrow_phase.moid_filter ? "true" : "false");
    out += ",\"narrow_moid_samples\":";
    out += std::to_string(cfg.narrow_phase.moid_samples);
    out += ",\"narrow_moid_reject_threshold_km\":";
    out += fmt_double(cfg.narrow_phase.moid_reject_threshold_km, 3);
    out += ",\"narrow_moid_max_e\":";
    out += fmt_double(cfg.narrow_phase.moid_max_e, 3);
    out += ",\"recovery_scale_t\":";
    out += fmt_double(recovery_cfg.scale_t, 8);
    out += ",\"recovery_scale_r\":";
    out += fmt_double(recovery_cfg.scale_r, 8);
    out += ",\"recovery_radial_share\":";
    out += fmt_double(recovery_cfg.radial_share, 6);
    out += ",\"recovery_scale_n\":";
    out += fmt_double(recovery_cfg.scale_n, 8);
    out += ",\"recovery_fallback_norm_km_s\":";
    out += fmt_double(recovery_cfg.fallback_norm_km_s, 8);
    out += ",\"recovery_max_request_ratio\":";
    out += fmt_double(recovery_cfg.max_request_ratio, 6);
    out += ",\"recovery_solver_mode\":\"";
    out += (recovery_cfg.solver_mode == RecoverySolverMode::CW_ZEM_EQUIVALENT)
        ? "cw_zem"
        : "heuristic";
    out += "\"";
    out += ",";
    out += "\"broad_shell_margin_km\":";
    out += fmt_double(cfg.broad_phase.shell_margin_km, 3);
    out += ",\"broad_invalid_shell_pad_km\":";
    out += fmt_double(cfg.broad_phase.invalid_shell_pad_km, 3);
    out += ",\"broad_a_bin_width_km\":";
    out += fmt_double(cfg.broad_phase.a_bin_width_km, 3);
    out += ",\"broad_i_bin_width_rad\":";
    out += fmt_double(cfg.broad_phase.i_bin_width_rad, 6);
    out += ",\"broad_band_neighbor_bins\":";
    out += std::to_string(cfg.broad_phase.band_neighbor_bins);
    out += ",\"broad_i_neighbor_filter\":";
    out += (cfg.broad_phase.enable_i_neighbor_filter ? "true" : "false");
    out += ",\"broad_high_e_fail_open\":";
    out += fmt_double(cfg.broad_phase.high_e_fail_open, 3);
    out += ",\"broad_dcriterion_enabled\":";
    out += (cfg.broad_phase.enable_dcriterion ? "true" : "false");
    out += ",\"broad_dcriterion_shadow\":";
    out += (cfg.broad_phase.shadow_dcriterion ? "true" : "false");
    out += ",\"broad_dcriterion_threshold\":";
    out += fmt_double(cfg.broad_phase.dcriterion_threshold, 3);
    out += "}}";
    return out;
}

// ---------------------------------------------------------------------------
// build_burns_json — serialize executed burn history + pending burn queue
// ---------------------------------------------------------------------------
std::string build_burns_json(const std::deque<ExecutedBurn>& history,
                             const std::deque<ScheduledBurn>& dropped,
                             const std::vector<ScheduledBurn>& pending,
                             const std::unordered_map<std::string, PerSatManeuverStats>& per_sat)
{
    std::string out;
    out.reserve(6144);
    out += "{\"executed\":[";
    bool first = true;
    for (const auto& b : history) {
        if (!first) out += ',';
        first = false;
        out += "{\"id\":";
        append_json_string(out, b.id);
        out += ",\"satellite_id\":";
        append_json_string(out, b.satellite_id);
        out += ",\"burn_epoch\":";
        append_json_string(out, iso8601(b.burn_epoch_s));
        out += ",\"upload_epoch\":";
        append_json_string(out, iso8601(b.upload_epoch_s));
        out += ",\"upload_station\":";
        append_json_string(out, b.upload_station_id);
        out += ",\"delta_v_km_s\":[";
        out += fmt_double(b.delta_v_km_s.x, 9);
        out += ',';
        out += fmt_double(b.delta_v_km_s.y, 9);
        out += ',';
        out += fmt_double(b.delta_v_km_s.z, 9);
        out += "],\"delta_v_norm_km_s\":";
        out += fmt_double(b.delta_v_norm_km_s, 9);
        out += ",\"fuel_before_kg\":";
        out += fmt_double(b.fuel_before_kg, 4);
        out += ",\"fuel_after_kg\":";
        out += fmt_double(b.fuel_after_kg, 4);
        out += ",\"auto_generated\":";
        out += (b.auto_generated ? "true" : "false");
        out += ",\"recovery_burn\":";
        out += (b.recovery_burn ? "true" : "false");
        out += ",\"graveyard_burn\":";
        out += (b.graveyard_burn ? "true" : "false");
        out += ",\"blackout_overlap\":";
        out += (b.blackout_overlap ? "true" : "false");
        out += ",\"cooldown_conflict\":";
        out += (b.cooldown_conflict ? "true" : "false");
        out += ",\"command_conflict\":";
        out += (b.command_conflict ? "true" : "false");
        out += ",\"scheduled_from_predictive_cdm\":";
        out += (b.scheduled_from_predictive_cdm ? "true" : "false");
        out += ",\"trigger_debris_id\":";
        append_json_string(out, b.trigger_debris_id);
        out += ",\"trigger_tca\":";
        append_json_string(out, b.trigger_tca_epoch_s > 0.0 ? iso8601(b.trigger_tca_epoch_s) : std::string{});
        out += ",\"trigger_tca_epoch_s\":";
        out += fmt_double(b.trigger_tca_epoch_s, 3);
        out += ",\"trigger_miss_distance_km\":";
        out += fmt_double(b.trigger_miss_distance_km, 6);
        out += ",\"trigger_approach_speed_km_s\":";
        out += fmt_double(b.trigger_approach_speed_km_s, 6);
        out += ",\"trigger_fail_open\":";
        out += (b.trigger_fail_open ? "true" : "false");
        out += ",\"mitigation_tracked\":";
        out += (b.mitigation_tracked ? "true" : "false");
        out += ",\"mitigation_evaluated\":";
        out += (b.mitigation_evaluated ? "true" : "false");
        out += ",\"collision_avoided\":";
        out += (b.collision_avoided ? "true" : "false");
        out += ",\"mitigation_eval_epoch\":";
        append_json_string(out, b.mitigation_eval_epoch_s > 0.0 ? iso8601(b.mitigation_eval_epoch_s) : std::string{});
        out += ",\"mitigation_eval_epoch_s\":";
        out += fmt_double(b.mitigation_eval_epoch_s, 3);
        out += ",\"mitigation_miss_distance_km\":";
        out += fmt_double(b.mitigation_miss_distance_km, 6);
        out += ",\"mitigation_fail_open\":";
        out += (b.mitigation_fail_open ? "true" : "false");
        out += '}';
    }
    out += "],\"dropped\":[";
    first = true;
    for (const auto& b : dropped) {
        if (!first) out += ',';
        first = false;
        out += "{\"id\":";
        append_json_string(out, b.id);
        out += ",\"satellite_id\":";
        append_json_string(out, b.satellite_id);
        out += ",\"burn_epoch\":";
        append_json_string(out, iso8601(b.burn_epoch_s));
        out += ",\"upload_epoch\":";
        append_json_string(out, b.upload_epoch_s > 0.0 ? iso8601(b.upload_epoch_s) : std::string{});
        out += ",\"upload_station\":";
        append_json_string(out, b.upload_station_id);
        out += ",\"delta_v_km_s\":[";
        out += fmt_double(b.delta_v_km_s.x, 9);
        out += ',';
        out += fmt_double(b.delta_v_km_s.y, 9);
        out += ',';
        out += fmt_double(b.delta_v_km_s.z, 9);
        out += "],\"delta_v_norm_km_s\":";
        out += fmt_double(b.delta_v_norm_km_s, 9);
        out += ",\"auto_generated\":";
        out += (b.auto_generated ? "true" : "false");
        out += ",\"recovery_burn\":";
        out += (b.recovery_burn ? "true" : "false");
        out += ",\"graveyard_burn\":";
        out += (b.graveyard_burn ? "true" : "false");
        out += ",\"blackout_overlap\":";
        out += (b.blackout_overlap ? "true" : "false");
        out += ",\"cooldown_conflict\":";
        out += (b.cooldown_conflict ? "true" : "false");
        out += ",\"command_conflict\":";
        out += (b.command_conflict ? "true" : "false");
        out += ",\"scheduled_from_predictive_cdm\":";
        out += (b.scheduled_from_predictive_cdm ? "true" : "false");
        out += ",\"trigger_debris_id\":";
        append_json_string(out, b.trigger_debris_id);
        out += ",\"trigger_tca\":";
        append_json_string(out, b.trigger_tca_epoch_s > 0.0 ? iso8601(b.trigger_tca_epoch_s) : std::string{});
        out += ",\"trigger_tca_epoch_s\":";
        out += fmt_double(b.trigger_tca_epoch_s, 3);
        out += ",\"trigger_miss_distance_km\":";
        out += fmt_double(b.trigger_miss_distance_km, 6);
        out += ",\"trigger_approach_speed_km_s\":";
        out += fmt_double(b.trigger_approach_speed_km_s, 6);
        out += ",\"trigger_fail_open\":";
        out += (b.trigger_fail_open ? "true" : "false");
        out += ",\"upload_window_missed\":";
        out += (b.upload_window_missed ? "true" : "false");
        out += ",\"dropped_epoch\":";
        append_json_string(out, b.dropped_epoch_s > 0.0 ? iso8601(b.dropped_epoch_s) : std::string{});
        out += ",\"dropped_epoch_s\":";
        out += fmt_double(b.dropped_epoch_s, 3);
        out += '}';
    }
    out += "],\"pending\":[";
    first = true;
    for (const auto& b : pending) {
        if (!first) out += ',';
        first = false;
        out += "{\"id\":";
        append_json_string(out, b.id);
        out += ",\"satellite_id\":";
        append_json_string(out, b.satellite_id);
        out += ",\"burn_epoch\":";
        append_json_string(out, iso8601(b.burn_epoch_s));
        out += ",\"upload_epoch\":";
        append_json_string(out, iso8601(b.upload_epoch_s));
        out += ",\"upload_station\":";
        append_json_string(out, b.upload_station_id);
        out += ",\"delta_v_km_s\":[";
        out += fmt_double(b.delta_v_km_s.x, 9);
        out += ',';
        out += fmt_double(b.delta_v_km_s.y, 9);
        out += ',';
        out += fmt_double(b.delta_v_km_s.z, 9);
        out += "],\"delta_v_norm_km_s\":";
        out += fmt_double(b.delta_v_norm_km_s, 9);
        out += ",\"auto_generated\":";
        out += (b.auto_generated ? "true" : "false");
        out += ",\"recovery_burn\":";
        out += (b.recovery_burn ? "true" : "false");
        out += ",\"graveyard_burn\":";
        out += (b.graveyard_burn ? "true" : "false");
        out += ",\"blackout_overlap\":";
        out += (b.blackout_overlap ? "true" : "false");
        out += ",\"cooldown_conflict\":";
        out += (b.cooldown_conflict ? "true" : "false");
        out += ",\"command_conflict\":";
        out += (b.command_conflict ? "true" : "false");
        out += ",\"scheduled_from_predictive_cdm\":";
        out += (b.scheduled_from_predictive_cdm ? "true" : "false");
        out += ",\"trigger_debris_id\":";
        append_json_string(out, b.trigger_debris_id);
        out += ",\"trigger_tca\":";
        append_json_string(out, b.trigger_tca_epoch_s > 0.0 ? iso8601(b.trigger_tca_epoch_s) : std::string{});
        out += ",\"trigger_tca_epoch_s\":";
        out += fmt_double(b.trigger_tca_epoch_s, 3);
        out += ",\"trigger_miss_distance_km\":";
        out += fmt_double(b.trigger_miss_distance_km, 6);
        out += ",\"trigger_approach_speed_km_s\":";
        out += fmt_double(b.trigger_approach_speed_km_s, 6);
        out += ",\"trigger_fail_open\":";
        out += (b.trigger_fail_open ? "true" : "false");
        out += ",\"upload_window_missed\":";
        out += (b.upload_window_missed ? "true" : "false");
        out += '}';
    }
    out += "],\"per_satellite\":{";
    first = true;
    for (const auto& [sat_id, st] : per_sat) {
        if (!first) out += ',';
        first = false;
        append_json_string(out, sat_id);
        out += ":{\"burns_executed\":";
        out += std::to_string(st.burns_executed);
        out += ",\"delta_v_total_km_s\":";
        out += fmt_double(st.delta_v_total_km_s, 9);
        out += ",\"fuel_consumed_kg\":";
        out += fmt_double(st.fuel_consumed_kg, 4);
        out += ",\"collisions_avoided\":";
        out += std::to_string(st.collisions_avoided);
        out += ",\"avoidance_burns_executed\":";
        out += std::to_string(st.avoidance_burns_executed);
        out += ",\"recovery_burns_executed\":";
        out += std::to_string(st.recovery_burns_executed);
        out += ",\"graveyard_burns_executed\":";
        out += std::to_string(st.graveyard_burns_executed);
        out += ",\"avoidance_fuel_consumed_kg\":";
        out += fmt_double(st.avoidance_fuel_consumed_kg, 4);
        out += '}';
    }
    double total_fuel_consumed_kg = 0.0;
    double total_avoidance_fuel_consumed_kg = 0.0;
    std::uint64_t total_collisions_avoided = 0;
    std::uint64_t total_avoidance_burns = 0;
    std::uint64_t total_recovery_burns = 0;
    std::uint64_t total_graveyard_burns = 0;
    for (const auto& [_, st] : per_sat) {
        total_fuel_consumed_kg += st.fuel_consumed_kg;
        total_avoidance_fuel_consumed_kg += st.avoidance_fuel_consumed_kg;
        total_collisions_avoided += st.collisions_avoided;
        total_avoidance_burns += st.avoidance_burns_executed;
        total_recovery_burns += st.recovery_burns_executed;
        total_graveyard_burns += st.graveyard_burns_executed;
    }
    out += "},\"summary\":{";
    out += "\"burns_executed\":";
    out += std::to_string(history.size());
    out += ",\"burns_pending\":";
    out += std::to_string(pending.size());
    out += ",\"burns_dropped\":";
    out += std::to_string(dropped.size());
    out += ",\"fuel_consumed_kg\":";
    out += fmt_double(total_fuel_consumed_kg, 4);
    out += ",\"avoidance_fuel_consumed_kg\":";
    out += fmt_double(total_avoidance_fuel_consumed_kg, 4);
    out += ",\"collisions_avoided\":";
    out += std::to_string(total_collisions_avoided);
    out += ",\"avoidance_burns_executed\":";
    out += std::to_string(total_avoidance_burns);
    out += ",\"recovery_burns_executed\":";
    out += std::to_string(total_recovery_burns);
    out += ",\"graveyard_burns_executed\":";
    out += std::to_string(total_graveyard_burns);
    out += "}}";
    return out;
}

// ---------------------------------------------------------------------------
// build_conjunctions_json — serialize conjunction history ring buffer
// ---------------------------------------------------------------------------
std::string build_conjunctions_json(const std::deque<ConjunctionRecord>& history,
                                    std::string_view satellite_id_filter,
                                    std::string_view source_filter)
{
    std::string out;
    out.reserve(4096);
    std::size_t emitted = 0;
    const bool only_predictive = source_filter == "predicted" || source_filter == "predictive";
    const bool only_history = source_filter == "history";

    out += "{\"conjunctions\":[";
    bool first = true;
    for (const auto& c : history) {
        if (!satellite_id_filter.empty() && c.satellite_id != satellite_id_filter) {
            continue;
        }
        if (only_predictive && !c.predictive) {
            continue;
        }
        if (only_history && c.predictive) {
            continue;
        }
        if (!first) out += ',';
        first = false;
        ++emitted;
        out += "{\"satellite_id\":";
        append_json_string(out, c.satellite_id);
        out += ",\"debris_id\":";
        append_json_string(out, c.debris_id);
        out += ",\"tca\":";
        append_json_string(out, iso8601(c.tca_epoch_s));
        out += ",\"tca_epoch_s\":";
        out += fmt_double(c.tca_epoch_s, 3);
        out += ",\"miss_distance_km\":";
        out += fmt_double(c.miss_distance_km, 6);
        out += ",\"approach_speed_km_s\":";
        out += fmt_double(c.approach_speed_km_s, 6);
        out += ",\"sat_pos_eci_km\":[";
        out += fmt_double(c.sat_pos_eci_km.x, 6);
        out += ',';
        out += fmt_double(c.sat_pos_eci_km.y, 6);
        out += ',';
        out += fmt_double(c.sat_pos_eci_km.z, 6);
        out += "],\"deb_pos_eci_km\":[";
        out += fmt_double(c.deb_pos_eci_km.x, 6);
        out += ',';
        out += fmt_double(c.deb_pos_eci_km.y, 6);
        out += ',';
        out += fmt_double(c.deb_pos_eci_km.z, 6);
        out += "],\"collision\":";
        out += (c.collision ? "true" : "false");
        out += ",\"predictive\":";
        out += (c.predictive ? "true" : "false");
        out += ",\"fail_open\":";
        out += (c.fail_open ? "true" : "false");
        out += ",\"severity\":";
        append_json_string(out, cdm_severity_str(c.severity));
        out += ",\"tick_id\":";
        out += std::to_string(c.tick_id);
        out += '}';
    }
    out += "],\"count\":";
    out += std::to_string(emitted);
    out += ",\"source\":";
    append_json_string(out, source_filter.empty() ? "combined" : std::string(source_filter));
    out += '}';
    return out;
}

// ---------------------------------------------------------------------------
// build_trajectory_json — serialize trailing track + predicted path
// ---------------------------------------------------------------------------
std::string build_trajectory_json(const StateStore& store,
                                  std::string_view satellite_id,
                                  const std::deque<TrackPoint>& trail,
                                  double current_epoch_s)
{
    constexpr double kTrailWindowSeconds = 90.0 * 60.0;
    constexpr double kTrailSampleIntervalSeconds = 60.0;
    constexpr double kTrajectoryPropMaxStepSeconds = 10.0;

    std::string out;
    out.reserve(8192);
    out += "{\"satellite_id\":";
    append_json_string(out, satellite_id);
    out += ",\"trail\":[";
    bool first = true;
    const auto append_track_point = [&](const TrackPoint& tp) {
        if (!first) out += ',';
        first = false;
        out += "{\"epoch_s\":";
        out += fmt_double(tp.epoch_s, 3);
        out += ",\"lat\":";
        out += fmt_double(tp.lat_deg, 6);
        out += ",\"lon\":";
        out += fmt_double(tp.lon_deg, 6);
        out += ",\"alt_km\":";
        out += fmt_double(tp.alt_km, 3);
        out += ",\"eci\":[";
        out += fmt_double(tp.eci_km.x, 6);
        out += ',';
        out += fmt_double(tp.eci_km.y, 6);
        out += ',';
        out += fmt_double(tp.eci_km.z, 6);
        out += "]}";
    };

    const std::size_t sat_idx = store.find(satellite_id);
    bool built_exact_trail = false;
    if (sat_idx < store.size()) {
        std::vector<TrackPoint> exact_trail;
        exact_trail.reserve(static_cast<std::size_t>(kTrailWindowSeconds / kTrailSampleIntervalSeconds) + 1U);

        Vec3 r{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
        Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};

        auto push_track_point = [&](double epoch_s_value, const Vec3& eci) {
            const Vec3 ecef = eci_to_ecef(eci, epoch_s_value);
            double lat = 0.0;
            double lon = 0.0;
            double alt = 0.0;
            ecef_to_geodetic(ecef, lat, lon, alt);

            TrackPoint tp;
            tp.epoch_s = epoch_s_value;
            tp.lat_deg = lat;
            tp.lon_deg = lon;
            tp.alt_km = alt;
            tp.eci_km = eci;
            tp.vel_km_s = v;
            exact_trail.push_back(std::move(tp));
        };

        push_track_point(current_epoch_s, r);

        built_exact_trail = true;
        const int kTrailPoints = static_cast<int>(kTrailWindowSeconds / kTrailSampleIntervalSeconds);
        for (int p = 0; p < kTrailPoints; ++p) {
            Vec3 rp = r;
            Vec3 vp = v;
            const bool ok = propagate_rk4_j2_substep(
                rp,
                vp,
                -kTrailSampleIntervalSeconds,
                kTrajectoryPropMaxStepSeconds
            );
            if (!ok) {
                built_exact_trail = false;
                break;
            }
            r = rp;
            v = vp;
            push_track_point(current_epoch_s - kTrailSampleIntervalSeconds * static_cast<double>(p + 1), r);
        }

        if (built_exact_trail) {
            std::reverse(exact_trail.begin(), exact_trail.end());
            for (const auto& tp : exact_trail) {
                append_track_point(tp);
            }
        }
    }

    if (!built_exact_trail) {
        const double trail_start_epoch = current_epoch_s - kTrailWindowSeconds;
        for (const auto& tp : trail) {
            if (tp.epoch_s + 1e-9 < trail_start_epoch) {
                continue;
            }
            append_track_point(tp);
        }
    }
    out += "],\"predicted\":[";

    // Forward-propagate current state for the next 90 minutes.
    first = true;
    if (sat_idx < store.size()) {
        Vec3 r{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
        Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};
        constexpr int kPredictPoints =
            static_cast<int>(kTrailWindowSeconds / kTrailSampleIntervalSeconds);
        constexpr double kPredictInterval_s = kTrailSampleIntervalSeconds;

        for (int p = 0; p < kPredictPoints; ++p) {
            Vec3 rp = r;
            Vec3 vp = v;
            const bool ok = propagate_rk4_j2_substep(rp, vp, kPredictInterval_s, kTrajectoryPropMaxStepSeconds);
            if (!ok) break;
            r = rp;
            v = vp;

            if (!first) out += ',';
            first = false;

            const double pred_epoch = current_epoch_s + kPredictInterval_s * (p + 1);
            const Vec3 ecef = eci_to_ecef(r, pred_epoch);
            double lat = 0.0, lon = 0.0, alt = 0.0;
            ecef_to_geodetic(ecef, lat, lon, alt);

            out += "{\"epoch_s\":";
            out += fmt_double(pred_epoch, 3);
            out += ",\"lat\":";
            out += fmt_double(lat, 6);
            out += ",\"lon\":";
            out += fmt_double(lon, 6);
            out += ",\"alt_km\":";
            out += fmt_double(alt, 3);
            out += ",\"eci\":[";
            out += fmt_double(r.x, 6);
            out += ',';
            out += fmt_double(r.y, 6);
            out += ',';
            out += fmt_double(r.z, 6);
            out += "]}";
        }
    }
    out += "]}";
    return out;
}

} // namespace

EngineRuntime::EngineRuntime()
{
    step_cfg_.broad_phase.enable_dcriterion = false;
    step_cfg_.broad_phase.shadow_dcriterion = true;
    step_cfg_.broad_phase.shell_margin_km = 50.0;
    step_cfg_.broad_phase.invalid_shell_pad_km = 200.0;
    step_cfg_.broad_phase.a_bin_width_km = 500.0;
    step_cfg_.broad_phase.i_bin_width_rad = 0.3490658503988659;
    step_cfg_.broad_phase.band_neighbor_bins = 2;
    step_cfg_.broad_phase.enable_i_neighbor_filter = false;
    step_cfg_.broad_phase.high_e_fail_open = 0.2;
    step_cfg_.broad_phase.dcriterion_threshold = 2.0;

    step_cfg_.broad_phase.enable_dcriterion =
        env_u64("PROJECTBONK_BROAD_DCRITERION_ENABLE", 0, 0, 1) == 1;
    step_cfg_.broad_phase.shadow_dcriterion =
        env_u64("PROJECTBONK_BROAD_DCRITERION_SHADOW", 1, 0, 1) == 1;
    step_cfg_.broad_phase.shell_margin_km =
        env_double("PROJECTBONK_BROAD_SHELL_MARGIN_KM", 50.0, 0.0, 500.0);
    step_cfg_.broad_phase.invalid_shell_pad_km =
        env_double("PROJECTBONK_BROAD_INVALID_SHELL_PAD_KM", 200.0, 0.0, 2000.0);
    step_cfg_.broad_phase.a_bin_width_km =
        env_double("PROJECTBONK_BROAD_A_BIN_WIDTH_KM", 500.0, 10.0, 5000.0);
    step_cfg_.broad_phase.i_bin_width_rad =
        env_double("PROJECTBONK_BROAD_I_BIN_WIDTH_RAD", 0.3490658503988659, 0.01, PI);
    step_cfg_.broad_phase.band_neighbor_bins = static_cast<int>(
        env_u64("PROJECTBONK_BROAD_BAND_NEIGHBOR_BINS", 2, 0, 12)
    );
    step_cfg_.broad_phase.enable_i_neighbor_filter =
        env_u64("PROJECTBONK_BROAD_I_NEIGHBOR_FILTER", 0, 0, 1) == 1;
    step_cfg_.broad_phase.high_e_fail_open =
        env_double("PROJECTBONK_BROAD_HIGH_E_FAIL_OPEN", 0.2, 0.0, 0.99);
    step_cfg_.broad_phase.dcriterion_threshold =
        env_double("PROJECTBONK_BROAD_DCRITERION_THRESHOLD", 2.0, 0.0, 10.0);

    step_cfg_.narrow_phase.plane_phase_shadow = true;
    step_cfg_.narrow_phase.plane_phase_filter = false;
    step_cfg_.narrow_phase.plane_angle_threshold_rad = 1.3089969389957472;
    step_cfg_.narrow_phase.phase_angle_threshold_rad = 2.6179938779914944;
    step_cfg_.narrow_phase.phase_max_e = 0.2;
    step_cfg_.narrow_phase.moid_shadow = true;
    step_cfg_.narrow_phase.moid_filter = false;
    step_cfg_.narrow_phase.moid_samples = 72;
    step_cfg_.narrow_phase.moid_reject_threshold_km = 2.0;
    step_cfg_.narrow_phase.moid_max_e = 0.2;

    step_cfg_.narrow_phase.tca_guard_km =
        env_double("PROJECTBONK_NARROW_TCA_GUARD_KM", 0.02, 0.0, 1.0);
    step_cfg_.narrow_phase.refine_band_km =
        env_double("PROJECTBONK_NARROW_REFINE_BAND_KM", 0.10, 0.0, 2.0);
    step_cfg_.narrow_phase.full_refine_band_km =
        env_double("PROJECTBONK_NARROW_FULL_REFINE_BAND_KM", 0.20, 0.0, 5.0);
    step_cfg_.narrow_phase.high_rel_speed_km_s =
        env_double("PROJECTBONK_NARROW_HIGH_REL_SPEED_KM_S", 8.0, 0.0, 30.0);
    step_cfg_.narrow_phase.high_rel_speed_extra_band_km =
        env_double("PROJECTBONK_NARROW_HIGH_REL_SPEED_EXTRA_BAND_KM", 0.10, 0.0, 5.0);
    step_cfg_.narrow_phase.full_refine_budget_base =
        env_u64("PROJECTBONK_NARROW_FULL_REFINE_BUDGET_BASE", 64, 1, 4096);
    step_cfg_.narrow_phase.full_refine_budget_min =
        env_u64("PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MIN", 8, 1, 4096);
    step_cfg_.narrow_phase.full_refine_budget_max =
        env_u64("PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MAX", 192, 1, 4096);
    if (step_cfg_.narrow_phase.full_refine_budget_max < step_cfg_.narrow_phase.full_refine_budget_min) {
        step_cfg_.narrow_phase.full_refine_budget_max = step_cfg_.narrow_phase.full_refine_budget_min;
    }
    step_cfg_.narrow_phase.full_refine_samples = static_cast<std::uint32_t>(
        env_u64("PROJECTBONK_NARROW_FULL_REFINE_SAMPLES", 16, 1, 256)
    );
    step_cfg_.narrow_phase.full_refine_substep_s =
        env_double("PROJECTBONK_NARROW_FULL_REFINE_SUBSTEP_S", 1.0, 0.05, 30.0);
    step_cfg_.narrow_phase.micro_refine_max_step_s =
        env_double("PROJECTBONK_NARROW_MICRO_REFINE_MAX_STEP_S", 5.0, 0.05, 60.0);
    step_cfg_.narrow_phase.plane_phase_shadow =
        env_u64("PROJECTBONK_NARROW_PLANE_PHASE_SHADOW", 1, 0, 1) == 1;
    step_cfg_.narrow_phase.plane_phase_filter =
        env_u64("PROJECTBONK_NARROW_PLANE_PHASE_FILTER", 0, 0, 1) == 1;
    step_cfg_.narrow_phase.plane_angle_threshold_rad =
        env_double("PROJECTBONK_NARROW_PLANE_ANGLE_THRESHOLD_RAD", 1.3089969389957472, 0.0, PI);
    step_cfg_.narrow_phase.phase_angle_threshold_rad =
        env_double("PROJECTBONK_NARROW_PHASE_ANGLE_THRESHOLD_RAD", 2.6179938779914944, 0.0, PI);
    step_cfg_.narrow_phase.phase_max_e =
        env_double("PROJECTBONK_NARROW_PHASE_MAX_E", 0.2, 0.0, 0.99);
    step_cfg_.narrow_phase.moid_shadow =
        env_u64("PROJECTBONK_NARROW_MOID_SHADOW", 1, 0, 1) == 1;
    step_cfg_.narrow_phase.moid_filter =
        env_u64("PROJECTBONK_NARROW_MOID_FILTER", 0, 0, 1) == 1;
    step_cfg_.narrow_phase.moid_samples = static_cast<std::uint32_t>(
        env_u64("PROJECTBONK_NARROW_MOID_SAMPLES", 72, 6, 256)
    );
    step_cfg_.narrow_phase.moid_reject_threshold_km =
        env_double("PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM", 2.0, 0.0, 50.0);
    step_cfg_.narrow_phase.moid_max_e =
        env_double("PROJECTBONK_NARROW_MOID_MAX_E", 0.2, 0.0, 0.99);

    recovery_cfg_ = recovery_planner_config_from_env();

    http_policy_.schedule_success_status = env_int(
        "PROJECTBONK_SCHEDULE_SUCCESS_STATUS",
        202,
        200,
        299
    );
    http_policy_.max_step_seconds = env_i64(
        "PROJECTBONK_MAX_STEP_SECONDS",
        86400,
        1,
        604800
    );

    const char* queue_depth_env = std::getenv("PROJECTBONK_MAX_COMMAND_QUEUE_DEPTH");
    if (queue_depth_env != nullptr) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(queue_depth_env, &end, 10);
        if (end != nullptr && *end == '\0' && parsed > 0UL) {
            max_queue_depth_ = static_cast<std::uint64_t>(parsed);
        }
    }

    publish_read_views();

    worker_ = std::thread([this]() { worker_loop(); });
}

EngineRuntime::~EngineRuntime()
{
    {
        std::lock_guard<std::mutex> lock(command_queue_mutex_);
        stop_worker_ = true;
    }
    command_queue_cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
}

TelemetryCommandResult EngineRuntime::ingest_telemetry(const TelemetryParseResult& parsed,
                                                       std::string_view source_id)
{
    TelemetryCommand cmd;
    cmd.parsed = parsed;
    cmd.source_id = std::string(source_id);
    cmd.enqueued_at = std::chrono::steady_clock::now();
    std::future<TelemetryCommandResult> done = cmd.completion.get_future();

    {
        std::lock_guard<std::mutex> lock(command_queue_mutex_);
        if (stop_worker_) {
            queue_rejected_total_.fetch_add(1, std::memory_order_relaxed);
            TelemetryCommandResult result;
            result.ok = false;
            result.http_status = 500;
            result.error_code = "RUNTIME_STOPPED";
            result.error_message = "runtime worker is stopped";
            return result;
        }
        if (command_queue_.size() >= max_queue_depth_) {
            queue_rejected_total_.fetch_add(1, std::memory_order_relaxed);
            TelemetryCommandResult result;
            result.ok = false;
            result.http_status = 503;
            result.error_code = "RUNTIME_BUSY";
            result.error_message = "runtime command queue is full";
            return result;
        }
        command_queue_.emplace_back(std::move(cmd));
        const std::size_t depth = command_queue_.size();
        observe_queue_depth(depth);
        queue_enqueued_total_.fetch_add(1, std::memory_order_relaxed);
    }

    command_queue_cv_.notify_one();

    try {
        if (done.wait_for(k_command_timeout) != std::future_status::ready) {
            queue_timeout_total_.fetch_add(1, std::memory_order_relaxed);
            TelemetryCommandResult result;
            result.ok = false;
            result.http_status = 503;
            result.error_code = "RUNTIME_BUSY";
            result.error_message = "runtime command timed out in queue";
            return result;
        }
        return done.get();
    } catch (...) {
        TelemetryCommandResult result;
        result.ok = false;
        result.http_status = 500;
        result.error_code = "RUNTIME_QUEUE_FAILURE";
        result.error_message = "telemetry command failed in runtime queue";
        return result;
    }
}

ScheduleCommandResult EngineRuntime::schedule_maneuver(std::string_view satellite_id,
                                                       std::vector<ScheduledBurn> burns)
{
    ScheduleCommand cmd;
    cmd.satellite_id = std::string(satellite_id);
    cmd.burns = std::move(burns);
    cmd.enqueued_at = std::chrono::steady_clock::now();
    std::future<ScheduleCommandResult> done = cmd.completion.get_future();

    {
        std::lock_guard<std::mutex> lock(command_queue_mutex_);
        if (stop_worker_) {
            queue_rejected_total_.fetch_add(1, std::memory_order_relaxed);
            ScheduleCommandResult result;
            result.ok = false;
            result.http_status = 500;
            result.error_code = "RUNTIME_STOPPED";
            result.error_message = "runtime worker is stopped";
            return result;
        }
        if (command_queue_.size() >= max_queue_depth_) {
            queue_rejected_total_.fetch_add(1, std::memory_order_relaxed);
            ScheduleCommandResult result;
            result.ok = false;
            result.http_status = 503;
            result.error_code = "RUNTIME_BUSY";
            result.error_message = "runtime command queue is full";
            return result;
        }
        command_queue_.emplace_back(std::move(cmd));
        const std::size_t depth = command_queue_.size();
        observe_queue_depth(depth);
        queue_enqueued_total_.fetch_add(1, std::memory_order_relaxed);
    }

    command_queue_cv_.notify_one();

    try {
        if (done.wait_for(k_command_timeout) != std::future_status::ready) {
            queue_timeout_total_.fetch_add(1, std::memory_order_relaxed);
            ScheduleCommandResult result;
            result.ok = false;
            result.http_status = 503;
            result.error_code = "RUNTIME_BUSY";
            result.error_message = "runtime command timed out in queue";
            return result;
        }
        return done.get();
    } catch (...) {
        ScheduleCommandResult result;
        result.ok = false;
        result.http_status = 500;
        result.error_code = "RUNTIME_QUEUE_FAILURE";
        result.error_message = "schedule command failed in runtime queue";
        return result;
    }
}

StepCommandResult EngineRuntime::simulate_step(std::int64_t step_seconds)
{
    StepCommand cmd;
    cmd.step_seconds = step_seconds;
    cmd.enqueued_at = std::chrono::steady_clock::now();
    std::future<StepCommandResult> done = cmd.completion.get_future();

    {
        std::lock_guard<std::mutex> lock(command_queue_mutex_);
        if (stop_worker_) {
            queue_rejected_total_.fetch_add(1, std::memory_order_relaxed);
            StepCommandResult result;
            result.ok = false;
            result.http_status = 500;
            result.error_code = "RUNTIME_STOPPED";
            result.error_message = "runtime worker is stopped";
            return result;
        }
        if (command_queue_.size() >= max_queue_depth_) {
            queue_rejected_total_.fetch_add(1, std::memory_order_relaxed);
            StepCommandResult result;
            result.ok = false;
            result.http_status = 503;
            result.error_code = "RUNTIME_BUSY";
            result.error_message = "runtime command queue is full";
            return result;
        }
        command_queue_.emplace_back(std::move(cmd));
        const std::size_t depth = command_queue_.size();
        observe_queue_depth(depth);
        queue_enqueued_total_.fetch_add(1, std::memory_order_relaxed);
    }

    command_queue_cv_.notify_one();

    try {
        if (done.wait_for(k_command_timeout) != std::future_status::ready) {
            queue_timeout_total_.fetch_add(1, std::memory_order_relaxed);
            StepCommandResult result;
            result.ok = false;
            result.http_status = 503;
            result.error_code = "RUNTIME_BUSY";
            result.error_message = "runtime command timed out in queue";
            return result;
        }
        return done.get();
    } catch (...) {
        StepCommandResult result;
        result.ok = false;
        result.http_status = 500;
        result.error_code = "RUNTIME_QUEUE_FAILURE";
        result.error_message = "step command failed in runtime queue";
        return result;
    }
}

void EngineRuntime::worker_loop()
{
    for (;;) {
        RuntimeCommand command;
        {
            std::unique_lock<std::mutex> lock(command_queue_mutex_);
            command_queue_cv_.wait(lock, [this]() {
                return stop_worker_ || !command_queue_.empty();
            });

            if (stop_worker_ && command_queue_.empty()) {
                return;
            }

            command = std::move(command_queue_.front());
            command_queue_.pop_front();
            observe_queue_depth(command_queue_.size());
        }

        std::visit([this](auto& typed_cmd) {
            using T = std::decay_t<decltype(typed_cmd)>;

            const auto command_started_at = std::chrono::steady_clock::now();
            const auto queue_wait_raw_us = std::chrono::duration_cast<std::chrono::microseconds>(
                command_started_at - typed_cmd.enqueued_at
            ).count();
            const std::uint64_t queue_wait_us = (queue_wait_raw_us > 0)
                ? static_cast<std::uint64_t>(queue_wait_raw_us)
                : 0;

            try {
                if constexpr (std::is_same_v<T, TelemetryCommand>) {
                    typed_cmd.completion.set_value(
                        execute_ingest_telemetry(typed_cmd.parsed, typed_cmd.source_id)
                    );
                } else if constexpr (std::is_same_v<T, ScheduleCommand>) {
                    typed_cmd.completion.set_value(
                        execute_schedule_maneuver(typed_cmd.satellite_id, std::move(typed_cmd.burns))
                    );
                } else if constexpr (std::is_same_v<T, StepCommand>) {
                    typed_cmd.completion.set_value(
                        execute_simulate_step(typed_cmd.step_seconds)
                    );
                }
            } catch (...) {
                typed_cmd.completion.set_exception(std::current_exception());
            }

            const auto command_finished_at = std::chrono::steady_clock::now();
            const auto execution_raw_us = std::chrono::duration_cast<std::chrono::microseconds>(
                command_finished_at - command_started_at
            ).count();
            const std::uint64_t execution_us = (execution_raw_us > 0)
                ? static_cast<std::uint64_t>(execution_raw_us)
                : 0;

            if constexpr (std::is_same_v<T, TelemetryCommand>) {
                record_command_latency(telemetry_latency_, queue_wait_us, execution_us);
            } else if constexpr (std::is_same_v<T, ScheduleCommand>) {
                record_command_latency(schedule_latency_, queue_wait_us, execution_us);
            } else if constexpr (std::is_same_v<T, StepCommand>) {
                record_command_latency(step_latency_, queue_wait_us, execution_us);
            }
        }, command);

        publish_read_views();
        queue_completed_total_.fetch_add(1, std::memory_order_relaxed);
    }
}

void EngineRuntime::observe_queue_depth(std::size_t depth) noexcept
{
    const std::uint64_t d = static_cast<std::uint64_t>(depth);
    queue_depth_current_.store(d, std::memory_order_relaxed);

    std::uint64_t prev = queue_depth_max_.load(std::memory_order_relaxed);
    while (d > prev
           && !queue_depth_max_.compare_exchange_weak(
               prev,
               d,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void EngineRuntime::observe_latency_max(std::atomic<std::uint64_t>& metric,
                                        std::uint64_t value) noexcept
{
    std::uint64_t prev = metric.load(std::memory_order_relaxed);
    while (value > prev
           && !metric.compare_exchange_weak(
               prev,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void EngineRuntime::record_command_latency(CommandLatencyAtomicStats& stats,
                                           std::uint64_t queue_wait_us,
                                           std::uint64_t execution_us) noexcept
{
    stats.count.fetch_add(1, std::memory_order_relaxed);

    stats.queue_wait_us_total.fetch_add(queue_wait_us, std::memory_order_relaxed);
    stats.queue_wait_us_last.store(queue_wait_us, std::memory_order_relaxed);
    observe_latency_max(stats.queue_wait_us_max, queue_wait_us);

    stats.execution_us_total.fetch_add(execution_us, std::memory_order_relaxed);
    stats.execution_us_last.store(execution_us, std::memory_order_relaxed);
    observe_latency_max(stats.execution_us_max, execution_us);
}

void EngineRuntime::publish_read_views()
{
    std::shared_ptr<PublishedReadViews> next = std::make_shared<PublishedReadViews>();

    {
        std::shared_lock lock(mutex_);
        next->snapshot_json = build_snapshot(store_, clock_);
        next->conflicts_json = build_conflicts_json(store_);
        next->propagation_json = build_propagation_json(store_, prop_stats_, step_cfg_, recovery_cfg_);
        next->burns_json = build_burns_json(
            executed_burn_history_,
            dropped_burn_history_,
            burn_queue_,
            per_sat_maneuver_stats_
        );
        next->predictive_conjunctions_json = build_conjunctions_json(
            predictive_conjunction_history_,
            {},
            "predicted"
        );
    }

    std::atomic_store_explicit(&published_views_, std::const_pointer_cast<const PublishedReadViews>(next), std::memory_order_release);
}

TelemetryCommandResult EngineRuntime::execute_ingest_telemetry(const TelemetryParseResult& parsed,
                                                               std::string_view source_id)
{
    TelemetryCommandResult result;

    if (!parsed.parse_ok) {
        result.ok = false;
        result.http_status = 400;
        result.error_code = parsed.error_code;
        result.error_message = parsed.error_message;
        return result;
    }

    std::unique_lock lock(mutex_);

    const TelemetryIngestResult ingest = apply_telemetry_batch(parsed, store_, clock_, source_id);
    if (!ingest.ok) {
        result.ok = false;
        result.http_status = (ingest.error_code == "STALE_TELEMETRY") ? 409 : 422;
        result.error_code = ingest.error_code;
        result.error_message = ingest.error_message;
        return result;
    }

    for (const auto& obj : parsed.valid_objects) {
        if (obj.type != ObjectType::SATELLITE) continue;
        const std::size_t idx = store_.find(obj.id);
        if (idx >= store_.size()) continue;

        if (slot_reference_by_sat_.find(obj.id) == slot_reference_by_sat_.end()) {
            OrbitalElements slot_ref{};
            if (cascade::get_current_elements(store_, idx, slot_ref)) {
                SlotReference ref;
                ref.elements = slot_ref;
                ref.reference_epoch_s = store_.telemetry_epoch_s(idx);
                ref.bootstrapped_from_telemetry = true;
                slot_reference_by_sat_[obj.id] = ref;
            }
        }

        if (store_.fuel_kg(idx) <= SAT_FUEL_EOL_KG + EPS_NUM) {
            graveyard_requested_by_sat_[obj.id] = true;
        }
    }

    result.ok = true;
    result.http_status = 200;
    result.processed_count = ingest.processed_count;
    result.active_cdm_warnings = cdm_warnings_count_;
    return result;
}

ScheduleCommandResult EngineRuntime::execute_schedule_maneuver(std::string_view satellite_id,
                                                               std::vector<ScheduledBurn> burns)
{
    ScheduleCommandResult result;

    if (burns.empty()) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "EMPTY_MANEUVER_SEQUENCE";
        result.error_message = "at least one burn is required";
        return result;
    }

    std::sort(burns.begin(), burns.end(),
              [](const ScheduledBurn& a, const ScheduledBurn& b) {
                  return a.burn_epoch_s < b.burn_epoch_s;
              });

    for (std::size_t i = 1; i < burns.size(); ++i) {
        const double dt = burns[i].burn_epoch_s - burns[i - 1].burn_epoch_s;
        if (dt + EPS_NUM < SAT_COOLDOWN_S) {
            result.ok = false;
            result.http_status = 422;
            result.error_code = "COOLDOWN_VIOLATION";
            result.error_message = "maneuver sequence violates 600s cooldown";
            return result;
        }
    }

    const std::string sat_id(satellite_id);
    std::unique_lock lock(mutex_);

    const double clock_epoch_s = clock_.epoch_s();
    const std::size_t idx = store_.find(sat_id);
    if (idx >= store_.size() || store_.type(idx) != ObjectType::SATELLITE) {
        result.ok = false;
        result.http_status = 404;
        result.error_code = "SATELLITE_NOT_FOUND";
        result.error_message = "satelliteId does not reference a known SATELLITE object";
        return result;
    }

    const bool sat_operational = (store_.sat_status(idx) != SatStatus::OFFLINE);
    if (!sat_operational) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "SATELLITE_OFFLINE";
        result.error_message = "satellite is offline after graveyard maneuver";
        return result;
    }

    if (graveyard_requested_by_sat_[sat_id]) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "GRAVEYARD_PENDING";
        result.error_message = "satellite is reserved for graveyard transfer due to EOL fuel";
        return result;
    }

    if (slot_reference_by_sat_.find(sat_id) == slot_reference_by_sat_.end()) {
        OrbitalElements slot_ref{};
        if (cascade::get_current_elements(store_, idx, slot_ref)) {
            SlotReference ref;
            ref.elements = slot_ref;
            ref.reference_epoch_s = clock_.epoch_s();
            ref.bootstrapped_from_telemetry = false; // manual schedule path
            slot_reference_by_sat_[sat_id] = ref;
        }
    }

    bool cooldown_ok = true;
    bool command_conflict = false;
    const auto last_it = last_burn_epoch_by_sat_.find(sat_id);
    const double last_burn_epoch = (last_it == last_burn_epoch_by_sat_.end())
        ? -1.0e30
        : last_it->second;

    const double from_last = burns.front().burn_epoch_s - last_burn_epoch;
    if (from_last + EPS_NUM < SAT_COOLDOWN_S) {
        cooldown_ok = false;
        command_conflict = true;
    }

    if (cooldown_ok) {
        for (const ScheduledBurn& queued : burn_queue_) {
            if (queued.satellite_id != sat_id) continue;
            for (const ScheduledBurn& incoming : burns) {
                const double dt = std::abs(incoming.burn_epoch_s - queued.burn_epoch_s);
                if (dt + EPS_NUM < SAT_COOLDOWN_S) {
                    cooldown_ok = false;
                    command_conflict = true;
                    break;
                }
            }
            if (!cooldown_ok) break;
        }
    }

    bool upload_window_ok = true;
    std::vector<double> parsed_upload_epochs(burns.size(), 0.0);
    std::vector<std::string> parsed_upload_stations(burns.size(), std::string{});
    for (std::size_t bi = 0; bi < burns.size(); ++bi) {
        const ScheduledBurn& b = burns[bi];
        if (b.burn_epoch_s <= clock_epoch_s + ops_signal_latency_s() + EPS_NUM) {
            upload_window_ok = false;
            break;
        }
        double upload_epoch = 0.0;
        std::string upload_station;
        if (!cascade::compute_upload_plan_for_burn(
                store_,
                idx,
                clock_epoch_s,
                b.burn_epoch_s,
                upload_epoch,
                upload_station)) {
            upload_window_ok = false;
            break;
        }
        parsed_upload_epochs[bi] = upload_epoch;
        parsed_upload_stations[bi] = upload_station;
    }

    double projected_fuel_remaining = store_.fuel_kg(idx);
    double projected_mass_remaining = store_.mass_kg(idx);
    for (const ScheduledBurn& b : burns) {
        const double fuel_need = cascade::propellant_used_kg(projected_mass_remaining, b.delta_v_norm_km_s);
        projected_fuel_remaining -= fuel_need;
        projected_mass_remaining -= fuel_need;
    }

    if (!upload_window_ok) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "UPLOAD_WINDOW_UNAVAILABLE";
        result.error_message = "no valid upload window with 10s latency for one or more burn times";
        return result;
    }

    if (!cooldown_ok) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "COOLDOWN_VIOLATION";
        result.error_message = "maneuver sequence violates 600s cooldown";
        return result;
    }

    if (!(projected_fuel_remaining > SAT_FUEL_EOL_KG)) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "INSUFFICIENT_FUEL";
        result.error_message = "projected fuel after burns is below EOL threshold";
        return result;
    }

    for (std::size_t bi = 0; bi < burns.size(); ++bi) {
        ScheduledBurn b = burns[bi];
        b.upload_epoch_s = parsed_upload_epochs[bi];
        b.upload_station_id = parsed_upload_stations[bi];
        b.cooldown_conflict = !cooldown_ok;
        b.command_conflict = command_conflict;
        b.blackout_overlap = cascade::burn_overlaps_blackout(store_, idx, b.burn_epoch_s);
        burn_queue_.push_back(std::move(b));
    }
    recovery_requests_by_sat_.erase(sat_id);
    graveyard_requested_by_sat_[sat_id] = false;

    result.ok = true;
    result.http_status = http_policy_.schedule_success_status;
    result.projected_mass_remaining_kg = projected_mass_remaining;
    result.ground_station_los = true;
    result.sufficient_fuel = true;
    return result;
}

StepCommandResult EngineRuntime::execute_simulate_step(std::int64_t step_seconds)
{
    StepCommandResult result;

    if (step_seconds <= 0) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "INVALID_STEP_SECONDS";
        result.error_message = "step_seconds must be > 0";
        return result;
    }

    if (step_seconds > http_policy_.max_step_seconds) {
        result.ok = false;
        result.http_status = 422;
        result.error_code = "STEP_SECONDS_EXCEEDS_LIMIT";
        result.error_message = "step_seconds exceeds configured max limit";
        return result;
    }

    const double step_s = static_cast<double>(step_seconds);
    StepRunStats stats{};
    std::string new_ts;

    {
        std::unique_lock lock(mutex_);

        if (!clock_.is_initialized()) {
            result.ok = false;
            result.http_status = 400;
            result.error_code = "CLOCK_UNINITIALIZED";
            result.error_message = "ingest telemetry before running simulate/step";
            return result;
        }

        const bool ran = run_simulation_step(store_, clock_, step_s, stats, step_cfg_);
        if (!ran) {
            result.ok = false;
            result.http_status = 422;
            result.error_code = "SIM_STEP_REJECTED";
            result.error_message = "simulation step could not be executed";
            return result;
        }

        const std::uint64_t tick_id = static_cast<std::uint64_t>(tick_count_.load()) + 1;

        // 24-hour predictive CDM scan (PS.md §3: "forecast potential
        // collisions up to 24 hours in the future").
        const CdmWarningScanResult cdm_scan = compute_cdm_warnings_24h(
            store_,
            clock_.epoch_s(),
            step_cfg_.broad_phase
        );
        cdm_warnings_count_ = cdm_scan.warning_count;
        predictive_conjunction_history_.clear();
        for (const auto& ce : cdm_scan.records) {
            predictive_conjunction_history_.push_back(ce);

            // Only CRITICAL severity records trigger auto-COLA.
            // WARNING/WATCH are informational for the UI only.
            if (ce.severity != CdmSeverity::CRITICAL) {
                continue;
            }

            const std::size_t sat_idx = store_.find(ce.satellite_id);
            if (sat_idx >= store_.size()) {
                continue;
            }
            const auto sat_idx_u32 = static_cast<std::uint32_t>(sat_idx);
            if (std::find(stats.collision_sat_indices.begin(),
                          stats.collision_sat_indices.end(),
                          sat_idx_u32) == stats.collision_sat_indices.end()) {
                stats.collision_sat_indices.push_back(sat_idx_u32);
            }
        }
        std::sort(stats.collision_sat_indices.begin(), stats.collision_sat_indices.end());

        const std::uint64_t auto_planned = plan_collision_avoidance_burns(
            store_,
            stats,
            cdm_scan.records,
            clock_.epoch_s(),
            tick_id,
            burn_queue_,
            last_burn_epoch_by_sat_,
            graveyard_completed_by_sat_,
            graveyard_requested_by_sat_
        );

        std::uint64_t upload_missed_tick = 0;
        std::vector<ScheduledBurn> dropped_burns_tick;
        cascade::validate_pending_upload_windows(
            store_,
            clock_.epoch_s(),
            burn_queue_,
            upload_missed_tick,
            &dropped_burns_tick
        );

        double slot_error_sum_tick = 0.0;
        double slot_error_max_tick = 0.0;
        std::uint64_t slot_error_samples_tick = 0;

        double slot_radius_err_sum_tick = 0.0;
        double slot_radius_err_max_tick = 0.0;
        std::uint64_t slot_radius_err_samples_tick = 0;
        std::uint64_t stationkeeping_outside_box_tick = 0;
        double stationkeeping_uptime_penalty_sum_tick = 0.0;
        std::uint64_t stationkeeping_uptime_penalty_samples_tick = 0;

        const std::uint64_t stationkeeping_recovery_planned = enforce_stationkeeping_recovery(
            clock_.epoch_s(),
            tick_id,
            slot_error_sum_tick,
            slot_error_max_tick,
            slot_error_samples_tick,
            slot_radius_err_sum_tick,
            slot_radius_err_max_tick,
            slot_radius_err_samples_tick,
            stationkeeping_outside_box_tick,
            stationkeeping_uptime_penalty_sum_tick,
            stationkeeping_uptime_penalty_samples_tick
        );

        // Station-keeping path only proposes corrective burns after slot-box
        // breach and still obeys cooldown/LOS/fuel safety checks.

        // --- Phase 0D: Snapshot burn queue + satellite fuel for burn capture ---
        std::unordered_map<std::string, double> fuel_snapshot_before;
        std::vector<ScheduledBurn> burns_before = burn_queue_;
        for (const auto& b : burns_before) {
            if (fuel_snapshot_before.find(b.satellite_id) == fuel_snapshot_before.end()) {
                const std::size_t si = store_.find(b.satellite_id);
                if (si < store_.size()) {
                    fuel_snapshot_before[b.satellite_id] = store_.fuel_kg(si);
                }
            }
        }

        const ManeuverExecStats exec_stats = cascade::execute_due_maneuvers(
            store_,
            clock_.epoch_s(),
            burn_queue_,
            last_burn_epoch_by_sat_,
            recovery_requests_by_sat_,
            graveyard_requested_by_sat_,
            graveyard_completed_by_sat_,
            &dropped_burns_tick
        );
        upload_missed_tick += exec_stats.upload_missed;
        stats.maneuvers_executed = exec_stats.executed;

        for (auto& dropped : dropped_burns_tick) {
            dropped_burn_history_.push_back(std::move(dropped));
            while (dropped_burn_history_.size() > kMaxDroppedBurnHistory) {
                dropped_burn_history_.pop_front();
            }
        }

        // --- Phase 0D: Identify executed burns and record them ---
        if (exec_stats.executed > 0) {
            // Build set of remaining burn IDs
            std::unordered_map<std::string, bool> remaining_ids;
            for (const auto& b : burn_queue_) {
                remaining_ids[b.id] = true;
            }
            // Any burn from before that's not in remaining was executed
            for (const auto& b : burns_before) {
                if (remaining_ids.find(b.id) != remaining_ids.end()) continue;
                ExecutedBurn eb;
                eb.id = b.id;
                eb.satellite_id = b.satellite_id;
                eb.upload_station_id = b.upload_station_id;
                eb.upload_epoch_s = b.upload_epoch_s;
                eb.burn_epoch_s = b.burn_epoch_s;
                eb.delta_v_km_s = b.delta_v_km_s;
                eb.delta_v_norm_km_s = b.delta_v_norm_km_s;
                eb.auto_generated = b.auto_generated;
                eb.recovery_burn = b.recovery_burn;
                eb.graveyard_burn = b.graveyard_burn;
                eb.blackout_overlap = b.blackout_overlap;
                eb.cooldown_conflict = b.cooldown_conflict;
                eb.command_conflict = b.command_conflict;
                eb.scheduled_from_predictive_cdm = b.scheduled_from_predictive_cdm;
                eb.trigger_debris_id = b.trigger_debris_id;
                eb.trigger_tca_epoch_s = b.trigger_tca_epoch_s;
                eb.trigger_miss_distance_km = b.trigger_miss_distance_km;
                eb.trigger_approach_speed_km_s = b.trigger_approach_speed_km_s;
                eb.trigger_fail_open = b.trigger_fail_open;
                // Fuel before from snapshot; fuel after from current store
                auto fit = fuel_snapshot_before.find(b.satellite_id);
                eb.fuel_before_kg = (fit != fuel_snapshot_before.end()) ? fit->second : 0.0;
                const std::size_t si = store_.find(b.satellite_id);
                eb.fuel_after_kg = (si < store_.size()) ? store_.fuel_kg(si) : 0.0;

                const MitigationEvaluationResult mitigation = evaluate_predictive_mitigation(
                    store_,
                    b,
                    clock_.epoch_s()
                );
                eb.mitigation_tracked = mitigation.tracked;
                eb.mitigation_evaluated = mitigation.evaluated;
                eb.collision_avoided = mitigation.collision_avoided;
                eb.mitigation_eval_epoch_s = mitigation.eval_epoch_s;
                eb.mitigation_miss_distance_km = mitigation.miss_distance_km;
                eb.mitigation_fail_open = mitigation.fail_open;

                executed_burn_history_.push_back(std::move(eb));
                while (executed_burn_history_.size() > kMaxExecutedBurnHistory) {
                    executed_burn_history_.pop_front();
                }
                // Update per-sat stats
                auto& ps = per_sat_maneuver_stats_[b.satellite_id];
                ++ps.burns_executed;
                ps.delta_v_total_km_s += b.delta_v_norm_km_s;
                const double burn_fuel_kg = burn_fuel_consumed_kg(executed_burn_history_.back());
                ps.fuel_consumed_kg += burn_fuel_kg;
                if (is_avoidance_burn(b)) {
                    ++ps.avoidance_burns_executed;
                    ps.avoidance_fuel_consumed_kg += burn_fuel_kg;
                    if (executed_burn_history_.back().collision_avoided) {
                        ++ps.collisions_avoided;
                    }
                }
                if (b.recovery_burn) {
                    ++ps.recovery_burns_executed;
                }
                if (b.graveyard_burn) {
                    ++ps.graveyard_burns_executed;
                }
            }
        }

        const RecoveryPlanStats rec_plan = cascade::plan_recovery_burns(
            store_,
            clock_.epoch_s(),
            tick_id,
            burn_queue_,
            last_burn_epoch_by_sat_,
            recovery_requests_by_sat_,
            graveyard_requested_by_sat_,
            slot_reference_by_sat_,
            ops_auto_upload_horizon_s(),
            recovery_cfg_
        );

        const GraveyardPlanStats grave_plan = cascade::plan_graveyard_burns(
            store_,
            clock_.epoch_s(),
            tick_id,
            burn_queue_,
            graveyard_requested_by_sat_,
            graveyard_completed_by_sat_
        );

        prop_stats_.fast_last_tick = stats.used_fast;
        prop_stats_.rk4_last_tick = stats.used_rk4;
        prop_stats_.escalated_last_tick = stats.escalated_after_probe;
        prop_stats_.narrow_pairs_last_tick = stats.narrow_pairs_checked;
        prop_stats_.collisions_last_tick = stats.collisions_detected;
        prop_stats_.maneuvers_last_tick = stats.maneuvers_executed;
        prop_stats_.narrow_refined_pairs_last_tick = stats.narrow_refined_pairs;
        prop_stats_.narrow_refine_cleared_last_tick = stats.narrow_refine_cleared;
        prop_stats_.narrow_refine_fail_open_last_tick = stats.narrow_refine_fail_open;
        prop_stats_.narrow_full_refined_pairs_last_tick = stats.narrow_full_refined_pairs;
        prop_stats_.narrow_full_refine_cleared_last_tick = stats.narrow_full_refine_cleared;
        prop_stats_.narrow_full_refine_fail_open_last_tick = stats.narrow_full_refine_fail_open;
        prop_stats_.narrow_full_refine_budget_allocated_last_tick = stats.narrow_full_refine_budget_allocated;
        prop_stats_.narrow_full_refine_budget_exhausted_last_tick = stats.narrow_full_refine_budget_exhausted;
        prop_stats_.narrow_uncertainty_promoted_pairs_last_tick = stats.narrow_uncertainty_promoted_pairs;
        prop_stats_.narrow_plane_phase_evaluated_pairs_last_tick = stats.narrow_plane_phase_evaluated_pairs;
        prop_stats_.narrow_plane_phase_shadow_rejected_pairs_last_tick = stats.narrow_plane_phase_shadow_rejected_pairs;
        prop_stats_.narrow_plane_phase_hard_rejected_pairs_last_tick = stats.narrow_plane_phase_hard_rejected_pairs;
        prop_stats_.narrow_plane_phase_fail_open_pairs_last_tick = stats.narrow_plane_phase_fail_open_pairs;
        prop_stats_.narrow_plane_phase_reject_reason_plane_angle_last_tick = stats.narrow_plane_phase_reject_reason_plane_angle_total;
        prop_stats_.narrow_plane_phase_reject_reason_phase_angle_last_tick = stats.narrow_plane_phase_reject_reason_phase_angle_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_elements_invalid_last_tick = stats.narrow_plane_phase_fail_open_reason_elements_invalid_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_eccentricity_guard_last_tick = stats.narrow_plane_phase_fail_open_reason_eccentricity_guard_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_non_finite_state_last_tick = stats.narrow_plane_phase_fail_open_reason_non_finite_state_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_last_tick = stats.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_last_tick = stats.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_last_tick = stats.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_uncertainty_override_last_tick = stats.narrow_plane_phase_fail_open_reason_uncertainty_override_total;
        prop_stats_.narrow_moid_evaluated_pairs_last_tick = stats.narrow_moid_evaluated_pairs;
        prop_stats_.narrow_moid_shadow_rejected_pairs_last_tick = stats.narrow_moid_shadow_rejected_pairs;
        prop_stats_.narrow_moid_hard_rejected_pairs_last_tick = stats.narrow_moid_hard_rejected_pairs;
        prop_stats_.narrow_moid_fail_open_pairs_last_tick = stats.narrow_moid_fail_open_pairs;
        prop_stats_.narrow_moid_reject_reason_distance_threshold_last_tick = stats.narrow_moid_reject_reason_distance_threshold_total;
        prop_stats_.narrow_moid_fail_open_reason_elements_invalid_last_tick = stats.narrow_moid_fail_open_reason_elements_invalid_total;
        prop_stats_.narrow_moid_fail_open_reason_eccentricity_guard_last_tick = stats.narrow_moid_fail_open_reason_eccentricity_guard_total;
        prop_stats_.narrow_moid_fail_open_reason_non_finite_state_last_tick = stats.narrow_moid_fail_open_reason_non_finite_state_total;
        prop_stats_.narrow_moid_fail_open_reason_sampling_failure_last_tick = stats.narrow_moid_fail_open_reason_sampling_failure_total;
        prop_stats_.narrow_moid_fail_open_reason_hf_placeholder_last_tick = stats.narrow_moid_fail_open_reason_hf_placeholder_total;
        prop_stats_.narrow_moid_fail_open_reason_uncertainty_override_last_tick = stats.narrow_moid_fail_open_reason_uncertainty_override_total;
        prop_stats_.narrow_refine_fail_open_reason_rk4_failure_last_tick = stats.narrow_refine_fail_open_reason_rk4_failure_total;
        prop_stats_.narrow_full_refine_fail_open_reason_rk4_failure_last_tick = stats.narrow_full_refine_fail_open_reason_rk4_failure_total;
        prop_stats_.narrow_full_refine_fail_open_reason_budget_exhausted_last_tick = stats.narrow_full_refine_fail_open_reason_budget_exhausted_total;
        prop_stats_.auto_planned_last_tick = auto_planned + stationkeeping_recovery_planned;
        prop_stats_.recovery_pending_marked_last_tick = exec_stats.recovery_pending_marked;
        prop_stats_.recovery_planned_last_tick = rec_plan.planned;
        prop_stats_.recovery_deferred_last_tick = rec_plan.deferred;
        prop_stats_.recovery_completed_last_tick = exec_stats.recovery_completed;
        prop_stats_.graveyard_planned_last_tick = grave_plan.planned;
        prop_stats_.graveyard_deferred_last_tick = grave_plan.deferred;
        prop_stats_.graveyard_completed_last_tick = exec_stats.graveyard_completed;
        prop_stats_.upload_missed_last_tick = upload_missed_tick;
        prop_stats_.stationkeeping_outside_box_last_tick = stationkeeping_outside_box_tick;
        prop_stats_.stationkeeping_uptime_penalty_mean_last_tick =
            (stationkeeping_uptime_penalty_samples_tick > 0)
            ? (stationkeeping_uptime_penalty_sum_tick
                / static_cast<double>(stationkeeping_uptime_penalty_samples_tick))
            : 0.0;
        prop_stats_.stationkeeping_slot_radius_error_mean_km_last_tick =
            (slot_radius_err_samples_tick > 0)
            ? (slot_radius_err_sum_tick / static_cast<double>(slot_radius_err_samples_tick))
            : 0.0;
        prop_stats_.stationkeeping_slot_radius_error_max_km_last_tick = slot_radius_err_max_tick;
        prop_stats_.recovery_slot_error_mean_last_tick =
            (slot_error_samples_tick > 0)
            ? (slot_error_sum_tick / static_cast<double>(slot_error_samples_tick))
            : 0.0;
        prop_stats_.recovery_slot_error_max_last_tick = slot_error_max_tick;
        prop_stats_.broad_pairs_last_tick = stats.broad_pairs_considered;
        prop_stats_.broad_candidates_last_tick = stats.broad_candidates;
        prop_stats_.broad_overlap_pass_last_tick = stats.broad_shell_overlap_pass;
        prop_stats_.broad_dcriterion_rejected_last_tick = stats.broad_dcriterion_rejected;
        prop_stats_.broad_dcriterion_shadow_rejected_last_tick = stats.broad_dcriterion_shadow_rejected;
        prop_stats_.broad_fail_open_objects_last_tick = stats.broad_fail_open_objects;
        prop_stats_.broad_fail_open_satellites_last_tick = stats.broad_fail_open_satellites;
        prop_stats_.broad_shell_margin_km_last_tick = stats.broad_shell_margin_km;
        prop_stats_.broad_dcriterion_enabled_last_tick = stats.broad_dcriterion_enabled;
        prop_stats_.broad_a_bin_width_km_last_tick = stats.broad_a_bin_width_km;
        prop_stats_.broad_band_neighbor_bins_last_tick = stats.broad_band_neighbor_bins;

        prop_stats_.fast_total += stats.used_fast;
        prop_stats_.rk4_total += stats.used_rk4;
        prop_stats_.escalated_total += stats.escalated_after_probe;
        prop_stats_.narrow_pairs_total += stats.narrow_pairs_checked;
        prop_stats_.collisions_total += stats.collisions_detected;
        prop_stats_.maneuvers_total += stats.maneuvers_executed;
        prop_stats_.narrow_refined_pairs_total += stats.narrow_refined_pairs;
        prop_stats_.narrow_refine_cleared_total += stats.narrow_refine_cleared;
        prop_stats_.narrow_refine_fail_open_total += stats.narrow_refine_fail_open;
        prop_stats_.narrow_full_refined_pairs_total += stats.narrow_full_refined_pairs;
        prop_stats_.narrow_full_refine_cleared_total += stats.narrow_full_refine_cleared;
        prop_stats_.narrow_full_refine_fail_open_total += stats.narrow_full_refine_fail_open;
        prop_stats_.narrow_full_refine_budget_allocated_total += stats.narrow_full_refine_budget_allocated;
        prop_stats_.narrow_full_refine_budget_exhausted_total += stats.narrow_full_refine_budget_exhausted;
        prop_stats_.narrow_uncertainty_promoted_pairs_total += stats.narrow_uncertainty_promoted_pairs;
        prop_stats_.narrow_plane_phase_evaluated_pairs_total += stats.narrow_plane_phase_evaluated_pairs;
        prop_stats_.narrow_plane_phase_shadow_rejected_pairs_total += stats.narrow_plane_phase_shadow_rejected_pairs;
        prop_stats_.narrow_plane_phase_hard_rejected_pairs_total += stats.narrow_plane_phase_hard_rejected_pairs;
        prop_stats_.narrow_plane_phase_fail_open_pairs_total += stats.narrow_plane_phase_fail_open_pairs;
        prop_stats_.narrow_plane_phase_reject_reason_plane_angle_total += stats.narrow_plane_phase_reject_reason_plane_angle_total;
        prop_stats_.narrow_plane_phase_reject_reason_phase_angle_total += stats.narrow_plane_phase_reject_reason_phase_angle_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_elements_invalid_total += stats.narrow_plane_phase_fail_open_reason_elements_invalid_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_eccentricity_guard_total += stats.narrow_plane_phase_fail_open_reason_eccentricity_guard_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_non_finite_state_total += stats.narrow_plane_phase_fail_open_reason_non_finite_state_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total += stats.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total += stats.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total += stats.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total;
        prop_stats_.narrow_plane_phase_fail_open_reason_uncertainty_override_total += stats.narrow_plane_phase_fail_open_reason_uncertainty_override_total;
        prop_stats_.narrow_moid_evaluated_pairs_total += stats.narrow_moid_evaluated_pairs;
        prop_stats_.narrow_moid_shadow_rejected_pairs_total += stats.narrow_moid_shadow_rejected_pairs;
        prop_stats_.narrow_moid_hard_rejected_pairs_total += stats.narrow_moid_hard_rejected_pairs;
        prop_stats_.narrow_moid_fail_open_pairs_total += stats.narrow_moid_fail_open_pairs;
        prop_stats_.narrow_moid_reject_reason_distance_threshold_total += stats.narrow_moid_reject_reason_distance_threshold_total;
        prop_stats_.narrow_moid_fail_open_reason_elements_invalid_total += stats.narrow_moid_fail_open_reason_elements_invalid_total;
        prop_stats_.narrow_moid_fail_open_reason_eccentricity_guard_total += stats.narrow_moid_fail_open_reason_eccentricity_guard_total;
        prop_stats_.narrow_moid_fail_open_reason_non_finite_state_total += stats.narrow_moid_fail_open_reason_non_finite_state_total;
        prop_stats_.narrow_moid_fail_open_reason_sampling_failure_total += stats.narrow_moid_fail_open_reason_sampling_failure_total;
        prop_stats_.narrow_moid_fail_open_reason_hf_placeholder_total += stats.narrow_moid_fail_open_reason_hf_placeholder_total;
        prop_stats_.narrow_moid_fail_open_reason_uncertainty_override_total += stats.narrow_moid_fail_open_reason_uncertainty_override_total;
        prop_stats_.narrow_refine_fail_open_reason_rk4_failure_total += stats.narrow_refine_fail_open_reason_rk4_failure_total;
        prop_stats_.narrow_full_refine_fail_open_reason_rk4_failure_total += stats.narrow_full_refine_fail_open_reason_rk4_failure_total;
        prop_stats_.narrow_full_refine_fail_open_reason_budget_exhausted_total += stats.narrow_full_refine_fail_open_reason_budget_exhausted_total;
        prop_stats_.auto_planned_total += auto_planned + stationkeeping_recovery_planned;
        prop_stats_.recovery_pending_marked_total += exec_stats.recovery_pending_marked;
        prop_stats_.recovery_planned_total += rec_plan.planned;
        prop_stats_.recovery_deferred_total += rec_plan.deferred;
        prop_stats_.recovery_completed_total += exec_stats.recovery_completed;
        prop_stats_.graveyard_planned_total += grave_plan.planned;
        prop_stats_.graveyard_deferred_total += grave_plan.deferred;
        prop_stats_.graveyard_completed_total += exec_stats.graveyard_completed;
        prop_stats_.upload_missed_total += upload_missed_tick;
        prop_stats_.stationkeeping_outside_box_total += stationkeeping_outside_box_tick;
        prop_stats_.stationkeeping_uptime_penalty_sum_total += stationkeeping_uptime_penalty_sum_tick;
        prop_stats_.stationkeeping_uptime_penalty_samples_total += stationkeeping_uptime_penalty_samples_tick;
        prop_stats_.stationkeeping_slot_radius_error_sum_total += slot_radius_err_sum_tick;
        prop_stats_.stationkeeping_slot_radius_error_samples_total += slot_radius_err_samples_tick;
        if (slot_radius_err_max_tick > prop_stats_.stationkeeping_slot_radius_error_max_total) {
            prop_stats_.stationkeeping_slot_radius_error_max_total = slot_radius_err_max_tick;
        }
        prop_stats_.recovery_slot_error_sum_total += slot_error_sum_tick;
        prop_stats_.recovery_slot_error_samples_total += slot_error_samples_tick;
        if (slot_error_max_tick > prop_stats_.recovery_slot_error_max_total) {
            prop_stats_.recovery_slot_error_max_total = slot_error_max_tick;
        }
        prop_stats_.broad_pairs_total += stats.broad_pairs_considered;
        prop_stats_.broad_candidates_total += stats.broad_candidates;
        prop_stats_.broad_overlap_pass_total += stats.broad_shell_overlap_pass;
        prop_stats_.broad_dcriterion_rejected_total += stats.broad_dcriterion_rejected;
        prop_stats_.broad_dcriterion_shadow_rejected_total += stats.broad_dcriterion_shadow_rejected;
        prop_stats_.broad_fail_open_objects_total += stats.broad_fail_open_objects;
        prop_stats_.broad_fail_open_satellites_total += stats.broad_fail_open_satellites;

        // --- Phase 0D: Record conjunction events from this step ---
        for (auto& ce : stats.conjunction_events) {
            ce.tick_id = tick_id;
            conjunction_history_.push_back(std::move(ce));
        }
        while (conjunction_history_.size() > kMaxConjunctionHistory) {
            conjunction_history_.pop_front();
        }

        // --- Phase 0D: Record 90-minute trajectory history for all satellites ---
        {
            const double epoch_s = clock_.epoch_s();
            const double trail_start_epoch = epoch_s - (90.0 * 60.0);
            for (std::size_t i = 0; i < store_.size(); ++i) {
                if (store_.type(i) != ObjectType::SATELLITE) continue;
                const auto& sid = store_.id(i);
                const Vec3 eci{store_.rx(i), store_.ry(i), store_.rz(i)};
                const Vec3 ecef = eci_to_ecef(eci, epoch_s);
                double lat = 0.0, lon = 0.0, alt = 0.0;
                ecef_to_geodetic(ecef, lat, lon, alt);
                TrackPoint tp;
                tp.epoch_s = epoch_s;
                tp.lat_deg = lat;
                tp.lon_deg = lon;
                tp.alt_km = alt;
                tp.eci_km = eci;
                tp.vel_km_s = {store_.vx(i), store_.vy(i), store_.vz(i)};
                auto& trail = trajectory_by_sat_[sid];
                while (!trail.empty() && trail.front().epoch_s + 1e-9 < trail_start_epoch) {
                    trail.pop_front();
                }
                if (!trail.empty() && std::abs(trail.back().epoch_s - tp.epoch_s) < 1e-6) {
                    trail.back() = std::move(tp);
                    continue;
                }
                trail.push_back(std::move(tp));
                while (trail.size() > kMaxTrackPointsPerSat) {
                    trail.pop_front();
                }
            }
        }

        new_ts = clock_.to_iso();
    }

    ++tick_count_;

    result.ok = true;
    result.http_status = 200;
    result.new_timestamp = new_ts;
    result.collisions_detected = stats.collisions_detected;
    result.maneuvers_executed = stats.maneuvers_executed;
    return result;
}

std::uint64_t EngineRuntime::enforce_stationkeeping_recovery(double epoch_s,
                                                             std::uint64_t tick_id,
                                                             double& slot_error_sum_tick,
                                                             double& slot_error_max_tick,
                                                             std::uint64_t& slot_error_samples_tick,
                                                             double& slot_radius_err_sum_tick,
                                                             double& slot_radius_err_max_tick,
                                                             std::uint64_t& slot_radius_err_samples_tick,
                                                             std::uint64_t& stationkeeping_outside_box_tick,
                                                             double& stationkeeping_uptime_penalty_sum_tick,
                                                             std::uint64_t& stationkeeping_uptime_penalty_samples_tick)
{
    std::uint64_t planned = 0;

    for (std::size_t i = 0; i < store_.size(); ++i) {
        if (store_.type(i) != ObjectType::SATELLITE) continue;
        if (store_.sat_status(i) == SatStatus::OFFLINE) continue;

        const auto& sat_id = store_.id(i);
        const auto it_slot = slot_reference_by_sat_.find(sat_id);
        if (it_slot == slot_reference_by_sat_.end()) {
            continue;
        }

        OrbitalElements cur{};
        if (!cascade::get_current_elements(store_, i, cur)) {
            continue;
        }

        const double slot_err = cascade::slot_error_score(it_slot->second.elements, cur);
        slot_error_sum_tick += slot_err;
        if (slot_err > slot_error_max_tick) {
            slot_error_max_tick = slot_err;
        }
        ++slot_error_samples_tick;

        double radius_err_km = 0.0;
        if (!cascade::slot_radius_error_km_at_epoch(
                store_,
                i,
                epoch_s,
                slot_reference_by_sat_,
                radius_err_km)) {
            continue;
        }

        slot_radius_err_sum_tick += radius_err_km;
        if (radius_err_km > slot_radius_err_max_tick) {
            slot_radius_err_max_tick = radius_err_km;
        }
        ++slot_radius_err_samples_tick;

        double penalty = 0.0;
        if (radius_err_km > ops_stationkeeping_box_radius_km()) {
            ++stationkeeping_outside_box_tick;
            penalty = std::exp((radius_err_km - ops_stationkeeping_box_radius_km())
                             / ops_stationkeeping_box_radius_km()) - 1.0;
        }
        stationkeeping_uptime_penalty_sum_tick += penalty;
        ++stationkeeping_uptime_penalty_samples_tick;

        if (radius_err_km <= ops_stationkeeping_box_radius_km()) {
            continue;
        }

        if (graveyard_requested_by_sat_[sat_id]) {
            continue;
        }

        auto grave_done_it = graveyard_completed_by_sat_.find(sat_id);
        if (grave_done_it != graveyard_completed_by_sat_.end() && grave_done_it->second) {
            continue;
        }

        if (cascade::has_any_pending_burn(burn_queue_, sat_id)) {
            continue;
        }

        const auto last_it = last_burn_epoch_by_sat_.find(sat_id);
        if (last_it != last_burn_epoch_by_sat_.end()) {
            const double dt = epoch_s - last_it->second;
            if (dt + EPS_NUM < SAT_COOLDOWN_S) {
                continue;
            }
        }
        if (cascade::has_pending_burn_in_cooldown_window(burn_queue_, sat_id, epoch_s)) {
            continue;
        }

        RecoveryRequest& req = recovery_requests_by_sat_[sat_id];
        if (req.earliest_epoch_s < epoch_s) {
            req.earliest_epoch_s = epoch_s;
        }

        const Vec3 dv = cascade::compute_slot_target_recovery_dv(
            store_,
            i,
            req,
            slot_reference_by_sat_,
            recovery_cfg_
        );
        const double dv_norm = cascade::dv_norm_km_s(dv);
        if (dv_norm <= EPS_NUM) {
            continue;
        }

        const double dv_cap = SAT_MAX_DELTAV_KM_S;
        const double scale = (dv_norm > dv_cap) ? (dv_cap / dv_norm) : 1.0;
        const Vec3 dv_cmd{dv.x * scale, dv.y * scale, dv.z * scale};
        const double cmd_norm = cascade::dv_norm_km_s(dv_cmd);

        const double mass_before = store_.mass_kg(i);
        const double fuel_before = store_.fuel_kg(i);
        const double fuel_need = cascade::propellant_used_kg(mass_before, cmd_norm);
        if (fuel_before - fuel_need <= SAT_FUEL_EOL_KG + EPS_NUM) {
            store_.set_sat_status(i, SatStatus::FUEL_LOW);
            graveyard_requested_by_sat_[sat_id] = true;
            continue;
        }

        const double earliest_burn = std::max(
            epoch_s,
            std::max(epoch_s + SAT_COOLDOWN_S, req.earliest_epoch_s)
        );

        double burn_epoch = 0.0;
        double upload_epoch = 0.0;
        std::string upload_station;
        if (!cascade::choose_burn_epoch_with_upload(
                store_,
                burn_queue_,
                last_burn_epoch_by_sat_,
                i,
                epoch_s,
                earliest_burn,
                earliest_burn + ops_auto_upload_horizon_s(),
                burn_epoch,
                upload_epoch,
                upload_station)) {
            continue;
        }

        ScheduledBurn burn;
        burn.id = "AUTO-STATIONKEEP-" + std::to_string(tick_id) + "-" + std::to_string(planned);
        burn.satellite_id = sat_id;
        burn.upload_station_id = upload_station;
        burn.upload_epoch_s = upload_epoch;
        burn.burn_epoch_s = burn_epoch;
        burn.delta_v_km_s = dv_cmd;
        burn.delta_v_norm_km_s = cmd_norm;
        burn.auto_generated = true;
        burn.recovery_burn = true;
        burn.graveyard_burn = false;
        burn.blackout_overlap = cascade::burn_overlaps_blackout(store_, i, burn_epoch);
        burn_queue_.push_back(std::move(burn));

        req.remaining_delta_v_km_s.x -= dv_cmd.x;
        req.remaining_delta_v_km_s.y -= dv_cmd.y;
        req.remaining_delta_v_km_s.z -= dv_cmd.z;
        req.earliest_epoch_s = burn_epoch + SAT_COOLDOWN_S;
        if (cascade::dv_norm_km_s(req.remaining_delta_v_km_s) <= 1e-6) {
            recovery_requests_by_sat_.erase(sat_id);
        }

        ++planned;
    }

    return planned;
}

std::string EngineRuntime::snapshot_json() const
{
    const std::shared_ptr<const PublishedReadViews> view =
        std::atomic_load_explicit(&published_views_, std::memory_order_acquire);
    if (view) {
        return view->snapshot_json;
    }

    std::shared_lock lock(mutex_);
    return build_snapshot(store_, clock_);
}

std::string EngineRuntime::status_json(bool include_details) const
{
    struct LatencySnapshot {
        std::uint64_t count = 0;
        std::uint64_t queue_wait_us_total = 0;
        std::uint64_t queue_wait_us_max = 0;
        std::uint64_t queue_wait_us_last = 0;
        std::uint64_t execution_us_total = 0;
        std::uint64_t execution_us_max = 0;
        std::uint64_t execution_us_last = 0;
    };

    const auto load_latency_snapshot = [](const CommandLatencyAtomicStats& stats) {
        LatencySnapshot snap;
        snap.count = stats.count.load(std::memory_order_relaxed);
        snap.queue_wait_us_total = stats.queue_wait_us_total.load(std::memory_order_relaxed);
        snap.queue_wait_us_max = stats.queue_wait_us_max.load(std::memory_order_relaxed);
        snap.queue_wait_us_last = stats.queue_wait_us_last.load(std::memory_order_relaxed);
        snap.execution_us_total = stats.execution_us_total.load(std::memory_order_relaxed);
        snap.execution_us_max = stats.execution_us_max.load(std::memory_order_relaxed);
        snap.execution_us_last = stats.execution_us_last.load(std::memory_order_relaxed);
        return snap;
    };

    const auto append_latency_snapshot = [](std::string& out,
                                            std::string_view name,
                                            const LatencySnapshot& snap) {
        out.push_back('"');
        out += name;
        out += R"(":{"count":)";
        out += std::to_string(snap.count);
        out += R"(,"queue_wait_us_total":)";
        out += std::to_string(snap.queue_wait_us_total);
        out += R"(,"queue_wait_us_max":)";
        out += std::to_string(snap.queue_wait_us_max);
        out += R"(,"queue_wait_us_last":)";
        out += std::to_string(snap.queue_wait_us_last);
        out += R"(,"queue_wait_us_mean":)";
        if (snap.count > 0) {
            out += std::to_string(snap.queue_wait_us_total / snap.count);
        } else {
            out += "0";
        }
        out += R"(,"execution_us_total":)";
        out += std::to_string(snap.execution_us_total);
        out += R"(,"execution_us_max":)";
        out += std::to_string(snap.execution_us_max);
        out += R"(,"execution_us_last":)";
        out += std::to_string(snap.execution_us_last);
        out += R"(,"execution_us_mean":)";
        if (snap.count > 0) {
            out += std::to_string(snap.execution_us_total / snap.count);
        } else {
            out += "0";
        }
        out += '}';
    };

    const LatencySnapshot telemetry_latency = load_latency_snapshot(telemetry_latency_);
    const LatencySnapshot schedule_latency = load_latency_snapshot(schedule_latency_);
    const LatencySnapshot step_latency = load_latency_snapshot(step_latency_);

    std::shared_lock lock(mutex_);

    std::size_t obj_count = store_.size();
    std::size_t sat_count = store_.satellite_count();
    std::size_t deb_count = store_.debris_count();
    std::size_t pending_burn_queue = burn_queue_.size();
    std::size_t pending_graveyard_requests = 0;
    for (const auto& kv : graveyard_requested_by_sat_) {
        if (kv.second) ++pending_graveyard_requests;
    }

    const std::uint64_t queue_depth_current = queue_depth_current_.load(std::memory_order_relaxed);
    const std::uint64_t queue_depth_max = queue_depth_max_.load(std::memory_order_relaxed);
    const std::uint64_t queue_enqueued_total = queue_enqueued_total_.load(std::memory_order_relaxed);
    const std::uint64_t queue_completed_total = queue_completed_total_.load(std::memory_order_relaxed);
    const std::uint64_t queue_rejected_total = queue_rejected_total_.load(std::memory_order_relaxed);
    const std::uint64_t queue_timeout_total = queue_timeout_total_.load(std::memory_order_relaxed);

    const double uptime = clock_.uptime_s();
    const std::uint64_t failed_last_tick = store_.failed_last_tick();
    const std::uint64_t failed_total = store_.failed_propagation_total();
    const std::uint64_t tick_count = static_cast<std::uint64_t>(tick_count_.load());

    std::string out;
    out.reserve(include_details ? 1600 : 128);
    out += "{\"status\":\"NOMINAL\",\"uptime_s\":";
    out += fmt_double(uptime, 1);
    out += ",\"tick_count\":";
    out += std::to_string(tick_count);
    out += ",\"object_count\":";
    out += std::to_string(obj_count);

    if (include_details) {
        out += ",\"internal_metrics\":{";
        out += "\"satellite_count\":";
        out += std::to_string(sat_count);
        out += ",\"debris_count\":";
        out += std::to_string(deb_count);
        out += ",\"pending_burn_queue\":";
        out += std::to_string(pending_burn_queue);
        out += ",\"active_cdm_warnings\":";
        out += std::to_string(cdm_warnings_count_);
        out += ",\"predictive_conjunction_count\":";
        out += std::to_string(predictive_conjunction_history_.size());
        out += ",\"history_conjunction_count\":";
        out += std::to_string(conjunction_history_.size());
        out += ",\"dropped_burn_count\":";
        out += std::to_string(dropped_burn_history_.size());
        out += ",\"pending_recovery_requests\":";
        out += std::to_string(recovery_requests_by_sat_.size());
        out += ",\"pending_graveyard_requests\":";
        out += std::to_string(pending_graveyard_requests);
        out += ",\"command_queue_depth\":";
        out += std::to_string(queue_depth_current);
        out += ",\"command_queue_depth_max\":";
        out += std::to_string(queue_depth_max);
        out += ",\"command_queue_depth_limit\":";
        out += std::to_string(max_queue_depth_);
        out += ",\"schedule_success_status\":";
        out += std::to_string(http_policy_.schedule_success_status);
        out += ",\"max_step_seconds\":";
        out += std::to_string(http_policy_.max_step_seconds);
        out += ",\"failed_objects_last_tick\":";
        out += std::to_string(failed_last_tick);
        out += ",\"failed_objects_total\":";
        out += std::to_string(failed_total);
        out += ",\"command_queue_enqueued_total\":";
        out += std::to_string(queue_enqueued_total);
        out += ",\"command_queue_completed_total\":";
        out += std::to_string(queue_completed_total);
        out += ",\"command_queue_rejected_total\":";
        out += std::to_string(queue_rejected_total);
        out += ",\"command_queue_timeout_total\":";
        out += std::to_string(queue_timeout_total);
        out += ",\"command_latency_us\":{";
        append_latency_snapshot(out, "telemetry", telemetry_latency);
        out += ",";
        append_latency_snapshot(out, "schedule", schedule_latency);
        out += ",";
        append_latency_snapshot(out, "step", step_latency);
        out += "}";
        out += ",\"broad_phase_shadow_dcriterion_rejected_total\":";
        out += std::to_string(prop_stats_.broad_dcriterion_shadow_rejected_total);
        out += ",\"narrow_uncertainty_promoted_pairs_total\":";
        out += std::to_string(prop_stats_.narrow_uncertainty_promoted_pairs_total);
        out += ",\"narrow_plane_phase_shadow_rejected_pairs_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_shadow_rejected_pairs_total);
        out += ",\"narrow_plane_phase_hard_rejected_pairs_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_hard_rejected_pairs_total);
        out += ",\"narrow_plane_phase_fail_open_pairs_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_pairs_total);
        out += ",\"narrow_plane_phase_reject_reason_plane_angle_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_reject_reason_plane_angle_total);
        out += ",\"narrow_plane_phase_reject_reason_phase_angle_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_reject_reason_phase_angle_total);
        out += ",\"narrow_plane_phase_fail_open_reason_elements_invalid_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_elements_invalid_total);
        out += ",\"narrow_plane_phase_fail_open_reason_eccentricity_guard_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_eccentricity_guard_total);
        out += ",\"narrow_plane_phase_fail_open_reason_non_finite_state_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_non_finite_state_total);
        out += ",\"narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total);
        out += ",\"narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total);
        out += ",\"narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total);
        out += ",\"narrow_plane_phase_fail_open_reason_uncertainty_override_total\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_uncertainty_override_total);
        out += ",\"narrow_moid_shadow_rejected_pairs_total\":";
        out += std::to_string(prop_stats_.narrow_moid_shadow_rejected_pairs_total);
        out += ",\"narrow_moid_hard_rejected_pairs_total\":";
        out += std::to_string(prop_stats_.narrow_moid_hard_rejected_pairs_total);
        out += ",\"narrow_moid_fail_open_pairs_total\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_pairs_total);
        out += ",\"narrow_moid_reject_reason_distance_threshold_total\":";
        out += std::to_string(prop_stats_.narrow_moid_reject_reason_distance_threshold_total);
        out += ",\"narrow_moid_fail_open_reason_elements_invalid_total\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_elements_invalid_total);
        out += ",\"narrow_moid_fail_open_reason_eccentricity_guard_total\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_eccentricity_guard_total);
        out += ",\"narrow_moid_fail_open_reason_non_finite_state_total\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_non_finite_state_total);
        out += ",\"narrow_moid_fail_open_reason_sampling_failure_total\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_sampling_failure_total);
        out += ",\"narrow_moid_fail_open_reason_hf_placeholder_total\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_hf_placeholder_total);
        out += ",\"narrow_moid_fail_open_reason_uncertainty_override_total\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_uncertainty_override_total);
        out += ",\"narrow_refine_fail_open_reason_rk4_failure_total\":";
        out += std::to_string(prop_stats_.narrow_refine_fail_open_reason_rk4_failure_total);
        out += ",\"narrow_full_refine_fail_open_reason_rk4_failure_total\":";
        out += std::to_string(prop_stats_.narrow_full_refine_fail_open_reason_rk4_failure_total);
        out += ",\"narrow_full_refine_fail_open_reason_budget_exhausted_total\":";
        out += std::to_string(prop_stats_.narrow_full_refine_fail_open_reason_budget_exhausted_total);
        out += ",\"collision_threshold_km\":";
        out += fmt_double(COLLISION_THRESHOLD_KM, 3);
        out += ",\"narrow_tca_guard_km\":";
        out += fmt_double(step_cfg_.narrow_phase.tca_guard_km, 3);
        out += ",\"effective_collision_threshold_km\":";
        out += fmt_double(COLLISION_THRESHOLD_KM + std::max(0.0, step_cfg_.narrow_phase.tca_guard_km), 3);
        out += ",\"predictive_cdm_threshold_km\":";
        out += fmt_double(COLLISION_THRESHOLD_KM, 3);
        out += ",\"predictive_screening_threshold_km\":";
        out += fmt_double(SCREENING_THRESHOLD_KM, 3);
        out += ",\"predictive_cdm_horizon_s\":";
        out += fmt_double(cdm_params().horizon_s, 1);
        out += ",\"predictive_cdm_substep_s\":";
        out += fmt_double(cdm_params().substep_s, 1);
        out += ",\"predictive_cdm_rk4_max_step_s\":";
        out += fmt_double(cdm_params().rk4_max_step_s, 1);
        out += ",\"propagation_last_tick\":{";
        out += "\"narrow_pairs_checked\":";
        out += std::to_string(prop_stats_.narrow_pairs_last_tick);
        out += ",\"collisions_detected\":";
        out += std::to_string(prop_stats_.collisions_last_tick);
        out += ",\"maneuvers_executed\":";
        out += std::to_string(prop_stats_.maneuvers_last_tick);
        out += ",\"auto_planned_maneuvers\":";
        out += std::to_string(prop_stats_.auto_planned_last_tick);
        out += ",\"recovery_planned\":";
        out += std::to_string(prop_stats_.recovery_planned_last_tick);
        out += ",\"recovery_deferred\":";
        out += std::to_string(prop_stats_.recovery_deferred_last_tick);
        out += ",\"recovery_completed\":";
        out += std::to_string(prop_stats_.recovery_completed_last_tick);
        out += ",\"graveyard_planned\":";
        out += std::to_string(prop_stats_.graveyard_planned_last_tick);
        out += ",\"graveyard_deferred\":";
        out += std::to_string(prop_stats_.graveyard_deferred_last_tick);
        out += ",\"graveyard_completed\":";
        out += std::to_string(prop_stats_.graveyard_completed_last_tick);
        out += ",\"upload_window_missed\":";
        out += std::to_string(prop_stats_.upload_missed_last_tick);
        out += ",\"stationkeeping_outside_box\":";
        out += std::to_string(prop_stats_.stationkeeping_outside_box_last_tick);
        out += ",\"stationkeeping_uptime_penalty_mean\":";
        out += fmt_double(prop_stats_.stationkeeping_uptime_penalty_mean_last_tick, 6);
        out += ",\"stationkeeping_slot_radius_error_mean_km\":";
        out += fmt_double(prop_stats_.stationkeeping_slot_radius_error_mean_km_last_tick, 6);
        out += ",\"stationkeeping_slot_radius_error_max_km\":";
        out += fmt_double(prop_stats_.stationkeeping_slot_radius_error_max_km_last_tick, 6);
        out += ",\"recovery_pending_marked\":";
        out += std::to_string(prop_stats_.recovery_pending_marked_last_tick);
        out += ",\"recovery_slot_error_mean\":";
        out += fmt_double(prop_stats_.recovery_slot_error_mean_last_tick, 6);
        out += ",\"recovery_slot_error_max\":";
        out += fmt_double(prop_stats_.recovery_slot_error_max_last_tick, 6);
        out += ",\"narrow_refined_pairs\":";
        out += std::to_string(prop_stats_.narrow_refined_pairs_last_tick);
        out += ",\"narrow_full_refined_pairs\":";
        out += std::to_string(prop_stats_.narrow_full_refined_pairs_last_tick);
        out += ",\"narrow_full_refine_budget_allocated\":";
        out += std::to_string(prop_stats_.narrow_full_refine_budget_allocated_last_tick);
        out += ",\"narrow_full_refine_budget_exhausted\":";
        out += std::to_string(prop_stats_.narrow_full_refine_budget_exhausted_last_tick);
        out += ",\"narrow_uncertainty_promoted_pairs\":";
        out += std::to_string(prop_stats_.narrow_uncertainty_promoted_pairs_last_tick);
        out += ",\"narrow_plane_phase_evaluated_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_evaluated_pairs_last_tick);
        out += ",\"narrow_plane_phase_shadow_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_shadow_rejected_pairs_last_tick);
        out += ",\"narrow_plane_phase_hard_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_hard_rejected_pairs_last_tick);
        out += ",\"narrow_plane_phase_fail_open_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_pairs_last_tick);
        out += ",\"narrow_plane_phase_reject_reason_plane_angle\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_reject_reason_plane_angle_last_tick);
        out += ",\"narrow_plane_phase_reject_reason_phase_angle\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_reject_reason_phase_angle_last_tick);
        out += ",\"narrow_plane_phase_fail_open_reason_elements_invalid\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_elements_invalid_last_tick);
        out += ",\"narrow_plane_phase_fail_open_reason_eccentricity_guard\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_eccentricity_guard_last_tick);
        out += ",\"narrow_plane_phase_fail_open_reason_non_finite_state\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_non_finite_state_last_tick);
        out += ",\"narrow_plane_phase_fail_open_reason_angular_momentum_degenerate\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_last_tick);
        out += ",\"narrow_plane_phase_fail_open_reason_plane_angle_non_finite\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_last_tick);
        out += ",\"narrow_plane_phase_fail_open_reason_phase_angle_non_finite\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_last_tick);
        out += ",\"narrow_plane_phase_fail_open_reason_uncertainty_override\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_uncertainty_override_last_tick);
        out += ",\"narrow_moid_evaluated_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_evaluated_pairs_last_tick);
        out += ",\"narrow_moid_shadow_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_shadow_rejected_pairs_last_tick);
        out += ",\"narrow_moid_hard_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_hard_rejected_pairs_last_tick);
        out += ",\"narrow_moid_fail_open_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_pairs_last_tick);
        out += ",\"narrow_moid_reject_reason_distance_threshold\":";
        out += std::to_string(prop_stats_.narrow_moid_reject_reason_distance_threshold_last_tick);
        out += ",\"narrow_moid_fail_open_reason_elements_invalid\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_elements_invalid_last_tick);
        out += ",\"narrow_moid_fail_open_reason_eccentricity_guard\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_eccentricity_guard_last_tick);
        out += ",\"narrow_moid_fail_open_reason_non_finite_state\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_non_finite_state_last_tick);
        out += ",\"narrow_moid_fail_open_reason_sampling_failure\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_sampling_failure_last_tick);
        out += ",\"narrow_moid_fail_open_reason_hf_placeholder\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_hf_placeholder_last_tick);
        out += ",\"narrow_moid_fail_open_reason_uncertainty_override\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_uncertainty_override_last_tick);
        out += ",\"narrow_refine_fail_open_reason_rk4_failure\":";
        out += std::to_string(prop_stats_.narrow_refine_fail_open_reason_rk4_failure_last_tick);
        out += ",\"narrow_full_refine_fail_open_reason_rk4_failure\":";
        out += std::to_string(prop_stats_.narrow_full_refine_fail_open_reason_rk4_failure_last_tick);
        out += ",\"narrow_full_refine_fail_open_reason_budget_exhausted\":";
        out += std::to_string(prop_stats_.narrow_full_refine_fail_open_reason_budget_exhausted_last_tick);
        out += ",\"broad_candidates\":";
        out += std::to_string(prop_stats_.broad_candidates_last_tick);
        out += ",\"broad_pairs_considered\":";
        out += std::to_string(prop_stats_.broad_pairs_last_tick);
        out += ",\"broad_dcriterion_shadow_rejected\":";
        out += std::to_string(prop_stats_.broad_dcriterion_shadow_rejected_last_tick);
        out += "},\"propagation_totals\":{";
        out += "\"narrow_pairs_checked\":";
        out += std::to_string(prop_stats_.narrow_pairs_total);
        out += ",\"collisions_detected\":";
        out += std::to_string(prop_stats_.collisions_total);
        out += ",\"maneuvers_executed\":";
        out += std::to_string(prop_stats_.maneuvers_total);
        out += ",\"auto_planned_maneuvers\":";
        out += std::to_string(prop_stats_.auto_planned_total);
        out += ",\"recovery_planned\":";
        out += std::to_string(prop_stats_.recovery_planned_total);
        out += ",\"recovery_deferred\":";
        out += std::to_string(prop_stats_.recovery_deferred_total);
        out += ",\"recovery_completed\":";
        out += std::to_string(prop_stats_.recovery_completed_total);
        out += ",\"graveyard_planned\":";
        out += std::to_string(prop_stats_.graveyard_planned_total);
        out += ",\"graveyard_deferred\":";
        out += std::to_string(prop_stats_.graveyard_deferred_total);
        out += ",\"graveyard_completed\":";
        out += std::to_string(prop_stats_.graveyard_completed_total);
        out += ",\"upload_window_missed\":";
        out += std::to_string(prop_stats_.upload_missed_total);
        out += ",\"stationkeeping_outside_box\":";
        out += std::to_string(prop_stats_.stationkeeping_outside_box_total);
        out += ",\"stationkeeping_uptime_penalty_mean\":";
        if (prop_stats_.stationkeeping_uptime_penalty_samples_total > 0) {
            out += fmt_double(
                prop_stats_.stationkeeping_uptime_penalty_sum_total
                    / static_cast<double>(prop_stats_.stationkeeping_uptime_penalty_samples_total),
                6
            );
        } else {
            out += "0.000000";
        }
        out += ",\"stationkeeping_slot_radius_error_mean_km\":";
        if (prop_stats_.stationkeeping_slot_radius_error_samples_total > 0) {
            out += fmt_double(
                prop_stats_.stationkeeping_slot_radius_error_sum_total
                    / static_cast<double>(prop_stats_.stationkeeping_slot_radius_error_samples_total),
                6
            );
        } else {
            out += "0.000000";
        }
        out += ",\"stationkeeping_slot_radius_error_max_km\":";
        out += fmt_double(prop_stats_.stationkeeping_slot_radius_error_max_total, 6);
        out += ",\"recovery_pending_marked\":";
        out += std::to_string(prop_stats_.recovery_pending_marked_total);
        out += ",\"recovery_slot_error_mean\":";
        if (prop_stats_.recovery_slot_error_samples_total > 0) {
            out += fmt_double(
                prop_stats_.recovery_slot_error_sum_total
                    / static_cast<double>(prop_stats_.recovery_slot_error_samples_total),
                6
            );
        } else {
            out += "0.000000";
        }
        out += ",\"recovery_slot_error_max\":";
        out += fmt_double(prop_stats_.recovery_slot_error_max_total, 6);
        out += ",\"narrow_refined_pairs\":";
        out += std::to_string(prop_stats_.narrow_refined_pairs_total);
        out += ",\"narrow_full_refined_pairs\":";
        out += std::to_string(prop_stats_.narrow_full_refined_pairs_total);
        out += ",\"narrow_full_refine_budget_allocated\":";
        out += std::to_string(prop_stats_.narrow_full_refine_budget_allocated_total);
        out += ",\"narrow_full_refine_budget_exhausted\":";
        out += std::to_string(prop_stats_.narrow_full_refine_budget_exhausted_total);
        out += ",\"narrow_uncertainty_promoted_pairs\":";
        out += std::to_string(prop_stats_.narrow_uncertainty_promoted_pairs_total);
        out += ",\"narrow_plane_phase_evaluated_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_evaluated_pairs_total);
        out += ",\"narrow_plane_phase_shadow_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_shadow_rejected_pairs_total);
        out += ",\"narrow_plane_phase_hard_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_hard_rejected_pairs_total);
        out += ",\"narrow_plane_phase_fail_open_pairs\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_pairs_total);
        out += ",\"narrow_plane_phase_reject_reason_plane_angle\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_reject_reason_plane_angle_total);
        out += ",\"narrow_plane_phase_reject_reason_phase_angle\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_reject_reason_phase_angle_total);
        out += ",\"narrow_plane_phase_fail_open_reason_elements_invalid\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_elements_invalid_total);
        out += ",\"narrow_plane_phase_fail_open_reason_eccentricity_guard\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_eccentricity_guard_total);
        out += ",\"narrow_plane_phase_fail_open_reason_non_finite_state\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_non_finite_state_total);
        out += ",\"narrow_plane_phase_fail_open_reason_angular_momentum_degenerate\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total);
        out += ",\"narrow_plane_phase_fail_open_reason_plane_angle_non_finite\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total);
        out += ",\"narrow_plane_phase_fail_open_reason_phase_angle_non_finite\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total);
        out += ",\"narrow_plane_phase_fail_open_reason_uncertainty_override\":";
        out += std::to_string(prop_stats_.narrow_plane_phase_fail_open_reason_uncertainty_override_total);
        out += ",\"narrow_moid_evaluated_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_evaluated_pairs_total);
        out += ",\"narrow_moid_shadow_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_shadow_rejected_pairs_total);
        out += ",\"narrow_moid_hard_rejected_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_hard_rejected_pairs_total);
        out += ",\"narrow_moid_fail_open_pairs\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_pairs_total);
        out += ",\"narrow_moid_reject_reason_distance_threshold\":";
        out += std::to_string(prop_stats_.narrow_moid_reject_reason_distance_threshold_total);
        out += ",\"narrow_moid_fail_open_reason_elements_invalid\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_elements_invalid_total);
        out += ",\"narrow_moid_fail_open_reason_eccentricity_guard\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_eccentricity_guard_total);
        out += ",\"narrow_moid_fail_open_reason_non_finite_state\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_non_finite_state_total);
        out += ",\"narrow_moid_fail_open_reason_sampling_failure\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_sampling_failure_total);
        out += ",\"narrow_moid_fail_open_reason_hf_placeholder\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_hf_placeholder_total);
        out += ",\"narrow_moid_fail_open_reason_uncertainty_override\":";
        out += std::to_string(prop_stats_.narrow_moid_fail_open_reason_uncertainty_override_total);
        out += ",\"narrow_refine_fail_open_reason_rk4_failure\":";
        out += std::to_string(prop_stats_.narrow_refine_fail_open_reason_rk4_failure_total);
        out += ",\"narrow_full_refine_fail_open_reason_rk4_failure\":";
        out += std::to_string(prop_stats_.narrow_full_refine_fail_open_reason_rk4_failure_total);
        out += ",\"narrow_full_refine_fail_open_reason_budget_exhausted\":";
        out += std::to_string(prop_stats_.narrow_full_refine_fail_open_reason_budget_exhausted_total);
        out += ",\"broad_dcriterion_shadow_rejected\":";
        out += std::to_string(prop_stats_.broad_dcriterion_shadow_rejected_total);
        out += ",\"broad_candidates\":";
        out += std::to_string(prop_stats_.broad_candidates_total);
        out += ",\"broad_pairs_considered\":";
        out += std::to_string(prop_stats_.broad_pairs_total);
        out += "}}";
    }

    out += '}';
    return out;
}

std::string EngineRuntime::conflicts_json() const
{
    const std::shared_ptr<const PublishedReadViews> view =
        std::atomic_load_explicit(&published_views_, std::memory_order_acquire);
    if (view) {
        return view->conflicts_json;
    }

    std::shared_lock lock(mutex_);
    return build_conflicts_json(store_);
}

std::string EngineRuntime::propagation_json() const
{
    const std::shared_ptr<const PublishedReadViews> view =
        std::atomic_load_explicit(&published_views_, std::memory_order_acquire);
    if (view) {
        return view->propagation_json;
    }

    std::shared_lock lock(mutex_);
    return build_propagation_json(store_, prop_stats_, step_cfg_, recovery_cfg_);
}

std::string EngineRuntime::burns_json() const
{
    const std::shared_ptr<const PublishedReadViews> view =
        std::atomic_load_explicit(&published_views_, std::memory_order_acquire);
    if (view) {
        return view->burns_json;
    }

    std::shared_lock lock(mutex_);
    return build_burns_json(
        executed_burn_history_,
        dropped_burn_history_,
        burn_queue_,
        per_sat_maneuver_stats_
    );
}

std::string EngineRuntime::conjunctions_json(std::string_view satellite_id_filter,
                                             std::string_view source_filter) const
{
    const bool wants_predictive = (source_filter == "predicted" || source_filter == "predictive");
    const bool wants_combined = (source_filter == "combined");
    if (wants_predictive && satellite_id_filter.empty()) {
        const std::shared_ptr<const PublishedReadViews> view =
            std::atomic_load_explicit(&published_views_, std::memory_order_acquire);
        if (view) {
            return view->predictive_conjunctions_json;
        }
    }

    // Conjunctions are not fully pre-published (filter varies per request).
    std::shared_lock lock(mutex_);
    if (wants_predictive) {
        return build_conjunctions_json(predictive_conjunction_history_, satellite_id_filter, source_filter);
    }
    if (!wants_combined) {
        const std::string_view history_label = source_filter.empty() ? std::string_view{"history"} : source_filter;
        return build_conjunctions_json(conjunction_history_, satellite_id_filter, history_label);
    }

    std::deque<ConjunctionRecord> combined = predictive_conjunction_history_;
    for (const auto& rec : conjunction_history_) {
        combined.push_back(rec);
    }
    return build_conjunctions_json(combined, satellite_id_filter, source_filter);
}

std::string EngineRuntime::trajectory_json(std::string_view satellite_id) const
{
    // Trajectory is not pre-published (per-satellite + forward prediction).
    std::shared_lock lock(mutex_);
    const auto it = trajectory_by_sat_.find(std::string(satellite_id));
    static const std::deque<TrackPoint> empty_trail;
    const auto& trail = (it != trajectory_by_sat_.end()) ? it->second : empty_trail;
    return build_trajectory_json(store_, satellite_id, trail, clock_.epoch_s());
}

} // namespace cascade
