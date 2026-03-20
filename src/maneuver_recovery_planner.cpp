// ---------------------------------------------------------------------------
// maneuver_recovery_planner.cpp — slot-targeted recovery planner helper
// ---------------------------------------------------------------------------

#include "maneuver_recovery_planner.hpp"

#include "orbit_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

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

RecoverySolverMode env_solver_mode(RecoverySolverMode default_value) noexcept
{
    const char* raw = std::getenv("PROJECTBONK_RECOVERY_SOLVER_MODE");
    if (raw == nullptr) {
        return default_value;
    }

    const std::string mode(raw);
    if (mode == "cw_zem" || mode == "CW_ZEM" || mode == "cw-zem") {
        return RecoverySolverMode::CW_ZEM_EQUIVALENT;
    }
    if (mode == "heuristic" || mode == "HEURISTIC") {
        return RecoverySolverMode::HEURISTIC;
    }
    return default_value;
}

Vec3 compute_slot_target_recovery_dv_heuristic(const StateStore& store,
                                               std::size_t sat_idx,
                                               const OrbitalElements& slot,
                                               const OrbitalElements& cur,
                                               const RecoveryPlannerConfig& cfg,
                                               const RecoveryRequest& req) noexcept
{
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

    return Vec3{
        t_hat.x * dv_t + r_hat.x * dv_r + n_hat.x * dv_n,
        t_hat.y * dv_t + r_hat.y * dv_r + n_hat.y * dv_n,
        t_hat.z * dv_t + r_hat.z * dv_r + n_hat.z * dv_n
    };
}

