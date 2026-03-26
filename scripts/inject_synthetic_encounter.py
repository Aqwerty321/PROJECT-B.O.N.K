#!/usr/bin/env python3
"""
Inject synthetic close-approach debris to trigger the full CDM → auto-COLA pipeline.

PS-safety: uses only the existing POST /api/telemetry endpoint.
No backend changes, no new API surface. This is a demo/test helper.

How it works:
  1. GET /api/status             → current simulation timestamp
  2. GET /api/visualization/snapshot → satellite positions (lat/lon/alt_km + eci_r/eci_v)
  3. Place debris nearly co-located with tiny position offset (< miss_km)
     and a small relative velocity so the CDM scanner's 24h lookahead
     sees a close approach at or near the desired miss distance.
  4. POST /api/telemetry with the debris objects

Modes:
  --mode single   (default) Target one satellite with --count debris at --miss-km
  --mode stress   Target multiple satellites with varied miss distances / severities
                  to exercise the full CRITICAL/WARNING/WATCH pipeline and populate
                  the dashboard with rich, varied conjunction data.

Usage:
  # Simple: 3 CRITICAL encounters on one sat
  python scripts/inject_synthetic_encounter.py --miss-km 0.05 --count 3

  # Stress: varied encounters across the fleet (all 10 sats, 6 each, staggered TCAs)
  python scripts/inject_synthetic_encounter.py --mode stress --seed 42
"""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from typing import Any

MU_KM3_S2 = 398600.4418
R_EARTH_KM = 6378.137

# ---------- helpers ----------

def die(msg: str) -> None:
    print(f"[error] {msg}", file=sys.stderr)
    raise SystemExit(1)


def api_get(base: str, path: str) -> Any:
    url = f"{base}{path}"
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        body = e.read().decode() if e.fp else ""
        die(f"GET {path} → HTTP {e.code}: {body[:300]}")
    except Exception as e:
        die(f"GET {path} failed: {e}")


