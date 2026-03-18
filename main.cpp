// ---------------------------------------------------------------------------
// CASCADE (Project BONK) — API server
// ---------------------------------------------------------------------------

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>
#include <cmath>

#include <jluna.hpp>
#include <httplib.h>
#include <simdjson.h>
#include <boost/version.hpp>

#include "types.hpp"
#include "json_util.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "telemetry.hpp"
#include "simulation_engine.hpp"
#include "earth_frame.hpp"

static cascade::StateStore  g_store;
static cascade::SimClock    g_clock;
static std::shared_mutex    g_mutex;
static std::atomic<int64_t> g_tick_count{0};
static cascade::StepRunConfig g_step_cfg = [] {
    cascade::StepRunConfig cfg;
    cfg.broad_phase.enable_dcriterion = false; // diagnostics-only until Phase 4
    cfg.broad_phase.shell_margin_km = 50.0;
    cfg.broad_phase.invalid_shell_pad_km = 200.0;
    cfg.broad_phase.a_bin_width_km = 500.0;
    cfg.broad_phase.i_bin_width_rad = 0.3490658503988659;
    cfg.broad_phase.band_neighbor_bins = 2;
    cfg.broad_phase.high_e_fail_open = 0.2;
    cfg.broad_phase.dcriterion_threshold = 2.0;
    return cfg;
}();

struct ScheduledBurn {
    std::string id;
    std::string satellite_id;
    double burn_epoch_s = 0.0;
    cascade::Vec3 delta_v_km_s{};
    double delta_v_norm_km_s = 0.0;
};

struct GroundStation {
    const char* id;
    double lat_deg;
    double lon_deg;
    double alt_km;
    double min_el_deg;
};

static constexpr GroundStation k_ground_stations[] = {
    {"GS-001", 13.0333,   77.5167, 0.820,  5.0},
    {"GS-002", 78.2297,   15.4077, 0.400,  5.0},
    {"GS-003", 35.4266, -116.8900, 1.000, 10.0},
    {"GS-004",-53.1500,  -70.9167, 0.030,  5.0},
    {"GS-005", 28.5450,   77.1926, 0.225, 15.0},
    {"GS-006",-77.8463,  166.6682, 0.010,  5.0}
};

static std::vector<ScheduledBurn> g_burn_queue;
static std::unordered_map<std::string, double> g_last_burn_epoch_by_sat;

static double dv_norm_km_s(const cascade::Vec3& dv) noexcept
{
    return std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
}

static double propellant_used_kg(double mass_kg, double delta_v_km_s) noexcept
{
    if (!(mass_kg > 0.0) || !(delta_v_km_s > 0.0)) {
        return 0.0;
    }
    const double ve_km_s = cascade::SAT_ISP_S * cascade::G0_KM_S2;
    if (!(ve_km_s > 0.0)) {
        return 0.0;
    }
    const double mass_ratio = std::exp(-delta_v_km_s / ve_km_s);
    return mass_kg * (1.0 - mass_ratio);
}

static bool has_ground_station_los(const cascade::Vec3& sat_eci_km,
                                   double epoch_s) noexcept
{
    const cascade::Vec3 sat_ecef = cascade::eci_to_ecef(sat_eci_km, epoch_s);

    for (const GroundStation& gs : k_ground_stations) {
        const double lat_rad = gs.lat_deg * cascade::PI / 180.0;
        const double lon_rad = gs.lon_deg * cascade::PI / 180.0;
        const double min_el_rad = gs.min_el_deg * cascade::PI / 180.0;
        const double el = cascade::elevation_angle_rad(
            sat_ecef,
            lat_rad,
            lon_rad,
            gs.alt_km
        );
        if (el >= min_el_rad) {
            return true;
        }
    }
    return false;
}

static std::uint64_t execute_due_maneuvers(cascade::StateStore& store,
                                           double current_epoch_s)
{
    std::uint64_t executed = 0;
    std::vector<ScheduledBurn> pending;
    pending.reserve(g_burn_queue.size());

    for (const ScheduledBurn& burn : g_burn_queue) {
        if (burn.burn_epoch_s > current_epoch_s + cascade::EPS_NUM) {
            pending.push_back(burn);
            continue;
        }

        const std::size_t idx = store.find(burn.satellite_id);
        if (idx >= store.size() || store.type(idx) != cascade::ObjectType::SATELLITE) {
            continue;
        }

        const double mass_before = store.mass_kg(idx);
        const double fuel_before = store.fuel_kg(idx);
        const double fuel_needed = propellant_used_kg(mass_before, burn.delta_v_norm_km_s);
        if (fuel_needed > fuel_before + cascade::EPS_NUM) {
            continue;
        }

        store.vx_mut(idx) += burn.delta_v_km_s.x;
        store.vy_mut(idx) += burn.delta_v_km_s.y;
        store.vz_mut(idx) += burn.delta_v_km_s.z;
        store.set_elements(idx, cascade::OrbitalElements{}, false);

        const double fuel_after = std::max(0.0, fuel_before - fuel_needed);
        const double mass_after = std::max(cascade::SAT_DRY_MASS_KG, mass_before - fuel_needed);
        store.fuel_kg_mut(idx) = fuel_after;
        store.mass_kg_mut(idx) = mass_after;

        if (fuel_after <= cascade::SAT_FUEL_EOL_KG) {
            store.set_sat_status(idx, cascade::SatStatus::FUEL_LOW);
        } else {
            store.set_sat_status(idx, cascade::SatStatus::MANEUVERING);
        }

        g_last_burn_epoch_by_sat[burn.satellite_id] = burn.burn_epoch_s;
        ++executed;
    }

    g_burn_queue.swap(pending);
    return executed;
}

