// ---------------------------------------------------------------------------
// json_util.hpp — lightweight JSON helpers (header-only)
//   iso8601()       : double (Unix epoch s) → ISO-8601 string
//   parse_iso8601() : ISO-8601 string → double (Unix epoch s)
//   fmt_double()    : double → fixed-precision string
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <string_view>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>

namespace cascade {

// ---------------------------------------------------------------------------
// iso8601 — format Unix epoch seconds as "YYYY-MM-DDTHH:MM:SS.mmmZ"
// ---------------------------------------------------------------------------
inline std::string iso8601(double unix_s) {
    long long whole = static_cast<long long>(unix_s);
    int millis = static_cast<int>(std::round((unix_s - static_cast<double>(whole)) * 1000.0));
    if (millis >= 1000) { millis -= 1000; ++whole; }
    if (millis <     0) { millis += 1000; --whole; }

    std::time_t t = static_cast<std::time_t>(whole);
    std::tm tm_val{};
    gmtime_r(&t, &tm_val);

    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
        tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec, millis);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// parse_iso8601 — parse "YYYY-MM-DDTHH:MM:SS[.mmm]Z" → Unix epoch seconds
// Returns 0.0 on parse failure.
// ---------------------------------------------------------------------------
inline double parse_iso8601(std::string_view sv) {
    if (sv.size() < 20) return 0.0;

    // Copy to null-terminated buffer (max 32 chars expected)
    char copy[32] = {};
    std::size_t len = sv.size() < sizeof(copy) - 1 ? sv.size() : sizeof(copy) - 1;
    std::memcpy(copy, sv.data(), len);

    std::tm tm_val{};
    int millis = 0;
    int n = std::sscanf(copy, "%4d-%2d-%2dT%2d:%2d:%2d.%3d",
        &tm_val.tm_year, &tm_val.tm_mon, &tm_val.tm_mday,
        &tm_val.tm_hour, &tm_val.tm_min, &tm_val.tm_sec, &millis);

    if (n < 6) return 0.0;

    tm_val.tm_year -= 1900;
    tm_val.tm_mon  -= 1;
    tm_val.tm_isdst = 0;

    std::time_t t = timegm(&tm_val);  // POSIX, available on glibc (Ubuntu 22.04)
    return static_cast<double>(t) + millis * 0.001;
}

// ---------------------------------------------------------------------------
// fmt_double — snprintf wrapper for fixed-precision double formatting
// ---------------------------------------------------------------------------
inline std::string fmt_double(double v, int decimals = 6) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return std::string(buf);
}

} // namespace cascade
