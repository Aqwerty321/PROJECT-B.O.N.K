// ---------------------------------------------------------------------------
// maneuver_common.cpp — shared maneuver/ops helpers
// ---------------------------------------------------------------------------

#include "maneuver_common.hpp"

#include "earth_frame.hpp"
#include "orbit_math.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>

namespace cascade {

namespace {

struct GroundStationRaw {
    const char* id;
    double lat_deg;
    double lon_deg;
    double alt_km;
    double min_el_deg;
};

constexpr GroundStationRaw k_default_ground_stations[] = {
    {"GS-001", 13.0333,   77.5167, 0.820,  5.0},
    {"GS-002", 78.2297,   15.4077, 0.400,  5.0},
    {"GS-003", 35.4266, -116.8900, 1.000, 10.0},
    {"GS-004",-53.1500,  -70.9167, 0.030,  5.0},
    {"GS-005", 28.5450,   77.1926, 0.225, 15.0},
    {"GS-006",-77.8463,  166.6682, 0.010,  5.0}
};

struct GroundStationCatalog {
    std::vector<GroundStation> stations;
    std::string source;
};

std::string_view trim_ascii(std::string_view raw) noexcept
{
    std::size_t begin = 0;
    std::size_t end = raw.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(raw[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
        --end;
    }

    return raw.substr(begin, end - begin);
}

bool parse_double_field(std::string_view raw,
                        double& out) noexcept
{
    const std::string token(trim_ascii(raw));
    if (token.empty()) {
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }

    out = parsed;
    return true;
}

bool parse_ground_station_csv_line(std::string_view line,
                                   GroundStation& out) noexcept
{
    std::vector<std::string_view> fields;
    fields.reserve(6);

    std::size_t begin = 0;
    while (begin <= line.size()) {
        const std::size_t comma = line.find(',', begin);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(begin));
            break;
        }
        fields.push_back(line.substr(begin, comma - begin));
        begin = comma + 1;
    }

    if (fields.size() != 6) {
        return false;
    }

    out.id = std::string(trim_ascii(fields[0]));
    if (out.id.empty()) {
        return false;
    }

    double alt_m = 0.0;
    if (!parse_double_field(fields[2], out.lat_deg)
        || !parse_double_field(fields[3], out.lon_deg)
        || !parse_double_field(fields[4], alt_m)
        || !parse_double_field(fields[5], out.min_el_deg)) {
        return false;
    }

    out.alt_km = alt_m * 1.0e-3;
    return true;
}

std::vector<GroundStation> default_ground_stations()
{
    std::vector<GroundStation> out;
    out.reserve(std::size(k_default_ground_stations));
    for (const GroundStationRaw& raw : k_default_ground_stations) {
        GroundStation gs;
        gs.id = raw.id;
        gs.lat_deg = raw.lat_deg;
        gs.lon_deg = raw.lon_deg;
        gs.alt_km = raw.alt_km;
        gs.min_el_deg = raw.min_el_deg;
        out.push_back(std::move(gs));
    }
    return out;
}

bool load_ground_stations_from_csv(const std::string& path,
                                   std::vector<GroundStation>& out) noexcept
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::vector<GroundStation> loaded;
    loaded.reserve(16);

    std::string line;
    while (std::getline(in, line)) {
        const std::string_view trimmed = trim_ascii(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.front() == '#') {
            continue;
        }
        if (trimmed.rfind("Station_ID", 0) == 0) {
            continue;
        }

        GroundStation gs;
        if (!parse_ground_station_csv_line(trimmed, gs)) {
            continue;
        }
        loaded.push_back(std::move(gs));
    }

    if (loaded.empty()) {
        return false;
    }

    out = std::move(loaded);
    return true;
}

GroundStationCatalog build_ground_station_catalog()
{
    GroundStationCatalog catalog;

    std::vector<std::string> candidates;
    if (const char* env_path = std::getenv("PROJECTBONK_GROUND_STATIONS_CSV");
        env_path != nullptr && *env_path != '\0') {
        candidates.emplace_back(env_path);
    }
    candidates.emplace_back("docs/groundstations.csv");
    candidates.emplace_back("../docs/groundstations.csv");
    candidates.emplace_back("groundstations.csv");
    candidates.emplace_back("../groundstations.csv");

    for (const std::string& path : candidates) {
        std::vector<GroundStation> loaded;
        if (!load_ground_stations_from_csv(path, loaded)) {
            continue;
        }

        catalog.stations = std::move(loaded);
        catalog.source = "file:" + path;
        return catalog;
    }

    catalog.stations = default_ground_stations();
    catalog.source = "builtin:ps_default";
    return catalog;
}

