// ---------------------------------------------------------------------------
// response_builders.cpp — API response serialization helpers
// ---------------------------------------------------------------------------

#include "response_builders.hpp"

#include "json_util.hpp"

namespace cascade::http {

void set_error_json(httplib::Response& res,
                    int http_status,
                    std::string_view code,
                    std::string_view message)
{
    std::string out;
    out.reserve(96 + code.size() + message.size());
    out += "{\"status\":\"ERROR\",\"code\":";
    append_json_string(out, code);
    out += ",\"message\":";
    append_json_string(out, message);
    out += '}';
    res.status = http_status;
    res.set_content(out, "application/json");
}

void set_telemetry_ack_json(httplib::Response& res,
                            const TelemetryCommandResult& ingest)
{
    std::string out;
    out.reserve(96);
    out += "{\"status\":\"ACK\",\"processed_count\":";
    out += std::to_string(ingest.processed_count);
    out += ",\"active_cdm_warnings\":";
    out += std::to_string(ingest.active_cdm_warnings);
    out += '}';

    res.status = 200;
    res.set_content(out, "application/json");
}

void set_schedule_ack_json(httplib::Response& res,
                           const ScheduleCommandResult& scheduled)
{
    std::string out;
    out.reserve(192);
    out += "{\"status\":\"SCHEDULED\",\"validation\":{";
    out += "\"ground_station_los\":true,";
    out += "\"sufficient_fuel\":true";
    out += ",\"projected_mass_remaining_kg\":";
    out += fmt_double(scheduled.projected_mass_remaining_kg, 3);
    out += "}}";

    res.status = scheduled.http_status;
    res.set_content(out, "application/json");
}

void set_step_ack_json(httplib::Response& res,
                       const StepCommandResult& step)
{
    std::string out;
    out.reserve(160);
    out += "{\"status\":\"STEP_COMPLETE\",\"new_timestamp\":";
    append_json_string(out, step.new_timestamp);
    out += ",\"collisions_detected\":";
    out += std::to_string(step.collisions_detected);
    out += ",\"maneuvers_executed\":";
    out += std::to_string(step.maneuvers_executed);
    out += '}';

    res.status = 200;
    res.set_content(out, "application/json");
}

} // namespace cascade::http
