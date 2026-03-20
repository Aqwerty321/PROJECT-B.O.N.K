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
    // a^3.5 = a^3 * a^0.5 = a^3 * sqrt(a)
    const double a3 = el.a_km * el.a_km * el.a_km;
    const double a3_5 = a3 * std::sqrt(el.a_km);
    const double e2 = el.e * el.e;
    const double oneminus_e2 = 1.0 - e2;
    const double oneminus_e2_sq = oneminus_e2 * oneminus_e2;
    const double fac = -1.5 * J2 * std::sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
        / (a3_5 * oneminus_e2_sq);
    return fac * std::cos(el.i_rad);
}

double j2_argp_dot(const OrbitalElements& el) noexcept
{
    if (el.p_km <= EPS_NUM) return 0.0;
    const double a3 = el.a_km * el.a_km * el.a_km;
    const double a3_5 = a3 * std::sqrt(el.a_km);
    const double e2 = el.e * el.e;
    const double oneminus_e2 = 1.0 - e2;
    const double oneminus_e2_sq = oneminus_e2 * oneminus_e2;
    const double fac = 0.75 * J2 * std::sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
        / (a3_5 * oneminus_e2_sq);
    return fac * (5.0 * std::cos(el.i_rad) * std::cos(el.i_rad) - 1.0);
}

double j2_M_dot(const OrbitalElements& el) noexcept
{
    if (el.p_km <= EPS_NUM) return el.n_rad_s;
    const double a3 = el.a_km * el.a_km * el.a_km;
    const double a3_5 = a3 * std::sqrt(el.a_km);
    const double e2 = el.e * el.e;
    const double oneminus_e2 = 1.0 - e2;
    const double oneminus_e2_1_5 = oneminus_e2 * std::sqrt(oneminus_e2);
    const double fac = 0.75 * J2 * std::sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
        / (a3_5 * oneminus_e2_1_5);
    return el.n_rad_s + fac * (3.0 * std::cos(el.i_rad) * std::cos(el.i_rad) - 1.0);
}

// ---------------------------------------------------------------------------
// Analytical MOID — orbital frame approach with Newton-Raphson refinement
// ---------------------------------------------------------------------------

// Orbital frame vectors for a Keplerian orbit:
//   P = direction of periapsis in ECI
//   Q = 90 deg ahead of P in the orbital plane
struct OrbitalFrame {
    Vec3 P;  // periapsis direction
    Vec3 Q;  // in-plane, 90 deg ahead
};

static inline OrbitalFrame compute_orbital_frame(const OrbitalElements& el) noexcept
{
    const double cO = std::cos(el.raan_rad);
    const double sO = std::sin(el.raan_rad);
    const double ci = std::cos(el.i_rad);
    const double si = std::sin(el.i_rad);
    const double cw = std::cos(el.argp_rad);
    const double sw = std::sin(el.argp_rad);

    OrbitalFrame fr{};
    // P = R_z(-Omega) * R_x(-i) * R_z(-omega) * [1,0,0]
    fr.P.x = cO * cw - sO * sw * ci;
    fr.P.y = sO * cw + cO * sw * ci;
    fr.P.z = sw * si;

    // Q = R_z(-Omega) * R_x(-i) * R_z(-omega) * [0,1,0]
    fr.Q.x = -cO * sw - sO * cw * ci;
    fr.Q.y = -sO * sw + cO * cw * ci;
    fr.Q.z = cw * si;

    return fr;
}

// Position on orbit at true anomaly f:
//   r(f) = (p / (1 + e*cos(f))) * (cos(f)*P + sin(f)*Q)
static inline Vec3 orbit_pos(double f, double p, double e,
                             const OrbitalFrame& fr) noexcept
{
    const double cf = std::cos(f);
    const double sf = std::sin(f);
    const double r_scalar = p / (1.0 + e * cf);
    return Vec3{
        r_scalar * (cf * fr.P.x + sf * fr.Q.x),
        r_scalar * (cf * fr.P.y + sf * fr.Q.y),
        r_scalar * (cf * fr.P.z + sf * fr.Q.z)
    };
}

