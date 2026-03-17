// ---------------------------------------------------------------------------
// telemetry.hpp — telemetry ingestion parser interface
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <cstdint>

namespace cascade {

// Forward declarations (full headers included only in .cpp)
class StateStore;
class SimClock;

// Result returned by parse_telemetry()
struct TelemetryResult {
    int         processed_count   = 0;
    std::string timestamp;         // ISO-8601 string from request body
    bool        timestamp_parsed  = false;
    bool        parse_ok          = false;  // false if JSON was malformed
};

// Parse a POST /api/telemetry JSON body using simdjson On-Demand.
//
// For each object in "objects[]" the function calls store.upsert().
// If the sim clock is uninitialized, it is set from the request timestamp.
//
// Thread safety: caller must hold an exclusive lock on the global state mutex
// before calling this function.
TelemetryResult parse_telemetry(const std::string& body,
                                StateStore&        store,
                                SimClock&          clock);

} // namespace cascade
