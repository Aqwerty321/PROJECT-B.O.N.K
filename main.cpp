// ---------------------------------------------------------------------------
// CASCADE (Project BONK) — API server
// ---------------------------------------------------------------------------

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <cstdio>
#include <vector>
#include <cstdint>

#include <jluna.hpp>
#include <httplib.h>
#include <simdjson.h>
#include <boost/version.hpp>

#include "types.hpp"
#include "json_util.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "telemetry.hpp"
#include "simulation_engine.hpp"

static cascade::StateStore  g_store;
static cascade::SimClock    g_clock;
static std::shared_mutex    g_mutex;
static std::atomic<int64_t> g_tick_count{0};

struct PropagationStats {
    std::uint64_t fast_last_tick = 0;
    std::uint64_t rk4_last_tick = 0;
    std::uint64_t escalated_last_tick = 0;

    std::uint64_t broad_pairs_last_tick = 0;
    std::uint64_t broad_candidates_last_tick = 0;
    std::uint64_t broad_overlap_pass_last_tick = 0;
    std::uint64_t broad_dcriterion_rejected_last_tick = 0;
    std::uint64_t broad_fail_open_objects_last_tick = 0;
    std::uint64_t broad_fail_open_satellites_last_tick = 0;
    double broad_shell_margin_km_last_tick = 0.0;

    std::uint64_t fast_total = 0;
    std::uint64_t rk4_total = 0;
    std::uint64_t escalated_total = 0;

    std::uint64_t broad_pairs_total = 0;
    std::uint64_t broad_candidates_total = 0;
    std::uint64_t broad_overlap_pass_total = 0;
    std::uint64_t broad_dcriterion_rejected_total = 0;
    std::uint64_t broad_fail_open_objects_total = 0;
    std::uint64_t broad_fail_open_satellites_total = 0;
};

static PropagationStats g_prop_stats;

static void set_error_json(httplib::Response& res,
                           int http_status,
                           std::string_view code,
                           std::string_view message)
{
    std::string out;
    out.reserve(96 + code.size() + message.size());
    out += "{\"status\":\"ERROR\",\"code\":";
    cascade::append_json_string(out, code);
    out += ",\"message\":";
    cascade::append_json_string(out, message);
    out += '}';
    res.status = http_status;
    res.set_content(out, "application/json");
}

static std::string get_source_id(const httplib::Request& req)
{
    const std::string source = req.get_header_value("X-Source-Id");
    if (source.empty()) return "unknown";
    return source;
}

static std::string build_snapshot(const cascade::StateStore& store,
                                  const cascade::SimClock&   clock)
{
    std::string out;
    out.reserve(store.satellite_count() * 128 + store.debris_count() * 56 + 128);

    out += "{\"timestamp\":";
    cascade::append_json_string(out, clock.to_iso());
    out += ",\"satellites\":[";

    bool first_sat = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != cascade::ObjectType::SATELLITE) continue;
        if (!first_sat) out.push_back(',');
        first_sat = false;

        out += "{\"id\":";
        cascade::append_json_string(out, store.id(i));
        out += ",\"lat\":0.0,\"lon\":0.0,\"fuel_kg\":";
        out += cascade::fmt_double(store.fuel_kg(i), 3);
        out += ",\"status\":";
        cascade::append_json_string(out, cascade::sat_status_str(store.sat_status(i)));
        out += '}';
    }

    out += "],\"debris_cloud\":[";
    bool first_deb = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != cascade::ObjectType::DEBRIS) continue;
        if (!first_deb) out.push_back(',');
        first_deb = false;

        out += '[';
        cascade::append_json_string(out, store.id(i));
        out += ",0.0,0.0,0.0]";
    }

    out += "]}";
    return out;
}

static std::string build_conflicts_json(const cascade::StateStore& store)
{
    const auto history = store.conflict_history_snapshot();
    const auto by_source = store.conflicts_by_source_snapshot();

    std::string out;
    out.reserve(256 + history.size() * 200 + by_source.size() * 32);

    out += "{\"status\":\"OK\",\"total_conflicts\":";
    out += std::to_string(store.total_type_conflicts());
    out += ",\"ring_size\":";
    out += std::to_string(history.size());
    out += ",\"conflicts_by_source\":{";

    bool first_src = true;
    for (const auto& kv : by_source) {
        if (!first_src) out.push_back(',');
        first_src = false;
        cascade::append_json_string(out, kv.first);
        out.push_back(':');
        out += std::to_string(kv.second);
    }

    out += "},\"recent\":[";

    bool first = true;
    for (const auto& rec : history) {
        if (!first) out.push_back(',');
        first = false;
        out += "{\"object_id\":";
        cascade::append_json_string(out, rec.object_id);
        out += ",\"stored_type\":";
        cascade::append_json_string(out, cascade::object_type_str(rec.stored_type));
        out += ",\"incoming_type\":";
        cascade::append_json_string(out, cascade::object_type_str(rec.incoming_type));
        out += ",\"telemetry_timestamp\":";
        cascade::append_json_string(out, rec.telemetry_timestamp);
        out += ",\"ingestion_timestamp\":";
        cascade::append_json_string(out, cascade::iso8601(rec.ingestion_unix_s));
        out += ",\"source_id\":";
        cascade::append_json_string(out, rec.source_id);
        out += ",\"reason\":";
        cascade::append_json_string(out, rec.reason);
        out += '}';
    }

    out += "]}";
    return out;
}

