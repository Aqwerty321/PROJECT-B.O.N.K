// ---------------------------------------------------------------------------
// maneuver_recovery_planner.cpp — slot-targeted recovery planner helper
// ---------------------------------------------------------------------------

#include "maneuver_recovery_planner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cascade {

namespace {

double env_double(std::string_view key,
                  double default_value,
                  double min_value,
                  double max_value) noexcept
{
    const char* raw = std::getenv(std::string(key).c_str());
    if (raw == nullptr) {
        return default_value;
    }

    char* end = nullptr;
    const double parsed = std::strtod(raw, &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return default_value;
    }

    if (parsed < min_value || parsed > max_value) {
        return default_value;
    }

    return parsed;
}

} // namespace

RecoveryPlannerConfig recovery_planner_config_from_env()
{
    RecoveryPlannerConfig cfg;
    cfg.scale_t = env_double("PROJECTBONK_RECOVERY_SCALE_T", cfg.scale_t, 0.0, 1e-2);
    cfg.scale_r = env_double("PROJECTBONK_RECOVERY_SCALE_R", cfg.scale_r, 0.0, 1e-1);
    cfg.radial_share = env_double("PROJECTBONK_RECOVERY_RADIAL_SHARE", cfg.radial_share, 0.0, 2.0);
    cfg.scale_n = env_double("PROJECTBONK_RECOVERY_SCALE_N", cfg.scale_n, 0.0, 1e-1);
    cfg.fallback_norm_km_s = env_double(
        "PROJECTBONK_RECOVERY_FALLBACK_NORM_KM_S",
        cfg.fallback_norm_km_s,
        0.0,
        SAT_MAX_DELTAV_KM_S
    );
    cfg.max_request_ratio = env_double(
        "PROJECTBONK_RECOVERY_MAX_REQUEST_RATIO",
        cfg.max_request_ratio,
        0.01,
        1.0
    );
    return cfg;
}

Vec3 compute_slot_target_recovery_dv(const StateStore& store,
                                     std::size_t sat_idx,
                                     const RecoveryRequest& req,
                                     std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                                     const RecoveryPlannerConfig& cfg) noexcept
{
    const OrbitalElements slot = derive_slot_elements_if_needed(store, sat_idx, slot_reference_by_sat);
    OrbitalElements cur{};
    if (!get_current_elements(store, sat_idx, cur)) {
        return req.remaining_delta_v_km_s;
    }

    const Vec3 r{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
    const Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};

    const double vx = v.x;
    const double vy = v.y;
    const double vz = v.z;
    const double v_norm = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (v_norm < EPS_NUM) {
        return req.remaining_delta_v_km_s;
    }

    const double rx = r.x;
    const double ry = r.y;
    const double rz = r.z;
    const double r_norm = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (r_norm < EPS_NUM) {
        return req.remaining_delta_v_km_s;
    }

    const Vec3 t_hat{vx / v_norm, vy / v_norm, vz / v_norm};
    const Vec3 r_hat{rx / r_norm, ry / r_norm, rz / r_norm};

    const double hx = ry * vz - rz * vy;
    const double hy = rz * vx - rx * vz;
    const double hz = rx * vy - ry * vx;
    const double h_norm = std::sqrt(hx * hx + hy * hy + hz * hz);
    if (h_norm < EPS_NUM) {
        return req.remaining_delta_v_km_s;
    }

    const Vec3 n_hat{hx / h_norm, hy / h_norm, hz / h_norm};

    const double da = slot.a_km - cur.a_km;
    const double de = slot.e - cur.e;
    const double di = slot.i_rad - cur.i_rad;
    const double d_raan = wrap_0_2pi(slot.raan_rad - cur.raan_rad + PI) - PI;

    const double dv_t = (da * cfg.scale_t) + (de * cfg.scale_r);
    const double dv_r = de * (cfg.radial_share * cfg.scale_r);
    const double dv_n = (di + d_raan) * cfg.scale_n;

    Vec3 slot_dv{
        t_hat.x * dv_t + r_hat.x * dv_r + n_hat.x * dv_n,
        t_hat.y * dv_t + r_hat.y * dv_r + n_hat.y * dv_n,
        t_hat.z * dv_t + r_hat.z * dv_r + n_hat.z * dv_n
    };

    const double slot_norm = dv_norm_km_s(slot_dv);
    const double rem_norm = dv_norm_km_s(req.remaining_delta_v_km_s);
    if (slot_norm < cfg.fallback_norm_km_s && rem_norm > EPS_NUM) {
        slot_dv = req.remaining_delta_v_km_s;
    }

    const double cmd_norm = dv_norm_km_s(slot_dv);
    const double max_allowed_norm = rem_norm * cfg.max_request_ratio;
    if (cmd_norm > max_allowed_norm + EPS_NUM && max_allowed_norm > EPS_NUM) {
        const double scale = max_allowed_norm / cmd_norm;
        slot_dv.x *= scale;
        slot_dv.y *= scale;
        slot_dv.z *= scale;
    }

    return slot_dv;
}

