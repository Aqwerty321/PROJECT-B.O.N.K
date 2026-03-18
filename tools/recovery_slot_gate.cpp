// ---------------------------------------------------------------------------
// recovery_slot_gate.cpp
//
// Acceptance gate: slot-targeted recovery should not regress aggregate slot
// error after a bounded horizon in deterministic synthetic scenarios.
//
// Default mode (CI gate):
//   recovery_slot_gate [--scenarios N] [--margin M]
//
// Sweep mode (offline tuning helper):
//   recovery_slot_gate --sweep [--profile strict|strict-expanded]
//                      [--scenarios N] [--margin M]
//                      [--fuel-ratio-cap R] [--json-out PATH]
// ---------------------------------------------------------------------------

#include "state_store.hpp"
#include "sim_clock.hpp"
#include "simulation_engine.hpp"
#include "types.hpp"
#include "orbit_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using cascade::ObjectType;
using cascade::OrbitalElements;
using cascade::StateStore;
using cascade::StepRunConfig;
using cascade::StepRunStats;
using cascade::Vec3;

constexpr int kAcceptanceDefaultScenarios = 1;
constexpr double kAcceptanceDefaultMargin = 0.1;
constexpr int kSweepDefaultScenarios = 24;
constexpr double kSweepDefaultMargin = 0.08;
constexpr double kSweepDefaultFuelRatioCap = 1.10;
constexpr std::string_view kSweepProfileStrict = "strict";
constexpr std::string_view kSweepProfileStrictExpanded = "strict-expanded";

struct Burn {
    std::string sat_id;
    double epoch_s = 0.0;
    Vec3 dv{};
    double dv_norm_km_s = 0.0;
    bool recovery = false;
};

struct RecoveryRequest {
    Vec3 rem{};
    double earliest_s = 0.0;
};

struct RecoveryGains {
    double scale_t = 6e-5;
    double scale_r = 2e-3;
    double radial_share = 0.5;
    double scale_n = 6e-3;
    double fallback_norm_km_s = 2e-4;
    double max_request_ratio = 1.0;
};

struct NamedGains {
    std::string name;
    RecoveryGains gains{};
};

struct RunOutcome {
    bool ok = false;
    double start_err = 0.0;
    double end_err = 0.0;
    std::uint64_t auto_cola_queued = 0;
    std::uint64_t maneuvers_executed = 0;
    std::uint64_t recovery_planned = 0;
    std::uint64_t recovery_deferred = 0;
    std::uint64_t recovery_completed = 0;
    double fuel_used_kg = 0.0;
};

struct GateOptions {
    bool sweep = false;
    std::string profile = std::string(kSweepProfileStrict);

    bool scenarios_set = false;
    int scenarios = kAcceptanceDefaultScenarios;

    bool margin_set = false;
    double margin = kAcceptanceDefaultMargin;

    bool fuel_ratio_cap_set = false;
    double fuel_ratio_cap = kSweepDefaultFuelRatioCap;

    bool json_out_set = false;
    std::string json_out;
};

struct ScenarioEval {
    int id = 0;
    bool pass = false;
    std::string reason;
    RunOutcome baseline{};
    RunOutcome recovered{};
    double delta_err = 0.0;
};

struct CandidateEval {
    std::string name;
    RecoveryGains gains{};

    std::vector<ScenarioEval> scenarios;

    int scenario_pass_count = 0;
    bool pass_all_scenarios = false;
    bool pass_fuel_ratio = false;
    bool pass = false;

    double mean_delta_slot_error = 0.0;
    double worst_delta_slot_error = 0.0;
    double mean_fuel_used_kg = 0.0;
    double fuel_ratio_to_default = 0.0;

    std::string fail_reason;
};

