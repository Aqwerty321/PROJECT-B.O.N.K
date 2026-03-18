// ---------------------------------------------------------------------------
// simulation_engine.cpp
// ---------------------------------------------------------------------------
#include "simulation_engine.hpp"

#include "broad_phase.hpp"
#include "orbit_math.hpp"
#include "propagator.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cascade {

namespace {

inline double norm2(double x, double y, double z) noexcept
{
    return x * x + y * y + z * z;
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
    out.broad_fail_open_objects = broad.fail_open_objects;
    out.broad_fail_open_satellites = broad.fail_open_satellites;
    out.broad_shell_margin_km = broad.shell_margin_km;
    out.broad_dcriterion_enabled = cfg.broad_phase.enable_dcriterion;
    out.broad_a_bin_width_km = cfg.broad_phase.a_bin_width_km;
    out.broad_band_neighbor_bins = cfg.broad_phase.band_neighbor_bins;

    std::vector<std::uint8_t> sat_collision_mark(n, 0);

    // Conservative narrow-phase sweep over [t0, t1] using multiple linear
    // TCA approximations derived from step endpoints.
    const double tca_guard_km = 0.02;
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
    const double refine_band_km = 0.10;
    const double refine_band_sq = (screening_threshold_km + refine_band_km)
                                * (screening_threshold_km + refine_band_km);
    const double full_refine_band_km = 0.20;
    const double full_refine_band_sq = (screening_threshold_km + full_refine_band_km)
                                     * (screening_threshold_km + full_refine_band_km);

    const std::uint64_t pair_hint = out.failed_objects == 0
        ? static_cast<std::uint64_t>(broad.candidates.size())
        : static_cast<std::uint64_t>(store.satellite_count())
            * static_cast<std::uint64_t>(store.debris_count());

    std::uint64_t full_refine_budget = 64;
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

    if (full_refine_budget < 8) full_refine_budget = 8;
    if (full_refine_budget > 192) full_refine_budget = 192;

    out.narrow_full_refine_budget_allocated = full_refine_budget;

    const auto full_window_min_d2_rk4 = [&](std::size_t sat_idx,
                                            std::size_t obj_idx,
                                            bool& ok) noexcept {
        constexpr int k_samples = 16;
        constexpr double k_substep_s = 1.0;

        Vec3 rs{rx0[sat_idx], ry0[sat_idx], rz0[sat_idx]};
        Vec3 vs{vx0[sat_idx], vy0[sat_idx], vz0[sat_idx]};
        Vec3 rd{rx0[obj_idx], ry0[obj_idx], rz0[obj_idx]};
        Vec3 vd{vx0[obj_idx], vy0[obj_idx], vz0[obj_idx]};

        ok = true;
        double min_d2 = norm2(rs.x - rd.x, rs.y - rd.y, rs.z - rd.z);
        const double dt = step_seconds / static_cast<double>(k_samples);
        for (int s = 0; s < k_samples; ++s) {
            if (!propagate_rk4_j2_substep(rs, vs, dt, k_substep_s)
                || !propagate_rk4_j2_substep(rd, vd, dt, k_substep_s)) {
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
                5.0,
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

        if (d2 > collision_threshold_sq && d2 <= full_refine_band_sq + 1e-9) {
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