const GroundStationCatalog& ground_station_catalog()
{
    static const GroundStationCatalog catalog = build_ground_station_catalog();
    return catalog;
}

bool can_schedule_burn_now(const StateStore& store,
                           const std::vector<ScheduledBurn>& burn_queue,
                           const std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                           std::size_t sat_idx,
                           double epoch_s) noexcept
{
    if (sat_idx >= store.size() || store.type(sat_idx) != ObjectType::SATELLITE) {
        return false;
    }

    if (store.sat_status(sat_idx) == SatStatus::OFFLINE) {
        return false;
    }

    const auto& sat_id = store.id(sat_idx);
    const auto last_it = last_burn_epoch_by_sat.find(sat_id);
    if (last_it != last_burn_epoch_by_sat.end()) {
        const double dt = epoch_s - last_it->second;
        if (dt + EPS_NUM < SAT_COOLDOWN_S) {
            return false;
        }
    }

    if (has_pending_burn_in_cooldown_window(burn_queue, sat_id, epoch_s)) {
        return false;
    }

    return true;
}

bool choose_burn_epoch_with_upload_impl(const StateStore& store,
                                        const std::vector<ScheduledBurn>& burn_queue,
                                        const std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                        std::size_t sat_idx,
                                        double now_epoch_s,
                                        double earliest_burn_epoch_s,
                                        double latest_burn_epoch_s,
                                        double& out_burn_epoch_s,
                                        double& out_upload_epoch_s,
                                        std::string& out_station_id) noexcept
{
    const double start = std::max(earliest_burn_epoch_s, now_epoch_s + ops_signal_latency_s());
    if (latest_burn_epoch_s + EPS_NUM < start) {
        return false;
    }

    for (double t = start; t <= latest_burn_epoch_s + EPS_NUM; t += ops_upload_scan_step_s()) {
        if (!can_schedule_burn_now(store, burn_queue, last_burn_epoch_by_sat, sat_idx, t)) {
            continue;
        }

        double upload_epoch = 0.0;
        std::string station;
        if (compute_upload_plan_for_burn(store, sat_idx, now_epoch_s, t, upload_epoch, station)) {
            out_burn_epoch_s = t;
            out_upload_epoch_s = upload_epoch;
            out_station_id = station;
            return true;
        }
    }

    return false;
}

} // namespace

bool choose_burn_epoch_with_upload(const StateStore& store,
                                   const std::vector<ScheduledBurn>& burn_queue,
                                   const std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                   std::size_t sat_idx,
                                   double now_epoch_s,
                                   double earliest_burn_epoch_s,
                                   double latest_burn_epoch_s,
                                   double& out_burn_epoch_s,
                                   double& out_upload_epoch_s,
                                   std::string& out_station_id) noexcept
{
    return choose_burn_epoch_with_upload_impl(
        store,
        burn_queue,
        last_burn_epoch_by_sat,
        sat_idx,
        now_epoch_s,
        earliest_burn_epoch_s,
        latest_burn_epoch_s,
        out_burn_epoch_s,
        out_upload_epoch_s,
        out_station_id
    );
}

bool get_current_elements(const StateStore& store,
                          std::size_t idx,
                          OrbitalElements& out) noexcept
{
    if (idx >= store.size()) return false;

    if (store.elements_valid(idx)) {
        out.a_km = store.a_km(idx);
        out.e = store.e(idx);
        out.i_rad = store.i_rad(idx);
        out.raan_rad = store.raan_rad(idx);
        out.argp_rad = store.argp_rad(idx);
        out.M_rad = store.M_rad(idx);
        out.n_rad_s = store.n_rad_s(idx);
        out.p_km = store.p_km(idx);
        out.rp_km = store.rp_km(idx);
        out.ra_km = store.ra_km(idx);
        return true;
    }

    const Vec3 r{store.rx(idx), store.ry(idx), store.rz(idx)};
    const Vec3 v{store.vx(idx), store.vy(idx), store.vz(idx)};
    return eci_to_elements(r, v, out);
}

