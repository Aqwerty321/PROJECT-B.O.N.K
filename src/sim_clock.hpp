// ---------------------------------------------------------------------------
// sim_clock.hpp — simulation epoch clock
//   Stores time as double (Unix epoch seconds) for fast arithmetic.
//   Converts to/from ISO-8601 only when building HTTP responses.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace cascade {

class SimClock {
public:
    SimClock();   // Records wall-clock start time; epoch starts uninitialized

    // Set simulation epoch from an ISO-8601 string ("2026-03-12T08:00:00.000Z")
    bool set_from_iso(std::string_view iso);

    // Set simulation epoch directly using Unix epoch seconds.
    void set_epoch_s(double epoch_s) noexcept {
        epoch_s_ = epoch_s;
        initialized_ = true;
    }

    // Advance simulation time by step_seconds (may be fractional)
    void advance(double step_seconds) noexcept;

    // Current simulation time as Unix epoch seconds (fast, no allocation)
    double epoch_s() const noexcept { return epoch_s_; }

    // Current simulation time as ISO-8601 string (allocates a 24-char string)
    std::string to_iso() const;

    // Wall-clock seconds elapsed since this SimClock was constructed
    double uptime_s() const noexcept;

    // True once set_from_iso() has been called at least once
    bool is_initialized() const noexcept { return initialized_; }

private:
    double   epoch_s_      = 0.0;
    bool     initialized_  = false;
    long long wall_start_ns_ = 0;  // steady_clock nanoseconds at construction
};

} // namespace cascade
