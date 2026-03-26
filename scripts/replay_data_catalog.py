#!/usr/bin/env python3
"""
Local-only catalog replay helper for backend smoke testing.

Why this is PS-safe:
- it does not change the backend API surface
- it only uses the existing `POST /api/telemetry` and `POST /api/simulate/step`
- it does not change default runtime behavior
- `data.txt` stays local and gitignored

What it does:
- reads Space-Track OMM JSON (`data.txt`) or local 3LE/TLE text (`3le_data.txt`)
- converts a filtered LEO subset into ECI state vectors at one common timestamp
- promotes a small operator fleet from real PAYLOAD records
- supports two operator-fleet modes:
  - `synthetic`: keep legacy `SAT-LOCAL-*` IDs on top of real payload orbits
  - `catalog`: promote real payloads directly as `SAT-<NORAD>` satellites
- can load repeatable replay defaults from a JSON scenario manifest
- can prioritize specific debris NORAD IDs so mined natural-watch scenes survive replay caps
- injects the rest as DEBRIS via the normal telemetry endpoint
- optionally fast-forwards a few short steps to populate trajectories

The stricter `catalog` mode minimizes synthetic data generation by keeping the
operator fleet tied to real payload records instead of synthetic satellite IDs.
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

DEFAULT_DATA_PATH = "data.txt"
DEFAULT_API_BASE = "http://localhost:8000"
DEFAULT_TIMESTAMP = ""
DEFAULT_OPERATOR_SATS = 8
DEFAULT_SATELLITE_MODE = "synthetic"
DEFAULT_MAX_DEBRIS = 5000
DEFAULT_MAX_PERIAPSIS_KM = 2000.0
DEFAULT_BATCH_SIZE = 500
DEFAULT_WARMUP_STEPS = 3
DEFAULT_WARMUP_STEP_SECONDS = 60


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
        description="Replay local catalog data through the normal telemetry API",
    )
    parser.add_argument(
        "--manifest",
        default="",
        help=(
            "Optional JSON scenario manifest. If provided, manifest values become defaults "
            "for the replay settings and may still be overridden by explicit CLI flags."
        ),
    )
    parser.add_argument(
        "--data",
        default=DEFAULT_DATA_PATH,
        help="Path to a local OMM JSON or 3LE/TLE text file (default: data.txt)",
    )
    parser.add_argument(
        "--api-base",
        default=DEFAULT_API_BASE,
        help="Backend base URL (default: http://localhost:8000)",
    )
    parser.add_argument(
        "--timestamp",
        default=DEFAULT_TIMESTAMP,
        help="Target telemetry timestamp (ISO-8601 UTC). Defaults to the first record epoch, bumped forward if backend time is newer.",
    )
    parser.add_argument(
        "--operator-sats",
        "--synthetic-sats",
        dest="operator_sats",
        type=int,
        default=DEFAULT_OPERATOR_SATS,
        help=(
            "Number of payload records to promote into the operator fleet (default: 8). "
            "In synthetic mode they become SAT-LOCAL-*; in catalog mode they become SAT-<NORAD>."
        ),
    )
    parser.add_argument(
        "--satellite-mode",
        choices=("synthetic", "catalog"),
        default=DEFAULT_SATELLITE_MODE,
        help=(
            "How to represent promoted payloads in telemetry: synthetic SAT-LOCAL-* IDs "
            "or real catalog-derived SAT-<NORAD> IDs (default: synthetic)"
        ),
    )
    parser.add_argument(
        "--operator-norad",
        action="append",
        default=[],
        metavar="NORAD_ID",
        help=(
            "Explicit PAYLOAD NORAD ID to include in the operator fleet; may be repeated. "
            "If omitted, the fleet is chosen deterministically from the usable payload set."
        ),
    )
    parser.add_argument(
        "--max-debris",
        type=int,
        default=DEFAULT_MAX_DEBRIS,
        help="Max catalog objects to inject as DEBRIS after satellite seeding; 0 means all filtered objects (default: 5000)",
    )
    parser.add_argument(
        "--priority-debris-norad",
        action="append",
        default=[],
        metavar="NORAD_ID",
        help=(
            "Explicit debris NORAD ID to place at the front of the replay set; may be repeated. "
            "Useful for manifest-driven natural watch scenarios."
        ),
    )
    parser.add_argument(
        "--max-periapsis-km",
        type=float,
        default=DEFAULT_MAX_PERIAPSIS_KM,
        help="LEO filter on periapsis altitude in km (default: 2000)",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=DEFAULT_BATCH_SIZE,
        help="Objects per telemetry POST (default: 500)",
    )
    parser.add_argument(
        "--warmup-steps",
        type=int,
        default=DEFAULT_WARMUP_STEPS,
        help="How many simulate/step calls to run after ingest (default: 3)",
    )
    parser.add_argument(
        "--warmup-step-seconds",
        type=int,
        default=DEFAULT_WARMUP_STEP_SECONDS,
        help="step_seconds value for each warmup step (default: 60)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Prepare and summarize the replay scenario without posting telemetry or stepping the backend",
    )
    return parser.parse_args()


def load_manifest(path: pathlib.Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        die(f"manifest file not found: {path}")
    except json.JSONDecodeError as exc:
        die(f"invalid JSON in manifest {path}: {exc}")

    if not isinstance(raw, dict):
        die("scenario manifest root must be a JSON object")
    return raw


def object_section(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def first_manifest_value(*values: Any) -> Any:
    for value in values:
        if value is not None:
            return value
    return None


def apply_manifest_defaults(args: argparse.Namespace) -> argparse.Namespace:
    if not args.manifest:
        args.manifest_data = None
        args.manifest_path = None
        return args

    manifest_path = pathlib.Path(args.manifest).expanduser().resolve()
    manifest = load_manifest(manifest_path)
    replay = object_section(manifest.get("replay"))
    operator_selection = object_section(manifest.get("operator_selection"))
    filters = object_section(manifest.get("filters"))
    warmup = object_section(manifest.get("warmup"))

    def apply_if_default(attr: str, default: Any, *manifest_values: Any) -> None:
        if getattr(args, attr) != default:
            return
        chosen = first_manifest_value(*manifest_values)
        if chosen is not None:
            setattr(args, attr, chosen)

    apply_if_default("data", DEFAULT_DATA_PATH, replay.get("data"), manifest.get("data"))
    apply_if_default("api_base", DEFAULT_API_BASE, replay.get("api_base"), manifest.get("api_base"))
    apply_if_default("timestamp", DEFAULT_TIMESTAMP, replay.get("timestamp"), manifest.get("timestamp"), manifest.get("target_epoch"))
    apply_if_default("satellite_mode", DEFAULT_SATELLITE_MODE, replay.get("satellite_mode"), operator_selection.get("mode"), manifest.get("satellite_mode"))
    apply_if_default("operator_sats", DEFAULT_OPERATOR_SATS, replay.get("operator_sats"), operator_selection.get("count"), manifest.get("operator_sats"))
    apply_if_default("max_debris", DEFAULT_MAX_DEBRIS, replay.get("max_debris"), filters.get("max_debris"), manifest.get("max_debris"))
    apply_if_default("max_periapsis_km", DEFAULT_MAX_PERIAPSIS_KM, replay.get("max_periapsis_km"), filters.get("max_periapsis_km"), manifest.get("max_periapsis_km"))
    apply_if_default("batch_size", DEFAULT_BATCH_SIZE, replay.get("batch_size"), filters.get("batch_size"), manifest.get("batch_size"))
    apply_if_default("warmup_steps", DEFAULT_WARMUP_STEPS, replay.get("warmup_steps"), warmup.get("steps"), manifest.get("warmup_steps"))
    apply_if_default("warmup_step_seconds", DEFAULT_WARMUP_STEP_SECONDS, replay.get("warmup_step_seconds"), warmup.get("step_seconds"), manifest.get("warmup_step_seconds"))

    if not args.operator_norad:
        raw_norads = first_manifest_value(
            replay.get("operator_norad"),
            operator_selection.get("norad_ids"),
            manifest.get("operator_norad"),
        )
        if raw_norads is not None:
            if not isinstance(raw_norads, list):
                die("manifest field operator_norad / operator_selection.norad_ids must be an array")
            args.operator_norad = [str(value).strip() for value in raw_norads if str(value).strip()]

    if not args.priority_debris_norad:
        raw_debris_norads = first_manifest_value(
            replay.get("priority_debris_norad"),
            manifest.get("priority_debris_norad"),
            object_section(manifest.get("mining")).get("priority_debris_norad"),
        )
        if raw_debris_norads is not None:
            if not isinstance(raw_debris_norads, list):
                die("manifest field priority_debris_norad must be an array")
            args.priority_debris_norad = [str(value).strip() for value in raw_debris_norads if str(value).strip()]

    if args.satellite_mode not in {"synthetic", "catalog"}:
        die("satellite_mode must be 'synthetic' or 'catalog'")

    args.manifest_data = manifest
    args.manifest_path = manifest_path
    return args


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


def parse_tle_epoch(raw: str) -> float:
    field = raw.strip()
    if len(field) < 5:
        raise ValueError("invalid TLE epoch")

    year_2 = int(field[:2])
    day_of_year = float(field[2:])
    year = 1900 + year_2 if year_2 >= 57 else 2000 + year_2
    whole_day = int(math.floor(day_of_year))
    frac_day = day_of_year - whole_day
    stamp = dt.datetime(year, 1, 1, tzinfo=dt.timezone.utc) + dt.timedelta(days=whole_day - 1, seconds=frac_day * 86400.0)
    return stamp.timestamp()


def guess_tle_object_type(name: str) -> str:
    upper = name.upper()
    if "R/B" in upper or "ROCKET BODY" in upper:
        return "ROCKET BODY"
    if "DEB" in upper or " DEB" in upper or "DEBRIS" in upper:
        return "DEBRIS"
    if name.strip().isdigit():
        return "UNKNOWN"
    return "PAYLOAD"


def semimajor_axis_from_mean_motion(n_rev_day: float) -> float:
    n_rad_s = n_rev_day * 2.0 * math.pi / 86400.0
    if n_rad_s <= EPS:
        raise ValueError("invalid mean motion")
    return (MU_KM3_S2 / (n_rad_s * n_rad_s)) ** (1.0 / 3.0)


def parse_omm_catalog(raw_text: str, path: pathlib.Path, max_periapsis_km: float) -> tuple[list[OmmRecord], float]:
    try:
        raw = json.loads(raw_text)
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

    print(f"[info] parsed {len(records)} usable LEO OMM records from {path} ({skipped} skipped)")
    return records, first_epoch_s or parse_iso8601_any("2026-03-20T00:00:00Z")


def parse_tle_catalog(raw_text: str, path: pathlib.Path, max_periapsis_km: float) -> tuple[list[OmmRecord], float]:
    lines = raw_text.splitlines()
    records: list[OmmRecord] = []
    first_epoch_s: float | None = None
    skipped = 0
    idx = 0

    while idx < len(lines):
        line0 = lines[idx].strip()
        if not line0:
            idx += 1
            continue

        name = ""
        if line0.startswith("0 "):
            if idx + 2 >= len(lines):
                skipped += 1
                break
            name = line0[2:].strip()
            line1 = lines[idx + 1].rstrip("\n")
            line2 = lines[idx + 2].rstrip("\n")
            idx += 3
        elif line0.startswith("1 "):
            if idx + 1 >= len(lines):
                skipped += 1
                break
            line1 = line0
            line2 = lines[idx + 1].rstrip("\n")
            idx += 2
        else:
            skipped += 1
            idx += 1
            continue

        if not line1.startswith("1 ") or not line2.startswith("2 "):
            skipped += 1
            continue

        try:
            norad_id = line1[2:7].strip() or line2[2:7].strip()
            epoch_s = parse_tle_epoch(line1[18:32])
            i_deg = float(line2[8:16].strip())
            raan_deg = float(line2[17:25].strip())
            ecc = float(f"0.{line2[26:33].strip()}")
            argp_deg = float(line2[34:42].strip())
            mean_anomaly_deg = float(line2[43:51].strip())
            mean_motion = float(line2[52:63].strip())
            a_km = semimajor_axis_from_mean_motion(mean_motion)
        except (ValueError, IndexError):
            skipped += 1
            continue

        periapsis_alt = a_km * (1.0 - ecc) - R_EARTH_KM
        if not norad_id or a_km < R_EARTH_KM or ecc < 0.0 or ecc >= 1.0 or periapsis_alt > max_periapsis_km:
            skipped += 1
            continue

        object_name = name or norad_id
        object_type = guess_tle_object_type(object_name)
        if first_epoch_s is None:
            first_epoch_s = epoch_s

        records.append(
            OmmRecord(
                norad_id=norad_id,
                object_name=object_name,
                object_type=object_type,
                epoch_s=epoch_s,
                a_km=a_km,
                e=ecc,
                i_rad=math.radians(i_deg),
                raan_rad=math.radians(raan_deg),
                argp_rad=math.radians(argp_deg),
                M_rad=math.radians(mean_anomaly_deg),
                periapsis_alt_km=periapsis_alt,
            )
        )

    if not records:
        die(f"no valid LEO TLE/3LE records found in {path}")

    print(f"[info] parsed {len(records)} usable LEO TLE/3LE records from {path} ({skipped} skipped)")
    return records, first_epoch_s or parse_iso8601_any("2026-03-20T00:00:00Z")


def parse_catalog(path: pathlib.Path, max_periapsis_km: float) -> tuple[list[OmmRecord], float]:
    try:
        raw_text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        die(f"data file not found: {path}")

    stripped = raw_text.lstrip()
    if stripped.startswith("["):
        return parse_omm_catalog(raw_text, path, max_periapsis_km)
    return parse_tle_catalog(raw_text, path, max_periapsis_km)


def operator_satellite_id(record: OmmRecord, satellite_mode: str, index: int) -> str:
    if satellite_mode == "catalog":
        return f"SAT-{record.norad_id}"
    return f"SAT-LOCAL-{index:02d}"


def debris_object_id(record: OmmRecord) -> str:
    return f"DEB-{record.norad_id}"


def select_operator_payloads(
    payloads: list[OmmRecord],
    operator_sat_count: int,
    operator_norads: list[str],
) -> list[OmmRecord]:
    by_norad = {rec.norad_id: rec for rec in payloads}
    selected: list[OmmRecord] = []
    seen: set[str] = set()

    normalized_requested = [value.strip() for value in operator_norads if value and value.strip()]
    for norad_id in normalized_requested:
        rec = by_norad.get(norad_id)
        if rec is None:
            die(f"requested operator NORAD {norad_id} is not available as a usable PAYLOAD record")
        if rec.norad_id in seen:
            continue
        selected.append(rec)
        seen.add(rec.norad_id)

    target_count = max(0, operator_sat_count)
    if len(selected) > target_count:
        target_count = len(selected)

    for rec in payloads:
        if len(selected) >= target_count:
            break
        if rec.norad_id in seen:
            continue
        selected.append(rec)
        seen.add(rec.norad_id)

    return selected


def backend_epoch(api_base: str) -> float | None:
    req = urllib.request.Request(f"{api_base}/api/visualization/snapshot", method="GET")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            body = resp.read().decode("utf-8")
    except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError):
        return None
    try:
        snapshot = json.loads(body)
    except json.JSONDecodeError:
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
    satellite_mode: str,
    operator_sat_count: int,
    max_debris: int,
    operator_norads: list[str],
    priority_debris_norads: list[str],
) -> tuple[list[dict[str, Any]], list[OmmRecord], int]:
    payloads = sorted((rec for rec in records if rec.is_payload), key=lambda rec: (rec.e, rec.periapsis_alt_km, rec.norad_id))
    others = sorted((rec for rec in records if not rec.is_payload), key=lambda rec: rec.norad_id)

    seeded_payloads = select_operator_payloads(payloads, operator_sat_count, operator_norads)
    seeded_norad_ids = {rec.norad_id for rec in seeded_payloads}

    objects: list[dict[str, Any]] = []

    for idx, rec in enumerate(seeded_payloads, start=1):
        r, v = propagate_record(rec, target_epoch_s)
        objects.append(
            {
                "id": operator_satellite_id(rec, satellite_mode, idx),
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

    if priority_debris_norads:
        priority_set = {value for value in priority_debris_norads if value}
        prioritized = [rec for rec in debris_records if rec.norad_id in priority_set]
        remaining = [rec for rec in debris_records if rec.norad_id not in priority_set]
        debris_records = prioritized + remaining

    if max_debris > 0:
        debris_records = debris_records[:max_debris]

    for rec in debris_records:
        r, v = propagate_record(rec, target_epoch_s)
        objects.append(
            {
                "id": debris_object_id(rec),
                "type": "DEBRIS",
                "r": {"x": r[0], "y": r[1], "z": r[2]},
                "v": {"x": v[0], "y": v[1], "z": v[2]},
            }
        )

    return objects, seeded_payloads, len(debris_records)


def chunked(items: list[dict[str, Any]], size: int) -> Iterable[list[dict[str, Any]]]:
    for idx in range(0, len(items), size):
        yield items[idx: idx + size]


def print_operator_summary(selected_payloads: list[OmmRecord], satellite_mode: str) -> None:
    print(f"[info] operator satellite mode: {satellite_mode}")
    if not selected_payloads:
        print("[info] selected operator satellites: none")
        return

    print("[info] selected operator satellites:")
    max_rows = 12
    for idx, rec in enumerate(selected_payloads[:max_rows], start=1):
        sat_id = operator_satellite_id(rec, satellite_mode, idx)
        print(f"  - {sat_id} <- {rec.object_name} [{rec.norad_id}]")
    if len(selected_payloads) > max_rows:
        remaining = len(selected_payloads) - max_rows
        print(f"  - ... and {remaining} more")


def print_priority_debris_summary(priority_debris_norads: list[str]) -> None:
    normalized = [value for value in priority_debris_norads if value]
    if not normalized:
        return
    preview = ", ".join(normalized[:8])
    if len(normalized) > 8:
        preview += f", ... (+{len(normalized) - 8} more)"
    print(f"[info] priority debris NORADs: {preview}")


def print_manifest_summary(args: argparse.Namespace) -> None:
    manifest_path = getattr(args, "manifest_path", None)
    manifest_data = getattr(args, "manifest_data", None)
    if manifest_path is None or manifest_data is None:
        return

    scenario_id = manifest_data.get("scenario_id")
    if isinstance(scenario_id, str) and scenario_id.strip():
        print(f"[info] scenario manifest: {scenario_id} ({manifest_path})")
    else:
        print(f"[info] scenario manifest: {manifest_path}")


def replay_catalog(args: argparse.Namespace) -> None:
    data_path = pathlib.Path(args.data).expanduser().resolve()
    records, first_epoch_s = parse_catalog(data_path, args.max_periapsis_km)
    target_epoch_s, target_timestamp = choose_target_epoch(args, first_epoch_s)

    objects, selected_payloads, debris_count = build_objects(
        records,
        target_epoch_s,
        args.satellite_mode,
        args.operator_sats,
        args.max_debris,
        args.operator_norad,
        args.priority_debris_norad,
    )
    sat_count = len(selected_payloads)
    print_manifest_summary(args)
    print(f"[info] replay timestamp: {target_timestamp}")
    print_operator_summary(selected_payloads, args.satellite_mode)
    print_priority_debris_summary(args.priority_debris_norad)
    print(f"[info] injecting {sat_count} satellites + {debris_count} debris objects")

    if args.dry_run:
        print("[dry-run] no telemetry posted and no simulation steps executed")
        return

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
    conjunctions_history = http_json("GET", f"{args.api_base}/api/debug/conjunctions?source=history")
    conjunctions_predicted = http_json("GET", f"{args.api_base}/api/debug/conjunctions?source=predicted")

    satellites = snapshot.get("satellites", [])
    debris_cloud = snapshot.get("debris_cloud", [])
    pending_burns = burns.get("pending", [])
    executed_burns = burns.get("executed", [])
    conjunction_history_count = conjunctions_history.get("count", 0)
    conjunction_predicted_count = conjunctions_predicted.get("count", 0)

    print("[summary]")
    print(f"  telemetry_processed: {total_processed}")
    print(f"  snapshot_timestamp:  {snapshot.get('timestamp', '--')}")
    print(f"  satellites_visible:  {len(satellites)}")
    print(f"  debris_visible:      {len(debris_cloud)}")
    print(f"  conjunctions_hist:   {conjunction_history_count}")
    print(f"  conjunctions_pred:   {conjunction_predicted_count}")
    print(f"  burns_pending:       {len(pending_burns)}")
    print(f"  burns_executed:      {len(executed_burns)}")
    print(f"  last_cdm_warning_ct: {final_warning_count}")
    print(f"  dashboard:           {args.api_base}")
    print("[note] for a clean replay, restart the backend/container before running this again.")


def main() -> None:
    args = apply_manifest_defaults(parse_args())
    replay_catalog(args)


if __name__ == "__main__":
    main()
