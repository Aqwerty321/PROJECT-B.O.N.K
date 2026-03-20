// ---------------------------------------------------------------------------
// simulation_engine.cpp
// ---------------------------------------------------------------------------
#include "simulation_engine.hpp"

#include "broad_phase.hpp"
#include "orbit_math.hpp"
#include "propagator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#if PROJECTBONK_HAVE_OPENMP
#include <omp.h>
#endif

namespace cascade {

namespace {

// Merge narrow-phase counters from `src` into `dst`.
// Only merges fields that process_pair() actually writes.
inline void merge_narrow_stats(StepRunStats& dst, const StepRunStats& src) noexcept
{
    dst.narrow_pairs_checked += src.narrow_pairs_checked;
    dst.collisions_detected += src.collisions_detected;
    dst.narrow_refined_pairs += src.narrow_refined_pairs;
    dst.narrow_refine_cleared += src.narrow_refine_cleared;
    dst.narrow_refine_fail_open += src.narrow_refine_fail_open;
    dst.narrow_full_refined_pairs += src.narrow_full_refined_pairs;
    dst.narrow_full_refine_cleared += src.narrow_full_refine_cleared;
    dst.narrow_full_refine_fail_open += src.narrow_full_refine_fail_open;
    dst.narrow_full_refine_budget_exhausted += src.narrow_full_refine_budget_exhausted;
    dst.narrow_uncertainty_promoted_pairs += src.narrow_uncertainty_promoted_pairs;
    dst.narrow_plane_phase_evaluated_pairs += src.narrow_plane_phase_evaluated_pairs;
    dst.narrow_plane_phase_shadow_rejected_pairs += src.narrow_plane_phase_shadow_rejected_pairs;
    dst.narrow_plane_phase_hard_rejected_pairs += src.narrow_plane_phase_hard_rejected_pairs;
    dst.narrow_plane_phase_fail_open_pairs += src.narrow_plane_phase_fail_open_pairs;
    dst.narrow_plane_phase_reject_reason_plane_angle_total += src.narrow_plane_phase_reject_reason_plane_angle_total;
    dst.narrow_plane_phase_reject_reason_phase_angle_total += src.narrow_plane_phase_reject_reason_phase_angle_total;
    dst.narrow_plane_phase_fail_open_reason_elements_invalid_total += src.narrow_plane_phase_fail_open_reason_elements_invalid_total;
    dst.narrow_plane_phase_fail_open_reason_eccentricity_guard_total += src.narrow_plane_phase_fail_open_reason_eccentricity_guard_total;
    dst.narrow_plane_phase_fail_open_reason_non_finite_state_total += src.narrow_plane_phase_fail_open_reason_non_finite_state_total;
    dst.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total += src.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total;
    dst.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total += src.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total;
    dst.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total += src.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total;
    dst.narrow_plane_phase_fail_open_reason_uncertainty_override_total += src.narrow_plane_phase_fail_open_reason_uncertainty_override_total;
    dst.narrow_moid_evaluated_pairs += src.narrow_moid_evaluated_pairs;
    dst.narrow_moid_shadow_rejected_pairs += src.narrow_moid_shadow_rejected_pairs;
    dst.narrow_moid_hard_rejected_pairs += src.narrow_moid_hard_rejected_pairs;
    dst.narrow_moid_fail_open_pairs += src.narrow_moid_fail_open_pairs;
    dst.narrow_moid_reject_reason_distance_threshold_total += src.narrow_moid_reject_reason_distance_threshold_total;
    dst.narrow_moid_fail_open_reason_elements_invalid_total += src.narrow_moid_fail_open_reason_elements_invalid_total;
    dst.narrow_moid_fail_open_reason_eccentricity_guard_total += src.narrow_moid_fail_open_reason_eccentricity_guard_total;
    dst.narrow_moid_fail_open_reason_non_finite_state_total += src.narrow_moid_fail_open_reason_non_finite_state_total;
    dst.narrow_moid_fail_open_reason_sampling_failure_total += src.narrow_moid_fail_open_reason_sampling_failure_total;
    dst.narrow_moid_fail_open_reason_hf_placeholder_total += src.narrow_moid_fail_open_reason_hf_placeholder_total;
    dst.narrow_moid_fail_open_reason_uncertainty_override_total += src.narrow_moid_fail_open_reason_uncertainty_override_total;
    dst.narrow_refine_fail_open_reason_rk4_failure_total += src.narrow_refine_fail_open_reason_rk4_failure_total;
    dst.narrow_full_refine_fail_open_reason_rk4_failure_total += src.narrow_full_refine_fail_open_reason_rk4_failure_total;
    dst.narrow_full_refine_fail_open_reason_budget_exhausted_total += src.narrow_full_refine_fail_open_reason_budget_exhausted_total;
    dst.narrow_fail_open_allpairs += src.narrow_fail_open_allpairs;
    // collision_sat_indices merged separately (vector)
}

// env_u64 helper for budget tier thresholds.
inline std::uint64_t sim_env_u64(const char* name, std::uint64_t fallback) noexcept
{
    const char* v = std::getenv(name);
    if (!v) return fallback;
    char* end = nullptr;
    const unsigned long long d = std::strtoull(v, &end, 10);
    if (end == v) return fallback;
    return static_cast<std::uint64_t>(d);
}

// Full-refine budget tier parameters — env-overridable once on first call.
struct BudgetTierParams {
    std::uint64_t tier1_threshold;    // pair_hint > this => budget = tier1_budget
    std::uint64_t tier1_budget;
    std::uint64_t tier2_threshold;    // pair_hint > this => budget = tier2_budget
    std::uint64_t tier2_budget;
    std::uint64_t tier3_threshold;    // pair_hint < this => budget = tier3_budget
    std::uint64_t tier3_budget;
    std::uint64_t tier4_threshold;    // pair_hint < this => budget = tier4_budget
    std::uint64_t tier4_budget;
    std::uint64_t short_step_bonus;   // added when step_seconds <= 5.0
};

static const BudgetTierParams& budget_tier_params() noexcept
{
    static const BudgetTierParams p{
        sim_env_u64("PROJECTBONK_BUDGET_TIER1_THRESHOLD", 2000000),
        sim_env_u64("PROJECTBONK_BUDGET_TIER1_BUDGET", 20),
        sim_env_u64("PROJECTBONK_BUDGET_TIER2_THRESHOLD", 500000),
        sim_env_u64("PROJECTBONK_BUDGET_TIER2_BUDGET", 32),
        sim_env_u64("PROJECTBONK_BUDGET_TIER3_THRESHOLD", 100000),
        sim_env_u64("PROJECTBONK_BUDGET_TIER3_BUDGET", 96),
        sim_env_u64("PROJECTBONK_BUDGET_TIER4_THRESHOLD", 300000),
        sim_env_u64("PROJECTBONK_BUDGET_TIER4_BUDGET", 80),
        sim_env_u64("PROJECTBONK_BUDGET_SHORT_STEP_BONUS", 24),
    };
    return p;
}

struct PlanePhaseGateResult {
    bool evaluated = false;
    bool reject = false;
    bool fail_open = false;
    enum class Reason : std::uint8_t {
        NONE = 0,
        REJECT_PLANE_ANGLE,
        REJECT_PHASE_ANGLE,
        FAIL_OPEN_ELEMENTS_INVALID,
        FAIL_OPEN_ECCENTRICITY_GUARD,
        FAIL_OPEN_NON_FINITE_STATE,
        FAIL_OPEN_ANGULAR_MOMENTUM_DEGENERATE,
        FAIL_OPEN_PLANE_ANGLE_NON_FINITE,
        FAIL_OPEN_PHASE_ANGLE_NON_FINITE,
    } reason = Reason::NONE;
};

struct MoidProxyGateResult {
    bool evaluated = false;
    bool reject = false;
    bool fail_open = false;
    enum class Reason : std::uint8_t {
        NONE = 0,
        REJECT_DISTANCE_THRESHOLD,
        FAIL_OPEN_ELEMENTS_INVALID,
        FAIL_OPEN_ECCENTRICITY_GUARD,
        FAIL_OPEN_NON_FINITE_STATE,
        FAIL_OPEN_SAMPLING_FAILURE,
        FAIL_OPEN_HF_PLACEHOLDER,
    } reason = Reason::NONE;
};

// Per-object data precomputed once per tick for the MOID and plane/phase
// gates.  Avoids redundant StateStore reads, J2 secular propagation, and
// angular-momentum-vector computation on every pair that shares the object.
struct PerObjectPrecomp {
    OrbitalElements el{};      // J2-secularly propagated to target epoch
    Vec3  h_unit{};            // angular momentum unit vector
    double h_norm = 0.0;       // magnitude of h (should be ~1 for valid)
    double phase  = 0.0;       // argp + M (wrapped 0..2pi) for plane/phase gate
    bool  elements_valid = false;   // elements present and finite
    bool  moid_eligible  = false;   // elements_valid && e <= moid_max_e
    bool  plane_phase_eligible = false; // elements_valid && e <= phase_max_e
};

inline double norm2(double x, double y, double z) noexcept
{
    return x * x + y * y + z * z;
}

// populate_moid_elements — fast path using precomputed per-object data.
// The J2 secular propagation, element copies, validity checks, and
// eccentricity guards were already done once per tick in the precomp pass.
inline bool populate_moid_elements(const PerObjectPrecomp& sat_pre,
                                   const PerObjectPrecomp& obj_pre,
                                   OrbitalElements& sat_el,
                                   OrbitalElements& obj_el,
                                   MoidProxyGateResult& out) noexcept
{
    if (!sat_pre.elements_valid || !obj_pre.elements_valid) {
        out.fail_open = true;
        out.reason = MoidProxyGateResult::Reason::FAIL_OPEN_ELEMENTS_INVALID;
        return false;
    }
    if (!sat_pre.moid_eligible || !obj_pre.moid_eligible) {
        out.fail_open = true;
        out.reason = sat_pre.elements_valid && obj_pre.elements_valid
            ? MoidProxyGateResult::Reason::FAIL_OPEN_ECCENTRICITY_GUARD
            : MoidProxyGateResult::Reason::FAIL_OPEN_ELEMENTS_INVALID;
        return false;
    }
    sat_el = sat_pre.el;
    obj_el = obj_pre.el;
    return true;
}

inline bool eci_position_at_mean_anomaly(const OrbitalElements& base,
                                         double mean_anomaly_rad,
                                         Vec3& r_out) noexcept
{
    OrbitalElements sample = base;
    sample.M_rad = wrap_0_2pi(mean_anomaly_rad);
    Vec3 v_dummy{};
    return elements_to_eci(sample, r_out, v_dummy);
}

// evaluate_plane_phase_gate — uses precomputed angular momentum unit vectors
// and phase angles to avoid redundant trig + StateStore reads.
inline PlanePhaseGateResult evaluate_plane_phase_gate(const PerObjectPrecomp& sat_pre,
                                                       const PerObjectPrecomp& obj_pre,
                                                       const NarrowPhaseConfig& cfg) noexcept
{
    PlanePhaseGateResult out{};
    out.evaluated = true;

    if (!sat_pre.elements_valid || !obj_pre.elements_valid) {
        out.fail_open = true;
        out.reason = PlanePhaseGateResult::Reason::FAIL_OPEN_ELEMENTS_INVALID;
        return out;
    }
    if (!sat_pre.plane_phase_eligible || !obj_pre.plane_phase_eligible) {
        out.fail_open = true;
        out.reason = PlanePhaseGateResult::Reason::FAIL_OPEN_ECCENTRICITY_GUARD;
        return out;
    }

    if (!(sat_pre.h_norm > EPS_NUM) || !(obj_pre.h_norm > EPS_NUM)) {
        out.fail_open = true;
        out.reason = PlanePhaseGateResult::Reason::FAIL_OPEN_ANGULAR_MOMENTUM_DEGENERATE;
        return out;
    }

    const double cos_plane = std::clamp(
        (sat_pre.h_unit.x * obj_pre.h_unit.x
         + sat_pre.h_unit.y * obj_pre.h_unit.y
         + sat_pre.h_unit.z * obj_pre.h_unit.z)
            / (sat_pre.h_norm * obj_pre.h_norm),
        -1.0,
        1.0
    );
    const double plane_angle = std::acos(cos_plane);
    if (!std::isfinite(plane_angle)) {
        out.fail_open = true;
        out.reason = PlanePhaseGateResult::Reason::FAIL_OPEN_PLANE_ANGLE_NON_FINITE;
        return out;
    }

    if (plane_angle > cfg.plane_angle_threshold_rad) {
        out.reject = true;
        out.reason = PlanePhaseGateResult::Reason::REJECT_PLANE_ANGLE;
        return out;
    }

    const double phase_delta =
        std::abs(wrap_0_2pi(sat_pre.phase - obj_pre.phase + PI) - PI);
    if (!std::isfinite(phase_delta)) {
        out.fail_open = true;
        out.reason = PlanePhaseGateResult::Reason::FAIL_OPEN_PHASE_ANGLE_NON_FINITE;
        return out;
    }

    if (phase_delta > cfg.phase_angle_threshold_rad) {
        out.reject = true;
        out.reason = PlanePhaseGateResult::Reason::REJECT_PHASE_ANGLE;
    }
    return out;
}

inline MoidProxyGateResult evaluate_moid_proxy_gate(const PerObjectPrecomp& sat_pre,
                                                     const PerObjectPrecomp& obj_pre,
                                                     const NarrowPhaseConfig& cfg) noexcept
{
    MoidProxyGateResult out{};
    out.evaluated = true;

    OrbitalElements sat_el{};
    OrbitalElements obj_el{};
    if (!populate_moid_elements(sat_pre, obj_pre, sat_el, obj_el, out)) {
        return out;
    }

    const std::uint32_t samples = std::max<std::uint32_t>(cfg.moid_samples, 6U);
    const double two_pi = TWO_PI;
    const double step = two_pi / static_cast<double>(samples);

    double min_d2 = std::numeric_limits<double>::infinity();
    bool any_valid = false;

    for (std::uint32_t s = 0; s < samples; ++s) {
        const double sat_u = step * static_cast<double>(s);
        Vec3 sat_r{};
        if (!eci_position_at_mean_anomaly(sat_el, sat_u, sat_r)) {
            continue;
        }

        for (std::uint32_t d = 0; d < samples; ++d) {
            const double obj_u = step * static_cast<double>(d);
            Vec3 obj_r{};
            if (!eci_position_at_mean_anomaly(obj_el, obj_u, obj_r)) {
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
        out.reason = MoidProxyGateResult::Reason::FAIL_OPEN_SAMPLING_FAILURE;
        return out;
    }

    const double threshold_km = std::max(0.0, cfg.moid_reject_threshold_km);
    out.reject = std::sqrt(min_d2) > threshold_km;
    if (out.reject) {
        out.reason = MoidProxyGateResult::Reason::REJECT_DISTANCE_THRESHOLD;
    }
    return out;
}

inline MoidProxyGateResult evaluate_moid_hf_gate(const PerObjectPrecomp& sat_pre,
                                                  const PerObjectPrecomp& obj_pre,
                                                  const NarrowPhaseConfig& cfg) noexcept
{
    MoidProxyGateResult out{};
    out.evaluated = true;

    // HF evaluator: coarse global sampling followed by local refinement around
    // the best coarse cell. Any numeric issue stays fail-open.
    OrbitalElements sat_el{};
    OrbitalElements obj_el{};
    if (!populate_moid_elements(sat_pre, obj_pre, sat_el, obj_el, out)) {
        return out;
    }

    const std::uint32_t base_samples = std::max<std::uint32_t>(cfg.moid_samples, 6U);
    const std::uint32_t coarse_samples = std::max<std::uint32_t>(base_samples * 2U, 12U);
    const double coarse_step = TWO_PI / static_cast<double>(coarse_samples);

    double min_d2 = std::numeric_limits<double>::infinity();
    bool any_valid = false;
    std::uint32_t best_sat_idx = 0;
    std::uint32_t best_obj_idx = 0;

    for (std::uint32_t s = 0; s < coarse_samples; ++s) {
        const double sat_u = coarse_step * static_cast<double>(s);
        Vec3 sat_r{};
        if (!eci_position_at_mean_anomaly(sat_el, sat_u, sat_r)) {
            continue;
        }

        for (std::uint32_t d = 0; d < coarse_samples; ++d) {
            const double obj_u = coarse_step * static_cast<double>(d);
            Vec3 obj_r{};
            if (!eci_position_at_mean_anomaly(obj_el, obj_u, obj_r)) {
                continue;
            }

            any_valid = true;
            const double dx = sat_r.x - obj_r.x;
            const double dy = sat_r.y - obj_r.y;
            const double dz = sat_r.z - obj_r.z;
            const double d2 = norm2(dx, dy, dz);
            if (d2 < min_d2) {
                min_d2 = d2;
                best_sat_idx = s;
                best_obj_idx = d;
            }
        }
    }

    if (!any_valid || !std::isfinite(min_d2)) {
        out.fail_open = true;
        out.reason = MoidProxyGateResult::Reason::FAIL_OPEN_SAMPLING_FAILURE;
        return out;
    }

    double best_sat_u = coarse_step * static_cast<double>(best_sat_idx);
    double best_obj_u = coarse_step * static_cast<double>(best_obj_idx);
    double fine_step = coarse_step * 0.25;

    for (int iter = 0; iter < 3; ++iter) {
        bool iter_valid = false;
        for (int si = -2; si <= 2; ++si) {
            const double sat_u = best_sat_u + static_cast<double>(si) * fine_step;
            Vec3 sat_r{};
            if (!eci_position_at_mean_anomaly(sat_el, sat_u, sat_r)) {
                continue;
            }
            for (int oi = -2; oi <= 2; ++oi) {
                const double obj_u = best_obj_u + static_cast<double>(oi) * fine_step;
                Vec3 obj_r{};
                if (!eci_position_at_mean_anomaly(obj_el, obj_u, obj_r)) {
                    continue;
                }
                iter_valid = true;
                const double d2 = norm2(sat_r.x - obj_r.x,
                                        sat_r.y - obj_r.y,
                                        sat_r.z - obj_r.z);
                if (d2 < min_d2) {
                    min_d2 = d2;
                    best_sat_u = sat_u;
                    best_obj_u = obj_u;
                }
            }
        }

        if (!iter_valid) {
            out.fail_open = true;
            out.reason = MoidProxyGateResult::Reason::FAIL_OPEN_SAMPLING_FAILURE;
            return out;
        }

        fine_step *= 0.5;
    }

    if (!std::isfinite(min_d2)) {
        out.fail_open = true;
        out.reason = MoidProxyGateResult::Reason::FAIL_OPEN_SAMPLING_FAILURE;
        return out;
    }

    const double threshold_km = std::max(0.0, cfg.moid_reject_threshold_km);
    out.reject = std::sqrt(min_d2) > threshold_km;
    if (out.reject) {
        out.reason = MoidProxyGateResult::Reason::REJECT_DISTANCE_THRESHOLD;
    }
    return out;
}

inline MoidProxyGateResult evaluate_moid_analytical_gate(const PerObjectPrecomp& sat_pre,
                                                          const PerObjectPrecomp& obj_pre,
                                                          const NarrowPhaseConfig& cfg) noexcept
{
    MoidProxyGateResult out{};
    out.evaluated = true;

    OrbitalElements sat_el{};
    OrbitalElements obj_el{};
    if (!populate_moid_elements(sat_pre, obj_pre, sat_el, obj_el, out)) {
        return out;
    }

    const double moid_km = compute_moid_analytical(sat_el, obj_el);
    if (!std::isfinite(moid_km)) {
        out.fail_open = true;
        out.reason = MoidProxyGateResult::Reason::FAIL_OPEN_SAMPLING_FAILURE;
        return out;
    }

    const double threshold_km = std::max(0.0, cfg.moid_reject_threshold_km);
    out.reject = moid_km > threshold_km;
    if (out.reject) {
        out.reason = MoidProxyGateResult::Reason::REJECT_DISTANCE_THRESHOLD;
    }
    return out;
}

inline NarrowPhaseConfig::MoidMode resolve_moid_mode(const NarrowPhaseConfig& cfg) noexcept
{
    const char* env = std::getenv("PROJECTBONK_NARROW_MOID_MODE");
    if (env != nullptr) {
        const std::string mode(env);
        if (mode == "hf" || mode == "HF") {
            return NarrowPhaseConfig::MoidMode::HF;
        }
        if (mode == "proxy" || mode == "PROXY") {
            return NarrowPhaseConfig::MoidMode::PROXY;
        }
        if (mode == "analytical" || mode == "ANALYTICAL") {
            return NarrowPhaseConfig::MoidMode::ANALYTICAL;
        }
    }
    return cfg.moid_mode;
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

    // Persistent scratch buffers for pre-step state snapshots.  Using static
    // vectors avoids heap allocation on every tick — resize() is a no-op
    // once capacity has been reached.  Safe because this function is called
    // from a single thread (the engine runtime loop).
    static std::vector<double> rx0, ry0, rz0, vx0, vy0, vz0;
    rx0.resize(n); ry0.resize(n); rz0.resize(n);
    vx0.resize(n); vy0.resize(n); vz0.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        rx0[i] = store.rx(i);
        ry0[i] = store.ry(i);
        rz0[i] = store.rz(i);
        vx0[i] = store.vx(i);
        vy0[i] = store.vy(i);
        vz0[i] = store.vz(i);
    }

    auto t_prop_start = std::chrono::steady_clock::now();
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
    auto t_prop_end = std::chrono::steady_clock::now();
    out.propagation_us = std::chrono::duration<double, std::micro>(t_prop_end - t_prop_start).count();

    // Conservative broad-phase candidate generation at the target epoch.
    auto t_broad_start = std::chrono::steady_clock::now();
    BroadPhaseResult broad = generate_broad_phase_candidates(store, cfg.broad_phase);
    auto t_broad_end = std::chrono::steady_clock::now();
    out.broad_phase_us = std::chrono::duration<double, std::micro>(t_broad_end - t_broad_start).count();
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

    static std::vector<std::uint8_t> sat_collision_mark;
    sat_collision_mark.assign(n, 0);

    // -----------------------------------------------------------------------
    // Per-object precomputation for MOID and plane/phase gates.  Each object's
    // J2-secularly-propagated orbital elements, angular-momentum unit vector,
    // and phase angle are computed once here rather than redundantly per pair.
    // -----------------------------------------------------------------------
    auto t_narrow_precomp_start = std::chrono::steady_clock::now();
    static std::vector<PerObjectPrecomp> obj_precomp;
    obj_precomp.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto& p = obj_precomp[i];
        p = PerObjectPrecomp{}; // reset from prior tick
        if (!store.elements_valid(i)) continue;

        p.el.a_km     = store.a_km(i);
        p.el.e        = store.e(i);
        p.el.i_rad    = store.i_rad(i);
        p.el.raan_rad = store.raan_rad(i);
        p.el.argp_rad = store.argp_rad(i);
        p.el.M_rad    = store.M_rad(i);
        p.el.n_rad_s  = store.n_rad_s(i);
        p.el.p_km     = store.p_km(i);
        p.el.rp_km    = store.rp_km(i);
        p.el.ra_km    = store.ra_km(i);

        if (!std::isfinite(p.el.a_km) || !std::isfinite(p.el.e)
            || !std::isfinite(p.el.i_rad) || !std::isfinite(p.el.raan_rad)
            || !std::isfinite(p.el.argp_rad) || !std::isfinite(p.el.M_rad)) {
            continue; // elements_valid stays false
        }

        // J2 secular propagation to target epoch
        const double obj_epoch = store.telemetry_epoch_s(i);
        const double dt = std::max(0.0, target_epoch - obj_epoch);
        if (!std::isfinite(dt)) continue;
        apply_j2_secular(p.el, dt);

        p.elements_valid = true;
        p.moid_eligible = (p.el.e <= cfg.narrow_phase.moid_max_e);
        p.plane_phase_eligible = (p.el.e <= cfg.narrow_phase.phase_max_e);

        // Angular momentum unit vector from (i, RAAN)
        const double si = std::sin(p.el.i_rad);
        const double ci = std::cos(p.el.i_rad);
        const double sr = std::sin(p.el.raan_rad);
        const double cr = std::cos(p.el.raan_rad);
        p.h_unit = Vec3{ si * sr, -si * cr, ci };
        p.h_norm = std::sqrt(norm2(p.h_unit.x, p.h_unit.y, p.h_unit.z));

        // Phase angle (argp + M)
        p.phase = wrap_0_2pi(p.el.argp_rad + p.el.M_rad);
    }
    auto t_narrow_precomp_end = std::chrono::steady_clock::now();
    out.narrow_precomp_us = std::chrono::duration<double, std::micro>(t_narrow_precomp_end - t_narrow_precomp_start).count();

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

    const auto& btp = budget_tier_params();
    std::uint64_t full_refine_budget =
        std::max<std::uint64_t>(cfg.narrow_phase.full_refine_budget_base, 1);
    if (pair_hint > btp.tier1_threshold) {
        full_refine_budget = btp.tier1_budget;
    } else if (pair_hint > btp.tier2_threshold) {
        full_refine_budget = btp.tier2_budget;
    } else if (pair_hint < btp.tier3_threshold) {
        full_refine_budget = btp.tier3_budget;
    } else if (pair_hint < btp.tier4_threshold) {
        full_refine_budget = btp.tier4_budget;
    }

    if (step_seconds > 120.0) {
        full_refine_budget = full_refine_budget / 2;
    } else if (step_seconds <= 5.0) {
        full_refine_budget += btp.short_step_bonus;
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

    // Atomic budget for thread-safe decrement in the parallel narrow sweep.
    std::atomic<uint64_t> full_refine_budget_atomic{full_refine_budget};

    // Resolve MOID mode once per tick (not per-pair) to avoid hot-path
    // getenv() calls and mid-sweep mode changes.
    const NarrowPhaseConfig::MoidMode resolved_moid_mode = resolve_moid_mode(cfg.narrow_phase);

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
            // Remember positions before this sub-step for linear interpolation
            const double prev_dx = rs.x - rd.x;
            const double prev_dy = rs.y - rd.y;
            const double prev_dz = rs.z - rd.z;
            // Relative velocity at sub-step start for linear TCA interpolation
            const double rel_vx = vs.x - vd.x;
            const double rel_vy = vs.y - vd.y;
            const double rel_vz = vs.z - vd.z;

            if (!propagate_rk4_j2_substep(rs, vs, dt, rk4_substep_s)
                || !propagate_rk4_j2_substep(rd, vd, dt, rk4_substep_s)) {
                ok = false;
                return 0.0;
            }
            // Check distance at sub-step endpoint
            const double dx = rs.x - rd.x;
            const double dy = rs.y - rd.y;
            const double dz = rs.z - rd.z;
            const double d2 = norm2(dx, dy, dz);
            if (d2 < min_d2) min_d2 = d2;

            // P2.1: Linear TCA interpolation within this sub-step.
            // Use relative position at start and relative velocity at start
            // to find closest approach between the two RK4 sample points.
            const double interp_d2 = min_d2_linear_segment(
                prev_dx, prev_dy, prev_dz,
                rel_vx, rel_vy, rel_vz,
                0.0, dt);
            if (interp_d2 < min_d2) min_d2 = interp_d2;

            // Also use secant velocity (position difference / dt) for a
            // second linear TCA estimate that captures nonlinear curvature.
            if (dt > EPS_NUM) {
                const double sec_vx = (dx - prev_dx) / dt;
                const double sec_vy = (dy - prev_dy) / dt;
                const double sec_vz = (dz - prev_dz) / dt;
                const double sec_d2 = min_d2_linear_segment(
                    prev_dx, prev_dy, prev_dz,
                    sec_vx, sec_vy, sec_vz,
                    0.0, dt);
                if (sec_d2 < min_d2) min_d2 = sec_d2;
            }
        }
        return min_d2;
    };