def api_post(base: str, path: str, body: dict) -> Any:
    url = f"{base}{path}"
    data = json.dumps(body).encode()
    try:
        req = urllib.request.Request(
            url, data=data, method="POST",
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        resp_body = e.read().decode() if e.fp else ""
        die(f"POST {path} → HTTP {e.code}: {resp_body[:300]}")
    except Exception as e:
        die(f"POST {path} failed: {e}")


def latlon_alt_to_eci(lat_deg: float, lon_deg: float, alt_km: float, timestamp: str) -> list[float]:
    """
    Convert geodetic lat/lon/alt to approximate ECI position.
    Returns [x, y, z] in km.
    Note: spherical approximation (ignores Earth oblateness).
    """
    dt_obj = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
    jd = (dt_obj - datetime(2000, 1, 1, 12, 0, 0, tzinfo=timezone.utc)).total_seconds() / 86400.0 + 2451545.0
    T = (jd - 2451545.0) / 36525.0
    gmst_deg = (280.46061837 + 360.98564736629 * (jd - 2451545.0) +
                0.000387933 * T * T) % 360.0

    lat_rad = math.radians(lat_deg)
    lon_rad = math.radians(lon_deg + gmst_deg)
    r = R_EARTH_KM + alt_km

    x = r * math.cos(lat_rad) * math.cos(lon_rad)
    y = r * math.cos(lat_rad) * math.sin(lon_rad)
    z = r * math.sin(lat_rad)
    return [x, y, z]


def vec_mag(v: list[float]) -> float:
    return math.sqrt(sum(c * c for c in v))


def vec_normalize(v: list[float]) -> list[float]:
    m = vec_mag(v)
    if m < 1e-12:
        return [0.0, 0.0, 0.0]
    return [c / m for c in v]


def vec_cross(a: list[float], b: list[float]) -> list[float]:
    return [
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    ]


def vec_scale(v: list[float], s: float) -> list[float]:
    return [c * s for c in v]


def vec_add(a: list[float], b: list[float]) -> list[float]:
    return [a[i] + b[i] for i in range(3)]


def circular_velocity(r_km: float) -> float:
    """Circular orbital velocity at radius r_km."""
    return math.sqrt(MU_KM3_S2 / r_km)


# ---------- encounter crafting ----------

# CDM severity thresholds (from types.hpp):
#   miss_km <= 0.1  → CRITICAL
#   miss_km <= 1.0  → WARNING
#   else            → WATCH

SEVERITY_PROFILES = {
    "critical": {
        "miss_km_range": (0.02, 0.08),
        "label": "CRITICAL (<100m)",
        "approach_dv_kms": 0.0001,     # 0.1 m/s — slow approach, stays close
    },
    "warning": {
        "miss_km_range": (0.15, 0.80),
        "label": "WARNING (100m-1km)",
        "approach_dv_kms": 0.0003,     # 0.3 m/s — moderate approach
    },
    "watch": {
        "miss_km_range": (1.2, 4.5),
        "label": "WATCH (1-5km)",
        "approach_dv_kms": 0.0008,     # 0.8 m/s — still under 16.7 m/s limit
    },                                  # (must traverse <10km per 600s CDM substep)
}


def craft_colocated_debris_from_eci(
    sat_eci: list[float],
    sat_vel: list[float],
    miss_km: float,
    debris_id: str,
    offset_index: int = 0,
    rel_velocity_scale: float = 1.0,
    tca_offset_s: float = 0.0,
    approach_dv_kms: float = 0.0,
) -> dict:
    """
    Place debris near the satellite using actual ECI r/v.

    Two modes controlled by tca_offset_s:

    tca_offset_s == 0  (original behaviour, TCA ≈ now):
        Debris placed at sat position + small offset.  rel_velocity_scale
        controls how fast it drifts through the screening volume.

    tca_offset_s > 0  (staggered TCA):
        Debris placed *ahead* in the orbit by along_offset km, with a
        slightly lower velocity (approach_dv_kms slower along-track).
        The satellite catches up → TCA occurs ~tca_offset_s seconds later.
        Cross-track/radial offset still determines miss distance.

        Key constraint for CDM detection: the debris must spend at least
        one CDM substep (600 s) inside the 5 km screening sphere.
        → approach_dv_kms must be < 10 km / 600 s ≈ 0.0167 km/s (16.7 m/s).
    """
    r_hat = vec_normalize(sat_eci)
    v_hat = vec_normalize(sat_vel)

    # Cross-track direction: h = r × v (angular momentum direction)
    h_hat = vec_normalize(vec_cross(r_hat, v_hat))
    if vec_mag(h_hat) < 0.5:
        h_hat = vec_normalize(vec_cross(r_hat, [0.0, 0.0, 1.0]))

    # Along-track direction (perpendicular to r in orbital plane)
    along_hat = vec_normalize(vec_cross(h_hat, r_hat))

    # --- Position offset ---
    # Cross-track / radial component → determines miss distance at TCA
    angle = (2.0 * math.pi / 6.0) * offset_index + random.uniform(-0.3, 0.3)
    radial_offset = miss_km * 0.8 * math.cos(angle)
    crosstrack_offset = miss_km * 0.8 * math.sin(angle)

    # Along-track component → determines *when* TCA occurs
    if tca_offset_s > 0 and approach_dv_kms > 0:
        # Place debris ahead: satellite catches up in ~tca_offset_s seconds
        along_offset = approach_dv_kms * tca_offset_s * random.uniform(0.95, 1.05)
    else:
        # Small along-track jitter (original TCA-near-now behaviour)
        along_offset = miss_km * 0.2 * math.sin(angle * 1.7)

    debris_pos = vec_add(
        sat_eci,
        vec_add(
            vec_add(
                vec_scale(r_hat, radial_offset),
                vec_scale(h_hat, crosstrack_offset),
            ),
            vec_scale(along_hat, along_offset),
        ),
    )

    # --- Velocity offset ---
    if tca_offset_s > 0 and approach_dv_kms > 0:
        # Debris is ahead and slightly slower → satellite catches up
        dv_along = -approach_dv_kms * random.uniform(0.95, 1.05)
        # Small perpendicular perturbations for realism
        dv_radial = miss_km * 0.0003 * random.uniform(-1, 1)
        dv_cross = miss_km * 0.0002 * random.uniform(-1, 1)
    else:
        # Original perturbation logic (TCA at / very near current epoch)
        base_dv = max(0.0001, miss_km * 0.001) * rel_velocity_scale
        dv_along = base_dv * (1 + 0.3 * offset_index) * random.uniform(0.8, 1.2)
        dv_radial = base_dv * 0.5 * (offset_index + 1) * random.uniform(0.6, 1.4)
        dv_cross = base_dv * 0.2 * random.uniform(-1, 1)
        # Alternate sign for variety (some approach, some recede)
        if offset_index % 2 == 1:
            dv_along = -dv_along

    debris_vel = vec_add(
        sat_vel,
        vec_add(
            vec_add(
                vec_scale(v_hat, dv_along),
                vec_scale(r_hat, dv_radial),
            ),
            vec_scale(h_hat, dv_cross),
        ),
    )

    return {
        "id": debris_id,
        "type": "DEBRIS",
        "r": {"x": round(debris_pos[0], 6), "y": round(debris_pos[1], 6), "z": round(debris_pos[2], 6)},
        "v": {"x": round(debris_vel[0], 6), "y": round(debris_vel[1], 6), "z": round(debris_vel[2], 6)},
    }


def craft_colocated_debris(
    sat_eci: list[float],
    timestamp: str,
    miss_km: float,
    debris_id: str,
    offset_index: int = 0,
) -> dict:
    """Fallback: place debris using geodetic-derived ECI (lossy)."""
    r_sat = vec_mag(sat_eci)
    r_hat = vec_normalize(sat_eci)

    z_ref = [0.0, 0.0, 1.0]
    v_hat = vec_normalize(vec_cross(z_ref, r_hat))
    if vec_mag(v_hat) < 0.5:
        v_hat = vec_normalize(vec_cross([0.0, 1.0, 0.0], r_hat))
    h_hat = vec_normalize(vec_cross(r_hat, v_hat))

    angle = (math.pi / 3) * offset_index
    radial_offset = miss_km * 0.8 * math.cos(angle)
    crosstrack_offset = miss_km * 0.8 * math.sin(angle)

    debris_pos = vec_add(
        sat_eci,
        vec_add(
            vec_scale(r_hat, radial_offset),
            vec_scale(h_hat, crosstrack_offset),
        ),
    )

    v_circ = circular_velocity(r_sat)
    sat_vel = vec_scale(v_hat, v_circ)

    dv_along = 0.0001 * (1 + 0.5 * offset_index)
    dv_radial = 0.00005 * (offset_index + 1)

    debris_vel = vec_add(
        sat_vel,
        vec_add(
            vec_scale(v_hat, dv_along),
            vec_scale(r_hat, dv_radial),
        ),
    )

    return {
        "id": debris_id,
        "type": "DEBRIS",
        "r": {"x": round(debris_pos[0], 6), "y": round(debris_pos[1], 6), "z": round(debris_pos[2], 6)},
        "v": {"x": round(debris_vel[0], 6), "y": round(debris_vel[1], 6), "z": round(debris_vel[2], 6)},
    }


# ---------- single mode ----------

def run_single(args, sats, timestamp):
    """Original mode: target one satellite with --count debris at --miss-km."""
    target = None
    if args.target:
        target = next((s for s in sats if s["id"] == args.target), None)
        if not target:
            die(f"Target satellite '{args.target}' not found. Available: {[s['id'] for s in sats[:10]]}")
    else:
        target = next((s for s in sats if s.get("status", "").upper() == "NOMINAL"), sats[0])

    target_alt = target.get("alt_km", 0.0)
    print(f"[inject] Target: {target['id']} at lat={target['lat']:.2f} lon={target['lon']:.2f} alt={target_alt:.1f} km")

    eci_r = target.get("eci_r")
    eci_v = target.get("eci_v")
    if eci_r and eci_v:
        sat_eci = [eci_r["x"], eci_r["y"], eci_r["z"]]
        sat_vel = [eci_v["x"], eci_v["y"], eci_v["z"]]
        print(f"[inject] ECI r from snapshot: [{sat_eci[0]:.3f}, {sat_eci[1]:.3f}, {sat_eci[2]:.3f}] km (r={vec_mag(sat_eci):.1f} km)")
        print(f"[inject] ECI v from snapshot: [{sat_vel[0]:.6f}, {sat_vel[1]:.6f}, {sat_vel[2]:.6f}] km/s (v={vec_mag(sat_vel):.3f} km/s)")
    else:
        print(f"[inject] WARNING: eci_r/eci_v not in snapshot, using geodetic conversion")
        sat_eci = latlon_alt_to_eci(target["lat"], target["lon"], target_alt, timestamp)
        r_mag = vec_mag(sat_eci)
        v_circ = circular_velocity(r_mag)
        r_hat = vec_normalize(sat_eci)
        z_ref = [0.0, 0.0, 1.0]
        v_hat = vec_normalize(vec_cross(z_ref, r_hat))
        sat_vel = vec_scale(v_hat, v_circ)

    objects = []
    for i in range(args.count):
        debris_id = f"DEB-SYNTH-{90001 + i}"
        obj = craft_colocated_debris_from_eci(
            sat_eci=sat_eci,
            sat_vel=sat_vel,
            miss_km=args.miss_km,
            debris_id=debris_id,
            offset_index=i,
        )
        objects.append(obj)
        dr = [obj["r"]["x"] - sat_eci[0], obj["r"]["y"] - sat_eci[1], obj["r"]["z"] - sat_eci[2]]
        sep = vec_mag(dr)
        dv = [obj["v"]["x"] - sat_vel[0], obj["v"]["y"] - sat_vel[1], obj["v"]["z"] - sat_vel[2]]
        rel_v = vec_mag(dv)
        print(f"[inject] Debris {debris_id}: separation={sep:.4f} km ({sep*1000:.1f} m), rel_v={rel_v*1000:.1f} m/s")

    return objects, target


# ---------- stress mode ----------

def run_stress(args, sats, timestamp):
    """
    Target multiple satellites with varied encounter severities and staggered TCAs.
    Creates a rich, realistic conjunction environment for dashboard testing.

    Severity schedule:  cycles CRITICAL → WARNING → WATCH (guarantees all three
    when per_target >= 3).

    TCA stagger:  encounters are spread from +0.5 h to +22 h across the 24 h
    CDM window so the bullseye chart shows dots at varied radial distances.
    """
    num_targets = min(args.targets, len(sats))
    per_target = args.per_target

    # Pick target satellites (prefer NOMINAL status)
    nominal_sats = [s for s in sats if s.get("status", "").upper() == "NOMINAL"]
    if len(nominal_sats) < num_targets:
        nominal_sats = sats
    targets = nominal_sats[:num_targets]

    print(f"[stress] Targeting {len(targets)} satellites with {per_target} encounters each")
    print(f"[stress] Severity cycle: CRITICAL → WARNING → WATCH")
    print(f"[stress] TCA spread: +0.5 h to +22.0 h across 24 h CDM window")

    # Severity schedule: cycle [critical, warning, watch, critical, ...]
    # Guarantees all three when per_target >= 3, biases toward CRITICAL.
    def severity_for(idx: int) -> str:
        return ["critical", "warning", "watch"][idx % 3]

    # TCA stagger: linearly space encounters from 0.5 h to 22 h
    def tca_offset_hours(idx: int) -> float:
        if per_target <= 1:
            return 0.5
        return 0.5 + (22.0 - 0.5) * idx / (per_target - 1)

    objects: list[dict] = []
    debris_counter = 90001
    total_by_severity = {"critical": 0, "warning": 0, "watch": 0}

    for sat_idx, target in enumerate(targets):
        target_alt = target.get("alt_km", 0.0)
        eci_r = target.get("eci_r")
        eci_v = target.get("eci_v")

        if eci_r and eci_v:
            sat_eci = [eci_r["x"], eci_r["y"], eci_r["z"]]
            sat_vel = [eci_v["x"], eci_v["y"], eci_v["z"]]
        else:
            print(f"[stress]   {target['id']}: no ECI state, using geodetic (lossy)")
            sat_eci = latlon_alt_to_eci(target["lat"], target["lon"], target_alt, timestamp)
            r_mag = vec_mag(sat_eci)
            v_circ = circular_velocity(r_mag)
            r_hat = vec_normalize(sat_eci)
            z_ref = [0.0, 0.0, 1.0]
            v_hat = vec_normalize(vec_cross(z_ref, r_hat))
            sat_vel = vec_scale(v_hat, v_circ)

        print(f"\n[stress] --- {target['id']} (alt={target_alt:.1f} km) ---")

        for enc_idx in range(per_target):
            sev_key = severity_for(enc_idx)
            profile = SEVERITY_PROFILES[sev_key]
            miss_lo, miss_hi = profile["miss_km_range"]
            miss_km = random.uniform(miss_lo, miss_hi)

            # TCA stagger for this encounter
            tca_h = tca_offset_hours(enc_idx)
            tca_s = tca_h * 3600.0
            approach_dv = profile["approach_dv_kms"]

            debris_id = f"DEB-SYNTH-{debris_counter}"
            debris_counter += 1

            obj = craft_colocated_debris_from_eci(
                sat_eci=sat_eci,
                sat_vel=sat_vel,
                miss_km=miss_km,
                debris_id=debris_id,
                offset_index=enc_idx + sat_idx * per_target,
                tca_offset_s=tca_s,
                approach_dv_kms=approach_dv,
            )
            objects.append(obj)
            total_by_severity[sev_key] += 1

            dr = [obj["r"]["x"] - sat_eci[0], obj["r"]["y"] - sat_eci[1], obj["r"]["z"] - sat_eci[2]]
            sep = vec_mag(dr)
            dv = [obj["v"]["x"] - sat_vel[0], obj["v"]["y"] - sat_vel[1], obj["v"]["z"] - sat_vel[2]]
            rel_v = vec_mag(dv)
            print(f"[stress]   {debris_id}: {profile['label']}  miss={miss_km:.3f} km  "
                  f"sep={sep*1000:.1f} m  rel_v={rel_v*1000:.1f} m/s  tca=+{tca_h:.1f}h")

    print(f"\n[stress] Totals: {total_by_severity['critical']} CRITICAL, "
          f"{total_by_severity['warning']} WARNING, {total_by_severity['watch']} WATCH")
    print(f"[stress] {len(objects)} debris objects across {len(targets)} satellites")
    print(f"[stress] TCA range: +0.5h to +{tca_offset_hours(per_target - 1):.1f}h "
          f"(varied radial distance on bullseye)")

    return objects, None


# ---------- main ----------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Inject synthetic close-approach debris for demo pipeline testing",
    )
    parser.add_argument("--api-base", default="http://localhost:8000", help="Backend base URL")
    parser.add_argument("--mode", choices=["single", "stress"], default="single",
                        help="Injection mode: single (one sat) or stress (multi-sat, varied severity)")
    # Single mode args
    parser.add_argument("--target", default="", help="Target satellite ID (auto-picks first if empty)")
    parser.add_argument("--miss-km", type=float, default=0.05,
                        help="Desired miss distance in km (default 0.05 = 50m -> CRITICAL)")
    parser.add_argument("--count", type=int, default=2, help="Number of debris to inject per target (1-10)")
    # Stress mode args
    parser.add_argument("--targets", type=int, default=10,
                        help="Number of satellites to target (stress mode, default 10 = all)")
    parser.add_argument("--per-target", type=int, default=6,
                        help="Encounters per satellite (stress mode, default 6)")
    parser.add_argument("--seed", type=int, default=None,
                        help="Random seed for reproducible stress runs")
    # Common
    parser.add_argument("--dry-run", action="store_true", help="Print payload without sending")
    args = parser.parse_args()

    args.count = max(1, min(10, args.count))
    args.targets = max(1, min(10, args.targets))
    args.per_target = max(1, min(12, args.per_target))

    if args.seed is not None:
        random.seed(args.seed)

    print(f"[inject] Connecting to {args.api_base} ...")
    print(f"[inject] Mode: {args.mode}")

    # 1. Get current simulation state
    status = api_get(args.api_base, "/api/status?details=1")
    print(f"[inject] Engine status: {status.get('status', '?')}, tick={status.get('tick_count', '?')}")

    # 2. Get current snapshot for satellite positions
    snapshot = api_get(args.api_base, "/api/visualization/snapshot")
    timestamp = snapshot.get("timestamp", "")
    sats = snapshot.get("satellites", [])
    if not sats:
        die("No satellites in snapshot -- is the engine running with a loaded catalog?")

    print(f"[inject] Snapshot timestamp: {timestamp}")
    print(f"[inject] Found {len(sats)} satellites")

    # 3. Generate debris based on mode
    if args.mode == "stress":
        objects, _ = run_stress(args, sats, timestamp)
    else:
        objects, target = run_single(args, sats, timestamp)

    # 4. Build telemetry payload
    payload = {
        "timestamp": timestamp,
        "objects": objects,
    }

    if args.dry_run:
        print("\n[dry-run] Telemetry payload:")
        print(json.dumps(payload, indent=2))
        return

    # 5. Inject via POST /api/telemetry
    print(f"\n[inject] Sending {len(objects)} debris via POST /api/telemetry ...")
    result = api_post(args.api_base, "/api/telemetry", payload)
    print(f"[inject] Response: {json.dumps(result)}")

    processed = result.get("processed_count", 0)
    warnings = result.get("active_cdm_warnings", 0)
    print(f"[inject] Processed: {processed}, Active CDM warnings: {warnings}")

    if processed > 0:
        print(f"\n[inject] SUCCESS -- {processed} synthetic debris injected.")
        if args.mode == "stress":
            print(f"[inject] Run a sim step to trigger CDM scanner:")
            print(f"         curl -X POST {args.api_base}/api/simulate/step \\")
            print(f"              -H 'Content-Type: application/json' -d '{{\"step_seconds\":60}}'")
            print(f"[inject] Then check conjunctions:")
            print(f"         curl '{args.api_base}/api/debug/conjunctions?source=combined'")
        else:
            target_id = target["id"] if target else "?"
            target_alt = target.get("alt_km", 0) if target else 0
            print(f"[inject] Debris placed {args.miss_km*1000:.0f}m from {target_id} at alt {target_alt:.1f}km")
            print(f"[inject] Run a sim step: POST /api/simulate/step  then check:")
            print(f"         GET /api/debug/conjunctions?source=combined")
    else:
        print(f"\n[inject] WARNING -- no objects were processed. Check engine logs.")


if __name__ == "__main__":
    main()
