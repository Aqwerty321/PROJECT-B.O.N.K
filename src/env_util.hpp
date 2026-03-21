// ---------------------------------------------------------------------------
// env_util.hpp — environment-variable helpers (replaces 5 independent copies)
// ---------------------------------------------------------------------------
#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace cascade {
namespace env_util {

// Simple 2-arg variant: read env var as double, return fallback if missing/invalid.
// Used by propagator.cpp, narrow_phase_false_negative_gate.cpp.
inline double env_double(const char* name, double fallback) noexcept
{
    const char* v = std::getenv(name);
    if (!v) return fallback;
    char* end = nullptr;
    const double d = std::strtod(v, &end);
    if (end == v || !std::isfinite(d)) return fallback;
    return d;
}

// Clamped 4-arg variant: parse env var with min/max bounds.
// Used by engine_runtime.cpp, maneuver_recovery_planner.cpp.
inline double env_double(std::string_view key,
                         double default_value,
                         double min_value,
                         double max_value) noexcept
{
    const char* raw = std::getenv(std::string(key).c_str());
    if (raw == nullptr) {
        return default_value;
    }

    char* end = nullptr;
    const double parsed = std::strtod(raw, &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return default_value;
    }

    if (parsed < min_value || parsed > max_value) {
        return default_value;
    }

    return parsed;
}

} // namespace env_util
} // namespace cascade
