// ---------------------------------------------------------------------------
// api_server.cpp — route registration for CASCADE HTTP API
// ---------------------------------------------------------------------------

#include "api_server.hpp"

#include "request_parsers.hpp"
#include "response_builders.hpp"
#include "telemetry.hpp"

// Ensure std::move is declared in all toolchains.
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>

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

bool has_non_empty(std::string_view s)
{
    return !s.empty();
}

std::string trim_ascii(std::string_view raw)
{
    std::size_t begin = 0;
    std::size_t end = raw.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(raw[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1]))) {
        --end;
    }

    return std::string(raw.substr(begin, end - begin));
}

std::vector<std::string> parse_allow_origin_list(std::string_view csv)
{
    std::vector<std::string> out;

    std::size_t begin = 0;
    while (begin <= csv.size()) {
        std::size_t end = csv.find(',', begin);
        if (end == std::string_view::npos) {
            end = csv.size();
        }

        std::string entry = trim_ascii(csv.substr(begin, end - begin));
        if (!entry.empty()) {
            out.push_back(std::move(entry));
        }

        if (end == csv.size()) {
            break;
        }
        begin = end + 1;
    }

    return out;
}

std::string resolve_allow_origin(const httplib::Request& req,
                                 const ApiServerConfig& config,
                                 const std::vector<std::string>& allowed_origins)
{
    if (!config.cors_enabled) {
        return {};
    }

    const std::string origin = req.get_header_value("Origin");
    if (origin.empty()) {
        return {};
    }

    for (const std::string& allowed : allowed_origins) {
        if (allowed == "*") {
            if (config.cors_allow_credentials) {
                return origin;
            }
            return "*";
        }
        if (origin == allowed) {
            return origin;
        }
    }

    return {};
}

void apply_cors_headers(const httplib::Request& req,
                        httplib::Response& res,
                        const ApiServerConfig& config,
                        const std::vector<std::string>& allowed_origins)
{
    const std::string allow_origin = resolve_allow_origin(req, config, allowed_origins);
    if (allow_origin.empty()) {
        return;
    }

    res.set_header("Access-Control-Allow-Origin", allow_origin);
    if (allow_origin != "*") {
        res.set_header("Vary", "Origin");
    }

    if (has_non_empty(config.cors_allow_methods)) {
        res.set_header("Access-Control-Allow-Methods", config.cors_allow_methods);
    }
    if (has_non_empty(config.cors_allow_headers)) {
        res.set_header("Access-Control-Allow-Headers", config.cors_allow_headers);
    }
    if (config.cors_allow_credentials) {
        res.set_header("Access-Control-Allow-Credentials", "true");
    }
}

} // namespace

ApiServerConfig api_server_config_from_env()
{
    ApiServerConfig cfg;

    const char* cors_enable = std::getenv("PROJECTBONK_CORS_ENABLE");
    if (cors_enable != nullptr) {
        cfg.cors_enabled = is_truthy(cors_enable);
    }

    const char* cors_origin = std::getenv("PROJECTBONK_CORS_ALLOW_ORIGIN");
    if (cors_origin != nullptr && *cors_origin != '\0') {
        cfg.cors_allow_origin = cors_origin;
    }

    const char* cors_credentials = std::getenv("PROJECTBONK_CORS_ALLOW_CREDENTIALS");
    if (cors_credentials != nullptr) {
        cfg.cors_allow_credentials = is_truthy(cors_credentials);
    }

    const char* cors_methods = std::getenv("PROJECTBONK_CORS_ALLOW_METHODS");
    if (cors_methods != nullptr && *cors_methods != '\0') {
        cfg.cors_allow_methods = cors_methods;
    }

    const char* cors_headers = std::getenv("PROJECTBONK_CORS_ALLOW_HEADERS");
    if (cors_headers != nullptr && *cors_headers != '\0') {
        cfg.cors_allow_headers = cors_headers;
    }

    if (cfg.cors_enabled && cfg.cors_allow_origin.empty()) {
        cfg.cors_allow_origin = "http://localhost:5173";
    }

    return cfg;
}

