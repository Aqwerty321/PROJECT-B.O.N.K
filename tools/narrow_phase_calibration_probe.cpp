// ---------------------------------------------------------------------------
// narrow_phase_calibration_probe.cpp
//
// Deterministic narrow-phase calibration probe over synthetic load.
// Reports uncertainty-promotion and refinement/budget evidence under
// configurable narrow-phase settings.
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "orbit_math.hpp"
#include "simulation_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>

namespace {

double rad(double deg)
{
    return deg * cascade::PI / 180.0;
}

double vec_norm(const cascade::Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

cascade::Vec3 cross(const cascade::Vec3& a, const cascade::Vec3& b)
{
    return cascade::Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

cascade::Vec3 normalize_or(const cascade::Vec3& v, const cascade::Vec3& fallback)
{
    const double n = vec_norm(v);
    if (n > cascade::EPS_NUM) {
        return cascade::Vec3{v.x / n, v.y / n, v.z / n};
    }

    const double f = vec_norm(fallback);
    if (f > cascade::EPS_NUM) {
        return cascade::Vec3{fallback.x / f, fallback.y / f, fallback.z / f};
    }

    return cascade::Vec3{1.0, 0.0, 0.0};
}

bool parse_int(const char* s, int& out)
{
    if (s == nullptr) return false;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end == nullptr || *end != '\0') return false;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(v);
    return true;
}

bool parse_double(const char* s, double& out)
{
    if (s == nullptr) return false;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (end == nullptr || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

void seed_store(cascade::StateStore& store,
                cascade::SimClock& clock,
                int sat_count,
                int deb_count,
                std::uint64_t seed)
{
    std::mt19937_64 rng(seed);

    std::uniform_real_distribution<double> sat_a(6778.0, 7378.0);
    std::uniform_real_distribution<double> sat_e(0.0, 0.02);
    std::uniform_real_distribution<double> sat_i(rad(15.0), rad(99.0));

    std::uniform_real_distribution<double> deb_a(6678.0, 7800.0);
    std::uniform_real_distribution<double> deb_e(0.0, 0.18);
    std::uniform_real_distribution<double> deb_i(rad(0.0), rad(130.0));

    std::uniform_real_distribution<double> ang(0.0, cascade::TWO_PI);

    auto insert_object = [&](int idx, cascade::ObjectType type) {
        cascade::OrbitalElements el{};
        if (type == cascade::ObjectType::SATELLITE) {
            el.a_km = sat_a(rng);
            el.e = sat_e(rng);
            el.i_rad = sat_i(rng);
        } else {
            el.a_km = deb_a(rng);
            el.e = deb_e(rng);
            el.i_rad = deb_i(rng);
        }
        el.raan_rad = ang(rng);
        el.argp_rad = ang(rng);
        el.M_rad = ang(rng);
        el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
        el.p_km = el.a_km * (1.0 - el.e * el.e);
        el.rp_km = el.a_km * (1.0 - el.e);
        el.ra_km = el.a_km * (1.0 + el.e);

        cascade::Vec3 r{};
        cascade::Vec3 v{};
        if (!cascade::elements_to_eci(el, r, v)) {
            return;
        }

        const std::string id = (type == cascade::ObjectType::SATELLITE)
            ? ("SAT-" + std::to_string(idx))
            : ("DEB-" + std::to_string(idx));

        bool conflict = false;
        (void)store.upsert(id,
                           type,
                           r.x,
                           r.y,
                           r.z,
                           v.x,
                           v.y,
                           v.z,
                           clock.epoch_s(),
                           conflict);

        const std::size_t obj_idx = store.find(id);
        if (obj_idx < store.size()) {
            store.set_elements(obj_idx, el, true);
            store.set_telemetry_epoch_s(obj_idx, clock.epoch_s());
        }
    };

    for (int i = 0; i < sat_count; ++i) {
        insert_object(i, cascade::ObjectType::SATELLITE);
    }
    for (int i = 0; i < deb_count; ++i) {
        insert_object(i, cascade::ObjectType::DEBRIS);
    }
}

int inject_high_rel_fixture_pairs(cascade::StateStore& store,
                                  int sat_count,
                                  int deb_count,
                                  int pairs_per_sat,
                                  double offset_km,
                                  double offset_jitter_km,
                                  double rel_speed_km_s)
{
    if (pairs_per_sat <= 0 || deb_count <= 0 || sat_count <= 0) {
        return 0;
    }

    int injected = 0;
    for (int s = 0; s < sat_count; ++s) {
        const std::string sat_id = "SAT-" + std::to_string(s);
        const std::size_t sat_idx = store.find(sat_id);
        if (sat_idx >= store.size() || store.type(sat_idx) != cascade::ObjectType::SATELLITE) {
            continue;
        }

        const cascade::Vec3 sat_r{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
        const cascade::Vec3 sat_v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};

        const cascade::Vec3 r_hat = normalize_or(sat_r, cascade::Vec3{1.0, 0.0, 0.0});
        const cascade::Vec3 t_hat = normalize_or(sat_v, cascade::Vec3{0.0, 1.0, 0.0});
        cascade::Vec3 n_hat = cross(r_hat, t_hat);
        if (vec_norm(n_hat) <= cascade::EPS_NUM) {
            n_hat = cross(r_hat, cascade::Vec3{0.0, 0.0, 1.0});
        }
        n_hat = normalize_or(n_hat, cascade::Vec3{0.0, 0.0, 1.0});

        for (int k = 0; k < pairs_per_sat; ++k) {
            const int deb_i = s * pairs_per_sat + k;
            if (deb_i >= deb_count) {
                break;
            }

            const std::string deb_id = "DEB-" + std::to_string(deb_i);
            const std::size_t deb_idx = store.find(deb_id);
            if (deb_idx >= store.size() || store.type(deb_idx) != cascade::ObjectType::DEBRIS) {
                continue;
            }

            const double sign = ((s + k) % 2 == 0) ? 1.0 : -1.0;
            const double offset = offset_km + offset_jitter_km * static_cast<double>(k % 3);

            store.rx_mut(deb_idx) = sat_r.x + r_hat.x * offset;
            store.ry_mut(deb_idx) = sat_r.y + r_hat.y * offset;
            store.rz_mut(deb_idx) = sat_r.z + r_hat.z * offset;

            store.vx_mut(deb_idx) = sat_v.x + n_hat.x * (sign * rel_speed_km_s);
            store.vy_mut(deb_idx) = sat_v.y + n_hat.y * (sign * rel_speed_km_s);
            store.vz_mut(deb_idx) = sat_v.z + n_hat.z * (sign * rel_speed_km_s);

            cascade::OrbitalElements deb_el{};
            const cascade::Vec3 deb_r{
                store.rx(deb_idx),
                store.ry(deb_idx),
                store.rz(deb_idx)
            };
            const cascade::Vec3 deb_v{
                store.vx(deb_idx),
                store.vy(deb_idx),
                store.vz(deb_idx)
            };
            if (cascade::eci_to_elements(deb_r, deb_v, deb_el)) {
                store.set_elements(deb_idx, deb_el, true);
            } else {
                // Fail-open path is still represented when injected states are
                // not convertible to elliptic elements.
                store.set_elements(deb_idx, cascade::OrbitalElements{}, false);
            }
            ++injected;
        }
    }

    return injected;
}

void print_usage(const char* argv0)
{
    std::cout
        << "usage: " << argv0
        << " [satellites] [debris] [ticks] [step_s]"
        << " [high_rel_speed_km_s] [high_rel_speed_extra_band_km]"
        << " [full_refine_budget_base] [full_refine_budget_min] [full_refine_budget_max]"
        << " [full_refine_band_km] [refine_band_km] [tca_guard_km]"
        << " [full_refine_samples] [full_refine_substep_s] [micro_refine_max_step_s]"
        << " [fixture_pairs_per_sat] [fixture_rel_speed_km_s]"
        << " [fixture_offset_km] [fixture_offset_jitter_km]"
        << " [moid_shadow] [moid_filter] [moid_samples]"
        << " [moid_reject_threshold_km] [moid_max_e]"
        << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    int sat_count = 50;
    int deb_count = 10000;
    int ticks = 5;
    double step_s = 30.0;
    int fixture_pairs_per_sat = 3;
    double fixture_rel_speed_km_s = -1.0;
    double fixture_offset_km = -1.0;
    double fixture_offset_jitter_km = 0.01;

    cascade::StepRunConfig cfg{};
    cfg.broad_phase.enable_dcriterion = false;
    cfg.broad_phase.shadow_dcriterion = true;

    if (argc >= 2 && !parse_int(argv[1], sat_count)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 3 && !parse_int(argv[2], deb_count)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 4 && !parse_int(argv[3], ticks)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 5 && !parse_double(argv[4], step_s)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 6 && !parse_double(argv[5], cfg.narrow_phase.high_rel_speed_km_s)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 7 && !parse_double(argv[6], cfg.narrow_phase.high_rel_speed_extra_band_km)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 8) {
        int v = 0;
        if (!parse_int(argv[7], v)) {
            print_usage(argv[0]);
            return 2;
        }
        cfg.narrow_phase.full_refine_budget_base = static_cast<std::uint64_t>(std::max(1, v));
    }
    if (argc >= 9) {
        int v = 0;
        if (!parse_int(argv[8], v)) {
            print_usage(argv[0]);
            return 2;
        }
        cfg.narrow_phase.full_refine_budget_min = static_cast<std::uint64_t>(std::max(1, v));
    }
    if (argc >= 10) {
        int v = 0;
        if (!parse_int(argv[9], v)) {
            print_usage(argv[0]);
            return 2;
        }
        cfg.narrow_phase.full_refine_budget_max = static_cast<std::uint64_t>(std::max(1, v));
    }
    if (argc >= 11 && !parse_double(argv[10], cfg.narrow_phase.full_refine_band_km)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 12 && !parse_double(argv[11], cfg.narrow_phase.refine_band_km)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 13 && !parse_double(argv[12], cfg.narrow_phase.tca_guard_km)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 14) {
        int v = 0;
        if (!parse_int(argv[13], v)) {
            print_usage(argv[0]);
            return 2;
        }
        cfg.narrow_phase.full_refine_samples = static_cast<std::uint32_t>(std::max(1, v));
    }
    if (argc >= 15 && !parse_double(argv[14], cfg.narrow_phase.full_refine_substep_s)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 16 && !parse_double(argv[15], cfg.narrow_phase.micro_refine_max_step_s)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 17 && !parse_int(argv[16], fixture_pairs_per_sat)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 18 && !parse_double(argv[17], fixture_rel_speed_km_s)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 19 && !parse_double(argv[18], fixture_offset_km)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 20 && !parse_double(argv[19], fixture_offset_jitter_km)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 21) {
        int v = 0;
        if (!parse_int(argv[20], v)) {
            print_usage(argv[0]);
            return 2;
        }
        cfg.narrow_phase.moid_shadow = (v != 0);
    }
    if (argc >= 22) {
        int v = 0;
        if (!parse_int(argv[21], v)) {
            print_usage(argv[0]);
            return 2;
        }
        cfg.narrow_phase.moid_filter = (v != 0);
    }
    if (argc >= 23) {
        int v = 0;
        if (!parse_int(argv[22], v)) {
            print_usage(argv[0]);
            return 2;
        }
        cfg.narrow_phase.moid_samples = static_cast<std::uint32_t>(std::max(6, v));
    }
    if (argc >= 24 && !parse_double(argv[23], cfg.narrow_phase.moid_reject_threshold_km)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc >= 25 && !parse_double(argv[24], cfg.narrow_phase.moid_max_e)) {
        print_usage(argv[0]);
        return 2;
    }

    if (cfg.narrow_phase.full_refine_budget_max < cfg.narrow_phase.full_refine_budget_min) {
        cfg.narrow_phase.full_refine_budget_max = cfg.narrow_phase.full_refine_budget_min;
    }

    cascade::StateStore store(static_cast<std::size_t>(sat_count + deb_count + 128));
    cascade::SimClock clock;
    clock.set_epoch_s(1773292800.0);
    seed_store(store, clock, sat_count, deb_count, 2026031901ULL);

    const double screening_threshold_km =
        cascade::COLLISION_THRESHOLD_KM + std::max(0.0, cfg.narrow_phase.tca_guard_km);
    const double full_refine_band_km = std::max(0.0, cfg.narrow_phase.full_refine_band_km);
    const double high_rel_extra_band_km = std::max(0.0, cfg.narrow_phase.high_rel_speed_extra_band_km);

    if (!(fixture_rel_speed_km_s > 0.0)) {
        fixture_rel_speed_km_s = std::max(1.0, cfg.narrow_phase.high_rel_speed_km_s + 1.0);
    }

    if (!(fixture_offset_km > 0.0)) {
        const double extra_for_offset = std::max(0.005, std::min(0.05, high_rel_extra_band_km * 0.5));
        fixture_offset_km = screening_threshold_km + full_refine_band_km + extra_for_offset;
    }

    const double min_promoted_offset = screening_threshold_km + full_refine_band_km + 1e-3;
    if (fixture_offset_km < min_promoted_offset) {
        fixture_offset_km = min_promoted_offset;
    }

    if (high_rel_extra_band_km > 0.0) {
        const double max_promoted_offset =
            screening_threshold_km + full_refine_band_km + high_rel_extra_band_km - 1e-3;
        if (fixture_offset_km > max_promoted_offset) {
            fixture_offset_km = max_promoted_offset;
        }
    }

    if (fixture_offset_jitter_km < 0.0) {
        fixture_offset_jitter_km = 0.0;
    }

    const int fixture_pairs_injected = inject_high_rel_fixture_pairs(
        store,
        sat_count,
        deb_count,
        fixture_pairs_per_sat,
        fixture_offset_km,
        fixture_offset_jitter_km,
        fixture_rel_speed_km_s
    );

    std::uint64_t narrow_pairs_total = 0;
    std::uint64_t uncertainty_promoted_total = 0;
    std::uint64_t full_refined_total = 0;
    std::uint64_t refine_fail_open_total = 0;
    std::uint64_t full_refine_fail_open_total = 0;
    std::uint64_t full_budget_alloc_total = 0;
    std::uint64_t full_budget_exhausted_total = 0;
    std::uint64_t plane_phase_evaluated_total = 0;
    std::uint64_t plane_phase_shadow_rejected_total = 0;
    std::uint64_t plane_phase_hard_rejected_total = 0;
    std::uint64_t plane_phase_fail_open_total = 0;
    std::uint64_t plane_phase_reject_reason_plane_angle_total = 0;
    std::uint64_t plane_phase_reject_reason_phase_angle_total = 0;
    std::uint64_t plane_phase_fail_open_reason_elements_invalid_total = 0;
    std::uint64_t plane_phase_fail_open_reason_eccentricity_guard_total = 0;
    std::uint64_t plane_phase_fail_open_reason_non_finite_state_total = 0;
    std::uint64_t plane_phase_fail_open_reason_angular_momentum_degenerate_total = 0;
    std::uint64_t plane_phase_fail_open_reason_plane_angle_non_finite_total = 0;
    std::uint64_t plane_phase_fail_open_reason_phase_angle_non_finite_total = 0;
    std::uint64_t plane_phase_fail_open_reason_uncertainty_override_total = 0;
    std::uint64_t moid_evaluated_total = 0;
    std::uint64_t moid_shadow_rejected_total = 0;
    std::uint64_t moid_hard_rejected_total = 0;
    std::uint64_t moid_fail_open_total = 0;
    std::uint64_t moid_reject_reason_distance_threshold_total = 0;
    std::uint64_t moid_fail_open_reason_elements_invalid_total = 0;
    std::uint64_t moid_fail_open_reason_eccentricity_guard_total = 0;
    std::uint64_t moid_fail_open_reason_non_finite_state_total = 0;
    std::uint64_t moid_fail_open_reason_sampling_failure_total = 0;
    std::uint64_t moid_fail_open_reason_uncertainty_override_total = 0;
    std::uint64_t refine_fail_open_reason_rk4_failure_total = 0;
    std::uint64_t full_refine_fail_open_reason_rk4_failure_total = 0;
    std::uint64_t full_refine_fail_open_reason_budget_exhausted_total = 0;
    std::uint64_t collisions_total = 0;

    for (int t = 0; t < ticks; ++t) {
        cascade::StepRunStats stats{};
        if (!cascade::run_simulation_step(store, clock, step_s, stats, cfg)) {
            std::cerr << "narrow_phase_calibration_probe_result=FAIL\n";
            std::cerr << "reason=simulation_step_failed\n";
            return 1;
        }

        narrow_pairs_total += stats.narrow_pairs_checked;
        uncertainty_promoted_total += stats.narrow_uncertainty_promoted_pairs;
        full_refined_total += stats.narrow_full_refined_pairs;
        refine_fail_open_total += stats.narrow_refine_fail_open;
        full_refine_fail_open_total += stats.narrow_full_refine_fail_open;
        full_budget_alloc_total += stats.narrow_full_refine_budget_allocated;
        full_budget_exhausted_total += stats.narrow_full_refine_budget_exhausted;
        plane_phase_evaluated_total += stats.narrow_plane_phase_evaluated_pairs;
        plane_phase_shadow_rejected_total += stats.narrow_plane_phase_shadow_rejected_pairs;
        plane_phase_hard_rejected_total += stats.narrow_plane_phase_hard_rejected_pairs;
        plane_phase_fail_open_total += stats.narrow_plane_phase_fail_open_pairs;
        plane_phase_reject_reason_plane_angle_total += stats.narrow_plane_phase_reject_reason_plane_angle_total;
        plane_phase_reject_reason_phase_angle_total += stats.narrow_plane_phase_reject_reason_phase_angle_total;
        plane_phase_fail_open_reason_elements_invalid_total += stats.narrow_plane_phase_fail_open_reason_elements_invalid_total;
        plane_phase_fail_open_reason_eccentricity_guard_total += stats.narrow_plane_phase_fail_open_reason_eccentricity_guard_total;
        plane_phase_fail_open_reason_non_finite_state_total += stats.narrow_plane_phase_fail_open_reason_non_finite_state_total;
        plane_phase_fail_open_reason_angular_momentum_degenerate_total += stats.narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total;
        plane_phase_fail_open_reason_plane_angle_non_finite_total += stats.narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total;
        plane_phase_fail_open_reason_phase_angle_non_finite_total += stats.narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total;
        plane_phase_fail_open_reason_uncertainty_override_total += stats.narrow_plane_phase_fail_open_reason_uncertainty_override_total;
        moid_evaluated_total += stats.narrow_moid_evaluated_pairs;
        moid_shadow_rejected_total += stats.narrow_moid_shadow_rejected_pairs;
        moid_hard_rejected_total += stats.narrow_moid_hard_rejected_pairs;
        moid_fail_open_total += stats.narrow_moid_fail_open_pairs;
        moid_reject_reason_distance_threshold_total += stats.narrow_moid_reject_reason_distance_threshold_total;
        moid_fail_open_reason_elements_invalid_total += stats.narrow_moid_fail_open_reason_elements_invalid_total;
        moid_fail_open_reason_eccentricity_guard_total += stats.narrow_moid_fail_open_reason_eccentricity_guard_total;
        moid_fail_open_reason_non_finite_state_total += stats.narrow_moid_fail_open_reason_non_finite_state_total;
        moid_fail_open_reason_sampling_failure_total += stats.narrow_moid_fail_open_reason_sampling_failure_total;
        moid_fail_open_reason_uncertainty_override_total += stats.narrow_moid_fail_open_reason_uncertainty_override_total;
        refine_fail_open_reason_rk4_failure_total += stats.narrow_refine_fail_open_reason_rk4_failure_total;
        full_refine_fail_open_reason_rk4_failure_total += stats.narrow_full_refine_fail_open_reason_rk4_failure_total;
        full_refine_fail_open_reason_budget_exhausted_total += stats.narrow_full_refine_fail_open_reason_budget_exhausted_total;
        collisions_total += stats.collisions_detected;
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "narrow_phase_calibration_probe\n";
    std::cout << "satellites=" << sat_count << "\n";
    std::cout << "debris=" << deb_count << "\n";
    std::cout << "ticks=" << ticks << "\n";
    std::cout << "step_seconds=" << step_s << "\n";
    std::cout << "fixture_pairs_per_sat=" << fixture_pairs_per_sat << "\n";
    std::cout << "fixture_pairs_injected=" << fixture_pairs_injected << "\n";
    std::cout << "fixture_rel_speed_km_s=" << fixture_rel_speed_km_s << "\n";
    std::cout << "fixture_offset_km=" << fixture_offset_km << "\n";
    std::cout << "fixture_offset_jitter_km=" << fixture_offset_jitter_km << "\n";
    std::cout << "narrow_tca_guard_km=" << cfg.narrow_phase.tca_guard_km << "\n";
    std::cout << "narrow_refine_band_km=" << cfg.narrow_phase.refine_band_km << "\n";
    std::cout << "narrow_full_refine_band_km=" << cfg.narrow_phase.full_refine_band_km << "\n";
    std::cout << "narrow_high_rel_speed_km_s=" << cfg.narrow_phase.high_rel_speed_km_s << "\n";
    std::cout << "narrow_high_rel_speed_extra_band_km=" << cfg.narrow_phase.high_rel_speed_extra_band_km << "\n";
    std::cout << "narrow_full_refine_budget_base=" << cfg.narrow_phase.full_refine_budget_base << "\n";
    std::cout << "narrow_full_refine_budget_min=" << cfg.narrow_phase.full_refine_budget_min << "\n";
    std::cout << "narrow_full_refine_budget_max=" << cfg.narrow_phase.full_refine_budget_max << "\n";
    std::cout << "narrow_full_refine_samples=" << cfg.narrow_phase.full_refine_samples << "\n";
    std::cout << "narrow_full_refine_substep_s=" << cfg.narrow_phase.full_refine_substep_s << "\n";
    std::cout << "narrow_micro_refine_max_step_s=" << cfg.narrow_phase.micro_refine_max_step_s << "\n";
    std::cout << "narrow_moid_shadow=" << (cfg.narrow_phase.moid_shadow ? 1 : 0) << "\n";
    std::cout << "narrow_moid_filter=" << (cfg.narrow_phase.moid_filter ? 1 : 0) << "\n";
    std::cout << "narrow_moid_samples=" << cfg.narrow_phase.moid_samples << "\n";
    std::cout << "narrow_moid_reject_threshold_km=" << cfg.narrow_phase.moid_reject_threshold_km << "\n";
    std::cout << "narrow_moid_max_e=" << cfg.narrow_phase.moid_max_e << "\n";
    std::cout << "narrow_pairs_checked_total=" << narrow_pairs_total << "\n";
    std::cout << "narrow_uncertainty_promoted_pairs_total=" << uncertainty_promoted_total << "\n";
    std::cout << "narrow_full_refined_pairs_total=" << full_refined_total << "\n";
    std::cout << "narrow_refine_fail_open_total=" << refine_fail_open_total << "\n";
    std::cout << "narrow_full_refine_fail_open_total=" << full_refine_fail_open_total << "\n";
    std::cout << "narrow_full_refine_budget_allocated_total=" << full_budget_alloc_total << "\n";
    std::cout << "narrow_full_refine_budget_exhausted_total=" << full_budget_exhausted_total << "\n";
    std::cout << "narrow_plane_phase_evaluated_pairs_total=" << plane_phase_evaluated_total << "\n";
    std::cout << "narrow_plane_phase_shadow_rejected_pairs_total=" << plane_phase_shadow_rejected_total << "\n";
    std::cout << "narrow_plane_phase_hard_rejected_pairs_total=" << plane_phase_hard_rejected_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_pairs_total=" << plane_phase_fail_open_total << "\n";
    std::cout << "narrow_plane_phase_reject_reason_plane_angle_total=" << plane_phase_reject_reason_plane_angle_total << "\n";
    std::cout << "narrow_plane_phase_reject_reason_phase_angle_total=" << plane_phase_reject_reason_phase_angle_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_reason_elements_invalid_total=" << plane_phase_fail_open_reason_elements_invalid_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_reason_eccentricity_guard_total=" << plane_phase_fail_open_reason_eccentricity_guard_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_reason_non_finite_state_total=" << plane_phase_fail_open_reason_non_finite_state_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_reason_angular_momentum_degenerate_total=" << plane_phase_fail_open_reason_angular_momentum_degenerate_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_reason_plane_angle_non_finite_total=" << plane_phase_fail_open_reason_plane_angle_non_finite_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_reason_phase_angle_non_finite_total=" << plane_phase_fail_open_reason_phase_angle_non_finite_total << "\n";
    std::cout << "narrow_plane_phase_fail_open_reason_uncertainty_override_total=" << plane_phase_fail_open_reason_uncertainty_override_total << "\n";
    std::cout << "narrow_moid_evaluated_pairs_total=" << moid_evaluated_total << "\n";
    std::cout << "narrow_moid_shadow_rejected_pairs_total=" << moid_shadow_rejected_total << "\n";
    std::cout << "narrow_moid_hard_rejected_pairs_total=" << moid_hard_rejected_total << "\n";
    std::cout << "narrow_moid_fail_open_pairs_total=" << moid_fail_open_total << "\n";
    std::cout << "narrow_moid_reject_reason_distance_threshold_total=" << moid_reject_reason_distance_threshold_total << "\n";
    std::cout << "narrow_moid_fail_open_reason_elements_invalid_total=" << moid_fail_open_reason_elements_invalid_total << "\n";
    std::cout << "narrow_moid_fail_open_reason_eccentricity_guard_total=" << moid_fail_open_reason_eccentricity_guard_total << "\n";
    std::cout << "narrow_moid_fail_open_reason_non_finite_state_total=" << moid_fail_open_reason_non_finite_state_total << "\n";
    std::cout << "narrow_moid_fail_open_reason_sampling_failure_total=" << moid_fail_open_reason_sampling_failure_total << "\n";
    std::cout << "narrow_moid_fail_open_reason_uncertainty_override_total=" << moid_fail_open_reason_uncertainty_override_total << "\n";
    std::cout << "narrow_refine_fail_open_reason_rk4_failure_total=" << refine_fail_open_reason_rk4_failure_total << "\n";
    std::cout << "narrow_full_refine_fail_open_reason_rk4_failure_total=" << full_refine_fail_open_reason_rk4_failure_total << "\n";
    std::cout << "narrow_full_refine_fail_open_reason_budget_exhausted_total=" << full_refine_fail_open_reason_budget_exhausted_total << "\n";
    std::cout << "collisions_detected_total=" << collisions_total << "\n";
    std::cout << "narrow_phase_calibration_probe_result=PASS\n";
    return 0;
}
