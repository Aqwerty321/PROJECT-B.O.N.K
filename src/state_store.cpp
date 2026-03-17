// ---------------------------------------------------------------------------
// state_store.cpp
// ---------------------------------------------------------------------------
#include "state_store.hpp"

#include <functional>

namespace cascade {

std::size_t StateStore::TransparentStringHash::operator()(std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(sv);
}

std::size_t StateStore::TransparentStringHash::operator()(const std::string& s) const noexcept {
    return std::hash<std::string_view>{}(s);
}

bool StateStore::TransparentStringEq::operator()(std::string_view a, std::string_view b) const noexcept {
    return a == b;
}

bool StateStore::TransparentStringEq::operator()(const std::string& a, const std::string& b) const noexcept {
    return a == b;
}

bool StateStore::TransparentStringEq::operator()(const std::string& a, std::string_view b) const noexcept {
    return std::string_view(a) == b;
}

bool StateStore::TransparentStringEq::operator()(std::string_view a, const std::string& b) const noexcept {
    return a == std::string_view(b);
}

StateStore::StateStore(std::size_t cap)
{
    ids_.reserve(cap);
    types_.reserve(cap);
    rx_.reserve(cap); ry_.reserve(cap); rz_.reserve(cap);
    vx_.reserve(cap); vy_.reserve(cap); vz_.reserve(cap);
    fuel_kg_.reserve(cap);
    mass_kg_.reserve(cap);
    sat_status_.reserve(cap);
    telemetry_epoch_s_.reserve(cap);

    a_km_.reserve(cap); e_.reserve(cap); i_rad_.reserve(cap);
    raan_rad_.reserve(cap); argp_rad_.reserve(cap); M_rad_.reserve(cap);
    n_rad_s_.reserve(cap); p_km_.reserve(cap); rp_km_.reserve(cap); ra_km_.reserve(cap);
    elements_valid_.reserve(cap);

    index_map_.reserve(cap);
    conflicts_by_source_.reserve(32);
}

bool StateStore::upsert(std::string_view id, ObjectType type,
                        double rx, double ry, double rz,
                        double vx, double vy, double vz,
                        double telemetry_epoch_s,
                        bool& conflict_out)
{
    conflict_out = false;

    auto it = index_map_.find(id);
    if (it != index_map_.end()) {
        const std::size_t i = it->second;
        const ObjectType stored = static_cast<ObjectType>(types_[i]);
        if (stored != type) {
            conflict_out = true;
            return false;
        }
        rx_[i] = rx; ry_[i] = ry; rz_[i] = rz;
        vx_[i] = vx; vy_[i] = vy; vz_[i] = vz;
        telemetry_epoch_s_[i] = telemetry_epoch_s;
        elements_valid_[i] = 0;
        return false;
    }

    const std::size_t i = ids_.size();
    ids_.emplace_back(id);
    types_.push_back(static_cast<uint8_t>(type));
    rx_.push_back(rx); ry_.push_back(ry); rz_.push_back(rz);
    vx_.push_back(vx); vy_.push_back(vy); vz_.push_back(vz);
    telemetry_epoch_s_.push_back(telemetry_epoch_s);

    if (type == ObjectType::SATELLITE) {
        fuel_kg_.push_back(SAT_INITIAL_FUEL_KG);
        mass_kg_.push_back(SAT_WET_MASS_KG);
        sat_status_.push_back(static_cast<uint8_t>(SatStatus::NOMINAL));
        ++sat_count_;
    } else {
        fuel_kg_.push_back(0.0);
        mass_kg_.push_back(0.0);
        sat_status_.push_back(static_cast<uint8_t>(SatStatus::NOMINAL));
        ++deb_count_;
    }

    a_km_.push_back(0.0); e_.push_back(0.0); i_rad_.push_back(0.0);
    raan_rad_.push_back(0.0); argp_rad_.push_back(0.0); M_rad_.push_back(0.0);
    n_rad_s_.push_back(0.0); p_km_.push_back(0.0); rp_km_.push_back(0.0); ra_km_.push_back(0.0);
    elements_valid_.push_back(0);

    index_map_.emplace(ids_[i], i);
    return true;
}

std::size_t StateStore::find(std::string_view id) const noexcept
{
    auto it = index_map_.find(id);
    if (it == index_map_.end()) return ids_.size();
    return it->second;
}

void StateStore::record_type_conflict(std::string_view object_id,
                                      ObjectType stored_type,
                                      ObjectType incoming_type,
                                      std::string_view telemetry_timestamp,
                                      double ingestion_unix_s,
                                      std::string_view source_id,
                                      std::string_view reason)
{
    TypeConflictRecord rec;
    rec.object_id = std::string(object_id);
    rec.stored_type = stored_type;
    rec.incoming_type = incoming_type;
    rec.telemetry_timestamp = std::string(telemetry_timestamp);
    rec.ingestion_unix_s = ingestion_unix_s;
    rec.source_id = std::string(source_id);
    rec.reason = std::string(reason);

    if (conflict_ring_size_ < CONFLICT_RING_CAP) {
        const std::size_t tail = (conflict_ring_head_ + conflict_ring_size_) % CONFLICT_RING_CAP;
        conflict_ring_[tail] = std::move(rec);
        ++conflict_ring_size_;
    } else {
        conflict_ring_[conflict_ring_head_] = std::move(rec);
        conflict_ring_head_ = (conflict_ring_head_ + 1) % CONFLICT_RING_CAP;
    }

    ++total_type_conflicts_;
    ++conflicts_by_source_[std::string(source_id)];
}

std::uint64_t StateStore::conflicts_from_source(std::string_view source_id) const noexcept
{
    auto it = conflicts_by_source_.find(source_id);
    if (it == conflicts_by_source_.end()) return 0;
    return it->second;
}

std::vector<TypeConflictRecord> StateStore::conflict_history_snapshot() const
{
    std::vector<TypeConflictRecord> out;
    out.reserve(conflict_ring_size_);
    for (std::size_t i = 0; i < conflict_ring_size_; ++i) {
        const std::size_t idx = (conflict_ring_head_ + i) % CONFLICT_RING_CAP;
        out.push_back(conflict_ring_[idx]);
    }
    return out;
}

std::vector<std::pair<std::string, std::uint64_t>> StateStore::conflicts_by_source_snapshot() const
{
    std::vector<std::pair<std::string, std::uint64_t>> out;
    out.reserve(conflicts_by_source_.size());
    for (const auto& kv : conflicts_by_source_) {
        out.emplace_back(kv.first, kv.second);
    }
    return out;
}

} // namespace cascade
