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
#include <cstdint>

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
// Returns true on success.
// ---------------------------------------------------------------------------
inline bool parse_iso8601(std::string_view sv, double& out_unix_s) {
    if (sv.size() < 20 || sv.size() > 31) return false;

    char copy[32] = {};
    std::memcpy(copy, sv.data(), sv.size());

    int year = 0, month = 0, day = 0;
    int hour = 0, min = 0, sec = 0, millis = 0;
    char z = '\0';
    int consumed = 0;

    // Try milliseconds form first.
    int n = std::sscanf(copy,
        "%4d-%2d-%2dT%2d:%2d:%2d.%3d%c%n",
        &year, &month, &day, &hour, &min, &sec, &millis, &z, &consumed);

    if (!(n == 8 && z == 'Z' && static_cast<std::size_t>(consumed) == sv.size())) {
        millis = 0;
        z = '\0';
        consumed = 0;
        n = std::sscanf(copy,
            "%4d-%2d-%2dT%2d:%2d:%2d%c%n",
            &year, &month, &day, &hour, &min, &sec, &z, &consumed);
        if (!(n == 7 && z == 'Z' && static_cast<std::size_t>(consumed) == sv.size())) {
            return false;
        }
    }

    if (month < 1 || month > 12) return false;
    if (day   < 1 || day   > 31) return false;
    if (hour  < 0 || hour  > 23) return false;
    if (min   < 0 || min   > 59) return false;
    if (sec   < 0 || sec   > 60) return false;
    if (millis < 0 || millis > 999) return false;

    std::tm tm_val{};
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon  = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hour;
    tm_val.tm_min  = min;
    tm_val.tm_sec  = sec;
    tm_val.tm_isdst = 0;

    std::time_t t = timegm(&tm_val);
    if (t == static_cast<std::time_t>(-1)) {
        return false;
    }

    out_unix_s = static_cast<double>(t) + static_cast<double>(millis) * 0.001;
    return true;
}

// ---------------------------------------------------------------------------
// fmt_double — snprintf wrapper for fixed-precision double formatting
// ---------------------------------------------------------------------------
inline std::string fmt_double(double v, int decimals = 6) {
    if (!std::isfinite(v)) {
        v = 0.0;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// append_json_string — append escaped JSON string, including quotes
// ---------------------------------------------------------------------------
inline void append_json_string(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char esc[7];
                    std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned int>(c));
                    out += esc;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

// ---------------------------------------------------------------------------
// now_unix_epoch_s — system wall-clock in Unix epoch seconds
// ---------------------------------------------------------------------------
inline double now_unix_epoch_s() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<double>(ts.tv_sec)
        + static_cast<double>(ts.tv_nsec) * 1.0e-9;
}

} // namespace cascade
