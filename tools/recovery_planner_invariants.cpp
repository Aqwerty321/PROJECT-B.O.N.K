// ---------------------------------------------------------------------------
// recovery_planner_invariants.cpp
//
// Gate: enforce conservative planner invariants in deterministic scenarios.
// 1) Do not schedule recovery while collision pressure persists (auto-COLA
//    executes each tick, recovery must be deferred).
// 2) Do not schedule recovery below fuel floor.
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
    Vec3 remaining_dv{};
    double earliest_s = 0.0;
};

struct PlannerStats {
    std::uint64_t auto_planned = 0;
    std::uint64_t recovery_planned = 0;
    std::uint64_t recovery_deferred = 0;
    std::uint64_t recovery_completed = 0;
    std::uint64_t maneuvers_executed = 0;
};

struct TestOutcome {
    bool pass = false;
    std::string reason;
    PlannerStats stats{};
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

bool has_pending_burn_in_cooldown_window(const std::vector<Burn>& queue,
                                         const std::string& sat_id,
                                         double epoch_s)
{
    for (const Burn& b : queue) {
        if (b.sat_id != sat_id) continue;
        if (b.recovery) continue;
        const double dt = std::abs(b.epoch_s - epoch_s);
        if (dt + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) {
            return true;
        }
    }
    return false;
}

bool has_any_pending_burn(const std::vector<Burn>& queue,
                          const std::string& sat_id)
{
    for (const Burn& b : queue) {
        if (b.sat_id == sat_id) {
            return true;
        }
    }
    return false;
}

bool can_schedule_burn_now(const std::unordered_map<std::string, double>& last_exec,
                           const std::vector<Burn>& queue,
                           const std::string& sat_id,
                           double epoch_s)
{
    auto it = last_exec.find(sat_id);
    if (it != last_exec.end()) {
        const double dt = epoch_s - it->second;
        if (dt + cascade::EPS_NUM < cascade::SAT_COOLDOWN_S) {
            return false;
        }
    }
    if (has_pending_burn_in_cooldown_window(queue, sat_id, epoch_s)) {
        return false;
    }
    return true;
}

void apply_burn(StateStore& store, const Burn& burn)
{
    const std::size_t idx = store.find(burn.sat_id);
    if (idx >= store.size() || store.type(idx) != ObjectType::SATELLITE) return;

    const double mass_before = store.mass_kg(idx);
    const double fuel_before = store.fuel_kg(idx);
    const double fuel_needed = propellant_used_kg(mass_before, burn.dv_norm_km_s);
    if (fuel_needed > fuel_before + cascade::EPS_NUM) return;

    store.vx_mut(idx) += burn.dv.x;
    store.vy_mut(idx) += burn.dv.y;
    store.vz_mut(idx) += burn.dv.z;
    store.set_elements(idx, cascade::OrbitalElements{}, false);

    const double fuel_after = std::max(0.0, fuel_before - fuel_needed);
    const double mass_after = std::max(cascade::SAT_DRY_MASS_KG, mass_before - fuel_needed);
    store.fuel_kg_mut(idx) = fuel_after;
    store.mass_kg_mut(idx) = mass_after;
}

void execute_due_maneuvers(StateStore& store,
                           double epoch_s,
                           std::vector<Burn>& queue,
                           std::unordered_map<std::string, double>& last_exec,
                           std::unordered_map<std::string, RecoveryRequest>& recovery_requests,
                           PlannerStats& stats)
{
    std::vector<Burn> pending;
    pending.reserve(queue.size());

    for (const Burn& burn : queue) {
        if (burn.epoch_s > epoch_s + cascade::EPS_NUM) {
            pending.push_back(burn);
            continue;
        }

        apply_burn(store, burn);
        last_exec[burn.sat_id] = burn.epoch_s;
        ++stats.maneuvers_executed;

        if (burn.recovery) {
            ++stats.recovery_completed;
            recovery_requests.erase(burn.sat_id);
            continue;
        }

        RecoveryRequest& req = recovery_requests[burn.sat_id];
        req.remaining_dv.x -= burn.dv.x;
        req.remaining_dv.y -= burn.dv.y;
        req.remaining_dv.z -= burn.dv.z;
        req.earliest_s = std::max(req.earliest_s, burn.epoch_s + cascade::SAT_COOLDOWN_S);
    }

    queue.swap(pending);
}

void plan_recovery_burns(StateStore& store,
                         double epoch_s,
                         std::uint64_t tick_id,
                         std::vector<Burn>& queue,
                         const std::unordered_map<std::string, double>& last_exec,
                         std::unordered_map<std::string, RecoveryRequest>& recovery_requests,
                         PlannerStats& stats)
{
    std::vector<std::string> ready;
    ready.reserve(recovery_requests.size());
    for (const auto& kv : recovery_requests) {
        if (kv.second.earliest_s <= epoch_s + cascade::EPS_NUM) {
            ready.push_back(kv.first);
        }
    }

    for (const std::string& sat_id : ready) {
        auto it = recovery_requests.find(sat_id);
        if (it == recovery_requests.end()) continue;

        const std::size_t idx = store.find(sat_id);
        if (idx >= store.size() || store.type(idx) != ObjectType::SATELLITE) {
            recovery_requests.erase(it);
            continue;
        }

        if (has_any_pending_burn(queue, sat_id)) {
            ++stats.recovery_deferred;
            continue;
        }

        if (!can_schedule_burn_now(last_exec, queue, sat_id, epoch_s)) {
            ++stats.recovery_deferred;
            continue;
        }

        const Vec3 req_dv = it->second.remaining_dv;
        const double req_norm = vec_norm(req_dv);
        if (req_norm <= cascade::EPS_NUM) {
            recovery_requests.erase(it);
            continue;
        }

        const double cap = cascade::SAT_MAX_DELTAV_KM_S;
        const double scale = (req_norm > cap) ? (cap / req_norm) : 1.0;
        const Vec3 dv_cmd{req_dv.x * scale, req_dv.y * scale, req_dv.z * scale};
        const double dv_norm = vec_norm(dv_cmd);

        const double fuel_before = store.fuel_kg(idx);
        const double mass_before = store.mass_kg(idx);
        const double fuel_need = propellant_used_kg(mass_before, dv_norm);
        if (fuel_before - fuel_need <= cascade::SAT_FUEL_EOL_KG + cascade::EPS_NUM) {
            ++stats.recovery_deferred;
            continue;
        }

        Burn burn;
        burn.sat_id = sat_id;
        burn.epoch_s = epoch_s;
        burn.dv = dv_cmd;
        burn.dv_norm_km_s = dv_norm;
        burn.recovery = true;
        queue.push_back(burn);

        it->second.remaining_dv.x -= dv_cmd.x;
        it->second.remaining_dv.y -= dv_cmd.y;
        it->second.remaining_dv.z -= dv_cmd.z;
        it->second.earliest_s = epoch_s + cascade::SAT_COOLDOWN_S;
        if (vec_norm(it->second.remaining_dv) <= 1e-6) {
            recovery_requests.erase(it);
        }

        ++stats.recovery_planned;
        (void)tick_id;
    }
}

bool maybe_queue_auto_cola(StateStore& store,
                           double epoch_s,
                           std::vector<Burn>& queue,
                           const std::unordered_map<std::string, double>& last_exec)
{
    const std::size_t sat_idx = store.find("SAT-INV-1");
    const std::size_t deb_idx = store.find("DEB-INV-1");
    if (sat_idx >= store.size() || deb_idx >= store.size()) return false;

    const double dx = store.rx(sat_idx) - store.rx(deb_idx);
    const double dy = store.ry(sat_idx) - store.ry(deb_idx);
    const double dz = store.rz(sat_idx) - store.rz(deb_idx);
    const double d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > cascade::COLLISION_THRESHOLD_KM * cascade::COLLISION_THRESHOLD_KM) {
        return false;
    }