void register_routes(httplib::Server& server,
                     EngineRuntime& runtime,
                     const ApiServerConfig& config)
{
    const ApiServerConfig cors_config = config;
    const std::vector<std::string> allowed_origins =
        parse_allow_origin_list(cors_config.cors_allow_origin);

    auto with_cors = [cors_config, allowed_origins](const httplib::Request& req, httplib::Response& res) {
        apply_cors_headers(req, res, cors_config, allowed_origins);
    };

    server.Options(R"(/api/.*)", [with_cors](const httplib::Request& req, httplib::Response& res) {
        with_cors(req, res);
        res.status = 204;
    });

    server.Post("/api/telemetry", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        TelemetryRequest telemetry_req;
        ParseError parse_err;
        if (!parse_telemetry_request(req.body, telemetry_req, parse_err)) {
            set_error_json(res, parse_err.http_status, parse_err.code, parse_err.message);
            with_cors(req, res);
            return;
        }

        const TelemetryCommandResult ingest = runtime.ingest_telemetry(
            telemetry_req.parsed,
            get_source_id(req)
        );

        if (!ingest.ok) {
            set_error_json(res, ingest.http_status, ingest.error_code, ingest.error_message);
            with_cors(req, res);
            return;
        }

        set_telemetry_ack_json(res, ingest);
        with_cors(req, res);
    });

    server.Post("/api/maneuver/schedule", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        ScheduleRequest parsed;
        ParseError parse_err;
        if (!parse_schedule_request(req.body, parsed, parse_err)) {
            set_error_json(res, parse_err.http_status, parse_err.code, parse_err.message);
            with_cors(req, res);
            return;
        }

        const ScheduleCommandResult scheduled = runtime.schedule_maneuver(
            parsed.satellite_id,
            std::move(parsed.burns)
        );
        if (!scheduled.ok) {
            set_error_json(res, scheduled.http_status, scheduled.error_code, scheduled.error_message);
            with_cors(req, res);
            return;
        }

        set_schedule_ack_json(res, scheduled);
        with_cors(req, res);
    });

    server.Post("/api/simulate/step", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        StepRequest parsed;
        ParseError parse_err;
        if (!parse_step_request(req.body, parsed, parse_err)) {
            set_error_json(res, parse_err.http_status, parse_err.code, parse_err.message);
            with_cors(req, res);
            return;
        }

        const StepCommandResult step = runtime.simulate_step(parsed.step_seconds);
        if (!step.ok) {
            set_error_json(res, step.http_status, step.error_code, step.error_message);
            with_cors(req, res);
            return;
        }

        set_step_ack_json(res, step);
        with_cors(req, res);
    });

    server.Get("/api/visualization/snapshot", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.snapshot_json(), "application/json");
        with_cors(req, res);
    });

    server.Get("/api/status", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        bool include_details = false;
        if (req.has_param("details")) {
            include_details = is_truthy(req.get_param_value("details"));
        }
        if (!include_details && req.has_param("verbose")) {
            include_details = is_truthy(req.get_param_value("verbose"));
        }

        res.status = 200;
        res.set_content(runtime.status_json(include_details), "application/json");
        with_cors(req, res);
    });

    server.Get("/api/debug/conflicts", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.conflicts_json(), "application/json");
        with_cors(req, res);
    });

    server.Get("/api/debug/propagation", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.propagation_json(), "application/json");
        with_cors(req, res);
    });

    server.Get("/api/debug/burns", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content(runtime.burns_json(), "application/json");
        with_cors(req, res);
    });

    server.Get("/api/debug/burn-counterfactual", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        const std::string burn_id = req.has_param("burn_id") ? req.get_param_value("burn_id") : std::string{};
        const BurnCounterfactualResult compare = runtime.burn_counterfactual_json(burn_id);
        if (!compare.ok) {
            set_error_json(res, compare.http_status, compare.error_code, compare.error_message);
            with_cors(req, res);
            return;
        }

        res.status = compare.http_status;
        res.set_content(compare.json, "application/json");
        with_cors(req, res);
    });

    server.Get("/api/debug/conjunctions", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        std::string_view filter;
        std::string filter_str;
        std::string source_filter;
        if (req.has_param("satellite_id")) {
            filter_str = req.get_param_value("satellite_id");
            filter = filter_str;
        }
        if (req.has_param("source")) {
            source_filter = req.get_param_value("source");
        }
        res.status = 200;
        res.set_content(runtime.conjunctions_json(filter, source_filter), "application/json");
        with_cors(req, res);
    });

    server.Get("/api/visualization/trajectory", [&runtime, with_cors](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("satellite_id")) {
            res.status = 400;
            res.set_content("{\"error\":\"missing satellite_id parameter\"}", "application/json");
            with_cors(req, res);
            return;
        }
        const std::string sat_id = req.get_param_value("satellite_id");
        res.status = 200;
        res.set_content(runtime.trajectory_json(sat_id), "application/json");
        with_cors(req, res);
    });
}

} // namespace cascade::http
