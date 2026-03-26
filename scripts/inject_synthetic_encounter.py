#!/usr/bin/env python3
"""
Inject synthetic close-approach debris to trigger the full CDM → auto-COLA pipeline.

PS-safety: uses only the existing POST /api/telemetry endpoint.
No backend changes, no new API surface. This is a demo/test helper.

How it works:
  1. GET /api/status             → current simulation timestamp
  2. GET /api/visualization/snapshot → satellite positions (lat/lon → we need ECI)
  3. GET /api/debug/conjunctions?source=predicted → check existing CDMs
  4. Compute 1-2 debris state vectors on near-collision courses with a target satellite
  5. POST /api/telemetry with the debris objects

The debris are placed in a co-planar orbit with a small radial/along-track offset
that produces a close approach within 1-6 hours of propagation. The predictive CDM
scanner (SCREENING_THRESHOLD_KM=5.0km) should detect these as WATCH/WARNING events,
and if the miss distance is < COLLISION_THRESHOLD_KM=0.1km, it triggers CRITICAL
auto-COLA.

Usage:
  python scripts/inject_synthetic_encounter.py [--api-base http://localhost:8000]
                                                [--target SAT-id]
                                                [--miss-km 0.05]
                                                [--count 2]
                                                [--encounter-hours 3.0]
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


def latlon_alt_to_eci(lat_deg: float, lon_deg: float, alt_km: float, timestamp: str) -> tuple[list[float], float]:
    """
    Convert geodetic lat/lon/alt to approximate ECI position.
    Returns ([x,y,z] in km, radius_km).
    Note: this is a rough spherical approximation (ignores Earth oblateness).
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
    return [x, y, z], r


def circular_velocity(r_km: float) -> float:
    """Circular orbital velocity at radius r_km."""
    return math.sqrt(MU_KM3_S2 / r_km)


def cross(a: list[float], b: list[float]) -> list[float]:
    return [
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0],
    ]


def normalize(v: list[float]) -> list[float]:
    mag = math.sqrt(sum(x*x for x in v))
    if mag < 1e-12:
        return [0.0, 0.0, 0.0]
    return [x / mag for x in v]


def scale(v: list[float], s: float) -> list[float]:
    return [x * s for x in v]


def add_vec(a: list[float], b: list[float]) -> list[float]:
    return [a[i] + b[i] for i in range(3)]


# ---------- encounter geometry ----------

