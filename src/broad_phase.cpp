// ---------------------------------------------------------------------------
// broad_phase.cpp
// ---------------------------------------------------------------------------
#include "broad_phase.hpp"

#include "state_store.hpp"

#include <cmath>

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

} // namespace

BroadPhaseResult generate_broad_phase_candidates(const StateStore& store,
                                                 const BroadPhaseConfig& cfg)
{
    BroadPhaseResult out;
    out.shell_margin_km = cfg.shell_margin_km;

    if (store.size() == 0 || store.satellite_count() == 0) {
        return out;
    }

    const std::size_t sat_count = store.satellite_count();
    const std::size_t deb_count = store.debris_count();

    // Conservative reserve: satellites against all non-self objects.
    out.candidates.reserve(sat_count * (deb_count + sat_count));

    for (std::size_t i = 0; i < store.size(); ++i) {
        if (store.type(i) != ObjectType::SATELLITE) continue;

        double sat_min = 0.0;
        double sat_max = 0.0;
        shell_bounds(store, i, cfg, sat_min, sat_max);

        for (std::size_t j = 0; j < store.size(); ++j) {
            if (j == i) continue;

            ++out.pairs_considered;

            double obj_min = 0.0;
            double obj_max = 0.0;
            shell_bounds(store, j, cfg, obj_min, obj_max);

            if (!intervals_overlap(sat_min, sat_max, obj_min, obj_max)) {
                continue;
            }

            ++out.shell_overlap_pass;
            out.candidates.push_back(BroadPhasePair{
                static_cast<std::uint32_t>(i),
                static_cast<std::uint32_t>(j)
            });
        }
    }

    return out;
}

} // namespace cascade
