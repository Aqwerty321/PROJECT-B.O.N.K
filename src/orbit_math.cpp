// ---------------------------------------------------------------------------
// orbit_math.cpp
// ---------------------------------------------------------------------------
#include "orbit_math.hpp"

#include <cmath>

namespace cascade {

static inline double dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static inline double norm(const Vec3& v) noexcept {
    return std::sqrt(dot(v, v));
}

double solve_kepler_elliptic(double M_rad, double e) noexcept
{
    const double M = wrap_0_2pi(M_rad);
    double E = (e < 0.8) ? M : PI;

    for (int iter = 0; iter < 30; ++iter) {
        const double s = std::sin(E);
        const double c = std::cos(E);
        const double f = E - e * s - M;
        const double fp = 1.0 - e * c;
        if (std::abs(fp) < EPS_NUM) break;
        const double dE = -f / fp;
        E += dE;
        if (std::abs(dE) < 1e-12) break;
    }
    return wrap_0_2pi(E);
}

bool eci_to_elements(const Vec3& r, const Vec3& v, OrbitalElements& out) noexcept
{
    const double rmag = norm(r);
    const double vmag = norm(v);
    if (rmag < EPS_NUM || vmag < EPS_NUM) return false;

    const Vec3 h = cross(r, v);
    const double hmag = norm(h);
    if (hmag < EPS_NUM) return false;

    const Vec3 k{0.0, 0.0, 1.0};
    const Vec3 n = cross(k, h);
    const double nmag = norm(n);

    const Vec3 e_vec{
        (v.y * h.z - v.z * h.y) / MU_KM3_S2 - r.x / rmag,
        (v.z * h.x - v.x * h.z) / MU_KM3_S2 - r.y / rmag,
        (v.x * h.y - v.y * h.x) / MU_KM3_S2 - r.z / rmag
    };
    const double e = norm(e_vec);
    if (!(e >= 0.0 && e < 1.0)) return false;

    const double energy = 0.5 * vmag * vmag - MU_KM3_S2 / rmag;
    if (std::abs(energy) < EPS_NUM) return false;
    const double a = -MU_KM3_S2 / (2.0 * energy);
    if (a <= 0.0) return false;

    const double i = std::acos(std::fmax(-1.0, std::fmin(1.0, h.z / hmag)));

    double raan = 0.0;
    if (nmag > EPS_NUM) {
        raan = std::acos(std::fmax(-1.0, std::fmin(1.0, n.x / nmag)));
        if (n.y < 0.0) raan = TWO_PI - raan;
    }

    double argp = 0.0;
    if (nmag > EPS_NUM && e > EPS_NUM) {
        const double c = dot(n, e_vec) / (nmag * e);
        argp = std::acos(std::fmax(-1.0, std::fmin(1.0, c)));
        if (e_vec.z < 0.0) argp = TWO_PI - argp;
    }

    double nu = 0.0;
    if (e > EPS_NUM) {
        const double c = dot(e_vec, r) / (e * rmag);
        nu = std::acos(std::fmax(-1.0, std::fmin(1.0, c)));
        if (dot(r, v) < 0.0) nu = TWO_PI - nu;
    } else if (nmag > EPS_NUM) {
        const double c = dot(n, r) / (nmag * rmag);
        nu = std::acos(std::fmax(-1.0, std::fmin(1.0, c)));
        if (r.z < 0.0) nu = TWO_PI - nu;
    }

    const double E = 2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(nu * 0.5),
                                      std::sqrt(1.0 + e) * std::cos(nu * 0.5));
    const double M = wrap_0_2pi(E - e * std::sin(E));

    out.a_km = a;
    out.e = e;
    out.i_rad = i;
    out.raan_rad = wrap_0_2pi(raan);
    out.argp_rad = wrap_0_2pi(argp);
    out.M_rad = M;
    out.n_rad_s = std::sqrt(MU_KM3_S2 / (a * a * a));
    out.p_km = a * (1.0 - e * e);
    out.rp_km = a * (1.0 - e);
    out.ra_km = a * (1.0 + e);
    return true;
}

bool elements_to_eci(const OrbitalElements& el, Vec3& r_out, Vec3& v_out) noexcept
{
    if (!(el.a_km > 0.0) || !(el.e >= 0.0 && el.e < 1.0)) return false;

    const double E = solve_kepler_elliptic(el.M_rad, el.e);
    const double cE = std::cos(E);
    const double sE = std::sin(E);
    const double one_minus_ecE = 1.0 - el.e * cE;
    if (std::abs(one_minus_ecE) < EPS_NUM) return false;

    const double x_pf = el.a_km * (cE - el.e);
    const double y_pf = el.a_km * std::sqrt(1.0 - el.e * el.e) * sE;

    const double n = std::sqrt(MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
    const double vx_pf = -el.a_km * n * sE / one_minus_ecE;
    const double vy_pf =  el.a_km * n * std::sqrt(1.0 - el.e * el.e) * cE / one_minus_ecE;

    const double cO = std::cos(el.raan_rad);
    const double sO = std::sin(el.raan_rad);
    const double ci = std::cos(el.i_rad);
    const double si = std::sin(el.i_rad);
    const double cw = std::cos(el.argp_rad);
    const double sw = std::sin(el.argp_rad);

    const double R11 = cO * cw - sO * sw * ci;
    const double R12 = -cO * sw - sO * cw * ci;
    const double R21 = sO * cw + cO * sw * ci;
    const double R22 = -sO * sw + cO * cw * ci;
    const double R31 = sw * si;
    const double R32 = cw * si;

    r_out.x = R11 * x_pf + R12 * y_pf;
    r_out.y = R21 * x_pf + R22 * y_pf;
    r_out.z = R31 * x_pf + R32 * y_pf;

    v_out.x = R11 * vx_pf + R12 * vy_pf;
    v_out.y = R21 * vx_pf + R22 * vy_pf;
    v_out.z = R31 * vx_pf + R32 * vy_pf;

    return true;
}

double j2_raan_dot(const OrbitalElements& el) noexcept
{
    if (el.p_km <= EPS_NUM) return 0.0;
    const double fac = -1.5 * J2 * std::sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
        / (std::pow(el.a_km, 3.5) * std::pow(1.0 - el.e * el.e, 2.0));
    return fac * std::cos(el.i_rad);
}

double j2_argp_dot(const OrbitalElements& el) noexcept
{
    if (el.p_km <= EPS_NUM) return 0.0;
    const double fac = 0.75 * J2 * std::sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
        / (std::pow(el.a_km, 3.5) * std::pow(1.0 - el.e * el.e, 2.0));
    return fac * (5.0 * std::cos(el.i_rad) * std::cos(el.i_rad) - 1.0);
}

double j2_M_dot(const OrbitalElements& el) noexcept
{
    if (el.p_km <= EPS_NUM) return el.n_rad_s;
    const double fac = 0.75 * J2 * std::sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
        / (std::pow(el.a_km, 3.5) * std::pow(1.0 - el.e * el.e, 1.5));
    return el.n_rad_s + fac * (3.0 * std::cos(el.i_rad) * std::cos(el.i_rad) - 1.0);
}

void apply_j2_secular(OrbitalElements& el, double dt_s) noexcept
{
    el.raan_rad = wrap_0_2pi(el.raan_rad + j2_raan_dot(el) * dt_s);
    el.argp_rad = wrap_0_2pi(el.argp_rad + j2_argp_dot(el) * dt_s);
    el.M_rad = wrap_0_2pi(el.M_rad + j2_M_dot(el) * dt_s);
}

} // namespace cascade
