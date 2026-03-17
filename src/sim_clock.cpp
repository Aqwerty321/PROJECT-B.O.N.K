// ---------------------------------------------------------------------------
// sim_clock.cpp
// ---------------------------------------------------------------------------
#include "sim_clock.hpp"
#include "json_util.hpp"

#include <chrono>

namespace cascade {

SimClock::SimClock()
{
    using namespace std::chrono;
    wall_start_ns_ = duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()).count();
}

void SimClock::set_from_iso(std::string_view iso)
{
    epoch_s_    = parse_iso8601(iso);
    initialized_ = true;
}

void SimClock::advance(double step_seconds) noexcept
{
    epoch_s_ += step_seconds;
}

std::string SimClock::to_iso() const
{
    if (!initialized_) return "1970-01-01T00:00:00.000Z";
    return iso8601(epoch_s_);
}

double SimClock::uptime_s() const noexcept
{
    using namespace std::chrono;
    long long now_ns = duration_cast<nanoseconds>(
        steady_clock::now().time_since_epoch()).count();
    return static_cast<double>(now_ns - wall_start_ns_) * 1.0e-9;
}

} // namespace cascade
