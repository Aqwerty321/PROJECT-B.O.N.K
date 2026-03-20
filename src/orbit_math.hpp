// ---------------------------------------------------------------------------
// orbit_math.hpp — orbital element conversions and secular updates
// ---------------------------------------------------------------------------
#pragma once

#include "types.hpp"

namespace cascade {

// Convert ECI position/velocity (km, km/s) to classical orbital elements.
// Returns false for numerically invalid or non-elliptic states.
bool eci_to_elements(const Vec3& r, const Vec3& v, OrbitalElements& out) noexcept;

// Convert classical orbital elements to ECI state.
// Returns false if elements are invalid.
bool elements_to_eci(const OrbitalElements& el, Vec3& r_out, Vec3& v_out) noexcept;

// Solve Kepler's equation M = E - e*sin(E) for elliptic orbits (0 <= e < 1).
// Returns wrapped eccentric anomaly E in [0, 2*pi).
double solve_kepler_elliptic(double M_rad, double e) noexcept;

// J2 secular rates (rad/s)
double j2_raan_dot(const OrbitalElements& el) noexcept;
double j2_argp_dot(const OrbitalElements& el) noexcept;
double j2_M_dot(const OrbitalElements& el) noexcept;

// Apply secular drift in place and keep angles wrapped.
void apply_j2_secular(OrbitalElements& el, double dt_s) noexcept;

// ---------------------------------------------------------------------------
// Analytical MOID (Minimum Orbit Intersection Distance)
// ---------------------------------------------------------------------------
// Computes the minimum distance between two Keplerian orbits using:
//   1. Orbital frame vectors (P, Q) for closed-form position on orbit
//   2. Coarse grid scan over true anomaly pairs to locate basins
//   3. Newton-Raphson refinement with analytical gradient/Hessian of D²
//
// Returns the MOID in km.  On failure (degenerate orbits), returns +infinity.
// Both orbits must be elliptic (0 <= e < 1) with a > 0.
double compute_moid_analytical(const OrbitalElements& el1,
                               const OrbitalElements& el2) noexcept;

} // namespace cascade