std::string escape_json(std::string_view in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::uint64_t mix64(std::uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

double deterministic_signed_noise(int scenario_id, int axis)
{
    const std::uint64_t key =
        mix64(0x9e3779b97f4a7c15ULL
              ^ (static_cast<std::uint64_t>(scenario_id + 1) * 0xA24BAED4963EE407ULL)
              ^ (static_cast<std::uint64_t>(axis + 11) * 0x9FB21C651E98DF25ULL));

    const double unit = static_cast<double>(key & 0xFFFFFFFFULL)
                        / static_cast<double>(0xFFFFFFFFULL);
    return (2.0 * unit) - 1.0;
}

bool parse_int(std::string_view text, int& out)
{
    if (text.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(std::string(text).c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(v);
    return true;
}

bool parse_double(std::string_view text, double& out)
{
    if (text.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(std::string(text).c_str(), &end);
    if (end == nullptr || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

void print_usage(const char* argv0)
{
    std::cout << "usage: " << argv0
              << " [--scenarios N] [--margin M] [--sweep]"
              << " [--profile strict|strict-expanded]"
              << " [--fuel-ratio-cap R] [--json-out PATH]\n";
}

bool parse_args(int argc, char** argv, GateOptions& opts)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        }
        if (arg == "--sweep") {
            opts.sweep = true;
            continue;
        }
        if (arg == "--profile") {
            if (i + 1 >= argc) return false;
            const std::string value = argv[++i];
            if (value != kSweepProfileStrict && value != kSweepProfileStrictExpanded) {
                return false;
            }
            opts.profile = value;
            continue;
        }
        if (arg == "--scenarios") {
            if (i + 1 >= argc) return false;
            int value = 0;
            if (!parse_int(argv[++i], value) || value <= 0) return false;
            opts.scenarios_set = true;
            opts.scenarios = value;
            continue;
        }
        if (arg == "--margin") {
            if (i + 1 >= argc) return false;
            double value = 0.0;
            if (!parse_double(argv[++i], value) || value < 0.0) return false;
            opts.margin_set = true;
            opts.margin = value;
            continue;
        }
        if (arg == "--fuel-ratio-cap") {
            if (i + 1 >= argc) return false;
            double value = 0.0;
            if (!parse_double(argv[++i], value) || value <= 0.0) return false;
            opts.fuel_ratio_cap_set = true;
            opts.fuel_ratio_cap = value;
            continue;
        }
        if (arg == "--json-out") {
            if (i + 1 >= argc) return false;
            opts.json_out_set = true;
            opts.json_out = argv[++i];
            if (opts.json_out.empty()) return false;
            continue;
        }
        return false;
    }
    return true;
}

double vec_norm(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

double propellant_used_kg(double mass_kg, double delta_v_km_s)
{
    if (!(mass_kg > 0.0) || !(delta_v_km_s > 0.0)) return 0.0;
    const double ve_km_s = cascade::SAT_ISP_S * cascade::G0_KM_S2;
    if (!(ve_km_s > 0.0)) return 0.0;
    return mass_kg * (1.0 - std::exp(-delta_v_km_s / ve_km_s));
}

bool get_elements(const StateStore& store, std::size_t idx, OrbitalElements& out)
{
    if (idx >= store.size()) return false;
    if (store.elements_valid(idx)) {
        out.a_km = store.a_km(idx);
        out.e = store.e(idx);
        out.i_rad = store.i_rad(idx);
        out.raan_rad = store.raan_rad(idx);
        out.argp_rad = store.argp_rad(idx);
        out.M_rad = store.M_rad(idx);
        out.n_rad_s = store.n_rad_s(idx);
        out.p_km = store.p_km(idx);
        out.rp_km = store.rp_km(idx);
        out.ra_km = store.ra_km(idx);
        return true;
    }
    return cascade::eci_to_elements(
        Vec3{store.rx(idx), store.ry(idx), store.rz(idx)},
        Vec3{store.vx(idx), store.vy(idx), store.vz(idx)},
        out
    );
}

double slot_error_score(const OrbitalElements& slot, const OrbitalElements& cur)
{
    const double da = std::abs(slot.a_km - cur.a_km);
    const double de = std::abs(slot.e - cur.e);
    const double di = std::abs(slot.i_rad - cur.i_rad);
    const double d_raan = std::abs(cascade::wrap_0_2pi(slot.raan_rad - cur.raan_rad + cascade::PI) - cascade::PI);
    return (da / 10.0) + (de / 1e-3) + (di / 1e-3) + (d_raan / 1e-3);
}

Vec3 slot_target_recovery_dv(const StateStore& store,
                             std::size_t sat_idx,
                             const RecoveryRequest& req,
                             const OrbitalElements& slot,
                             const RecoveryGains& gains)
{
    OrbitalElements cur{};
    if (!get_elements(store, sat_idx, cur)) return req.rem;

    const Vec3 r{store.rx(sat_idx), store.ry(sat_idx), store.rz(sat_idx)};
    const Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};

    const double v_norm = vec_norm(v);
    const double r_norm = vec_norm(r);
    if (v_norm < cascade::EPS_NUM || r_norm < cascade::EPS_NUM) return req.rem;

    const Vec3 t_hat{v.x / v_norm, v.y / v_norm, v.z / v_norm};
    const Vec3 r_hat{r.x / r_norm, r.y / r_norm, r.z / r_norm};

    const double hx = r.y * v.z - r.z * v.y;
    const double hy = r.z * v.x - r.x * v.z;
    const double hz = r.x * v.y - r.y * v.x;
    const double h_norm = std::sqrt(hx * hx + hy * hy + hz * hz);
    if (h_norm < cascade::EPS_NUM) return req.rem;
    const Vec3 n_hat{hx / h_norm, hy / h_norm, hz / h_norm};

    const double da = slot.a_km - cur.a_km;
    const double de = slot.e - cur.e;
    const double di = slot.i_rad - cur.i_rad;
    const double d_raan = cascade::wrap_0_2pi(slot.raan_rad - cur.raan_rad + cascade::PI) - cascade::PI;

    const double dv_t = (da * gains.scale_t) + (de * gains.scale_r);
    const double dv_r = de * (gains.radial_share * gains.scale_r);
    const double dv_n = (di + d_raan) * gains.scale_n;

    Vec3 slot_dv{
        t_hat.x * dv_t + r_hat.x * dv_r + n_hat.x * dv_n,
        t_hat.y * dv_t + r_hat.y * dv_r + n_hat.y * dv_n,
        t_hat.z * dv_t + r_hat.z * dv_r + n_hat.z * dv_n
    };

    const double rem_norm = vec_norm(req.rem);
    if (vec_norm(slot_dv) < gains.fallback_norm_km_s && rem_norm > cascade::EPS_NUM) {
        slot_dv = req.rem;
    }

    const double cmd_norm = vec_norm(slot_dv);
    const double max_allowed_norm = rem_norm * gains.max_request_ratio;
    if (cmd_norm > max_allowed_norm + cascade::EPS_NUM
        && max_allowed_norm > cascade::EPS_NUM) {
        const double scale = max_allowed_norm / cmd_norm;
        slot_dv.x *= scale;
        slot_dv.y *= scale;
        slot_dv.z *= scale;
    }

    return slot_dv;
}

bool has_cooldown_or_pending(const std::vector<Burn>& queue,
                             const std::unordered_map<std::string, double>& last_exec,
                             const std::string& sat_id,
                             double epoch_s)
{
    auto it = last_exec.find(sat_id);
    if (it != last_exec.end()) {
        if (epoch_s - it->second + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) return true;
    }
    for (const Burn& b : queue) {
        if (b.sat_id != sat_id) continue;
        if (std::abs(b.epoch_s - epoch_s) + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) return true;
    }
    return false;
}

void apply_burn(StateStore& store, const Burn& b)
{
    const std::size_t idx = store.find(b.sat_id);
    if (idx >= store.size() || store.type(idx) != ObjectType::SATELLITE) return;

    const double mass_before = store.mass_kg(idx);
    const double fuel_before = store.fuel_kg(idx);
    const double fuel_need = propellant_used_kg(mass_before, b.dv_norm_km_s);
    if (fuel_need > fuel_before + cascade::EPS_NUM) return;

    store.vx_mut(idx) += b.dv.x;
    store.vy_mut(idx) += b.dv.y;
    store.vz_mut(idx) += b.dv.z;
    store.set_elements(idx, OrbitalElements{}, false);

    const double fuel_after = std::max(0.0, fuel_before - fuel_need);
    const double mass_after = std::max(cascade::SAT_DRY_MASS_KG, mass_before - fuel_need);
    store.fuel_kg_mut(idx) = fuel_after;
    store.mass_kg_mut(idx) = mass_after;
}

double aggregate_slot_error(const StateStore& store,
                            const std::unordered_map<std::string, OrbitalElements>& slot)
{
    double sum = 0.0;
    std::uint64_t n = 0;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != ObjectType::SATELLITE) continue;
        const auto it = slot.find(store.id(i));
        if (it == slot.end()) continue;
        OrbitalElements cur{};
        if (!get_elements(store, i, cur)) continue;
        sum += slot_error_score(it->second, cur);
        ++n;
    }
    if (n == 0) return 0.0;
    return sum / static_cast<double>(n);
}

RunOutcome run_scenario(bool enable_recovery,
                        const RecoveryGains& gains,
                        int scenario_id)
{
    RunOutcome out;

    StateStore store;
    cascade::SimClock clock;
    StepRunConfig cfg{};
    cfg.broad_phase.enable_dcriterion = false;

    const double t0 = 1773302400.0; // 2026-03-12T08:00:00Z
    clock.set_epoch_s(t0);

    const double offset_x = 0.02 * deterministic_signed_noise(scenario_id, 0);
    const double offset_y = 0.02 * deterministic_signed_noise(scenario_id, 1);
    const double offset_z = 0.02 * deterministic_signed_noise(scenario_id, 2);
    const double sat_dv = 0.00010 * deterministic_signed_noise(scenario_id, 3);
    const double deb_dv = 0.00010 * deterministic_signed_noise(scenario_id, 4);

    bool conflict = false;
    (void)store.upsert("SAT-GATE-1", ObjectType::SATELLITE,
                       7000.0, 0.0, 0.0,
                       0.0, 7.5 + sat_dv, 0.0,
                       t0,
                       conflict);
    (void)store.upsert("DEB-GATE-1", ObjectType::DEBRIS,
                       7000.0 + offset_x, offset_y, offset_z,
                       0.0, 7.5 + deb_dv, 0.0,
                       t0,
                       conflict);

    std::unordered_map<std::string, OrbitalElements> slot;
    {
        const std::size_t idx = store.find("SAT-GATE-1");
        OrbitalElements el{};
        if (!get_elements(store, idx, el)) {
            return out;
        }
        slot["SAT-GATE-1"] = el;
    }

    std::unordered_map<std::string, RecoveryRequest> rec;
    std::unordered_map<std::string, double> last_exec;
    std::vector<Burn> queue;

    auto maybe_queue_auto_cola = [&](double epoch_s) {
        const std::size_t s = store.find("SAT-GATE-1");
        const std::size_t d = store.find("DEB-GATE-1");
        if (s >= store.size() || d >= store.size()) return false;

        const double dx = store.rx(s) - store.rx(d);
        const double dy = store.ry(s) - store.ry(d);
        const double dz = store.rz(s) - store.rz(d);
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > cascade::COLLISION_THRESHOLD_KM * cascade::COLLISION_THRESHOLD_KM) return false;

        if (has_cooldown_or_pending(queue, last_exec, "SAT-GATE-1", epoch_s)) return false;

        const Vec3 v{store.vx(s), store.vy(s), store.vz(s)};
        const double vn = vec_norm(v);
        if (vn < cascade::EPS_NUM) return false;

        const double dv = 0.001;
        Burn b;
        b.sat_id = "SAT-GATE-1";
        b.epoch_s = epoch_s;
        b.dv = Vec3{v.x / vn * dv, v.y / vn * dv, v.z / vn * dv};
        b.dv_norm_km_s = dv;
        b.recovery = false;
        queue.push_back(b);
        return true;
    };

    auto execute_due = [&](double epoch_s, std::uint64_t& rec_completed, std::uint64_t& executed) {
        std::vector<Burn> pending;
        pending.reserve(queue.size());
        for (const Burn& b : queue) {
            if (b.epoch_s > epoch_s + cascade::EPS_NUM) {
                pending.push_back(b);
                continue;
            }
            apply_burn(store, b);
            last_exec[b.sat_id] = b.epoch_s;
            ++executed;
            if (!b.recovery) {
                RecoveryRequest& r = rec[b.sat_id];
                r.rem.x -= b.dv.x;
                r.rem.y -= b.dv.y;
                r.rem.z -= b.dv.z;
                r.earliest_s = std::max(r.earliest_s, b.epoch_s + cascade::SAT_COOLDOWN_S);
            } else {
                ++rec_completed;
            }
        }
        queue.swap(pending);
    };

    auto plan_recovery = [&](double epoch_s, std::uint64_t& planned, std::uint64_t& deferred) {
        std::vector<std::string> ready;
        for (const auto& kv : rec) {
            if (kv.second.earliest_s <= epoch_s + cascade::EPS_NUM) ready.push_back(kv.first);
        }
        for (const std::string& sat_id : ready) {
            auto it = rec.find(sat_id);
            if (it == rec.end()) continue;
            const std::size_t s = store.find(sat_id);
            if (s >= store.size()) {
                rec.erase(it);
                continue;
            }
            if (has_cooldown_or_pending(queue, last_exec, sat_id, epoch_s)) {
                ++deferred;
                continue;
            }

            Vec3 dv = slot_target_recovery_dv(store, s, it->second, slot[sat_id], gains);
            const double n = vec_norm(dv);
            if (n <= cascade::EPS_NUM) {
                rec.erase(it);
                continue;
            }

            const double cap = cascade::SAT_MAX_DELTAV_KM_S;
            const double scale = (n > cap) ? (cap / n) : 1.0;
            Burn b;
            b.sat_id = sat_id;
            b.epoch_s = epoch_s;
            b.dv = Vec3{dv.x * scale, dv.y * scale, dv.z * scale};
            b.dv_norm_km_s = vec_norm(b.dv);
            b.recovery = true;
            queue.push_back(b);

            it->second.rem.x -= b.dv.x;
            it->second.rem.y -= b.dv.y;
            it->second.rem.z -= b.dv.z;
            it->second.earliest_s = epoch_s + cascade::SAT_COOLDOWN_S;
            if (vec_norm(it->second.rem) <= 1e-6) rec.erase(it);
            ++planned;
        }
    };

    const std::size_t sat_idx = store.find("SAT-GATE-1");
    const double fuel_initial = (sat_idx < store.size()) ? store.fuel_kg(sat_idx) : 0.0;

    StepRunStats stats{};
    if (!cascade::run_simulation_step(store, clock, 1.0, stats, cfg)) {
        return out;
    }
    if (maybe_queue_auto_cola(clock.epoch_s())) {
        ++out.auto_cola_queued;
    }
    execute_due(clock.epoch_s(), out.recovery_completed, out.maneuvers_executed);

    out.start_err = aggregate_slot_error(store, slot);

    for (int k = 0; k < 15; ++k) {
        StepRunStats s{};
        if (!cascade::run_simulation_step(store, clock, 600.0, s, cfg)) {
            return out;
        }
        if (maybe_queue_auto_cola(clock.epoch_s())) {
            ++out.auto_cola_queued;
        }
        execute_due(clock.epoch_s(), out.recovery_completed, out.maneuvers_executed);
        if (enable_recovery) {
            plan_recovery(clock.epoch_s(), out.recovery_planned, out.recovery_deferred);
        }
    }

    const double fuel_final = (sat_idx < store.size()) ? store.fuel_kg(sat_idx) : fuel_initial;
    out.fuel_used_kg = std::max(0.0, fuel_initial - fuel_final);
    out.end_err = aggregate_slot_error(store, slot);
    out.ok = true;
    return out;
}

ScenarioEval evaluate_scenario(int scenario_id,
                               const RecoveryGains& gains,
                               double margin)
{
    ScenarioEval eval{};
    eval.id = scenario_id;
    eval.baseline = run_scenario(false, gains, scenario_id);
    eval.recovered = run_scenario(true, gains, scenario_id);

    if (!eval.baseline.ok || !eval.recovered.ok) {
        eval.reason = "scenario execution failed";
        return eval;
    }

    if (eval.recovered.recovery_planned == 0 || eval.recovered.recovery_completed == 0) {
        eval.reason = "no recovery burn was planned/executed";
        return eval;
    }

    eval.delta_err = eval.recovered.end_err - eval.baseline.end_err;
    if (eval.recovered.end_err > eval.baseline.end_err + margin) {
        eval.reason = "recovery worsened slot error beyond margin";
        return eval;
    }

    eval.pass = true;
    return eval;
}

std::vector<NamedGains> build_sweep_candidates()
{
    const RecoveryGains base{};
    const std::array<double, 3> mults{0.8, 1.0, 1.2};

    std::vector<NamedGains> out;
    out.reserve(48);

    for (double mt : mults) {
        for (double mr : mults) {
            for (double mn : mults) {
                RecoveryGains g = base;
                g.scale_t *= mt;
                g.scale_r *= mr;
                g.scale_n *= mn;
                g.radial_share = 0.5;
                g.fallback_norm_km_s = 2e-4;

                std::ostringstream name;
                if (mt == 1.0 && mr == 1.0 && mn == 1.0) {
                    name << "default";
                } else {
                    name << std::fixed << std::setprecision(1)
                         << "grid_t" << mt
                         << "_r" << mr
                         << "_n" << mn;
                }
                out.push_back(NamedGains{name.str(), g});
            }
        }
    }

    {
        RecoveryGains g = base;
        g.fallback_norm_km_s = 1e-4;
        out.push_back(NamedGains{"fallback_1e-4", g});
    }
    {
        RecoveryGains g = base;
        g.fallback_norm_km_s = 2e-4;
        out.push_back(NamedGains{"fallback_2e-4", g});
    }
    {
        RecoveryGains g = base;
        g.fallback_norm_km_s = 3e-4;
        out.push_back(NamedGains{"fallback_3e-4", g});
    }

    {
        RecoveryGains g = base;
        g.radial_share = 0.35;
        out.push_back(NamedGains{"radial_0.35", g});
    }
    {
        RecoveryGains g = base;
        g.radial_share = 0.50;
        out.push_back(NamedGains{"radial_0.50", g});
    }
    {
        RecoveryGains g = base;
        g.radial_share = 0.65;
        out.push_back(NamedGains{"radial_0.65", g});
    }

    {
        const std::array<double, 6> ratios{0.20, 0.30, 0.40, 0.50, 0.60, 0.80};
        for (double ratio : ratios) {
            RecoveryGains g = base;
            g.max_request_ratio = ratio;

            std::ostringstream name;
            name << std::fixed << std::setprecision(2)
                 << "request_ratio_" << ratio;
            out.push_back(NamedGains{name.str(), g});
        }
    }

    {
        const std::array<double, 4> ratios{0.20, 0.30, 0.40, 0.50};
        for (double ratio : ratios) {
            RecoveryGains g = base;
            g.fallback_norm_km_s = 1e-4;
            g.max_request_ratio = ratio;

            std::ostringstream name;
            name << std::fixed << std::setprecision(2)
                 << "fallback_1e-4_ratio_" << ratio;
            out.push_back(NamedGains{name.str(), g});
        }
    }

    return out;
}

std::vector<NamedGains> build_sweep_candidates_for_profile(std::string_view profile)
{
    std::vector<NamedGains> out = build_sweep_candidates();
    if (profile == kSweepProfileStrict) {
        return out;
    }
    if (profile != kSweepProfileStrictExpanded) {
        return out;
    }

    const RecoveryGains base{};

    const std::array<double, 3> scales{0.6, 0.8, 1.0};
    const std::array<double, 4> radial_shares{0.35, 0.45, 0.55, 0.65};

    for (double st : scales) {
        for (double sr : scales) {
            for (double sn : scales) {
                for (double rs : radial_shares) {
                    RecoveryGains g = base;
                    g.scale_t *= st;
                    g.scale_r *= sr;
                    g.scale_n *= sn;
                    g.radial_share = rs;
                    g.fallback_norm_km_s = 1e-4;
                    g.max_request_ratio = 1.0;

                    std::ostringstream name;
                    name << std::fixed << std::setprecision(2)
                         << "expanded_grid_t" << st
                         << "_r" << sr
                         << "_n" << sn
                         << "_rad" << rs
                         << "_fb1e-4";
                    out.push_back(NamedGains{name.str(), g});
                }
            }
        }
    }

    const std::array<double, 4> fallback_values{0.8e-4, 1.0e-4, 1.2e-4, 1.4e-4};
    for (double fallback : fallback_values) {
        RecoveryGains g = base;
        g.fallback_norm_km_s = fallback;
        g.max_request_ratio = 1.0;

        std::ostringstream name;
        name << std::scientific << std::setprecision(1)
             << "expanded_base_fallback_" << fallback;
        out.push_back(NamedGains{name.str(), g});
    }

    return out;
}

std::string format_double(double x)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(12) << x;
    return ss.str();
}