OrbitalElements derive_slot_elements_if_needed(const StateStore& store,
                                                std::size_t sat_idx,
                                                std::unordered_map<std::string, SlotReference>& slot_reference_by_sat) noexcept
{
    const auto& sat_id = store.id(sat_idx);
    auto it = slot_reference_by_sat.find(sat_id);
    if (it != slot_reference_by_sat.end()) {
        return it->second.elements;
    }

    // P3.2: Late bootstrapping fallback. If we reach here, the slot was not
    // established during telemetry ingestion (unusual). We still create one
    // from current state but mark it as a non-telemetry bootstrap so callers
    // can distinguish.
    OrbitalElements cur{};
    if (get_current_elements(store, sat_idx, cur)) {
        SlotReference ref;
        ref.elements = cur;
        ref.reference_epoch_s = store.telemetry_epoch_s(sat_idx);
        ref.bootstrapped_from_telemetry = false;
        slot_reference_by_sat[sat_id] = ref;
        return cur;
    }

    return cur;
}

bool slot_elements_at_epoch(const std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                            const std::string& sat_id,
                            double epoch_s,
                            OrbitalElements& out) noexcept
{
    auto it = slot_reference_by_sat.find(sat_id);
    if (it == slot_reference_by_sat.end()) {
        return false;
    }

    out = it->second.elements;
    const double dt = epoch_s - it->second.reference_epoch_s;
    if (std::abs(dt) > EPS_NUM) {
        apply_j2_secular(out, dt);
        out.n_rad_s = std::sqrt(MU_KM3_S2 / (out.a_km * out.a_km * out.a_km));
        out.p_km = out.a_km * (1.0 - out.e * out.e);
        out.rp_km = out.a_km * (1.0 - out.e);
        out.ra_km = out.a_km * (1.0 + out.e);
    }
    return true;
}

bool slot_radius_error_km_at_epoch(const StateStore& store,
                                   std::size_t sat_idx,
                                   double epoch_s,
                                   const std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                                   double& out_error_km) noexcept
{
    if (sat_idx >= store.size()) return false;
    if (store.type(sat_idx) != ObjectType::SATELLITE) return false;

    OrbitalElements slot_el{};
    if (!slot_elements_at_epoch(slot_reference_by_sat, store.id(sat_idx), epoch_s, slot_el)) {
        return false;
    }

    Vec3 slot_r{};
    Vec3 slot_v{};
    if (!elements_to_eci(slot_el, slot_r, slot_v)) {
        return false;
    }

    const double dx = store.rx(sat_idx) - slot_r.x;
    const double dy = store.ry(sat_idx) - slot_r.y;
    const double dz = store.rz(sat_idx) - slot_r.z;
    out_error_km = std::sqrt(dx * dx + dy * dy + dz * dz);
    return true;
}

namespace {

// Slot error normalization scales — env-overridable once on first call.
struct SlotErrorScales {
    double a_km;
    double e;
    double i_rad;
    double raan_rad;
};

static const SlotErrorScales& slot_error_scales() noexcept
{
    static const SlotErrorScales s{
        detail::ops_env_double("PROJECTBONK_SLOT_NORM_A_KM", 10.0),
        detail::ops_env_double("PROJECTBONK_SLOT_NORM_E", 1e-3),
        detail::ops_env_double("PROJECTBONK_SLOT_NORM_I_RAD", 1e-3),
        detail::ops_env_double("PROJECTBONK_SLOT_NORM_RAAN_RAD", 1e-3),
    };
    return s;
}

} // namespace

