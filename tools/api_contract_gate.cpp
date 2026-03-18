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

GateResult run_contract_checks(std::string_view host,
                               int port)
{
    GateResult out;
    httplib::Client cli(std::string(host), port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);

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
        auto res = cli.Post("/api/maneuver/schedule", schedule_payload_valid(), "application/json");
        std::string reason;
        if (!require_http(res, 202, "\"status\":\"SCHEDULED\"", reason)) {
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

    const GateResult result = run_contract_checks(host, port);

    std::cout << "api_contract_gate\n";
    std::cout << "host=" << host << "\n";
    std::cout << "port=" << port << "\n";
    std::cout << "api_contract_gate_result=" << (result.pass ? "PASS" : "FAIL") << "\n";
    if (!result.pass) {
        std::cout << "api_contract_gate_reason=" << result.reason << "\n";
    }

    return result.pass ? 0 : 1;
}
