// ---------------------------------------------------------------------------
// propagator.hpp — adaptive propagation (J2+Kepler fast path + RK4 fallback)
// ---------------------------------------------------------------------------
#pragma once

#include "types.hpp"

#include <cstdint>

namespace cascade {

struct PropagationDecision {
    bool use_rk4 = false;
};

struct AdaptivePropagationResult {
    bool ok = false;
    bool used_rk4 = false;
    bool escalated_after_probe = false;
};

PropagationDecision choose_propagation_mode(double step_seconds,
                                            const OrbitalElements& el) noexcept;

bool propagate_fast_j2_kepler(Vec3& r,
                              Vec3& v,
                              OrbitalElements& el,
                              double dt_s) noexcept;

bool propagate_rk4_j2(Vec3& r,
                      Vec3& v,
                      double dt_s) noexcept;

// RK4 J2 propagation with configurable substep size (seconds).
bool propagate_rk4_j2_substep(Vec3& r,
                              Vec3& v,
                              double dt_s,
                              double max_step_s) noexcept;

AdaptivePropagationResult propagate_adaptive(Vec3& r,
                                             Vec3& v,
                                             OrbitalElements& el,
                                             double dt_s) noexcept;

// ECI acceleration model: central gravity + J2 perturbation
Vec3 acceleration_j2(const Vec3& r) noexcept;

} // namespace cascade
