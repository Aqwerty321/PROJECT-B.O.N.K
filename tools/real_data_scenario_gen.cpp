// ---------------------------------------------------------------------------
// real_data_scenario_gen.cpp
//
// Real-data scenario benchmark:
//   - parses data.txt (Space-Track OMM JSON catalog) via simdjson
//   - extracts Keplerian elements, filters to LEO
//   - designates PAYLOAD objects as satellites, rest as debris
//   - populates StateStore with real orbital elements
//   - runs simulation ticks and reports latency + stats
//
// Usage:
//   ./real_data_scenario_gen <path_to_data.txt> [max_sats] [max_debris]
//                            [warmup] [measure] [step_s]
//
// If max_sats/max_debris are 0, all matching objects are loaded.
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "orbit_math.hpp"
#include "simulation_engine.hpp"

#include <simdjson.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

double rad(double deg) {
    return deg * cascade::PI / 180.0;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double p95(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t idx = static_cast<std::size_t>(0.95 * static_cast<double>(v.size() - 1));
    return v[idx];
}

bool env_flag_enabled(const char* key, bool fallback) {
    const char* raw = std::getenv(key);
    if (!raw || raw[0] == '\0') return fallback;
    return std::atoi(raw) != 0;
}

// Read entire file into a string.
bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(out.data(), sz);
    return f.good();
}

// Parsed OMM record — minimal fields needed.
struct OmmRecord {
    std::string norad_id;
    std::string object_name;
    bool is_payload = false;     // OBJECT_TYPE == "PAYLOAD"
    double a_km      = 0.0;     // SEMIMAJOR_AXIS
    double e         = 0.0;     // ECCENTRICITY
    double i_deg     = 0.0;     // INCLINATION (degrees in OMM)
    double raan_deg  = 0.0;     // RA_OF_ASC_NODE
    double argp_deg  = 0.0;     // ARG_OF_PERICENTER
    double M_deg     = 0.0;     // MEAN_ANOMALY
    double n_rev_day = 0.0;     // MEAN_MOTION (rev/day)
    double periapsis = 0.0;     // PERIAPSIS (altitude in km)
};

} // namespace

