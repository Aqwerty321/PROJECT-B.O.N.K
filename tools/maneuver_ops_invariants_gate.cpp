// ---------------------------------------------------------------------------
// maneuver_ops_invariants_gate.cpp
//
// Gate: focused deterministic checks for maneuver/upload/graveyard semantics.
// 1) A burn uploaded before blackout still executes at burn time even when
//    LOS at burn epoch is unavailable.
// 2) Pending burns with invalid/missing upload epochs are pruned.
// 3) Graveyard execution transitions satellite to OFFLINE and finalizes state.
// ---------------------------------------------------------------------------

#include "maneuver_common.hpp"
#include "maneuver_recovery_planner.hpp"
#include "orbit_math.hpp"
#include "state_store.hpp"
#include "types.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using cascade::ObjectType;
using cascade::SatStatus;
using cascade::ScheduledBurn;
using cascade::StateStore;
using cascade::Vec3;

struct ScenarioOutcome {
    bool pass = false;
    std::string reason;
};

bool seed_satellite(StateStore& store,
                    const std::string& sat_id,
                    double epoch_s,
                    std::size_t& sat_idx_out)
{
    cascade::OrbitalElements el{};
    el.a_km = 7050.0;
    el.e = 0.002;
    el.i_rad = 97.4 * cascade::PI / 180.0;
    el.raan_rad = 0.4;
    el.argp_rad = 0.7;
    el.M_rad = 0.2;
    el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
    el.p_km = el.a_km * (1.0 - el.e * el.e);
    el.rp_km = el.a_km * (1.0 - el.e);
    el.ra_km = el.a_km * (1.0 + el.e);

    Vec3 r{};
    Vec3 v{};
    if (!cascade::elements_to_eci(el, r, v)) {
        return false;
    }

    bool conflict = false;
    (void)store.upsert(
        sat_id,
        ObjectType::SATELLITE,
        r.x,
        r.y,
        r.z,
        v.x,
        v.y,
        v.z,
        epoch_s,
        conflict
    );
    if (conflict) {
        return false;
    }

    const std::size_t idx = store.find(sat_id);
    if (idx >= store.size()) {
        return false;
    }

    store.set_telemetry_epoch_s(idx, epoch_s);
    store.set_elements(idx, el, true);
    store.set_sat_status(idx, SatStatus::NOMINAL);
    sat_idx_out = idx;
    return true;
}

ScenarioOutcome test_blackout_burn_executes_when_uploaded_prior()
{
    ScenarioOutcome out;

    StateStore store;
    const std::string sat_id = "SAT-UPLINK";
    const double epoch_s = 1773302400.0;
    std::size_t sat_idx = 0;
    if (!seed_satellite(store, sat_id, epoch_s, sat_idx)) {
        out.reason = "failed to seed satellite";
        return out;
    }

    const double now_epoch_s = epoch_s;
    bool found = false;
    double burn_epoch_s = 0.0;
    double upload_epoch_s = 0.0;
    std::string upload_station;

    for (double dt = 180.0; dt <= 21600.0; dt += 20.0) {
        const double burn_candidate = now_epoch_s + dt;
        if (cascade::has_ground_station_los_for_sat_epoch(store, sat_idx, burn_candidate, nullptr)) {
            continue;
        }

        double upload_candidate = 0.0;
        std::string station_candidate;
        if (!cascade::compute_upload_plan_for_burn(
                store,
                sat_idx,
                now_epoch_s,
                burn_candidate,
                upload_candidate,
                station_candidate)) {
            continue;
        }

        burn_epoch_s = burn_candidate;
        upload_epoch_s = upload_candidate;
        upload_station = station_candidate;
        found = true;
        break;
    }

    if (!found) {
        out.reason = "could not find deterministic blackout burn with valid prior upload";
        return out;
    }

    std::vector<ScheduledBurn> burn_queue;
    ScheduledBurn burn;
    burn.id = "BURN-BLACKOUT-1";
    burn.satellite_id = sat_id;
    burn.upload_station_id = upload_station;
    burn.upload_epoch_s = upload_epoch_s;
    burn.burn_epoch_s = burn_epoch_s;
    burn.delta_v_km_s = Vec3{0.0, 0.001, 0.0};
    burn.delta_v_norm_km_s = 0.001;
    burn.auto_generated = true;
    burn.recovery_burn = false;
    burn.graveyard_burn = false;
    burn_queue.push_back(burn);

    const double fuel_before = store.fuel_kg(sat_idx);

    std::unordered_map<std::string, double> last_burn_epoch_by_sat;
    std::unordered_map<std::string, cascade::RecoveryRequest> recovery_requests_by_sat;
    std::unordered_map<std::string, bool> graveyard_requested_by_sat;
    std::unordered_map<std::string, bool> graveyard_completed_by_sat;

    const cascade::ManeuverExecStats exec = cascade::execute_due_maneuvers(
        store,
        burn_epoch_s,
        burn_queue,
        last_burn_epoch_by_sat,
        recovery_requests_by_sat,
        graveyard_requested_by_sat,
        graveyard_completed_by_sat
    );

    if (exec.executed != 1) {
        out.reason = "blackout burn was not executed";
        return out;
    }
    if (exec.upload_missed != 0) {
        out.reason = "blackout burn counted as upload miss despite prior upload";
        return out;
    }
    if (!burn_queue.empty()) {
        out.reason = "executed blackout burn remained in queue";
        return out;
    }
    if (!(store.fuel_kg(sat_idx) < fuel_before)) {
        out.reason = "fuel did not decrease after blackout burn execution";
        return out;
    }

    out.pass = true;
    return out;
}