static bool has_pending_burn_in_cooldown_window(const std::string& sat_id,
                                                 double epoch_s) noexcept
{
    for (const ScheduledBurn& b : g_burn_queue) {
        if (b.satellite_id != sat_id) continue;
        const double dt = std::abs(b.burn_epoch_s - epoch_s);
        if (dt + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) {
            return true;
        }
    }
    return false;
}

static std::uint64_t plan_collision_avoidance_burns(cascade::StateStore& store,
                                                     const cascade::StepRunStats& step_stats,
                                                     double epoch_s,
                                                     std::uint64_t tick_id)
{
    std::uint64_t planned = 0;

    for (std::uint32_t sat_idx_u32 : step_stats.collision_sat_indices) {
        const std::size_t sat_idx = static_cast<std::size_t>(sat_idx_u32);
        if (sat_idx >= store.size()) continue;
        if (store.type(sat_idx) != cascade::ObjectType::SATELLITE) continue;

        const std::string sat_id = store.id(sat_idx);

        const auto last_it = g_last_burn_epoch_by_sat.find(sat_id);
        if (last_it != g_last_burn_epoch_by_sat.end()) {
            const double dt = epoch_s - last_it->second;
            if (dt + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) {
                continue;
            }
        }

        if (has_pending_burn_in_cooldown_window(sat_id, epoch_s)) {
            continue;
        }

        const cascade::Vec3 sat_eci{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
        if (!has_ground_station_los(sat_eci, epoch_s)) {
            continue;
        }

        const cascade::Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};
        const double v_norm = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (v_norm < cascade::EPS_NUM) {
            continue;
        }

        constexpr double k_auto_dv_km_s = 0.001; // 1 m/s conservative impulse
        const cascade::Vec3 dv{
            (v.x / v_norm) * k_auto_dv_km_s,
            (v.y / v_norm) * k_auto_dv_km_s,
            (v.z / v_norm) * k_auto_dv_km_s
        };

        const double mass_before = store.mass_kg(sat_idx);
        const double fuel_before = store.fuel_kg(sat_idx);
        const double fuel_need = propellant_used_kg(mass_before, k_auto_dv_km_s);
        const double fuel_after = fuel_before - fuel_need;
        if (fuel_after <= cascade::SAT_FUEL_EOL_KG + cascade::EPS_NUM) {
            store.set_sat_status(sat_idx, cascade::SatStatus::FUEL_LOW);
            continue;
        }

        ScheduledBurn burn;
        burn.id = "AUTO-COLA-" + std::to_string(tick_id) + "-" + std::to_string(planned);
        burn.satellite_id = sat_id;
        burn.burn_epoch_s = epoch_s;
        burn.delta_v_km_s = dv;
        burn.delta_v_norm_km_s = k_auto_dv_km_s;

        g_burn_queue.push_back(std::move(burn));
        ++planned;
    }

    return planned;
}

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
    std::uint64_t auto_planned_last_tick = 0;

    std::uint64_t broad_pairs_last_tick = 0;
    std::uint64_t broad_candidates_last_tick = 0;
    std::uint64_t broad_overlap_pass_last_tick = 0;
    std::uint64_t broad_dcriterion_rejected_last_tick = 0;
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
    std::uint64_t auto_planned_total = 0;

    std::uint64_t broad_pairs_total = 0;
    std::uint64_t broad_candidates_total = 0;
    std::uint64_t broad_overlap_pass_total = 0;
    std::uint64_t broad_dcriterion_rejected_total = 0;
    std::uint64_t broad_fail_open_objects_total = 0;
    std::uint64_t broad_fail_open_satellites_total = 0;
};

static PropagationStats g_prop_stats;

