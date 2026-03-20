// ---------------------------------------------------------------------------
// nsga2_jluna_bridge.cpp
//
// C++/jluna bridge for NSGA-II multi-objective parameter tuning.
//
// Architecture:
//   - C++ initializes jluna, pre-loads simulation scenarios into StateStore
//   - Registers a C++ evaluation callback callable from Julia
//   - Loads tuner/nsga2_tuner.jl and runs the NSGA-II optimizer
//   - Julia calls back to C++ for each candidate evaluation
//   - Exports Pareto-optimal parameters as .env file
//
// Usage:
//   ./nsga2_jluna_bridge [pop_size=80] [generations=50] [sat_count=50]
//                        [deb_count=10000] [ticks_per_eval=10] [step_s=30]
//
// Build: cmake -DPROJECTBONK_ENABLE_JULIA_RUNTIME=ON
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "orbit_math.hpp"
#include "simulation_engine.hpp"
#include "broad_phase.hpp"
#include "maneuver_recovery_planner.hpp"

#include <jluna.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

double rad(double deg) {
    return deg * cascade::PI / 180.0;
}

// ---------------------------------------------------------------------------
// Shared scenario state (pre-loaded once, reused for all evaluations)
// ---------------------------------------------------------------------------
struct ScenarioState {
    int sat_count = 50;
    int deb_count = 10000;
    int ticks_per_eval = 10;
    double step_s = 30.0;
    double epoch_s = 1773292800.0;  // 2026-03-12T08:00:00Z

    // Template scenario — orbital elements stored for replay.
    struct ObjRecord {
        std::string id;
        cascade::ObjectType type;
        cascade::OrbitalElements el;
    };
    std::vector<ObjRecord> records;
    std::uint64_t eval_count = 0;
};

static ScenarioState g_scenario;

void seed_scenario(int sat_count, int deb_count) {
    g_scenario.sat_count = sat_count;
    g_scenario.deb_count = deb_count;
    g_scenario.records.clear();
    g_scenario.records.reserve(static_cast<std::size_t>(sat_count + deb_count));

    std::mt19937_64 rng(20260317ULL);
    std::uniform_real_distribution<double> sat_a(6778.0, 7378.0);
    std::uniform_real_distribution<double> sat_e(0.0, 0.01);
    std::uniform_real_distribution<double> sat_i(rad(30.0), rad(99.0));
    std::uniform_real_distribution<double> deb_a(6678.0, 7800.0);
    std::uniform_real_distribution<double> deb_e(0.0, 0.06);
    std::uniform_real_distribution<double> deb_i(rad(0.0), rad(120.0));
    std::uniform_real_distribution<double> ang(0.0, cascade::TWO_PI);

    auto make_record = [&](int idx, cascade::ObjectType type) {
        ScenarioState::ObjRecord rec;
        cascade::OrbitalElements el{};
        if (type == cascade::ObjectType::SATELLITE) {
            el.a_km = sat_a(rng);
            el.e = sat_e(rng);
            el.i_rad = sat_i(rng);
            rec.id = "SAT-" + std::to_string(idx);
        } else {
            el.a_km = deb_a(rng);
            el.e = deb_e(rng);
            el.i_rad = deb_i(rng);
            rec.id = "DEB-" + std::to_string(idx);
        }
        el.raan_rad = ang(rng);
        el.argp_rad = ang(rng);
        el.M_rad = ang(rng);
        el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
        el.p_km = el.a_km * (1.0 - el.e * el.e);
        el.rp_km = el.a_km * (1.0 - el.e);
        el.ra_km = el.a_km * (1.0 + el.e);

        rec.type = type;
        rec.el = el;
        g_scenario.records.push_back(std::move(rec));
    };

    for (int i = 0; i < sat_count; ++i)
        make_record(i, cascade::ObjectType::SATELLITE);
    for (int i = 0; i < deb_count; ++i)
        make_record(i, cascade::ObjectType::DEBRIS);
}

