// ---------------------------------------------------------------------------
// broad_phase.cpp
// ---------------------------------------------------------------------------
#include "broad_phase.hpp"

#include "state_store.hpp"

#include <cmath>
#include <unordered_map>

namespace cascade {

namespace {

inline double radius_from_xyz(double x, double y, double z) {
    return std::sqrt(x * x + y * y + z * z);
}

inline void shell_bounds(const StateStore& store,
                         std::size_t idx,
                         const BroadPhaseConfig& cfg,
                         double& out_min,
                         double& out_max)
{
    if (store.elements_valid(idx)) {
        out_min = store.rp_km(idx) - cfg.shell_margin_km;
        out_max = store.ra_km(idx) + cfg.shell_margin_km;
        return;
    }

    const double r_now = radius_from_xyz(store.rx(idx), store.ry(idx), store.rz(idx));
    out_min = r_now - cfg.invalid_shell_pad_km - cfg.shell_margin_km;
    out_max = r_now + cfg.invalid_shell_pad_km + cfg.shell_margin_km;
}

inline bool intervals_overlap(double a_min, double a_max,
                              double b_min, double b_max) {
    return !(a_max < b_min || b_max < a_min);
}

inline bool fail_open_object(const StateStore& store,
                             std::size_t idx,
                             const BroadPhaseConfig& cfg)
{
    if (!store.elements_valid(idx)) return true;
    if (store.e(idx) > cfg.high_e_fail_open) return true;

    const double span_km = store.ra_km(idx) - store.rp_km(idx);
    const double max_span_for_index = cfg.band_neighbor_bins * cfg.a_bin_width_km
                                    - 2.0 * cfg.shell_margin_km;
    if (max_span_for_index <= 0.0) return true;
    return span_km > max_span_for_index;
}

inline int a_bin_of(double a_km, double a_bin_width_km) {
    return static_cast<int>(std::floor(a_km / a_bin_width_km));
}

inline int i_bin_of(double i_rad, double i_bin_width_rad) {
    return static_cast<int>(std::floor(i_rad / i_bin_width_rad));
}

inline double dcriterion_simple(const StateStore& store,
                                std::size_t i,
                                std::size_t j)
{
    const double a1 = store.a_km(i);
    const double a2 = store.a_km(j);
    const double e1 = store.e(i);
    const double e2 = store.e(j);
    const double i1 = store.i_rad(i);
    const double i2 = store.i_rad(j);

    const double da = (a1 - a2) / (std::abs(a1 + a2) + EPS_NUM);
    const double de = e1 - e2;
    const double di = 2.0 * std::sin(0.5 * (i1 - i2));
    return std::sqrt(da * da + de * de + di * di);
}

} // namespace

BroadPhaseResult generate_broad_phase_candidates(const StateStore& store,
                                                 const BroadPhaseConfig& cfg)
{
    BroadPhaseResult out;
    out.shell_margin_km = cfg.shell_margin_km;

    if (store.size() == 0 || store.satellite_count() == 0) {
        return out;
    }

    const std::size_t n = store.size();
    const std::size_t sat_count = store.satellite_count();
    const std::size_t deb_count = store.debris_count();
    out.candidates.reserve(sat_count * deb_count);

    std::vector<std::uint32_t> debris_indices;
    debris_indices.reserve(deb_count);

    std::vector<std::uint32_t> fail_open_debris_indices;
    fail_open_debris_indices.reserve(deb_count / 8 + 8);

    std::uint64_t fail_open_objects = 0;

    std::unordered_map<int, std::unordered_map<int, std::vector<std::uint32_t>>> bands;
    bands.reserve(n);

    for (std::size_t idx = 0; idx < n; ++idx) {
        const bool is_debris = store.type(idx) == ObjectType::DEBRIS;
        if (is_debris) {
            debris_indices.push_back(static_cast<std::uint32_t>(idx));
        }

        if (fail_open_object(store, idx, cfg)) {
            ++fail_open_objects;
            if (is_debris) {
                fail_open_debris_indices.push_back(static_cast<std::uint32_t>(idx));
            }
            continue;
        }

        if (!is_debris) {
            continue;
        }

        const int a_bin = a_bin_of(store.a_km(idx), cfg.a_bin_width_km);
        const int i_bin = i_bin_of(store.i_rad(idx), cfg.i_bin_width_rad);
        bands[a_bin][i_bin].push_back(static_cast<std::uint32_t>(idx));
    }

    out.fail_open_objects = fail_open_objects;

    std::vector<std::uint32_t> seen_stamp(n, 0);
    std::uint32_t stamp = 0;
    std::vector<std::uint32_t> selected_debris_indices;
    selected_debris_indices.reserve(deb_count);

    for (std::size_t i = 0; i < n; ++i) {
        if (store.type(i) != ObjectType::SATELLITE) continue;

        // For transparency, count full satellite-vs-debris pair space.
        out.pairs_considered += static_cast<std::uint64_t>(debris_indices.size());

        double sat_min = 0.0;
        double sat_max = 0.0;
        shell_bounds(store, i, cfg, sat_min, sat_max);

        const bool sat_fail_open = fail_open_object(store, i, cfg);
        if (sat_fail_open) {
            ++out.fail_open_satellites;
            for (std::uint32_t j_u32 : debris_indices) {
                const std::size_t j = static_cast<std::size_t>(j_u32);

                ++out.pairs_after_band_index;

                double obj_min = 0.0;
                double obj_max = 0.0;
                shell_bounds(store, j, cfg, obj_min, obj_max);
                if (!intervals_overlap(sat_min, sat_max, obj_min, obj_max)) {
                    continue;
                }

                ++out.shell_overlap_pass;

                if (store.elements_valid(i)
                    && store.elements_valid(j)
                    && store.e(i) <= cfg.high_e_fail_open
                    && store.e(j) <= cfg.high_e_fail_open)
                {
                    const double d = dcriterion_simple(store, i, j);
                    if (cfg.shadow_dcriterion && d > cfg.dcriterion_threshold) {
                        ++out.dcriterion_shadow_rejected;
                    }
                    if (cfg.enable_dcriterion && d > cfg.dcriterion_threshold) {
                        ++out.dcriterion_rejected;
                        continue;
                    }
                }

                out.candidates.push_back(BroadPhasePair{
                    static_cast<std::uint32_t>(i),
                    static_cast<std::uint32_t>(j)
                });
            }
            continue;
        }

        ++stamp;
        if (stamp == 0) {
            std::fill(seen_stamp.begin(), seen_stamp.end(), 0);
            stamp = 1;
        }
        selected_debris_indices.clear();

        // Always include fail-open objects.
        for (std::uint32_t idx : fail_open_debris_indices) {
            if (seen_stamp[idx] != stamp) {
                seen_stamp[idx] = stamp;
                selected_debris_indices.push_back(idx);
            }
        }

        const int sat_a_bin = a_bin_of(store.a_km(i), cfg.a_bin_width_km);
        const int sat_i_bin = i_bin_of(store.i_rad(i), cfg.i_bin_width_rad);

        for (int da = -cfg.band_neighbor_bins; da <= cfg.band_neighbor_bins; ++da) {
            auto a_it = bands.find(sat_a_bin + da);
            if (a_it == bands.end()) continue;

            auto& i_band = a_it->second;
            for (const auto& kv : i_band) {
                for (std::uint32_t idx : kv.second) {
                    if (seen_stamp[idx] != stamp) {
                        seen_stamp[idx] = stamp;
                        selected_debris_indices.push_back(idx);
                    }
                }
            }
        }

        for (std::uint32_t j_u32 : selected_debris_indices) {
            const std::size_t j = static_cast<std::size_t>(j_u32);

            ++out.pairs_after_band_index;

            double obj_min = 0.0;
            double obj_max = 0.0;
            shell_bounds(store, j, cfg, obj_min, obj_max);
            if (!intervals_overlap(sat_min, sat_max, obj_min, obj_max)) {
                continue;
            }

            ++out.shell_overlap_pass;

            if (store.elements_valid(i)
                && store.elements_valid(j)
                && store.e(i) <= cfg.high_e_fail_open
                && store.e(j) <= cfg.high_e_fail_open)
            {
                const double d = dcriterion_simple(store, i, j);
                if (cfg.shadow_dcriterion && d > cfg.dcriterion_threshold) {
                    ++out.dcriterion_shadow_rejected;
                }
                if (cfg.enable_dcriterion && d > cfg.dcriterion_threshold) {
                    ++out.dcriterion_rejected;
                    continue;
                }
            }

            out.candidates.push_back(BroadPhasePair{
                static_cast<std::uint32_t>(i),
                static_cast<std::uint32_t>(j)
            });
        }
    }

    return out;
}

} // namespace cascade