RecoveryPlanStats plan_recovery_burns(StateStore& store,
                                      double current_epoch_s,
                                      std::uint64_t tick_id,
                                      std::vector<ScheduledBurn>& burn_queue,
                                      std::unordered_map<std::string, double>& last_burn_epoch_by_sat,
                                      std::unordered_map<std::string, RecoveryRequest>& recovery_requests_by_sat,
                                      std::unordered_map<std::string, bool>& graveyard_requested_by_sat,
                                      std::unordered_map<std::string, SlotReference>& slot_reference_by_sat,
                                      double auto_upload_horizon_s,
                                      const RecoveryPlannerConfig& cfg) noexcept
{
    RecoveryPlanStats stats{};
    if (recovery_requests_by_sat.empty()) {
        return stats;
    }

    std::vector<std::string> ready;
    ready.reserve(recovery_requests_by_sat.size());
    for (const auto& kv : recovery_requests_by_sat) {
        if (kv.second.earliest_epoch_s <= current_epoch_s + EPS_NUM) {
            ready.push_back(kv.first);
        }
    }

    for (const std::string& sat_id : ready) {
        auto it = recovery_requests_by_sat.find(sat_id);
        if (it == recovery_requests_by_sat.end()) continue;

        const std::size_t idx = store.find(sat_id);
        if (idx >= store.size() || store.type(idx) != ObjectType::SATELLITE) {
            recovery_requests_by_sat.erase(it);
            continue;
        }

        if (graveyard_requested_by_sat[sat_id]) {
            ++stats.deferred;
            continue;
        }

        if (has_any_pending_burn(burn_queue, sat_id)) {
            ++stats.deferred;
            continue;
        }

        const auto last_it = last_burn_epoch_by_sat.find(sat_id);
        bool cooldown_ok = true;
        if (last_it != last_burn_epoch_by_sat.end()) {
            const double dt = current_epoch_s - last_it->second;
            if (dt + EPS_NUM < SAT_COOLDOWN_S) {
                cooldown_ok = false;
            }
        }
        if (cooldown_ok && has_pending_burn_in_cooldown_window(burn_queue, sat_id, current_epoch_s)) {
            cooldown_ok = false;
        }
        if (!cooldown_ok) {
            ++stats.deferred;
            continue;
        }

        const Vec3 dv = compute_slot_target_recovery_dv(store, idx, it->second, slot_reference_by_sat, cfg);
        const double dv_norm = dv_norm_km_s(dv);
        if (dv_norm <= EPS_NUM) {
            recovery_requests_by_sat.erase(it);
            continue;
        }

        const double dv_cap = SAT_MAX_DELTAV_KM_S;
        const double scale = (dv_norm > dv_cap) ? (dv_cap / dv_norm) : 1.0;
        const Vec3 dv_cmd{dv.x * scale, dv.y * scale, dv.z * scale};
        const double cmd_norm = dv_norm_km_s(dv_cmd);

        const double mass_before = store.mass_kg(idx);
        const double fuel_before = store.fuel_kg(idx);
        const double fuel_need = propellant_used_kg(mass_before, cmd_norm);
        if (fuel_before - fuel_need <= SAT_FUEL_EOL_KG + EPS_NUM) {
            store.set_sat_status(idx, SatStatus::FUEL_LOW);
            graveyard_requested_by_sat[sat_id] = true;
            ++stats.deferred;
            continue;
        }

        const double earliest_burn = std::max(
            current_epoch_s,
            std::max(current_epoch_s + SAT_COOLDOWN_S, it->second.earliest_epoch_s)
        );
        double burn_epoch = 0.0;
        double upload_epoch = 0.0;
        std::string upload_station;
        if (!choose_burn_epoch_with_upload(
                store,
                burn_queue,
                last_burn_epoch_by_sat,
                idx,
                current_epoch_s,
                earliest_burn,
                earliest_burn + auto_upload_horizon_s,
                burn_epoch,
                upload_epoch,
                upload_station)) {
            ++stats.deferred;
            continue;
        }

        ScheduledBurn burn;
        burn.id = "AUTO-RECOVERY-" + std::to_string(tick_id) + "-" + std::to_string(stats.planned);
        burn.satellite_id = sat_id;
        burn.upload_station_id = upload_station;
        burn.upload_epoch_s = upload_epoch;
        burn.burn_epoch_s = burn_epoch;
        burn.delta_v_km_s = dv_cmd;
        burn.delta_v_norm_km_s = cmd_norm;
        burn.auto_generated = true;
        burn.recovery_burn = true;
        burn.graveyard_burn = false;
        burn_queue.push_back(std::move(burn));

        it->second.remaining_delta_v_km_s.x -= dv_cmd.x;
        it->second.remaining_delta_v_km_s.y -= dv_cmd.y;
        it->second.remaining_delta_v_km_s.z -= dv_cmd.z;
        it->second.earliest_epoch_s = burn_epoch + SAT_COOLDOWN_S;

        const double rem = dv_norm_km_s(it->second.remaining_delta_v_km_s);
        if (rem <= 1e-6) {
            recovery_requests_by_sat.erase(it);
        }

        ++stats.planned;
    }

    return stats;
}

} // namespace cascade