void write_sweep_json(const std::string& path,
                      std::string_view profile,
                      int scenarios,
                      double margin,
                      double fuel_ratio_cap,
                      double default_mean_fuel,
                      const std::vector<CandidateEval>& candidates,
                      const CandidateEval* selected,
                      const std::string& selection_reason,
                      bool success)
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    out << "{\n";
    out << "  \"tool\":\"recovery_slot_sweep\",\n";
    out << "  \"profile\":{\n";
    out << "    \"name\":\"" << escape_json(profile) << "\",\n";
    out << "    \"scenarios\":" << scenarios << ",\n";
    out << "    \"margin\":" << format_double(margin) << ",\n";
    out << "    \"fuel_ratio_cap\":" << format_double(fuel_ratio_cap) << "\n";
    out << "  },\n";
    out << "  \"candidate_count\":" << candidates.size() << ",\n";
    out << "  \"default_candidate\":{\n";
    out << "    \"name\":\"default\",\n";
    out << "    \"mean_fuel_used_kg\":" << format_double(default_mean_fuel) << "\n";
    out << "  },\n";
    out << "  \"selection\":{\n";
    out << "    \"status\":\"" << (success ? "PASS" : "FAIL") << "\",\n";
    out << "    \"reason\":\"" << escape_json(selection_reason) << "\",\n";
    if (selected != nullptr) {
        out << "    \"selected_candidate\":\"" << escape_json(selected->name) << "\",\n";
        out << "    \"selected_mean_delta_slot_error\":" << format_double(selected->mean_delta_slot_error) << ",\n";
        out << "    \"selected_mean_fuel_used_kg\":" << format_double(selected->mean_fuel_used_kg) << ",\n";
        out << "    \"selected_fuel_ratio_to_default\":" << format_double(selected->fuel_ratio_to_default) << ",\n";
    } else {
        out << "    \"selected_candidate\":null,\n";
    }
    out << "    \"ranking_rule\":\"min(mean_delta_slot_error), then min(mean_fuel_used_kg), then lexical name\"\n";
    out << "  },\n";
    out << "  \"candidates\":[\n";

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const CandidateEval& c = candidates[i];
        out << "    {\n";
        out << "      \"name\":\"" << escape_json(c.name) << "\",\n";
        out << "      \"gains\":{\n";
        out << "        \"scale_t\":" << format_double(c.gains.scale_t) << ",\n";
        out << "        \"scale_r\":" << format_double(c.gains.scale_r) << ",\n";
        out << "        \"scale_n\":" << format_double(c.gains.scale_n) << ",\n";
        out << "        \"radial_share\":" << format_double(c.gains.radial_share) << ",\n";
        out << "        \"fallback_norm_km_s\":" << format_double(c.gains.fallback_norm_km_s) << ",\n";
        out << "        \"max_request_ratio\":" << format_double(c.gains.max_request_ratio) << "\n";
        out << "      },\n";
        out << "      \"pass\":" << (c.pass ? "true" : "false") << ",\n";
        out << "      \"pass_all_scenarios\":" << (c.pass_all_scenarios ? "true" : "false") << ",\n";
        out << "      \"pass_fuel_ratio\":" << (c.pass_fuel_ratio ? "true" : "false") << ",\n";
        out << "      \"scenario_pass_count\":" << c.scenario_pass_count << ",\n";
        out << "      \"scenario_count\":" << c.scenarios.size() << ",\n";
        out << "      \"mean_delta_slot_error\":" << format_double(c.mean_delta_slot_error) << ",\n";
        out << "      \"worst_delta_slot_error\":" << format_double(c.worst_delta_slot_error) << ",\n";
        out << "      \"mean_fuel_used_kg\":" << format_double(c.mean_fuel_used_kg) << ",\n";
        out << "      \"fuel_ratio_to_default\":" << format_double(c.fuel_ratio_to_default) << ",\n";
        out << "      \"fail_reason\":\"" << escape_json(c.fail_reason) << "\",\n";
        out << "      \"scenarios\":[\n";

        for (std::size_t j = 0; j < c.scenarios.size(); ++j) {
            const ScenarioEval& s = c.scenarios[j];
            out << "        {\n";
            out << "          \"id\":" << s.id << ",\n";
            out << "          \"pass\":" << (s.pass ? "true" : "false") << ",\n";
            out << "          \"reason\":\"" << escape_json(s.reason) << "\",\n";
            out << "          \"delta_slot_error\":" << format_double(s.delta_err) << ",\n";
            out << "          \"baseline_end_slot_error\":" << format_double(s.baseline.end_err) << ",\n";
            out << "          \"recovery_end_slot_error\":" << format_double(s.recovered.end_err) << ",\n";
            out << "          \"recovery_planned\":" << s.recovered.recovery_planned << ",\n";
            out << "          \"recovery_completed\":" << s.recovered.recovery_completed << ",\n";
            out << "          \"fuel_used_kg\":" << format_double(s.recovered.fuel_used_kg) << "\n";
            out << "        }" << (j + 1 < c.scenarios.size() ? "," : "") << "\n";
        }

        out << "      ]\n";
        out << "    }" << (i + 1 < candidates.size() ? "," : "") << "\n";
    }

    out << "  ]\n";
    out << "}\n";
}