    if (!can_schedule_burn_now(last_exec, queue, "SAT-INV-1", epoch_s)) {
        return false;
    }

    const Vec3 v{store.vx(sat_idx), store.vy(sat_idx), store.vz(sat_idx)};
    const double vn = vec_norm(v);
    if (vn < cascade::EPS_NUM) return false;

    constexpr double k_auto_dv_km_s = 0.001;
    const double fuel_before = store.fuel_kg(sat_idx);
    const double mass_before = store.mass_kg(sat_idx);
    const double fuel_need = propellant_used_kg(mass_before, k_auto_dv_km_s);
    if (fuel_before - fuel_need <= cascade::SAT_FUEL_EOL_KG + cascade::EPS_NUM) {
        return false;
    }

    Burn burn;
    burn.sat_id = "SAT-INV-1";
    burn.epoch_s = epoch_s;
    burn.dv = Vec3{(v.x / vn) * k_auto_dv_km_s,
                   (v.y / vn) * k_auto_dv_km_s,
                   (v.z / vn) * k_auto_dv_km_s};
    burn.dv_norm_km_s = k_auto_dv_km_s;
    burn.recovery = false;
    queue.push_back(burn);
    return true;
}

void force_persistent_collision(StateStore& store)
{
    const std::size_t sat_idx = store.find("SAT-INV-1");
    const std::size_t deb_idx = store.find("DEB-INV-1");
    if (sat_idx >= store.size() || deb_idx >= store.size()) return;

    store.rx_mut(deb_idx) = store.rx(sat_idx);
    store.ry_mut(deb_idx) = store.ry(sat_idx);
    store.rz_mut(deb_idx) = store.rz(sat_idx);
    store.vx_mut(deb_idx) = store.vx(sat_idx);
    store.vy_mut(deb_idx) = store.vy(sat_idx);
    store.vz_mut(deb_idx) = store.vz(sat_idx);
    store.set_elements(deb_idx, cascade::OrbitalElements{}, false);
}

