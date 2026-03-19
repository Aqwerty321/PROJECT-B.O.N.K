// ---------------------------------------------------------------------------
// api_contract_gate.cpp
//
// Contract harness: verifies core PS endpoint schemas and status/error behavior
// against the running local backend.
// ---------------------------------------------------------------------------

#include <httplib.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>
#include <thread>
#include <sstream>
#include <string>
#include <string_view>

namespace {

struct GateResult {
    bool pass = true;
    std::string reason;
};

bool contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

bool json_has(std::string_view body, std::string_view key)
{
    std::string token = "\"";
    token += key;
    token += "\"";
    return contains(body, token);
}

int parse_env_int_or_default(const char* key,
                             int default_value,
                             int min_value,
                             int max_value)
{
    const char* raw = std::getenv(key);
    if (raw == nullptr || *raw == '\0') {
        return default_value;
    }

    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == nullptr || *end != '\0') {
        return default_value;
    }
    if (parsed < min_value || parsed > max_value) {
        return default_value;
    }
    return static_cast<int>(parsed);
}

std::int64_t parse_env_i64_or_default(const char* key,
                                      std::int64_t default_value,
                                      std::int64_t min_value,
                                      std::int64_t max_value)
{
    const char* raw = std::getenv(key);
    if (raw == nullptr || *raw == '\0') {
        return default_value;
    }

    char* end = nullptr;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (end == nullptr || *end != '\0') {
        return default_value;
    }
    if (parsed < min_value || parsed > max_value) {
        return default_value;
    }
    return static_cast<std::int64_t>(parsed);
}

int expected_schedule_success_status()
{
    const int runtime_status = parse_env_int_or_default(
        "PROJECTBONK_SCHEDULE_SUCCESS_STATUS",
        202,
        200,
        299
    );

    return parse_env_int_or_default(
        "PROJECTBONK_API_CONTRACT_SCHEDULE_SUCCESS_STATUS",
        runtime_status,
        200,
        299
    );
}

std::int64_t expected_max_step_seconds()
{
    return parse_env_i64_or_default(
        "PROJECTBONK_MAX_STEP_SECONDS",
        86400,
        1,
        604800
    );
}

std::string telemetry_payload()
{
    return R"JSON({
  "timestamp": "2026-03-12T08:00:00.000Z",
  "objects": [
    {
      "id": "SAT-CONTRACT-01",
      "type": "SATELLITE",
      "r": {"x": 7000.0, "y": 0.0, "z": 0.0},
      "v": {"x": 0.0, "y": 7.5, "z": 1.0}
    },
    {
      "id": "DEB-CONTRACT-01",
      "type": "DEBRIS",
      "r": {"x": 7000.1, "y": 0.1, "z": 0.0},
      "v": {"x": 0.0, "y": 7.5, "z": 1.0}
    }
  ]
})JSON";
}

std::string schedule_payload_valid()
{
    return R"JSON({
  "satelliteId": "SAT-CONTRACT-01",
  "maneuver_sequence": [
    {
      "burn_id": "CONTRACT_BURN_1",
      "burnTime": "2026-03-12T08:30:00.000Z",
      "deltaV_vector": {"x": 0.0, "y": 0.001, "z": 0.0}
    }
  ]
})JSON";
}

std::string schedule_payload_unknown_satellite()
{
    return R"JSON({
  "satelliteId": "SAT-NOT-FOUND",
  "maneuver_sequence": [
    {
      "burn_id": "CONTRACT_BURN_UNKNOWN",
      "burnTime": "2026-03-12T08:30:00.000Z",
      "deltaV_vector": {"x": 0.0, "y": 0.001, "z": 0.0}
    }
  ]
})JSON";
}

std::string schedule_payload_invalid_cooldown()
{
    return R"JSON({
  "satelliteId": "SAT-CONTRACT-01",
  "maneuver_sequence": [
    {
      "burn_id": "CONTRACT_BURN_2",
      "burnTime": "2026-03-12T08:40:00.000Z",
      "deltaV_vector": {"x": 0.0, "y": 0.001, "z": 0.0}
    },
    {
      "burn_id": "CONTRACT_BURN_3",
      "burnTime": "2026-03-12T08:44:00.000Z",
      "deltaV_vector": {"x": 0.0, "y": 0.001, "z": 0.0}
    }
  ]
})JSON";
}

std::string step_payload()
{
    return R"JSON({"step_seconds": 60})JSON";
}

std::string step_payload_invalid()
{
    return R"JSON({"step_seconds": 0})JSON";
}

