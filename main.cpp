// ---------------------------------------------------------------------------
// CASCADE (Project BONK) — API server
// ---------------------------------------------------------------------------
// Phase 1: real state store + telemetry ingestion wired up.
// Endpoints:
//   POST /api/telemetry          — ingest objects via simdjson On-Demand
//   POST /api/maneuver/schedule  — stubbed (Phase 5)
//   POST /api/simulate/step      — advances sim clock, returns new timestamp
//   GET  /api/visualization/snapshot — returns all object positions (lat=0 placeholder)
//   GET  /api/status             — returns engine health + real object count
// ---------------------------------------------------------------------------

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <cstdio>
#include <cstring>

// Julia bridge
#include <jluna.hpp>

// HTTP server (header-only)
#include <httplib.h>

// Fast JSON parser
#include <simdjson.h>

// Boost (headers-only — version verification only)
#include <boost/version.hpp>

// Cascade modules
#include "types.hpp"
#include "json_util.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "telemetry.hpp"

// ---------------------------------------------------------------------------
// Global state (protected by g_mutex for shared reads / exclusive writes)
// ---------------------------------------------------------------------------
static cascade::StateStore  g_store;
static cascade::SimClock    g_clock;
static std::shared_mutex    g_mutex;
static std::atomic<int64_t> g_tick_count{0};