    auto process_pair = [&](std::size_t sat_idx, std::size_t obj_idx,
                            StepRunStats& local_out) noexcept {
        ++local_out.narrow_pairs_checked;

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
            ++local_out.narrow_uncertainty_promoted_pairs;
        }

        PlanePhaseGateResult plane_phase{};
        const bool near_refine_window =
            ((d2 >= collision_threshold_sq && d2 <= refine_band_sq)
             || (d2 > collision_threshold_sq
                 && (d2 <= full_refine_band_sq + 1e-9 || uncertainty_promoted)));
        if (near_refine_window) {
            plane_phase = evaluate_plane_phase_gate(obj_precomp[sat_idx], obj_precomp[obj_idx], cfg.narrow_phase);
            if (plane_phase.evaluated) {
                ++local_out.narrow_plane_phase_evaluated_pairs;
            }
            if (plane_phase.fail_open) {
                ++local_out.narrow_plane_phase_fail_open_pairs;
                switch (plane_phase.reason) {
                    case PlanePhaseGateResult::Reason::FAIL_OPEN_ELEMENTS_INVALID:
                        ++local_out.narrow_plane_phase_fail_open_reason_elements_invalid_total;
                        break;
                    case PlanePhaseGateResult::Reason::FAIL_OPEN_ECCENTRICITY_GUARD:
                        ++local_out.narrow_plane_phase_fail_open_reason_eccentricity_guard_total;
                        break;
                    case PlanePhaseGateResult::Reason::FAIL_OPEN_NON_FINITE_STATE:
                        ++local_out.narrow_plane_phase_fail_open_reason_non_finite_state_total;
                        break;
                    case PlanePhaseGateResult::Reason::FAIL_OPEN_ANGULAR_MOMENTUM_DEGENERATE:
                        ++local_out.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total;
                        break;
                    case PlanePhaseGateResult::Reason::FAIL_OPEN_PLANE_ANGLE_NON_FINITE:
                        ++local_out.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total;
                        break;
                    case PlanePhaseGateResult::Reason::FAIL_OPEN_PHASE_ANGLE_NON_FINITE:
                        ++local_out.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total;
                        break;
                    default:
                        break;
                }
            }
            if (plane_phase.reject) {
                if (plane_phase.reason == PlanePhaseGateResult::Reason::REJECT_PLANE_ANGLE) {
                    ++local_out.narrow_plane_phase_reject_reason_plane_angle_total;
                } else if (plane_phase.reason == PlanePhaseGateResult::Reason::REJECT_PHASE_ANGLE) {
                    ++local_out.narrow_plane_phase_reject_reason_phase_angle_total;
                }
                if (plane_phase_shadow) {
                    ++local_out.narrow_plane_phase_shadow_rejected_pairs;
                }
                if (plane_phase_filter && !plane_phase.fail_open && !uncertainty_promoted) {
                    ++local_out.narrow_plane_phase_hard_rejected_pairs;
                    d2 = std::max(d2, std::max(refine_band_sq, full_refine_band_sq) + 1.0);
                } else if (plane_phase_filter && uncertainty_promoted) {
                    ++local_out.narrow_plane_phase_fail_open_pairs;
                    ++local_out.narrow_plane_phase_fail_open_reason_uncertainty_override_total;
                }
            }

            MoidProxyGateResult moid{};
            if (resolved_moid_mode == NarrowPhaseConfig::MoidMode::ANALYTICAL) {
                moid = evaluate_moid_analytical_gate(obj_precomp[sat_idx], obj_precomp[obj_idx], cfg.narrow_phase);
            } else if (resolved_moid_mode == NarrowPhaseConfig::MoidMode::HF) {
                moid = evaluate_moid_hf_gate(obj_precomp[sat_idx], obj_precomp[obj_idx], cfg.narrow_phase);
            } else {
                moid = evaluate_moid_proxy_gate(obj_precomp[sat_idx], obj_precomp[obj_idx], cfg.narrow_phase);
            }
            if (moid.evaluated) {
                ++local_out.narrow_moid_evaluated_pairs;
            }
            if (moid.fail_open) {
                ++local_out.narrow_moid_fail_open_pairs;
                switch (moid.reason) {
                    case MoidProxyGateResult::Reason::FAIL_OPEN_ELEMENTS_INVALID:
                        ++local_out.narrow_moid_fail_open_reason_elements_invalid_total;
                        break;
                    case MoidProxyGateResult::Reason::FAIL_OPEN_ECCENTRICITY_GUARD:
                        ++local_out.narrow_moid_fail_open_reason_eccentricity_guard_total;
                        break;
                    case MoidProxyGateResult::Reason::FAIL_OPEN_NON_FINITE_STATE:
                        ++local_out.narrow_moid_fail_open_reason_non_finite_state_total;
                        break;
                    case MoidProxyGateResult::Reason::FAIL_OPEN_SAMPLING_FAILURE:
                        ++local_out.narrow_moid_fail_open_reason_sampling_failure_total;
                        break;
                    case MoidProxyGateResult::Reason::FAIL_OPEN_HF_PLACEHOLDER:
                        ++local_out.narrow_moid_fail_open_reason_hf_placeholder_total;
                        break;
                    default:
                        break;
                }
            }
            if (moid.reject) {
                if (moid.reason == MoidProxyGateResult::Reason::REJECT_DISTANCE_THRESHOLD) {
                    ++local_out.narrow_moid_reject_reason_distance_threshold_total;
                }
                if (moid_shadow) {
                    ++local_out.narrow_moid_shadow_rejected_pairs;
                }
                if (moid_filter && !moid.fail_open && !uncertainty_promoted) {
                    ++local_out.narrow_moid_hard_rejected_pairs;
                    d2 = std::max(d2, std::max(refine_band_sq, full_refine_band_sq) + 1.0);
                } else if (moid_filter && uncertainty_promoted) {
                    ++local_out.narrow_moid_fail_open_pairs;
                    ++local_out.narrow_moid_fail_open_reason_uncertainty_override_total;
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

            ++local_out.narrow_refined_pairs;
            if (!refine_ok) {
                ++local_out.narrow_refine_fail_open;
                ++local_out.narrow_refine_fail_open_reason_rk4_failure_total;
                d2 = 0.0;
            } else if (d2_refined >= collision_threshold_sq) {
                ++local_out.narrow_refine_cleared;
                d2 = d2_refined;
            } else {
                d2 = d2_refined;
            }
        }

        if (d2 > collision_threshold_sq
            && (d2 <= full_refine_band_sq + 1e-9 || uncertainty_promoted)) {
            // Atomically try to claim a budget slot.
            uint64_t prev = full_refine_budget_atomic.load(std::memory_order_relaxed);
            bool got_budget = false;
            while (prev > 0) {
                if (full_refine_budget_atomic.compare_exchange_weak(
                        prev, prev - 1,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    got_budget = true;
                    break;
                }
            }
            if (!got_budget) {
                ++local_out.narrow_full_refine_budget_exhausted;
                ++local_out.narrow_full_refine_fail_open_reason_budget_exhausted_total;
                // Fail-open policy: if a pair is near-threshold but budget is
                // exhausted, classify as potential conjunction to avoid
                // false negatives from under-refinement.
                d2 = 0.0;
            } else {
                bool full_ok = true;
                const double d2_full = full_window_min_d2_rk4(sat_idx, obj_idx, full_ok);
                ++local_out.narrow_full_refined_pairs;
                if (!full_ok) {
                    ++local_out.narrow_full_refine_fail_open;
                    ++local_out.narrow_full_refine_fail_open_reason_rk4_failure_total;
                    d2 = 0.0;
                } else if (d2_full >= collision_threshold_sq) {
                    ++local_out.narrow_full_refine_cleared;
                    d2 = d2_full;
                } else {
                    d2 = d2_full;
                }
            }
        }

        if (d2 < collision_threshold_sq) {
            ++local_out.collisions_detected;
            sat_collision_mark[sat_idx] = 1;
        }
    };

    auto t_narrow_sweep_start = std::chrono::steady_clock::now();
    if (out.failed_objects == 0) {
        // Happy path: all objects propagated successfully.
        // Sort candidates by (sat_idx, obj_idx) so that full-refine budget
        // exhaustion is deterministic regardless of broad-phase hash-map
        // iteration order.
        std::sort(broad.candidates.begin(), broad.candidates.end(),
                  [](const BroadPhasePair& a, const BroadPhasePair& b) noexcept {
                      if (a.sat_idx != b.sat_idx) return a.sat_idx < b.sat_idx;
                      return a.obj_idx < b.obj_idx;
                  });

        const auto n_candidates = static_cast<int64_t>(broad.candidates.size());

#if PROJECTBONK_HAVE_OPENMP
        // Each thread accumulates into its own StepRunStats, merged after.
        const int max_threads = omp_get_max_threads();
        std::vector<StepRunStats> thread_stats(static_cast<size_t>(max_threads));

        #pragma omp parallel
        {
            const int tid = omp_get_thread_num();
            StepRunStats& local = thread_stats[static_cast<size_t>(tid)];

            #pragma omp for schedule(dynamic, 256)
            for (int64_t ci = 0; ci < n_candidates; ++ci) {
                const BroadPhasePair& pair = broad.candidates[static_cast<size_t>(ci)];
                const std::size_t sat_idx = static_cast<std::size_t>(pair.sat_idx);
                const std::size_t obj_idx = static_cast<std::size_t>(pair.obj_idx);
                if (sat_idx >= store.size() || obj_idx >= store.size()) continue;
                if (store.type(sat_idx) != ObjectType::SATELLITE) continue;
                if (store.type(obj_idx) != ObjectType::DEBRIS) continue;

                process_pair(sat_idx, obj_idx, local);
            }
        }

        // Merge per-thread stats into the main output.
        for (int t = 0; t < max_threads; ++t) {
            merge_narrow_stats(out, thread_stats[static_cast<size_t>(t)]);
        }
#else
        for (int64_t ci = 0; ci < n_candidates; ++ci) {
            const BroadPhasePair& pair = broad.candidates[static_cast<size_t>(ci)];
            const std::size_t sat_idx = static_cast<std::size_t>(pair.sat_idx);
            const std::size_t obj_idx = static_cast<std::size_t>(pair.obj_idx);
            if (sat_idx >= store.size() || obj_idx >= store.size()) continue;
            if (store.type(sat_idx) != ObjectType::SATELLITE) continue;
            if (store.type(obj_idx) != ObjectType::DEBRIS) continue;

            process_pair(sat_idx, obj_idx, out);
        }
#endif
    } else {
        // Fail-open: if any object failed propagation, bypass broad-phase
        // filtering entirely and brute-force all SAT×DEBRIS pairs.  This is
        // necessary because (a) failed objects have stale states that place
        // them in wrong broad-phase shells, and (b) successfully-propagated
        // objects that share a shell with a failed object may also have
        // incorrect pairing.  The all-pairs sweep is the only guaranteed
        // zero-FN fallback.
        //
        // Performance note: this is O(S*D) which is expensive for large
        // constellations.  A future optimisation (T4-series) can implement
        // per-object tracking once the broad phase handles crossing orbits.
        ++out.narrow_fail_open_allpairs;

        // Build a flat list of (sat, debris) pairs for OpenMP iteration.
        std::vector<std::pair<std::size_t, std::size_t>> all_pairs;
        all_pairs.reserve(store.satellite_count() * store.debris_count());
        for (std::size_t i = 0; i < store.size(); ++i) {
            if (store.type(i) != ObjectType::SATELLITE) continue;
            for (std::size_t j = 0; j < store.size(); ++j) {
                if (store.type(j) != ObjectType::DEBRIS) continue;
                all_pairs.emplace_back(i, j);
            }
        }

        const auto n_all = static_cast<int64_t>(all_pairs.size());

#if PROJECTBONK_HAVE_OPENMP
        const int max_threads = omp_get_max_threads();
        std::vector<StepRunStats> thread_stats(static_cast<size_t>(max_threads));

        #pragma omp parallel
        {
            const int tid = omp_get_thread_num();
            StepRunStats& local = thread_stats[static_cast<size_t>(tid)];

            #pragma omp for schedule(dynamic, 256)
            for (int64_t pi = 0; pi < n_all; ++pi) {
                const auto& [si, di] = all_pairs[static_cast<size_t>(pi)];
                process_pair(si, di, local);
            }
        }

        for (int t = 0; t < max_threads; ++t) {
            merge_narrow_stats(out, thread_stats[static_cast<size_t>(t)]);
        }
#else
        for (int64_t pi = 0; pi < n_all; ++pi) {
            const auto& [si, di] = all_pairs[static_cast<size_t>(pi)];
            process_pair(si, di, out);
        }
#endif
    }
    auto t_narrow_sweep_end = std::chrono::steady_clock::now();
    out.narrow_sweep_us = std::chrono::duration<double, std::micro>(t_narrow_sweep_end - t_narrow_sweep_start).count();

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
