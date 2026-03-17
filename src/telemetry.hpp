// ---------------------------------------------------------------------------
// telemetry.hpp — telemetry ingestion parser interface
// ---------------------------------------------------------------------------
#pragma once

#include "types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace cascade {

class StateStore;
class SimClock;

struct TelemetryObject {
    std::string id;
    ObjectType  type = ObjectType::DEBRIS;
    Vec3        r;
    Vec3        v;
};

struct TelemetryParseResult {
    bool parse_ok = false;
    std::string error_code;
    std::string error_message;

    std::string timestamp_iso;
    double telemetry_epoch_s = 0.0;
    bool timestamp_valid = false;

    std::vector<TelemetryObject> valid_objects;
    int invalid_object_count = 0;
};

struct TelemetryIngestResult {
    bool ok = false;
    std::string error_code;
    std::string error_message;

    int processed_count = 0;
    int invalid_object_count = 0;
    int type_conflict_count = 0;
};

// Parse telemetry payload outside global lock.
TelemetryParseResult parse_telemetry_payload(const std::string& body);

// Commit parsed telemetry under lock.
TelemetryIngestResult apply_telemetry_batch(const TelemetryParseResult& parsed,
                                            StateStore& store,
                                            SimClock& clock,
                                            std::string_view source_id);

} // namespace cascade
