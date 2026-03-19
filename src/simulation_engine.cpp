// ---------------------------------------------------------------------------
// simulation_engine.cpp
// ---------------------------------------------------------------------------
#include "simulation_engine.hpp"

#include "broad_phase.hpp"
#include "orbit_math.hpp"
#include "propagator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace cascade {

namespace {

struct PlanePhaseGateResult {
    bool evaluated = false;
    bool reject = false;
    bool fail_open = false;
};

struct MoidProxyGateResult {
    bool evaluated = false;
    bool reject = false;
    bool fail_open = false;
};

inline double norm2(double x, double y, double z) noexcept
{
    return x * x + y * y + z * z;
}

inline PlanePhaseGateResult evaluate_plane_phase_gate(const StateStore& store,
                                                      std::size_t sat_idx,
                                                      std::size_t obj_idx,
                                                      const NarrowPhaseConfig& cfg) noexcept
{
    PlanePhaseGateResult out{};
    out.evaluated = true;

    const bool sat_valid = store.elements_valid(sat_idx);
    const bool obj_valid = store.elements_valid(obj_idx);
    if (!sat_valid || !obj_valid) {
        out.fail_open = true;
        return out;
    }

    const double sat_e = store.e(sat_idx);
    const double obj_e = store.e(obj_idx);
    if (!std::isfinite(sat_e) || !std::isfinite(obj_e)) {
        out.fail_open = true;
        return out;
    }
    if (sat_e > cfg.phase_max_e || obj_e > cfg.phase_max_e) {
        out.fail_open = true;
        return out;
    }

    const double sat_i = store.i_rad(sat_idx);
    const double sat_raan = store.raan_rad(sat_idx);
    const double obj_i = store.i_rad(obj_idx);
    const double obj_raan = store.raan_rad(obj_idx);
    const double sat_argp = store.argp_rad(sat_idx);
    const double sat_M = store.M_rad(sat_idx);
    const double obj_argp = store.argp_rad(obj_idx);
    const double obj_M = store.M_rad(obj_idx);

    if (!std::isfinite(sat_i)
        || !std::isfinite(sat_raan)
        || !std::isfinite(obj_i)
        || !std::isfinite(obj_raan)
        || !std::isfinite(sat_argp)
        || !std::isfinite(sat_M)
        || !std::isfinite(obj_argp)
        || !std::isfinite(obj_M)) {
        out.fail_open = true;
        return out;
    }

    const Vec3 sat_h{
        std::sin(sat_i) * std::sin(sat_raan),
        -std::sin(sat_i) * std::cos(sat_raan),
        std::cos(sat_i)
    };
    const Vec3 obj_h{
        std::sin(obj_i) * std::sin(obj_raan),
        -std::sin(obj_i) * std::cos(obj_raan),
        std::cos(obj_i)
    };

    const double sat_h_norm = std::sqrt(norm2(sat_h.x, sat_h.y, sat_h.z));
    const double obj_h_norm = std::sqrt(norm2(obj_h.x, obj_h.y, obj_h.z));
    if (!(sat_h_norm > EPS_NUM) || !(obj_h_norm > EPS_NUM)) {
        out.fail_open = true;
        return out;
    }

    const double cos_plane = std::clamp(
        (sat_h.x * obj_h.x + sat_h.y * obj_h.y + sat_h.z * obj_h.z)
            / (sat_h_norm * obj_h_norm),
        -1.0,
        1.0
    );
    const double plane_angle = std::acos(cos_plane);
    if (!std::isfinite(plane_angle)) {
        out.fail_open = true;
        return out;
    }

    if (plane_angle > cfg.plane_angle_threshold_rad) {
        out.reject = true;
        return out;
    }

    const double sat_phase = wrap_0_2pi(sat_argp + sat_M);
    const double obj_phase = wrap_0_2pi(obj_argp + obj_M);
    const double phase_delta =
        std::abs(wrap_0_2pi(sat_phase - obj_phase + PI) - PI);
    if (!std::isfinite(phase_delta)) {
        out.fail_open = true;
        return out;
    }

    if (phase_delta > cfg.phase_angle_threshold_rad) {
        out.reject = true;
    }
    return out;
}

inline MoidProxyGateResult evaluate_moid_proxy_gate(const StateStore& store,
                                                    std::size_t sat_idx,
                                                    std::size_t obj_idx,
                                                    double epoch_s,
                                                    const NarrowPhaseConfig& cfg) noexcept
{
    MoidProxyGateResult out{};
    out.evaluated = true;

    const bool sat_valid = store.elements_valid(sat_idx);
    const bool obj_valid = store.elements_valid(obj_idx);
    if (!sat_valid || !obj_valid) {
        out.fail_open = true;
        return out;
    }

    OrbitalElements sat_el{};
    sat_el.a_km = store.a_km(sat_idx);
    sat_el.e = store.e(sat_idx);
    sat_el.i_rad = store.i_rad(sat_idx);
    sat_el.raan_rad = store.raan_rad(sat_idx);
    sat_el.argp_rad = store.argp_rad(sat_idx);
    sat_el.M_rad = store.M_rad(sat_idx);
    sat_el.n_rad_s = store.n_rad_s(sat_idx);
    sat_el.p_km = store.p_km(sat_idx);
    sat_el.rp_km = store.rp_km(sat_idx);
    sat_el.ra_km = store.ra_km(sat_idx);

    OrbitalElements obj_el{};
    obj_el.a_km = store.a_km(obj_idx);
    obj_el.e = store.e(obj_idx);
    obj_el.i_rad = store.i_rad(obj_idx);
    obj_el.raan_rad = store.raan_rad(obj_idx);
    obj_el.argp_rad = store.argp_rad(obj_idx);
    obj_el.M_rad = store.M_rad(obj_idx);
    obj_el.n_rad_s = store.n_rad_s(obj_idx);
    obj_el.p_km = store.p_km(obj_idx);
    obj_el.rp_km = store.rp_km(obj_idx);
    obj_el.ra_km = store.ra_km(obj_idx);

    if (!std::isfinite(sat_el.a_km)
        || !std::isfinite(sat_el.e)
        || !std::isfinite(obj_el.a_km)
        || !std::isfinite(obj_el.e)
        || sat_el.e > cfg.moid_max_e
        || obj_el.e > cfg.moid_max_e) {
        out.fail_open = true;
        return out;
    }

    const double sat_epoch = store.telemetry_epoch_s(sat_idx);
    const double obj_epoch = store.telemetry_epoch_s(obj_idx);
    const double sat_dt = std::max(0.0, epoch_s - sat_epoch);
    const double obj_dt = std::max(0.0, epoch_s - obj_epoch);
    if (!std::isfinite(sat_dt) || !std::isfinite(obj_dt)) {
        out.fail_open = true;
        return out;
    }

    apply_j2_secular(sat_el, sat_dt);
    apply_j2_secular(obj_el, obj_dt);

    const std::uint32_t samples = std::max<std::uint32_t>(cfg.moid_samples, 6U);
    const double two_pi = TWO_PI;
    const double step = two_pi / static_cast<double>(samples);

    double min_d2 = std::numeric_limits<double>::infinity();
    bool any_valid = false;

    for (std::uint32_t s = 0; s < samples; ++s) {
        const double sat_u = step * static_cast<double>(s);
        OrbitalElements sat_sample = sat_el;
        sat_sample.M_rad = sat_u;

        Vec3 sat_r{};
        Vec3 sat_v{};
        if (!elements_to_eci(sat_sample, sat_r, sat_v)) {
            continue;
        }

        for (std::uint32_t d = 0; d < samples; ++d) {
            const double obj_u = step * static_cast<double>(d);
            OrbitalElements obj_sample = obj_el;
            obj_sample.M_rad = obj_u;

            Vec3 obj_r{};
            Vec3 obj_v{};
            if (!elements_to_eci(obj_sample, obj_r, obj_v)) {
                continue;
            }

            any_valid = true;
            const double dx = sat_r.x - obj_r.x;
            const double dy = sat_r.y - obj_r.y;
            const double dz = sat_r.z - obj_r.z;
            const double d2 = norm2(dx, dy, dz);
            if (d2 < min_d2) {
                min_d2 = d2;
            }
        }
    }

    if (!any_valid || !std::isfinite(min_d2)) {
        out.fail_open = true;
        return out;
    }

    const double threshold_km = std::max(0.0, cfg.moid_reject_threshold_km);
    out.reject = std::sqrt(min_d2) > threshold_km;
    return out;
}

inline double min_d2_linear_segment(double rx,
                                    double ry,
                                    double rz,
                                    double vx,
                                    double vy,
                                    double vz,
                                    double t_lo,
                                    double t_hi) noexcept
{
    const double vv = norm2(vx, vy, vz);
    if (vv <= EPS_NUM) {
        return norm2(rx, ry, rz);
    }
    double t = -(rx * vx + ry * vy + rz * vz) / vv;
    if (t < t_lo) t = t_lo;
    if (t > t_hi) t = t_hi;
    const double px = rx + vx * t;
    const double py = ry + vy * t;
    const double pz = rz + vz * t;
    return norm2(px, py, pz);
}

inline double refine_pair_min_d2_rk4(const Vec3& sat_r0,
                                     const Vec3& sat_v0,
                                     const Vec3& deb_r0,
                                     const Vec3& deb_v0,
                                     double step_seconds,
                                     double max_step_s,
                                     bool& ok) noexcept
{
    ok = true;
    Vec3 rs = sat_r0;
    Vec3 vs = sat_v0;
    Vec3 rd = deb_r0;
    Vec3 vd = deb_v0;

    if (!propagate_rk4_j2_substep(rs, vs, step_seconds, max_step_s)) {
        ok = false;
        return 0.0;
    }
    if (!propagate_rk4_j2_substep(rd, vd, step_seconds, max_step_s)) {
        ok = false;
        return 0.0;
    }

    const double r0x = sat_r0.x - deb_r0.x;
    const double r0y = sat_r0.y - deb_r0.y;
    const double r0z = sat_r0.z - deb_r0.z;
    const double r1x = rs.x - rd.x;
    const double r1y = rs.y - rd.y;
    const double r1z = rs.z - rd.z;

    const double v0x = sat_v0.x - deb_v0.x;
    const double v0y = sat_v0.y - deb_v0.y;
    const double v0z = sat_v0.z - deb_v0.z;
    const double v1x = vs.x - vd.x;
    const double v1y = vs.y - vd.y;
    const double v1z = vs.z - vd.z;

    double min_d2 = std::min(norm2(r0x, r0y, r0z), norm2(r1x, r1y, r1z));
    min_d2 = std::min(min_d2,
                      min_d2_linear_segment(r0x, r0y, r0z,
                                            v0x, v0y, v0z,
                                            0.0, step_seconds));
    min_d2 = std::min(min_d2,
                      min_d2_linear_segment(r1x, r1y, r1z,
                                            -v1x, -v1y, -v1z,
                                            0.0, step_seconds));
    if (step_seconds > EPS_NUM) {
        const double vsecx = (r1x - r0x) / step_seconds;
        const double vsecy = (r1y - r0y) / step_seconds;
        const double vsecz = (r1z - r0z) / step_seconds;
        min_d2 = std::min(min_d2,
                          min_d2_linear_segment(r0x, r0y, r0z,
                                                vsecx, vsecy, vsecz,
                                                0.0, step_seconds));
    }
    return min_d2;
}

inline double relative_speed_km_s(const Vec3& sat_v,
                                  const Vec3& deb_v) noexcept
{
    const double dvx = sat_v.x - deb_v.x;
    const double dvy = sat_v.y - deb_v.y;
    const double dvz = sat_v.z - deb_v.z;
    return std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz);
}

} // namespace

