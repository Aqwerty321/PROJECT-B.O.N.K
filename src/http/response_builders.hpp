// ---------------------------------------------------------------------------
// response_builders.hpp — API response serialization helpers
// ---------------------------------------------------------------------------
#pragma once

#include "engine_runtime.hpp"

#include <httplib.h>

#include <string_view>

namespace cascade::http {

void set_error_json(httplib::Response& res,
                    int http_status,
                    std::string_view code,
                    std::string_view message);

void set_telemetry_ack_json(httplib::Response& res,
                            const TelemetryCommandResult& ingest);

void set_schedule_ack_json(httplib::Response& res,
                           const ScheduleCommandResult& scheduled);

void set_step_ack_json(httplib::Response& res,
                       const StepCommandResult& step);

} // namespace cascade::http
