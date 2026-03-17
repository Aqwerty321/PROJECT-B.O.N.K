// ---------------------------------------------------------------------------
// telemetry.cpp — strict telemetry parse + locked commit
// ---------------------------------------------------------------------------
#include "telemetry.hpp"

#include "json_util.hpp"
#include "orbit_math.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"

#include <simdjson.h>

namespace cascade {

static inline bool parse_vec3(simdjson::ondemand::object& parent,
                              const char* key,
                              Vec3& out)
{
    simdjson::ondemand::object obj;
    if (parent.find_field_unordered(key).get_object().get(obj) != simdjson::SUCCESS) {
        return false;
    }

    auto xres = obj.find_field_unordered("x").get_double();
    auto yres = obj.find_field_unordered("y").get_double();
    auto zres = obj.find_field_unordered("z").get_double();
    if (xres.error() || yres.error() || zres.error()) {
        return false;
    }

    out.x = xres.value_unsafe();
    out.y = yres.value_unsafe();
    out.z = zres.value_unsafe();
    return true;
}

TelemetryParseResult parse_telemetry_payload(const std::string& body)
{
    TelemetryParseResult out;
    if (body.empty()) {
        out.error_code = "EMPTY_BODY";
        out.error_message = "request body is empty";
        return out;
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(body.data(), body.size());
    simdjson::ondemand::document doc;

    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        out.error_code = "MALFORMED_JSON";
        out.error_message = "request body is not valid JSON";
        return out;
    }

    simdjson::ondemand::object root;
    if (doc.get_object().get(root) != simdjson::SUCCESS) {
        out.error_code = "MALFORMED_JSON";
        out.error_message = "root JSON value must be an object";
        return out;
    }

    std::string_view ts_sv;
    if (root.find_field_unordered("timestamp").get_string().get(ts_sv) != simdjson::SUCCESS) {
        out.error_code = "MISSING_TIMESTAMP";
        out.error_message = "field 'timestamp' is required";
        return out;
    }

    out.timestamp_iso = std::string(ts_sv);
    out.timestamp_valid = parse_iso8601(out.timestamp_iso, out.telemetry_epoch_s);
    if (!out.timestamp_valid) {
        out.error_code = "INVALID_TIMESTAMP";
        out.error_message = "timestamp must be ISO-8601 UTC (e.g. 2026-03-12T08:00:00.000Z)";
        return out;
    }

    simdjson::ondemand::array objects;
    if (root.find_field_unordered("objects").get_array().get(objects) != simdjson::SUCCESS) {
        out.error_code = "MISSING_OBJECTS";
        out.error_message = "field 'objects' must be an array";
        return out;
    }

    out.valid_objects.reserve(1024);
    for (auto item_result : objects) {
        simdjson::ondemand::object obj;
        if (item_result.get_object().get(obj) != simdjson::SUCCESS) {
            ++out.invalid_object_count;
            continue;
        }

        std::string_view id_sv;
        if (obj.find_field_unordered("id").get_string().get(id_sv) != simdjson::SUCCESS || id_sv.empty()) {
            ++out.invalid_object_count;
            continue;
        }

        std::string_view type_sv;
        if (obj.find_field_unordered("type").get_string().get(type_sv) != simdjson::SUCCESS) {
            ++out.invalid_object_count;
            continue;
        }

        ObjectType type;
        if (type_sv == "SATELLITE") {
            type = ObjectType::SATELLITE;
        } else if (type_sv == "DEBRIS") {
            type = ObjectType::DEBRIS;
        } else {
            ++out.invalid_object_count;
            continue;
        }

        Vec3 r{};
        Vec3 v{};
        if (!parse_vec3(obj, "r", r) || !parse_vec3(obj, "v", v)) {
            ++out.invalid_object_count;
            continue;
        }

        TelemetryObject entry;
        entry.id = std::string(id_sv);
        entry.type = type;
        entry.r = r;
        entry.v = v;
        out.valid_objects.emplace_back(std::move(entry));
    }

    out.parse_ok = true;
    return out;
}

TelemetryIngestResult apply_telemetry_batch(const TelemetryParseResult& parsed,
                                            StateStore& store,
                                            SimClock& clock,
                                            std::string_view source_id)
{
    TelemetryIngestResult out;
    if (!parsed.parse_ok || !parsed.timestamp_valid) {
        out.error_code = "INVALID_PARSED_BATCH";
        out.error_message = "cannot ingest unparsed telemetry batch";
        return out;
    }

    if (clock.is_initialized() && parsed.telemetry_epoch_s + EPS_NUM < clock.epoch_s()) {
        out.error_code = "STALE_TELEMETRY";
        out.error_message = "telemetry timestamp is older than current simulation time";
        return out;
    }

    if (!clock.is_initialized()) {
        clock.set_epoch_s(parsed.telemetry_epoch_s);
    }

    const double ingest_now = now_unix_epoch_s();

    for (const TelemetryObject& obj : parsed.valid_objects) {
        bool conflict = false;
        (void)store.upsert(obj.id,
                           obj.type,
                           obj.r.x,
                           obj.r.y,
                           obj.r.z,
                           obj.v.x,
                           obj.v.y,
                           obj.v.z,
                           parsed.telemetry_epoch_s,
                           conflict);
        if (conflict) {
            const std::size_t idx = store.find(obj.id);
            ObjectType stored_type = ObjectType::DEBRIS;
            if (idx < store.size()) {
                stored_type = store.type(idx);
            }
            store.record_type_conflict(obj.id,
                                       stored_type,
                                       obj.type,
                                       parsed.timestamp_iso,
                                       ingest_now,
                                       source_id,
                                       "incoming type conflicts with canonical type");
            ++out.type_conflict_count;
            continue;
        }

        const std::size_t idx = store.find(obj.id);
        if (idx < store.size()) {
            OrbitalElements el{};
            const bool el_ok = eci_to_elements(obj.r, obj.v, el);
            store.set_elements(idx, el, el_ok);
            store.set_telemetry_epoch_s(idx, parsed.telemetry_epoch_s);
        }

        ++out.processed_count;
    }

    out.invalid_object_count = parsed.invalid_object_count;
    out.ok = true;
    return out;
}

} // namespace cascade
