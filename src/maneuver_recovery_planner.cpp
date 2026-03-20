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

// CW solver tunable bounds — env-overridable once on first call.
struct CwSolverParams {
    double horizon_fraction;   // fraction of orbit period for CW horizon
    double horizon_min_s;      // minimum CW horizon [s]
    double horizon_max_s;      // maximum CW horizon [s]
    double pos_blend;          // position correction weight in blend
    double vel_blend;          // velocity error weight in blend
    double rem_error_cap;      // cap on correction as ratio of remaining dv
    double heur_norm_cap;      // cap on correction as ratio of heuristic norm
};

static const CwSolverParams& cw_solver_params() noexcept
{
    static const CwSolverParams p{
        env_double("PROJECTBONK_CW_HORIZON_FRACTION", 0.25, 0.01, 1.0),
        env_double("PROJECTBONK_CW_HORIZON_MIN_S", 300.0, 10.0, 3600.0),
        env_double("PROJECTBONK_CW_HORIZON_MAX_S", 5400.0, 300.0, 86400.0),
        env_double("PROJECTBONK_CW_POS_BLEND", 0.7, 0.0, 1.0),
        env_double("PROJECTBONK_CW_VEL_BLEND", 0.3, 0.0, 1.0),
        env_double("PROJECTBONK_CW_REM_ERROR_CAP", 0.15, 0.0, 1.0),
        env_double("PROJECTBONK_CW_HEUR_NORM_CAP", 0.4, 0.0, 2.0),
    };
    return p;
}

// Compute RTN frame from ECI position and velocity.
// r_hat = r / |r|
// n_hat = (r x v) / |r x v|   (orbit-normal)
// t_hat = n_hat x r_hat        (true tangential, NOT velocity direction)
struct RTNFrame {
    Vec3 r_hat{};
    Vec3 t_hat{};
    Vec3 n_hat{};
    bool valid = false;
};