Vec3 compute_slot_target_recovery_dv_cw_zem_equivalent(const StateStore& store,
                                                        std::size_t sat_idx,
                                                        const OrbitalElements& slot,
                                                        const OrbitalElements& cur,
                                                        const RecoveryPlannerConfig& cfg,
                                                        const RecoveryRequest& req) noexcept
{
    const Vec3 r_cur{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
    const Vec3 v_cur{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};

    const double r_norm = std::sqrt(r_cur.x * r_cur.x + r_cur.y * r_cur.y + r_cur.z * r_cur.z);
    const double v_norm = std::sqrt(v_cur.x * v_cur.x + v_cur.y * v_cur.y + v_cur.z * v_cur.z);
    if (r_norm < EPS_NUM || v_norm < EPS_NUM) {
        return req.remaining_delta_v_km_s;
    }

    Vec3 r_slot{};
    Vec3 v_slot{};
    if (!elements_to_eci(slot, r_slot, v_slot)) {
        return req.remaining_delta_v_km_s;
    }

    const Vec3 r_hat{r_cur.x / r_norm, r_cur.y / r_norm, r_cur.z / r_norm};
    const Vec3 t_hat{v_cur.x / v_norm, v_cur.y / v_norm, v_cur.z / v_norm};
    const Vec3 h_vec{
        r_cur.y * v_cur.z - r_cur.z * v_cur.y,
        r_cur.z * v_cur.x - r_cur.x * v_cur.z,
        r_cur.x * v_cur.y - r_cur.y * v_cur.x
    };
    const double h_norm = std::sqrt(h_vec.x * h_vec.x + h_vec.y * h_vec.y + h_vec.z * h_vec.z);
    if (h_norm < EPS_NUM) {
        return req.remaining_delta_v_km_s;
    }
    const Vec3 n_hat{h_vec.x / h_norm, h_vec.y / h_norm, h_vec.z / h_norm};

    const Vec3 dr{r_slot.x - r_cur.x, r_slot.y - r_cur.y, r_slot.z - r_cur.z};
    const Vec3 dv{v_slot.x - v_cur.x, v_slot.y - v_cur.y, v_slot.z - v_cur.z};

    const double dr_r = dr.x * r_hat.x + dr.y * r_hat.y + dr.z * r_hat.z;
    const double dr_t = dr.x * t_hat.x + dr.y * t_hat.y + dr.z * t_hat.z;
    const double dr_n = dr.x * n_hat.x + dr.y * n_hat.y + dr.z * n_hat.z;

    const double dv_r_err = dv.x * r_hat.x + dv.y * r_hat.y + dv.z * r_hat.z;
    const double dv_t_err = dv.x * t_hat.x + dv.y * t_hat.y + dv.z * t_hat.z;
    const double dv_n_err = dv.x * n_hat.x + dv.y * n_hat.y + dv.z * n_hat.z;

    const double mean_motion = std::sqrt(MU_KM3_S2 / (r_norm * r_norm * r_norm));
    const double orbit_period_s = TWO_PI / std::max(mean_motion, EPS_NUM);
    const double horizon_s = std::clamp(orbit_period_s * 0.25, 300.0, 5400.0);

    // Heuristic RTN correction keeps gain calibration behavior and provides a
    // stable baseline when linearized CW/ZEM terms are noisy.
    const double da = slot.a_km - cur.a_km;
    const double de = slot.e - cur.e;
    const double di = slot.i_rad - cur.i_rad;
    const double d_raan = wrap_0_2pi(slot.raan_rad - cur.raan_rad + PI) - PI;
    const double heur_t = (da * cfg.scale_t) + (de * cfg.scale_r);
    const double heur_r = de * (cfg.radial_share * cfg.scale_r);
    const double heur_n = (di + d_raan) * cfg.scale_n;

    // CW/ZEM-equivalent single-burn approximation in RTN.
    const double cw_r = (2.0 / horizon_s) * dr_r + 0.5 * dv_r_err;
    const double cw_t = (2.0 / horizon_s) * dr_t + 0.5 * dv_t_err + 0.5 * mean_motion * dr_r;
    const double cw_n = (2.0 / horizon_s) * dr_n + 0.5 * dv_n_err;

    Vec3 heuristic_dv{
        t_hat.x * heur_t + r_hat.x * heur_r + n_hat.x * heur_n,
        t_hat.y * heur_t + r_hat.y * heur_r + n_hat.y * heur_n,
        t_hat.z * heur_t + r_hat.z * heur_r + n_hat.z * heur_n
    };

    Vec3 cw_raw_dv{
        t_hat.x * cw_t + r_hat.x * cw_r + n_hat.x * cw_n,
        t_hat.y * cw_t + r_hat.y * cw_r + n_hat.y * cw_n,
        t_hat.z * cw_t + r_hat.z * cw_r + n_hat.z * cw_n
    };

    Vec3 correction{
        cw_raw_dv.x - heuristic_dv.x,
        cw_raw_dv.y - heuristic_dv.y,
        cw_raw_dv.z - heuristic_dv.z
    };

    const double heur_norm = dv_norm_km_s(heuristic_dv);
    const double rem_norm = dv_norm_km_s(req.remaining_delta_v_km_s);
    const double corr_norm = dv_norm_km_s(correction);

    const double correction_cap = std::max(rem_norm * 0.15, heur_norm * 0.4);
    if (corr_norm > correction_cap + EPS_NUM && corr_norm > EPS_NUM) {
        const double scale = correction_cap / corr_norm;
        correction.x *= scale;
        correction.y *= scale;
        correction.z *= scale;
    }

    return Vec3{
        heuristic_dv.x + correction.x,
        heuristic_dv.y + correction.y,
        heuristic_dv.z + correction.z
    };
}

Vec3 apply_recovery_dv_guards(Vec3 slot_dv,
                              const RecoveryRequest& req,
                              const RecoveryPlannerConfig& cfg) noexcept
{
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
    cfg.solver_mode = env_solver_mode(cfg.solver_mode);
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

    Vec3 slot_dv{};
    if (cfg.solver_mode == RecoverySolverMode::CW_ZEM_EQUIVALENT) {
        slot_dv = compute_slot_target_recovery_dv_cw_zem_equivalent(
            store,
            sat_idx,
            slot,
            cur,
            cfg,
            req
        );
    } else {
        slot_dv = compute_slot_target_recovery_dv_heuristic(
            store,
            sat_idx,
            slot,
            cur,
            cfg,
            req
        );
    }

    return apply_recovery_dv_guards(slot_dv, req, cfg);
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