bool run_simulation_step(StateStore& store,
                         SimClock& clock,
                         double step_seconds,
                         StepRunStats& out,
                         const StepRunConfig& cfg) noexcept
{
    out = StepRunStats{};
    if (!clock.is_initialized() || !(step_seconds > 0.0)) {
        return false;
    }

    const double target_epoch = clock.epoch_s() + step_seconds;
    out.target_epoch_s = target_epoch;

    const std::size_t n = store.size();
    std::vector<double> rx0(n), ry0(n), rz0(n), vx0(n), vy0(n), vz0(n);
    for (std::size_t i = 0; i < n; ++i) {
        rx0[i] = store.rx(i);
        ry0[i] = store.ry(i);
        rz0[i] = store.rz(i);
        vx0[i] = store.vx(i);
        vy0[i] = store.vy(i);
        vz0[i] = store.vz(i);
    }

    for (std::size_t i = 0; i < store.size(); ++i) {
        Vec3 r{store.rx(i), store.ry(i), store.rz(i)};
        Vec3 v{store.vx(i), store.vy(i), store.vz(i)};

        OrbitalElements el{};
        bool el_ok = false;
        if (store.elements_valid(i)) {
            el.a_km = store.a_km(i);
            el.e = store.e(i);
            el.i_rad = store.i_rad(i);
            el.raan_rad = store.raan_rad(i);
            el.argp_rad = store.argp_rad(i);
            el.M_rad = store.M_rad(i);
            el.n_rad_s = store.n_rad_s(i);
            el.p_km = store.p_km(i);
            el.rp_km = store.rp_km(i);
            el.ra_km = store.ra_km(i);
            el_ok = true;
        } else {
            el_ok = eci_to_elements(r, v, el);
        }

        if (!el_ok) {
            ++out.failed_objects;
            continue;
        }

        const double obj_epoch = store.telemetry_epoch_s(i);
        double obj_dt = target_epoch - obj_epoch;
        if (obj_dt < 0.0) obj_dt = 0.0;

        bool ok = true;
        if (obj_dt > 0.0) {
            AdaptivePropagationResult prop = propagate_adaptive(r, v, el, obj_dt);
            ok = prop.ok;
            if (prop.used_rk4) ++out.used_rk4;
            else ++out.used_fast;
            if (prop.escalated_after_probe) ++out.escalated_after_probe;
        }

        if (!ok) {
            ++out.failed_objects;
            continue;
        }

        store.rx_mut(i) = r.x;
        store.ry_mut(i) = r.y;
        store.rz_mut(i) = r.z;
        store.vx_mut(i) = v.x;
        store.vy_mut(i) = v.y;
        store.vz_mut(i) = v.z;
        const double applied_epoch = (obj_epoch > target_epoch) ? obj_epoch : target_epoch;
        store.set_telemetry_epoch_s(i, applied_epoch);
        store.set_elements(i, el, true);
        ++out.propagated_objects;
    }

    // Conservative broad-phase candidate generation at the target epoch.
    const BroadPhaseResult broad = generate_broad_phase_candidates(store, cfg.broad_phase);
    out.broad_pairs_considered = broad.pairs_considered;
    out.broad_candidates = static_cast<std::uint64_t>(broad.candidates.size());
    out.broad_shell_overlap_pass = broad.shell_overlap_pass;
    out.broad_dcriterion_rejected = broad.dcriterion_rejected;
    out.broad_dcriterion_shadow_rejected = broad.dcriterion_shadow_rejected;
    out.broad_fail_open_objects = broad.fail_open_objects;
    out.broad_fail_open_satellites = broad.fail_open_satellites;
    out.broad_shell_margin_km = broad.shell_margin_km;
    out.broad_dcriterion_enabled = cfg.broad_phase.enable_dcriterion;
    out.broad_a_bin_width_km = cfg.broad_phase.a_bin_width_km;
    out.broad_band_neighbor_bins = cfg.broad_phase.band_neighbor_bins;

    std::vector<std::uint8_t> sat_collision_mark(n, 0);

    // Conservative narrow-phase sweep over [t0, t1] using multiple linear
    // TCA approximations derived from step endpoints.
    const double tca_guard_km = std::max(0.0, cfg.narrow_phase.tca_guard_km);
    const double screening_threshold_km = COLLISION_THRESHOLD_KM + tca_guard_km;
    const double collision_threshold_sq = screening_threshold_km * screening_threshold_km;

    const auto tca_min_d2 = [&](std::size_t sat_idx, std::size_t obj_idx) noexcept {
        const double r0x = rx0[sat_idx] - rx0[obj_idx];
        const double r0y = ry0[sat_idx] - ry0[obj_idx];
        const double r0z = rz0[sat_idx] - rz0[obj_idx];
        const double v0x = vx0[sat_idx] - vx0[obj_idx];
        const double v0y = vy0[sat_idx] - vy0[obj_idx];
        const double v0z = vz0[sat_idx] - vz0[obj_idx];

        const double r1x = store.rx(sat_idx) - store.rx(obj_idx);
        const double r1y = store.ry(sat_idx) - store.ry(obj_idx);
        const double r1z = store.rz(sat_idx) - store.rz(obj_idx);
        const double v1x = store.vx(sat_idx) - store.vx(obj_idx);
        const double v1y = store.vy(sat_idx) - store.vy(obj_idx);
        const double v1z = store.vz(sat_idx) - store.vz(obj_idx);

        double min_d2 = std::min(norm2(r0x, r0y, r0z), norm2(r1x, r1y, r1z));

        min_d2 = std::min(min_d2,
                          min_d2_linear_segment(r0x, r0y, r0z,
                                                v0x, v0y, v0z,
                                                0.0, step_seconds));

        min_d2 = std::min(min_d2,
                          min_d2_linear_segment(r1x, r1y, r1z,
                                                -v1x, -v1y, -v1z,
                                                0.0, step_seconds));

        const double vavgx = 0.5 * (v0x + v1x);
        const double vavgy = 0.5 * (v0y + v1y);
        const double vavgz = 0.5 * (v0z + v1z);
        min_d2 = std::min(min_d2,
                          min_d2_linear_segment(r0x, r0y, r0z,
                                                vavgx, vavgy, vavgz,
                                                0.0, step_seconds));

        if (step_seconds > EPS_NUM) {
            const double vsecx = (r1x - r0x) / step_seconds;
            const double vsecy = (r1y - r0y) / step_seconds;
            const double vsecz = (r1z - r0z) / step_seconds;
            min_d2 = std::min(min_d2,
                              min_d2_linear_segment(r0x, r0y, r0z,
                                                    vsecx, vsecy, vsecz,
                                                    0.0, step_seconds));
        }

        return min_d2;
    };

    // If propagation failed for any object this tick, fall back to full
    // SATELLITE-vs-DEBRIS scan to avoid relying on broad-phase filtering.
    const double refine_band_km = std::max(0.0, cfg.narrow_phase.refine_band_km);
    const double refine_band_sq = (screening_threshold_km + refine_band_km)
                                * (screening_threshold_km + refine_band_km);
    const double full_refine_band_km = std::max(0.0, cfg.narrow_phase.full_refine_band_km);
    const double full_refine_band_sq = (screening_threshold_km + full_refine_band_km)
                                     * (screening_threshold_km + full_refine_band_km);
    const bool plane_phase_shadow = cfg.narrow_phase.plane_phase_shadow;
    const bool plane_phase_filter = cfg.narrow_phase.plane_phase_filter;
    const bool moid_shadow = cfg.narrow_phase.moid_shadow;
    const bool moid_filter = cfg.narrow_phase.moid_filter;

    const double high_rel_speed_km_s = std::max(0.0, cfg.narrow_phase.high_rel_speed_km_s);
    const double high_rel_speed_extra_band_km =
        std::max(0.0, cfg.narrow_phase.high_rel_speed_extra_band_km);
    const double high_rel_speed_band_sq =
        (screening_threshold_km + full_refine_band_km + high_rel_speed_extra_band_km)
        * (screening_threshold_km + full_refine_band_km + high_rel_speed_extra_band_km);

    const std::uint64_t pair_hint = out.failed_objects == 0
        ? static_cast<std::uint64_t>(broad.candidates.size())
        : static_cast<std::uint64_t>(store.satellite_count())
            * static_cast<std::uint64_t>(store.debris_count());

    std::uint64_t full_refine_budget =
        std::max<std::uint64_t>(cfg.narrow_phase.full_refine_budget_base, 1);
    if (pair_hint > 2000000ULL) {
        full_refine_budget = 20;
    } else if (pair_hint > 500000ULL) {
        full_refine_budget = 32;
    } else if (pair_hint < 100000ULL) {
        full_refine_budget = 96;
    } else if (pair_hint < 300000ULL) {
        full_refine_budget = 80;
    }

    if (step_seconds > 120.0) {
        full_refine_budget = full_refine_budget / 2;
    } else if (step_seconds <= 5.0) {
        full_refine_budget += 24;
    }

    if (out.failed_objects > 0) {
        full_refine_budget = full_refine_budget / 2;
    }

    const std::uint64_t budget_min =
        std::max<std::uint64_t>(cfg.narrow_phase.full_refine_budget_min, 1);
    const std::uint64_t budget_max =
        std::max<std::uint64_t>(cfg.narrow_phase.full_refine_budget_max, budget_min);
    if (full_refine_budget < budget_min) full_refine_budget = budget_min;
    if (full_refine_budget > budget_max) full_refine_budget = budget_max;

    out.narrow_full_refine_budget_allocated = full_refine_budget;

    const auto full_window_min_d2_rk4 = [&](std::size_t sat_idx,
                                            std::size_t obj_idx,
                                            bool& ok) noexcept {
        const std::uint32_t sample_count =
            std::max<std::uint32_t>(cfg.narrow_phase.full_refine_samples, 1U);
        const double rk4_substep_s =
            std::max(0.1, cfg.narrow_phase.full_refine_substep_s);

        Vec3 rs{rx0[sat_idx], ry0[sat_idx], rz0[sat_idx]};
        Vec3 vs{vx0[sat_idx], vy0[sat_idx], vz0[sat_idx]};
        Vec3 rd{rx0[obj_idx], ry0[obj_idx], rz0[obj_idx]};
        Vec3 vd{vx0[obj_idx], vy0[obj_idx], vz0[obj_idx]};

        ok = true;
        double min_d2 = norm2(rs.x - rd.x, rs.y - rd.y, rs.z - rd.z);
        const double dt = step_seconds / static_cast<double>(sample_count);
        for (std::uint32_t s = 0; s < sample_count; ++s) {
            if (!propagate_rk4_j2_substep(rs, vs, dt, rk4_substep_s)
                || !propagate_rk4_j2_substep(rd, vd, dt, rk4_substep_s)) {
                ok = false;
                return 0.0;
            }
            const double dx = rs.x - rd.x;
            const double dy = rs.y - rd.y;
            const double dz = rs.z - rd.z;
            const double d2 = norm2(dx, dy, dz);
            if (d2 < min_d2) min_d2 = d2;
        }
        return min_d2;
    };

    auto process_pair = [&](std::size_t sat_idx, std::size_t obj_idx) noexcept {
        ++out.narrow_pairs_checked;

        double d2 = tca_min_d2(sat_idx, obj_idx);
        const double rel_speed = relative_speed_km_s(
            Vec3{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)},
            Vec3{store.vx(obj_idx), store.vy(obj_idx), store.vz(obj_idx)}
        );
        const bool uncertainty_promoted =
            (d2 > full_refine_band_sq
             && rel_speed >= high_rel_speed_km_s
             && d2 <= high_rel_speed_band_sq + 1e-9);
        if (uncertainty_promoted) {
            ++out.narrow_uncertainty_promoted_pairs;
        }

        PlanePhaseGateResult plane_phase{};
        const bool near_refine_window =
            ((d2 >= collision_threshold_sq && d2 <= refine_band_sq)
             || (d2 > collision_threshold_sq
                 && (d2 <= full_refine_band_sq + 1e-9 || uncertainty_promoted)));
        if (near_refine_window) {
            plane_phase = evaluate_plane_phase_gate(store, sat_idx, obj_idx, cfg.narrow_phase);
            if (plane_phase.evaluated) {
                ++out.narrow_plane_phase_evaluated_pairs;
            }
            if (plane_phase.fail_open) {
                ++out.narrow_plane_phase_fail_open_pairs;
            }
            if (plane_phase.reject) {
                if (plane_phase_shadow) {
                    ++out.narrow_plane_phase_shadow_rejected_pairs;
                }
                if (plane_phase_filter && !plane_phase.fail_open && !uncertainty_promoted) {
                    ++out.narrow_plane_phase_hard_rejected_pairs;
                    d2 = std::max(d2, std::max(refine_band_sq, full_refine_band_sq) + 1.0);
                } else if (plane_phase_filter && uncertainty_promoted) {
                    ++out.narrow_plane_phase_fail_open_pairs;
                }
            }

            MoidProxyGateResult moid{};
            moid = evaluate_moid_proxy_gate(store, sat_idx, obj_idx, target_epoch, cfg.narrow_phase);
            if (moid.evaluated) {
                ++out.narrow_moid_evaluated_pairs;
            }
            if (moid.fail_open) {
                ++out.narrow_moid_fail_open_pairs;
            }
            if (moid.reject) {
                if (moid_shadow) {
                    ++out.narrow_moid_shadow_rejected_pairs;
                }
                if (moid_filter && !moid.fail_open && !uncertainty_promoted) {
                    ++out.narrow_moid_hard_rejected_pairs;
                    d2 = std::max(d2, std::max(refine_band_sq, full_refine_band_sq) + 1.0);
                } else if (moid_filter && uncertainty_promoted) {
                    ++out.narrow_moid_fail_open_pairs;
                }
            }
        }

        if (d2 >= collision_threshold_sq && d2 <= refine_band_sq) {
            const Vec3 sat_r0{rx0[sat_idx], ry0[sat_idx], rz0[sat_idx]};
            const Vec3 sat_v0{vx0[sat_idx], vy0[sat_idx], vz0[sat_idx]};
            const Vec3 deb_r0{rx0[obj_idx], ry0[obj_idx], rz0[obj_idx]};
            const Vec3 deb_v0{vx0[obj_idx], vy0[obj_idx], vz0[obj_idx]};

            bool refine_ok = true;
            const double d2_refined = refine_pair_min_d2_rk4(
                sat_r0,
                sat_v0,
                deb_r0,
                deb_v0,
                step_seconds,
                std::max(0.1, cfg.narrow_phase.micro_refine_max_step_s),
                refine_ok
            );

            ++out.narrow_refined_pairs;
            if (!refine_ok) {
                ++out.narrow_refine_fail_open;
                d2 = 0.0;
            } else if (d2_refined >= collision_threshold_sq) {
                ++out.narrow_refine_cleared;
                d2 = d2_refined;
            } else {
                d2 = d2_refined;
            }
        }

        if (d2 > collision_threshold_sq
            && (d2 <= full_refine_band_sq + 1e-9 || uncertainty_promoted)) {
            if (full_refine_budget == 0) {
                ++out.narrow_full_refine_budget_exhausted;
                // Fail-open policy: if a pair is near-threshold but budget is
                // exhausted, classify as potential conjunction to avoid
                // false negatives from under-refinement.
                d2 = 0.0;
            } else {
                --full_refine_budget;
                bool full_ok = true;
                const double d2_full = full_window_min_d2_rk4(sat_idx, obj_idx, full_ok);
                ++out.narrow_full_refined_pairs;
                if (!full_ok) {
                    ++out.narrow_full_refine_fail_open;
                    d2 = 0.0;
                } else if (d2_full >= collision_threshold_sq) {
                    ++out.narrow_full_refine_cleared;
                    d2 = d2_full;
                } else {
                    d2 = d2_full;
                }
            }
        }

        if (d2 < collision_threshold_sq) {
            ++out.collisions_detected;
            sat_collision_mark[sat_idx] = 1;
        }
    };

    if (out.failed_objects == 0) {
        for (const BroadPhasePair& pair : broad.candidates) {
            const std::size_t sat_idx = static_cast<std::size_t>(pair.sat_idx);
            const std::size_t obj_idx = static_cast<std::size_t>(pair.obj_idx);
            if (sat_idx >= store.size() || obj_idx >= store.size()) continue;
            if (store.type(sat_idx) != ObjectType::SATELLITE) continue;
            if (store.type(obj_idx) != ObjectType::DEBRIS) continue;

            process_pair(sat_idx, obj_idx);
        }
    } else {
        for (std::size_t i = 0; i < store.size(); ++i) {
            if (store.type(i) != ObjectType::SATELLITE) continue;

            for (std::size_t j = 0; j < store.size(); ++j) {
                if (store.type(j) != ObjectType::DEBRIS) continue;

                process_pair(i, j);
            }
        }
    }

    for (std::size_t i = 0; i < store.size(); ++i) {
        if (sat_collision_mark[i] != 0) {
            out.collision_sat_indices.push_back(static_cast<std::uint32_t>(i));
        }
    }

    store.set_failed_last_tick(out.failed_objects);
    clock.set_epoch_s(target_epoch);
    return true;
}

} // namespace cascade
