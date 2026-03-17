// ---------------------------------------------------------------------------
// offline_multiobjective_tuner.cpp
//
// Deterministic offline multi-objective tuner (separate from runtime path).
//
// Hard safety rule:
//   Any candidate with non-zero risk_proxy on train OR eval is disqualified.
//
// Objectives (minimize among safe candidates):
//   1) eval_compute_proxy   = sum(pairs_after_band_index)
//   2) eval_candidate_proxy = sum(candidates.size())
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

struct EvalSummary {
    std::uint64_t risk_proxy = 0;
    std::uint64_t compute_proxy = 0;
    std::uint64_t candidate_proxy = 0;
    std::uint64_t overlap_proxy = 0;
};

struct Candidate {
    cascade::BroadPhaseConfig cfg{};
    EvalSummary train{};
    EvalSummary eval{};
    bool safe = false;
};

bool dominates_safe(const Candidate& a, const Candidate& b) {
    const bool no_worse =
        (a.eval.compute_proxy <= b.eval.compute_proxy)
        && (a.eval.candidate_proxy <= b.eval.candidate_proxy);

    const bool strictly_better =
        (a.eval.compute_proxy < b.eval.compute_proxy)
        || (a.eval.candidate_proxy < b.eval.candidate_proxy);

    return no_worse && strictly_better;
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

EvalSummary evaluate_config(const cascade::BroadPhaseConfig& cfg,
                            const std::vector<cascade::StateStore>& scenarios)
{
    EvalSummary out{};
    for (const auto& store : scenarios) {
        const cascade::BroadPhaseResult r = cascade::generate_broad_phase_candidates(store, cfg);
        out.risk_proxy += r.dcriterion_rejected;
        out.compute_proxy += r.pairs_after_band_index;
        out.candidate_proxy += static_cast<std::uint64_t>(r.candidates.size());
        out.overlap_proxy += r.shell_overlap_pass;
    }
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    int samples = 240;
    int sat_count = 50;
    int deb_count = 10000;
    int train_scenarios = 3;
    int eval_scenarios = 2;

    if (argc >= 2) samples = std::max(40, std::atoi(argv[1]));
    if (argc >= 3) sat_count = std::max(1, std::atoi(argv[2]));
    if (argc >= 4) deb_count = std::max(1, std::atoi(argv[3]));
    if (argc >= 5) train_scenarios = std::max(1, std::atoi(argv[4]));
    if (argc >= 6) eval_scenarios = std::max(1, std::atoi(argv[5]));

    std::vector<cascade::StateStore> train_sets;
    std::vector<cascade::StateStore> eval_sets;
    train_sets.reserve(static_cast<std::size_t>(train_scenarios));
    eval_sets.reserve(static_cast<std::size_t>(eval_scenarios));

    for (int i = 0; i < train_scenarios; ++i) {
        cascade::StateStore s(static_cast<std::size_t>(sat_count + deb_count + 128));
        cascade::SimClock c;
        c.set_epoch_s(1773292800.0);
        seed_store(s, c, sat_count, deb_count, 2026031700ULL + static_cast<std::uint64_t>(i));
        train_sets.push_back(std::move(s));
    }
    for (int i = 0; i < eval_scenarios; ++i) {
        cascade::StateStore s(static_cast<std::size_t>(sat_count + deb_count + 128));
        cascade::SimClock c;
        c.set_epoch_s(1773292800.0);
        seed_store(s, c, sat_count, deb_count, 2026032700ULL + static_cast<std::uint64_t>(i));
        eval_sets.push_back(std::move(s));
    }

    std::mt19937_64 rng(20260318ULL);
    std::uniform_real_distribution<double> shell_margin_dist(30.0, 120.0);
    std::uniform_real_distribution<double> invalid_pad_dist(120.0, 500.0);
    std::uniform_real_distribution<double> a_bin_dist(250.0, 900.0);
    std::uniform_real_distribution<double> i_bin_deg_dist(8.0, 35.0);
    std::uniform_int_distribution<int> neighbor_bins_dist(1, 4);
    std::uniform_real_distribution<double> high_e_dist(0.10, 0.40);
    std::uniform_real_distribution<double> dcrit_dist(1.2, 3.5);

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

        c.train = evaluate_config(c.cfg, train_sets);
        c.eval = evaluate_config(c.cfg, eval_sets);

        c.safe = (c.train.risk_proxy == 0) && (c.eval.risk_proxy == 0);
        population.push_back(c);
    }

    std::vector<Candidate> safe;
    safe.reserve(population.size());
    int disqualified = 0;
    for (const Candidate& c : population) {
        if (c.safe) safe.push_back(c);
        else ++disqualified;
    }

    std::vector<Candidate> pareto;
    pareto.reserve(safe.size());
    for (std::size_t i = 0; i < safe.size(); ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < safe.size(); ++j) {
            if (i == j) continue;
            if (dominates_safe(safe[j], safe[i])) {
                dominated = true;
                break;
            }
        }
        if (!dominated) pareto.push_back(safe[i]);
    }

    std::sort(pareto.begin(), pareto.end(), [](const Candidate& a, const Candidate& b) {
        if (a.eval.compute_proxy != b.eval.compute_proxy) return a.eval.compute_proxy < b.eval.compute_proxy;
        return a.eval.candidate_proxy < b.eval.candidate_proxy;
    });

    const std::size_t report_n = std::min<std::size_t>(20, pareto.size());

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "CASCADE Offline Broad-Phase Tuner\n";
    std::cout << "samples=" << samples << "\n";
    std::cout << "satellites=" << sat_count << "\n";
    std::cout << "debris=" << deb_count << "\n";
    std::cout << "train_scenarios=" << train_scenarios << "\n";
    std::cout << "eval_scenarios=" << eval_scenarios << "\n";
    std::cout << "strict_zero_risk=true\n";
    std::cout << "disqualified_nonzero_risk=" << disqualified << "\n";
    std::cout << "safe_population=" << safe.size() << "\n";
    std::cout << "pareto_size=" << pareto.size() << "\n";

    for (std::size_t i = 0; i < report_n; ++i) {
        const Candidate& c = pareto[i];
        std::cout << "candidate[" << i << "] "
                  << "train_compute_proxy=" << c.train.compute_proxy << ' '
                  << "train_candidate_proxy=" << c.train.candidate_proxy << ' '
                  << "eval_compute_proxy=" << c.eval.compute_proxy << ' '
                  << "eval_candidate_proxy=" << c.eval.candidate_proxy << ' '
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
