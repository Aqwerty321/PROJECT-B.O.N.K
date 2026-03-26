// ---------------------------------------------------------------------------
// types.hpp — core type aliases, Vec3, ObjectType, physics/propulsion constants
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>

namespace cascade {

// ---------------------------------------------------------------------------
// Physics constants (SI-compatible, all distances in km, time in seconds)
// ---------------------------------------------------------------------------
inline constexpr double MU_KM3_S2          = 398600.4418;   // km³/s²
inline constexpr double R_EARTH_KM         = 6378.137;      // km
inline constexpr double J2                 = 1.08263e-3;    // dimensionless
inline constexpr double G0_M_S2            = 9.80665;       // m/s²  (Tsiolkovsky)
inline constexpr double G0_KM_S2           = G0_M_S2 * 1.0e-3; // km/s²
inline constexpr double PI                 = 3.14159265358979323846;
inline constexpr double TWO_PI             = 2.0 * PI;
inline constexpr double EPS_NUM            = 1e-9;

// ---------------------------------------------------------------------------
// Satellite propulsion constants (PS.md §5)
// ---------------------------------------------------------------------------
inline constexpr double SAT_DRY_MASS_KG        = 500.0;        // kg
inline constexpr double SAT_INITIAL_FUEL_KG    = 50.0;         // kg
inline constexpr double SAT_WET_MASS_KG        = SAT_DRY_MASS_KG + SAT_INITIAL_FUEL_KG;
inline constexpr double SAT_ISP_S              = 300.0;        // s
inline constexpr double SAT_MAX_DELTAV_KM_S    = 15.0e-3;      // km/s  (15 m/s)
inline constexpr double SAT_COOLDOWN_S         = 600.0;        // s
inline constexpr double SAT_FUEL_EOL_KG        = SAT_INITIAL_FUEL_KG * 0.05; // 2.5 kg guard

// ---------------------------------------------------------------------------
// Conjunction / collision threshold (PS.md §3)
// ---------------------------------------------------------------------------
inline constexpr double COLLISION_THRESHOLD_KM = 0.100;   // 100 m

// ---------------------------------------------------------------------------
// Tiered screening threshold for predictive CDM scan (additive, PS-safe).
// Records within this range are surfaced as watch/warning events for the UI
// but do NOT trigger auto-COLA. Only COLLISION_THRESHOLD_KM triggers burns.
// ---------------------------------------------------------------------------
inline constexpr double SCREENING_THRESHOLD_KM = 5.0;     // 5 km

// ---------------------------------------------------------------------------
// CDM severity tiers — additive classification for conjunction records.
// Only CRITICAL events (<= COLLISION_THRESHOLD_KM) trigger autonomous
// collision avoidance.  WARNING and WATCH are informational for the UI.
// ---------------------------------------------------------------------------
enum class CdmSeverity : uint8_t {
    CRITICAL = 0,   // miss < COLLISION_THRESHOLD_KM  (100 m)
    WARNING  = 1,   // miss < 1 km
    WATCH    = 2    // miss < SCREENING_THRESHOLD_KM  (5 km)
};

inline const char* cdm_severity_str(CdmSeverity s) noexcept {
    switch (s) {
        case CdmSeverity::CRITICAL: return "critical";
        case CdmSeverity::WARNING:  return "warning";
        case CdmSeverity::WATCH:    return "watch";
        default:                    return "unknown";
    }
}

inline CdmSeverity classify_miss_distance(double miss_km) noexcept {
    if (miss_km <= COLLISION_THRESHOLD_KM) return CdmSeverity::CRITICAL;
    if (miss_km <= 1.0)                    return CdmSeverity::WARNING;
    return CdmSeverity::WATCH;
}

// ---------------------------------------------------------------------------
// Pre-allocation capacity (50 sats + 10 000 debris + small margin)
// ---------------------------------------------------------------------------
inline constexpr std::size_t DEFAULT_CAPACITY  = 10'100;

// ---------------------------------------------------------------------------
// Object type
// ---------------------------------------------------------------------------
enum class ObjectType : uint8_t {
    SATELLITE = 0,
    DEBRIS    = 1
};

inline const char* object_type_str(ObjectType t) noexcept {
    switch (t) {
        case ObjectType::SATELLITE: return "SATELLITE";
        case ObjectType::DEBRIS:    return "DEBRIS";
        default:                    return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// Satellite operational status
// ---------------------------------------------------------------------------
enum class SatStatus : uint8_t {
    NOMINAL      = 0,
    MANEUVERING  = 1,
    FUEL_LOW     = 2,
    OFFLINE      = 3
};

inline const char* sat_status_str(SatStatus s) noexcept {
    switch (s) {
        case SatStatus::NOMINAL:     return "NOMINAL";
        case SatStatus::MANEUVERING: return "MANEUVERING";
        case SatStatus::FUEL_LOW:    return "FUEL_LOW";
        case SatStatus::OFFLINE:     return "OFFLINE";
        default:                     return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// Minimal 3D vector used in API-layer code (hot path uses raw SoA arrays)
// ---------------------------------------------------------------------------
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// ---------------------------------------------------------------------------
// Derived classical orbital elements (all angles in radians)
// ---------------------------------------------------------------------------
struct OrbitalElements {
    double a_km       = 0.0; // semi-major axis
    double e          = 0.0; // eccentricity
    double i_rad      = 0.0; // inclination
    double raan_rad   = 0.0; // right ascension of ascending node
    double argp_rad   = 0.0; // argument of perigee
    double M_rad      = 0.0; // mean anomaly
    double n_rad_s    = 0.0; // mean motion
    double p_km       = 0.0; // semi-latus rectum
    double rp_km      = 0.0; // perigee radius
    double ra_km      = 0.0; // apogee radius
};

inline double wrap_0_2pi(double angle_rad) noexcept {
    double x = std::fmod(angle_rad, TWO_PI);
    if (x < 0.0) x += TWO_PI;
    return x;
}

} // namespace cascade