static std::string build_propagation_json(const cascade::StateStore& store,
                                          const PropagationStats& stats)
{
    std::string out;
    out.reserve(320);
    out += "{\"status\":\"OK\",\"last_tick\":{";
    out += "\"adaptive_fast\":";
    out += std::to_string(stats.fast_last_tick);
    out += ",\"adaptive_rk4\":";
    out += std::to_string(stats.rk4_last_tick);
    out += ",\"escalated_after_probe\":";
    out += std::to_string(stats.escalated_last_tick);
    out += ",\"failed_objects\":";
    out += std::to_string(store.failed_last_tick());
    out += ",\"broad_pairs_considered\":";
    out += std::to_string(stats.broad_pairs_last_tick);
    out += ",\"broad_candidates\":";
    out += std::to_string(stats.broad_candidates_last_tick);
    out += ",\"broad_shell_overlap_pass\":";
    out += std::to_string(stats.broad_overlap_pass_last_tick);
    out += ",\"broad_dcriterion_rejected\":";
    out += std::to_string(stats.broad_dcriterion_rejected_last_tick);
    out += ",\"broad_fail_open_objects\":";
    out += std::to_string(stats.broad_fail_open_objects_last_tick);
    out += ",\"broad_fail_open_satellites\":";
    out += std::to_string(stats.broad_fail_open_satellites_last_tick);
    out += ",\"broad_shell_margin_km\":";
    out += cascade::fmt_double(stats.broad_shell_margin_km_last_tick, 3);
    out += "},\"totals\":{";
    out += "\"adaptive_fast\":";
    out += std::to_string(stats.fast_total);
    out += ",\"adaptive_rk4\":";
    out += std::to_string(stats.rk4_total);
    out += ",\"escalated_after_probe\":";
    out += std::to_string(stats.escalated_total);
    out += ",\"failed_objects\":";
    out += std::to_string(store.failed_propagation_total());
    out += ",\"broad_pairs_considered\":";
    out += std::to_string(stats.broad_pairs_total);
    out += ",\"broad_candidates\":";
    out += std::to_string(stats.broad_candidates_total);
    out += ",\"broad_shell_overlap_pass\":";
    out += std::to_string(stats.broad_overlap_pass_total);
    out += ",\"broad_dcriterion_rejected\":";
    out += std::to_string(stats.broad_dcriterion_rejected_total);
    out += ",\"broad_fail_open_objects\":";
    out += std::to_string(stats.broad_fail_open_objects_total);
    out += ",\"broad_fail_open_satellites\":";
    out += std::to_string(stats.broad_fail_open_satellites_total);
    out += "},\"config\":{";
    out += "\"mode_threshold_step_seconds\":21600,";
    out += "\"mode_threshold_perigee_alt_km\":350.0,";
    out += "\"mode_threshold_e\":0.98,";
    out += "\"fast_lane_max_dt_s\":30.0,";
    out += "\"fast_lane_max_e\":0.02,";
    out += "\"fast_lane_min_perigee_alt_km\":500.0,";
    out += "\"fast_lane_ext_max_dt_s\":45.0,";
    out += "\"fast_lane_ext_max_e\":0.003,";
    out += "\"fast_lane_ext_min_perigee_alt_km\":650.0,";
    out += "\"probe_max_step_s\":120.0,";
    out += "\"probe_pos_thresh_km\":0.5,";
    out += "\"probe_vel_thresh_ms\":0.5";
    out += "}}";
    return out;
}