RTNFrame compute_rtn_frame(const Vec3& r, const Vec3& v) noexcept
{
    RTNFrame frame;
    const double r_norm = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (r_norm < EPS_NUM) return frame;

    frame.r_hat = Vec3{r.x / r_norm, r.y / r_norm, r.z / r_norm};

    // h = r x v  (angular momentum)
    const double hx = r.y * v.z - r.z * v.y;
    const double hy = r.z * v.x - r.x * v.z;
    const double hz = r.x * v.y - r.y * v.x;
    const double h_norm = std::sqrt(hx * hx + hy * hy + hz * hz);
    if (h_norm < EPS_NUM) return frame;

    frame.n_hat = Vec3{hx / h_norm, hy / h_norm, hz / h_norm};

    // t_hat = n_hat x r_hat  (true tangential in orbit plane)
    frame.t_hat = Vec3{
        frame.n_hat.y * frame.r_hat.z - frame.n_hat.z * frame.r_hat.y,
        frame.n_hat.z * frame.r_hat.x - frame.n_hat.x * frame.r_hat.z,
        frame.n_hat.x * frame.r_hat.y - frame.n_hat.y * frame.r_hat.x
    };

    frame.valid = true;
    return frame;
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

    // P0.3: Use true tangential t_hat = n_hat x r_hat, not v/|v|
    const RTNFrame frame = compute_rtn_frame(r, v);
    if (!frame.valid) {
        return req.remaining_delta_v_km_s;
    }

    const double da = slot.a_km - cur.a_km;
    const double de = slot.e - cur.e;
    const double di = slot.i_rad - cur.i_rad;
    const double d_raan = wrap_0_2pi(slot.raan_rad - cur.raan_rad + PI) - PI;

    const double dv_t = (da * cfg.scale_t) + (de * cfg.scale_r);
    const double dv_r = de * (cfg.radial_share * cfg.scale_r);
    const double dv_n = (di + d_raan) * cfg.scale_n;

    return Vec3{
        frame.t_hat.x * dv_t + frame.r_hat.x * dv_r + frame.n_hat.x * dv_n,
        frame.t_hat.y * dv_t + frame.r_hat.y * dv_r + frame.n_hat.y * dv_n,
        frame.t_hat.z * dv_t + frame.r_hat.z * dv_r + frame.n_hat.z * dv_n
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

    // P0.3: Use true RTN frame (t_hat = n_hat x r_hat, not v/|v|)
    const RTNFrame frame = compute_rtn_frame(r_cur, v_cur);
    if (!frame.valid) {
        return req.remaining_delta_v_km_s;
    }

    Vec3 r_slot{};
    Vec3 v_slot{};
    if (!elements_to_eci(slot, r_slot, v_slot)) {
        return req.remaining_delta_v_km_s;
    }

    // Position and velocity errors in ECI
    const Vec3 dr{r_slot.x - r_cur.x, r_slot.y - r_cur.y, r_slot.z - r_cur.z};
    const Vec3 dv{v_slot.x - v_cur.x, v_slot.y - v_cur.y, v_slot.z - v_cur.z};

    // Project errors into RTN frame
    const double dr_r = dr.x * frame.r_hat.x + dr.y * frame.r_hat.y + dr.z * frame.r_hat.z;
    const double dr_t = dr.x * frame.t_hat.x + dr.y * frame.t_hat.y + dr.z * frame.t_hat.z;
    const double dr_n = dr.x * frame.n_hat.x + dr.y * frame.n_hat.y + dr.z * frame.n_hat.z;

    const double dv_r_err = dv.x * frame.r_hat.x + dv.y * frame.r_hat.y + dv.z * frame.r_hat.z;
    const double dv_t_err = dv.x * frame.t_hat.x + dv.y * frame.t_hat.y + dv.z * frame.t_hat.z;
    const double dv_n_err = dv.x * frame.n_hat.x + dv.y * frame.n_hat.y + dv.z * frame.n_hat.z;

    // P0.2: Use semi-major axis a for mean motion, not instantaneous r
    const double a_km = cur.a_km;
    if (a_km < EPS_NUM) {
        return req.remaining_delta_v_km_s;
    }
    const double mean_motion = std::sqrt(MU_KM3_S2 / (a_km * a_km * a_km));
    const double orbit_period_s = TWO_PI / std::max(mean_motion, EPS_NUM);
    const auto& cwp = cw_solver_params();
    const double horizon_s = std::clamp(orbit_period_s * cwp.horizon_fraction, cwp.horizon_min_s, cwp.horizon_max_s);

    // Heuristic RTN correction keeps gain calibration behavior and provides a
    // stable baseline when linearized CW terms are ill-conditioned.
    const double da = slot.a_km - cur.a_km;
    const double de = slot.e - cur.e;
    const double di = slot.i_rad - cur.i_rad;
    const double d_raan = wrap_0_2pi(slot.raan_rad - cur.raan_rad + PI) - PI;
    const double heur_t = (da * cfg.scale_t) + (de * cfg.scale_r);
    const double heur_r = de * (cfg.radial_share * cfg.scale_r);
    const double heur_n = (di + d_raan) * cfg.scale_n;

    // ---------- P0.1: True CW state-transition matrix inverse ----------
    // CW (Hill/Clohessy-Wiltshire) velocity-to-position submatrix Phi_rv:
    //
    //   Phi_rv = [ sin(nt)/n,         2(1-cos(nt))/n,   0          ]
    //            [ -2(1-cos(nt))/n,    (4sin(nt)-3nt)/n, 0          ]
    //            [ 0,                  0,                 sin(nt)/n  ]
    //
    // We need dv0 = Phi_rv^{-1} * dr_target to correct position error.
    // The in-plane 2x2 block has determinant:
    //   det = [sin(nt)*(4sin(nt)-3nt) + 4*(1-cos(nt))^2] / n^2
    //
    // Out-of-plane is decoupled: dv_n = dr_n * n / sin(nt)

    const double n = mean_motion;
    const double tau = horizon_s;
    const double nt = n * tau;

    const double sin_nt = std::sin(nt);
    const double cos_nt = std::cos(nt);
    const double one_minus_cos = 1.0 - cos_nt;

    // Phi_rv elements (divided by 1/n factor)
    const double phi11 = sin_nt / n;                     // R->R
    const double phi12 = 2.0 * one_minus_cos / n;        // T->R
    const double phi21 = -2.0 * one_minus_cos / n;       // R->T
    const double phi22 = (4.0 * sin_nt - 3.0 * nt) / n;  // T->T
    const double phi33 = sin_nt / n;                     // N->N

    // In-plane 2x2 determinant
    const double det_ip = phi11 * phi22 - phi12 * phi21;

    // Guard against near-singular CW matrix (near nt = k*2*pi where
    // in-plane transfer is degenerate). Fall back to heuristic if singular.
    double cw_r = 0.0;
    double cw_t = 0.0;
    double cw_n = 0.0;

    if (std::fabs(det_ip) > EPS_NUM && std::fabs(sin_nt) > EPS_NUM) {
        // Invert in-plane 2x2: inv = [phi22, -phi12; -phi21, phi11] / det
        const double inv_det = 1.0 / det_ip;
        const double pos_dv_r = inv_det * ( phi22 * dr_r - phi12 * dr_t);
        const double pos_dv_t = inv_det * (-phi21 * dr_r + phi11 * dr_t);
        const double pos_dv_n = dr_n * n / sin_nt;

        // Blend: 70% position correction + 30% velocity error correction
        // Position correction drives the satellite toward the slot;
        // velocity error correction damps residual drift.
        cw_r = cwp.pos_blend * pos_dv_r + cwp.vel_blend * dv_r_err;
        cw_t = cwp.pos_blend * pos_dv_t + cwp.vel_blend * dv_t_err;
        cw_n = cwp.pos_blend * pos_dv_n + cwp.vel_blend * dv_n_err;
    } else {
        // Degenerate case: fall back to velocity-matching only
        cw_r = dv_r_err;
        cw_t = dv_t_err;
        cw_n = dv_n_err;
    }

    Vec3 heuristic_dv{
        frame.t_hat.x * heur_t + frame.r_hat.x * heur_r + frame.n_hat.x * heur_n,
        frame.t_hat.y * heur_t + frame.r_hat.y * heur_r + frame.n_hat.y * heur_n,
        frame.t_hat.z * heur_t + frame.r_hat.z * heur_r + frame.n_hat.z * heur_n
    };

    Vec3 cw_raw_dv{
        frame.t_hat.x * cw_t + frame.r_hat.x * cw_r + frame.n_hat.x * cw_n,
        frame.t_hat.y * cw_t + frame.r_hat.y * cw_r + frame.n_hat.y * cw_n,
        frame.t_hat.z * cw_t + frame.r_hat.z * cw_r + frame.n_hat.z * cw_n
    };

    Vec3 correction{
        cw_raw_dv.x - heuristic_dv.x,
        cw_raw_dv.y - heuristic_dv.y,
        cw_raw_dv.z - heuristic_dv.z
    };

    const double heur_norm = dv_norm_km_s(heuristic_dv);
    const double rem_norm = dv_norm_km_s(req.remaining_delta_v_km_s);
    const double corr_norm = dv_norm_km_s(correction);

    const double correction_cap = std::max(rem_norm * cwp.rem_error_cap, heur_norm * cwp.heur_norm_cap);
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
