// ---------------------------------------------------------------------------
// CASCADE (Project BONK) — API server
// ---------------------------------------------------------------------------

#include <iostream>
#include <string>
#include <string_view>
#include <cstdint>
#include <utility>
#include <vector>

#if PROJECTBONK_ENABLE_JULIA_RUNTIME
#include <jluna.hpp>
#endif
#include <httplib.h>
#include <simdjson.h>
#include <boost/version.hpp>

#include "engine_runtime.hpp"
#include "json_util.hpp"
#include "telemetry.hpp"
#include "types.hpp"

namespace {

void set_error_json(httplib::Response& res,
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

std::string get_source_id(const httplib::Request& req)
{
    const std::string source = req.get_header_value("X-Source-Id");
    if (source.empty()) return "unknown";
    return source;
}

bool parse_vec3_field(simdjson::ondemand::object& parent,
                      const char* key,
                      cascade::Vec3& out)
{
    simdjson::ondemand::object obj;
    if (parent.find_field_unordered(key).get_object().get(obj) != simdjson::SUCCESS) {
        return false;
    }

    auto x = obj.find_field_unordered("x").get_double();
    auto y = obj.find_field_unordered("y").get_double();
    auto z = obj.find_field_unordered("z").get_double();
    if (x.error() || y.error() || z.error()) {
        return false;
    }

    out.x = x.value_unsafe();
    out.y = y.value_unsafe();
    out.z = z.value_unsafe();
    return true;
}

} // namespace

int main()
{
#if PROJECTBONK_ENABLE_JULIA_RUNTIME
    jluna::initialize();
#endif

    std::cout << "CASCADE (Project BONK) SYSTEM ONLINE\n";
    std::cout << "Boost version : " << BOOST_LIB_VERSION << "\n";
    std::cout << "Starting HTTP server on 0.0.0.0:8000 ...\n";

    cascade::EngineRuntime runtime;
    httplib::Server svr;

    // ------------------------------------------------------------------
    // POST /api/telemetry
    // ------------------------------------------------------------------
    svr.Post("/api/telemetry", [&](const httplib::Request& req, httplib::Response& res) {
        const cascade::TelemetryParseResult parsed = cascade::parse_telemetry_payload(req.body);
        const cascade::TelemetryCommandResult ingest = runtime.ingest_telemetry(parsed, get_source_id(req));

        if (!ingest.ok) {
            set_error_json(res, ingest.http_status, ingest.error_code, ingest.error_message);
            return;
        }

        std::string out;
        out.reserve(96);
        out += "{\"status\":\"ACK\",\"processed_count\":";
        out += std::to_string(ingest.processed_count);
        out += ",\"active_cdm_warnings\":";
        out += std::to_string(ingest.active_cdm_warnings);
        out += '}';

        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/maneuver/schedule
    // ------------------------------------------------------------------
    svr.Post("/api/maneuver/schedule", [&](const httplib::Request& req, httplib::Response& res) {
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

        std::vector<cascade::ScheduledBurn> parsed_burns;
        parsed_burns.reserve(8);

        for (auto burn_value : burns) {
            simdjson::ondemand::object burn_obj;
            if (burn_value.get_object().get(burn_obj) != simdjson::SUCCESS) {
                set_error_json(res, 422, "INVALID_BURN", "each maneuver entry must be an object");
                return;
            }

            std::string_view burn_id_sv;
            if (burn_obj.find_field_unordered("burn_id").get_string().get(burn_id_sv) != simdjson::SUCCESS || burn_id_sv.empty()) {
                set_error_json(res, 422, "MISSING_BURN_ID", "each burn must include non-empty 'burn_id'");
                return;
            }

            std::string_view burn_time_sv;
            if (burn_obj.find_field_unordered("burnTime").get_string().get(burn_time_sv) != simdjson::SUCCESS || burn_time_sv.empty()) {
                set_error_json(res, 422, "MISSING_BURN_TIME", "each burn must include non-empty 'burnTime'");
                return;
            }

            double burn_epoch_s = 0.0;
            if (!cascade::parse_iso8601(burn_time_sv, burn_epoch_s)) {
                set_error_json(res, 422, "INVALID_BURN_TIME", "burnTime must be ISO-8601 UTC");
                return;
            }

            cascade::Vec3 dv{};
            if (!parse_vec3_field(burn_obj, "deltaV_vector", dv)) {
                set_error_json(res, 422, "MISSING_DELTAV_VECTOR", "each burn must include deltaV_vector{x,y,z}");
                return;
            }

            const double dv_norm = cascade::dv_norm_km_s(dv);
            if (dv_norm > cascade::SAT_MAX_DELTAV_KM_S + cascade::EPS_NUM) {
                set_error_json(res, 422, "DELTA_V_EXCEEDS_LIMIT", "burn deltaV exceeds 15 m/s limit");
                return;
            }

            cascade::ScheduledBurn b;
            b.id = std::string(burn_id_sv);
            b.satellite_id = std::string(sat_sv);
            b.burn_epoch_s = burn_epoch_s;
            b.delta_v_km_s = dv;
            b.delta_v_norm_km_s = dv_norm;
            parsed_burns.push_back(std::move(b));
        }

        if (parsed_burns.empty()) {
            set_error_json(res, 422, "EMPTY_MANEUVER_SEQUENCE", "at least one burn is required");
            return;
        }

        const cascade::ScheduleCommandResult scheduled = runtime.schedule_maneuver(sat_sv, std::move(parsed_burns));
        if (!scheduled.ok) {
            set_error_json(res, scheduled.http_status, scheduled.error_code, scheduled.error_message);
            return;
        }

        std::string out;
        out.reserve(192);
        out += "{\"status\":\"SCHEDULED\",\"validation\":{";
        out += "\"ground_station_los\":true,";
        out += "\"sufficient_fuel\":true";
        out += ",\"projected_mass_remaining_kg\":";
        out += cascade::fmt_double(scheduled.projected_mass_remaining_kg, 3);
        out += "}}";

        res.status = 202;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/simulate/step
    // ------------------------------------------------------------------
    svr.Post("/api/simulate/step", [&](const httplib::Request& req, httplib::Response& res) {
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

        const std::int64_t step_s_i64 = step_res.value_unsafe();
        if (step_s_i64 <= 0) {
            set_error_json(res, 422, "INVALID_STEP_SECONDS", "step_seconds must be > 0");
            return;
        }

        const cascade::StepCommandResult step = runtime.simulate_step(step_s_i64);
        if (!step.ok) {
            set_error_json(res, step.http_status, step.error_code, step.error_message);
            return;
        }

        std::string out;
        out.reserve(160);
        out += "{\"status\":\"STEP_COMPLETE\",\"new_timestamp\":";
        cascade::append_json_string(out, step.new_timestamp);
        out += ",\"collisions_detected\":";
        out += std::to_string(step.collisions_detected);
        out += ",\"maneuvers_executed\":";
        out += std::to_string(step.maneuvers_executed);
        out += '}';

        res.status = 200;
        res.set_content(out, "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/visualization/snapshot
    // ------------------------------------------------------------------
    svr.Get("/api/visualization/snapshot", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.snapshot_json(), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/status
    // ------------------------------------------------------------------
    svr.Get("/api/status", [&](const httplib::Request& req, httplib::Response& res) {
        const auto truthy = [](const std::string& v) {
            return v == "1" || v == "true" || v == "yes" || v == "on";
        };

        bool include_details = false;
        if (req.has_param("details")) {
            include_details = truthy(req.get_param_value("details"));
        }
        if (!include_details && req.has_param("verbose")) {
            include_details = truthy(req.get_param_value("verbose"));
        }

        res.status = 200;
        res.set_content(runtime.status_json(include_details), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/debug/conflicts
    // ------------------------------------------------------------------
    svr.Get("/api/debug/conflicts", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.conflicts_json(), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/debug/propagation
    // ------------------------------------------------------------------
    svr.Get("/api/debug/propagation", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.propagation_json(), "application/json");
    });

    if (!svr.listen("0.0.0.0", 8000)) {
        std::cerr << "FATAL: failed to bind to 0.0.0.0:8000\n";
        return 1;
    }

    return 0;
}
