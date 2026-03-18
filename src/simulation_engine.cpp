// ---------------------------------------------------------------------------
// simulation_engine.cpp
// ---------------------------------------------------------------------------
#include "simulation_engine.hpp"

#include "broad_phase.hpp"
#include "orbit_math.hpp"
#include "propagator.hpp"

#include <algorithm>
#include <vector>

namespace cascade {

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
    const double collision_threshold_sq = (COLLISION_THRESHOLD_KM + tca_guard_km)
                                        * (COLLISION_THRESHOLD_KM + tca_guard_km);

    const auto norm2 = [](double x, double y, double z) noexcept {
        return x * x + y * y + z * z;
    };

    const auto line_min_d2 = [](double rx,
                                double ry,
                                double rz,
                                double vx,
                                double vy,
                                double vz,
                                double t_lo,
                                double t_hi) noexcept {
        const double vv = vx * vx + vy * vy + vz * vz;
        if (vv <= EPS_NUM) {
            return rx * rx + ry * ry + rz * rz;
        }
        double t = -(rx * vx + ry * vy + rz * vz) / vv;
        if (t < t_lo) t = t_lo;
        if (t > t_hi) t = t_hi;
        const double px = rx + vx * t;
        const double py = ry + vy * t;
        const double pz = rz + vz * t;
        return px * px + py * py + pz * pz;
    };

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
                          line_min_d2(r0x, r0y, r0z,
                                      v0x, v0y, v0z,
                                      0.0, step_seconds));

        min_d2 = std::min(min_d2,
                          line_min_d2(r1x, r1y, r1z,
                                      -v1x, -v1y, -v1z,
                                      0.0, step_seconds));

        const double vavgx = 0.5 * (v0x + v1x);
        const double vavgy = 0.5 * (v0y + v1y);
        const double vavgz = 0.5 * (v0z + v1z);
        min_d2 = std::min(min_d2,
                          line_min_d2(r0x, r0y, r0z,
                                      vavgx, vavgy, vavgz,
                                      0.0, step_seconds));

        if (step_seconds > EPS_NUM) {
            const double vsecx = (r1x - r0x) / step_seconds;
            const double vsecy = (r1y - r0y) / step_seconds;
            const double vsecz = (r1z - r0z) / step_seconds;
            min_d2 = std::min(min_d2,
                              line_min_d2(r0x, r0y, r0z,
                                          vsecx, vsecy, vsecz,
                                          0.0, step_seconds));
        }

        return min_d2;
    };

    // If propagation failed for any object this tick, fall back to full
    // SATELLITE-vs-DEBRIS scan to avoid relying on broad-phase filtering.
    if (out.failed_objects == 0) {
        for (const BroadPhasePair& pair : broad.candidates) {
            const std::size_t sat_idx = static_cast<std::size_t>(pair.sat_idx);
            const std::size_t obj_idx = static_cast<std::size_t>(pair.obj_idx);
            if (sat_idx >= store.size() || obj_idx >= store.size()) continue;
            if (store.type(sat_idx) != ObjectType::SATELLITE) continue;
            if (store.type(obj_idx) != ObjectType::DEBRIS) continue;

            ++out.narrow_pairs_checked;

            const double d2 = tca_min_d2(sat_idx, obj_idx);
            if (d2 < collision_threshold_sq) {
                ++out.collisions_detected;
                sat_collision_mark[sat_idx] = 1;
            }
        }
    } else {
        for (std::size_t i = 0; i < store.size(); ++i) {
            if (store.type(i) != ObjectType::SATELLITE) continue;

            for (std::size_t j = 0; j < store.size(); ++j) {
                if (store.type(j) != ObjectType::DEBRIS) continue;

                ++out.narrow_pairs_checked;

                const double d2 = tca_min_d2(i, j);
                if (d2 < collision_threshold_sq) {
                    ++out.collisions_detected;
                    sat_collision_mark[i] = 1;
                }
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