int main()
{
    jluna::initialize();

    std::cout << "CASCADE (Project BONK) SYSTEM ONLINE\n";
    std::cout << "Boost version : " << BOOST_LIB_VERSION << "\n";
    std::cout << "Starting HTTP server on 0.0.0.0:8000 ...\n";

    httplib::Server svr;

    // ------------------------------------------------------------------
    // POST /api/telemetry
    // ------------------------------------------------------------------
    svr.Post("/api/telemetry", [](const httplib::Request& req, httplib::Response& res) {
        const cascade::TelemetryParseResult parsed = cascade::parse_telemetry_payload(req.body);
        if (!parsed.parse_ok) {
            set_error_json(res, 400, parsed.error_code, parsed.error_message);
            return;
        }

        cascade::TelemetryIngestResult ingest;
        {
            std::unique_lock lock(g_mutex);
            ingest = cascade::apply_telemetry_batch(parsed, g_store, g_clock, get_source_id(req));
        }

        if (!ingest.ok) {
            const int status = (ingest.error_code == "STALE_TELEMETRY") ? 409 : 422;
            set_error_json(res, status, ingest.error_code, ingest.error_message);
            return;
        }

        std::string out;
        out.reserve(96);
        out += "{\"status\":\"ACK\",\"processed_count\":";
        out += std::to_string(ingest.processed_count);
        out += ",\"active_cdm_warnings\":0}";
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/maneuver/schedule
    // ------------------------------------------------------------------
    svr.Post("/api/maneuver/schedule", [](const httplib::Request& req, httplib::Response& res) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(req.body.data(), req.body.size());
        simdjson::ondemand::document doc;
        if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
            set_error_json(res, 400, "MALFORMED_JSON", "request body is not valid JSON");
            return;
        }

        simdjson::ondemand::object root;
        if (doc.get_object().get(root) != simdjson::SUCCESS) {
            set_error_json(res, 400, "INVALID_REQUEST", "root JSON value must be an object");
            return;
        }

        std::string_view sat_sv;
        if (root.find_field_unordered("satelliteId").get_string().get(sat_sv) != simdjson::SUCCESS || sat_sv.empty()) {
            set_error_json(res, 422, "MISSING_SATELLITE_ID", "field 'satelliteId' is required");
            return;
        }

        simdjson::ondemand::array burns;
        if (root.find_field_unordered("maneuver_sequence").get_array().get(burns) != simdjson::SUCCESS) {
            set_error_json(res, 422, "MISSING_MANEUVER_SEQUENCE", "field 'maneuver_sequence' must be an array");
            return;
        }

        std::size_t burn_count = 0;
        for (auto burn : burns) {
            simdjson::ondemand::object obj;
            if (burn.get_object().get(obj) == simdjson::SUCCESS) {
                ++burn_count;
            }
        }
        if (burn_count == 0) {
            set_error_json(res, 422, "EMPTY_MANEUVER_SEQUENCE", "at least one burn is required");
            return;
        }

        const std::string sat_id(sat_sv);
        double fuel_remaining = 0.0;
        double mass_remaining = 0.0;
        bool found_sat = false;

        {
            std::shared_lock lock(g_mutex);
            const std::size_t idx = g_store.find(sat_id);
            if (idx < g_store.size() && g_store.type(idx) == cascade::ObjectType::SATELLITE) {
                found_sat = true;
                fuel_remaining = g_store.fuel_kg(idx);
                mass_remaining = g_store.mass_kg(idx);
            }
        }

        if (!found_sat) {
            set_error_json(res, 404, "SATELLITE_NOT_FOUND", "satelliteId does not reference a known SATELLITE object");
            return;
        }

        const bool sufficient_fuel = (fuel_remaining > cascade::SAT_FUEL_EOL_KG);

        std::string out;
        out.reserve(192);
        out += "{\"status\":\"SCHEDULED\",\"validation\":{";
        out += "\"ground_station_los\":true,";
        out += "\"sufficient_fuel\":";
        out += (sufficient_fuel ? "true" : "false");
        out += ",\"projected_mass_remaining_kg\":";
        out += cascade::fmt_double(mass_remaining, 3);
        out += "}}";
        res.status = 202;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/simulate/step
    // ------------------------------------------------------------------
    svr.Post("/api/simulate/step", [](const httplib::Request& req, httplib::Response& res) {
        simdjson::ondemand::parser parser;
        simdjson::padded_string padded(req.body.data(), req.body.size());
        simdjson::ondemand::document doc;
        if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
            set_error_json(res, 400, "MALFORMED_JSON", "request body is not valid JSON");
            return;
        }

        simdjson::ondemand::object root;
        if (doc.get_object().get(root) != simdjson::SUCCESS) {
            set_error_json(res, 400, "INVALID_REQUEST", "root JSON value must be an object");
            return;
        }

        auto step_res = root.find_field_unordered("step_seconds").get_int64();
        if (step_res.error()) {
            set_error_json(res, 422, "MISSING_STEP_SECONDS", "field 'step_seconds' is required and must be an integer");
            return;
        }
        const int64_t step_s_i64 = step_res.value_unsafe();
        if (step_s_i64 <= 0) {
            set_error_json(res, 422, "INVALID_STEP_SECONDS", "step_seconds must be > 0");
            return;
        }

        const double step_s = static_cast<double>(step_s_i64);
        std::string new_ts;
        cascade::StepRunStats stats{};

        {
            std::unique_lock lock(g_mutex);

            if (!g_clock.is_initialized()) {
                set_error_json(res, 400, "CLOCK_UNINITIALIZED", "ingest telemetry before running simulate/step");
                return;
            }

            const bool ran = cascade::run_simulation_step(g_store, g_clock, step_s, stats);
            if (!ran) {
                set_error_json(res, 422, "SIM_STEP_REJECTED", "simulation step could not be executed");
                return;
            }

            g_prop_stats.fast_last_tick = stats.used_fast;
            g_prop_stats.rk4_last_tick = stats.used_rk4;
            g_prop_stats.escalated_last_tick = stats.escalated_after_probe;
            g_prop_stats.broad_pairs_last_tick = stats.broad_pairs_considered;
            g_prop_stats.broad_candidates_last_tick = stats.broad_candidates;
            g_prop_stats.broad_overlap_pass_last_tick = stats.broad_shell_overlap_pass;
            g_prop_stats.broad_dcriterion_rejected_last_tick = stats.broad_dcriterion_rejected;
            g_prop_stats.broad_fail_open_objects_last_tick = stats.broad_fail_open_objects;
            g_prop_stats.broad_fail_open_satellites_last_tick = stats.broad_fail_open_satellites;
            g_prop_stats.broad_shell_margin_km_last_tick = stats.broad_shell_margin_km;

            g_prop_stats.fast_total += stats.used_fast;
            g_prop_stats.rk4_total += stats.used_rk4;
            g_prop_stats.escalated_total += stats.escalated_after_probe;
            g_prop_stats.broad_pairs_total += stats.broad_pairs_considered;
            g_prop_stats.broad_candidates_total += stats.broad_candidates;
            g_prop_stats.broad_overlap_pass_total += stats.broad_shell_overlap_pass;
            g_prop_stats.broad_dcriterion_rejected_total += stats.broad_dcriterion_rejected;
            g_prop_stats.broad_fail_open_objects_total += stats.broad_fail_open_objects;
            g_prop_stats.broad_fail_open_satellites_total += stats.broad_fail_open_satellites;

            new_ts = g_clock.to_iso();
        }

        ++g_tick_count;

        std::string out;
        out.reserve(160);
        out += "{\"status\":\"STEP_COMPLETE\",\"new_timestamp\":";
        cascade::append_json_string(out, new_ts);
        out += ",\"collisions_detected\":0,\"maneuvers_executed\":0}";
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/visualization/snapshot
    // ------------------------------------------------------------------
    svr.Get("/api/visualization/snapshot", [](const httplib::Request&, httplib::Response& res) {
        std::string snapshot;
        {
            std::shared_lock lock(g_mutex);
            snapshot = build_snapshot(g_store, g_clock);
        }
        res.status = 200;
        res.set_content(snapshot, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/status
    // ------------------------------------------------------------------
    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        std::size_t obj_count = 0;
        double uptime = 0.0;
        {
            std::shared_lock lock(g_mutex);
            obj_count = g_store.size();
            uptime = g_clock.uptime_s();
        }

        std::string out;
        out.reserve(128);
        out += "{\"status\":\"NOMINAL\",\"uptime_s\":";
        out += cascade::fmt_double(uptime, 1);
        out += ",\"tick_count\":";
        out += std::to_string(g_tick_count.load());
        out += ",\"object_count\":";
        out += std::to_string(obj_count);
        out += '}';
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/debug/conflicts
    // ------------------------------------------------------------------
    svr.Get("/api/debug/conflicts", [](const httplib::Request&, httplib::Response& res) {
        std::string out;
        {
            std::shared_lock lock(g_mutex);
            out = build_conflicts_json(g_store);
        }
        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/debug/propagation
    // ------------------------------------------------------------------
    svr.Get("/api/debug/propagation", [](const httplib::Request&, httplib::Response& res) {
        std::string out;
        {
            std::shared_lock lock(g_mutex);
            out = build_propagation_json(g_store, g_prop_stats);
        }
        res.status = 200;
        res.set_content(out, "application/json");
    });

    if (!svr.listen("0.0.0.0", 8000)) {
        std::cerr << "FATAL: failed to bind to 0.0.0.0:8000\n";
        return 1;
    }

    return 0;
}