int run_default_gate(const GateOptions& opts)
{
    const int scenarios = opts.scenarios_set ? opts.scenarios : kAcceptanceDefaultScenarios;
    const double margin = opts.margin_set ? opts.margin : kAcceptanceDefaultMargin;

    std::cout << "recovery_slot_gate\n";
    std::cout << "mode=acceptance\n";
    std::cout << "scenarios=" << scenarios << "\n";
    std::cout << "margin=" << margin << "\n";

    const RecoveryGains gains{};

    bool all_pass = true;
    std::uint64_t planned_total = 0;
    std::uint64_t completed_total = 0;
    double worst_delta = -1.0e30;
    double mean_delta = 0.0;

    for (int s = 0; s < scenarios; ++s) {
        const ScenarioEval eval = evaluate_scenario(s, gains, margin);
        planned_total += eval.recovered.recovery_planned;
        completed_total += eval.recovered.recovery_completed;
        worst_delta = std::max(worst_delta, eval.delta_err);
        mean_delta += eval.delta_err;

        std::cout << "scenario_" << s << "_baseline_end=" << eval.baseline.end_err << "\n";
        std::cout << "scenario_" << s << "_recovery_end=" << eval.recovered.end_err << "\n";
        std::cout << "scenario_" << s << "_delta=" << eval.delta_err << "\n";
        std::cout << "scenario_" << s << "_result=" << (eval.pass ? "PASS" : "FAIL") << "\n";
        if (!eval.pass) {
            all_pass = false;
            std::cout << "scenario_" << s << "_reason=" << eval.reason << "\n";
        }
    }

    mean_delta /= static_cast<double>(scenarios);

    std::cout << "recovery_planned_total=" << planned_total << "\n";
    std::cout << "recovery_completed_total=" << completed_total << "\n";
    std::cout << "worst_delta_slot_error=" << worst_delta << "\n";
    std::cout << "mean_delta_slot_error=" << mean_delta << "\n";

    if (!all_pass || planned_total == 0 || completed_total == 0) {
        std::cout << "recovery_slot_gate_result=FAIL\n";
        return 1;
    }

    std::cout << "recovery_slot_gate_result=PASS\n";
    return 0;
}