static void set_error_json(httplib::Response& res,
                           int http_status,
                           std::string_view code,
                           std::string_view message)
{
    std::string out;
    out.reserve(96 + code.size() + message.size());
    out += "{\"status\":\"ERROR\",\"code\":";
    cascade::append_json_string(out, code);
    out += ",\"message\":";
    cascade::append_json_string(out, message);
    out += '}';
    res.status = http_status;
    res.set_content(out, "application/json");
}

static std::string get_source_id(const httplib::Request& req)
{
    const std::string source = req.get_header_value("X-Source-Id");
    if (source.empty()) return "unknown";
    return source;
}

static bool parse_vec3_field(simdjson::ondemand::object& parent,
                             const char* key,
                             cascade::Vec3& out)
{
    simdjson::ondemand::object obj;
    if (parent.find_field_unordered(key).get_object().get(obj) != simdjson::SUCCESS) {
        return false;
    }

    auto x = obj.find_field_unordered("x").get_double();
    auto y = obj.find_field_unordered("y").get_double();
    auto z = obj.find_field_unordered("z").get_double();
    if (x.error() || y.error() || z.error()) {
        return false;
    }

    out.x = x.value_unsafe();
    out.y = y.value_unsafe();
    out.z = z.value_unsafe();
    return true;
}

static std::string build_snapshot(const cascade::StateStore& store,
                                  const cascade::SimClock&   clock)
{
    std::string out;
    out.reserve(store.satellite_count() * 128 + store.debris_count() * 56 + 128);
    const double epoch_s = clock.epoch_s();

    out += "{\"timestamp\":";
    cascade::append_json_string(out, clock.to_iso());
    out += ",\"satellites\":[";

    bool first_sat = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != cascade::ObjectType::SATELLITE) continue;
        if (!first_sat) out.push_back(',');
        first_sat = false;

        out += "{\"id\":";
        cascade::append_json_string(out, store.id(i));

        const cascade::Vec3 eci{store.rx(i), store.ry(i), store.rz(i)};
        const cascade::Vec3 ecef = cascade::eci_to_ecef(eci, epoch_s);
        double lat_deg = 0.0;
        double lon_deg = 0.0;
        double alt_km = 0.0;
        (void)cascade::ecef_to_geodetic(ecef, lat_deg, lon_deg, alt_km);

        out += ",\"lat\":";
        out += cascade::fmt_double(lat_deg, 6);
        out += ",\"lon\":";
        out += cascade::fmt_double(lon_deg, 6);
        out += ",\"fuel_kg\":";
        out += cascade::fmt_double(store.fuel_kg(i), 3);
        out += ",\"status\":";
        cascade::append_json_string(out, cascade::sat_status_str(store.sat_status(i)));
        out += '}';
    }

    out += "],\"debris_cloud\":[";
    bool first_deb = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != cascade::ObjectType::DEBRIS) continue;
        if (!first_deb) out.push_back(',');
        first_deb = false;

        out += '[';
        cascade::append_json_string(out, store.id(i));

        const cascade::Vec3 eci{store.rx(i), store.ry(i), store.rz(i)};
        const cascade::Vec3 ecef = cascade::eci_to_ecef(eci, epoch_s);
        double lat_deg = 0.0;
        double lon_deg = 0.0;
        double alt_km = 0.0;
        (void)cascade::ecef_to_geodetic(ecef, lat_deg, lon_deg, alt_km);

        out += ',';
        out += cascade::fmt_double(lat_deg, 6);
        out += ',';
        out += cascade::fmt_double(lon_deg, 6);
        out += ',';
        out += cascade::fmt_double(alt_km, 3);
        out += ']';
    }

    out += "]}";
    return out;
}

static std::string build_conflicts_json(const cascade::StateStore& store)
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
        cascade::append_json_string(out, kv.first);
        out.push_back(':');
        out += std::to_string(kv.second);
    }

    out += "},\"recent\":[";

    bool first = true;
    for (const auto& rec : history) {
        if (!first) out.push_back(',');
        first = false;
        out += "{\"object_id\":";
        cascade::append_json_string(out, rec.object_id);
        out += ",\"stored_type\":";
        cascade::append_json_string(out, cascade::object_type_str(rec.stored_type));
        out += ",\"incoming_type\":";
        cascade::append_json_string(out, cascade::object_type_str(rec.incoming_type));
        out += ",\"telemetry_timestamp\":";
        cascade::append_json_string(out, rec.telemetry_timestamp);
        out += ",\"ingestion_timestamp\":";
        cascade::append_json_string(out, cascade::iso8601(rec.ingestion_unix_s));
        out += ",\"source_id\":";
        cascade::append_json_string(out, rec.source_id);
        out += ",\"reason\":";
        cascade::append_json_string(out, rec.reason);
        out += '}';
    }

    out += "]}";
    return out;
}