TestOutcome test_persistent_collision_blocks_recovery()
{
    TestOutcome out;

    StateStore store;
    cascade::SimClock clock;
    StepRunConfig cfg{};
    cfg.broad_phase.enable_dcriterion = false;

    const double t0 = 1773302400.0;
    clock.set_epoch_s(t0);

    bool conflict = false;
    (void)store.upsert("SAT-INV-1", ObjectType::SATELLITE,
                       7000.0, 0.0, 0.0,
                       0.0, 7.5, 0.0,
                       t0,
                       conflict);
    (void)store.upsert("DEB-INV-1", ObjectType::DEBRIS,
                       7000.0, 0.0, 0.0,
                       0.0, 7.5, 0.0,
                       t0,
                       conflict);

    std::vector<Burn> queue;
    std::unordered_map<std::string, double> last_exec;
    std::unordered_map<std::string, RecoveryRequest> recovery_requests;

    for (int k = 0; k < 8; ++k) {
        StepRunStats step{};
        if (!cascade::run_simulation_step(store, clock, 600.0, step, cfg)) {
            out.reason = "simulation step failed";
            return out;
        }

        force_persistent_collision(store);

        if (maybe_queue_auto_cola(store, clock.epoch_s(), queue, last_exec)) {
            ++out.stats.auto_planned;
        }

        execute_due_maneuvers(
            store,
            clock.epoch_s(),
            queue,
            last_exec,
            recovery_requests,
            out.stats
        );

        plan_recovery_burns(
            store,
            clock.epoch_s(),
            static_cast<std::uint64_t>(k + 1),
            queue,
            last_exec,
            recovery_requests,
            out.stats
        );
    }

    if (out.stats.auto_planned == 0) {
        out.reason = "auto-COLA was never triggered under persistent collision pressure";
        return out;
    }
    if (out.stats.recovery_planned != 0) {
        out.reason = "recovery was planned while collision pressure persisted";
        return out;
    }
    if (out.stats.recovery_completed != 0) {
        out.reason = "recovery completed under persistent collision pressure";
        return out;
    }
    if (recovery_requests.empty()) {
        out.reason = "no pending recovery request remained under persistent collision pressure";
        return out;
    }

    out.pass = true;
    return out;
}

TestOutcome test_fuel_floor_blocks_recovery()
{
    TestOutcome out;

    StateStore store;
    const double t0 = 1773302400.0;
    bool conflict = false;
    (void)store.upsert("SAT-INV-FUEL", ObjectType::SATELLITE,
                       7000.0, 0.0, 0.0,
                       0.0, 7.5, 0.0,
                       t0,
                       conflict);

    const std::size_t idx = store.find("SAT-INV-FUEL");
    if (idx >= store.size()) {
        out.reason = "failed to initialize satellite";
        return out;
    }

    store.fuel_kg_mut(idx) = cascade::SAT_FUEL_EOL_KG;

    std::vector<Burn> queue;
    std::unordered_map<std::string, double> last_exec;
    std::unordered_map<std::string, RecoveryRequest> recovery_requests;
    recovery_requests["SAT-INV-FUEL"] = RecoveryRequest{
        Vec3{0.001, 0.0, 0.0},
        t0
    };

    plan_recovery_burns(
        store,
        t0,
        1,
        queue,
        last_exec,
        recovery_requests,
        out.stats
    );

    if (out.stats.recovery_planned != 0) {
        out.reason = "recovery planned despite fuel floor";
        return out;
    }
    if (out.stats.recovery_deferred == 0) {
        out.reason = "fuel-floor recovery request was not deferred";
        return out;
    }
    if (!queue.empty()) {
        out.reason = "recovery burn queued despite fuel floor";
        return out;
    }

    out.pass = true;
    return out;
}

} // namespace

int main()
{
    const TestOutcome persistent = test_persistent_collision_blocks_recovery();
    const TestOutcome fuel_floor = test_fuel_floor_blocks_recovery();

    std::cout << "recovery_planner_invariants_gate\n";
    std::cout << "persistent_collision_result=" << (persistent.pass ? "PASS" : "FAIL") << "\n";
    std::cout << "persistent_collision_auto_planned=" << persistent.stats.auto_planned << "\n";
    std::cout << "persistent_collision_recovery_planned=" << persistent.stats.recovery_planned << "\n";
    std::cout << "persistent_collision_recovery_deferred=" << persistent.stats.recovery_deferred << "\n";
    std::cout << "persistent_collision_recovery_completed=" << persistent.stats.recovery_completed << "\n";
    if (!persistent.pass) {
        std::cout << "persistent_collision_reason=" << persistent.reason << "\n";
    }

    std::cout << "fuel_floor_result=" << (fuel_floor.pass ? "PASS" : "FAIL") << "\n";
    std::cout << "fuel_floor_recovery_planned=" << fuel_floor.stats.recovery_planned << "\n";
    std::cout << "fuel_floor_recovery_deferred=" << fuel_floor.stats.recovery_deferred << "\n";
    if (!fuel_floor.pass) {
        std::cout << "fuel_floor_reason=" << fuel_floor.reason << "\n";
    }

    if (!persistent.pass || !fuel_floor.pass) {
        std::cout << "recovery_planner_invariants_result=FAIL\n";
        return 1;
    }

    std::cout << "recovery_planner_invariants_result=PASS\n";
    return 0;
}
