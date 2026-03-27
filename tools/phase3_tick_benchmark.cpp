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
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

bool env_flag_enabled(const char* key, bool fallback) {
    const char* raw = std::getenv(key);
    if (!raw) {
        return fallback;
    }
    if (raw[0] == '\0') {
        return fallback;
    }
    return std::atoi(raw) != 0;
}

std::uint64_t fnv1a_init() noexcept {
    return 1469598103934665603ULL;
}

void fnv1a_mix_u64(std::uint64_t& state, std::uint64_t value) noexcept {
    state ^= value;
    state *= 1099511628211ULL;
}

void fnv1a_mix_double(std::uint64_t& state, double value) noexcept {
    fnv1a_mix_u64(state, std::bit_cast<std::uint64_t>(value));
}

void fnv1a_mix_string(std::uint64_t& state, const std::string& value) noexcept {
    for (unsigned char ch : value) {
        fnv1a_mix_u64(state, static_cast<std::uint64_t>(ch));
    }
    fnv1a_mix_u64(state, 0xffULL);
}

struct FinalStateSummary {
    std::uint64_t fingerprint = 0;
    std::uint64_t satellite_nominal = 0;
    std::uint64_t satellite_maneuvering = 0;
    std::uint64_t satellite_fuel_low = 0;
    std::uint64_t satellite_offline = 0;
    double fuel_kg_sum = 0.0;
    double mass_kg_sum = 0.0;
    double position_l1_sum_km = 0.0;
    double velocity_l1_sum_km_s = 0.0;
    double telemetry_epoch_sum_s = 0.0;
};

FinalStateSummary summarize_state(const cascade::StateStore& store) noexcept {
    FinalStateSummary out{};
    out.fingerprint = fnv1a_init();

    for (std::size_t idx = 0; idx < store.size(); ++idx) {
        fnv1a_mix_string(out.fingerprint, store.id(idx));
        fnv1a_mix_u64(out.fingerprint, static_cast<std::uint64_t>(store.type(idx)));
        fnv1a_mix_u64(out.fingerprint, static_cast<std::uint64_t>(store.sat_status(idx)));
        fnv1a_mix_u64(out.fingerprint, store.elements_valid(idx) ? 1ULL : 0ULL);

        fnv1a_mix_double(out.fingerprint, store.rx(idx));
        fnv1a_mix_double(out.fingerprint, store.ry(idx));
        fnv1a_mix_double(out.fingerprint, store.rz(idx));
        fnv1a_mix_double(out.fingerprint, store.vx(idx));
        fnv1a_mix_double(out.fingerprint, store.vy(idx));
        fnv1a_mix_double(out.fingerprint, store.vz(idx));
        fnv1a_mix_double(out.fingerprint, store.fuel_kg(idx));
        fnv1a_mix_double(out.fingerprint, store.mass_kg(idx));
        fnv1a_mix_double(out.fingerprint, store.telemetry_epoch_s(idx));
        fnv1a_mix_double(out.fingerprint, store.a_km(idx));
        fnv1a_mix_double(out.fingerprint, store.e(idx));
        fnv1a_mix_double(out.fingerprint, store.i_rad(idx));
        fnv1a_mix_double(out.fingerprint, store.raan_rad(idx));
        fnv1a_mix_double(out.fingerprint, store.argp_rad(idx));
        fnv1a_mix_double(out.fingerprint, store.M_rad(idx));
        fnv1a_mix_double(out.fingerprint, store.n_rad_s(idx));
        fnv1a_mix_double(out.fingerprint, store.p_km(idx));
        fnv1a_mix_double(out.fingerprint, store.rp_km(idx));
        fnv1a_mix_double(out.fingerprint, store.ra_km(idx));

        out.fuel_kg_sum += store.fuel_kg(idx);
        out.mass_kg_sum += store.mass_kg(idx);
        out.position_l1_sum_km +=
            std::abs(store.rx(idx)) + std::abs(store.ry(idx)) + std::abs(store.rz(idx));
        out.velocity_l1_sum_km_s +=
            std::abs(store.vx(idx)) + std::abs(store.vy(idx)) + std::abs(store.vz(idx));
        out.telemetry_epoch_sum_s += store.telemetry_epoch_s(idx);

        if (store.type(idx) == cascade::ObjectType::SATELLITE) {
            switch (store.sat_status(idx)) {
                case cascade::SatStatus::NOMINAL:
                    ++out.satellite_nominal;
                    break;
                case cascade::SatStatus::MANEUVERING:
                    ++out.satellite_maneuvering;
                    break;
                case cascade::SatStatus::FUEL_LOW:
                    ++out.satellite_fuel_low;
                    break;
                case cascade::SatStatus::OFFLINE:
                    ++out.satellite_offline;
                    break;
                default:
                    break;
            }
        }
    }

    return out;
}

} // namespace