// First derivative of position w.r.t. true anomaly f:
//   dr/df = (p * h^2) * (-sin(f)*P + (e + cos(f))*Q)
// where h = 1/(1 + e*cos(f))
static inline Vec3 orbit_dpos(double f, double p, double e,
                              const OrbitalFrame& fr) noexcept
{
    const double cf = std::cos(f);
    const double sf = std::sin(f);
    const double h = 1.0 / (1.0 + e * cf);
    const double ph2 = p * h * h;
    const double a_coeff = -sf;
    const double b_coeff = e + cf;
    return Vec3{
        ph2 * (a_coeff * fr.P.x + b_coeff * fr.Q.x),
        ph2 * (a_coeff * fr.P.y + b_coeff * fr.Q.y),
        ph2 * (a_coeff * fr.P.z + b_coeff * fr.Q.z)
    };
}

// Second derivative of position w.r.t. true anomaly f:
//   d²r/df² = p*h^2 * [(-2*e*sin²f*h - cos f)*P
//                     + (2*e*sinf*(e+cosf)*h - sinf)*Q]
static inline Vec3 orbit_d2pos(double f, double p, double e,
                               const OrbitalFrame& fr) noexcept
{
    const double cf = std::cos(f);
    const double sf = std::sin(f);
    const double h = 1.0 / (1.0 + e * cf);
    const double ph2 = p * h * h;
    const double esf = e * sf;
    const double a_coeff = -2.0 * esf * sf * h - cf;
    const double b_coeff = 2.0 * esf * (e + cf) * h - sf;
    return Vec3{
        ph2 * (a_coeff * fr.P.x + b_coeff * fr.Q.x),
        ph2 * (a_coeff * fr.P.y + b_coeff * fr.Q.y),
        ph2 * (a_coeff * fr.P.z + b_coeff * fr.Q.z)
    };
}