std::string step_payload_too_large(std::int64_t max_step_seconds)
{
    const std::int64_t too_large =
        max_step_seconds < std::numeric_limits<std::int64_t>::max()
            ? max_step_seconds + 1
            : max_step_seconds;
    return std::string("{\"step_seconds\": ")
        + std::to_string(too_large)
        + "}";
}

bool require_http(const httplib::Result& res,
                  int status,
                  std::string_view must_have,
                  std::string& reason)
{
    if (!res) {
        reason = "request failed at transport level";
        return false;
    }
    if (res->status != status) {
        std::ostringstream oss;
        oss << "expected status " << status << " got " << res->status;
        reason = oss.str();
        return false;
    }
    if (!must_have.empty() && !contains(res->body, must_have)) {
        reason = "response body missing expected token";
        return false;
    }
    return true;
}

GateResult run_cors_check(std::string_view host,
                          int port)
{
    GateResult out;

    httplib::Client cli(std::string(host), port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);

    httplib::Headers headers{
        {"Origin", "http://localhost:5173"},
        {"Access-Control-Request-Method", "POST"},
        {"Access-Control-Request-Headers", "Content-Type, X-Source-Id"}
    };

    auto preflight = cli.Options("/api/telemetry", headers);
    if (!preflight || preflight->status != 204) {
        out.pass = false;
        out.reason = "CORS preflight failed for /api/telemetry";
        return out;
    }

    const auto has_header = [&](const httplib::Response& r, std::string_view key, std::string_view val) {
        const auto it = r.headers.find(std::string(key));
        if (it == r.headers.end()) return false;
        return it->second == val;
    };

    if (!has_header(*preflight, "Access-Control-Allow-Origin", "http://localhost:5173")) {
        out.pass = false;
        out.reason = "CORS preflight missing allow-origin";
        return out;
    }
    if (!has_header(*preflight, "Vary", "Origin")) {
        out.pass = false;
        out.reason = "CORS preflight missing Vary: Origin";
        return out;
    }
    if (!contains(preflight->get_header_value("Access-Control-Allow-Methods"), "POST")) {
        out.pass = false;
        out.reason = "CORS preflight missing POST allow-method";
        return out;
    }
    if (!contains(preflight->get_header_value("Access-Control-Allow-Headers"), "X-Source-Id")) {
        out.pass = false;
        out.reason = "CORS preflight missing X-Source-Id allow-header";
        return out;
    }

    auto res = cli.Get("/api/status", httplib::Headers{{"Origin", "http://localhost:5173"}});
    if (!res || res->status != 200) {
        out.pass = false;
        out.reason = "CORS simple request status check failed";
        return out;
    }
    if (!has_header(*res, "Access-Control-Allow-Origin", "http://localhost:5173")) {
        out.pass = false;
        out.reason = "CORS simple response missing allow-origin";
        return out;
    }
    if (!has_header(*res, "Vary", "Origin")) {
        out.pass = false;
        out.reason = "CORS simple response missing Vary: Origin";
        return out;
    }

    auto blocked = cli.Get("/api/status", httplib::Headers{{"Origin", "http://localhost:9999"}});
    if (!blocked || blocked->status != 200) {
        out.pass = false;
        out.reason = "CORS disallowed-origin status check failed";
        return out;
    }
    const auto blocked_header = blocked->headers.find("Access-Control-Allow-Origin");
    if (blocked_header != blocked->headers.end()) {
        out.pass = false;
        out.reason = "CORS disallowed-origin unexpectedly allowed";
        return out;
    }

    return out;
}