static std::string build_propagation_json(const cascade::StateStore& store,
                                          const PropagationStats& stats,
                                          const cascade::StepRunConfig& cfg)
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
    out += ",\"auto_planned_maneuvers\":";
    out += std::to_string(stats.auto_planned_last_tick);
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
    out += ",\"broad_fail_open_objects\":";
    out += std::to_string(stats.broad_fail_open_objects_last_tick);
    out += ",\"broad_fail_open_satellites\":";
    out += std::to_string(stats.broad_fail_open_satellites_last_tick);
    out += ",\"broad_shell_margin_km\":";
    out += cascade::fmt_double(stats.broad_shell_margin_km_last_tick, 3);
    out += ",\"broad_dcriterion_enabled\":";
    out += (stats.broad_dcriterion_enabled_last_tick ? "true" : "false");
    out += ",\"broad_a_bin_width_km\":";
    out += cascade::fmt_double(stats.broad_a_bin_width_km_last_tick, 3);
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
    out += ",\"auto_planned_maneuvers\":";
    out += std::to_string(stats.auto_planned_total);
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
    out += "\"broad_shell_margin_km\":";
    out += cascade::fmt_double(cfg.broad_phase.shell_margin_km, 3);
    out += ",\"broad_invalid_shell_pad_km\":";
    out += cascade::fmt_double(cfg.broad_phase.invalid_shell_pad_km, 3);
    out += ",\"broad_a_bin_width_km\":";
    out += cascade::fmt_double(cfg.broad_phase.a_bin_width_km, 3);
    out += ",\"broad_i_bin_width_rad\":";
    out += cascade::fmt_double(cfg.broad_phase.i_bin_width_rad, 6);
    out += ",\"broad_band_neighbor_bins\":";
    out += std::to_string(cfg.broad_phase.band_neighbor_bins);
    out += ",\"broad_high_e_fail_open\":";
    out += cascade::fmt_double(cfg.broad_phase.high_e_fail_open, 3);
    out += ",\"broad_dcriterion_enabled\":";
    out += (cfg.broad_phase.enable_dcriterion ? "true" : "false");
    out += ",\"broad_dcriterion_threshold\":";
    out += cascade::fmt_double(cfg.broad_phase.dcriterion_threshold, 3);
    out += "}}";
    return out;
}

