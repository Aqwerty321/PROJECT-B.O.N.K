// ---------------------------------------------------------------------------
// phase2_regression.cpp
//
// Regression harness comparing:
//   - Fast propagator only: Kepler + J2 secular
//   - Adaptive runtime mode: (fast OR RK4 fallback)
//   - Reference: RK4 + J2
//
// This is a non-grading utility to quantify broad-phase propagation error.
// ---------------------------------------------------------------------------

#include "types.hpp"
#include "orbit_math.hpp"
#include "propagator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace {

using cascade::Vec3;

double norm(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 diff(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

struct MetricSummary {
    std::vector<double> values;

    void add(double v) { values.push_back(v); }

    double mean() const {
        if (values.empty()) return 0.0;
        double s = 0.0;
        for (double v : values) s += v;
        return s / static_cast<double>(values.size());
    }

    double max() const {
        if (values.empty()) return 0.0;
        return *std::max_element(values.begin(), values.end());
    }

    double p95() {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        const std::size_t idx = static_cast<std::size_t>(0.95 * static_cast<double>(values.size() - 1));
        return values[idx];
    }
};

double rad(double deg) {
    return deg * cascade::PI / 180.0;
}

} // namespace

int main(int argc, char** argv)
{
    int samples = 2000;
    double dt_min_s = 300.0;
    double dt_max_s = 86400.0;

    if (argc >= 2) {
        samples = std::max(100, std::atoi(argv[1]));
    }
    if (argc >= 3) {
        dt_min_s = std::atof(argv[2]);
    }
    if (argc >= 4) {
        dt_max_s = std::atof(argv[3]);
    }
    if (!(dt_min_s > 0.0) || !(dt_max_s > 0.0) || dt_max_s < dt_min_s) {
        std::cerr << "Invalid dt range. Usage: phase2_regression [samples] [dt_min_s] [dt_max_s]\n";
        return 1;
    }

    std::mt19937_64 rng(20260317ULL);

    // LEO-heavy distribution for current CASCADE target domain.
    std::uniform_real_distribution<double> a_dist(6678.0, 7800.0);      // km
    std::uniform_real_distribution<double> e_dist(0.0, 0.12);           // mostly near-circular
    std::uniform_real_distribution<double> i_dist(rad(0.0), rad(110.0));
    std::uniform_real_distribution<double> ang_dist(0.0, cascade::TWO_PI);
    std::uniform_real_distribution<double> dt_dist(dt_min_s, dt_max_s);

    MetricSummary pos_fast_km;
    MetricSummary vel_fast_ms;
    MetricSummary pos_adapt_km;
    MetricSummary vel_adapt_ms;

    int attempted = 0;
    int valid = 0;
    int skipped = 0;
    int fast_viol_pos = 0;
    int fast_viol_vel = 0;
    int adapt_viol_pos = 0;
    int adapt_viol_vel = 0;
    int adaptive_used_rk4 = 0;
    int adaptive_used_fast = 0;
    int adaptive_escalated_after_probe = 0;

    for (int s = 0; s < samples; ++s) {
        ++attempted;

        cascade::OrbitalElements el{};
        el.a_km = a_dist(rng);
        el.e = e_dist(rng);
        el.i_rad = i_dist(rng);
        el.raan_rad = ang_dist(rng);
        el.argp_rad = ang_dist(rng);
        el.M_rad = ang_dist(rng);
        el.n_rad_s = std::sqrt(cascade::MU_KM3_S2 / (el.a_km * el.a_km * el.a_km));
        el.p_km = el.a_km * (1.0 - el.e * el.e);
        el.rp_km = el.a_km * (1.0 - el.e);
        el.ra_km = el.a_km * (1.0 + el.e);

        Vec3 r0{}, v0{};
        if (!cascade::elements_to_eci(el, r0, v0)) {
            ++skipped;
            continue;
        }

        const double dt = dt_dist(rng);

        Vec3 r_ref = r0;
        Vec3 v_ref = v0;
        if (!cascade::propagate_rk4_j2(r_ref, v_ref, dt)) {
            ++skipped;
            continue;
        }

        Vec3 r_fast = r0;
        Vec3 v_fast = v0;
        cascade::OrbitalElements el_fast = el;
        if (!cascade::propagate_fast_j2_kepler(r_fast, v_fast, el_fast, dt)) {
            ++skipped;
            continue;
        }

        Vec3 r_adapt = r0;
        Vec3 v_adapt = v0;
        cascade::OrbitalElements el_adapt = el;
        const cascade::AdaptivePropagationResult adapt = cascade::propagate_adaptive(r_adapt, v_adapt, el_adapt, dt);
        if (!adapt.ok) {
            ++skipped;
            continue;
        }
        if (adapt.used_rk4) ++adaptive_used_rk4;
        else ++adaptive_used_fast;
        if (adapt.escalated_after_probe) ++adaptive_escalated_after_probe;

        ++valid;

        const double pe_fast_km = norm(diff(r_fast, r_ref));
        const double ve_fast_ms = norm(diff(v_fast, v_ref)) * 1000.0;
        const double pe_adapt_km = norm(diff(r_adapt, r_ref));
        const double ve_adapt_ms = norm(diff(v_adapt, v_ref)) * 1000.0;

        pos_fast_km.add(pe_fast_km);
        vel_fast_ms.add(ve_fast_ms);
        pos_adapt_km.add(pe_adapt_km);
        vel_adapt_ms.add(ve_adapt_ms);

        if (pe_fast_km > 1.0) ++fast_viol_pos;
        if (ve_fast_ms > 1.0) ++fast_viol_vel;
        if (pe_adapt_km > 1.0) ++adapt_viol_pos;
        if (ve_adapt_ms > 1.0) ++adapt_viol_vel;
    }

    const double pos_fast_p95 = pos_fast_km.p95();
    const double vel_fast_p95 = vel_fast_ms.p95();
    const double pos_adapt_p95 = pos_adapt_km.p95();
    const double vel_adapt_p95 = vel_adapt_ms.p95();

    const bool pass_fast_pos = (pos_fast_p95 <= 1.0) && (pos_fast_km.max() <= 1.0);
    const bool pass_fast_vel = (vel_fast_p95 <= 1.0) && (vel_fast_ms.max() <= 1.0);
    const bool pass_adapt_pos = (pos_adapt_p95 <= 1.0) && (pos_adapt_km.max() <= 1.0);
    const bool pass_adapt_vel = (vel_adapt_p95 <= 1.0) && (vel_adapt_ms.max() <= 1.0);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "CASCADE Phase2 Regression\n";
    std::cout << "samples_attempted=" << attempted << "\n";
    std::cout << "samples_valid=" << valid << "\n";
    std::cout << "samples_skipped=" << skipped << "\n";
    std::cout << "dt_min_s=" << dt_min_s << "\n";
    std::cout << "dt_max_s=" << dt_max_s << "\n";

    std::cout << "adaptive_used_fast=" << adaptive_used_fast << "\n";
    std::cout << "adaptive_used_rk4=" << adaptive_used_rk4 << "\n";
    std::cout << "adaptive_escalated_after_probe=" << adaptive_escalated_after_probe << "\n";

    std::cout << "fast_pos_err_km_mean=" << pos_fast_km.mean() << "\n";
    std::cout << "fast_pos_err_km_p95=" << pos_fast_p95 << "\n";
    std::cout << "fast_pos_err_km_max=" << pos_fast_km.max() << "\n";
    std::cout << "fast_pos_err_km_violations_gt_1=" << fast_viol_pos << "\n";

    std::cout << "fast_vel_err_ms_mean=" << vel_fast_ms.mean() << "\n";
    std::cout << "fast_vel_err_ms_p95=" << vel_fast_p95 << "\n";
    std::cout << "fast_vel_err_ms_max=" << vel_fast_ms.max() << "\n";
    std::cout << "fast_vel_err_ms_violations_gt_1=" << fast_viol_vel << "\n";

    std::cout << "adaptive_pos_err_km_mean=" << pos_adapt_km.mean() << "\n";
    std::cout << "adaptive_pos_err_km_p95=" << pos_adapt_p95 << "\n";
    std::cout << "adaptive_pos_err_km_max=" << pos_adapt_km.max() << "\n";
    std::cout << "adaptive_pos_err_km_violations_gt_1=" << adapt_viol_pos << "\n";

    std::cout << "adaptive_vel_err_ms_mean=" << vel_adapt_ms.mean() << "\n";
    std::cout << "adaptive_vel_err_ms_p95=" << vel_adapt_p95 << "\n";
    std::cout << "adaptive_vel_err_ms_max=" << vel_adapt_ms.max() << "\n";
    std::cout << "adaptive_vel_err_ms_violations_gt_1=" << adapt_viol_vel << "\n";

    std::cout << "fast_target_pos_le_1km=" << (pass_fast_pos ? "PASS" : "FAIL") << "\n";
    std::cout << "fast_target_vel_le_1ms=" << (pass_fast_vel ? "PASS" : "FAIL") << "\n";
    std::cout << "adaptive_target_pos_le_1km=" << (pass_adapt_pos ? "PASS" : "FAIL") << "\n";
    std::cout << "adaptive_target_vel_le_1ms=" << (pass_adapt_vel ? "PASS" : "FAIL") << "\n";

    // Keep harness non-failing so it can be used as an observational tool.
    return 0;
}
