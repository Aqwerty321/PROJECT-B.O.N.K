// ---------------------------------------------------------------------------
// narrow_phase_false_negative_gate.cpp
//
// Regression gate: production narrow-phase must not miss conjunctions found
// by a denser RK4 reference sweep over the same step window.
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "orbit_math.hpp"
#include "propagator.hpp"
#include "simulation_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

double rad(double deg)
{
    return deg * cascade::PI / 180.0;
}

double norm2(const cascade::Vec3& v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

double min_distance_km_dense_reference(const cascade::Vec3& sat_r0,
                                       const cascade::Vec3& sat_v0,
                                       const cascade::Vec3& deb_r0,
                                       const cascade::Vec3& deb_v0,
                                       double step_seconds,
                                       bool& ok)
{
    ok = true;
    constexpr int k_samples = 80;
    constexpr double k_substep_s = 2.0;

    cascade::Vec3 rs = sat_r0;
    cascade::Vec3 vs = sat_v0;
    cascade::Vec3 rd = deb_r0;
    cascade::Vec3 vd = deb_v0;

    cascade::Vec3 d0{rs.x - rd.x, rs.y - rd.y, rs.z - rd.z};
    double min_d2 = norm2(d0);

    const double dt = step_seconds / static_cast<double>(k_samples);
    for (int s = 0; s < k_samples; ++s) {
        if (!cascade::propagate_rk4_j2_substep(rs, vs, dt, k_substep_s)
            || !cascade::propagate_rk4_j2_substep(rd, vd, dt, k_substep_s)) {
            ok = false;
            return 0.0;
        }
        cascade::Vec3 d{rs.x - rd.x, rs.y - rd.y, rs.z - rd.z};
        const double d2 = norm2(d);
        if (d2 < min_d2) {
            min_d2 = d2;
        }
    }

    return std::sqrt(min_d2);
}

void seed_store(cascade::StateStore& store,
                cascade::SimClock& clock,
                int sat_count,
                int deb_count,
                std::uint64_t seed)
{
    std::mt19937_64 rng(seed);

    std::uniform_real_distribution<double> sat_a(6778.0, 7378.0);
    std::uniform_real_distribution<double> sat_e(0.0, 0.01);
    std::uniform_real_distribution<double> sat_i(rad(20.0), rad(99.0));

    std::uniform_real_distribution<double> deb_a(6678.0, 7800.0);
    std::uniform_real_distribution<double> deb_e(0.0, 0.08);
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

        const std::string id = (type == cascade::ObjectType::SATELLITE)
            ? ("SAT-" + std::to_string(idx))
            : ("DEB-" + std::to_string(idx));

        bool conflict = false;
        (void)store.upsert(
            id,
            type,
            r.x,
            r.y,
            r.z,
            v.x,
            v.y,
            v.z,
            clock.epoch_s(),
            conflict
        );

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

    // Inject deterministic near-collision pairs at t0 to ensure reference
    // coverage includes conjunction-positive cases.
    const int inject_pairs = std::min(sat_count, std::min(deb_count, 3));
    for (int k = 0; k < inject_pairs; ++k) {
        const std::string sat_id = "SAT-" + std::to_string(k);
        const std::string deb_id = "DEB-" + std::to_string(k);

        const std::size_t sat_idx = store.find(sat_id);
        const std::size_t deb_idx = store.find(deb_id);
        if (sat_idx >= store.size() || deb_idx >= store.size()) {
            continue;
        }

        const double offset_km = 0.02 + 0.01 * static_cast<double>(k);
        store.rx_mut(deb_idx) = store.rx(sat_idx) + offset_km;
        store.ry_mut(deb_idx) = store.ry(sat_idx);
        store.rz_mut(deb_idx) = store.rz(sat_idx);
        store.vx_mut(deb_idx) = store.vx(sat_idx);
        store.vy_mut(deb_idx) = store.vy(sat_idx);
        store.vz_mut(deb_idx) = store.vz(sat_idx);
        store.set_elements(deb_idx, cascade::OrbitalElements{}, false);
    }
}

struct ScenarioOutcome {
    bool ok = false;
    std::uint64_t reference_collision_sats = 0;
    std::uint64_t production_collision_sats = 0;
    std::uint64_t false_negative_sats = 0;
};

ScenarioOutcome run_scenario(int scenario_id,
                             int sat_count,
                             int deb_count,
                             double step_seconds)
{
    ScenarioOutcome out;

    cascade::StateStore store_ref(static_cast<std::size_t>(sat_count + deb_count + 64));
    cascade::SimClock clock_ref;
    clock_ref.set_epoch_s(1773292800.0 + static_cast<double>(scenario_id) * 300.0);
    seed_store(store_ref, clock_ref, sat_count, deb_count, 2026031800ULL + static_cast<std::uint64_t>(scenario_id));

    cascade::StateStore store_prod = store_ref;
    cascade::SimClock clock_prod = clock_ref;

    cascade::StepRunConfig cfg{};
    cfg.broad_phase.enable_dcriterion = false;

    cascade::StepRunStats stats{};
    if (!cascade::run_simulation_step(store_prod, clock_prod, step_seconds, stats, cfg)) {
        return out;
    }

    std::unordered_set<std::string> production_collision_sat_ids;
    for (std::uint32_t sat_idx_u32 : stats.collision_sat_indices) {
        const std::size_t sat_idx = static_cast<std::size_t>(sat_idx_u32);
        if (sat_idx >= store_prod.size()) continue;
        if (store_prod.type(sat_idx) != cascade::ObjectType::SATELLITE) continue;
        production_collision_sat_ids.insert(store_prod.id(sat_idx));
    }

    std::unordered_set<std::string> reference_collision_sat_ids;
    for (std::size_t i = 0; i < store_ref.size(); ++i) {
        if (store_ref.type(i) != cascade::ObjectType::SATELLITE) continue;

        bool sat_reference_collision = false;
        const cascade::Vec3 sat_r0{store_ref.rx(i), store_ref.ry(i), store_ref.rz(i)};
        const cascade::Vec3 sat_v0{store_ref.vx(i), store_ref.vy(i), store_ref.vz(i)};

        for (std::size_t j = 0; j < store_ref.size(); ++j) {
            if (store_ref.type(j) != cascade::ObjectType::DEBRIS) continue;

            const cascade::Vec3 deb_r0{store_ref.rx(j), store_ref.ry(j), store_ref.rz(j)};
            const cascade::Vec3 deb_v0{store_ref.vx(j), store_ref.vy(j), store_ref.vz(j)};

            bool ok = true;
            const double min_d_km = min_distance_km_dense_reference(
                sat_r0,
                sat_v0,
                deb_r0,
                deb_v0,
                step_seconds,
                ok
            );
            if (!ok) {
                // Fail-open in reference harness: if dense propagation fails,
                // treat as collision-positive for safety.
                sat_reference_collision = true;
                break;
            }

            if (min_d_km <= cascade::COLLISION_THRESHOLD_KM + 1e-12) {
                sat_reference_collision = true;
                break;
            }
        }

        if (sat_reference_collision) {
            reference_collision_sat_ids.insert(store_ref.id(i));
        }
    }

    out.reference_collision_sats = reference_collision_sat_ids.size();
    out.production_collision_sats = production_collision_sat_ids.size();

    std::uint64_t fn = 0;
    for (const std::string& sat_id : reference_collision_sat_ids) {
        if (production_collision_sat_ids.find(sat_id) == production_collision_sat_ids.end()) {
            ++fn;
        }
    }
    out.false_negative_sats = fn;
    out.ok = true;
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    int scenarios = 6;
    int sat_count = 6;
    int deb_count = 80;

    if (argc >= 2) scenarios = std::max(1, std::atoi(argv[1]));
    if (argc >= 3) sat_count = std::max(1, std::atoi(argv[2]));
    if (argc >= 4) deb_count = std::max(1, std::atoi(argv[3]));

    const std::vector<double> steps{30.0, 120.0, 600.0, 1200.0};

    std::uint64_t total_reference = 0;
    std::uint64_t total_production = 0;
    std::uint64_t total_false_negatives = 0;
    bool all_ok = true;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "narrow_phase_false_negative_gate\n";
    std::cout << "scenarios=" << scenarios << "\n";
    std::cout << "satellites=" << sat_count << "\n";
    std::cout << "debris=" << deb_count << "\n";

    for (int s = 0; s < scenarios; ++s) {
        const double step_seconds = steps[static_cast<std::size_t>(s) % steps.size()];
        const ScenarioOutcome outcome = run_scenario(s, sat_count, deb_count, step_seconds);

        if (!outcome.ok) {
            all_ok = false;
            std::cout << "scenario_" << s << "_result=FAIL\n";
            std::cout << "scenario_" << s << "_reason=execution_failed\n";
            continue;
        }

        total_reference += outcome.reference_collision_sats;
        total_production += outcome.production_collision_sats;
        total_false_negatives += outcome.false_negative_sats;

        std::cout << "scenario_" << s << "_step_seconds=" << step_seconds << "\n";
        std::cout << "scenario_" << s << "_reference_collision_sats=" << outcome.reference_collision_sats << "\n";
        std::cout << "scenario_" << s << "_production_collision_sats=" << outcome.production_collision_sats << "\n";
        std::cout << "scenario_" << s << "_false_negative_sats=" << outcome.false_negative_sats << "\n";
        std::cout << "scenario_" << s << "_result=" << (outcome.false_negative_sats == 0 ? "PASS" : "FAIL") << "\n";
    }

    std::cout << "reference_collision_sats_total=" << total_reference << "\n";
    std::cout << "production_collision_sats_total=" << total_production << "\n";
    std::cout << "false_negative_sats_total=" << total_false_negatives << "\n";

    if (!all_ok) {
        std::cout << "narrow_phase_false_negative_gate_result=FAIL\n";
        return 1;
    }

    if (total_reference == 0) {
        std::cout << "narrow_phase_false_negative_gate_result=FAIL\n";
        std::cout << "reason=no_reference_collisions_observed\n";
        return 1;
    }

    if (total_false_negatives != 0) {
        std::cout << "narrow_phase_false_negative_gate_result=FAIL\n";
        return 1;
    }

    std::cout << "narrow_phase_false_negative_gate_result=PASS\n";
    return 0;
}
