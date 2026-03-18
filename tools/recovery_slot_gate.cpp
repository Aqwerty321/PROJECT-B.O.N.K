// ---------------------------------------------------------------------------
// recovery_slot_gate.cpp
//
// Acceptance gate: slot-targeted recovery should not regress aggregate slot
// error after a bounded horizon in a deterministic synthetic scenario.
// ---------------------------------------------------------------------------

#include "state_store.hpp"
#include "sim_clock.hpp"
#include "simulation_engine.hpp"
#include "types.hpp"
#include "orbit_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
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
                             const OrbitalElements& slot)
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

    const double dv_t = (da * 6e-5) + (de * 2e-3);
    const double dv_r = de * 1e-3;
    const double dv_n = (di + d_raan) * 6e-3;

    Vec3 slot_dv{
        t_hat.x * dv_t + r_hat.x * dv_r + n_hat.x * dv_n,
        t_hat.y * dv_t + r_hat.y * dv_r + n_hat.y * dv_n,
        t_hat.z * dv_t + r_hat.z * dv_r + n_hat.z * dv_n
    };

    if (vec_norm(slot_dv) < 2e-4 && vec_norm(req.rem) > cascade::EPS_NUM) {
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

} // namespace

struct RunOutcome {
    bool ok = false;
    double start_err = 0.0;
    double end_err = 0.0;
    std::uint64_t auto_cola_queued = 0;
    std::uint64_t maneuvers_executed = 0;
    std::uint64_t recovery_planned = 0;
    std::uint64_t recovery_deferred = 0;
    std::uint64_t recovery_completed = 0;
};

RunOutcome run_scenario(bool enable_recovery)
{
    RunOutcome out;

    StateStore store;
    cascade::SimClock clock;
    StepRunConfig cfg{};
    cfg.broad_phase.enable_dcriterion = false;

    const double t0 = 1773302400.0; // 2026-03-12T08:00:00Z
    clock.set_epoch_s(t0);

    bool conflict = false;
    (void)store.upsert("SAT-GATE-1", ObjectType::SATELLITE,
                       7000.0, 0.0, 0.0,
                       0.0, 7.5, 0.0,
                       t0,
                       conflict);
    (void)store.upsert("DEB-GATE-1", ObjectType::DEBRIS,
                       7000.0, 0.0, 0.0,
                       0.0, 7.5, 0.0,
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

            Vec3 dv = slot_target_recovery_dv(store, s, it->second, slot[sat_id]);
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

    out.end_err = aggregate_slot_error(store, slot);
    out.ok = true;
    return out;
}

int main()
{
    const RunOutcome baseline = run_scenario(false);
    const RunOutcome recovered = run_scenario(true);

    if (!baseline.ok || !recovered.ok) {
        std::cout << "recovery_slot_gate\n";
        std::cout << "recovery_slot_gate_result=FAIL\n";
        return 1;
    }

    std::cout << "recovery_slot_gate\n";
    std::cout << "baseline_start_slot_error_mean=" << baseline.start_err << "\n";
    std::cout << "baseline_end_slot_error_mean=" << baseline.end_err << "\n";
    std::cout << "recovery_start_slot_error_mean=" << recovered.start_err << "\n";
    std::cout << "recovery_end_slot_error_mean=" << recovered.end_err << "\n";
    std::cout << "baseline_auto_cola_queued=" << baseline.auto_cola_queued << "\n";
    std::cout << "recovery_auto_cola_queued=" << recovered.auto_cola_queued << "\n";
    std::cout << "recovery_planned_total=" << recovered.recovery_planned << "\n";
    std::cout << "recovery_deferred_total=" << recovered.recovery_deferred << "\n";
    std::cout << "recovery_completed_total=" << recovered.recovery_completed << "\n";

    if (recovered.recovery_planned == 0 || recovered.recovery_completed == 0) {
        std::cout << "recovery_slot_gate_result=FAIL\n";
        return 1;
    }

    if (recovered.end_err > baseline.end_err + 0.1) {
        std::cout << "recovery_slot_gate_result=FAIL\n";
        return 1;
    }

    std::cout << "recovery_slot_gate_result=PASS\n";
    return 0;
}
