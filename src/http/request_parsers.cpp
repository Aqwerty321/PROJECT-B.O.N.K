// ---------------------------------------------------------------------------
// request_parsers.cpp — HTTP payload parsing helpers for API handlers
// ---------------------------------------------------------------------------

#include "request_parsers.hpp"

#include "telemetry.hpp"
#include "json_util.hpp"

#include <simdjson.h>

namespace cascade::http {

namespace {

bool parse_vec3_field(simdjson::ondemand::object& parent,
                      const char* key,
                      Vec3& out)
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

bool parse_schedule_request(std::string_view body,
                            ScheduleRequest& out,
                            ParseError& err)
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(body.data(), body.size());
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        err.http_status = 400;
        err.code = "MALFORMED_JSON";
        err.message = "request body is not valid JSON";
        return false;
    }

    simdjson::ondemand::object root;
    if (doc.get_object().get(root) != simdjson::SUCCESS) {
        err.http_status = 400;
        err.code = "INVALID_REQUEST";
        err.message = "root JSON value must be an object";
        return false;
    }

    std::string_view sat_sv;
    if (root.find_field_unordered("satelliteId").get_string().get(sat_sv) != simdjson::SUCCESS
        || sat_sv.empty()) {
        err.http_status = 422;
        err.code = "MISSING_SATELLITE_ID";
        err.message = "field 'satelliteId' is required";
        return false;
    }
    out.satellite_id = std::string(sat_sv);

    simdjson::ondemand::array burns;
    if (root.find_field_unordered("maneuver_sequence").get_array().get(burns) != simdjson::SUCCESS) {
        err.http_status = 422;
        err.code = "MISSING_MANEUVER_SEQUENCE";
        err.message = "field 'maneuver_sequence' must be an array";
        return false;
    }

    out.burns.clear();
    out.burns.reserve(8);

    for (auto burn_value : burns) {
        simdjson::ondemand::object burn_obj;
        if (burn_value.get_object().get(burn_obj) != simdjson::SUCCESS) {
            err.http_status = 422;
            err.code = "INVALID_BURN";
            err.message = "each maneuver entry must be an object";
            return false;
        }

        std::string_view burn_id_sv;
        if (burn_obj.find_field_unordered("burn_id").get_string().get(burn_id_sv) != simdjson::SUCCESS
            || burn_id_sv.empty()) {
            err.http_status = 422;
            err.code = "MISSING_BURN_ID";
            err.message = "each burn must include non-empty 'burn_id'";
            return false;
        }

        std::string_view burn_time_sv;
        if (burn_obj.find_field_unordered("burnTime").get_string().get(burn_time_sv) != simdjson::SUCCESS
            || burn_time_sv.empty()) {
            err.http_status = 422;
            err.code = "MISSING_BURN_TIME";
            err.message = "each burn must include non-empty 'burnTime'";
            return false;
        }

        double burn_epoch_s = 0.0;
        if (!parse_iso8601(burn_time_sv, burn_epoch_s)) {
            err.http_status = 422;
            err.code = "INVALID_BURN_TIME";
            err.message = "burnTime must be ISO-8601 UTC";
            return false;
        }

        Vec3 dv{};
        if (!parse_vec3_field(burn_obj, "deltaV_vector", dv)) {
            err.http_status = 422;
            err.code = "MISSING_DELTAV_VECTOR";
            err.message = "each burn must include deltaV_vector{x,y,z}";
            return false;
        }

        const double dv_norm = dv_norm_km_s(dv);
        if (dv_norm > SAT_MAX_DELTAV_KM_S + EPS_NUM) {
            err.http_status = 422;
            err.code = "DELTA_V_EXCEEDS_LIMIT";
            err.message = "burn deltaV exceeds 15 m/s limit";
            return false;
        }

        ScheduledBurn b;
        b.id = std::string(burn_id_sv);
        b.satellite_id = out.satellite_id;
        b.burn_epoch_s = burn_epoch_s;
        b.delta_v_km_s = dv;
        b.delta_v_norm_km_s = dv_norm;
        out.burns.push_back(std::move(b));
    }

    if (out.burns.empty()) {
        err.http_status = 422;
        err.code = "EMPTY_MANEUVER_SEQUENCE";
        err.message = "at least one burn is required";
        return false;
    }

    return true;
}

bool parse_step_request(std::string_view body,
                        StepRequest& out,
                        ParseError& err)
{
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(body.data(), body.size());
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        err.http_status = 400;
        err.code = "MALFORMED_JSON";
        err.message = "request body is not valid JSON";
        return false;
    }

    simdjson::ondemand::object root;
    if (doc.get_object().get(root) != simdjson::SUCCESS) {
        err.http_status = 400;
        err.code = "INVALID_REQUEST";
        err.message = "root JSON value must be an object";
        return false;
    }

    auto step_res = root.find_field_unordered("step_seconds").get_int64();
    if (step_res.error()) {
        err.http_status = 422;
        err.code = "MISSING_STEP_SECONDS";
        err.message = "field 'step_seconds' is required and must be an integer";
        return false;
    }

    out.step_seconds = step_res.value_unsafe();
    if (out.step_seconds <= 0) {
        err.http_status = 422;
        err.code = "INVALID_STEP_SECONDS";
        err.message = "step_seconds must be > 0";
        return false;
    }

    return true;
}

} // namespace cascade::http
