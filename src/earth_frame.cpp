// ---------------------------------------------------------------------------
// earth_frame.cpp
// ---------------------------------------------------------------------------
#include "earth_frame.hpp"

#include <cmath>

namespace cascade {

namespace {

inline Vec3 rotate_z(const Vec3& v, double angle_rad) noexcept
{
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    return Vec3{
        c * v.x + s * v.y,
        -s * v.x + c * v.y,
        v.z
    };
}

inline double wrap_pm_pi(double rad) noexcept
{
    double x = std::fmod(rad + PI, TWO_PI);
    if (x < 0.0) x += TWO_PI;
    return x - PI;
}

} // namespace

double gmst_rad(double unix_epoch_s) noexcept
{
    const double jd = unix_epoch_s / 86400.0 + 2440587.5;
    const double d = jd - 2451545.0;
    const double t = d / 36525.0;

    double gmst_deg = 280.46061837
                    + 360.98564736629 * d
                    + 0.000387933 * t * t
                    - (t * t * t) / 38710000.0;

    gmst_deg = std::fmod(gmst_deg, 360.0);
    if (gmst_deg < 0.0) gmst_deg += 360.0;
    return gmst_deg * PI / 180.0;
}

Vec3 eci_to_ecef(const Vec3& eci_km, double unix_epoch_s) noexcept
{
    return rotate_z(eci_km, gmst_rad(unix_epoch_s));
}

Vec3 geodetic_to_ecef(double lat_rad,
                      double lon_rad,
                      double alt_km) noexcept
{
    // WGS-84
    constexpr double a = 6378.137;
    constexpr double f = 1.0 / 298.257223563;
    constexpr double e2 = f * (2.0 - f);

    const double s_lat = std::sin(lat_rad);
    const double c_lat = std::cos(lat_rad);
    const double s_lon = std::sin(lon_rad);
    const double c_lon = std::cos(lon_rad);

    const double n = a / std::sqrt(1.0 - e2 * s_lat * s_lat);

    return Vec3{
        (n + alt_km) * c_lat * c_lon,
        (n + alt_km) * c_lat * s_lon,
        (n * (1.0 - e2) + alt_km) * s_lat
    };
}

bool ecef_to_geodetic(const Vec3& ecef_km,
                      double& lat_deg,
                      double& lon_deg,
                      double& alt_km) noexcept
{
    // WGS-84
    constexpr double a = 6378.137;
    constexpr double f = 1.0 / 298.257223563;
    constexpr double e2 = f * (2.0 - f);

    const double x = ecef_km.x;
    const double y = ecef_km.y;
    const double z = ecef_km.z;
    const double p = std::sqrt(x * x + y * y);

    if (p < EPS_NUM && std::abs(z) < EPS_NUM) {
        lat_deg = 0.0;
        lon_deg = 0.0;
        alt_km = -a;
        return false;
    }

    double lat = std::atan2(z, p * (1.0 - e2));
    for (int i = 0; i < 8; ++i) {
        const double s = std::sin(lat);
        const double n = a / std::sqrt(1.0 - e2 * s * s);
        const double alt = p / std::cos(lat) - n;
        const double lat_next = std::atan2(z, p * (1.0 - e2 * n / (n + alt)));
        if (std::abs(lat_next - lat) < 1e-13) {
            lat = lat_next;
            break;
        }
        lat = lat_next;
    }

    const double lon = std::atan2(y, x);
    const double s = std::sin(lat);
    const double n = a / std::sqrt(1.0 - e2 * s * s);
    alt_km = p / std::cos(lat) - n;

    lat_deg = lat * 180.0 / PI;
    lon_deg = wrap_pm_pi(lon) * 180.0 / PI;
    return true;
}

double elevation_angle_rad(const Vec3& sat_ecef_km,
                           double station_lat_rad,
                           double station_lon_rad,
                           double station_alt_km) noexcept
{
    const Vec3 gs = geodetic_to_ecef(station_lat_rad, station_lon_rad, station_alt_km);
    const Vec3 rel{sat_ecef_km.x - gs.x, sat_ecef_km.y - gs.y, sat_ecef_km.z - gs.z};

    const double s_lat = std::sin(station_lat_rad);
    const double c_lat = std::cos(station_lat_rad);
    const double s_lon = std::sin(station_lon_rad);
    const double c_lon = std::cos(station_lon_rad);

    // ECEF -> ENU
    const double e = -s_lon * rel.x + c_lon * rel.y;
    const double n = -s_lat * c_lon * rel.x - s_lat * s_lon * rel.y + c_lat * rel.z;
    const double u = c_lat * c_lon * rel.x + c_lat * s_lon * rel.y + s_lat * rel.z;
    const double horiz = std::sqrt(e * e + n * n);

    return std::atan2(u, horiz);
}

} // namespace cascade