int run_sweep_mode(const GateOptions& opts)
{
    const int scenarios = opts.scenarios_set ? opts.scenarios : kSweepDefaultScenarios;
    const double margin = opts.margin_set ? opts.margin : kSweepDefaultMargin;
    const double fuel_ratio_cap = opts.fuel_ratio_cap_set ? opts.fuel_ratio_cap : kSweepDefaultFuelRatioCap;
    const std::string_view profile = opts.profile;

    std::cout << "recovery_slot_sweep\n";
    std::cout << "mode=sweep\n";
    std::cout << "profile=" << profile << "\n";
    std::cout << "scenarios=" << scenarios << "\n";
    std::cout << "margin=" << margin << "\n";
    std::cout << "fuel_ratio_cap=" << fuel_ratio_cap << "\n";

    const std::vector<NamedGains> candidate_defs = build_sweep_candidates_for_profile(profile);
    std::vector<CandidateEval> candidates;
    candidates.reserve(candidate_defs.size());

    for (const NamedGains& def : candidate_defs) {
        CandidateEval eval;
        eval.name = def.name;
        eval.gains = def.gains;
        eval.worst_delta_slot_error = -1.0e30;
        eval.scenarios.reserve(static_cast<std::size_t>(scenarios));

        for (int s = 0; s < scenarios; ++s) {
            ScenarioEval scenario = evaluate_scenario(s, def.gains, margin);
            if (scenario.pass) {
                ++eval.scenario_pass_count;
            }
            eval.mean_delta_slot_error += scenario.delta_err;
            eval.worst_delta_slot_error = std::max(eval.worst_delta_slot_error, scenario.delta_err);
            eval.mean_fuel_used_kg += scenario.recovered.fuel_used_kg;
            eval.scenarios.push_back(std::move(scenario));
        }

        eval.mean_delta_slot_error /= static_cast<double>(scenarios);
        eval.mean_fuel_used_kg /= static_cast<double>(scenarios);
        eval.pass_all_scenarios = (eval.scenario_pass_count == scenarios);

        candidates.push_back(std::move(eval));
    }

    auto default_it = std::find_if(candidates.begin(), candidates.end(),
                                   [](const CandidateEval& c) { return c.name == "default"; });
    if (default_it == candidates.end()) {
        std::cout << "sweep_result=FAIL\n";
        std::cout << "reason=default_candidate_missing\n";
        if (opts.json_out_set) {
            write_sweep_json(opts.json_out, profile, scenarios, margin, fuel_ratio_cap, 0.0,
                             candidates, nullptr, "default candidate missing", false);
            std::cout << "json_out=" << opts.json_out << "\n";
        }
        return 1;
    }

    const double default_mean_fuel = default_it->mean_fuel_used_kg;
    const double fuel_limit = default_mean_fuel * fuel_ratio_cap;

    for (CandidateEval& c : candidates) {
        c.fuel_ratio_to_default = (default_mean_fuel > cascade::EPS_NUM)
            ? (c.mean_fuel_used_kg / default_mean_fuel)
            : 0.0;

        c.pass_fuel_ratio = (c.mean_fuel_used_kg <= fuel_limit + 1e-12);
        c.pass = c.pass_all_scenarios && c.pass_fuel_ratio;

        if (!c.pass_all_scenarios) {
            c.fail_reason = "scenario criteria not met";
        } else if (!c.pass_fuel_ratio) {
            c.fail_reason = "fuel ratio cap exceeded";
        }

        std::cout << "candidate=" << c.name
                  << " pass=" << (c.pass ? "true" : "false")
                  << " scenario_pass_count=" << c.scenario_pass_count << "/" << scenarios
                  << " mean_delta_slot_error=" << c.mean_delta_slot_error
                  << " mean_fuel_used_kg=" << c.mean_fuel_used_kg
                  << " max_request_ratio=" << c.gains.max_request_ratio
                  << " fuel_ratio_to_default=" << c.fuel_ratio_to_default
                  << "\n";
    }

    std::vector<const CandidateEval*> passing;
    passing.reserve(candidates.size());
    for (const CandidateEval& c : candidates) {
        if (c.pass) {
            passing.push_back(&c);
        }
    }

    std::sort(passing.begin(), passing.end(), [](const CandidateEval* a, const CandidateEval* b) {
        if (a->mean_delta_slot_error != b->mean_delta_slot_error) {
            return a->mean_delta_slot_error < b->mean_delta_slot_error;
        }
        if (a->mean_fuel_used_kg != b->mean_fuel_used_kg) {
            return a->mean_fuel_used_kg < b->mean_fuel_used_kg;
        }
        return a->name < b->name;
    });

    const CandidateEval* selected = passing.empty() ? nullptr : passing.front();

    bool success = (selected != nullptr);
    std::string selection_reason;
    if (success) {
        selection_reason = "selected by ranking rule over passing candidates";
        std::cout << "selected_candidate=" << selected->name << "\n";
        std::cout << "selected_mean_delta_slot_error=" << selected->mean_delta_slot_error << "\n";
        std::cout << "selected_mean_fuel_used_kg=" << selected->mean_fuel_used_kg << "\n";
        std::cout << "selected_fuel_ratio_to_default=" << selected->fuel_ratio_to_default << "\n";
    } else {
        selection_reason = "no candidate met strict scenario + fuel-ratio criteria";
        std::cout << "selected_candidate=NONE\n";
    }

    std::cout << "default_candidate_mean_fuel_used_kg=" << default_mean_fuel << "\n";
    std::cout << "fuel_limit_mean_fuel_used_kg=" << fuel_limit << "\n";

    if (opts.json_out_set) {
        write_sweep_json(opts.json_out, profile, scenarios, margin, fuel_ratio_cap, default_mean_fuel,
                         candidates, selected, selection_reason, success);
        std::cout << "json_out=" << opts.json_out << "\n";
    }

    if (!success) {
        std::cout << "recovery_slot_sweep_result=FAIL\n";
        std::cout << "reason=" << selection_reason << "\n";
        return 1;
    }

    std::cout << "recovery_slot_sweep_result=PASS\n";
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    GateOptions opts;
    if (!parse_args(argc, argv, opts)) {
        print_usage(argv[0]);
        return 2;
    }

    if (opts.sweep) {
        return run_sweep_mode(opts);
    }
    return run_default_gate(opts);
}
