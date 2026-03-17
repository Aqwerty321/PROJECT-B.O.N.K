// ---------------------------------------------------------------------------
// telemetry.cpp — simdjson On-Demand parser for POST /api/telemetry
//
// Expected JSON schema (PS.md §4.1):
// {
//   "timestamp": "2026-03-12T08:00:00.000Z",
//   "objects": [
//     { "id": "DEB-99421", "type": "DEBRIS",
//       "r": {"x": 4500.2, "y": -2100.5, "z": 4800.1},
//       "v": {"x": 1.25,   "y":  6.84,   "z":  3.12}  }
//   ]
// }
// ---------------------------------------------------------------------------
#include "telemetry.hpp"
#include "state_store.hpp"
#include "sim_clock.hpp"
#include "types.hpp"

#include <simdjson.h>

namespace cascade {

// Consume a simdjson double result: sets `out` if no error, leaves it
// unchanged otherwise.  Calling convention avoids the warn_unused_result
// attribute that fires on simdjson_result<T>::get(T&).
static inline void sj_double(simdjson::simdjson_result<double> res, double& out) {
    if (!res.error()) out = res.value_unsafe();
}


TelemetryResult parse_telemetry(const std::string& body,
                                StateStore&        store,
                                SimClock&          clock)
{
    TelemetryResult result;
    if (body.empty()) return result;

    // simdjson On-Demand: padded_string copies + pads the input buffer.
    simdjson::ondemand::parser parser;
    simdjson::padded_string    padded(body.data(), body.size());

    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        return result;   // malformed JSON
    }
    result.parse_ok = true;

    // -----------------------------------------------------------------------
    // "timestamp" field
    // -----------------------------------------------------------------------
    {
        std::string_view ts_sv;
        if (doc["timestamp"].get_string().get(ts_sv) == simdjson::SUCCESS) {
            result.timestamp        = std::string(ts_sv);
            result.timestamp_parsed = true;
            // Seed the sim clock from first telemetry batch if uninitialized
            if (!clock.is_initialized()) {
                clock.set_from_iso(result.timestamp);
            }
        }
    }

    // -----------------------------------------------------------------------
    // "objects" array
    // -----------------------------------------------------------------------
    simdjson::ondemand::array objects;
    if (doc["objects"].get_array().get(objects) != simdjson::SUCCESS) {
        return result;
    }

    for (auto item_result : objects) {
        simdjson::ondemand::object obj;
        if (item_result.get_object().get(obj) != simdjson::SUCCESS) continue;

        // "id" — required; skip object if missing
        std::string_view id_sv;
        if (obj.find_field_unordered("id").get_string().get(id_sv) != simdjson::SUCCESS) continue;

        // "type" — optional, default DEBRIS
        ObjectType otype = ObjectType::DEBRIS;
        {
            std::string_view type_sv;
            if (obj.find_field_unordered("type").get_string().get(type_sv) == simdjson::SUCCESS) {
                if (type_sv == "SATELLITE") otype = ObjectType::SATELLITE;
            }
        }

        // "r" — position, km ECI
        double rx = 0.0, ry = 0.0, rz = 0.0;
        {
            simdjson::ondemand::object r_obj;
            if (obj.find_field_unordered("r").get_object().get(r_obj) == simdjson::SUCCESS) {
                sj_double(r_obj.find_field("x").get_double(), rx);
                sj_double(r_obj.find_field("y").get_double(), ry);
                sj_double(r_obj.find_field("z").get_double(), rz);
            }
        }

        // "v" — velocity, km/s ECI
        double vx = 0.0, vy = 0.0, vz = 0.0;
        {
            simdjson::ondemand::object v_obj;
            if (obj.find_field_unordered("v").get_object().get(v_obj) == simdjson::SUCCESS) {
                sj_double(v_obj.find_field("x").get_double(), vx);
                sj_double(v_obj.find_field("y").get_double(), vy);
                sj_double(v_obj.find_field("z").get_double(), vz);
            }
        }

        store.upsert(id_sv, otype, rx, ry, rz, vx, vy, vz);
        ++result.processed_count;
    }

    return result;
}

} // namespace cascade
