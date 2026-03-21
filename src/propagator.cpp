// ---------------------------------------------------------------------------
// propagator.cpp
// ---------------------------------------------------------------------------
#include "propagator.hpp"

#include "env_util.hpp"
#include "orbit_math.hpp"

#include <cmath>
#include <cstdlib>

namespace cascade {

using env_util::env_double;

namespace {

// Propagator fast-lane thresholds — env-overridable once on first call.
struct FastLaneParams {
    double max_dt_s;
    double max_e;
    double min_perigee_alt_km;
    double ext_max_dt_s;
    double ext_max_e;
    double ext_min_perigee_alt_km;
    double probe_max_step_s;
    double probe_pos_thresh_km;
    double probe_vel_thresh_ms;
};

static const FastLaneParams& fast_lane_params() noexcept
{
    static const FastLaneParams p{
        env_double("PROJECTBONK_FAST_LANE_MAX_DT_S", 30.0),
        env_double("PROJECTBONK_FAST_LANE_MAX_E", 0.02),
        env_double("PROJECTBONK_FAST_LANE_MIN_PERIGEE_ALT_KM", 500.0),
        env_double("PROJECTBONK_FAST_LANE_EXT_MAX_DT_S", 45.0),
        env_double("PROJECTBONK_FAST_LANE_EXT_MAX_E", 0.003),
        env_double("PROJECTBONK_FAST_LANE_EXT_MIN_PERIGEE_ALT_KM", 650.0),
        env_double("PROJECTBONK_PROBE_MAX_STEP_S", 120.0),
        env_double("PROJECTBONK_PROBE_POS_THRESH_KM", 0.5),
        env_double("PROJECTBONK_PROBE_VEL_THRESH_MS", 0.5),
    };
    return p;
}

} // namespace

static inline Vec3 add(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline Vec3 mul(const Vec3& a, double s) noexcept {
    return Vec3{a.x * s, a.y * s, a.z * s};
}

PropagationDecision choose_propagation_mode(double step_seconds,
                                            const OrbitalElements& el) noexcept
{
    PropagationDecision d;
    const double perigee_alt_km = el.rp_km - R_EARTH_KM;
    d.use_rk4 = (step_seconds > 21600.0) || (perigee_alt_km < 350.0) || (el.e >= 0.98);
    return d;
}

Vec3 acceleration_j2(const Vec3& r) noexcept
{
    const double x = r.x;
    const double y = r.y;
    const double z = r.z;
    const double r2 = x * x + y * y + z * z;
    const double rmag = std::sqrt(r2);
    if (rmag < EPS_NUM) return Vec3{};

    const double r3 = r2 * rmag;
    const double mu_over_r3 = MU_KM3_S2 / r3;

    Vec3 a{-mu_over_r3 * x, -mu_over_r3 * y, -mu_over_r3 * z};

    const double z2 = z * z;
    const double r2r2 = r2 * r2;
    const double r5 = r2r2 * rmag;
    const double factor = 1.5 * J2 * MU_KM3_S2 * (R_EARTH_KM * R_EARTH_KM)
        / r5;
    const double common = 5.0 * z2 / r2;

    a.x += factor * x * (common - 1.0);
    a.y += factor * y * (common - 1.0);
    a.z += factor * z * (common - 3.0);
    return a;
}

bool propagate_fast_j2_kepler(Vec3& r,
                              Vec3& v,
                              OrbitalElements& el,
                              double dt_s) noexcept
{
    // Substep secular propagation to reduce long-horizon drift while keeping
    // this path cheaper than full RK4 integration.
    const int steps = static_cast<int>(std::ceil(std::abs(dt_s) / 300.0));
    if (steps <= 0) return true;
    const double h = dt_s / static_cast<double>(steps);

    for (int s = 0; s < steps; ++s) {
        apply_j2_secular(el, h);
        if (!elements_to_eci(el, r, v)) {
            return false;
        }
        OrbitalElements refreshed;
        if (!eci_to_elements(r, v, refreshed)) {
            return false;
        }
        el = refreshed;
    }

    return true;
}

AdaptivePropagationResult propagate_adaptive(Vec3& r,
                                             Vec3& v,
                                             OrbitalElements& el,
                                             double dt_s) noexcept
{
    AdaptivePropagationResult out;
    const PropagationDecision mode = choose_propagation_mode(dt_s, el);

    if (mode.use_rk4) {
        out.used_rk4 = true;
        out.ok = propagate_rk4_j2(r, v, dt_s);
        if (out.ok) {
            out.ok = eci_to_elements(r, v, el);
        }
        return out;
    }

    // Low-risk fast lane: very small dt, low eccentricity, and comfortably
    // above drag-sensitive altitudes.
    const double perigee_alt_km = el.rp_km - R_EARTH_KM;
    const auto& fl = fast_lane_params();
    const bool low_risk_fast_lane =
        (dt_s <= fl.max_dt_s
         && el.e <= fl.max_e
         && perigee_alt_km >= fl.min_perigee_alt_km)
        ||
        (dt_s <= fl.ext_max_dt_s
         && el.e <= fl.ext_max_e
         && perigee_alt_km >= fl.ext_min_perigee_alt_km);

    if (low_risk_fast_lane) {
        if (propagate_fast_j2_kepler(r, v, el, dt_s)) {
            out.ok = true;
            out.used_rk4 = false;
            return out;
        }

        out.used_rk4 = true;
        out.escalated_after_probe = true;
        out.ok = propagate_rk4_j2(r, v, dt_s);
        if (out.ok) {
            out.ok = eci_to_elements(r, v, el);
        }
        return out;
    }

    // Probe fast mode against a coarse RK4 prediction and escalate to full RK4
    // when drift exceeds conservative safety thresholds.
    Vec3 r_fast = r;
    Vec3 v_fast = v;
    OrbitalElements el_fast = el;
    if (!propagate_fast_j2_kepler(r_fast, v_fast, el_fast, dt_s)) {
        out.used_rk4 = true;
        out.escalated_after_probe = true;
        out.ok = propagate_rk4_j2(r, v, dt_s);
        if (out.ok) {
            out.ok = eci_to_elements(r, v, el);
        }
        return out;
    }

    Vec3 r_probe = r;
    Vec3 v_probe = v;
    if (!propagate_rk4_j2_substep(r_probe, v_probe, dt_s, fl.probe_max_step_s)) {
        // Probe failed; accept fast path to avoid throwing away progress.
        r = r_fast;
        v = v_fast;
        el = el_fast;
        out.ok = true;
        out.used_rk4 = false;
        return out;
    }

    const double dx = r_fast.x - r_probe.x;
    const double dy = r_fast.y - r_probe.y;
    const double dz = r_fast.z - r_probe.z;
    const double drift_km = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double dvx = v_fast.x - v_probe.x;
    const double dvy = v_fast.y - v_probe.y;
    const double dvz = v_fast.z - v_probe.z;
    const double drift_kms = std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz);
    const double drift_ms = drift_kms * 1000.0;

    if (drift_km < fl.probe_pos_thresh_km && drift_ms < fl.probe_vel_thresh_ms) {
        r = r_fast;
        v = v_fast;
        el = el_fast;
        out.ok = true;
        out.used_rk4 = false;
        return out;
    }

    out.used_rk4 = true;
    out.escalated_after_probe = true;
    out.ok = propagate_rk4_j2(r, v, dt_s);
    if (out.ok) {
        out.ok = eci_to_elements(r, v, el);
    }
    return out;
}

bool propagate_rk4_j2(Vec3& r,
                      Vec3& v,
                      double dt_s) noexcept
{
    return propagate_rk4_j2_substep(r, v, dt_s, 60.0);
}

bool propagate_rk4_j2_substep(Vec3& r,
                              Vec3& v,
                              double dt_s,
                              double max_step_s) noexcept
{
    if (!(max_step_s > 0.0)) {
        return false;
    }
    const int steps = static_cast<int>(std::ceil(std::abs(dt_s) / max_step_s));
    if (steps <= 0) return true;
    const double h = dt_s / static_cast<double>(steps);

    for (int s = 0; s < steps; ++s) {
        const Vec3 k1_r = v;
        const Vec3 k1_v = acceleration_j2(r);

        const Vec3 r2 = add(r, mul(k1_r, 0.5 * h));
        const Vec3 v2 = add(v, mul(k1_v, 0.5 * h));
        const Vec3 k2_r = v2;
        const Vec3 k2_v = acceleration_j2(r2);

        const Vec3 r3 = add(r, mul(k2_r, 0.5 * h));
        const Vec3 v3 = add(v, mul(k2_v, 0.5 * h));
        const Vec3 k3_r = v3;
        const Vec3 k3_v = acceleration_j2(r3);

        const Vec3 r4 = add(r, mul(k3_r, h));
        const Vec3 v4 = add(v, mul(k3_v, h));
        const Vec3 k4_r = v4;
        const Vec3 k4_v = acceleration_j2(r4);

        r = add(r, mul(add(add(k1_r, mul(k2_r, 2.0)), add(mul(k3_r, 2.0), k4_r)), h / 6.0));
        v = add(v, mul(add(add(k1_v, mul(k2_v, 2.0)), add(mul(k3_v, 2.0), k4_v)), h / 6.0));

        if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z)
            || !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
            return false;
        }
    }

    return true;
}

} // namespace cascade
