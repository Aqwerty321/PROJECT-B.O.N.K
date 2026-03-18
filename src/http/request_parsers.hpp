// ---------------------------------------------------------------------------
// request_parsers.hpp — HTTP payload parsing helpers for API handlers
// ---------------------------------------------------------------------------
#pragma once

#include "maneuver_common.hpp"
#include "telemetry.hpp"
#include "types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cascade::http {

struct ParseError {
    int http_status = 400;
    std::string code;
    std::string message;
};

struct TelemetryRequest {
    TelemetryParseResult parsed;
};

struct ScheduleRequest {
    std::string satellite_id;
    std::vector<ScheduledBurn> burns;
};

struct StepRequest {
    std::int64_t step_seconds = 0;
};

bool parse_schedule_request(std::string_view body,
                            ScheduleRequest& out,
                            ParseError& err);

bool parse_telemetry_request(std::string_view body,
                             TelemetryRequest& out,
                             ParseError& err);

bool parse_step_request(std::string_view body,
                        StepRequest& out,
                        ParseError& err);

} // namespace cascade::http