ScenarioOutcome test_invalid_upload_epoch_is_pruned()
{
    ScenarioOutcome out;

    StateStore store;
    const std::string sat_id = "SAT-UPLOAD-PRUNE";
    const double epoch_s = 1773302400.0;
    std::size_t sat_idx = 0;
    if (!seed_satellite(store, sat_id, epoch_s, sat_idx)) {
        out.reason = "failed to seed satellite";
        return out;
    }

    std::vector<ScheduledBurn> burn_queue;
    ScheduledBurn burn;
    burn.id = "BURN-PRUNE-1";
    burn.satellite_id = sat_id;
    burn.upload_epoch_s = 0.0;
    burn.burn_epoch_s = epoch_s + 1200.0;
    burn.delta_v_km_s = Vec3{0.0, 0.0005, 0.0};
    burn.delta_v_norm_km_s = 0.0005;
    burn.auto_generated = true;
    burn_queue.push_back(burn);

    std::uint64_t upload_missed = 0;
    cascade::validate_pending_upload_windows(store, epoch_s + 10.0, burn_queue, upload_missed);

    if (upload_missed != 1) {
        out.reason = "invalid upload epoch was not counted as missed";
        return out;
    }
    if (!burn_queue.empty()) {
        out.reason = "invalid-upload burn was not pruned";
        return out;
    }

    out.pass = true;
    return out;
}

