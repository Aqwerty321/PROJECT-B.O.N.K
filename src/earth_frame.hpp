// ---------------------------------------------------------------------------
// earth_frame.hpp — ECI/ECEF/geodetic conversion helpers
// ---------------------------------------------------------------------------
#pragma once

#include "types.hpp"

namespace cascade {

double gmst_rad(double unix_epoch_s) noexcept;

Vec3 eci_to_ecef(const Vec3& eci_km, double unix_epoch_s) noexcept;

Vec3 geodetic_to_ecef(double lat_rad,
                      double lon_rad,
                      double alt_km) noexcept;

bool ecef_to_geodetic(const Vec3& ecef_km,
                      double& lat_deg,
                      double& lon_deg,
                      double& alt_km) noexcept;

double elevation_angle_rad(const Vec3& sat_ecef_km,
                           double station_lat_rad,
                           double station_lon_rad,
                           double station_alt_km) noexcept;

} // namespace cascade
