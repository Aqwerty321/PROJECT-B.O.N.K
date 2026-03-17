// ---------------------------------------------------------------------------
// state_store.hpp — Structure-of-Arrays (SoA) in-process state store
// ---------------------------------------------------------------------------
#pragma once

#include "types.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cascade {

struct TypeConflictRecord {
    std::string object_id;
    ObjectType  stored_type = ObjectType::DEBRIS;
    ObjectType  incoming_type = ObjectType::DEBRIS;
    std::string telemetry_timestamp;
    double      ingestion_unix_s = 0.0;
    std::string source_id;
    std::string reason;
};

class StateStore {
public:
    explicit StateStore(std::size_t reserve_capacity = DEFAULT_CAPACITY);

    // Insert or update an object at a given telemetry epoch.
    // Returns true if inserted as new object, false if existing object updated.
    // If type conflict is detected for an existing object, no update occurs and
    // the call returns false with conflict_out=true.
    bool upsert(std::string_view id, ObjectType type,
                double rx, double ry, double rz,
                double vx, double vy, double vz,
                double telemetry_epoch_s,
                bool& conflict_out);

    std::size_t size()            const noexcept { return ids_.size(); }
    std::size_t satellite_count() const noexcept { return sat_count_; }
    std::size_t debris_count()    const noexcept { return deb_count_; }

    const std::string& id(std::size_t i) const noexcept { return ids_[i]; }
    ObjectType type(std::size_t i) const noexcept { return static_cast<ObjectType>(types_[i]); }

    double rx(std::size_t i) const noexcept { return rx_[i]; }
    double ry(std::size_t i) const noexcept { return ry_[i]; }
    double rz(std::size_t i) const noexcept { return rz_[i]; }
    double vx(std::size_t i) const noexcept { return vx_[i]; }
    double vy(std::size_t i) const noexcept { return vy_[i]; }
    double vz(std::size_t i) const noexcept { return vz_[i]; }

    double    fuel_kg(std::size_t i) const noexcept { return fuel_kg_[i]; }
    double    mass_kg(std::size_t i) const noexcept { return mass_kg_[i]; }
    SatStatus sat_status(std::size_t i) const noexcept { return static_cast<SatStatus>(sat_status_[i]); }

    double telemetry_epoch_s(std::size_t i) const noexcept { return telemetry_epoch_s_[i]; }
    bool   elements_valid(std::size_t i) const noexcept { return elements_valid_[i] != 0; }

    // Derived orbital element SoA
    double a_km(std::size_t i) const noexcept { return a_km_[i]; }
    double e(std::size_t i) const noexcept { return e_[i]; }
    double i_rad(std::size_t i) const noexcept { return i_rad_[i]; }
    double raan_rad(std::size_t i) const noexcept { return raan_rad_[i]; }
    double argp_rad(std::size_t i) const noexcept { return argp_rad_[i]; }
    double M_rad(std::size_t i) const noexcept { return M_rad_[i]; }
    double n_rad_s(std::size_t i) const noexcept { return n_rad_s_[i]; }
    double p_km(std::size_t i) const noexcept { return p_km_[i]; }
    double rp_km(std::size_t i) const noexcept { return rp_km_[i]; }
    double ra_km(std::size_t i) const noexcept { return ra_km_[i]; }

    // Best-effort runtime failure counters for propagation
    std::uint64_t failed_propagation_total() const noexcept { return failed_propagation_total_; }
    std::uint64_t failed_last_tick() const noexcept { return failed_last_tick_; }
    void set_failed_last_tick(std::uint64_t n) noexcept {
        failed_last_tick_ = n;
        failed_propagation_total_ += n;
    }

    // Mutable references for propagation/maneuver phases
    double& rx_mut(std::size_t i) noexcept { return rx_[i]; }
    double& ry_mut(std::size_t i) noexcept { return ry_[i]; }
    double& rz_mut(std::size_t i) noexcept { return rz_[i]; }
    double& vx_mut(std::size_t i) noexcept { return vx_[i]; }
    double& vy_mut(std::size_t i) noexcept { return vy_[i]; }
    double& vz_mut(std::size_t i) noexcept { return vz_[i]; }
    double& fuel_kg_mut(std::size_t i) noexcept { return fuel_kg_[i]; }
    double& mass_kg_mut(std::size_t i) noexcept { return mass_kg_[i]; }
    void set_sat_status(std::size_t i, SatStatus s) noexcept { sat_status_[i] = static_cast<uint8_t>(s); }

