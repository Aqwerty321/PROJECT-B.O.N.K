// ---------------------------------------------------------------------------
// api_server.cpp — route registration for CASCADE HTTP API
// ---------------------------------------------------------------------------

#include "api_server.hpp"

#include "request_parsers.hpp"
#include "response_builders.hpp"
#include "telemetry.hpp"

// Ensure std::move is declared in all toolchains.
#include <utility>

namespace cascade::http {

namespace {

std::string get_source_id(const httplib::Request& req)
{
    const std::string source = req.get_header_value("X-Source-Id");
    if (source.empty()) return "unknown";
    return source;
}

bool is_truthy(const std::string& v)
{
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

} // namespace

void register_routes(httplib::Server& server,
                     EngineRuntime& runtime)
{
    server.Post("/api/telemetry", [&](const httplib::Request& req, httplib::Response& res) {
        TelemetryRequest telemetry_req;
        ParseError parse_err;
        if (!parse_telemetry_request(req.body, telemetry_req, parse_err)) {
            set_error_json(res, parse_err.http_status, parse_err.code, parse_err.message);
            return;
        }

        const TelemetryCommandResult ingest = runtime.ingest_telemetry(
            telemetry_req.parsed,
            get_source_id(req)
        );

        if (!ingest.ok) {
            set_error_json(res, ingest.http_status, ingest.error_code, ingest.error_message);
            return;
        }

        set_telemetry_ack_json(res, ingest);
    });

    server.Post("/api/maneuver/schedule", [&](const httplib::Request& req, httplib::Response& res) {
        ScheduleRequest parsed;
        ParseError parse_err;
        if (!parse_schedule_request(req.body, parsed, parse_err)) {
            set_error_json(res, parse_err.http_status, parse_err.code, parse_err.message);
            return;
        }

        const ScheduleCommandResult scheduled = runtime.schedule_maneuver(
            parsed.satellite_id,
            std::move(parsed.burns)
        );
        if (!scheduled.ok) {
            set_error_json(res, scheduled.http_status, scheduled.error_code, scheduled.error_message);
            return;
        }

        set_schedule_ack_json(res, scheduled);
    });

    server.Post("/api/simulate/step", [&](const httplib::Request& req, httplib::Response& res) {
        StepRequest parsed;
        ParseError parse_err;
        if (!parse_step_request(req.body, parsed, parse_err)) {
            set_error_json(res, parse_err.http_status, parse_err.code, parse_err.message);
            return;
        }

        const StepCommandResult step = runtime.simulate_step(parsed.step_seconds);
        if (!step.ok) {
            set_error_json(res, step.http_status, step.error_code, step.error_message);
            return;
        }

        set_step_ack_json(res, step);
    });

    server.Get("/api/visualization/snapshot", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.snapshot_json(), "application/json");
    });

    server.Get("/api/status", [&](const httplib::Request& req, httplib::Response& res) {
        bool include_details = false;
        if (req.has_param("details")) {
            include_details = is_truthy(req.get_param_value("details"));
        }
        if (!include_details && req.has_param("verbose")) {
            include_details = is_truthy(req.get_param_value("verbose"));
        }

        res.status = 200;
        res.set_content(runtime.status_json(include_details), "application/json");
    });

    server.Get("/api/debug/conflicts", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.conflicts_json(), "application/json");
    });

    server.Get("/api/debug/propagation", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.propagation_json(), "application/json");
    });
}

} // namespace cascade::http
