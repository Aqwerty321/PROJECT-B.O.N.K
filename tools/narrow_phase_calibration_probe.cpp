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

void print_usage(const char* argv0)
{
    std::cout
        << "usage: " << argv0
        << " [satellites] [debris] [ticks] [step_s]"
        << " [high_rel_speed_km_s] [high_rel_speed_extra_band_km]"
        << " [full_refine_budget_base] [full_refine_budget_min] [full_refine_budget_max]"
        << " [full_refine_band_km] [refine_band_km] [tca_guard_km]"
        << " [full_refine_samples] [full_refine_substep_s] [micro_refine_max_step_s]"
        << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    int sat_count = 50;
    int deb_count = 10000;
    int ticks = 5;
    double step_s = 30.0;

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

    if (cfg.narrow_phase.full_refine_budget_max < cfg.narrow_phase.full_refine_budget_min) {
        cfg.narrow_phase.full_refine_budget_max = cfg.narrow_phase.full_refine_budget_min;
    }

    cascade::StateStore store(static_cast<std::size_t>(sat_count + deb_count + 128));
    cascade::SimClock clock;
    clock.set_epoch_s(1773292800.0);
    seed_store(store, clock, sat_count, deb_count, 2026031901ULL);

    std::uint64_t narrow_pairs_total = 0;
    std::uint64_t uncertainty_promoted_total = 0;
    std::uint64_t full_refined_total = 0;
    std::uint64_t full_budget_alloc_total = 0;
    std::uint64_t full_budget_exhausted_total = 0;
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
        full_budget_alloc_total += stats.narrow_full_refine_budget_allocated;
        full_budget_exhausted_total += stats.narrow_full_refine_budget_exhausted;
        collisions_total += stats.collisions_detected;
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "narrow_phase_calibration_probe\n";
    std::cout << "satellites=" << sat_count << "\n";
    std::cout << "debris=" << deb_count << "\n";
    std::cout << "ticks=" << ticks << "\n";
    std::cout << "step_seconds=" << step_s << "\n";
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
    std::cout << "narrow_pairs_checked_total=" << narrow_pairs_total << "\n";
    std::cout << "narrow_uncertainty_promoted_pairs_total=" << uncertainty_promoted_total << "\n";
    std::cout << "narrow_full_refined_pairs_total=" << full_refined_total << "\n";
    std::cout << "narrow_full_refine_budget_allocated_total=" << full_budget_alloc_total << "\n";
    std::cout << "narrow_full_refine_budget_exhausted_total=" << full_budget_exhausted_total << "\n";
    std::cout << "collisions_detected_total=" << collisions_total << "\n";
    std::cout << "narrow_phase_calibration_probe_result=PASS\n";
    return 0;
}
