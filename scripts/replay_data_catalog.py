#!/usr/bin/env python3
"""
Local-only `data.txt` replay helper for backend smoke testing.

Why this is PS-safe:
- it does not change the backend API surface
- it only uses the existing `POST /api/telemetry` and `POST /api/simulate/step`
- it does not change default runtime behavior
- `data.txt` stays local and gitignored

What it does:
- reads Space-Track OMM records from `data.txt`
- converts a filtered LEO subset into ECI state vectors at one common timestamp
- seeds a small local test constellation from a handful of PAYLOAD records
- injects the rest as DEBRIS via the normal telemetry endpoint
- optionally fast-forwards a few short steps to populate trajectories

The local test satellites are clearly synthetic (`SAT-LOCAL-*`) and exist only to
exercise the dashboard/UI while keeping the real catalog objects as the threat field.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import pathlib
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Iterable


MU_KM3_S2 = 398600.4418
J2 = 1.08263e-3
R_EARTH_KM = 6378.137
SOURCE_ID = "local-data-txt-replay"
EPS = 1.0e-12


@dataclass
class OmmRecord:
    norad_id: str
    object_name: str
    object_type: str
    epoch_s: float
    a_km: float
    e: float
    i_rad: float
    raan_rad: float
    argp_rad: float
    M_rad: float
    periapsis_alt_km: float

    @property
    def is_payload(self) -> bool:
        return self.object_type == "PAYLOAD"


def die(message: str) -> None:
    print(f"[error] {message}", file=sys.stderr)
    raise SystemExit(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Replay local data.txt through the normal telemetry API",
    )
    parser.add_argument(
        "--data",
        default="data.txt",
        help="Path to Space-Track OMM JSON file (default: data.txt)",
    )
    parser.add_argument(
        "--api-base",
        default="http://localhost:8000",
        help="Backend base URL (default: http://localhost:8000)",
    )
    parser.add_argument(
        "--timestamp",
        default="",
        help="Target telemetry timestamp (ISO-8601 UTC). Defaults to the first record epoch, bumped forward if backend time is newer.",
    )
    parser.add_argument(
        "--synthetic-sats",
        type=int,
        default=8,
        help="Number of local SAT-LOCAL-* satellites to seed from payload orbits (default: 8)",
    )
    parser.add_argument(
        "--max-debris",
        type=int,
        default=5000,
        help="Max catalog objects to inject as DEBRIS after satellite seeding; 0 means all filtered objects (default: 5000)",
    )
    parser.add_argument(
        "--max-periapsis-km",
        type=float,
        default=2000.0,
        help="LEO filter on periapsis altitude in km (default: 2000)",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=500,
        help="Objects per telemetry POST (default: 500)",
    )
    parser.add_argument(
        "--warmup-steps",
        type=int,
        default=3,
        help="How many simulate/step calls to run after ingest (default: 3)",
    )
    parser.add_argument(
        "--warmup-step-seconds",
        type=int,
        default=60,
        help="step_seconds value for each warmup step (default: 60)",
    )
    return parser.parse_args()


def http_json(
    method: str,
    url: str,
    payload: dict[str, Any] | None = None,
) -> dict[str, Any]:
    data = None
    headers = {"X-Source-Id": SOURCE_ID}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            body = resp.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        die(f"{method} {url} -> HTTP {exc.code}: {body}")
    except urllib.error.URLError as exc:
        die(f"{method} {url} failed: {exc}")
    try:
        return json.loads(body)
    except json.JSONDecodeError as exc:
        die(f"{method} {url} returned non-JSON body: {exc}")


def parse_iso8601_any(value: str) -> float:
    raw = value.strip()
    if not raw:
        raise ValueError("empty timestamp")
    if raw.endswith("Z"):
        raw = raw[:-1] + "+00:00"
    parsed = dt.datetime.fromisoformat(raw)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=dt.timezone.utc)
    else:
        parsed = parsed.astimezone(dt.timezone.utc)
    return parsed.timestamp()


def iso8601_utc(epoch_s: float) -> str:
    whole = int(math.floor(epoch_s))
    millis = int(round((epoch_s - whole) * 1000.0))
    if millis >= 1000:
        millis -= 1000
        whole += 1
    stamp = dt.datetime.fromtimestamp(whole, tz=dt.timezone.utc)
    return f"{stamp:%Y-%m-%dT%H:%M:%S}.{millis:03d}Z"


def wrap_0_2pi(angle: float) -> float:
    wrapped = math.fmod(angle, 2.0 * math.pi)
    if wrapped < 0.0:
        wrapped += 2.0 * math.pi
    return wrapped


def solve_kepler_elliptic(M_rad: float, e: float) -> float:
    M = wrap_0_2pi(M_rad)
    E = M if e < 0.8 else math.pi
    for _ in range(15):
        f = E - e * math.sin(E) - M
        fp = 1.0 - e * math.cos(E)
        if abs(fp) < EPS:
            break
        step = f / fp
        E -= step
        if abs(step) < 1.0e-13:
            break
    return wrap_0_2pi(E)


def j2_rates(a_km: float, e: float, i_rad: float) -> tuple[float, float, float]:
    n = math.sqrt(MU_KM3_S2 / (a_km * a_km * a_km))
    p_km = a_km * (1.0 - e * e)
    if p_km <= EPS:
        return 0.0, 0.0, n
    a3_5 = (a_km * a_km * a_km) * math.sqrt(a_km)
    one_minus_e2 = 1.0 - e * e
    one_minus_e2_sq = one_minus_e2 * one_minus_e2
    cos_i = math.cos(i_rad)

    raan_dot = -1.5 * J2 * math.sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
    raan_dot /= a3_5 * one_minus_e2_sq
    raan_dot *= cos_i

    argp_dot = 0.75 * J2 * math.sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
    argp_dot /= a3_5 * one_minus_e2_sq
    argp_dot *= (5.0 * cos_i * cos_i - 1.0)

    mean_dot = 0.75 * J2 * math.sqrt(MU_KM3_S2) * (R_EARTH_KM * R_EARTH_KM)
    mean_dot /= a3_5 * (one_minus_e2 * math.sqrt(one_minus_e2))
    mean_dot = n + mean_dot * (3.0 * cos_i * cos_i - 1.0)

    return raan_dot, argp_dot, mean_dot


def elements_to_eci(
    a_km: float,
    e: float,
    i_rad: float,
    raan_rad: float,
    argp_rad: float,
    M_rad: float,
) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    E = solve_kepler_elliptic(M_rad, e)
    cE = math.cos(E)
    sE = math.sin(E)
    one_minus_ecE = 1.0 - e * cE
    if abs(one_minus_ecE) < EPS:
        raise ValueError("degenerate eccentric anomaly")

    x_pf = a_km * (cE - e)
    y_pf = a_km * math.sqrt(1.0 - e * e) * sE

    n = math.sqrt(MU_KM3_S2 / (a_km * a_km * a_km))
    vx_pf = -a_km * n * sE / one_minus_ecE
    vy_pf = a_km * n * math.sqrt(1.0 - e * e) * cE / one_minus_ecE

    cO = math.cos(raan_rad)
    sO = math.sin(raan_rad)
    ci = math.cos(i_rad)
    si = math.sin(i_rad)
    cw = math.cos(argp_rad)
    sw = math.sin(argp_rad)

    R11 = cO * cw - sO * sw * ci
    R12 = -cO * sw - sO * cw * ci
    R21 = sO * cw + cO * sw * ci
    R22 = -sO * sw + cO * cw * ci
    R31 = sw * si
    R32 = cw * si

    r = (
        R11 * x_pf + R12 * y_pf,
        R21 * x_pf + R22 * y_pf,
        R31 * x_pf + R32 * y_pf,
    )
    v = (
        R11 * vx_pf + R12 * vy_pf,
        R21 * vx_pf + R22 * vy_pf,
        R31 * vx_pf + R32 * vy_pf,
    )
    return r, v


def as_float(record: dict[str, Any], key: str) -> float | None:
    value = record.get(key)
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str) and value.strip():
        try:
            return float(value)
        except ValueError:
            return None
    return None


def as_string(record: dict[str, Any], key: str) -> str:
    value = record.get(key)
    if value is None:
        return ""
    return str(value).strip()


def parse_catalog(path: pathlib.Path, max_periapsis_km: float) -> tuple[list[OmmRecord], float]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        die(f"data file not found: {path}")
    except json.JSONDecodeError as exc:
        die(f"invalid JSON in {path}: {exc}")

    if not isinstance(raw, list):
        die("data.txt root must be a JSON array")

    records: list[OmmRecord] = []
    first_epoch_s: float | None = None
    skipped = 0
    for item in raw:
        if not isinstance(item, dict):
            skipped += 1
            continue
        norad_id = as_string(item, "NORAD_CAT_ID")
        epoch_raw = as_string(item, "EPOCH")
        a_km = as_float(item, "SEMIMAJOR_AXIS")
        e = as_float(item, "ECCENTRICITY")
        i_deg = as_float(item, "INCLINATION")
        raan_deg = as_float(item, "RA_OF_ASC_NODE")
        argp_deg = as_float(item, "ARG_OF_PERICENTER")
        M_deg = as_float(item, "MEAN_ANOMALY")
        periapsis_alt = as_float(item, "PERIAPSIS")
        object_type = as_string(item, "OBJECT_TYPE").upper() or "UNKNOWN"
        object_name = as_string(item, "OBJECT_NAME") or norad_id

        if not norad_id or not epoch_raw:
            skipped += 1
            continue
        if None in (a_km, e, i_deg, raan_deg, argp_deg, M_deg, periapsis_alt):
            skipped += 1
            continue
        if a_km is None or e is None or i_deg is None or raan_deg is None or argp_deg is None or M_deg is None or periapsis_alt is None:
            skipped += 1
            continue
        if a_km < R_EARTH_KM or e < 0.0 or e >= 1.0 or periapsis_alt > max_periapsis_km:
            skipped += 1
            continue

        try:
            epoch_s = parse_iso8601_any(epoch_raw)
        except ValueError:
            skipped += 1
            continue

        if first_epoch_s is None:
            first_epoch_s = epoch_s

        records.append(
            OmmRecord(
                norad_id=norad_id,
                object_name=object_name,
                object_type=object_type,
                epoch_s=epoch_s,
                a_km=a_km,
                e=e,
                i_rad=math.radians(i_deg),
                raan_rad=math.radians(raan_deg),
                argp_rad=math.radians(argp_deg),
                M_rad=math.radians(M_deg),
                periapsis_alt_km=periapsis_alt,
            )
        )

    if not records:
        die("no valid LEO OMM records found in data.txt")

    print(f"[info] parsed {len(records)} usable LEO records from {path} ({skipped} skipped)")
    return records, first_epoch_s or parse_iso8601_any("2026-03-20T00:00:00Z")


def backend_epoch(api_base: str) -> float | None:
    try:
        snapshot = http_json("GET", f"{api_base}/api/visualization/snapshot")
    except SystemExit:
        return None
    timestamp = snapshot.get("timestamp")
    if not isinstance(timestamp, str) or not timestamp:
        return None
    try:
        return parse_iso8601_any(timestamp)
    except ValueError:
        return None


def choose_target_epoch(args: argparse.Namespace, first_epoch_s: float) -> tuple[float, str]:
    base_epoch = parse_iso8601_any(args.timestamp) if args.timestamp else first_epoch_s
    current_epoch = backend_epoch(args.api_base)
    if current_epoch is not None and base_epoch <= current_epoch + 1.0:
        base_epoch = current_epoch + 3600.0
        print(
            "[info] backend clock is already ahead of the catalog base epoch; "
            f"bumping replay timestamp to {iso8601_utc(base_epoch)}"
        )
    return base_epoch, iso8601_utc(base_epoch)


def propagate_record(record: OmmRecord, target_epoch_s: float) -> tuple[tuple[float, float, float], tuple[float, float, float]]:
    dt_s = target_epoch_s - record.epoch_s
    raan_dot, argp_dot, mean_dot = j2_rates(record.a_km, record.e, record.i_rad)
    raan = wrap_0_2pi(record.raan_rad + raan_dot * dt_s)
    argp = wrap_0_2pi(record.argp_rad + argp_dot * dt_s)
    mean = wrap_0_2pi(record.M_rad + mean_dot * dt_s)
    return elements_to_eci(record.a_km, record.e, record.i_rad, raan, argp, mean)


def build_objects(
    records: list[OmmRecord],
    target_epoch_s: float,
    synthetic_sat_count: int,
    max_debris: int,
) -> tuple[list[dict[str, Any]], int, int]:
    payloads = sorted((rec for rec in records if rec.is_payload), key=lambda rec: (rec.e, rec.periapsis_alt_km, rec.norad_id))
    others = sorted((rec for rec in records if not rec.is_payload), key=lambda rec: rec.norad_id)

    seeded_payloads = payloads[: max(0, synthetic_sat_count)]
    seeded_norad_ids = {rec.norad_id for rec in seeded_payloads}

    objects: list[dict[str, Any]] = []

    for idx, rec in enumerate(seeded_payloads, start=1):
        r, v = propagate_record(rec, target_epoch_s)
        objects.append(
            {
                "id": f"SAT-LOCAL-{idx:02d}",
                "type": "SATELLITE",
                "r": {"x": r[0], "y": r[1], "z": r[2]},
                "v": {"x": v[0], "y": v[1], "z": v[2]},
            }
        )

    debris_records: list[OmmRecord] = []
    for rec in payloads:
        if rec.norad_id in seeded_norad_ids:
            continue
        debris_records.append(rec)
    debris_records.extend(others)
    if max_debris > 0:
        debris_records = debris_records[:max_debris]

    for rec in debris_records:
        r, v = propagate_record(rec, target_epoch_s)
        objects.append(
            {
                "id": f"DEB-{rec.norad_id}",
                "type": "DEBRIS",
                "r": {"x": r[0], "y": r[1], "z": r[2]},
                "v": {"x": v[0], "y": v[1], "z": v[2]},
            }
        )

    return objects, len(seeded_payloads), len(debris_records)


def chunked(items: list[dict[str, Any]], size: int) -> Iterable[list[dict[str, Any]]]:
    for idx in range(0, len(items), size):
        yield items[idx: idx + size]


def replay_catalog(args: argparse.Namespace) -> None:
    data_path = pathlib.Path(args.data).expanduser().resolve()
    records, first_epoch_s = parse_catalog(data_path, args.max_periapsis_km)
    target_epoch_s, target_timestamp = choose_target_epoch(args, first_epoch_s)

    objects, sat_count, debris_count = build_objects(
        records,
        target_epoch_s,
        args.synthetic_sats,
        args.max_debris,
    )
    print(f"[info] replay timestamp: {target_timestamp}")
    print(f"[info] injecting {sat_count} local satellites + {debris_count} debris objects")

    total_processed = 0
    final_warning_count = 0
    for batch_index, batch in enumerate(chunked(objects, max(1, args.batch_size)), start=1):
        payload = {
            "timestamp": target_timestamp,
            "objects": batch,
        }
        ack = http_json("POST", f"{args.api_base}/api/telemetry", payload)
        total_processed += int(ack.get("processed_count", 0))
        final_warning_count = int(ack.get("active_cdm_warnings", final_warning_count))
        print(
            f"[telemetry {batch_index}] processed_count={ack.get('processed_count', 0)} "
            f"active_cdm_warnings={ack.get('active_cdm_warnings', 0)}"
        )

    for step_idx in range(args.warmup_steps):
        step = http_json(
            "POST",
            f"{args.api_base}/api/simulate/step",
            {"step_seconds": args.warmup_step_seconds},
        )
        print(
            f"[warmup {step_idx + 1}/{args.warmup_steps}] "
            f"new_timestamp={step.get('new_timestamp')} "
            f"collisions={step.get('collisions_detected')} "
            f"maneuvers={step.get('maneuvers_executed')}"
        )

    snapshot = http_json("GET", f"{args.api_base}/api/visualization/snapshot")
    burns = http_json("GET", f"{args.api_base}/api/debug/burns")
    conjunctions = http_json("GET", f"{args.api_base}/api/debug/conjunctions")

    satellites = snapshot.get("satellites", [])
    debris_cloud = snapshot.get("debris_cloud", [])
    pending_burns = burns.get("pending", [])
    executed_burns = burns.get("executed", [])
    conjunction_count = conjunctions.get("count", 0)

    print("[summary]")
    print(f"  telemetry_processed: {total_processed}")
    print(f"  snapshot_timestamp:  {snapshot.get('timestamp', '--')}")
    print(f"  satellites_visible:  {len(satellites)}")
    print(f"  debris_visible:      {len(debris_cloud)}")
    print(f"  conjunctions:        {conjunction_count}")
    print(f"  burns_pending:       {len(pending_burns)}")
    print(f"  burns_executed:      {len(executed_burns)}")
    print(f"  last_cdm_warning_ct: {final_warning_count}")
    print(f"  dashboard:           {args.api_base}")
    print("[note] for a clean replay, restart the backend/container before running this again.")


def main() -> None:
    args = parse_args()
    replay_catalog(args)


if __name__ == "__main__":
    main()
