#!/usr/bin/env python3
"""
Inject synthetic close-approach debris to trigger the full CDM → auto-COLA pipeline.

PS-safety: uses only the existing POST /api/telemetry endpoint.
No backend changes, no new API surface. This is a demo/test helper.

How it works:
  1. GET /api/status             → current simulation timestamp
  2. GET /api/visualization/snapshot → satellite positions (lat/lon/alt_km)
  3. Convert target satellite geodetic position to approximate ECI
  4. Place debris nearly co-located with tiny position offset (< miss_km)
     and a small relative velocity so the CDM scanner's 24h lookahead
     sees a close approach at or near the desired miss distance.
  5. POST /api/telemetry with the debris objects

The debris are placed almost on top of the satellite with a slight velocity
perturbation. This guarantees the predictive CDM scanner (SCREENING_THRESHOLD_KM=5.0km)
detects them immediately. If miss < COLLISION_THRESHOLD_KM=0.1km → CRITICAL → auto-COLA.

Usage:
  python scripts/inject_synthetic_encounter.py [--api-base http://localhost:8000]
                                                [--target SAT-id]
                                                [--miss-km 0.05]
                                                [--count 2]
"""

from __future__ import annotations

import argparse
import json
import math
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
    # Greenwich Mean Sidereal Time (rough)
    jd = (dt_obj - datetime(2000, 1, 1, 12, 0, 0, tzinfo=timezone.utc)).total_seconds() / 86400.0 + 2451545.0
    T = (jd - 2451545.0) / 36525.0
    gmst_deg = (280.46061837 + 360.98564736629 * (jd - 2451545.0) +
                0.000387933 * T * T) % 360.0

    lat_rad = math.radians(lat_deg)
    lon_rad = math.radians(lon_deg + gmst_deg)  # convert to ECI longitude
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

def craft_colocated_debris_from_eci(
    sat_eci: list[float],
    sat_vel: list[float],
    miss_km: float,
    debris_id: str,
    offset_index: int = 0,
) -> dict:
    """
    Place debris nearly co-located with the satellite using actual ECI r/v.

    Strategy:
    - Position the debris at the satellite's ECI position + a small offset
      perpendicular to the orbit (radial + cross-track) of magnitude ~miss_km.
    - Use the satellite's actual velocity with only ~0.1 m/s relative
      perturbation so the pair stays within SCREENING_THRESHOLD_KM (5km)
      throughout most of the 24h CDM lookahead window.
    """
    r_hat = vec_normalize(sat_eci)
    v_hat = vec_normalize(sat_vel)

    # Cross-track direction: h = r × v (angular momentum direction)
    h_hat = vec_normalize(vec_cross(r_hat, v_hat))
    # If h_hat is degenerate, use a fallback
    if vec_mag(h_hat) < 0.5:
        h_hat = vec_normalize(vec_cross(r_hat, [0.0, 0.0, 1.0]))

    # Position offset: mostly radial + some cross-track, magnitude ~ miss_km
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

    # Velocity: use satellite's actual velocity with tiny perturbation
    # ~0.1 m/s along-track → 8.6 km drift over 24h (within screening threshold)
    dv_along = 0.0001 * (1 + 0.5 * offset_index)   # km/s (~0.1-0.2 m/s)
    dv_radial = 0.00005 * (offset_index + 1)         # km/s (~0.05-0.1 m/s)

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


def craft_colocated_debris(
    sat_eci: list[float],
    timestamp: str,
    miss_km: float,
    debris_id: str,
    offset_index: int = 0,
) -> dict:
    """
    Place debris nearly co-located with the satellite.

    Strategy:
    - Position the debris at the satellite's ECI position + a small offset
      perpendicular to the orbit (radial + cross-track) of magnitude ~miss_km.
    - Give it nearly identical circular velocity with only ~0.1 m/s relative
      velocity so the pair stays within SCREENING_THRESHOLD_KM (5km) throughout
      most of the 24h CDM lookahead window (~8.6 km max drift over 24h).

    This is much simpler than trying to engineer a future encounter and is
    guaranteed to produce a CDM because the initial separation itself is
    within SCREENING_THRESHOLD_KM (5km).
    """
    r_sat = vec_mag(sat_eci)
    r_hat = vec_normalize(sat_eci)

    # Construct local orbital frame: r_hat, along-track (v_hat), cross-track (h_hat)
    z_ref = [0.0, 0.0, 1.0]
    v_hat = vec_normalize(vec_cross(z_ref, r_hat))
    if vec_mag(v_hat) < 0.5:
        v_hat = vec_normalize(vec_cross([0.0, 1.0, 0.0], r_hat))
    h_hat = vec_normalize(vec_cross(r_hat, v_hat))

    # Position offset: mostly radial + some cross-track, magnitude ~ miss_km * (1 + small factor)
    # Use offset_index to spread debris in different directions
    angle = (math.pi / 3) * offset_index  # rotate the offset direction for each debris
    radial_offset = miss_km * 0.8 * math.cos(angle)
    crosstrack_offset = miss_km * 0.8 * math.sin(angle)

    debris_pos = vec_add(
        sat_eci,
        vec_add(
            vec_scale(r_hat, radial_offset),
            vec_scale(h_hat, crosstrack_offset),
        ),
    )

    # Velocity: same circular velocity as satellite (tangent), with a VERY tiny
    # relative velocity perturbation. The key insight: the CDM scanner uses a
    # 24h lookahead with 600s substeps. A relative velocity of 15 m/s would
    # drift ~1300 km in 24h — far beyond SCREENING_THRESHOLD_KM (5km).
    # Instead, use ~0.1 m/s (0.0001 km/s) so 24h drift is only ~8.6 km.
    # The initial separation of ~miss_km (e.g. 40m) is already well within
    # screening range, so the CDM scanner will see it on every substep.
    v_circ = circular_velocity(r_sat)
    sat_vel = vec_scale(v_hat, v_circ)

    # Tiny along-track perturbation: ~0.1 m/s → 8.6 km drift over 24h
    # This keeps the pair within SCREENING_THRESHOLD_KM for most of the window
    # while still providing a non-zero relative velocity (realistic).
    dv_along = 0.0001 * (1 + 0.5 * offset_index)  # km/s (~0.1-0.2 m/s relative)
    dv_radial = 0.00005 * (offset_index + 1)        # km/s (~0.05-0.1 m/s)

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