ScenarioOutcome test_graveyard_execution_transitions_offline()
{
    ScenarioOutcome out;

    StateStore store;
    const std::string sat_id = "SAT-GRAVE";
    const double epoch_s = 1773302400.0;
    std::size_t sat_idx = 0;
    if (!seed_satellite(store, sat_id, epoch_s, sat_idx)) {
        out.reason = "failed to seed satellite";
        return out;
    }

    store.fuel_kg_mut(sat_idx) = cascade::SAT_FUEL_EOL_KG;
    store.mass_kg_mut(sat_idx) = cascade::SAT_DRY_MASS_KG + cascade::SAT_FUEL_EOL_KG;
    store.set_sat_status(sat_idx, SatStatus::FUEL_LOW);

    bool found_now = false;
    double now_epoch_s = epoch_s;
    for (double dt = 0.0; dt <= 86400.0; dt += 60.0) {
        const double now_candidate = epoch_s + dt;
        double upload_epoch = 0.0;
        std::string station;
        if (cascade::compute_upload_plan_for_burn(
                store,
                sat_idx,
                now_candidate,
                now_candidate + cascade::SAT_COOLDOWN_S,
                upload_epoch,
                station)) {
            now_epoch_s = now_candidate;
            found_now = true;
            break;
        }
    }

    if (!found_now) {
        out.reason = "could not find deterministic graveyard planning epoch with upload window";
        return out;
    }

    std::vector<ScheduledBurn> burn_queue;
    std::unordered_map<std::string, bool> graveyard_requested_by_sat;
    std::unordered_map<std::string, bool> graveyard_completed_by_sat;

    const cascade::GraveyardPlanStats planned = cascade::plan_graveyard_burns(
        store,
        now_epoch_s,
        1,
        burn_queue,
        graveyard_requested_by_sat,
        graveyard_completed_by_sat
    );

    if (planned.planned != 1) {
        out.reason = "graveyard burn was not planned at EOL fuel";
        return out;
    }
    if (burn_queue.size() != 1) {
        out.reason = "graveyard planning did not enqueue exactly one burn";
        return out;
    }
    if (!burn_queue.front().graveyard_burn) {
        out.reason = "planned graveyard burn missing graveyard flag";
        return out;
    }
    if (!graveyard_requested_by_sat[sat_id]) {
        out.reason = "graveyard request flag not set";
        return out;
    }

    std::unordered_map<std::string, double> last_burn_epoch_by_sat;
    std::unordered_map<std::string, cascade::RecoveryRequest> recovery_requests_by_sat;

    const double burn_epoch_s = burn_queue.front().burn_epoch_s;
    const cascade::ManeuverExecStats exec = cascade::execute_due_maneuvers(
        store,
        burn_epoch_s,
        burn_queue,
        last_burn_epoch_by_sat,
        recovery_requests_by_sat,
        graveyard_requested_by_sat,
        graveyard_completed_by_sat
    );

    if (exec.executed != 1 || exec.graveyard_completed != 1) {
        out.reason = "graveyard burn execution counters not updated";
        return out;
    }
    if (!burn_queue.empty()) {
        out.reason = "graveyard burn remained queued after execution";
        return out;
    }
    if (store.sat_status(sat_idx) != SatStatus::OFFLINE) {
        out.reason = "satellite did not transition to OFFLINE after graveyard execution";
        return out;
    }
    if (!graveyard_completed_by_sat[sat_id]) {
        out.reason = "graveyard completion flag not set";
        return out;
    }
    if (graveyard_requested_by_sat[sat_id]) {
        out.reason = "graveyard request flag not cleared after completion";
        return out;
    }

    const bool should_again = cascade::should_request_graveyard(store, sat_idx, graveyard_completed_by_sat);
    if (should_again) {
        out.reason = "completed graveyard satellite was requested again";
        return out;
    }

    out.pass = true;
    return out;
}

