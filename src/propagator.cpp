// ---------------------------------------------------------------------------
// propagator.cpp
// ---------------------------------------------------------------------------
#include "propagator.hpp"

#include "orbit_math.hpp"

#include <cmath>

namespace cascade {

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
    const double factor = 1.5 * J2 * MU_KM3_S2 * (R_EARTH_KM * R_EARTH_KM)
        / (std::pow(rmag, 5.0));
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
    apply_j2_secular(el, dt_s);
    if (!elements_to_eci(el, r, v)) {
        return false;
    }
    OrbitalElements refreshed;
    if (!eci_to_elements(r, v, refreshed)) {
        return false;
    }
    el = refreshed;
    return true;
}

bool propagate_rk4_j2(Vec3& r,
                      Vec3& v,
                      double dt_s) noexcept
{
    const int steps = static_cast<int>(std::ceil(std::abs(dt_s) / 60.0));
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