int main(int argc, char** argv)
{
    // -----------------------------------------------------------------------
    // CLI arguments
    // -----------------------------------------------------------------------
    if (argc < 2) {
        std::cerr << "usage: real_data_scenario_gen <data.txt> "
                     "[max_sats=0] [max_debris=0] "
                     "[warmup=5] [measure=20] [step_s=30]\n";
        return 1;
    }

    const std::string data_path = argv[1];
    int max_sats     = (argc >= 3) ? std::max(0, std::atoi(argv[2])) : 0;
    int max_debris   = (argc >= 4) ? std::max(0, std::atoi(argv[3])) : 0;
    int warmup_ticks = (argc >= 5) ? std::max(0, std::atoi(argv[4])) : 5;
    int measure_ticks= (argc >= 6) ? std::max(1, std::atoi(argv[5])) : 20;
    double step_s    = (argc >= 7) ? std::max(1.0, std::atof(argv[6])): 30.0;

    // -----------------------------------------------------------------------
    // Read and parse data.txt
    // -----------------------------------------------------------------------
    std::cerr << "[info] reading " << data_path << " ...\n";
    std::string raw;
    if (!read_file(data_path, raw)) {
        std::cerr << "[error] cannot read " << data_path << "\n";
        return 1;
    }
    std::cerr << "[info] file size: " << (raw.size() / (1024 * 1024)) << " MB\n";

    // Parse with simdjson.
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded = simdjson::padded_string(raw.data(), raw.size());
    raw.clear();  // free raw memory
    raw.shrink_to_fit();

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        std::cerr << "[error] invalid JSON in " << data_path << "\n";
        return 1;
    }

    simdjson::ondemand::array arr;
    if (doc.get_array().get(arr) != simdjson::SUCCESS) {
        std::cerr << "[error] root must be a JSON array\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // Extract OMM records
    // -----------------------------------------------------------------------
    std::vector<OmmRecord> payloads;
    std::vector<OmmRecord> debris;
    payloads.reserve(4000);
    debris.reserve(30000);

    std::uint64_t total_records = 0;
    std::uint64_t skipped_parse = 0;
    std::uint64_t skipped_non_leo = 0;
    std::uint64_t skipped_invalid = 0;

    constexpr double LEO_MAX_PERIAPSIS_KM = 2000.0;  // altitude above Earth surface
    constexpr double R_EARTH_KM = 6371.0;

    for (auto item_result : arr) {
        ++total_records;
        simdjson::ondemand::object obj;
        if (item_result.get_object().get(obj) != simdjson::SUCCESS) {
            ++skipped_parse;
            continue;
        }

        // Extract SEMIMAJOR_AXIS (km) — string or double in OMM JSON.
        double a_km = 0.0;
        {
            auto field = obj.find_field_unordered("SEMIMAJOR_AXIS");
            // OMM fields are typically strings in Space-Track JSON.
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                a_km = std::strtod(sv.data(), &end);
            } else {
                // Try as raw double.
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) {
                    a_km = d;
                }
            }
        }

        double ecc = 0.0;
        {
            auto field = obj.find_field_unordered("ECCENTRICITY");
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                ecc = std::strtod(sv.data(), &end);
            } else {
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) ecc = d;
            }
        }

        double inc_deg = 0.0;
        {
            auto field = obj.find_field_unordered("INCLINATION");
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                inc_deg = std::strtod(sv.data(), &end);
            } else {
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) inc_deg = d;
            }
        }

        double raan_deg = 0.0;
        {
            auto field = obj.find_field_unordered("RA_OF_ASC_NODE");
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                raan_deg = std::strtod(sv.data(), &end);
            } else {
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) raan_deg = d;
            }
        }

        double argp_deg = 0.0;
        {
            auto field = obj.find_field_unordered("ARG_OF_PERICENTER");
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                argp_deg = std::strtod(sv.data(), &end);
            } else {
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) argp_deg = d;
            }
        }

        double M_deg = 0.0;
        {
            auto field = obj.find_field_unordered("MEAN_ANOMALY");
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                M_deg = std::strtod(sv.data(), &end);
            } else {
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) M_deg = d;
            }
        }

        double n_rev_day = 0.0;
        {
            auto field = obj.find_field_unordered("MEAN_MOTION");
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                n_rev_day = std::strtod(sv.data(), &end);
            } else {
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) n_rev_day = d;
            }
        }

        double periapsis_alt = 0.0;
        {
            auto field = obj.find_field_unordered("PERIAPSIS");
            std::string_view sv;
            if (field.get_string().get(sv) == simdjson::SUCCESS) {
                char* end = nullptr;
                periapsis_alt = std::strtod(sv.data(), &end);
            } else {
                double d;
                if (field.get_double().get(d) == simdjson::SUCCESS) periapsis_alt = d;
            }
        }

        // Validate essential fields.
        if (a_km < R_EARTH_KM || ecc < 0.0 || ecc >= 1.0 || n_rev_day <= 0.0) {
            ++skipped_invalid;
            continue;
        }

        // LEO filter: periapsis altitude < 2000 km.
        if (periapsis_alt > LEO_MAX_PERIAPSIS_KM) {
            ++skipped_non_leo;
            continue;
        }

        // Object type.
        std::string_view obj_type_sv;
        bool is_payload = false;
        if (obj.find_field_unordered("OBJECT_TYPE").get_string().get(obj_type_sv) == simdjson::SUCCESS) {
            is_payload = (obj_type_sv == "PAYLOAD");
        }

        // NORAD catalog ID as unique identifier.
        std::string norad_id;
        {
            std::string_view sv;
            if (obj.find_field_unordered("NORAD_CAT_ID").get_string().get(sv) == simdjson::SUCCESS) {
                norad_id = std::string(sv);
            } else {
                uint64_t u;
                if (obj.find_field_unordered("NORAD_CAT_ID").get_uint64().get(u) == simdjson::SUCCESS) {
                    norad_id = std::to_string(u);
                }
            }
        }
        if (norad_id.empty()) {
            ++skipped_parse;
            continue;
        }

        // Object name (for informational purposes).
        std::string name;
        {
            std::string_view sv;
            if (obj.find_field_unordered("OBJECT_NAME").get_string().get(sv) == simdjson::SUCCESS) {
                name = std::string(sv);
            }
        }

        OmmRecord rec;
        rec.norad_id    = std::move(norad_id);
        rec.object_name = std::move(name);
        rec.is_payload  = is_payload;
        rec.a_km        = a_km;
        rec.e           = ecc;
        rec.i_deg       = inc_deg;
        rec.raan_deg    = raan_deg;
        rec.argp_deg    = argp_deg;
        rec.M_deg       = M_deg;
        rec.n_rev_day   = n_rev_day;
        rec.periapsis   = periapsis_alt;

        if (is_payload) {
            payloads.push_back(std::move(rec));
        } else {
            debris.push_back(std::move(rec));
        }
    }

    std::cerr << "[info] parsed " << total_records << " records\n";
    std::cerr << "[info] payloads (LEO): " << payloads.size() << "\n";
    std::cerr << "[info] debris   (LEO): " << debris.size() << "\n";
    std::cerr << "[info] skipped parse: " << skipped_parse
              << ", non-LEO: " << skipped_non_leo
              << ", invalid: " << skipped_invalid << "\n";

    // Apply caps.
    if (max_sats > 0 && payloads.size() > static_cast<std::size_t>(max_sats)) {
        payloads.resize(static_cast<std::size_t>(max_sats));
    }
    if (max_debris > 0 && debris.size() > static_cast<std::size_t>(max_debris)) {
        debris.resize(static_cast<std::size_t>(max_debris));
    }

    const std::size_t total_objects = payloads.size() + debris.size();
    if (total_objects == 0) {
        std::cerr << "[error] no objects to simulate\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // Populate StateStore
    // -----------------------------------------------------------------------
    cascade::StateStore store(total_objects + 128);
    cascade::SimClock clock;
    clock.set_epoch_s(1773292800.0);  // 2026-03-12T08:00:00Z

    std::uint64_t eci_failures = 0;

    auto load_record = [&](const OmmRecord& rec, cascade::ObjectType type) {
        cascade::OrbitalElements el{};
        el.a_km     = rec.a_km;
        el.e        = rec.e;
        el.i_rad    = rad(rec.i_deg);
        el.raan_rad = rad(rec.raan_deg);
        el.argp_rad = rad(rec.argp_deg);
        el.M_rad    = rad(rec.M_deg);

        // Derive mean motion from semi-major axis (Keplerian).
        el.n_rad_s  = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
        el.p_km     = el.a_km * (1.0 - el.e * el.e);
        el.rp_km    = el.a_km * (1.0 - el.e);
        el.ra_km    = el.a_km * (1.0 + el.e);

        cascade::Vec3 r{};
        cascade::Vec3 v{};
        if (!cascade::elements_to_eci(el, r, v)) {
            ++eci_failures;
            return;
        }

        const std::string id =
            (type == cascade::ObjectType::SATELLITE ? "SAT-" : "DEB-") + rec.norad_id;

        bool conflict = false;
        store.upsert(id, type, r.x, r.y, r.z, v.x, v.y, v.z,
                     clock.epoch_s(), conflict);

        const std::size_t idx = store.find(id);
        if (idx < store.size()) {
            store.set_telemetry_epoch_s(idx, clock.epoch_s());
            store.set_elements(idx, el, true);
        }
    };

    for (const auto& rec : payloads) {
        load_record(rec, cascade::ObjectType::SATELLITE);
    }
    for (const auto& rec : debris) {
        load_record(rec, cascade::ObjectType::DEBRIS);
    }

    std::cerr << "[info] loaded into StateStore: "
              << store.satellite_count() << " sats + "
              << store.debris_count() << " debris = "
              << store.size() << " total\n";
    if (eci_failures > 0) {
        std::cerr << "[warn] ECI conversion failures: " << eci_failures << "\n";
    }

    // -----------------------------------------------------------------------
    // Run simulation ticks (same reporting as phase3_tick_benchmark)
    // -----------------------------------------------------------------------
    std::vector<double> tick_ms;
    tick_ms.reserve(static_cast<std::size_t>(measure_ticks));

    std::uint64_t sum_fast = 0, sum_rk4 = 0, sum_esc = 0;
    std::uint64_t sum_narrow_pairs = 0, sum_collisions = 0, sum_maneuvers = 0;
    std::uint64_t sum_refined_pairs = 0, sum_refine_cleared = 0, sum_refine_fail_open = 0;
    std::uint64_t sum_full_refined = 0, sum_full_cleared = 0, sum_full_fail_open = 0;
    std::uint64_t sum_full_budget_alloc = 0, sum_full_budget_exhaust = 0;
    std::uint64_t sum_failed = 0;
    std::uint64_t sum_broad_pairs = 0, sum_broad_candidates = 0, sum_broad_overlap = 0;
    double sum_prop_us = 0.0, sum_broad_us = 0.0, sum_precomp_us = 0.0, sum_sweep_us = 0.0;

    cascade::StepRunConfig cfg{};
    cfg.broad_phase.enable_i_neighbor_filter =
        env_flag_enabled("PROJECTBONK_BROAD_I_NEIGHBOR_FILTER", false);

    const int total_ticks = warmup_ticks + measure_ticks;
    std::cerr << "[info] running " << total_ticks << " ticks ("
              << warmup_ticks << " warmup + " << measure_ticks
              << " measure) at step_s=" << step_s << "\n";

    for (int t = 0; t < total_ticks; ++t) {
        cascade::StepRunStats stats{};
        auto t0 = std::chrono::steady_clock::now();
        const bool ok = cascade::run_simulation_step(store, clock, step_s, stats, cfg);
        auto t1 = std::chrono::steady_clock::now();
        if (!ok) {
            std::cerr << "[error] run_simulation_step failed at tick " << t << "\n";
            return 1;
        }

        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (t >= warmup_ticks) {
            tick_ms.push_back(ms);
            sum_fast += stats.used_fast;
            sum_rk4 += stats.used_rk4;
            sum_esc += stats.escalated_after_probe;
            sum_narrow_pairs += stats.narrow_pairs_checked;
            sum_collisions += stats.collisions_detected;
            sum_maneuvers += stats.maneuvers_executed;
            sum_refined_pairs += stats.narrow_refined_pairs;
            sum_refine_cleared += stats.narrow_refine_cleared;
            sum_refine_fail_open += stats.narrow_refine_fail_open;
            sum_full_refined += stats.narrow_full_refined_pairs;
            sum_full_cleared += stats.narrow_full_refine_cleared;
            sum_full_fail_open += stats.narrow_full_refine_fail_open;
            sum_full_budget_alloc += stats.narrow_full_refine_budget_allocated;
            sum_full_budget_exhaust += stats.narrow_full_refine_budget_exhausted;
            sum_failed += stats.failed_objects;
            sum_broad_pairs += stats.broad_pairs_considered;
            sum_broad_candidates += stats.broad_candidates;
            sum_broad_overlap += stats.broad_shell_overlap_pass;
            sum_prop_us += stats.propagation_us;
            sum_broad_us += stats.broad_phase_us;
            sum_precomp_us += stats.narrow_precomp_us;
            sum_sweep_us += stats.narrow_sweep_us;
        }

        if ((t + 1) % 5 == 0) {
            std::cerr << "[tick " << (t + 1) << "/" << total_ticks
                      << "] " << std::fixed << std::setprecision(1) << ms << " ms\n";
        }
    }

    // -----------------------------------------------------------------------
    // Report (same format as phase3_tick_benchmark for comparability)
    // -----------------------------------------------------------------------
    double mean_ms = 0.0;
    for (double ms : tick_ms) mean_ms += ms;
    if (!tick_ms.empty()) mean_ms /= static_cast<double>(tick_ms.size());

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "CASCADE Real-Data Scenario Benchmark\n";
    std::cout << "data_source=" << data_path << "\n";
    std::cout << "objects_total=" << store.size() << "\n";
    std::cout << "satellites=" << store.satellite_count() << "\n";
    std::cout << "debris=" << store.debris_count() << "\n";
    std::cout << "step_seconds=" << step_s << "\n";
    std::cout << "warmup_ticks=" << warmup_ticks << "\n";
    std::cout << "measure_ticks=" << measure_ticks << "\n";
    std::cout << "broad_i_neighbor_filter="
              << (cfg.broad_phase.enable_i_neighbor_filter ? 1 : 0) << "\n";
    std::cout << "tick_ms_mean=" << mean_ms << "\n";
    std::cout << "tick_ms_median=" << median(tick_ms) << "\n";
    std::cout << "tick_ms_p95=" << p95(tick_ms) << "\n";
    std::cout << "adaptive_fast_total=" << sum_fast << "\n";
    std::cout << "adaptive_rk4_total=" << sum_rk4 << "\n";
    std::cout << "escalated_after_probe_total=" << sum_esc << "\n";
    std::cout << "narrow_pairs_checked_total=" << sum_narrow_pairs << "\n";
    std::cout << "collisions_detected_total=" << sum_collisions << "\n";
    std::cout << "maneuvers_executed_total=" << sum_maneuvers << "\n";
    std::cout << "narrow_refined_pairs_total=" << sum_refined_pairs << "\n";
    std::cout << "narrow_refine_cleared_total=" << sum_refine_cleared << "\n";
    std::cout << "narrow_refine_fail_open_total=" << sum_refine_fail_open << "\n";
    std::cout << "narrow_full_refined_pairs_total=" << sum_full_refined << "\n";
    std::cout << "narrow_full_refine_cleared_total=" << sum_full_cleared << "\n";
    std::cout << "narrow_full_refine_fail_open_total=" << sum_full_fail_open << "\n";
    std::cout << "narrow_full_refine_budget_allocated_total=" << sum_full_budget_alloc << "\n";
    std::cout << "narrow_full_refine_budget_exhausted_total=" << sum_full_budget_exhaust << "\n";
    std::cout << "failed_objects_total=" << sum_failed << "\n";
    std::cout << "broad_pairs_considered_total=" << sum_broad_pairs << "\n";
    std::cout << "broad_candidates_total=" << sum_broad_candidates << "\n";
    std::cout << "broad_shell_overlap_pass_total=" << sum_broad_overlap << "\n";

    const double n_ticks = static_cast<double>(tick_ms.size());
    if (n_ticks > 0.0) {
        std::cout << "phase_propagation_ms_mean=" << (sum_prop_us / n_ticks / 1000.0) << "\n";
        std::cout << "phase_broad_ms_mean=" << (sum_broad_us / n_ticks / 1000.0) << "\n";
        std::cout << "phase_narrow_precomp_ms_mean=" << (sum_precomp_us / n_ticks / 1000.0) << "\n";
        std::cout << "phase_narrow_sweep_ms_mean=" << (sum_sweep_us / n_ticks / 1000.0) << "\n";
        const double accounted = (sum_prop_us + sum_broad_us + sum_precomp_us + sum_sweep_us) / n_ticks / 1000.0;
        std::cout << "phase_other_ms_mean=" << (mean_ms - accounted) << "\n";
    }

    return 0;
}