ScenarioOutcome test_stationkeeping_breach_triggers_recovery_plan()
{
    ScenarioOutcome out;

    StateStore store;
    const std::string sat_id = "SAT-STATIONKEEP";
    const double epoch_s = 1773302400.0;
    std::size_t sat_idx = 0;
    if (!seed_satellite(store, sat_id, epoch_s, sat_idx)) {
        out.reason = "failed to seed satellite";
        return out;
    }

    // Seed slot reference before forcing a large position deviation.
    std::unordered_map<std::string, cascade::SlotReference> slot_reference_by_sat;
    const cascade::OrbitalElements slot_ref = cascade::derive_slot_elements_if_needed(
        store,
        sat_idx,
        slot_reference_by_sat
    );
    (void)slot_ref;

    // Force >10 km box violation while keeping the orbit state valid enough for
    // planner math; this emulates immediate post-evasion drift.
    store.rx_mut(sat_idx) += 20.0;
    store.set_elements(sat_idx, cascade::OrbitalElements{}, false);

    double radius_err_km = 0.0;
    if (!cascade::slot_radius_error_km_at_epoch(
            store,
            sat_idx,
            epoch_s,
            slot_reference_by_sat,
            radius_err_km)) {
        out.reason = "failed to evaluate slot radius error";
        return out;
    }
    if (!(radius_err_km > cascade::STATIONKEEPING_BOX_RADIUS_KM)) {
        out.reason = "stationkeeping breach not established for test satellite";
        return out;
    }

    std::vector<ScheduledBurn> burn_queue;
    std::unordered_map<std::string, double> last_burn_epoch_by_sat;
    std::unordered_map<std::string, cascade::RecoveryRequest> recovery_requests_by_sat;
    std::unordered_map<std::string, bool> graveyard_requested_by_sat;

    // Build a recovery request from current state drift and ask planner to
    // schedule corrective burn under upload/cooldown/fuel constraints.
    recovery_requests_by_sat[sat_id] = cascade::RecoveryRequest{};
    const cascade::Vec3 dv = cascade::compute_slot_target_recovery_dv(
        store,
        sat_idx,
        recovery_requests_by_sat[sat_id],
        slot_reference_by_sat
    );
    if (cascade::dv_norm_km_s(dv) <= cascade::EPS_NUM) {
        out.reason = "computed stationkeeping recovery dv is zero";
        return out;
    }
    recovery_requests_by_sat[sat_id].remaining_delta_v_km_s = dv;
    recovery_requests_by_sat[sat_id].earliest_epoch_s = epoch_s;

    // Find a deterministic epoch where upload path exists in horizon and plan.
    bool planned_any = false;
    for (double dt = 0.0; dt <= 3600.0; dt += 60.0) {
        const double now_epoch_s = epoch_s + dt;
        const cascade::RecoveryPlanStats rec = cascade::plan_recovery_burns(
            store,
            now_epoch_s,
            9,
            burn_queue,
            last_burn_epoch_by_sat,
            recovery_requests_by_sat,
            graveyard_requested_by_sat,
            slot_reference_by_sat,
            cascade::AUTO_UPLOAD_HORIZON_S
        );
        if (rec.planned > 0) {
            planned_any = true;
            break;
        }
    }

    if (!planned_any) {
        out.reason = "stationkeeping recovery burn was not planned";
        return out;
    }
    if (burn_queue.empty()) {
        out.reason = "planner reported planned recovery but queue is empty";
        return out;
    }
    if (!burn_queue.front().recovery_burn) {
        out.reason = "planned stationkeeping burn is not marked as recovery";
        return out;
    }
    if (burn_queue.front().upload_epoch_s <= 0.0) {
        out.reason = "planned stationkeeping recovery burn missing upload epoch";
        return out;
    }

    out.pass = true;
    return out;
}

} // namespace

int main()
{
    const ScenarioOutcome blackout = test_blackout_burn_executes_when_uploaded_prior();
    const ScenarioOutcome upload_prune = test_invalid_upload_epoch_is_pruned();
    const ScenarioOutcome graveyard = test_graveyard_execution_transitions_offline();
    const ScenarioOutcome stationkeeping = test_stationkeeping_breach_triggers_recovery_plan();

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "maneuver_ops_invariants_gate\n";

    std::cout << "blackout_upload_execution_result=" << (blackout.pass ? "PASS" : "FAIL") << "\n";
    if (!blackout.pass) {
        std::cout << "blackout_upload_execution_reason=" << blackout.reason << "\n";
    }

    std::cout << "upload_prune_result=" << (upload_prune.pass ? "PASS" : "FAIL") << "\n";
    if (!upload_prune.pass) {
        std::cout << "upload_prune_reason=" << upload_prune.reason << "\n";
    }

    std::cout << "graveyard_offline_transition_result=" << (graveyard.pass ? "PASS" : "FAIL") << "\n";
    if (!graveyard.pass) {
        std::cout << "graveyard_offline_transition_reason=" << graveyard.reason << "\n";
    }

    std::cout << "stationkeeping_recovery_plan_result=" << (stationkeeping.pass ? "PASS" : "FAIL") << "\n";
    if (!stationkeeping.pass) {
        std::cout << "stationkeeping_recovery_plan_reason=" << stationkeeping.reason << "\n";
    }

    const bool pass_all = blackout.pass && upload_prune.pass && graveyard.pass && stationkeeping.pass;
    std::cout << "maneuver_ops_invariants_gate_result=" << (pass_all ? "PASS" : "FAIL") << "\n";
    return pass_all ? 0 : 1;
}
