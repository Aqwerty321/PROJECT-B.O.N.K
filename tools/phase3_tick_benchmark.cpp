// ---------------------------------------------------------------------------
// phase3_tick_benchmark.cpp
//
// Synthetic Phase 3 benchmark:
//   - populates StateStore with satellites + debris
//   - runs simulation ticks via run_simulation_step()
//   - reports latency and adaptive mode mix
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "orbit_math.hpp"
#include "simulation_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

double rad(double deg) {
    return deg * cascade::PI / 180.0;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double p95(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t idx = static_cast<std::size_t>(0.95 * static_cast<double>(v.size() - 1));
    return v[idx];
}

} // namespace

int main(int argc, char** argv)
{
    int sat_count = 50;
    int deb_count = 10000;
    int warmup_ticks = 10;
    int measure_ticks = 40;
    double step_seconds = 30.0;

    if (argc >= 2) sat_count = std::max(1, std::atoi(argv[1]));
    if (argc >= 3) deb_count = std::max(1, std::atoi(argv[2]));
    if (argc >= 4) warmup_ticks = std::max(0, std::atoi(argv[3]));
    if (argc >= 5) measure_ticks = std::max(1, std::atoi(argv[4]));
    if (argc >= 6) step_seconds = std::max(1.0, std::atof(argv[5]));

    const int total_objects = sat_count + deb_count;

    cascade::StateStore store(static_cast<std::size_t>(total_objects + 128));
    cascade::SimClock clock;
    clock.set_epoch_s(1773292800.0); // 2026-03-12T08:00:00Z

    std::mt19937_64 rng(20260317ULL);

    std::uniform_real_distribution<double> sat_a(6778.0, 7378.0);
    std::uniform_real_distribution<double> sat_e(0.0, 0.01);
    std::uniform_real_distribution<double> sat_i(rad(30.0), rad(99.0));

    std::uniform_real_distribution<double> deb_a(6678.0, 7800.0);
    std::uniform_real_distribution<double> deb_e(0.0, 0.06);
    std::uniform_real_distribution<double> deb_i(rad(0.0), rad(120.0));

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

        std::string id;
        if (type == cascade::ObjectType::SATELLITE) {
            id = "SAT-" + std::to_string(idx);
        } else {
            id = "DEB-" + std::to_string(idx);
        }

        bool conflict = false;
        store.upsert(id,
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
            store.set_telemetry_epoch_s(obj_idx, clock.epoch_s());
            store.set_elements(obj_idx, el, true);
        }
    };

    for (int i = 0; i < sat_count; ++i) {
        insert_object(i, cascade::ObjectType::SATELLITE);
    }
    for (int i = 0; i < deb_count; ++i) {
        insert_object(i, cascade::ObjectType::DEBRIS);
    }

    std::vector<double> tick_ms;
    tick_ms.reserve(static_cast<std::size_t>(measure_ticks));

    std::uint64_t sum_fast = 0;
    std::uint64_t sum_rk4 = 0;
    std::uint64_t sum_esc = 0;
    std::uint64_t sum_failed = 0;
    std::uint64_t sum_broad_pairs = 0;
    std::uint64_t sum_broad_candidates = 0;
    std::uint64_t sum_broad_overlap = 0;

    const int total_ticks = warmup_ticks + measure_ticks;
    for (int t = 0; t < total_ticks; ++t) {
        cascade::StepRunStats stats{};

        auto t0 = std::chrono::steady_clock::now();
        const bool ok = cascade::run_simulation_step(store, clock, step_seconds, stats);
        auto t1 = std::chrono::steady_clock::now();
        if (!ok) {
            std::cerr << "run_simulation_step failed\n";
            return 1;
        }

        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (t >= warmup_ticks) {
            tick_ms.push_back(ms);
            sum_fast += stats.used_fast;
            sum_rk4 += stats.used_rk4;
            sum_esc += stats.escalated_after_probe;
            sum_failed += stats.failed_objects;
            sum_broad_pairs += stats.broad_pairs_considered;
            sum_broad_candidates += stats.broad_candidates;
            sum_broad_overlap += stats.broad_shell_overlap_pass;
        }
    }

    double mean_ms = 0.0;
    for (double ms : tick_ms) mean_ms += ms;
    if (!tick_ms.empty()) mean_ms /= static_cast<double>(tick_ms.size());

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "CASCADE Phase3 Tick Benchmark\n";
    std::cout << "objects_total=" << store.size() << "\n";
    std::cout << "satellites=" << store.satellite_count() << "\n";
    std::cout << "debris=" << store.debris_count() << "\n";
    std::cout << "step_seconds=" << step_seconds << "\n";
    std::cout << "warmup_ticks=" << warmup_ticks << "\n";
    std::cout << "measure_ticks=" << measure_ticks << "\n";
    std::cout << "tick_ms_mean=" << mean_ms << "\n";
    std::cout << "tick_ms_median=" << median(tick_ms) << "\n";
    std::cout << "tick_ms_p95=" << p95(tick_ms) << "\n";
    std::cout << "adaptive_fast_total=" << sum_fast << "\n";
    std::cout << "adaptive_rk4_total=" << sum_rk4 << "\n";
    std::cout << "escalated_after_probe_total=" << sum_esc << "\n";
    std::cout << "failed_objects_total=" << sum_failed << "\n";
    std::cout << "broad_pairs_considered_total=" << sum_broad_pairs << "\n";
    std::cout << "broad_candidates_total=" << sum_broad_candidates << "\n";
    std::cout << "broad_shell_overlap_pass_total=" << sum_broad_overlap << "\n";

    return 0;
}
