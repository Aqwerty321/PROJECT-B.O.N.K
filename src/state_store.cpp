// ---------------------------------------------------------------------------
// state_store.cpp
// ---------------------------------------------------------------------------
#include "state_store.hpp"
#include "types.hpp"

#include <cassert>

namespace cascade {

StateStore::StateStore(std::size_t cap)
{
    ids_.reserve(cap);
    types_.reserve(cap);
    rx_.reserve(cap);
    ry_.reserve(cap);
    rz_.reserve(cap);
    vx_.reserve(cap);
    vy_.reserve(cap);
    vz_.reserve(cap);
    fuel_kg_.reserve(cap);
    mass_kg_.reserve(cap);
    sat_status_.reserve(cap);
    index_map_.reserve(cap);
}

bool StateStore::upsert(std::string_view id, ObjectType type,
                        double rx, double ry, double rz,
                        double vx, double vy, double vz)
{
    // --- update existing ---
    auto it = index_map_.find(std::string(id));
    if (it != index_map_.end()) {
        std::size_t i = it->second;
        rx_[i] = rx;  ry_[i] = ry;  rz_[i] = rz;
        vx_[i] = vx;  vy_[i] = vy;  vz_[i] = vz;
        // Note: type, fuel, mass, status are NOT updated on re-ingestion.
        return false;
    }

    // --- insert new ---
    std::size_t i = ids_.size();

    ids_.emplace_back(id);
    types_.push_back(static_cast<uint8_t>(type));
    rx_.push_back(rx);  ry_.push_back(ry);  rz_.push_back(rz);
    vx_.push_back(vx);  vy_.push_back(vy);  vz_.push_back(vz);

    if (type == ObjectType::SATELLITE) {
        fuel_kg_.push_back(SAT_INITIAL_FUEL_KG);
        mass_kg_.push_back(SAT_WET_MASS_KG);
        sat_status_.push_back(static_cast<uint8_t>(SatStatus::NOMINAL));
        ++sat_count_;
    } else {
        fuel_kg_.push_back(0.0);
        mass_kg_.push_back(0.0);
        sat_status_.push_back(static_cast<uint8_t>(SatStatus::NOMINAL)); // unused
        ++deb_count_;
    }

    index_map_.emplace(std::string(id), i);
    return true;
}

std::size_t StateStore::find(std::string_view id) const noexcept
{
    auto it = index_map_.find(std::string(id));
    if (it == index_map_.end()) return ids_.size();
    return it->second;
}

} // namespace cascade