static inline double vec3_dot(const Vec3& a, const Vec3& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double compute_moid_analytical(const OrbitalElements& el1,
                               const OrbitalElements& el2) noexcept
{
    constexpr double INF = std::numeric_limits<double>::infinity();

    // Validate both orbits are elliptic
    if (!(el1.a_km > 0.0) || !(el1.e >= 0.0 && el1.e < 1.0)) return INF;
    if (!(el2.a_km > 0.0) || !(el2.e >= 0.0 && el2.e < 1.0)) return INF;

    const double p1 = el1.a_km * (1.0 - el1.e * el1.e);
    const double p2 = el2.a_km * (1.0 - el2.e * el2.e);
    if (!(p1 > 0.0) || !(p2 > 0.0)) return INF;

    const OrbitalFrame fr1 = compute_orbital_frame(el1);
    const OrbitalFrame fr2 = compute_orbital_frame(el2);

    // Phase 1: Coarse grid scan.
    // Use 72 samples per orbit (5-deg resolution) = 5184 evaluations.
    // Track the top-K local minima for Newton refinement.
    constexpr int N_COARSE = 72;
    constexpr double COARSE_STEP = TWO_PI / static_cast<double>(N_COARSE);

    // Precompute positions on both orbits at coarse grid points.
    Vec3 pos1[N_COARSE];
    Vec3 pos2[N_COARSE];
    for (int i = 0; i < N_COARSE; ++i) {
        const double f = COARSE_STEP * static_cast<double>(i);
        pos1[i] = orbit_pos(f, p1, el1.e, fr1);
        pos2[i] = orbit_pos(f, p2, el2.e, fr2);
    }

    // Find best D² at each (i,j) and identify local minima.
    // A point is a local minimum if D²(i,j) <= all 8 neighbors on the torus.
    // We keep up to MAX_SEEDS best candidates for Newton refinement.
    constexpr int MAX_SEEDS = 16;
    struct Seed {
        double d2;
        int i, j;
    };
    Seed seeds[MAX_SEEDS];
    int n_seeds = 0;

    // Full grid D² computation + local minimum detection in one pass.
    // We need D² at (i,j) and its 8 neighbors; store the full grid.
    // 72*72 = 5184 doubles = ~41 KB on stack — fine.
    double grid[N_COARSE][N_COARSE];
    for (int i = 0; i < N_COARSE; ++i) {
        for (int j = 0; j < N_COARSE; ++j) {
            const double dx = pos1[i].x - pos2[j].x;
            const double dy = pos1[i].y - pos2[j].y;
            const double dz = pos1[i].z - pos2[j].z;
            grid[i][j] = dx * dx + dy * dy + dz * dz;
        }
    }

    // Identify local minima on the torus (wrapping at boundaries).
    for (int i = 0; i < N_COARSE; ++i) {
        for (int j = 0; j < N_COARSE; ++j) {
            const double val = grid[i][j];
            bool is_min = true;
            for (int di = -1; di <= 1 && is_min; ++di) {
                for (int dj = -1; dj <= 1 && is_min; ++dj) {
                    if (di == 0 && dj == 0) continue;
                    const int ni = (i + di + N_COARSE) % N_COARSE;
                    const int nj = (j + dj + N_COARSE) % N_COARSE;
                    if (grid[ni][nj] < val) {
                        is_min = false;
                    }
                }
            }
            if (is_min) {
                if (n_seeds < MAX_SEEDS) {
                    seeds[n_seeds++] = {val, i, j};
                } else {
                    // Replace the worst seed if this one is better.
                    int worst = 0;
                    for (int k = 1; k < MAX_SEEDS; ++k) {
                        if (seeds[k].d2 > seeds[worst].d2) worst = k;
                    }
                    if (val < seeds[worst].d2) {
                        seeds[worst] = {val, i, j};
                    }
                }
            }
        }
    }

    // If no seeds found (shouldn't happen for valid orbits), use global best.
    if (n_seeds == 0) {
        double best = INF;
        int bi = 0, bj = 0;
        for (int i = 0; i < N_COARSE; ++i) {
            for (int j = 0; j < N_COARSE; ++j) {
                if (grid[i][j] < best) {
                    best = grid[i][j];
                    bi = i;
                    bj = j;
                }
            }
        }
        seeds[0] = {best, bi, bj};
        n_seeds = 1;
    }

    // Phase 2: Newton-Raphson refinement of each seed.
    // We minimize D²(f1, f2) = |r1(f1) - r2(f2)|² using analytical
    // gradient and Hessian.
    //
    // Gradient:
    //   g1 = ∂D²/∂f1 = 2*(r1-r2) · dr1/df1
    //   g2 = ∂D²/∂f2 = -2*(r1-r2) · dr2/df2
    //
    // Hessian:
    //   H11 = 2*(dr1·dr1) + 2*(r1-r2)·d²r1/df1²
    //   H12 = -2*(dr1·dr2)
    //   H22 = 2*(dr2·dr2) - 2*(r1-r2)·d²r2/df2²

    double global_min_d2 = INF;
    constexpr int MAX_NEWTON = 20;
    constexpr double NEWTON_TOL = 1e-14;   // tolerance on |grad|² for D²
    constexpr double STEP_TOL = 1e-12;     // tolerance on step size

    for (int s = 0; s < n_seeds; ++s) {
        double f1 = COARSE_STEP * static_cast<double>(seeds[s].i);
        double f2 = COARSE_STEP * static_cast<double>(seeds[s].j);

        for (int iter = 0; iter < MAX_NEWTON; ++iter) {
            const Vec3 r1 = orbit_pos(f1, p1, el1.e, fr1);
            const Vec3 r2 = orbit_pos(f2, p2, el2.e, fr2);
            const Vec3 dr1 = orbit_dpos(f1, p1, el1.e, fr1);
            const Vec3 dr2 = orbit_dpos(f2, p2, el2.e, fr2);
            const Vec3 d2r1 = orbit_d2pos(f1, p1, el1.e, fr1);
            const Vec3 d2r2 = orbit_d2pos(f2, p2, el2.e, fr2);

            const Vec3 delta{r1.x - r2.x, r1.y - r2.y, r1.z - r2.z};

            // Gradient
            const double g1 = 2.0 * vec3_dot(delta, dr1);
            const double g2 = -2.0 * vec3_dot(delta, dr2);

            const double grad_norm2 = g1 * g1 + g2 * g2;
            if (grad_norm2 < NEWTON_TOL) break;

            // Hessian
            const double H11 = 2.0 * (vec3_dot(dr1, dr1) + vec3_dot(delta, d2r1));
            const double H12 = -2.0 * vec3_dot(dr1, dr2);
            const double H22 = 2.0 * (vec3_dot(dr2, dr2) - vec3_dot(delta, d2r2));

            // Solve 2x2 system: H * [df1, df2]^T = -[g1, g2]^T
            const double det = H11 * H22 - H12 * H12;
            if (std::abs(det) < 1e-30) {
                // Degenerate Hessian — try gradient descent step instead.
                const double alpha = 1e-6;
                f1 -= alpha * g1;
                f2 -= alpha * g2;
                f1 = wrap_0_2pi(f1);
                f2 = wrap_0_2pi(f2);
                continue;
            }

            const double inv_det = 1.0 / det;
            double df1 = -inv_det * (H22 * g1 - H12 * g2);
            double df2 = -inv_det * (-H12 * g1 + H11 * g2);

            // Damping: limit Newton step to pi radians max to avoid
            // overshooting into a different basin.
            const double step_norm = std::sqrt(df1 * df1 + df2 * df2);
            if (step_norm > PI) {
                const double scale = PI / step_norm;
                df1 *= scale;
                df2 *= scale;
            }

            f1 += df1;
            f2 += df2;
            f1 = wrap_0_2pi(f1);
            f2 = wrap_0_2pi(f2);

            if (df1 * df1 + df2 * df2 < STEP_TOL) break;
        }

        // Evaluate final D² at the converged point.
        const Vec3 r1 = orbit_pos(f1, p1, el1.e, fr1);
        const Vec3 r2 = orbit_pos(f2, p2, el2.e, fr2);
        const double dx = r1.x - r2.x;
        const double dy = r1.y - r2.y;
        const double dz = r1.z - r2.z;
        const double d2 = dx * dx + dy * dy + dz * dz;

        if (d2 < global_min_d2) {
            global_min_d2 = d2;
        }
    }

    if (!std::isfinite(global_min_d2) || global_min_d2 < 0.0) return INF;
    return std::sqrt(global_min_d2);
}

void apply_j2_secular(OrbitalElements& el, double dt_s) noexcept
{
    if (el.p_km <= EPS_NUM) {
        // Only M_dot has a non-zero fallback (el.n_rad_s)
        el.M_rad = wrap_0_2pi(el.M_rad + el.n_rad_s * dt_s);
        return;
    }

    // Precompute shared quantities once for all three rates
    const double a3 = el.a_km * el.a_km * el.a_km;
    const double a3_5 = a3 * std::sqrt(el.a_km);
    const double e2 = el.e * el.e;
    const double oneminus_e2 = 1.0 - e2;
    const double sqrt_oneminus_e2 = std::sqrt(oneminus_e2);
    const double oneminus_e2_sq = oneminus_e2 * oneminus_e2;       // (1-e²)²
    const double oneminus_e2_1_5 = oneminus_e2 * sqrt_oneminus_e2; // (1-e²)^1.5
    const double cos_i = std::cos(el.i_rad);
    const double cos_i_sq = cos_i * cos_i;

    // Common J2 coefficient: J2 * sqrt(mu) * Re² / a^3.5
    const double base = J2 * std::sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM) / a3_5;

    // RAAN dot = -1.5 * base / (1-e²)² * cos(i)
    const double raan_dot = -1.5 * base / oneminus_e2_sq * cos_i;

    // argp dot = 0.75 * base / (1-e²)² * (5cos²i - 1)
    const double argp_dot = 0.75 * base / oneminus_e2_sq * (5.0 * cos_i_sq - 1.0);

    // M dot = n + 0.75 * base / (1-e²)^1.5 * (3cos²i - 1)
    const double M_dot = el.n_rad_s + 0.75 * base / oneminus_e2_1_5 * (3.0 * cos_i_sq - 1.0);

    el.raan_rad = wrap_0_2pi(el.raan_rad + raan_dot * dt_s);
    el.argp_rad = wrap_0_2pi(el.argp_rad + argp_dot * dt_s);
    el.M_rad = wrap_0_2pi(el.M_rad + M_dot * dt_s);
}

} // namespace cascade