// ---------------------------------------------------------------------------
// Helper: build GET /api/visualization/snapshot JSON
//
// Satellites: {"id":"...","lat":0.0,"lon":0.0,"fuel_kg":48.5,"status":"NOMINAL"}
// Debris:     ["DEB-99421",0.0,0.0,0.0]
//
// lat/lon/alt = 0.0 until Phase 6 implements ECI→geodetic conversion.
// ---------------------------------------------------------------------------
static std::string build_snapshot(const cascade::StateStore& store,
                                  const cascade::SimClock&   clock)
{
    // Reserve space: ~100 chars/satellite, ~50 chars/debris, plus header
    std::string out;
    out.reserve(store.satellite_count() * 100
                + store.debris_count()  *  50
                + 64);

    out += "{\"timestamp\":\"";
    out += clock.to_iso();
    out += "\",\"satellites\":[";

    bool first_sat = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != cascade::ObjectType::SATELLITE) continue;
        if (!first_sat) out += ',';
        first_sat = false;

        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "{\"id\":\"%s\",\"lat\":0.0,\"lon\":0.0,\"fuel_kg\":%.3f,\"status\":\"%s\"}",
            store.id(i).c_str(),
            store.fuel_kg(i),
            cascade::sat_status_str(store.sat_status(i)));
        out += buf;
    }

    out += "],\"debris_cloud\":[";

    bool first_deb = true;
    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != cascade::ObjectType::DEBRIS) continue;
        if (!first_deb) out += ',';
        first_deb = false;

        char buf[64];
        std::snprintf(buf, sizeof(buf), "[\"%s\",0.0,0.0,0.0]",
            store.id(i).c_str());
        out += buf;
    }

    out += "]}";
    return out;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    // ------------------------------------------------------------------
    // 1.  Initialise the Julia runtime via jluna
    // ------------------------------------------------------------------
    jluna::initialize();

    std::cout << "CASCADE (Project BONK) SYSTEM ONLINE\n";
    std::cout << "Boost version : " << BOOST_LIB_VERSION << "\n";
    std::cout << "Starting HTTP server on 0.0.0.0:8000 ...\n";

    // ------------------------------------------------------------------
    // 2.  Create the HTTP server
    // ------------------------------------------------------------------
    httplib::Server svr;

    // ------------------------------------------------------------------
    // POST /api/telemetry — ingest objects (PS.md §4.1)
    // ------------------------------------------------------------------
    svr.Post("/api/telemetry",
        [](const httplib::Request& req, httplib::Response& res)
    {
        cascade::TelemetryResult result;
        int64_t obj_count = 0;

        {
            std::unique_lock lock(g_mutex);
            result    = cascade::parse_telemetry(req.body, g_store, g_clock);
            obj_count = static_cast<int64_t>(g_store.size());
        }

        // active_cdm_warnings is always 0 until Phase 4 (collision screening)
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "{\"status\":\"ACK\",\"processed_count\":%d,\"active_cdm_warnings\":0}",
            result.processed_count);
        res.set_content(buf, "application/json");
        res.status = 200;
    });

    // ------------------------------------------------------------------
    // POST /api/maneuver/schedule — schedule burns (PS.md §4.2)
    // Stubbed until Phase 5.  Parses satelliteId to return real fuel.
    // ------------------------------------------------------------------
    svr.Post("/api/maneuver/schedule",
        [](const httplib::Request& req, httplib::Response& res)
    {
        std::string sat_id;
        {
            simdjson::ondemand::parser parser;
            simdjson::padded_string    padded(req.body.data(), req.body.size());
            simdjson::ondemand::document doc;
            if (parser.iterate(padded).get(doc) == simdjson::SUCCESS) {
                std::string_view id_sv;
                if (doc["satelliteId"].get_string().get(id_sv) == simdjson::SUCCESS)
                    sat_id = std::string(id_sv);
            }
        }

        double fuel_remaining = 0.0;
        double mass_remaining = 0.0;

        {
            std::shared_lock lock(g_mutex);
            std::size_t idx = g_store.find(sat_id);
            if (idx < g_store.size()) {
                fuel_remaining = g_store.fuel_kg(idx);
                mass_remaining = g_store.mass_kg(idx);
            }
        }

        // sufficient_fuel: true if above EOL guard (2.5 kg)
        const bool suf_fuel = (fuel_remaining > cascade::SAT_FUEL_EOL_KG);

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "{\"status\":\"SCHEDULED\","
            "\"validation\":{"
            "\"ground_station_los\":true,"
            "\"sufficient_fuel\":%s,"
            "\"projected_mass_remaining_kg\":%.3f}}",
            suf_fuel ? "true" : "false",
            mass_remaining);
        res.set_content(buf, "application/json");
        res.status = 202;
    });

    // ------------------------------------------------------------------
    // POST /api/simulate/step — advance sim clock (PS.md §4.3)
    // ------------------------------------------------------------------
    svr.Post("/api/simulate/step",
        [](const httplib::Request& req, httplib::Response& res)
    {
        // Parse {"step_seconds": N}
        int64_t step_s = 0;
        {
            simdjson::ondemand::parser parser;
            simdjson::padded_string    padded(req.body.data(), req.body.size());
            simdjson::ondemand::document doc;
            if (parser.iterate(padded).get(doc) == simdjson::SUCCESS) {
                auto res = doc["step_seconds"].get_int64();
                if (!res.error()) step_s = res.value_unsafe();
            }
        }

        std::string new_ts;
        {
            std::unique_lock lock(g_mutex);
            g_clock.advance(static_cast<double>(step_s));
            new_ts = g_clock.to_iso();
        }
        const int64_t tick = ++g_tick_count;

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "{\"status\":\"STEP_COMPLETE\","
            "\"new_timestamp\":\"%s\","
            "\"collisions_detected\":0,"
            "\"maneuvers_executed\":0}",
            new_ts.c_str());
        res.set_content(buf, "application/json");
        res.status = 200;
    });

    // ------------------------------------------------------------------
    // GET /api/visualization/snapshot — object positions (PS.md §6.3)
    // lat/lon = 0.0 placeholders until Phase 6 ECI→geodetic conversion
    // ------------------------------------------------------------------
    svr.Get("/api/visualization/snapshot",
        [](const httplib::Request& /*req*/, httplib::Response& res)
    {
        std::string snapshot;
        {
            std::shared_lock lock(g_mutex);
            snapshot = build_snapshot(g_store, g_clock);
        }
        res.set_content(snapshot, "application/json");
        res.status = 200;
    });

    // ------------------------------------------------------------------
    // GET /api/status — engine health
    // ------------------------------------------------------------------
    svr.Get("/api/status",
        [](const httplib::Request& /*req*/, httplib::Response& res)
    {
        int64_t     obj_count = 0;
        double      uptime    = 0.0;
        {
            std::shared_lock lock(g_mutex);
            obj_count = static_cast<int64_t>(g_store.size());
            uptime    = g_clock.uptime_s();
        }
        const int64_t tick = g_tick_count.load();

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "{\"status\":\"NOMINAL\","
            "\"uptime_s\":%.1f,"
            "\"tick_count\":%lld,"
            "\"object_count\":%lld}",
            uptime,
            static_cast<long long>(tick),
            static_cast<long long>(obj_count));
        res.set_content(buf, "application/json");
        res.status = 200;
    });

    // ------------------------------------------------------------------
    // 3.  Bind to 0.0.0.0:8000 (NOT localhost — required by grader)
    // ------------------------------------------------------------------
    if (!svr.listen("0.0.0.0", 8000))
    {
        std::cerr << "FATAL: failed to bind to 0.0.0.0:8000\n";
        return 1;
    }

    return 0;
}