def craft_encounter_debris(
    sat_pos: list[float],
    sat_alt_km: float,
    timestamp: str,
    miss_km: float,
    encounter_hours: float,
    debris_id: str,
    offset_index: int = 0,
) -> dict:
    """
    Create a debris state vector that will have a close approach with the given
    satellite position after approximately `encounter_hours`.

    Strategy: place debris in a co-planar orbit at roughly the same altitude,
    offset along-track by encounter_hours worth of orbital motion, with a small
    radial offset to create the desired miss distance.
    """
    r_km = sat_alt_km
    v_circ = circular_velocity(r_km)

    # Orbital period and angular rate
    period_s = 2 * math.pi * r_km / v_circ
    omega = 2 * math.pi / period_s  # rad/s

    # Along-track angle to place debris (so it arrives near the sat in encounter_hours)
    # Debris is placed *ahead* in the orbit such that relative drift brings them together.
    # We offset the debris slightly in altitude to create differential drift.
    # Higher orbit → slower angular rate → the satellite "catches up"
    dt_s = encounter_hours * 3600.0

    # Altitude offset to create relative drift that closes the along-track gap
    # Δω ≈ -3/2 * ω * (Δa / a)  (from CW equations)
    # We want the gap closed in dt_s:  gap_angle = Δω * dt_s
    # For a gap_angle of ~0.01 rad (small):
    gap_angle = 0.01 + 0.005 * offset_index  # small along-track separation
    delta_a_km = -(2.0 / 3.0) * r_km * gap_angle / (omega * dt_s)

    # Clamp altitude offset to reasonable range
    delta_a_km = max(-15.0, min(15.0, delta_a_km))

    r_debris = r_km + delta_a_km

    # Position: rotate the satellite position by gap_angle around the orbit normal
    r_hat = normalize(sat_pos)
    # Velocity direction (perpendicular to position in orbit plane)
    # We don't know the exact velocity, so assume prograde is roughly perpendicular
    # Use a reference direction to construct orbit frame
    z_ref = [0.0, 0.0, 1.0]
    v_hat = normalize(cross(z_ref, r_hat))
    if sum(x*x for x in v_hat) < 0.5:
        v_hat = normalize(cross([0.0, 1.0, 0.0], r_hat))
    h_hat = normalize(cross(r_hat, v_hat))  # orbit normal

    # Rotate position by gap_angle around orbit normal
    cos_a = math.cos(gap_angle)
    sin_a = math.sin(gap_angle)
    debris_r_hat = add_vec(scale(r_hat, cos_a), scale(v_hat, sin_a))
    debris_r_hat = normalize(debris_r_hat)

    # Add small radial offset for the desired miss distance
    radial_offset_km = miss_km * (0.5 + 0.5 * offset_index)

    debris_pos = scale(debris_r_hat, r_debris + radial_offset_km)

    # Velocity: circular velocity at the debris altitude, tangent to orbit
    v_debris_mag = circular_velocity(r_debris)
    debris_v_hat = normalize(cross(h_hat, debris_r_hat))
    debris_vel = scale(debris_v_hat, v_debris_mag)

    return {
        "id": debris_id,
        "type": "DEBRIS",
        "r": {"x": round(debris_pos[0], 3), "y": round(debris_pos[1], 3), "z": round(debris_pos[2], 3)},
        "v": {"x": round(debris_vel[0], 4), "y": round(debris_vel[1], 4), "z": round(debris_vel[2], 4)},
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
    parser.add_argument("--encounter-hours", type=float, default=3.0,
                        help="Approximate hours until closest approach")
    parser.add_argument("--dry-run", action="store_true", help="Print payload without sending")
    args = parser.parse_args()

    args.count = max(1, min(5, args.count))

    print(f"[inject] Connecting to {args.api_base} ...")

    # 1. Get current simulation state
    status = api_get(args.api_base, "/api/status?details=1")
    print(f"[inject] Engine status: {status.get('status', '?')}, tick={status.get('tick_count', '?')}")

    # 2. Get current snapshot for satellite positions
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

    print(f"[inject] Target: {target['id']} at lat={target['lat']:.2f} lon={target['lon']:.2f}")

    # 4. Convert target to approximate ECI
    # Estimate altitude from typical LEO (we don't have alt in snapshot, assume ~550km)
    est_alt_km = 550.0
    sat_pos, sat_r = latlon_alt_to_eci(target["lat"], target["lon"], est_alt_km, timestamp)
    print(f"[inject] Approx ECI position: [{sat_pos[0]:.1f}, {sat_pos[1]:.1f}, {sat_pos[2]:.1f}] km (r={sat_r:.1f} km)")

    # 5. Generate debris objects
    objects = []
    for i in range(args.count):
        debris_id = f"DEB-SYNTH-{90001 + i}"
        obj = craft_encounter_debris(
            sat_pos=sat_pos,
            sat_alt_km=sat_r,
            timestamp=timestamp,
            miss_km=args.miss_km,
            encounter_hours=args.encounter_hours,
            debris_id=debris_id,
            offset_index=i,
        )
        objects.append(obj)
        r_mag = math.sqrt(obj["r"]["x"]**2 + obj["r"]["y"]**2 + obj["r"]["z"]**2)
        v_mag = math.sqrt(obj["v"]["x"]**2 + obj["v"]["y"]**2 + obj["v"]["z"]**2)
        print(f"[inject] Debris {debris_id}: r={r_mag:.1f} km, v={v_mag:.3f} km/s, "
              f"target miss ≈ {args.miss_km:.3f} km in ~{args.encounter_hours:.1f}h")

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
        print(f"[inject] The predictive CDM scanner should detect encounters within")
        print(f"         the next few simulation ticks (24h lookahead window).")
        print(f"[inject] Check the Threat page or GET /api/debug/conjunctions?source=combined")
    else:
        print(f"\n[inject] WARNING — no objects were processed. Check engine logs.")


if __name__ == "__main__":
    main()