int main()
{
    jluna::initialize();

    std::cout << "CASCADE (Project BONK) SYSTEM ONLINE\n";
    std::cout << "Boost version : " << BOOST_LIB_VERSION << "\n";
    std::cout << "Starting HTTP server on 0.0.0.0:8000 ...\n";

    httplib::Server svr;

    // ------------------------------------------------------------------
    // POST /api/telemetry
    // ------------------------------------------------------------------
    svr.Post("/api/telemetry", [](const httplib::Request& req, httplib::Response& res) {
        const cascade::TelemetryParseResult parsed = cascade::parse_telemetry_payload(req.body);
        if (!parsed.parse_ok) {
            set_error_json(res, 400, parsed.error_code, parsed.error_message);
            return;
        }

        cascade::TelemetryIngestResult ingest;
        {
            std::unique_lock lock(g_mutex);
            ingest = cascade::apply_telemetry_batch(parsed, g_store, g_clock, get_source_id(req));
        }

        if (!ingest.ok) {
            const int status = (ingest.error_code == "STALE_TELEMETRY") ? 409 : 422;
            set_error_json(res, status, ingest.error_code, ingest.error_message);
            return;
        }

        std::string out;
        out.reserve(96);
        out += "{\"status\":\"ACK\",\"processed_count\":";
        out += std::to_string(ingest.processed_count);
        out += ",\"active_cdm_warnings\":0}";
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/maneuver/schedule
    // ------------------------------------------------------------------
    svr.Post("/api/maneuver/schedule", [](const httplib::Request& req, httplib::Response& res) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(req.body.data(), req.body.size());
        simdjson::ondemand::document doc;
        if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
            set_error_json(res, 400, "MALFORMED_JSON", "request body is not valid JSON");
            return;
        }

        simdjson::ondemand::object root;
        if (doc.get_object().get(root) != simdjson::SUCCESS) {
            set_error_json(res, 400, "INVALID_REQUEST", "root JSON value must be an object");
            return;
        }

        std::string_view sat_sv;
        if (root.find_field_unordered("satelliteId").get_string().get(sat_sv) != simdjson::SUCCESS || sat_sv.empty()) {
            set_error_json(res, 422, "MISSING_SATELLITE_ID", "field 'satelliteId' is required");
            return;
        }

        simdjson::ondemand::array burns;
        if (root.find_field_unordered("maneuver_sequence").get_array().get(burns) != simdjson::SUCCESS) {
            set_error_json(res, 422, "MISSING_MANEUVER_SEQUENCE", "field 'maneuver_sequence' must be an array");
            return;
        }

        std::vector<ScheduledBurn> parsed_burns;
        parsed_burns.reserve(8);

        for (auto burn_value : burns) {
            simdjson::ondemand::object burn_obj;
            if (burn_value.get_object().get(burn_obj) != simdjson::SUCCESS) {
                set_error_json(res, 422, "INVALID_BURN", "each maneuver entry must be an object");
                return;
            }

            std::string_view burn_id_sv;
            if (burn_obj.find_field_unordered("burn_id").get_string().get(burn_id_sv) != simdjson::SUCCESS || burn_id_sv.empty()) {
                set_error_json(res, 422, "MISSING_BURN_ID", "each burn must include non-empty 'burn_id'");
                return;
            }

            std::string_view burn_time_sv;
            if (burn_obj.find_field_unordered("burnTime").get_string().get(burn_time_sv) != simdjson::SUCCESS || burn_time_sv.empty()) {
                set_error_json(res, 422, "MISSING_BURN_TIME", "each burn must include non-empty 'burnTime'");
                return;
            }

            double burn_epoch_s = 0.0;
            if (!cascade::parse_iso8601(burn_time_sv, burn_epoch_s)) {
                set_error_json(res, 422, "INVALID_BURN_TIME", "burnTime must be ISO-8601 UTC");
                return;
            }

            cascade::Vec3 dv{};
            if (!parse_vec3_field(burn_obj, "deltaV_vector", dv)) {
                set_error_json(res, 422, "MISSING_DELTAV_VECTOR", "each burn must include deltaV_vector{x,y,z}");
                return;
            }

            const double dv_norm = dv_norm_km_s(dv);
            if (dv_norm > cascade::SAT_MAX_DELTAV_KM_S + cascade::EPS_NUM) {
                set_error_json(res, 422, "DELTA_V_EXCEEDS_LIMIT", "burn deltaV exceeds 15 m/s limit");
                return;
            }

            ScheduledBurn b;
            b.id = std::string(burn_id_sv);
            b.satellite_id = std::string(sat_sv);
            b.burn_epoch_s = burn_epoch_s;
            b.delta_v_km_s = dv;
            b.delta_v_norm_km_s = dv_norm;
            parsed_burns.push_back(std::move(b));
        }

        if (parsed_burns.empty()) {
            set_error_json(res, 422, "EMPTY_MANEUVER_SEQUENCE", "at least one burn is required");
            return;
        }

        std::sort(parsed_burns.begin(), parsed_burns.end(),
                  [](const ScheduledBurn& a, const ScheduledBurn& b) {
                      return a.burn_epoch_s < b.burn_epoch_s;
                  });

        for (std::size_t i = 1; i < parsed_burns.size(); ++i) {
            const double dt = parsed_burns[i].burn_epoch_s - parsed_burns[i - 1].burn_epoch_s;
            if (dt + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) {
                set_error_json(res, 422, "COOLDOWN_VIOLATION", "maneuver sequence violates 600s cooldown");
                return;
            }
        }

        const std::string sat_id(sat_sv);
        bool found_sat = false;
        bool cooldown_ok = true;
        bool los_ok = true;
        double projected_fuel_remaining = 0.0;
        double projected_mass_remaining = 0.0;
        double clock_epoch_s = 0.0;

        {
            std::unique_lock lock(g_mutex);

            clock_epoch_s = g_clock.epoch_s();
            const std::size_t idx = g_store.find(sat_id);
            if (idx < g_store.size() && g_store.type(idx) == cascade::ObjectType::SATELLITE) {
                found_sat = true;
                const double fuel_remaining = g_store.fuel_kg(idx);
                const double mass_remaining = g_store.mass_kg(idx);

                const auto last_it = g_last_burn_epoch_by_sat.find(sat_id);
                const double last_burn_epoch = (last_it == g_last_burn_epoch_by_sat.end())
                    ? -1.0e30
                    : last_it->second;

                if (!parsed_burns.empty()) {
                    const double from_last = parsed_burns.front().burn_epoch_s - last_burn_epoch;
                    if (from_last + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) {
                        cooldown_ok = false;
                    }
                }

                if (cooldown_ok) {
                    for (const ScheduledBurn& queued : g_burn_queue) {
                        if (queued.satellite_id != sat_id) continue;
                        for (const ScheduledBurn& incoming : parsed_burns) {
                            const double dt = std::abs(incoming.burn_epoch_s - queued.burn_epoch_s);
                            if (dt + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) {
                                cooldown_ok = false;
                                break;
                            }
                        }
                        if (!cooldown_ok) break;
                    }
                }

                for (const ScheduledBurn& b : parsed_burns) {
                    if (b.burn_epoch_s + cascade::EPS_NUM < clock_epoch_s) {
                        los_ok = false;
                        break;
                    }
                }

                if (los_ok) {
                    const cascade::Vec3 sat_eci{g_store.rx(idx), g_store.ry(idx), g_store.rz(idx)};
                    for (const ScheduledBurn& b : parsed_burns) {
                        if (!has_ground_station_los(sat_eci, b.burn_epoch_s)) {
                            los_ok = false;
                            break;
                        }
                    }
                }

                projected_fuel_remaining = fuel_remaining;
                projected_mass_remaining = mass_remaining;
                for (const ScheduledBurn& b : parsed_burns) {
                    const double fuel_need = propellant_used_kg(projected_mass_remaining, b.delta_v_norm_km_s);
                    projected_fuel_remaining -= fuel_need;
                    projected_mass_remaining -= fuel_need;
                }

                if (cooldown_ok && los_ok && projected_fuel_remaining > cascade::SAT_FUEL_EOL_KG) {
                    for (const ScheduledBurn& b : parsed_burns) {
                        g_burn_queue.push_back(b);
                    }
                }
            }
        }

        if (!found_sat) {
            set_error_json(res, 404, "SATELLITE_NOT_FOUND", "satelliteId does not reference a known SATELLITE object");
            return;
        }

        const bool sufficient_fuel = (projected_fuel_remaining > cascade::SAT_FUEL_EOL_KG);

        if (!los_ok) {
            set_error_json(res, 422, "GROUND_STATION_LOS_UNAVAILABLE", "no ground station LOS for one or more burn times");
            return;
        }

        if (!cooldown_ok) {
            set_error_json(res, 422, "COOLDOWN_VIOLATION", "maneuver sequence violates 600s cooldown");
            return;
        }

        if (!sufficient_fuel) {
            set_error_json(res, 422, "INSUFFICIENT_FUEL", "projected fuel after burns is below EOL threshold");
            return;
        }

        std::string out;
        out.reserve(192);
        out += "{\"status\":\"SCHEDULED\",\"validation\":{";
        out += "\"ground_station_los\":true,";
        out += "\"sufficient_fuel\":true";
        out += ",\"projected_mass_remaining_kg\":";
        out += cascade::fmt_double(projected_mass_remaining, 3);
        out += "}}";
        res.status = 202;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/simulate/step
    // ------------------------------------------------------------------
    svr.Post("/api/simulate/step", [](const httplib::Request& req, httplib::Response& res) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(req.body.data(), req.body.size());
        simdjson::ondemand::document doc;
        if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
            set_error_json(res, 400, "MALFORMED_JSON", "request body is not valid JSON");
            return;
        }

        simdjson::ondemand::object root;
        if (doc.get_object().get(root) != simdjson::SUCCESS) {
            set_error_json(res, 400, "INVALID_REQUEST", "root JSON value must be an object");
            return;
        }

        auto step_res = root.find_field_unordered("step_seconds").get_int64();
        if (step_res.error()) {
            set_error_json(res, 422, "MISSING_STEP_SECONDS", "field 'step_seconds' is required and must be an integer");
            return;
        }
        const int64_t step_s_i64 = step_res.value_unsafe();
        if (step_s_i64 <= 0) {
            set_error_json(res, 422, "INVALID_STEP_SECONDS", "step_seconds must be > 0");
            return;
        }

        const double step_s = static_cast<double>(step_s_i64);
        std::string new_ts;
        cascade::StepRunStats stats{};

        {
            std::unique_lock lock(g_mutex);

            if (!g_clock.is_initialized()) {
                set_error_json(res, 400, "CLOCK_UNINITIALIZED", "ingest telemetry before running simulate/step");
                return;
            }

            const bool ran = cascade::run_simulation_step(g_store, g_clock, step_s, stats, g_step_cfg);
            if (!ran) {
                set_error_json(res, 422, "SIM_STEP_REJECTED", "simulation step could not be executed");
                return;
            }

            const std::uint64_t tick_id = static_cast<std::uint64_t>(g_tick_count.load()) + 1;
            const std::uint64_t auto_planned = plan_collision_avoidance_burns(
                g_store,
                stats,
                g_clock.epoch_s(),
                tick_id
            );
            stats.maneuvers_executed = execute_due_maneuvers(g_store, g_clock.epoch_s());

            g_prop_stats.fast_last_tick = stats.used_fast;
            g_prop_stats.rk4_last_tick = stats.used_rk4;
            g_prop_stats.escalated_last_tick = stats.escalated_after_probe;
            g_prop_stats.narrow_pairs_last_tick = stats.narrow_pairs_checked;
            g_prop_stats.collisions_last_tick = stats.collisions_detected;
            g_prop_stats.maneuvers_last_tick = stats.maneuvers_executed;
            g_prop_stats.narrow_refined_pairs_last_tick = stats.narrow_refined_pairs;
            g_prop_stats.narrow_refine_cleared_last_tick = stats.narrow_refine_cleared;
            g_prop_stats.narrow_refine_fail_open_last_tick = stats.narrow_refine_fail_open;
            g_prop_stats.narrow_full_refined_pairs_last_tick = stats.narrow_full_refined_pairs;
            g_prop_stats.narrow_full_refine_cleared_last_tick = stats.narrow_full_refine_cleared;
            g_prop_stats.narrow_full_refine_fail_open_last_tick = stats.narrow_full_refine_fail_open;
            g_prop_stats.narrow_full_refine_budget_allocated_last_tick = stats.narrow_full_refine_budget_allocated;
            g_prop_stats.narrow_full_refine_budget_exhausted_last_tick = stats.narrow_full_refine_budget_exhausted;
            g_prop_stats.auto_planned_last_tick = auto_planned;
            g_prop_stats.broad_pairs_last_tick = stats.broad_pairs_considered;
            g_prop_stats.broad_candidates_last_tick = stats.broad_candidates;
            g_prop_stats.broad_overlap_pass_last_tick = stats.broad_shell_overlap_pass;
            g_prop_stats.broad_dcriterion_rejected_last_tick = stats.broad_dcriterion_rejected;
            g_prop_stats.broad_fail_open_objects_last_tick = stats.broad_fail_open_objects;
            g_prop_stats.broad_fail_open_satellites_last_tick = stats.broad_fail_open_satellites;
            g_prop_stats.broad_shell_margin_km_last_tick = stats.broad_shell_margin_km;
            g_prop_stats.broad_dcriterion_enabled_last_tick = stats.broad_dcriterion_enabled;
            g_prop_stats.broad_a_bin_width_km_last_tick = stats.broad_a_bin_width_km;
            g_prop_stats.broad_band_neighbor_bins_last_tick = stats.broad_band_neighbor_bins;

            g_prop_stats.fast_total += stats.used_fast;
            g_prop_stats.rk4_total += stats.used_rk4;
            g_prop_stats.escalated_total += stats.escalated_after_probe;
            g_prop_stats.narrow_pairs_total += stats.narrow_pairs_checked;
            g_prop_stats.collisions_total += stats.collisions_detected;
            g_prop_stats.maneuvers_total += stats.maneuvers_executed;
            g_prop_stats.narrow_refined_pairs_total += stats.narrow_refined_pairs;
            g_prop_stats.narrow_refine_cleared_total += stats.narrow_refine_cleared;
            g_prop_stats.narrow_refine_fail_open_total += stats.narrow_refine_fail_open;
            g_prop_stats.narrow_full_refined_pairs_total += stats.narrow_full_refined_pairs;
            g_prop_stats.narrow_full_refine_cleared_total += stats.narrow_full_refine_cleared;
            g_prop_stats.narrow_full_refine_fail_open_total += stats.narrow_full_refine_fail_open;
            g_prop_stats.narrow_full_refine_budget_allocated_total += stats.narrow_full_refine_budget_allocated;
            g_prop_stats.narrow_full_refine_budget_exhausted_total += stats.narrow_full_refine_budget_exhausted;
            g_prop_stats.auto_planned_total += auto_planned;
            g_prop_stats.broad_pairs_total += stats.broad_pairs_considered;
            g_prop_stats.broad_candidates_total += stats.broad_candidates;
            g_prop_stats.broad_overlap_pass_total += stats.broad_shell_overlap_pass;
            g_prop_stats.broad_dcriterion_rejected_total += stats.broad_dcriterion_rejected;
            g_prop_stats.broad_fail_open_objects_total += stats.broad_fail_open_objects;
            g_prop_stats.broad_fail_open_satellites_total += stats.broad_fail_open_satellites;

            new_ts = g_clock.to_iso();
        }

        ++g_tick_count;

        std::string out;
        out.reserve(160);
        out += "{\"status\":\"STEP_COMPLETE\",\"new_timestamp\":";
        cascade::append_json_string(out, new_ts);
        out += ",\"collisions_detected\":";
        out += std::to_string(stats.collisions_detected);
        out += ",\"maneuvers_executed\":";
        out += std::to_string(stats.maneuvers_executed);
        out += '}';
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/visualization/snapshot
    // ------------------------------------------------------------------
    svr.Get("/api/visualization/snapshot", [](const httplib::Request&, httplib::Response& res) {
        std::string snapshot;
        {
            std::shared_lock lock(g_mutex);
            snapshot = build_snapshot(g_store, g_clock);
        }
        res.status = 200;
        res.set_content(snapshot, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/status
    // ------------------------------------------------------------------
    svr.Get("/api/status", [](const httplib::Request& req, httplib::Response& res) {
        const auto truthy = [](const std::string& v) {
            return v == "1" || v == "true" || v == "yes" || v == "on";
        };

        bool include_details = false;
        if (req.has_param("details")) {
            include_details = truthy(req.get_param_value("details"));
        }
        if (!include_details && req.has_param("verbose")) {
            include_details = truthy(req.get_param_value("verbose"));
        }

        std::size_t obj_count = 0;
        std::size_t sat_count = 0;
        std::size_t deb_count = 0;
        std::size_t pending_burn_queue = 0;
        double uptime = 0.0;
        std::uint64_t failed_last_tick = 0;
        std::uint64_t failed_total = 0;
        std::uint64_t tick_count = static_cast<std::uint64_t>(g_tick_count.load());
        PropagationStats prop{};
        {
            std::shared_lock lock(g_mutex);
            obj_count = g_store.size();
            sat_count = g_store.satellite_count();
            deb_count = g_store.debris_count();
            pending_burn_queue = g_burn_queue.size();
            uptime = g_clock.uptime_s();
            failed_last_tick = g_store.failed_last_tick();
            failed_total = g_store.failed_propagation_total();
            prop = g_prop_stats;
        }

        std::string out;
        out.reserve(include_details ? 960 : 128);
        out += "{\"status\":\"NOMINAL\",\"uptime_s\":";
        out += cascade::fmt_double(uptime, 1);
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
            out += ",\"failed_objects_last_tick\":";
            out += std::to_string(failed_last_tick);
            out += ",\"failed_objects_total\":";
            out += std::to_string(failed_total);
            out += ",\"propagation_last_tick\":{";
            out += "\"narrow_pairs_checked\":";
            out += std::to_string(prop.narrow_pairs_last_tick);
            out += ",\"collisions_detected\":";
            out += std::to_string(prop.collisions_last_tick);
            out += ",\"maneuvers_executed\":";
            out += std::to_string(prop.maneuvers_last_tick);
            out += ",\"auto_planned_maneuvers\":";
            out += std::to_string(prop.auto_planned_last_tick);
            out += ",\"narrow_refined_pairs\":";
            out += std::to_string(prop.narrow_refined_pairs_last_tick);
            out += ",\"narrow_full_refined_pairs\":";
            out += std::to_string(prop.narrow_full_refined_pairs_last_tick);
            out += ",\"narrow_full_refine_budget_allocated\":";
            out += std::to_string(prop.narrow_full_refine_budget_allocated_last_tick);
            out += ",\"narrow_full_refine_budget_exhausted\":";
            out += std::to_string(prop.narrow_full_refine_budget_exhausted_last_tick);
            out += ",\"broad_candidates\":";
            out += std::to_string(prop.broad_candidates_last_tick);
            out += ",\"broad_pairs_considered\":";
            out += std::to_string(prop.broad_pairs_last_tick);
            out += "},\"propagation_totals\":{";
            out += "\"narrow_pairs_checked\":";
            out += std::to_string(prop.narrow_pairs_total);
            out += ",\"collisions_detected\":";
            out += std::to_string(prop.collisions_total);
            out += ",\"maneuvers_executed\":";
            out += std::to_string(prop.maneuvers_total);
            out += ",\"auto_planned_maneuvers\":";
            out += std::to_string(prop.auto_planned_total);
            out += ",\"narrow_refined_pairs\":";
            out += std::to_string(prop.narrow_refined_pairs_total);
            out += ",\"narrow_full_refined_pairs\":";
            out += std::to_string(prop.narrow_full_refined_pairs_total);
            out += ",\"narrow_full_refine_budget_allocated\":";
            out += std::to_string(prop.narrow_full_refine_budget_allocated_total);
            out += ",\"narrow_full_refine_budget_exhausted\":";
            out += std::to_string(prop.narrow_full_refine_budget_exhausted_total);
            out += ",\"broad_candidates\":";
            out += std::to_string(prop.broad_candidates_total);
            out += ",\"broad_pairs_considered\":";
            out += std::to_string(prop.broad_pairs_total);
            out += "}}";
        }

        out += '}';
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/debug/conflicts
    // ------------------------------------------------------------------
    svr.Get("/api/debug/conflicts", [](const httplib::Request&, httplib::Response& res) {
        std::string out;
        {
            std::shared_lock lock(g_mutex);
            out = build_conflicts_json(g_store);
        }
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/debug/propagation
    // ------------------------------------------------------------------
    svr.Get("/api/debug/propagation", [](const httplib::Request&, httplib::Response& res) {
        std::string out;
        {
            std::shared_lock lock(g_mutex);
            out = build_propagation_json(g_store, g_prop_stats, g_step_cfg);
        }
        res.status = 200;
        res.set_content(out, "application/json");
    });

    if (!svr.listen("0.0.0.0", 8000)) {
        std::cerr << "FATAL: failed to bind to 0.0.0.0:8000\n";
        return 1;
    }

    return 0;
}
