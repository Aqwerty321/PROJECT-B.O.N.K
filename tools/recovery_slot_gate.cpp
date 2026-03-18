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
//   recovery_slot_gate --sweep [--scenarios N] [--margin M]
// ---------------------------------------------------------------------------

#include "state_store.hpp"
#include "sim_clock.hpp"
#include "simulation_engine.hpp"
#include "types.hpp"
#include "orbit_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    int scenarios = 1;
    double margin = 0.1;
};

struct ScenarioEval {
    bool pass = false;
    std::string reason;
    RunOutcome baseline{};
    RunOutcome recovered{};
    double delta_err = 0.0;
};

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
    std::cout << "usage: " << argv0 << " [--scenarios N] [--margin M] [--sweep]\n";
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
        if (arg == "--scenarios") {
            if (i + 1 >= argc) return false;
            int value = 0;
            if (!parse_int(argv[++i], value) || value <= 0) return false;
            opts.scenarios = value;
            continue;
        }
        if (arg == "--margin") {
            if (i + 1 >= argc) return false;
            double value = 0.0;
            if (!parse_double(argv[++i], value) || value < 0.0) return false;
            opts.margin = value;
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

    if (vec_norm(slot_dv) < gains.fallback_norm_km_s && vec_norm(req.rem) > cascade::EPS_NUM) {
        slot_dv = req.rem;
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
    return {
        {"default", base},
        {"balanced_low", RecoveryGains{base.scale_t * 0.85, base.scale_r * 0.85, base.radial_share, base.scale_n * 0.85, base.fallback_norm_km_s}},
        {"balanced_high", RecoveryGains{base.scale_t * 1.15, base.scale_r * 1.15, base.radial_share, base.scale_n * 1.15, base.fallback_norm_km_s}},
        {"t_low", RecoveryGains{base.scale_t * 0.75, base.scale_r, base.radial_share, base.scale_n, base.fallback_norm_km_s}},
        {"t_high", RecoveryGains{base.scale_t * 1.25, base.scale_r, base.radial_share, base.scale_n, base.fallback_norm_km_s}},
        {"n_low", RecoveryGains{base.scale_t, base.scale_r, base.radial_share, base.scale_n * 0.75, base.fallback_norm_km_s}},
        {"n_high", RecoveryGains{base.scale_t, base.scale_r, base.radial_share, base.scale_n * 1.25, base.fallback_norm_km_s}},
        {"fallback_strict", RecoveryGains{base.scale_t, base.scale_r, base.radial_share, base.scale_n, 1.0e-4}},
        {"fallback_loose", RecoveryGains{base.scale_t, base.scale_r, base.radial_share, base.scale_n, 3.0e-4}},
    };
}

int run_default_gate(const GateOptions& opts)
{
    std::cout << "recovery_slot_gate\n";
    std::cout << "mode=acceptance\n";
    std::cout << "scenarios=" << opts.scenarios << "\n";
    std::cout << "margin=" << opts.margin << "\n";

    const RecoveryGains gains{};

    bool all_pass = true;
    std::uint64_t planned_total = 0;
    std::uint64_t completed_total = 0;
    double worst_delta = -1.0e30;
    double mean_delta = 0.0;

    for (int s = 0; s < opts.scenarios; ++s) {
        const ScenarioEval eval = evaluate_scenario(s, gains, opts.margin);
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

    mean_delta /= static_cast<double>(opts.scenarios);

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
    std::cout << "recovery_slot_sweep\n";
    std::cout << "mode=sweep\n";
    std::cout << "scenarios=" << opts.scenarios << "\n";
    std::cout << "margin=" << opts.margin << "\n";

    const std::vector<NamedGains> candidates = build_sweep_candidates();

    bool default_pass = false;
    std::string best_name;
    double best_mean_delta = std::numeric_limits<double>::infinity();

    for (const NamedGains& candidate : candidates) {
        int pass_count = 0;
        double worst_delta = -1.0e30;
        double mean_delta = 0.0;
        double mean_fuel_used = 0.0;

        for (int s = 0; s < opts.scenarios; ++s) {
            const ScenarioEval eval = evaluate_scenario(s, candidate.gains, opts.margin);
            if (eval.pass) {
                ++pass_count;
            }
            worst_delta = std::max(worst_delta, eval.delta_err);
            mean_delta += eval.delta_err;
            mean_fuel_used += eval.recovered.fuel_used_kg;
        }

        mean_delta /= static_cast<double>(opts.scenarios);
        mean_fuel_used /= static_cast<double>(opts.scenarios);

        std::cout << "candidate=" << candidate.name
                  << " pass_count=" << pass_count << "/" << opts.scenarios
                  << " worst_delta=" << worst_delta
                  << " mean_delta=" << mean_delta
                  << " mean_fuel_used_kg=" << mean_fuel_used
                  << "\n";

        const bool candidate_pass = (pass_count == opts.scenarios);
        if (candidate.name == "default") {
            default_pass = candidate_pass;
        }

        if (candidate_pass && mean_delta < best_mean_delta) {
            best_mean_delta = mean_delta;
            best_name = candidate.name;
        }
    }

    if (!best_name.empty()) {
        std::cout << "best_passing_candidate=" << best_name << "\n";
        std::cout << "best_passing_mean_delta=" << best_mean_delta << "\n";
    } else {
        std::cout << "best_passing_candidate=NONE\n";
    }

    if (!default_pass) {
        std::cout << "recovery_slot_sweep_default_result=FAIL\n";
        return 1;
    }

    std::cout << "recovery_slot_sweep_default_result=PASS\n";
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