# ---------- main ----------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Inject synthetic close-approach debris for demo pipeline testing",
    )
    parser.add_argument("--api-base", default="http://localhost:8000", help="Backend base URL")
    parser.add_argument("--target", default="", help="Target satellite ID (auto-picks first if empty)")
    parser.add_argument("--miss-km", type=float, default=0.05,
                        help="Desired miss distance in km (default 0.05 = 50m → CRITICAL)")
    parser.add_argument("--count", type=int, default=2, help="Number of debris to inject (1-5)")
    parser.add_argument("--dry-run", action="store_true", help="Print payload without sending")
    args = parser.parse_args()

    args.count = max(1, min(5, args.count))

    print(f"[inject] Connecting to {args.api_base} ...")

    # 1. Get current simulation state
    status = api_get(args.api_base, "/api/status?details=1")
    print(f"[inject] Engine status: {status.get('status', '?')}, tick={status.get('tick_count', '?')}")

    # 2. Get current snapshot for satellite positions (includes eci_r, eci_v)
    snapshot = api_get(args.api_base, "/api/visualization/snapshot")
    timestamp = snapshot.get("timestamp", "")
    sats = snapshot.get("satellites", [])
    if not sats:
        die("No satellites in snapshot — is the engine running with a loaded catalog?")

    print(f"[inject] Snapshot timestamp: {timestamp}")
    print(f"[inject] Found {len(sats)} satellites")

    # 3. Pick target satellite
    target = None
    if args.target:
        target = next((s for s in sats if s["id"] == args.target), None)
        if not target:
            die(f"Target satellite '{args.target}' not found. Available: {[s['id'] for s in sats[:10]]}")
    else:
        # Pick the first nominal satellite
        target = next((s for s in sats if s.get("status", "").upper() == "NOMINAL"), sats[0])

    target_alt = target.get("alt_km", 0.0)
    print(f"[inject] Target: {target['id']} at lat={target['lat']:.2f} lon={target['lon']:.2f} alt={target_alt:.1f} km")

    # 4. Get ECI state directly from snapshot (critical: avoids lossy geodetic→ECI conversion)
    eci_r = target.get("eci_r")
    eci_v = target.get("eci_v")
    if eci_r and eci_v:
        sat_eci = [eci_r["x"], eci_r["y"], eci_r["z"]]
        sat_vel = [eci_v["x"], eci_v["y"], eci_v["z"]]
        print(f"[inject] ECI r from snapshot: [{sat_eci[0]:.3f}, {sat_eci[1]:.3f}, {sat_eci[2]:.3f}] km (r={vec_mag(sat_eci):.1f} km)")
        print(f"[inject] ECI v from snapshot: [{sat_vel[0]:.6f}, {sat_vel[1]:.6f}, {sat_vel[2]:.6f}] km/s (v={vec_mag(sat_vel):.3f} km/s)")
    else:
        # Fallback: convert geodetic to ECI (lossy, may introduce ~100s of km error)
        print(f"[inject] WARNING: eci_r/eci_v not in snapshot (backend needs rebuild?), using geodetic conversion")
        sat_eci = latlon_alt_to_eci(target["lat"], target["lon"], target_alt, timestamp)
        r_mag = vec_mag(sat_eci)
        v_circ = circular_velocity(r_mag)
        # Approximate velocity direction
        r_hat = vec_normalize(sat_eci)
        z_ref = [0.0, 0.0, 1.0]
        v_hat = vec_normalize(vec_cross(z_ref, r_hat))
        sat_vel = vec_scale(v_hat, v_circ)
        print(f"[inject] ECI position (approx): [{sat_eci[0]:.1f}, {sat_eci[1]:.1f}, {sat_eci[2]:.1f}] km (r={r_mag:.1f} km)")

    # 5. Generate co-located debris objects using actual ECI state
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

    # 6. Build telemetry payload
    payload = {
        "timestamp": timestamp,
        "objects": objects,
    }

    if args.dry_run:
        print("\n[dry-run] Telemetry payload:")
        print(json.dumps(payload, indent=2))
        return

    # 7. Inject via POST /api/telemetry
    print(f"\n[inject] Sending {len(objects)} debris via POST /api/telemetry ...")
    result = api_post(args.api_base, "/api/telemetry", payload)
    print(f"[inject] Response: {json.dumps(result)}")

    processed = result.get("processed_count", 0)
    warnings = result.get("active_cdm_warnings", 0)
    print(f"[inject] Processed: {processed}, Active CDM warnings: {warnings}")

    if processed > 0:
        print(f"\n[inject] SUCCESS — {processed} synthetic debris injected.")
        print(f"[inject] Debris placed {args.miss_km*1000:.0f}m from {target['id']} at alt {target_alt:.1f}km")
        print(f"[inject] The predictive CDM scanner should detect encounters within")
        print(f"         the next simulation tick (24h lookahead window).")
        print(f"[inject] Run a sim step: POST /api/simulate/step  then check:")
        print(f"         GET /api/debug/conjunctions?source=combined")
    else:
        print(f"\n[inject] WARNING — no objects were processed. Check engine logs.")


if __name__ == "__main__":
    main()