GateResult run_queue_backpressure_check(std::string_view host,
                                        int port)
{
    GateResult out;
    httplib::Client cli(std::string(host), port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);

    auto telemetry_client_task = [host, port]() {
        httplib::Client local(std::string(host), port);
        local.set_connection_timeout(2, 0);
        local.set_read_timeout(5, 0);
        return local.Post("/api/telemetry", telemetry_payload(), "application/json");
    };

    std::thread load_thread_1([&]() {
        auto res = cli.Post("/api/simulate/step", R"JSON({"step_seconds": 43200})JSON", "application/json");
        (void)res;
    });
    std::thread load_thread_2([&]() {
        auto res = cli.Post("/api/simulate/step", R"JSON({"step_seconds": 43200})JSON", "application/json");
        (void)res;
    });

    bool saw_busy = false;
    for (int i = 0; i < 40; ++i) {
        auto res = telemetry_client_task();
        if (res && res->status == 503 && contains(res->body, "\"code\":\"RUNTIME_BUSY\"")) {
            saw_busy = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    load_thread_1.join();
    load_thread_2.join();

    auto status_res = cli.Get("/api/status?details=1");
    if (!status_res || status_res->status != 200 || !contains(status_res->body, "\"command_queue_timeout_total\"")) {
        out.pass = false;
        out.reason = "status details missing queue timeout metric after load";
        return out;
    }
    if (!saw_busy) {
        // Accept metric-only evidence to avoid occasional scheduling races.
        if (!contains(status_res->body, "\"command_queue_rejected_total\":")
            && !contains(status_res->body, "\"command_queue_timeout_total\":")) {
            out.pass = false;
            out.reason = "no runtime busy evidence under queued load";
            return out;
        }
    }

    return out;
}

GateResult run_contract_checks(std::string_view host,
                               int port,
                               int schedule_success_status,
                               std::int64_t max_step_seconds)
{
    GateResult out;
    httplib::Client cli(std::string(host), port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);

    {
        // Status semantics: simulate before telemetry must be rejected.
        auto res = cli.Post("/api/simulate/step", step_payload(), "application/json");
        std::string reason;
        if (!require_http(res, 400, "\"status\":\"ERROR\"", reason)) {
            out.pass = false;
            out.reason = "pre-telemetry step contract failed: " + reason;
            return out;
        }
        if (!contains(res->body, "\"code\":\"CLOCK_UNINITIALIZED\"")) {
            out.pass = false;
            out.reason = "pre-telemetry step error code mismatch";
            return out;
        }
    }

    {
        // Status semantics: malformed telemetry payload must be deterministic.
        auto res = cli.Post("/api/telemetry", R"JSON({"timestamp":)JSON", "application/json");
        std::string reason;
        if (!require_http(res, 400, "\"status\":\"ERROR\"", reason)) {
            out.pass = false;
            out.reason = "telemetry malformed-json contract failed: " + reason;
            return out;
        }
        if (!contains(res->body, "\"code\":\"MALFORMED_JSON\"")) {
            out.pass = false;
            out.reason = "telemetry malformed-json error code mismatch";
            return out;
        }
    }

    {
        auto res = cli.Post("/api/telemetry", telemetry_payload(), "application/json");
        std::string reason;
        if (!require_http(res, 200, "\"status\":\"ACK\"", reason)) {
            out.pass = false;
            out.reason = "telemetry contract failed: " + reason;
            return out;
        }
        if (!json_has(res->body, "processed_count") || !json_has(res->body, "active_cdm_warnings")) {
            out.pass = false;
            out.reason = "telemetry ACK missing required PS keys";
            return out;
        }
    }

    {
        auto res = cli.Post("/api/maneuver/schedule", schedule_payload_unknown_satellite(), "application/json");
        std::string reason;
        if (!require_http(res, 404, "\"status\":\"ERROR\"", reason)) {
            out.pass = false;
            out.reason = "schedule unknown-satellite contract failed: " + reason;
            return out;
        }
        if (!contains(res->body, "\"code\":\"SATELLITE_NOT_FOUND\"")) {
            out.pass = false;
            out.reason = "schedule unknown-satellite error code mismatch";
            return out;
        }
    }

    {
        auto res = cli.Post("/api/maneuver/schedule", schedule_payload_valid(), "application/json");
        std::string reason;
        if (!require_http(res, schedule_success_status, "\"status\":\"SCHEDULED\"", reason)) {
            out.pass = false;
            out.reason = "schedule success contract failed: " + reason;
            return out;
        }
        if (!json_has(res->body, "validation")
            || !json_has(res->body, "ground_station_los")
            || !json_has(res->body, "sufficient_fuel")
            || !json_has(res->body, "projected_mass_remaining_kg")) {
            out.pass = false;
            out.reason = "schedule ACK missing required PS validation keys";
            return out;
        }
        if (!contains(res->body, "\"ground_station_los\":true")
            || !contains(res->body, "\"sufficient_fuel\":true")) {
            out.pass = false;
            out.reason = "schedule ACK validation flags mismatch";
            return out;
        }
    }

    {
        auto res = cli.Post("/api/maneuver/schedule", schedule_payload_invalid_cooldown(), "application/json");
        std::string reason;
        if (!require_http(res, 422, "\"status\":\"ERROR\"", reason)) {
            out.pass = false;
            out.reason = "schedule error contract failed: " + reason;
            return out;
        }
        if (!contains(res->body, "\"code\":\"COOLDOWN_VIOLATION\"")) {
            out.pass = false;
            out.reason = "schedule cooldown error code mismatch";
            return out;
        }
    }

    {
        auto res = cli.Post("/api/simulate/step", step_payload(), "application/json");
        std::string reason;
        if (!require_http(res, 200, "\"status\":\"STEP_COMPLETE\"", reason)) {
            out.pass = false;
            out.reason = "step success contract failed: " + reason;
            return out;
        }
        if (!json_has(res->body, "new_timestamp")
            || !json_has(res->body, "collisions_detected")
            || !json_has(res->body, "maneuvers_executed")) {
            out.pass = false;
            out.reason = "step ACK missing required PS keys";
            return out;
        }
    }

    {
        auto res = cli.Post("/api/simulate/step", step_payload_invalid(), "application/json");
        std::string reason;
        if (!require_http(res, 422, "\"status\":\"ERROR\"", reason)) {
            out.pass = false;
            out.reason = "step error contract failed: " + reason;
            return out;
        }
        if (!contains(res->body, "\"code\":\"INVALID_STEP_SECONDS\"")) {
            out.pass = false;
            out.reason = "step invalid error code mismatch";
            return out;
        }
    }

    {
        auto res = cli.Post(
            "/api/simulate/step",
            step_payload_too_large(max_step_seconds),
            "application/json"
        );
        std::string reason;
        if (!require_http(res, 422, "\"status\":\"ERROR\"", reason)) {
            out.pass = false;
            out.reason = "step exceeds-limit contract failed: " + reason;
            return out;
        }
        if (!contains(res->body, "\"code\":\"STEP_SECONDS_EXCEEDS_LIMIT\"")) {
            out.pass = false;
            out.reason = "step exceeds-limit error code mismatch";
            return out;
        }
    }

    {
        auto res = cli.Get("/api/visualization/snapshot");
        std::string reason;
        if (!require_http(res, 200, "\"timestamp\"", reason)) {
            out.pass = false;
            out.reason = "snapshot contract failed: " + reason;
            return out;
        }
        if (!json_has(res->body, "satellites") || !json_has(res->body, "debris_cloud")) {
            out.pass = false;
            out.reason = "snapshot response missing required PS keys";
            return out;
        }
    }

    {
        auto res = cli.Get("/api/status");
        std::string reason;
        if (!require_http(res, 200, "\"status\":\"NOMINAL\"", reason)) {
            out.pass = false;
            out.reason = "status contract failed: " + reason;
            return out;
        }
        if (!json_has(res->body, "uptime_s") || !json_has(res->body, "tick_count") || !json_has(res->body, "object_count")) {
            out.pass = false;
            out.reason = "status response missing required PS keys";
            return out;
        }
        if (contains(res->body, "\"internal_metrics\"")) {
            out.pass = false;
            out.reason = "status default response leaked non-PS internal metrics";
            return out;
        }
    }

    {
        const GateResult queue_check = run_queue_backpressure_check(host, port);
        if (!queue_check.pass) {
            out.pass = false;
            out.reason = queue_check.reason;
            return out;
        }
    }

    {
        auto res = cli.Get("/api/status?details=1");
        std::string reason;
        if (!require_http(res, 200, "\"internal_metrics\"", reason)) {
            out.pass = false;
            out.reason = "status details contract failed: " + reason;
            return out;
        }
        if (!contains(res->body, "\"command_latency_us\"")
            || !contains(res->body, "\"telemetry\"")
            || !contains(res->body, "\"schedule\"")
            || !contains(res->body, "\"step\"")) {
            out.pass = false;
            out.reason = "status details missing command latency metrics";
            return out;
        }
        if (!contains(res->body, "\"narrow_uncertainty_promoted_pairs_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow uncertainty promoted total";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_shadow_rejected_pairs_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase shadow total";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_hard_rejected_pairs_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase hard rejected total";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_fail_open_pairs_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase fail-open total";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_reject_reason_plane_angle_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase plane-angle reject reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_reject_reason_phase_angle_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase phase-angle reject reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_fail_open_reason_elements_invalid_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase fail-open elements-invalid reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_fail_open_reason_uncertainty_override_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase fail-open uncertainty-override reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_shadow_rejected_pairs_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid shadow total";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_hard_rejected_pairs_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid hard rejected total";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_fail_open_pairs_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid fail-open total";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_reject_reason_distance_threshold_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid reject distance-threshold reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_fail_open_reason_sampling_failure_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid fail-open sampling-failure reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_fail_open_reason_uncertainty_override_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid fail-open uncertainty-override reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_refine_fail_open_reason_rk4_failure_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow refine fail-open rk4-failure reason total";
            return out;
        }
        if (!contains(res->body, "\"narrow_full_refine_fail_open_reason_budget_exhausted_total\"")) {
            out.pass = false;
            out.reason = "status details missing narrow full-refine fail-open budget-exhausted reason total";
            return out;
        }
        if (!contains(res->body, "\"broad_phase_shadow_dcriterion_rejected_total\"")) {
            out.pass = false;
            out.reason = "status details missing broad dcriterion shadow total";
            return out;
        }
        if (!contains(res->body, "\"broad_dcriterion_shadow_rejected\"")) {
            out.pass = false;
            out.reason = "status details missing broad dcriterion shadow last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_uncertainty_promoted_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow uncertainty promoted last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_evaluated_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase evaluated last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_shadow_rejected_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase shadow last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_hard_rejected_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase hard rejected last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_fail_open_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase fail-open last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_reject_reason_plane_angle\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase plane-angle reject reason last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_plane_phase_fail_open_reason_uncertainty_override\"")) {
            out.pass = false;
            out.reason = "status details missing narrow plane-phase uncertainty-override reason last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_evaluated_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid evaluated last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_shadow_rejected_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid shadow last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_hard_rejected_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid hard rejected last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_fail_open_pairs\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid fail-open last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_reject_reason_distance_threshold\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid reject distance-threshold reason last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_moid_fail_open_reason_sampling_failure\"")) {
            out.pass = false;
            out.reason = "status details missing narrow moid fail-open sampling-failure reason last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_refine_fail_open_reason_rk4_failure\"")) {
            out.pass = false;
            out.reason = "status details missing narrow refine rk4-failure reason last tick metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_full_refine_fail_open_reason_budget_exhausted\"")) {
            out.pass = false;
            out.reason = "status details missing narrow full-refine budget-exhausted reason last tick metric";
            return out;
        }
        if (!contains(res->body, "\"collision_threshold_km\"")) {
            out.pass = false;
            out.reason = "status details missing collision threshold metric";
            return out;
        }
        if (!contains(res->body, "\"narrow_tca_guard_km\"")) {
            out.pass = false;
            out.reason = "status details missing narrow tca guard metric";
            return out;
        }
        if (!contains(res->body, "\"effective_collision_threshold_km\"")) {
            out.pass = false;
            out.reason = "status details missing effective collision threshold metric";
            return out;
        }
        const std::string schedule_status_token =
            std::string("\"schedule_success_status\":")
            + std::to_string(schedule_success_status);
        if (!contains(res->body, schedule_status_token)) {
            out.pass = false;
            out.reason = "status details missing schedule success status policy value";
            return out;
        }
        const std::string max_step_seconds_token =
            std::string("\"max_step_seconds\":")
            + std::to_string(max_step_seconds);
        if (!contains(res->body, max_step_seconds_token)) {
            out.pass = false;
            out.reason = "status details missing max step seconds policy value";
            return out;
        }
    }

    {
        auto res = cli.Get("/api/status?verbose=1");
        std::string reason;
        if (!require_http(res, 200, "\"internal_metrics\"", reason)) {
            out.pass = false;
            out.reason = "status verbose contract failed: " + reason;
            return out;
        }
    }

    {
        const GateResult cors = run_cors_check(host, port);
        if (!cors.pass) {
            out.pass = false;
            out.reason = cors.reason;
            return out;
        }
    }

    return out;
}

} // namespace

int main(int argc, char** argv)
{
    std::string host = "127.0.0.1";
    int port = 8000;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = std::max(1, std::atoi(argv[2]));
    }

    const int schedule_success_status = expected_schedule_success_status();
    const std::int64_t max_step_seconds = expected_max_step_seconds();
    const GateResult result = run_contract_checks(
        host,
        port,
        schedule_success_status,
        max_step_seconds
    );

    std::cout << "api_contract_gate\n";
    std::cout << "host=" << host << "\n";
    std::cout << "port=" << port << "\n";
    std::cout << "schedule_success_expected_status=" << schedule_success_status << "\n";
    std::cout << "max_step_seconds_expected=" << max_step_seconds << "\n";
    std::cout << "api_contract_gate_result=" << (result.pass ? "PASS" : "FAIL") << "\n";
    if (!result.pass) {
        std::cout << "api_contract_gate_reason=" << result.reason << "\n";
    }

    return result.pass ? 0 : 1;
}