int main(int argc, char** argv)
{
    int sat_count = 50;
    int deb_count = 10000;
    int warmup_ticks = 10;
    int measure_ticks = 40;
    double step_seconds = 30.0;
    std::uint64_t seed = 20260317ULL;
    double start_epoch_s = 1773292800.0;

    if (argc >= 2) sat_count = std::max(1, std::atoi(argv[1]));
    if (argc >= 3) deb_count = std::max(1, std::atoi(argv[2]));
    if (argc >= 4) warmup_ticks = std::max(0, std::atoi(argv[3]));
    if (argc >= 5) measure_ticks = std::max(1, std::atoi(argv[4]));
    if (argc >= 6) step_seconds = std::max(1.0, std::atof(argv[5]));
    if (argc >= 7) seed = static_cast<std::uint64_t>(std::strtoull(argv[6], nullptr, 10));
    if (argc >= 8) start_epoch_s = std::atof(argv[7]);

    const int total_objects = sat_count + deb_count;

    cascade::StateStore store(static_cast<std::size_t>(total_objects + 128));
    cascade::SimClock clock;
    clock.set_epoch_s(start_epoch_s);

    std::mt19937_64 rng(seed);

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
    std::uint64_t sum_narrow_pairs = 0;
    std::uint64_t sum_collisions = 0;
    std::uint64_t sum_maneuvers = 0;
    std::uint64_t sum_refined_pairs = 0;
    std::uint64_t sum_refine_cleared = 0;
    std::uint64_t sum_refine_fail_open = 0;
    std::uint64_t sum_full_refined_pairs = 0;
    std::uint64_t sum_full_refine_cleared = 0;
    std::uint64_t sum_full_refine_fail_open = 0;
    std::uint64_t sum_full_refine_budget_allocated = 0;
    std::uint64_t sum_full_refine_budget_exhausted = 0;
    std::uint64_t sum_failed = 0;
    std::uint64_t sum_broad_pairs = 0;
    std::uint64_t sum_broad_candidates = 0;
    std::uint64_t sum_broad_overlap = 0;

    // Per-phase timing accumulators (microseconds)
    double sum_propagation_us = 0.0;
    double sum_broad_phase_us = 0.0;
    double sum_narrow_precomp_us = 0.0;
    double sum_narrow_sweep_us = 0.0;

    cascade::StepRunConfig cfg{};
    cfg.broad_phase.enable_i_neighbor_filter =
        env_flag_enabled("PROJECTBONK_BROAD_I_NEIGHBOR_FILTER", false);

    const int total_ticks = warmup_ticks + measure_ticks;
    for (int t = 0; t < total_ticks; ++t) {
        cascade::StepRunStats stats{};

        auto t0 = std::chrono::steady_clock::now();
        const bool ok = cascade::run_simulation_step(store, clock, step_seconds, stats, cfg);
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
            sum_narrow_pairs += stats.narrow_pairs_checked;
            sum_collisions += stats.collisions_detected;
            sum_maneuvers += stats.maneuvers_executed;
            sum_refined_pairs += stats.narrow_refined_pairs;
            sum_refine_cleared += stats.narrow_refine_cleared;
            sum_refine_fail_open += stats.narrow_refine_fail_open;
            sum_full_refined_pairs += stats.narrow_full_refined_pairs;
            sum_full_refine_cleared += stats.narrow_full_refine_cleared;
            sum_full_refine_fail_open += stats.narrow_full_refine_fail_open;
            sum_full_refine_budget_allocated += stats.narrow_full_refine_budget_allocated;
            sum_full_refine_budget_exhausted += stats.narrow_full_refine_budget_exhausted;
            sum_failed += stats.failed_objects;
            sum_broad_pairs += stats.broad_pairs_considered;
            sum_broad_candidates += stats.broad_candidates;
            sum_broad_overlap += stats.broad_shell_overlap_pass;
            sum_propagation_us += stats.propagation_us;
            sum_broad_phase_us += stats.broad_phase_us;
            sum_narrow_precomp_us += stats.narrow_precomp_us;
            sum_narrow_sweep_us += stats.narrow_sweep_us;
        }
    }

    double mean_ms = 0.0;
    for (double ms : tick_ms) mean_ms += ms;
    if (!tick_ms.empty()) mean_ms /= static_cast<double>(tick_ms.size());

    const FinalStateSummary final_state = summarize_state(store);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "CASCADE Phase3 Tick Benchmark\n";
    std::cout << "objects_total=" << store.size() << "\n";
    std::cout << "satellites=" << store.satellite_count() << "\n";
    std::cout << "debris=" << store.debris_count() << "\n";
    std::cout << "step_seconds=" << step_seconds << "\n";
    std::cout << "rng_seed=" << seed << "\n";
    std::cout << "start_epoch_s=" << start_epoch_s << "\n";
    std::cout << "warmup_ticks=" << warmup_ticks << "\n";
    std::cout << "measure_ticks=" << measure_ticks << "\n";
    std::cout << "broad_i_neighbor_filter="
              << (cfg.broad_phase.enable_i_neighbor_filter ? 1 : 0)
              << "\n";
    std::cout << "tick_ms_mean=" << mean_ms << "\n";
    std::cout << "tick_ms_median=" << median(tick_ms) << "\n";
    std::cout << "tick_ms_p95=" << p95(tick_ms) << "\n";
    std::cout << "adaptive_fast_total=" << sum_fast << "\n";
    std::cout << "adaptive_rk4_total=" << sum_rk4 << "\n";
    std::cout << "escalated_after_probe_total=" << sum_esc << "\n";
    std::cout << "narrow_pairs_checked_total=" << sum_narrow_pairs << "\n";
    std::cout << "collisions_detected_total=" << sum_collisions << "\n";
    std::cout << "maneuvers_executed_total=" << sum_maneuvers << "\n";
    std::cout << "narrow_refined_pairs_total=" << sum_refined_pairs << "\n";
    std::cout << "narrow_refine_cleared_total=" << sum_refine_cleared << "\n";
    std::cout << "narrow_refine_fail_open_total=" << sum_refine_fail_open << "\n";
    std::cout << "narrow_full_refined_pairs_total=" << sum_full_refined_pairs << "\n";
    std::cout << "narrow_full_refine_cleared_total=" << sum_full_refine_cleared << "\n";
    std::cout << "narrow_full_refine_fail_open_total=" << sum_full_refine_fail_open << "\n";
    std::cout << "narrow_full_refine_budget_allocated_total=" << sum_full_refine_budget_allocated << "\n";
    std::cout << "narrow_full_refine_budget_exhausted_total=" << sum_full_refine_budget_exhausted << "\n";
    std::cout << "failed_objects_total=" << sum_failed << "\n";
    std::cout << "broad_pairs_considered_total=" << sum_broad_pairs << "\n";
    std::cout << "broad_candidates_total=" << sum_broad_candidates << "\n";
    std::cout << "broad_shell_overlap_pass_total=" << sum_broad_overlap << "\n";

    // Per-phase timing breakdown (mean per tick, in milliseconds)
    const double n_ticks = static_cast<double>(tick_ms.size());
    if (n_ticks > 0.0) {
        std::cout << "phase_propagation_ms_mean=" << (sum_propagation_us / n_ticks / 1000.0) << "\n";
        std::cout << "phase_broad_ms_mean=" << (sum_broad_phase_us / n_ticks / 1000.0) << "\n";
        std::cout << "phase_narrow_precomp_ms_mean=" << (sum_narrow_precomp_us / n_ticks / 1000.0) << "\n";
        std::cout << "phase_narrow_sweep_ms_mean=" << (sum_narrow_sweep_us / n_ticks / 1000.0) << "\n";
        const double accounted = (sum_propagation_us + sum_broad_phase_us + sum_narrow_precomp_us + sum_narrow_sweep_us) / n_ticks / 1000.0;
        std::cout << "phase_other_ms_mean=" << (mean_ms - accounted) << "\n";
    }

    std::cout << std::hex;
    std::cout << "final_state_fingerprint=0x" << final_state.fingerprint << "\n";
    std::cout << std::dec;
    std::cout << "final_satellite_nominal=" << final_state.satellite_nominal << "\n";
    std::cout << "final_satellite_maneuvering=" << final_state.satellite_maneuvering << "\n";
    std::cout << "final_satellite_fuel_low=" << final_state.satellite_fuel_low << "\n";
    std::cout << "final_satellite_offline=" << final_state.satellite_offline << "\n";
    std::cout << "final_fuel_kg_sum=" << final_state.fuel_kg_sum << "\n";
    std::cout << "final_mass_kg_sum=" << final_state.mass_kg_sum << "\n";
    std::cout << "final_position_l1_sum_km=" << final_state.position_l1_sum_km << "\n";
    std::cout << "final_velocity_l1_sum_km_s=" << final_state.velocity_l1_sum_km_s << "\n";
    std::cout << "final_telemetry_epoch_sum_s=" << final_state.telemetry_epoch_sum_s << "\n";

    return 0;
}
