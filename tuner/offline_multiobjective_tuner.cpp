// ---------------------------------------------------------------------------
// offline_multiobjective_tuner.cpp
//
// Minimal offline multi-objective tuner scaffold (separate from runtime path).
// Objectives (minimize):
//   1) risk_proxy      = dcriterion_rejected
//   2) compute_proxy   = pairs_after_band_index
//   3) candidate_proxy = candidates.size()
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "orbit_math.hpp"
#include "broad_phase.hpp"

#include <algorithm>
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

struct Candidate {
    cascade::BroadPhaseConfig cfg{};
    std::uint64_t risk_proxy = 0;
    std::uint64_t compute_proxy = 0;
    std::uint64_t candidate_proxy = 0;
    std::uint64_t shell_overlap_pass = 0;
};

bool dominates(const Candidate& a, const Candidate& b) {
    const bool no_worse =
        (a.risk_proxy <= b.risk_proxy)
        && (a.compute_proxy <= b.compute_proxy)
        && (a.candidate_proxy <= b.candidate_proxy);

    const bool strictly_better =
        (a.risk_proxy < b.risk_proxy)
        || (a.compute_proxy < b.compute_proxy)
        || (a.candidate_proxy < b.candidate_proxy);

    return no_worse && strictly_better;
}

void seed_store(cascade::StateStore& store,
                cascade::SimClock& clock,
                int sat_count,
                int deb_count)
{
    std::mt19937_64 rng(20260317ULL);

    std::uniform_real_distribution<double> sat_a(6778.0, 7378.0);
    std::uniform_real_distribution<double> sat_e(0.0, 0.02);
    std::uniform_real_distribution<double> sat_i(rad(30.0), rad(99.0));

    std::uniform_real_distribution<double> deb_a(6678.0, 7800.0);
    std::uniform_real_distribution<double> deb_e(0.0, 0.12);
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
        if (!cascade::elements_to_eci(el, r, v)) return;

        std::string id = (type == cascade::ObjectType::SATELLITE)
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

} // namespace

int main(int argc, char** argv)
{
    int samples = 200;
    int sat_count = 50;
    int deb_count = 10000;

    if (argc >= 2) samples = std::max(20, std::atoi(argv[1]));
    if (argc >= 3) sat_count = std::max(1, std::atoi(argv[2]));
    if (argc >= 4) deb_count = std::max(1, std::atoi(argv[3]));

    cascade::StateStore store(static_cast<std::size_t>(sat_count + deb_count + 128));
    cascade::SimClock clock;
    clock.set_epoch_s(1773292800.0);
    seed_store(store, clock, sat_count, deb_count);

    std::mt19937_64 rng(20260318ULL);
    std::uniform_real_distribution<double> shell_margin_dist(20.0, 100.0);
    std::uniform_real_distribution<double> invalid_pad_dist(100.0, 400.0);
    std::uniform_real_distribution<double> a_bin_dist(300.0, 900.0);
    std::uniform_real_distribution<double> i_bin_deg_dist(10.0, 30.0);
    std::uniform_int_distribution<int> neighbor_bins_dist(1, 3);
    std::uniform_real_distribution<double> high_e_dist(0.1, 0.35);
    std::uniform_real_distribution<double> dcrit_dist(1.2, 3.0);

    std::vector<Candidate> population;
    population.reserve(static_cast<std::size_t>(samples));

    for (int s = 0; s < samples; ++s) {
        Candidate c{};
        c.cfg.shell_margin_km = shell_margin_dist(rng);
        c.cfg.invalid_shell_pad_km = invalid_pad_dist(rng);
        c.cfg.a_bin_width_km = a_bin_dist(rng);
        c.cfg.i_bin_width_rad = rad(i_bin_deg_dist(rng));
        c.cfg.band_neighbor_bins = neighbor_bins_dist(rng);
        c.cfg.high_e_fail_open = high_e_dist(rng);
        c.cfg.enable_dcriterion = true;
        c.cfg.dcriterion_threshold = dcrit_dist(rng);

        const cascade::BroadPhaseResult result = cascade::generate_broad_phase_candidates(store, c.cfg);

        c.risk_proxy = result.dcriterion_rejected;
        c.compute_proxy = result.pairs_after_band_index;
        c.candidate_proxy = static_cast<std::uint64_t>(result.candidates.size());
        c.shell_overlap_pass = result.shell_overlap_pass;

        population.push_back(c);
    }

    std::vector<Candidate> pareto;
    pareto.reserve(population.size());

    for (std::size_t i = 0; i < population.size(); ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < population.size(); ++j) {
            if (i == j) continue;
            if (dominates(population[j], population[i])) {
                dominated = true;
                break;
            }
        }
        if (!dominated) pareto.push_back(population[i]);
    }

    std::sort(pareto.begin(), pareto.end(), [](const Candidate& a, const Candidate& b) {
        if (a.risk_proxy != b.risk_proxy) return a.risk_proxy < b.risk_proxy;
        if (a.compute_proxy != b.compute_proxy) return a.compute_proxy < b.compute_proxy;
        return a.candidate_proxy < b.candidate_proxy;
    });

    const std::size_t report_n = std::min<std::size_t>(20, pareto.size());

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "CASCADE Offline Broad-Phase Tuner\n";
    std::cout << "samples=" << samples << "\n";
    std::cout << "satellites=" << sat_count << "\n";
    std::cout << "debris=" << deb_count << "\n";
    std::cout << "pareto_size=" << pareto.size() << "\n";

    for (std::size_t i = 0; i < report_n; ++i) {
        const Candidate& c = pareto[i];
        std::cout << "candidate[" << i << "] "
                  << "risk_proxy=" << c.risk_proxy << ' '
                  << "compute_proxy=" << c.compute_proxy << ' '
                  << "candidate_proxy=" << c.candidate_proxy << ' '
                  << "shell_margin_km=" << c.cfg.shell_margin_km << ' '
                  << "invalid_shell_pad_km=" << c.cfg.invalid_shell_pad_km << ' '
                  << "a_bin_width_km=" << c.cfg.a_bin_width_km << ' '
                  << "i_bin_width_deg=" << (c.cfg.i_bin_width_rad * 180.0 / cascade::PI) << ' '
                  << "band_neighbor_bins=" << c.cfg.band_neighbor_bins << ' '
                  << "high_e_fail_open=" << c.cfg.high_e_fail_open << ' '
                  << "dcriterion_threshold=" << c.cfg.dcriterion_threshold << '\n';
    }

    return 0;
}
