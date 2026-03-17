// ---------------------------------------------------------------------------
// state_store.hpp — Structure-of-Arrays (SoA) in-process state store
//
// Layout: 6 contiguous double arrays for ECI position/velocity (cache-friendly
// for the propagation and screening loops), parallel arrays for IDs, types,
// satellite-specific fields (fuel, mass, status), and an unordered_map for
// O(1) ID → index lookup.
//
// Thread safety: NONE — callers (main.cpp) must hold an external lock.
// ---------------------------------------------------------------------------
#pragma once

#include "types.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <cstddef>
#include <cstdint>

namespace cascade {

class StateStore {
public:
    // Pre-reserve to avoid reallocation during ingestion of up to
    // DEFAULT_CAPACITY objects.  Can be overridden for testing.
    explicit StateStore(std::size_t reserve_capacity = DEFAULT_CAPACITY);

    // -----------------------------------------------------------------------
    // Mutations
    // -----------------------------------------------------------------------

    // Insert or update an object.
    //   - If id is new: appends to all arrays; returns true.
    //   - If id exists: overwrites r/v in place; returns false.
    //   Satellite-specific fields (fuel, mass, status) are only initialised
    //   on first insertion and are NOT overwritten by subsequent upserts.
    bool upsert(std::string_view id, ObjectType type,
                double rx, double ry, double rz,
                double vx, double vy, double vz);

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    // Total number of tracked objects (satellites + debris)
    std::size_t size()           const noexcept { return ids_.size(); }
    std::size_t satellite_count() const noexcept { return sat_count_; }
    std::size_t debris_count()    const noexcept { return deb_count_; }

    // Index-based read accessors (bounds-unchecked — caller ensures i < size())
    const std::string& id(std::size_t i) const noexcept { return ids_[i]; }
    ObjectType  type(std::size_t i) const noexcept { return static_cast<ObjectType>(types_[i]); }

    double rx(std::size_t i) const noexcept { return rx_[i]; }
    double ry(std::size_t i) const noexcept { return ry_[i]; }
    double rz(std::size_t i) const noexcept { return rz_[i]; }
    double vx(std::size_t i) const noexcept { return vx_[i]; }
    double vy(std::size_t i) const noexcept { return vy_[i]; }
    double vz(std::size_t i) const noexcept { return vz_[i]; }

    double    fuel_kg(std::size_t i)   const noexcept { return fuel_kg_[i]; }
    double    mass_kg(std::size_t i)   const noexcept { return mass_kg_[i]; }
    SatStatus sat_status(std::size_t i) const noexcept { return static_cast<SatStatus>(sat_status_[i]); }

    // Mutable references for propagation/maneuver phases
    double& rx_mut(std::size_t i) noexcept { return rx_[i]; }
    double& ry_mut(std::size_t i) noexcept { return ry_[i]; }
    double& rz_mut(std::size_t i) noexcept { return rz_[i]; }
    double& vx_mut(std::size_t i) noexcept { return vx_[i]; }
    double& vy_mut(std::size_t i) noexcept { return vy_[i]; }
    double& vz_mut(std::size_t i) noexcept { return vz_[i]; }
    double& fuel_kg_mut(std::size_t i) noexcept { return fuel_kg_[i]; }
    double& mass_kg_mut(std::size_t i) noexcept { return mass_kg_[i]; }
    void    set_sat_status(std::size_t i, SatStatus s) noexcept { sat_status_[i] = static_cast<uint8_t>(s); }

    // ID → index lookup.  Returns size() (i.e. past-the-end) if not found.
    std::size_t find(std::string_view id) const noexcept;

    // Raw const pointer to rx array — for SIMD / bulk copy in future phases
    const double* rx_data() const noexcept { return rx_.data(); }
    const double* ry_data() const noexcept { return ry_.data(); }
    const double* rz_data() const noexcept { return rz_.data(); }
    const double* vx_data() const noexcept { return vx_.data(); }
    const double* vy_data() const noexcept { return vy_.data(); }
    const double* vz_data() const noexcept { return vz_.data(); }

private:
    // -----------------------------------------------------------------------
    // SoA arrays — all indexed by object index [0 .. size()-1]
    // -----------------------------------------------------------------------
    std::vector<std::string>  ids_;      // unique object ID strings
    std::vector<uint8_t>      types_;    // ObjectType cast to uint8_t

    // ECI state (km, km/s)
    std::vector<double> rx_, ry_, rz_;
    std::vector<double> vx_, vy_, vz_;

    // Satellite-specific (zero-filled for debris; ignored during debris ops)
    std::vector<double>   fuel_kg_;    // current propellant, kg
    std::vector<double>   mass_kg_;    // current total (dry + fuel), kg
    std::vector<uint8_t>  sat_status_; // SatStatus cast to uint8_t

    // Fast ID → index map
    std::unordered_map<std::string, std::size_t> index_map_;

    // Counters
    std::size_t sat_count_ = 0;
    std::size_t deb_count_ = 0;
};

} // namespace cascade