double slot_error_score(const OrbitalElements& slot,
                        const OrbitalElements& cur) noexcept
{
    const double da = std::abs(slot.a_km - cur.a_km);
    const double de = std::abs(slot.e - cur.e);
    const double di = std::abs(slot.i_rad - cur.i_rad);
    const double d_raan = std::abs(wrap_0_2pi(slot.raan_rad - cur.raan_rad + PI) - PI);

    // P3.1: Include mean anomaly to capture along-track (phasing) drift.
    // Mean anomaly difference is wrapped to [-pi, pi] and normalized by the
    // along-track position error: dM (rad) * a (km) gives approximate arc-km,
    // then normalized by the stationkeeping box radius for unit consistency.
    const double d_M = std::abs(wrap_0_2pi(slot.M_rad - cur.M_rad + PI) - PI);
    const double along_track_km = d_M * cur.a_km;

    const auto& ses = slot_error_scales();
    return (da / ses.a_km) + (de / ses.e) + (di / ses.i_rad) + (d_raan / ses.raan_rad)
         + (along_track_km / ops_stationkeeping_box_radius_km());
}

double dv_norm_km_s(const Vec3& dv) noexcept
{
    return std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);
}

double propellant_used_kg(double mass_kg,
                          double delta_v_km_s) noexcept
{
    if (!(mass_kg > 0.0) || !(delta_v_km_s > 0.0)) {
        return 0.0;
    }
    const double ve_km_s = SAT_ISP_S * G0_KM_S2;
    if (!(ve_km_s > 0.0)) {
        return 0.0;
    }
    const double mass_ratio = std::exp(-delta_v_km_s / ve_km_s);
    return mass_kg * (1.0 - mass_ratio);
}