    void set_telemetry_epoch_s(std::size_t i, double epoch_s) noexcept { telemetry_epoch_s_[i] = epoch_s; }

    void set_elements(std::size_t i, const OrbitalElements& el, bool valid) noexcept {
        a_km_[i]     = el.a_km;
        e_[i]        = el.e;
        i_rad_[i]    = el.i_rad;
        raan_rad_[i] = el.raan_rad;
        argp_rad_[i] = el.argp_rad;
        M_rad_[i]    = el.M_rad;
        n_rad_s_[i]  = el.n_rad_s;
        p_km_[i]     = el.p_km;
        rp_km_[i]    = el.rp_km;
        ra_km_[i]    = el.ra_km;
        elements_valid_[i] = static_cast<uint8_t>(valid ? 1 : 0);
    }

    std::size_t find(std::string_view id) const noexcept;

    // Conflict audit
    void record_type_conflict(std::string_view object_id,
                              ObjectType stored_type,
                              ObjectType incoming_type,
                              std::string_view telemetry_timestamp,
                              double ingestion_unix_s,
                              std::string_view source_id,
                              std::string_view reason);

    std::uint64_t total_type_conflicts() const noexcept { return total_type_conflicts_; }

    std::uint64_t conflicts_from_source(std::string_view source_id) const noexcept;

    std::vector<std::pair<std::string, std::uint64_t>> conflicts_by_source_snapshot() const;

    // Returns oldest->newest snapshot copy for endpoint serialization.
    std::vector<TypeConflictRecord> conflict_history_snapshot() const;

    // Raw pointers for future SIMD loops
    const double* rx_data() const noexcept { return rx_.data(); }
    const double* ry_data() const noexcept { return ry_.data(); }
    const double* rz_data() const noexcept { return rz_.data(); }
    const double* vx_data() const noexcept { return vx_.data(); }
    const double* vy_data() const noexcept { return vy_.data(); }
    const double* vz_data() const noexcept { return vz_.data(); }

private:
    struct TransparentStringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view sv) const noexcept;
        std::size_t operator()(const std::string& s) const noexcept;
    };

    struct TransparentStringEq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept;
        bool operator()(const std::string& a, const std::string& b) const noexcept;
        bool operator()(const std::string& a, std::string_view b) const noexcept;
        bool operator()(std::string_view a, const std::string& b) const noexcept;
    };

    // Core SoA fields
    std::vector<std::string> ids_;
    std::vector<uint8_t>     types_;
    std::vector<double> rx_, ry_, rz_;
    std::vector<double> vx_, vy_, vz_;
    std::vector<double> fuel_kg_;
    std::vector<double> mass_kg_;
    std::vector<uint8_t> sat_status_;

    // Metadata
    std::vector<double> telemetry_epoch_s_;

    // Derived orbital elements
    std::vector<double> a_km_, e_, i_rad_, raan_rad_, argp_rad_, M_rad_;
    std::vector<double> n_rad_s_, p_km_, rp_km_, ra_km_;
    std::vector<uint8_t> elements_valid_;

    std::unordered_map<std::string, std::size_t, TransparentStringHash, TransparentStringEq> index_map_;
    std::size_t sat_count_ = 0;
    std::size_t deb_count_ = 0;

    // Type conflict ring buffer (fixed 4096 entries)
    static constexpr std::size_t CONFLICT_RING_CAP = 4096;
    std::array<TypeConflictRecord, CONFLICT_RING_CAP> conflict_ring_{};
    std::size_t conflict_ring_head_ = 0;
    std::size_t conflict_ring_size_ = 0;
    std::uint64_t total_type_conflicts_ = 0;
    std::unordered_map<std::string, std::uint64_t, TransparentStringHash, TransparentStringEq> conflicts_by_source_;

    // Best-effort propagation failure counters
    std::uint64_t failed_propagation_total_ = 0;
    std::uint64_t failed_last_tick_ = 0;
};

} // namespace cascade