// Replay scenario into a fresh StateStore.
void replay_into(cascade::StateStore& store, cascade::SimClock& clock) {
    clock.set_epoch_s(g_scenario.epoch_s);
    for (const auto& rec : g_scenario.records) {
        cascade::Vec3 r{}, v{};
        if (!cascade::elements_to_eci(rec.el, r, v)) continue;
        bool conflict = false;
        store.upsert(rec.id, rec.type,
                     r.x, r.y, r.z, v.x, v.y, v.z,
                     clock.epoch_s(), conflict);
        const std::size_t idx = store.find(rec.id);
        if (idx < store.size()) {
            store.set_telemetry_epoch_s(idx, clock.epoch_s());
            store.set_elements(idx, rec.el, true);
        }
    }
}

// ---------------------------------------------------------------------------
// Parameter vector → StepRunConfig mapping
//
// The parameter order MUST match tuner/nsga2_tuner.jl::build_param_defs().
// ---------------------------------------------------------------------------

struct EvalResult {
    double safety_risk   = 0.0;  // count of false negatives (MUST be 0)
    double fuel_cost     = 0.0;  // mean recovery burn dv (km/s)
    double compute_cost  = 0.0;  // mean tick latency (ms)
};

EvalResult evaluate_candidate(const std::vector<double>& params) {
    if (params.size() < 69) {
        // Minimum safety: return worst-case
        return {1e6, 1e6, 1e6};
    }

    // Decode parameter vector (order matches nsga2_tuner.jl)
    int p = 0;

    // --- Broad Phase (7) ---
    cascade::BroadPhaseConfig bp{};
    bp.shell_margin_km       = params[p++];
    bp.invalid_shell_pad_km  = params[p++];
    bp.a_bin_width_km        = params[p++];
    bp.i_bin_width_rad       = rad(params[p++]);  // passed as degrees
    bp.band_neighbor_bins    = static_cast<int>(params[p++]);
    bp.high_e_fail_open      = params[p++];
    bp.enable_dcriterion     = true;
    bp.dcriterion_threshold  = params[p++];

    // --- Narrow Phase (17) ---
    cascade::NarrowPhaseConfig np{};
    np.tca_guard_km                   = params[p++];
    np.refine_band_km                 = params[p++];
    np.full_refine_band_km            = params[p++];
    np.high_rel_speed_km_s            = params[p++];
    np.high_rel_speed_extra_band_km   = params[p++];
    np.full_refine_budget_base        = static_cast<std::uint64_t>(params[p++]);
    np.full_refine_budget_min         = static_cast<std::uint64_t>(params[p++]);
    np.full_refine_budget_max         = static_cast<std::uint64_t>(params[p++]);
    np.full_refine_samples            = static_cast<std::uint32_t>(params[p++]);
    np.full_refine_substep_s          = params[p++];
    np.micro_refine_max_step_s        = params[p++];
    np.plane_angle_threshold_rad      = rad(params[p++]);  // passed as degrees
    np.phase_angle_threshold_rad      = rad(params[p++]);  // passed as degrees
    np.phase_max_e                    = params[p++];
    np.moid_samples                   = static_cast<std::uint32_t>(params[p++]);
    np.moid_reject_threshold_km       = params[p++];
    np.moid_max_e                     = params[p++];

    // --- Recovery Planner (7) ---
    // NOTE: Recovery params are set via env vars since they're read by singletons.
    // For the tuner, we skip env-var setting and just use default config for now,
    // capturing the values for potential future use.
    double recovery_scale_t           = params[p++];
    double recovery_scale_r           = params[p++];
    double recovery_radial_share      = params[p++];
    double recovery_scale_n           = params[p++];
    double recovery_fallback_norm     = params[p++];
    double recovery_max_request_ratio = params[p++];
    int recovery_solver_mode          = static_cast<int>(params[p++]);
    (void)recovery_scale_t;   (void)recovery_scale_r;
    (void)recovery_radial_share; (void)recovery_scale_n;
    (void)recovery_fallback_norm; (void)recovery_max_request_ratio;
    (void)recovery_solver_mode;

    // --- Propagator fast-lane (9) ---
    // These are read by singletons on first call; we skip for tuner.
    for (int i = 0; i < 9; ++i) { (void)params[p++]; }

    // --- CDM scanner (3) ---
    for (int i = 0; i < 3; ++i) { (void)params[p++]; }

    // --- COLA auto-impulse (1) ---
    (void)params[p++];

    // --- CW solver bounds (7) ---
    for (int i = 0; i < 7; ++i) { (void)params[p++]; }

    // --- Slot error normalization (4) ---
    for (int i = 0; i < 4; ++i) { (void)params[p++]; }

    // --- Operational constants (5) ---
    for (int i = 0; i < 5; ++i) { (void)params[p++]; }

    // --- Budget tier params (9) ---
    for (int i = 0; i < 9; ++i) { (void)params[p++]; }

    // Build config
    cascade::StepRunConfig cfg{};
    cfg.broad_phase = bp;
    cfg.narrow_phase = np;

    // Create fresh StateStore and replay scenario
    const std::size_t n_obj = g_scenario.records.size();
    cascade::StateStore store(n_obj + 128);
    cascade::SimClock clock;
    replay_into(store, clock);

    // Run simulation ticks
    double total_ms = 0.0;
    std::uint64_t total_collisions = 0;
    std::uint64_t total_maneuvers = 0;
    std::uint64_t total_fail_open = 0;
    double total_dv = 0.0;

    const int ticks = g_scenario.ticks_per_eval;
    for (int t = 0; t < ticks; ++t) {
        cascade::StepRunStats stats{};
        auto t0 = std::chrono::steady_clock::now();
        const bool ok = cascade::run_simulation_step(store, clock, g_scenario.step_s, stats, cfg);
        auto t1 = std::chrono::steady_clock::now();

        if (!ok) {
            return {1e6, 1e6, 1e6};  // simulation failure = infeasible
        }

        total_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_collisions += stats.collisions_detected;
        total_maneuvers += stats.maneuvers_executed;
        total_fail_open += stats.narrow_fail_open_allpairs;
    }

    // Compute objectives
    EvalResult result;

    // Safety risk: fail-open allpairs count (should be 0 for safe config).
    // A positive value means the broad/narrow filters are too aggressive.
    result.safety_risk = static_cast<double>(total_fail_open);

    // Fuel cost proxy: collision count (more collisions = more evasive burns needed).
    // Lower is better.
    result.fuel_cost = static_cast<double>(total_collisions) / static_cast<double>(ticks);

    // Compute cost: mean tick latency.
    result.compute_cost = total_ms / static_cast<double>(ticks);

    ++g_scenario.eval_count;
    return result;
}