bool has_ground_station_los(const Vec3& sat_eci_km,
                            double epoch_s) noexcept
{
    const Vec3 sat_ecef = eci_to_ecef(sat_eci_km, epoch_s);

    for (const GroundStation& gs : ground_station_catalog().stations) {
        const double lat_rad = gs.lat_deg * PI / 180.0;
        const double lon_rad = gs.lon_deg * PI / 180.0;
        const double min_el_rad = gs.min_el_deg * PI / 180.0;
        const double el = elevation_angle_rad(
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

bool predict_satellite_eci_at_epoch(const StateStore& store,
                                    std::size_t sat_idx,
                                    double epoch_s,
                                    Vec3& out_sat_eci_km) noexcept
{
    if (sat_idx >= store.size()) return false;
    if (store.type(sat_idx) != ObjectType::SATELLITE) return false;

    const double obj_epoch = store.telemetry_epoch_s(sat_idx);
    const double dt = epoch_s - obj_epoch;
    if (dt <= EPS_NUM) {
        out_sat_eci_km = Vec3{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
        return true;
    }

    OrbitalElements el{};
    if (!get_current_elements(store, sat_idx, el)) {
        return false;
    }

    apply_j2_secular(el, dt);
    Vec3 r{};
    Vec3 v{};
    if (!elements_to_eci(el, r, v)) {
        return false;
    }

    out_sat_eci_km = r;
    return true;
}

bool has_ground_station_los_for_sat_epoch(const StateStore& store,
                                          std::size_t sat_idx,
                                          double epoch_s,
                                          std::string* station_id_out) noexcept
{
    Vec3 sat_eci{};
    if (!predict_satellite_eci_at_epoch(store, sat_idx, epoch_s, sat_eci)) {
        return false;
    }

    const Vec3 sat_ecef = eci_to_ecef(sat_eci, epoch_s);
    for (const GroundStation& gs : ground_station_catalog().stations) {
        const double lat_rad = gs.lat_deg * PI / 180.0;
        const double lon_rad = gs.lon_deg * PI / 180.0;
        const double min_el_rad = gs.min_el_deg * PI / 180.0;
        const double el = elevation_angle_rad(
            sat_ecef,
            lat_rad,
            lon_rad,
            gs.alt_km
        );
        if (el >= min_el_rad) {
            if (station_id_out != nullptr) {
                *station_id_out = gs.id;
            }
            return true;
        }
    }

    return false;
}

bool find_latest_upload_slot_epoch(const StateStore& store,
                                   std::size_t sat_idx,
                                   double now_epoch_s,
                                   double burn_epoch_s,
                                   double& out_upload_epoch_s,
                                   std::string& out_station_id) noexcept
{
    const double latest_upload = burn_epoch_s - ops_signal_latency_s();
    if (latest_upload < now_epoch_s + EPS_NUM) {
        return false;
    }

    bool found = false;
    double best_epoch = now_epoch_s;
    std::string best_station;

    for (double t = now_epoch_s; t <= latest_upload + EPS_NUM; t += ops_upload_scan_step_s()) {
        std::string station;
        if (has_ground_station_los_for_sat_epoch(store, sat_idx, t, &station)) {
            found = true;
            best_epoch = t;
            best_station = station;
        }
    }

    if (!found) {
        std::string station;
        if (has_ground_station_los_for_sat_epoch(store, sat_idx, latest_upload, &station)) {
            found = true;
            best_epoch = latest_upload;
            best_station = station;
        }
    }

    if (!found) return false;

    out_upload_epoch_s = best_epoch;
    out_station_id = best_station;
    return true;
}

bool find_earliest_upload_slot_epoch(const StateStore& store,
                                     std::size_t sat_idx,
                                     double start_epoch_s,
                                     double end_epoch_s,
                                     double& out_upload_epoch_s,
                                     std::string& out_station_id) noexcept
{
    if (end_epoch_s < start_epoch_s + EPS_NUM) {
        return false;
    }

    for (double t = start_epoch_s; t <= end_epoch_s + EPS_NUM; t += ops_upload_scan_step_s()) {
        std::string station;
        if (has_ground_station_los_for_sat_epoch(store, sat_idx, t, &station)) {
            out_upload_epoch_s = t;
            out_station_id = station;
            return true;
        }
    }

    return false;
}

bool compute_upload_plan_for_burn(const StateStore& store,
                                  std::size_t sat_idx,
                                  double now_epoch_s,
                                  double burn_epoch_s,
                                  double& out_upload_epoch_s,
                                  std::string& out_station_id) noexcept
{
    return find_latest_upload_slot_epoch(
        store,
        sat_idx,
        now_epoch_s,
        burn_epoch_s,
        out_upload_epoch_s,
        out_station_id
    );
}

bool upload_window_ready_for_execution(const ScheduledBurn& burn,
                                       double current_epoch_s) noexcept
{
    if (burn.upload_epoch_s <= 0.0) {
        return false;
    }
    if (burn.upload_epoch_s > burn.burn_epoch_s - ops_signal_latency_s() + EPS_NUM) {
        return false;
    }
    if (burn.upload_epoch_s > current_epoch_s + EPS_NUM) {
        return false;
    }
    return true;
}

bool has_pending_burn_in_cooldown_window(const std::vector<ScheduledBurn>& burn_queue,
                                         const std::string& sat_id,
                                         double epoch_s) noexcept
{
    for (const ScheduledBurn& b : burn_queue) {
        if (b.satellite_id != sat_id) continue;
        const double dt = std::abs(b.burn_epoch_s - epoch_s);
        if (dt + EPS_NUM < SAT_COOLDOWN_S) {
            return true;
        }
    }
    return false;
}

bool has_any_pending_burn(const std::vector<ScheduledBurn>& burn_queue,
                          const std::string& sat_id) noexcept
{
    for (const ScheduledBurn& b : burn_queue) {
        if (b.satellite_id == sat_id) {
            return true;
        }
    }
    return false;
}

bool should_request_graveyard(const StateStore& store,
                              std::size_t sat_idx,
                              const std::unordered_map<std::string, bool>& graveyard_completed_by_sat) noexcept
{
    if (sat_idx >= store.size()) return false;
    if (store.type(sat_idx) != ObjectType::SATELLITE) return false;
    if (store.sat_status(sat_idx) == SatStatus::OFFLINE) return false;

    const auto& sat_id = store.id(sat_idx);
    auto done_it = graveyard_completed_by_sat.find(sat_id);
    if (done_it != graveyard_completed_by_sat.end() && done_it->second) {
        return false;
    }

    return store.fuel_kg(sat_idx) <= SAT_FUEL_EOL_KG + EPS_NUM;
}

GraveyardPlanStats plan_graveyard_burns(StateStore& store,
                                        double current_epoch_s,
                                        std::uint64_t tick_id,
                                        std::vector<ScheduledBurn>& burn_queue,
                                        std::unordered_map<std::string, bool>& graveyard_requested_by_sat,
                                        const std::unordered_map<std::string, bool>& graveyard_completed_by_sat)
{
    GraveyardPlanStats stats{};

    std::unordered_map<std::string, double> no_last_burn{};

    for (std::size_t sat_idx = 0; sat_idx < store.size(); ++sat_idx) {
        if (!should_request_graveyard(store, sat_idx, graveyard_completed_by_sat)) {
            continue;
        }

        const auto& sat_id = store.id(sat_idx);
        graveyard_requested_by_sat[sat_id] = true;

        if (has_any_pending_burn(burn_queue, sat_id)
            || has_pending_burn_in_cooldown_window(burn_queue, sat_id, current_epoch_s)) {
            ++stats.deferred;
            continue;
        }

        const Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};
        const double v_norm = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (v_norm < EPS_NUM) {
            ++stats.deferred;
            continue;
        }

        const double graveyard_dv = ops_graveyard_target_dv_km_s();
        const Vec3 dv{
            (v.x / v_norm) * graveyard_dv,
            (v.y / v_norm) * graveyard_dv,
            (v.z / v_norm) * graveyard_dv
        };

        // P2.2: Verify satellite has enough fuel for the graveyard burn.
        const double mass_before = store.mass_kg(sat_idx);
        const double fuel_before = store.fuel_kg(sat_idx);
        const double fuel_needed = propellant_used_kg(mass_before, graveyard_dv);
        if (fuel_before < fuel_needed + EPS_NUM) {
            // Not enough fuel even for graveyard — mark as fuel-low and skip.
            store.set_sat_status(sat_idx, SatStatus::FUEL_LOW);
            ++stats.deferred;
            continue;
        }

        const double burn_epoch = current_epoch_s + SAT_COOLDOWN_S;
        double upload_epoch = 0.0;
        std::string upload_station;
        if (!compute_upload_plan_for_burn(
                store,
                sat_idx,
                current_epoch_s,
                burn_epoch,
                upload_epoch,
                upload_station)) {
            ++stats.deferred;
            continue;
        }

        ScheduledBurn burn;
        burn.id = "AUTO-GRAVEYARD-" + std::to_string(tick_id) + "-" + std::to_string(stats.planned);
        burn.satellite_id = sat_id;
        burn.upload_station_id = upload_station;
        burn.upload_epoch_s = upload_epoch;
        burn.burn_epoch_s = burn_epoch;
        burn.delta_v_km_s = dv;
        burn.delta_v_norm_km_s = graveyard_dv;
        burn.auto_generated = true;
        burn.recovery_burn = false;
        burn.graveyard_burn = true;
        burn_queue.push_back(std::move(burn));

        ++stats.planned;
    }

    return stats;
}

void accumulate_recovery_request(const ScheduledBurn& burn,
                                 double current_epoch_s,
                                 std::unordered_map<std::string, RecoveryRequest>& recovery_requests_by_sat) noexcept
{
    if (!burn.auto_generated || burn.recovery_burn) {
        return;
    }

    RecoveryRequest& req = recovery_requests_by_sat[burn.satellite_id];
    req.remaining_delta_v_km_s.x -= burn.delta_v_km_s.x;
    req.remaining_delta_v_km_s.y -= burn.delta_v_km_s.y;
    req.remaining_delta_v_km_s.z -= burn.delta_v_km_s.z;

    const double earliest = burn.burn_epoch_s + SAT_COOLDOWN_S;
    if (req.earliest_epoch_s < current_epoch_s) {
        req.earliest_epoch_s = earliest;
    } else {
        req.earliest_epoch_s = std::max(req.earliest_epoch_s, earliest);
    }
}

ManeuverExecStats execute_due_maneuvers(StateStore& store,
                                        double current_epoch_s,
                                        std::vector<ScheduledBurn>& burn_queue,
                                        std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                        std::unordered_map<std::string, RecoveryRequest>& recovery_requests_by_sat,
                                        std::unordered_map<std::string, bool>& graveyard_requested_by_sat,
                                        std::unordered_map<std::string, bool>& graveyard_completed_by_sat)
{
    ManeuverExecStats stats{};
    std::vector<ScheduledBurn> pending;
    pending.reserve(burn_queue.size());

    for (ScheduledBurn& burn : burn_queue) {
        if (burn.burn_epoch_s > current_epoch_s + EPS_NUM) {
            pending.push_back(std::move(burn));
            continue;
        }

        const std::size_t idx = store.find(burn.satellite_id);
        if (idx >= store.size() || store.type(idx) != ObjectType::SATELLITE) {
            continue;
        }

        if (!upload_window_ready_for_execution(burn, current_epoch_s)) {
            ++stats.upload_missed;
            continue;
        }

        const double mass_before = store.mass_kg(idx);
        const double fuel_before = store.fuel_kg(idx);
        const double fuel_needed = propellant_used_kg(mass_before, burn.delta_v_norm_km_s);
        if (fuel_needed > fuel_before + EPS_NUM) {
            continue;
        }

        store.vx_mut(idx) += burn.delta_v_km_s.x;
        store.vy_mut(idx) += burn.delta_v_km_s.y;
        store.vz_mut(idx) += burn.delta_v_km_s.z;
        store.set_elements(idx, OrbitalElements{}, false);

        const double fuel_after = std::max(0.0, fuel_before - fuel_needed);
        const double mass_after = std::max(SAT_DRY_MASS_KG, mass_before - fuel_needed);
        store.fuel_kg_mut(idx) = fuel_after;
        store.mass_kg_mut(idx) = mass_after;

        if (fuel_after <= SAT_FUEL_EOL_KG) {
            store.set_sat_status(idx, SatStatus::FUEL_LOW);
        } else {
            store.set_sat_status(idx, SatStatus::MANEUVERING);
        }

        last_burn_epoch_by_sat[burn.satellite_id] = burn.burn_epoch_s;

        if (burn.recovery_burn) {
            ++stats.recovery_completed;
            recovery_requests_by_sat.erase(burn.satellite_id);
            if (!burn.graveyard_burn) {
                graveyard_requested_by_sat[burn.satellite_id] = false;
            }
        } else {
            accumulate_recovery_request(burn, current_epoch_s, recovery_requests_by_sat);
            ++stats.recovery_pending_marked;
        }

        if (burn.graveyard_burn) {
            ++stats.graveyard_completed;
            graveyard_completed_by_sat[burn.satellite_id] = true;
            graveyard_requested_by_sat[burn.satellite_id] = false;
            store.set_sat_status(idx, SatStatus::OFFLINE);
            recovery_requests_by_sat.erase(burn.satellite_id);
        } else if (graveyard_requested_by_sat[burn.satellite_id]) {
            store.set_sat_status(idx, SatStatus::FUEL_LOW);
        }

        ++stats.executed;
    }

    burn_queue.swap(pending);
    return stats;
}

void validate_pending_upload_windows(StateStore& store,
                                     double current_epoch_s,
                                     std::vector<ScheduledBurn>& burn_queue,
                                     std::uint64_t& upload_missed)
{
    std::vector<ScheduledBurn> retained;
    retained.reserve(burn_queue.size());

    for (ScheduledBurn& b : burn_queue) {
        if (b.burn_epoch_s <= current_epoch_s + EPS_NUM) {
            retained.push_back(std::move(b));
            continue;
        }

        if (b.upload_epoch_s <= 0.0) {
            ++upload_missed;
            continue;
        }

        const std::size_t idx = store.find(b.satellite_id);
        if (idx >= store.size() || store.type(idx) != ObjectType::SATELLITE) {
            retained.push_back(std::move(b));
            continue;
        }

        double new_upload_epoch = 0.0;
        std::string new_station;
        const bool ok = compute_upload_plan_for_burn(
            store,
            idx,
            current_epoch_s,
            b.burn_epoch_s,
            new_upload_epoch,
            new_station
        );
        if (!ok) {
            ++upload_missed;
            continue;
        }

        b.upload_epoch_s = new_upload_epoch;
        b.upload_station_id = new_station;
        retained.push_back(std::move(b));
    }

    burn_queue.swap(retained);
}

std::size_t active_ground_station_count() noexcept
{
    return ground_station_catalog().stations.size();
}

bool active_ground_station_has_id(std::string_view station_id) noexcept
{
    if (station_id.empty()) {
        return false;
    }

    for (const GroundStation& gs : ground_station_catalog().stations) {
        if (gs.id == station_id) {
            return true;
        }
    }
    return false;
}

std::string active_ground_station_source()
{
    return ground_station_catalog().source;
}

} // namespace cascade
