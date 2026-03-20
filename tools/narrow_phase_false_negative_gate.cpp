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

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
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

bool env_bool(const char* key, bool default_value)
{
    const char* raw = std::getenv(key);
    if (raw == nullptr) {
        return default_value;
    }
    if (std::string(raw) == "1" || std::string(raw) == "true" || std::string(raw) == "TRUE") {
        return true;
    }
    if (std::string(raw) == "0" || std::string(raw) == "false" || std::string(raw) == "FALSE") {
        return false;
    }
    return default_value;
}

double env_double(const char* key, double default_value)
{
    const char* raw = std::getenv(key);
    if (raw == nullptr) {
        return default_value;
    }
    char* end = nullptr;
    const double parsed = std::strtod(raw, &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return default_value;
    }
    return parsed;
}

std::uint32_t env_u32(const char* key, std::uint32_t default_value, std::uint32_t min_value)
{
    const char* raw = std::getenv(key);
    if (raw == nullptr) {
        return default_value;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(raw, &end, 10);
    if (end == nullptr || *end != '\0') {
        return default_value;
    }
    if (parsed < static_cast<unsigned long>(min_value)) {
        return min_value;
    }
    return static_cast<std::uint32_t>(parsed);
}

cascade::NarrowPhaseConfig::MoidMode env_moid_mode(cascade::NarrowPhaseConfig::MoidMode default_value)
{
    const char* raw = std::getenv("PROJECTBONK_NARROW_MOID_MODE");
    if (raw == nullptr) {
        return default_value;
    }
    const std::string mode(raw);
    if (mode == "hf" || mode == "HF") {
        return cascade::NarrowPhaseConfig::MoidMode::HF;
    }
    if (mode == "proxy" || mode == "PROXY") {
        return cascade::NarrowPhaseConfig::MoidMode::PROXY;
    }
    return default_value;
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
    std::uint64_t uncertainty_promoted_pairs = 0;
    std::uint64_t plane_phase_hard_rejected_pairs = 0;
    std::uint64_t plane_phase_fail_open_pairs = 0;
    std::uint64_t moid_hard_rejected_pairs = 0;
    std::uint64_t moid_fail_open_pairs = 0;
    std::string family;
    int scenario_seed = 0;
    double step_seconds = 0.0;
};

ScenarioOutcome run_scenario(int scenario_id,
                              int sat_count,
                              int deb_count,
                              double step_seconds,
                              std::uint64_t seed,
                              bool high_e_bias,
                              bool high_alt_bias,
                              bool coorbital_bias,
                              bool crossing_injection,
                              bool long_step_stress,
                              bool plane_edge_bias,
                              bool phase_edge_bias,
                              bool moid_edge_bias,
                              bool moid_threshold_edge_bias,
                              bool moid_high_e_guard_bias,
                              bool moid_stale_epoch_bias)
{
    ScenarioOutcome out;
    out.step_seconds = step_seconds;
    out.scenario_seed = static_cast<int>(seed % 1000000000ULL);
    if (plane_edge_bias) {
        out.family = "plane_edge";
    } else if (phase_edge_bias) {
        out.family = "phase_edge";
    } else if (moid_edge_bias) {
        out.family = "moid_edge";
    } else if (moid_threshold_edge_bias) {
        out.family = "moid_threshold_edge";
    } else if (moid_high_e_guard_bias) {
        out.family = "moid_high_e_guard";
    } else if (moid_stale_epoch_bias) {
        out.family = "moid_stale_epoch";
    } else if (long_step_stress) {
        out.family = "long_step";
    } else if (high_alt_bias) {
        out.family = "high_alt";
    } else if (crossing_injection) {
        out.family = "crossing";
    } else if (coorbital_bias) {
        out.family = "coorbital";
    } else if (high_e_bias) {
        out.family = "high_e";
    } else {
        out.family = "baseline";
    }

    cascade::StateStore store_ref(static_cast<std::size_t>(sat_count + deb_count + 64));
    cascade::SimClock clock_ref;
    clock_ref.set_epoch_s(1773292800.0 + static_cast<double>(scenario_id) * 300.0);
    seed_store(store_ref, clock_ref, sat_count, deb_count, seed);

    if (coorbital_bias) {
        const int pair_count = std::min(sat_count, std::min(deb_count, 4));
        for (int k = 0; k < pair_count; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || deb_idx >= store_ref.size()) {
                continue;
            }

            const double offset = 0.015 + 0.005 * static_cast<double>(k);
            store_ref.rx_mut(deb_idx) = store_ref.rx(sat_idx) + offset;
            store_ref.ry_mut(deb_idx) = store_ref.ry(sat_idx);
            store_ref.rz_mut(deb_idx) = store_ref.rz(sat_idx);
            store_ref.vx_mut(deb_idx) = store_ref.vx(sat_idx) + 1e-5 * (k + 1);
            store_ref.vy_mut(deb_idx) = store_ref.vy(sat_idx) - 1e-5 * (k + 1);
            store_ref.vz_mut(deb_idx) = store_ref.vz(sat_idx);
            store_ref.set_elements(deb_idx, cascade::OrbitalElements{}, false);
        }
    }

    if (high_e_bias) {
        const int count = std::min(deb_count, 6);
        for (int k = 0; k < count; ++k) {
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (deb_idx >= store_ref.size()) {
                continue;
            }

            cascade::OrbitalElements el{};
            if (!store_ref.elements_valid(deb_idx)) {
                continue;
            }
            el.a_km = store_ref.a_km(deb_idx);
            el.e = std::min(0.25, std::max(0.12, store_ref.e(deb_idx) + 0.10));
            el.i_rad = store_ref.i_rad(deb_idx);
            el.raan_rad = store_ref.raan_rad(deb_idx);
            el.argp_rad = store_ref.argp_rad(deb_idx);
            el.M_rad = store_ref.M_rad(deb_idx);
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(deb_idx) = r.x;
            store_ref.ry_mut(deb_idx) = r.y;
            store_ref.rz_mut(deb_idx) = r.z;
            store_ref.vx_mut(deb_idx) = v.x;
            store_ref.vy_mut(deb_idx) = v.y;
            store_ref.vz_mut(deb_idx) = v.z;
            store_ref.set_elements(deb_idx, el, true);
        }
    }

    if (high_alt_bias) {
        const int sat_begin = std::min(sat_count, 3);
        const int sat_end = std::min(sat_count, 7);
        for (int k = sat_begin; k < sat_end; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || !store_ref.elements_valid(sat_idx)) {
                continue;
            }

            cascade::OrbitalElements el{};
            el.a_km = 11800.0 + 250.0 * static_cast<double>(k - sat_begin);
            el.e = std::min(0.08, std::max(0.005, store_ref.e(sat_idx)));
            el.i_rad = store_ref.i_rad(sat_idx);
            el.raan_rad = store_ref.raan_rad(sat_idx);
            el.argp_rad = store_ref.argp_rad(sat_idx);
            el.M_rad = store_ref.M_rad(sat_idx);
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(sat_idx) = r.x;
            store_ref.ry_mut(sat_idx) = r.y;
            store_ref.rz_mut(sat_idx) = r.z;
            store_ref.vx_mut(sat_idx) = v.x;
            store_ref.vy_mut(sat_idx) = v.y;
            store_ref.vz_mut(sat_idx) = v.z;
            store_ref.set_elements(sat_idx, el, true);
        }

        const int deb_begin = std::min(deb_count, 3);
        const int deb_end = std::min(deb_count, 7);
        for (int k = deb_begin; k < deb_end; ++k) {
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (deb_idx >= store_ref.size() || !store_ref.elements_valid(deb_idx)) {
                continue;
            }

            cascade::OrbitalElements el{};
            el.a_km = 11950.0 + 280.0 * static_cast<double>(k - deb_begin);
            el.e = std::min(0.12, std::max(0.02, store_ref.e(deb_idx)));
            el.i_rad = store_ref.i_rad(deb_idx);
            el.raan_rad = store_ref.raan_rad(deb_idx);
            el.argp_rad = store_ref.argp_rad(deb_idx);
            el.M_rad = store_ref.M_rad(deb_idx);
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(deb_idx) = r.x;
            store_ref.ry_mut(deb_idx) = r.y;
            store_ref.rz_mut(deb_idx) = r.z;
            store_ref.vx_mut(deb_idx) = v.x;
            store_ref.vy_mut(deb_idx) = v.y;
            store_ref.vz_mut(deb_idx) = v.z;
            store_ref.set_elements(deb_idx, el, true);
        }
    }

    if (crossing_injection) {
        const int pair_count = std::min(sat_count, std::min(deb_count, 4));
        for (int k = 0; k < pair_count; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || deb_idx >= store_ref.size()) {
                continue;
            }

            const cascade::Vec3 sat_r{store_ref.rx(sat_idx), store_ref.ry(sat_idx), store_ref.rz(sat_idx)};
            const cascade::Vec3 sat_v{store_ref.vx(sat_idx), store_ref.vy(sat_idx), store_ref.vz(sat_idx)};
            const double v_norm = std::sqrt(sat_v.x * sat_v.x + sat_v.y * sat_v.y + sat_v.z * sat_v.z);
            if (v_norm <= cascade::EPS_NUM) {
                continue;
            }

            const cascade::Vec3 t_hat{sat_v.x / v_norm, sat_v.y / v_norm, sat_v.z / v_norm};
            const double sign = (k % 2 == 0) ? 1.0 : -1.0;
            store_ref.rx_mut(deb_idx) = sat_r.x + 0.04 * sign;
            store_ref.ry_mut(deb_idx) = sat_r.y;
            store_ref.rz_mut(deb_idx) = sat_r.z;
            store_ref.vx_mut(deb_idx) = sat_v.x - t_hat.x * (12.0 * sign);
            store_ref.vy_mut(deb_idx) = sat_v.y - t_hat.y * (12.0 * sign);
            store_ref.vz_mut(deb_idx) = sat_v.z - t_hat.z * (12.0 * sign);
            store_ref.set_elements(deb_idx, cascade::OrbitalElements{}, false);
        }
    }

    const double plane_angle_threshold_rad =
        env_double("PROJECTBONK_NARROW_PLANE_ANGLE_THRESHOLD_RAD", 1.3089969389957472);
    const double phase_angle_threshold_rad =
        env_double("PROJECTBONK_NARROW_PHASE_ANGLE_THRESHOLD_RAD", 2.6179938779914944);
    const double moid_reject_threshold_km =
        env_double("PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM", 2.0);

    if (plane_edge_bias) {
        const int pair_begin = std::min(sat_count, 3);
        const int pair_end = std::min(sat_count, std::min(deb_count, 7));
        for (int k = pair_begin; k < pair_end; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || deb_idx >= store_ref.size()) {
                continue;
            }
            if (!store_ref.elements_valid(sat_idx)) {
                continue;
            }

            cascade::OrbitalElements el{};
            el.a_km = store_ref.a_km(sat_idx);
            el.e = std::max(0.001, std::min(0.12, store_ref.e(sat_idx)));
            el.i_rad = std::max(0.01, std::min(cascade::PI - 0.01,
                    store_ref.i_rad(sat_idx) + (plane_angle_threshold_rad - 0.03)));
            el.raan_rad = store_ref.raan_rad(sat_idx);
            el.argp_rad = store_ref.argp_rad(sat_idx);
            el.M_rad = store_ref.M_rad(sat_idx);
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(deb_idx) = r.x;
            store_ref.ry_mut(deb_idx) = r.y;
            store_ref.rz_mut(deb_idx) = r.z;
            store_ref.vx_mut(deb_idx) = v.x;
            store_ref.vy_mut(deb_idx) = v.y;
            store_ref.vz_mut(deb_idx) = v.z;
            store_ref.set_elements(deb_idx, el, true);
        }
    }

    if (phase_edge_bias) {
        const int pair_begin = std::min(sat_count, 3);
        const int pair_end = std::min(sat_count, std::min(deb_count, 7));
        for (int k = pair_begin; k < pair_end; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || deb_idx >= store_ref.size()) {
                continue;
            }
            if (!store_ref.elements_valid(sat_idx)) {
                continue;
            }

            cascade::OrbitalElements el{};
            el.a_km = store_ref.a_km(sat_idx);
            el.e = std::max(0.001, std::min(0.12, store_ref.e(sat_idx)));
            el.i_rad = store_ref.i_rad(sat_idx);
            el.raan_rad = store_ref.raan_rad(sat_idx);
            el.argp_rad = store_ref.argp_rad(sat_idx);
            el.M_rad = std::fmod(store_ref.M_rad(sat_idx) + (phase_angle_threshold_rad - 0.05), cascade::TWO_PI);
            if (el.M_rad < 0.0) {
                el.M_rad += cascade::TWO_PI;
            }
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(deb_idx) = r.x;
            store_ref.ry_mut(deb_idx) = r.y;
            store_ref.rz_mut(deb_idx) = r.z;
            store_ref.vx_mut(deb_idx) = v.x;
            store_ref.vy_mut(deb_idx) = v.y;
            store_ref.vz_mut(deb_idx) = v.z;
            store_ref.set_elements(deb_idx, el, true);
        }
    }

    if (moid_edge_bias) {
        const int pair_begin = std::min(sat_count, 3);
        const int pair_end = std::min(sat_count, std::min(deb_count, 7));
        const double edge_delta_km = std::max(0.05, moid_reject_threshold_km - 0.15);
        for (int k = pair_begin; k < pair_end; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || deb_idx >= store_ref.size()) {
                continue;
            }
            if (!store_ref.elements_valid(sat_idx)) {
                continue;
            }

            cascade::OrbitalElements el{};
            el.a_km = store_ref.a_km(sat_idx) + edge_delta_km;
            el.e = std::max(0.001, std::min(0.15, store_ref.e(sat_idx)));
            el.i_rad = store_ref.i_rad(sat_idx);
            el.raan_rad = store_ref.raan_rad(sat_idx);
            el.argp_rad = store_ref.argp_rad(sat_idx);
            el.M_rad = store_ref.M_rad(sat_idx);
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(deb_idx) = r.x;
            store_ref.ry_mut(deb_idx) = r.y;
            store_ref.rz_mut(deb_idx) = r.z;
            store_ref.vx_mut(deb_idx) = v.x;
            store_ref.vy_mut(deb_idx) = v.y;
            store_ref.vz_mut(deb_idx) = v.z;
            store_ref.set_elements(deb_idx, el, true);
        }
    }

    if (moid_threshold_edge_bias) {
        const int pair_begin = std::min(sat_count, 3);
        const int pair_end = std::min(sat_count, std::min(deb_count, 9));
        for (int k = pair_begin; k < pair_end; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || deb_idx >= store_ref.size()) {
                continue;
            }
            if (!store_ref.elements_valid(sat_idx)) {
                continue;
            }

            // Alternate around MOID threshold to stress reject boundary logic.
            const double side = (k % 2 == 0) ? -1.0 : 1.0;
            const double offset = std::max(0.03, moid_reject_threshold_km + side * 0.06);

            cascade::OrbitalElements el{};
            el.a_km = std::max(6600.0, store_ref.a_km(sat_idx) + offset);
            el.e = std::max(0.001, std::min(0.12, store_ref.e(sat_idx)));
            el.i_rad = store_ref.i_rad(sat_idx);
            el.raan_rad = store_ref.raan_rad(sat_idx);
            el.argp_rad = store_ref.argp_rad(sat_idx);
            el.M_rad = store_ref.M_rad(sat_idx);
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(deb_idx) = r.x;
            store_ref.ry_mut(deb_idx) = r.y;
            store_ref.rz_mut(deb_idx) = r.z;
            store_ref.vx_mut(deb_idx) = v.x;
            store_ref.vy_mut(deb_idx) = v.y;
            store_ref.vz_mut(deb_idx) = v.z;
            store_ref.set_elements(deb_idx, el, true);
        }
    }

    if (moid_high_e_guard_bias) {
        const int pair_begin = std::min(sat_count, 2);
        const int pair_end = std::min(sat_count, std::min(deb_count, 8));
        for (int k = pair_begin; k < pair_end; ++k) {
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (deb_idx >= store_ref.size() || !store_ref.elements_valid(deb_idx)) {
                continue;
            }

            cascade::OrbitalElements el{};
            el.a_km = store_ref.a_km(deb_idx);
            el.e = std::min(0.95, std::max(0.22, store_ref.e(deb_idx) + 0.18));
            el.i_rad = store_ref.i_rad(deb_idx);
            el.raan_rad = store_ref.raan_rad(deb_idx);
            el.argp_rad = store_ref.argp_rad(deb_idx);
            el.M_rad = store_ref.M_rad(deb_idx);
            el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
            el.p_km = el.a_km * (1.0 - el.e * el.e);
            el.rp_km = el.a_km * (1.0 - el.e);
            el.ra_km = el.a_km * (1.0 + el.e);

            cascade::Vec3 r{};
            cascade::Vec3 v{};
            if (!cascade::elements_to_eci(el, r, v)) {
                continue;
            }
            store_ref.rx_mut(deb_idx) = r.x;
            store_ref.ry_mut(deb_idx) = r.y;
            store_ref.rz_mut(deb_idx) = r.z;
            store_ref.vx_mut(deb_idx) = v.x;
            store_ref.vy_mut(deb_idx) = v.y;
            store_ref.vz_mut(deb_idx) = v.z;
            store_ref.set_elements(deb_idx, el, true);
        }
    }

    if (moid_stale_epoch_bias) {
        const int pair_begin = std::min(sat_count, 2);
        const int pair_end = std::min(sat_count, std::min(deb_count, 8));
        for (int k = pair_begin; k < pair_end; ++k) {
            const std::size_t sat_idx = store_ref.find("SAT-" + std::to_string(k));
            const std::size_t deb_idx = store_ref.find("DEB-" + std::to_string(k));
            if (sat_idx >= store_ref.size() || deb_idx >= store_ref.size()) {
                continue;
            }

            const double stale_dt_s = 7.0 * 24.0 * 3600.0 + 3600.0 * static_cast<double>(k);
            store_ref.set_telemetry_epoch_s(sat_idx, clock_ref.epoch_s() - stale_dt_s);
            store_ref.set_telemetry_epoch_s(deb_idx, clock_ref.epoch_s() - stale_dt_s);
        }
    }

    cascade::StateStore store_prod = store_ref;
    cascade::SimClock clock_prod = clock_ref;

    cascade::StepRunConfig cfg{};
    cfg.broad_phase.enable_dcriterion = false;
    cfg.narrow_phase.plane_phase_shadow =
        env_bool("PROJECTBONK_NARROW_PLANE_PHASE_SHADOW", cfg.narrow_phase.plane_phase_shadow);
    cfg.narrow_phase.plane_phase_filter =
        env_bool("PROJECTBONK_NARROW_PLANE_PHASE_FILTER", cfg.narrow_phase.plane_phase_filter);
    cfg.narrow_phase.plane_angle_threshold_rad =
        env_double("PROJECTBONK_NARROW_PLANE_ANGLE_THRESHOLD_RAD", cfg.narrow_phase.plane_angle_threshold_rad);
    cfg.narrow_phase.phase_angle_threshold_rad =
        env_double("PROJECTBONK_NARROW_PHASE_ANGLE_THRESHOLD_RAD", cfg.narrow_phase.phase_angle_threshold_rad);
    cfg.narrow_phase.phase_max_e =
        env_double("PROJECTBONK_NARROW_PHASE_MAX_E", cfg.narrow_phase.phase_max_e);
    cfg.narrow_phase.moid_shadow =
        env_bool("PROJECTBONK_NARROW_MOID_SHADOW", cfg.narrow_phase.moid_shadow);
    cfg.narrow_phase.moid_filter =
        env_bool("PROJECTBONK_NARROW_MOID_FILTER", cfg.narrow_phase.moid_filter);
    cfg.narrow_phase.moid_mode =
        env_moid_mode(cfg.narrow_phase.moid_mode);
    cfg.narrow_phase.moid_samples =
        env_u32("PROJECTBONK_NARROW_MOID_SAMPLES", cfg.narrow_phase.moid_samples, 6);
    cfg.narrow_phase.moid_reject_threshold_km =
        env_double("PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM", cfg.narrow_phase.moid_reject_threshold_km);
    cfg.narrow_phase.moid_max_e =
        env_double("PROJECTBONK_NARROW_MOID_MAX_E", cfg.narrow_phase.moid_max_e);

    cascade::StepRunStats stats{};
    if (!cascade::run_simulation_step(store_prod, clock_prod, step_seconds, stats, cfg)) {
        return out;
    }

    out.uncertainty_promoted_pairs = stats.narrow_uncertainty_promoted_pairs;
    out.plane_phase_hard_rejected_pairs = stats.narrow_plane_phase_hard_rejected_pairs;
    out.plane_phase_fail_open_pairs = stats.narrow_plane_phase_fail_open_pairs;
    out.moid_hard_rejected_pairs = stats.narrow_moid_hard_rejected_pairs;
    out.moid_fail_open_pairs = stats.narrow_moid_fail_open_pairs;

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

    const std::array<double, 13> steps{{30.0, 600.0, 1200.0, 3600.0, 43200.0, 172800.0, 900.0, 5400.0, 10800.0, 120.0, 1800.0, 7200.0, 14400.0}};
    const std::array<const char*, 13> family{{
        "baseline",
        "high_e",
        "coorbital",
        "crossing",
        "high_alt",
        "long_step",
        "plane_edge",
        "phase_edge",
        "moid_edge",
        "moid_threshold_edge",
        "moid_high_e_guard",
        "moid_stale_epoch",
        "baseline"
    }};

    struct FamilySummary {
        std::uint64_t scenarios = 0;
        std::uint64_t reference_collision_sats = 0;
        std::uint64_t production_collision_sats = 0;
        std::uint64_t false_negative_sats = 0;
    };
    std::unordered_map<std::string, FamilySummary> family_summary;

    std::uint64_t total_reference = 0;
    std::uint64_t total_production = 0;
    std::uint64_t total_false_negatives = 0;
    std::uint64_t total_uncertainty_promoted = 0;
    std::uint64_t total_plane_phase_hard_rejected = 0;
    std::uint64_t total_plane_phase_fail_open = 0;
    std::uint64_t total_moid_hard_rejected = 0;
    std::uint64_t total_moid_fail_open = 0;
    bool all_ok = true;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "narrow_phase_false_negative_gate\n";
    std::cout << "scenarios=" << scenarios << "\n";
    std::cout << "satellites=" << sat_count << "\n";
    std::cout << "debris=" << deb_count << "\n";
    std::cout << "narrow_plane_phase_filter="
              << (env_bool("PROJECTBONK_NARROW_PLANE_PHASE_FILTER", false) ? 1 : 0)
              << "\n";
    std::cout << "narrow_moid_filter="
              << (env_bool("PROJECTBONK_NARROW_MOID_FILTER", false) ? 1 : 0)
              << "\n";

    for (int s = 0; s < scenarios; ++s) {
        const double step_seconds = steps[static_cast<std::size_t>(s) % steps.size()];
        const std::string family_tag = family[static_cast<std::size_t>(s) % family.size()];
        const bool high_e_bias = (family_tag == "high_e");
        const bool high_alt_bias = (family_tag == "high_alt");
        const bool coorbital_bias = (family_tag == "coorbital");
        const bool crossing_injection = (family_tag == "crossing");
        const bool long_step_stress = (family_tag == "long_step");
        const bool plane_edge_bias = (family_tag == "plane_edge");
        const bool phase_edge_bias = (family_tag == "phase_edge");
        const bool moid_edge_bias = (family_tag == "moid_edge");
        const bool moid_threshold_edge_bias = (family_tag == "moid_threshold_edge");
        const bool moid_high_e_guard_bias = (family_tag == "moid_high_e_guard");
        const bool moid_stale_epoch_bias = (family_tag == "moid_stale_epoch");
        const std::uint64_t seed = 2026031800ULL + static_cast<std::uint64_t>(s) * 131ULL;

        const ScenarioOutcome outcome = run_scenario(
            s,
            sat_count,
            deb_count,
            step_seconds,
            seed,
            high_e_bias,
            high_alt_bias,
            coorbital_bias,
            crossing_injection,
            long_step_stress,
            plane_edge_bias,
            phase_edge_bias,
            moid_edge_bias,
            moid_threshold_edge_bias,
            moid_high_e_guard_bias,
            moid_stale_epoch_bias
        );

        if (!outcome.ok) {
            all_ok = false;
            std::cout << "scenario_" << s << "_result=FAIL\n";
            std::cout << "scenario_" << s << "_reason=execution_failed\n";
            continue;
        }

        total_reference += outcome.reference_collision_sats;
        total_production += outcome.production_collision_sats;
        total_false_negatives += outcome.false_negative_sats;
        total_uncertainty_promoted += outcome.uncertainty_promoted_pairs;
        total_plane_phase_hard_rejected += outcome.plane_phase_hard_rejected_pairs;
        total_plane_phase_fail_open += outcome.plane_phase_fail_open_pairs;
        total_moid_hard_rejected += outcome.moid_hard_rejected_pairs;
        total_moid_fail_open += outcome.moid_fail_open_pairs;

        FamilySummary& fs = family_summary[outcome.family];
        ++fs.scenarios;
        fs.reference_collision_sats += outcome.reference_collision_sats;
        fs.production_collision_sats += outcome.production_collision_sats;
        fs.false_negative_sats += outcome.false_negative_sats;

        std::cout << "scenario_" << s << "_family=" << outcome.family << "\n";
        std::cout << "scenario_" << s << "_seed=" << outcome.scenario_seed << "\n";
        std::cout << "scenario_" << s << "_step_seconds=" << outcome.step_seconds << "\n";
        std::cout << "scenario_" << s << "_reference_collision_sats=" << outcome.reference_collision_sats << "\n";
        std::cout << "scenario_" << s << "_production_collision_sats=" << outcome.production_collision_sats << "\n";
        std::cout << "scenario_" << s << "_false_negative_sats=" << outcome.false_negative_sats << "\n";
        std::cout << "scenario_" << s << "_result=" << (outcome.false_negative_sats == 0 ? "PASS" : "FAIL") << "\n";
    }

    std::cout << "reference_collision_sats_total=" << total_reference << "\n";
    std::cout << "production_collision_sats_total=" << total_production << "\n";
    std::cout << "false_negative_sats_total=" << total_false_negatives << "\n";
    std::cout << "narrow_uncertainty_promoted_pairs_total=" << total_uncertainty_promoted << "\n";
    std::cout << "narrow_plane_phase_hard_rejected_pairs_total=" << total_plane_phase_hard_rejected << "\n";
    std::cout << "narrow_plane_phase_fail_open_pairs_total=" << total_plane_phase_fail_open << "\n";
    std::cout << "narrow_moid_hard_rejected_pairs_total=" << total_moid_hard_rejected << "\n";
    std::cout << "narrow_moid_fail_open_pairs_total=" << total_moid_fail_open << "\n";

    const std::array<const char*, 12> family_order{{
        "baseline",
        "high_e",
        "coorbital",
        "crossing",
        "high_alt",
        "long_step",
        "plane_edge",
        "phase_edge",
        "moid_edge",
        "moid_threshold_edge",
        "moid_high_e_guard",
        "moid_stale_epoch"
    }};
    for (const char* fam : family_order) {
        const auto it = family_summary.find(fam);
        if (it == family_summary.end()) {
            continue;
        }
        std::cout << "family_" << fam << "_scenarios=" << it->second.scenarios << "\n";
        std::cout << "family_" << fam << "_reference_collision_sats_total=" << it->second.reference_collision_sats << "\n";
        std::cout << "family_" << fam << "_production_collision_sats_total=" << it->second.production_collision_sats << "\n";
        std::cout << "family_" << fam << "_false_negative_sats_total=" << it->second.false_negative_sats << "\n";
    }

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