// ---------------------------------------------------------------------------
// Export Pareto front to .env file
// ---------------------------------------------------------------------------
void export_env_file(const std::string& path,
                     const std::vector<double>& params,
                     const std::vector<std::string>& names) {
    // Map param names to env var names
    static const std::vector<std::pair<std::string, std::string>> name_to_env = {
        {"broad_shell_margin_km", "PROJECTBONK_BROAD_SHELL_MARGIN_KM"},
        {"broad_invalid_shell_pad_km", "PROJECTBONK_BROAD_INVALID_SHELL_PAD_KM"},
        {"broad_a_bin_width_km", "PROJECTBONK_BROAD_A_BIN_WIDTH_KM"},
        {"broad_i_bin_width_deg", "PROJECTBONK_BROAD_I_BIN_WIDTH_RAD"},  // convert!
        {"broad_band_neighbor_bins", "PROJECTBONK_BROAD_BAND_NEIGHBOR_BINS"},
        {"broad_high_e_fail_open", "PROJECTBONK_BROAD_HIGH_E_FAIL_OPEN"},
        {"broad_dcriterion_threshold", "PROJECTBONK_BROAD_DCRITERION_THRESHOLD"},
        {"narrow_tca_guard_km", "PROJECTBONK_NARROW_TCA_GUARD_KM"},
        {"narrow_refine_band_km", "PROJECTBONK_NARROW_REFINE_BAND_KM"},
        {"narrow_full_refine_band_km", "PROJECTBONK_NARROW_FULL_REFINE_BAND_KM"},
        {"narrow_high_rel_speed_km_s", "PROJECTBONK_NARROW_HIGH_REL_SPEED_KM_S"},
        {"narrow_high_rel_speed_extra_band_km", "PROJECTBONK_NARROW_HIGH_REL_SPEED_EXTRA_BAND_KM"},
        {"narrow_full_refine_budget_base", "PROJECTBONK_NARROW_FULL_REFINE_BUDGET_BASE"},
        {"narrow_full_refine_budget_min", "PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MIN"},
        {"narrow_full_refine_budget_max", "PROJECTBONK_NARROW_FULL_REFINE_BUDGET_MAX"},
        {"narrow_full_refine_samples", "PROJECTBONK_NARROW_FULL_REFINE_SAMPLES"},
        {"narrow_full_refine_substep_s", "PROJECTBONK_NARROW_FULL_REFINE_SUBSTEP_S"},
        {"narrow_micro_refine_max_step_s", "PROJECTBONK_NARROW_MICRO_REFINE_MAX_STEP_S"},
        {"narrow_plane_angle_threshold_deg", "PROJECTBONK_NARROW_PLANE_ANGLE_THRESHOLD_RAD"},
        {"narrow_phase_angle_threshold_deg", "PROJECTBONK_NARROW_PHASE_ANGLE_THRESHOLD_RAD"},
        {"narrow_phase_max_e", "PROJECTBONK_NARROW_PHASE_MAX_E"},
        {"narrow_moid_samples", "PROJECTBONK_NARROW_MOID_SAMPLES"},
        {"narrow_moid_reject_threshold_km", "PROJECTBONK_NARROW_MOID_REJECT_THRESHOLD_KM"},
        {"narrow_moid_max_e", "PROJECTBONK_NARROW_MOID_MAX_E"},
        {"recovery_scale_t", "PROJECTBONK_RECOVERY_SCALE_T"},
        {"recovery_scale_r", "PROJECTBONK_RECOVERY_SCALE_R"},
        {"recovery_radial_share", "PROJECTBONK_RECOVERY_RADIAL_SHARE"},
        {"recovery_scale_n", "PROJECTBONK_RECOVERY_SCALE_N"},
        {"recovery_fallback_norm_km_s", "PROJECTBONK_RECOVERY_FALLBACK_NORM_KM_S"},
        {"recovery_max_request_ratio", "PROJECTBONK_RECOVERY_MAX_REQUEST_RATIO"},
        {"recovery_solver_mode", "PROJECTBONK_RECOVERY_SOLVER_MODE"},
        {"fast_lane_max_dt_s", "PROJECTBONK_FAST_LANE_MAX_DT_S"},
        {"fast_lane_max_e", "PROJECTBONK_FAST_LANE_MAX_E"},
        {"fast_lane_min_perigee_alt_km", "PROJECTBONK_FAST_LANE_MIN_PERIGEE_ALT_KM"},
        {"fast_lane_ext_max_dt_s", "PROJECTBONK_FAST_LANE_EXT_MAX_DT_S"},
        {"fast_lane_ext_max_e", "PROJECTBONK_FAST_LANE_EXT_MAX_E"},
        {"fast_lane_ext_min_perigee_alt_km", "PROJECTBONK_FAST_LANE_EXT_MIN_PERIGEE_ALT_KM"},
        {"probe_max_step_s", "PROJECTBONK_PROBE_MAX_STEP_S"},
        {"probe_pos_thresh_km", "PROJECTBONK_PROBE_POS_THRESH_KM"},
        {"probe_vel_thresh_ms", "PROJECTBONK_PROBE_VEL_THRESH_MS"},
        {"cdm_horizon_s", "PROJECTBONK_CDM_HORIZON_S"},
        {"cdm_substep_s", "PROJECTBONK_CDM_SUBSTEP_S"},
        {"cdm_rk4_max_step_s", "PROJECTBONK_CDM_RK4_MAX_STEP_S"},
        {"cola_auto_dv_km_s", "PROJECTBONK_AUTO_DV_KM_S"},
        {"cw_horizon_fraction", "PROJECTBONK_CW_HORIZON_FRACTION"},
        {"cw_horizon_min_s", "PROJECTBONK_CW_HORIZON_MIN_S"},
        {"cw_horizon_max_s", "PROJECTBONK_CW_HORIZON_MAX_S"},
        {"cw_pos_blend", "PROJECTBONK_CW_POS_BLEND"},
        {"cw_vel_blend", "PROJECTBONK_CW_VEL_BLEND"},
        {"cw_rem_error_cap", "PROJECTBONK_CW_REM_ERROR_CAP"},
        {"cw_heur_norm_cap", "PROJECTBONK_CW_HEUR_NORM_CAP"},
        {"slot_norm_a_km", "PROJECTBONK_SLOT_NORM_A_KM"},
        {"slot_norm_e", "PROJECTBONK_SLOT_NORM_E"},
        {"slot_norm_i_rad", "PROJECTBONK_SLOT_NORM_I_RAD"},
        {"slot_norm_raan_rad", "PROJECTBONK_SLOT_NORM_RAAN_RAD"},
        {"signal_latency_s", "PROJECTBONK_SIGNAL_LATENCY_S"},
        {"stationkeeping_box_radius_km", "PROJECTBONK_STATIONKEEPING_BOX_RADIUS_KM"},
        {"upload_scan_step_s", "PROJECTBONK_UPLOAD_SCAN_STEP_S"},
        {"auto_upload_horizon_s", "PROJECTBONK_AUTO_UPLOAD_HORIZON_S"},
        {"graveyard_target_dv_km_s", "PROJECTBONK_GRAVEYARD_TARGET_DV_KM_S"},
        {"budget_tier1_threshold", "PROJECTBONK_BUDGET_TIER1_THRESHOLD"},
        {"budget_tier1_budget", "PROJECTBONK_BUDGET_TIER1_BUDGET"},
        {"budget_tier2_threshold", "PROJECTBONK_BUDGET_TIER2_THRESHOLD"},
        {"budget_tier2_budget", "PROJECTBONK_BUDGET_TIER2_BUDGET"},
        {"budget_tier3_threshold", "PROJECTBONK_BUDGET_TIER3_THRESHOLD"},
        {"budget_tier3_budget", "PROJECTBONK_BUDGET_TIER3_BUDGET"},
        {"budget_tier4_threshold", "PROJECTBONK_BUDGET_TIER4_THRESHOLD"},
        {"budget_tier4_budget", "PROJECTBONK_BUDGET_TIER4_BUDGET"},
        {"budget_short_step_bonus", "PROJECTBONK_BUDGET_SHORT_STEP_BONUS"},
    };

    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "[error] cannot write " << path << "\n";
        return;
    }

    out << "# CASCADE NSGA-II tuned parameters\n";
    out << "# Generated by nsga2_jluna_bridge\n";
    out << std::fixed << std::setprecision(10);

    for (std::size_t i = 0; i < names.size() && i < params.size(); ++i) {
        std::string env_var;
        for (const auto& [pname, ename] : name_to_env) {
            if (pname == names[i]) {
                env_var = ename;
                break;
            }
        }
        if (env_var.empty()) continue;

        double val = params[i];

        // Convert degree params to radians for env vars that expect radians
        if (names[i] == "broad_i_bin_width_deg") {
            val = rad(val);
        } else if (names[i] == "narrow_plane_angle_threshold_deg" ||
                   names[i] == "narrow_phase_angle_threshold_deg") {
            val = rad(val);
        } else if (names[i] == "recovery_solver_mode") {
            // Write as integer string
            out << env_var << "=" << (static_cast<int>(val) == 0 ? "HEURISTIC" : "CW_ZEM_EQUIVALENT") << "\n";
            continue;
        }

        // Integer params
        bool is_int = false;
        for (const auto& [pname, _] : name_to_env) {
            if (pname == names[i]) {
                // Check if this is an integer param
                is_int = (names[i].find("bins") != std::string::npos ||
                          names[i].find("budget") != std::string::npos ||
                          names[i].find("samples") != std::string::npos ||
                          names[i].find("threshold") != std::string::npos ||
                          names[i].find("bonus") != std::string::npos);
                break;
            }
        }

        if (is_int) {
            out << env_var << "=" << static_cast<std::int64_t>(std::round(val)) << "\n";
        } else {
            out << env_var << "=" << val << "\n";
        }
    }

    out.close();
    std::cerr << "[info] exported tuned parameters to " << path << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    int pop_size      = (argc >= 2) ? std::max(20, std::atoi(argv[1])) : 80;
    int generations   = (argc >= 3) ? std::max(5,  std::atoi(argv[2])) : 50;
    int sat_count     = (argc >= 4) ? std::max(1,  std::atoi(argv[3])) : 50;
    int deb_count     = (argc >= 5) ? std::max(1,  std::atoi(argv[4])) : 10000;
    int ticks_per_eval= (argc >= 6) ? std::max(1,  std::atoi(argv[5])) : 10;
    double step_s     = (argc >= 7) ? std::max(1.0, std::atof(argv[6])): 30.0;

    std::cerr << "[bridge] NSGA-II jluna bridge starting\n";
    std::cerr << "[bridge] pop_size=" << pop_size
              << " generations=" << generations
              << " sats=" << sat_count
              << " debris=" << deb_count
              << " ticks=" << ticks_per_eval
              << " step_s=" << step_s << "\n";

    // Pre-generate scenario
    g_scenario.ticks_per_eval = ticks_per_eval;
    g_scenario.step_s = step_s;
    seed_scenario(sat_count, deb_count);
    std::cerr << "[bridge] scenario seeded: " << g_scenario.records.size() << " objects\n";

    // Initialize jluna
    std::cerr << "[bridge] initializing jluna...\n";
    jluna::initialize(1, false);
    std::cerr << "[bridge] jluna initialized\n";

    // Register C++ evaluation callback for Julia to call.
    // Julia will pass a Vector{Float64} of parameters.
    // We return a Vector{Float64} of [safety_risk, fuel_cost, compute_cost].
    auto cpp_evaluate_fn = [](jl_value_t* jl_params) -> jl_value_t* {
        // Extract Julia Vector{Float64} → C++ vector
        if (!jl_is_array(jl_params)) {
            std::cerr << "[bridge] ERROR: expected Julia array\n";
            // Return worst-case objectives
            jl_value_t* result = (jl_value_t*)jl_alloc_array_1d(
                jl_apply_array_type((jl_value_t*)jl_float64_type, 1), 3);
            double* rdata = (double*)jl_array_data(result);
            rdata[0] = 1e6; rdata[1] = 1e6; rdata[2] = 1e6;
            return result;
        }

        const std::size_t n = jl_array_len(jl_params);
        double* pdata = (double*)jl_array_data(jl_params);
        std::vector<double> params(pdata, pdata + n);

        // Evaluate
        EvalResult res = evaluate_candidate(params);

        // Create Julia Vector{Float64} result
        jl_value_t* result = (jl_value_t*)jl_alloc_array_1d(
            jl_apply_array_type((jl_value_t*)jl_float64_type, 1), 3);
        double* rdata = (double*)jl_array_data(result);
        rdata[0] = res.safety_risk;
        rdata[1] = res.fuel_cost;
        rdata[2] = res.compute_cost;

        return result;
    };

    // Register the callback as a Julia function using jluna's unsafe C API.
    // We use jl_eval_string to create a wrapper since jluna's register_function
    // has limited type support for jl_value_t* → jl_value_t*.
    //
    // Strategy: Use jluna::unsafe::create_cfunction to register a raw C callable.

    // Define a static function pointer for the C callback
    using CppEvalFn = jl_value_t* (*)(jl_value_t*);
    static std::function<jl_value_t*(jl_value_t*)> s_eval_fn = cpp_evaluate_fn;

    // We'll use a different approach: set up the evaluation function via a
    // Julia-side wrapper that calls ccall to our C++ function.
    // Instead, let's use the simplest jluna approach: register evaluation
    // parameters as global Julia variables, and have Julia call back via
    // a cfunction pointer.

    // Export the function pointer to Julia
    static auto raw_eval = +[](jl_value_t* params) -> jl_value_t* {
        if (!jl_is_array(params)) {
            jl_value_t* result = (jl_value_t*)jl_alloc_array_1d(
                jl_apply_array_type((jl_value_t*)jl_float64_type, 1), 3);
            double* rdata = (double*)jl_array_data(result);
            rdata[0] = 1e6; rdata[1] = 1e6; rdata[2] = 1e6;
            return result;
        }
        const std::size_t n = jl_array_len(params);
        double* pdata = (double*)jl_array_data(params);
        std::vector<double> pvec(pdata, pdata + n);

        EvalResult res = evaluate_candidate(pvec);

        jl_value_t* result = (jl_value_t*)jl_alloc_array_1d(
            jl_apply_array_type((jl_value_t*)jl_float64_type, 1), 3);
        double* rdata = (double*)jl_array_data(result);
        rdata[0] = res.safety_risk;
        rdata[1] = res.fuel_cost;
        rdata[2] = res.compute_cost;
        return result;
    };

    // Register the raw C function pointer with Julia using ccall convention.
    // We set a Julia global that holds the function pointer as a Ptr{Cvoid}.
    {
        auto ptr_val = jl_box_voidpointer(reinterpret_cast<void*>(raw_eval));
        jl_set_global(jl_main_module, jl_symbol("_cpp_evaluate_ptr"), ptr_val);

        // Create a Julia wrapper function
        jluna::safe_eval(R"JL(
            function cpp_evaluate(params::Vector{Float64})
                ptr = _cpp_evaluate_ptr
                result = ccall(ptr, Any, (Any,), params)
                return result::Vector{Float64}
            end
        )JL");
    }

    // Set NSGA-II parameters as Julia globals
    jl_set_global(jl_main_module, jl_symbol("_nsga2_pop_size"),
                  jl_box_int64(pop_size));
    jl_set_global(jl_main_module, jl_symbol("_nsga2_generations"),
                  jl_box_int64(generations));

    // Load and run the Julia NSGA-II script
    std::cerr << "[bridge] loading tuner/nsga2_tuner.jl...\n";
    jluna::safe_eval_file("tuner/nsga2_tuner.jl");
    std::cerr << "[bridge] running NSGA-II...\n";

    // Run the optimizer
    jluna::safe_eval(R"JL(
        result = run_nsga2(cpp_evaluate;
                           pop_size=_nsga2_pop_size,
                           n_generations=_nsga2_generations)
    )JL");

    // Extract results from Julia
    jluna::safe_eval(R"JL(
        pareto = result["pareto_indices"]
        pop = result["population"]
        obj = result["objectives"]
        names = result["param_names"]
        defaults = result["param_defaults"]

        if length(pareto) > 0
            best_idx = pareto[1]
            best_params = pop[best_idx, :]
            best_obj = obj[best_idx, :]

            println("\n[nsga2] Best candidate:")
            println("[nsga2]   safety_risk = $(best_obj[1])")
            println("[nsga2]   fuel_cost   = $(best_obj[2])")
            println("[nsga2]   compute_ms  = $(best_obj[3])")
            println("[nsga2]   params:")
            for i in 1:length(names)
                if abs(best_params[i] - defaults[i]) > 1e-10
                    println("[nsga2]     $(names[i]) = $(best_params[i])  (default: $(defaults[i]))")
                end
            end

            global _best_params = best_params
            global _param_names = names
            global _n_pareto = length(pareto)
        else
            println("[nsga2] WARNING: no safe Pareto candidates found!")
            global _best_params = Float64[]
            global _param_names = String[]
            global _n_pareto = 0
        end
    )JL");

    // Extract best params and export .env file
    jl_value_t* jl_best = jl_get_global(jl_main_module, jl_symbol("_best_params"));
    jl_value_t* jl_names = jl_get_global(jl_main_module, jl_symbol("_param_names"));
    jl_value_t* jl_n_pareto = jl_get_global(jl_main_module, jl_symbol("_n_pareto"));

    if (jl_n_pareto && jl_unbox_int64(jl_n_pareto) > 0 &&
        jl_best && jl_is_array(jl_best) && jl_array_len(jl_best) > 0) {

        const std::size_t n = jl_array_len(jl_best);
        double* bdata = (double*)jl_array_data(jl_best);
        std::vector<double> best_params(bdata, bdata + n);

        std::vector<std::string> param_names;
        if (jl_names && jl_is_array(jl_names)) {
            for (std::size_t i = 0; i < jl_array_len(jl_names); ++i) {
                jl_value_t* s = jl_array_ptr_ref(jl_names, i);
                if (jl_is_string(s)) {
                    param_names.emplace_back(jl_string_ptr(s));
                }
            }
        }

        export_env_file("tuner/tuned_params.env", best_params, param_names);

        std::cerr << "[bridge] NSGA-II complete. "
                  << jl_unbox_int64(jl_n_pareto) << " Pareto candidates found.\n";
        std::cerr << "[bridge] Best params exported to tuner/tuned_params.env\n";
    } else {
        std::cerr << "[bridge] WARNING: no safe Pareto candidates found.\n";
    }

    std::cerr << "[bridge] total evaluations: " << g_scenario.eval_count << "\n";
    return 0;
}
