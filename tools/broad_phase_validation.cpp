// ---------------------------------------------------------------------------
// broad_phase_validation.cpp
//
// Validates conservative broad-phase behavior against shell-only baseline.
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "orbit_math.hpp"
#include "broad_phase.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

double rad(double deg) {
    return deg * cascade::PI / 180.0;
}

double radius_from_xyz(double x, double y, double z) {
    return std::sqrt(x * x + y * y + z * z);
}

void shell_bounds(const cascade::StateStore& store,
                  std::size_t idx,
                  const cascade::BroadPhaseConfig& cfg,
                  double& out_min,
                  double& out_max)
{
    if (store.elements_valid(idx)) {
        out_min = store.rp_km(idx) - cfg.shell_margin_km;
        out_max = store.ra_km(idx) + cfg.shell_margin_km;
        return;
    }
    const double r = radius_from_xyz(store.rx(idx), store.ry(idx), store.rz(idx));
    out_min = r - cfg.invalid_shell_pad_km - cfg.shell_margin_km;
    out_max = r + cfg.invalid_shell_pad_km + cfg.shell_margin_km;
}

bool overlap(double a_min, double a_max, double b_min, double b_max) {
    return !(a_max < b_min || b_max < a_min);
}

std::uint64_t pair_key(std::uint32_t a, std::uint32_t b) {
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
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
    std::uniform_real_distribution<double> sat_i(rad(30.0), rad(99.0));

    std::uniform_real_distribution<double> deb_a(6678.0, 7800.0);
    std::uniform_real_distribution<double> deb_e(0.0, 0.20);
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
        if (!cascade::elements_to_eci(el, r, v)) return;

        const std::string id = (type == cascade::ObjectType::SATELLITE)
            ? ("SAT-" + std::to_string(idx))
            : ("DEB-" + std::to_string(idx));

        bool conflict = false;
        store.upsert(id, type,
                     r.x, r.y, r.z,
                     v.x, v.y, v.z,
                     clock.epoch_s(),
                     conflict);

        const std::size_t obj_idx = store.find(id);
        if (obj_idx < store.size()) {
            store.set_elements(obj_idx, el, true);
            store.set_telemetry_epoch_s(obj_idx, clock.epoch_s());
        }
    };

    for (int i = 0; i < sat_count; ++i) insert_object(i, cascade::ObjectType::SATELLITE);
    for (int i = 0; i < deb_count; ++i) insert_object(i, cascade::ObjectType::DEBRIS);
}

std::unordered_set<std::uint64_t> baseline_shell_pairs(const cascade::StateStore& store,
                                                       const cascade::BroadPhaseConfig& cfg)
{
    std::unordered_set<std::uint64_t> out;
    out.reserve(store.satellite_count() * (store.debris_count() / 2 + 1));

    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != cascade::ObjectType::SATELLITE) continue;

        double sat_min = 0.0, sat_max = 0.0;
        shell_bounds(store, i, cfg, sat_min, sat_max);

        for (std::size_t j = 0; j < store.size(); ++j) {
            if (store.type(j) != cascade::ObjectType::DEBRIS) continue;
            double obj_min = 0.0, obj_max = 0.0;
            shell_bounds(store, j, cfg, obj_min, obj_max);
            if (!overlap(sat_min, sat_max, obj_min, obj_max)) continue;
            out.insert(pair_key(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j)));
        }
    }

    return out;
}

} // namespace

int main(int argc, char** argv)
{
    int scenarios = 3;
    int sat_count = 50;
    int deb_count = 10000;
    bool strict_dcriterion_zero = true;

    if (argc >= 2) scenarios = std::max(1, std::atoi(argv[1]));
    if (argc >= 3) sat_count = std::max(1, std::atoi(argv[2]));
    if (argc >= 4) deb_count = std::max(1, std::atoi(argv[3]));
    if (argc >= 5) strict_dcriterion_zero = (std::atoi(argv[4]) != 0);

    cascade::BroadPhaseConfig cfg{};

    std::uint64_t total_baseline = 0;
    std::uint64_t total_indexed = 0;
    std::uint64_t total_missing = 0;
    std::uint64_t total_shadow_reject = 0;
    std::uint64_t total_dreject = 0;

    for (int s = 0; s < scenarios; ++s) {
        cascade::StateStore store(static_cast<std::size_t>(sat_count + deb_count + 128));
        cascade::SimClock clock;
        clock.set_epoch_s(1773292800.0);
        seed_store(store, clock, sat_count, deb_count, 2026031900ULL + static_cast<std::uint64_t>(s));

        auto baseline = baseline_shell_pairs(store, cfg);

        cascade::BroadPhaseConfig indexed_cfg = cfg;
        indexed_cfg.enable_dcriterion = false;
        const cascade::BroadPhaseResult indexed = cascade::generate_broad_phase_candidates(store, indexed_cfg);

        std::unordered_set<std::uint64_t> indexed_set;
        indexed_set.reserve(indexed.candidates.size() + 16);
        for (const auto& p : indexed.candidates) {
            indexed_set.insert(pair_key(p.sat_idx, p.obj_idx));
        }

        std::uint64_t missing = 0;
        for (std::uint64_t k : baseline) {
            if (indexed_set.find(k) == indexed_set.end()) ++missing;
        }

        cascade::BroadPhaseConfig d_cfg = cfg;
        d_cfg.enable_dcriterion = true;
        const cascade::BroadPhaseResult d_result = cascade::generate_broad_phase_candidates(store, d_cfg);

        total_baseline += static_cast<std::uint64_t>(baseline.size());
        total_indexed += static_cast<std::uint64_t>(indexed.candidates.size());
        total_missing += missing;
        total_shadow_reject += indexed.dcriterion_shadow_rejected;
        total_dreject += d_result.dcriterion_rejected;
    }

    std::cout << "CASCADE Broad-Phase Validation\n";
    std::cout << "scenarios=" << scenarios << "\n";
    std::cout << "satellites=" << sat_count << "\n";
    std::cout << "debris=" << deb_count << "\n";
    std::cout << "baseline_shell_pairs_total=" << total_baseline << "\n";
    std::cout << "indexed_pairs_total=" << total_indexed << "\n";
    std::cout << "missing_vs_shell_baseline_total=" << total_missing << "\n";
    std::cout << "dcriterion_shadow_rejected_total=" << total_shadow_reject << "\n";
    std::cout << "dcriterion_rejected_total=" << total_dreject << "\n";
    std::cout << "strict_dcriterion_zero=" << (strict_dcriterion_zero ? "true" : "false") << "\n";

    if (total_missing > 0) {
        std::cerr << "ERROR: indexed broad-phase missed shell-baseline pairs\n";
        return 3;
    }
    if (strict_dcriterion_zero && total_dreject > 0) {
        std::cerr << "ERROR: strict mode requires zero D-criterion rejections\n";
        return 4;
    }

    std::cout << "broad_phase_validation=PASS\n";
    return 0;
}
