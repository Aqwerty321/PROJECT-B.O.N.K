#!/usr/bin/env python3
"""
Lightweight natural-scene miner for strict real-data replay manifests.

This is intentionally conservative and local-only. It mines replay presets from
real catalog data without fabricating new orbital geometry. The current version
supports both OMM JSON and 3LE/TLE text catalogs, scores payloads by nearby
natural traffic, and emits manifest files that can be fed directly into
`scripts/replay_data_catalog.py --manifest ...`.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import pathlib
from dataclasses import dataclass
from typing import Any

from evaluate_strict_manifest import evaluate_manifest
from rank_strict_manifests import runtime_score, start_backend, stop_backend
from replay_data_catalog import (
    DEFAULT_API_BASE,
    DEFAULT_MAX_DEBRIS,
    DEFAULT_MAX_PERIAPSIS_KM,
    DEFAULT_SATELLITE_MODE,
    DEFAULT_WARMUP_STEP_SECONDS,
    DEFAULT_WARMUP_STEPS,
    OmmRecord,
    iso8601_utc,
    parse_catalog,
    propagate_record,
)
from replay_data_catalog import choose_target_epoch as replay_choose_target_epoch


@dataclass
class PayloadThreatSummary:
    payload: OmmRecord
    score: float
    min_miss_km: float
    best_miss_epoch_s: float
    avg_top3_miss_km: float
    critical_count: int
    warning_count: int
    close_count: int
    near_100_count: int
    near_250_count: int
    near_500_count: int
    los_sample_count: int
    los_station_ids: list[str]
    best_station_id: str | None
    best_elevation_deg: float
    family_tag: str
    shell_tag: str
    shell_density_count: int
    phase_density_count: int
    top_debris: list["ThreatOpportunity"]


@dataclass
class ThreatOpportunity:
    norad_id: str
    min_miss_km: float
    tca_epoch_s: float


@dataclass
class AnchorPrefilterSummary:
    payload: OmmRecord
    pre_score: float
    shell_density_count: int
    phase_density_count: int
    family_tag: str
    shell_tag: str


@dataclass
class EncounterWindow:
    target_epoch_s: float
    cluster_start_epoch_s: float
    cluster_end_epoch_s: float
    cluster_peak_epoch_s: float
    cluster_score: float
    min_miss_km: float
    focus_satellite_norad: list[str]
    focus_debris_norad: list[str]


@dataclass
class GroundStation:
    station_id: str
    station_name: str
    lat_rad: float
    lon_rad: float
    alt_km: float
    min_el_rad: float


@dataclass
class FeedbackWeights:
    near_100_multiplier: float = 1.0
    near_250_multiplier: float = 1.0
    near_500_multiplier: float = 1.0
    los_multiplier: float = 1.0
    family_balance_multiplier: float = 1.0
    shell_balance_multiplier: float = 1.0
    diversity_bonus_multiplier: float = 1.0


def feedback_weights_dict(weights: FeedbackWeights) -> dict[str, float]:
    return {
        "near_100_multiplier": weights.near_100_multiplier,
        "near_250_multiplier": weights.near_250_multiplier,
        "near_500_multiplier": weights.near_500_multiplier,
        "los_multiplier": weights.los_multiplier,
        "family_balance_multiplier": weights.family_balance_multiplier,
        "shell_balance_multiplier": weights.shell_balance_multiplier,
        "diversity_bonus_multiplier": weights.diversity_bonus_multiplier,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Mine strict real-data replay manifests from local catalog data",
    )
    parser.add_argument("--data", default="data.txt", help="Path to a local OMM JSON or 3LE/TLE text file (default: data.txt)")
    parser.add_argument(
        "--output-dir",
        default="docs/scenarios/generated",
        help="Directory to write mined manifest JSON files (default: docs/scenarios/generated)",
    )
    parser.add_argument("--scenario-prefix", default="strict_mined", help="Prefix for emitted scenario IDs")
    parser.add_argument("--timestamp", default="", help="Optional target timestamp (ISO-8601 UTC)")
    parser.add_argument("--api-base", default=DEFAULT_API_BASE, help="Backend URL used for epoch negotiation")
    parser.add_argument("--groundstations", default="docs/groundstations.csv", help="Ground-station CSV for LOS-aware scoring (default: docs/groundstations.csv)")
    parser.add_argument("--top-scenarios", type=int, default=3, help="How many ranked manifests to emit (default: 3)")
    parser.add_argument("--candidate-scenarios", type=int, default=0, help="How many candidate manifests to emit before optional backend ranking (default: top-scenarios)")
    parser.add_argument("--operator-sats", type=int, default=8, help="Satellites per emitted scenario (default: 8)")
    parser.add_argument("--payload-candidates", type=int, default=12, help="Top payload anchors to score (default: 12)")
    parser.add_argument("--threat-candidates", type=int, default=600, help="Nearby threat records to evaluate per payload (default: 600)")
    parser.add_argument("--priority-debris", type=int, default=24, help="Priority debris NORADs to retain per manifest (default: 24)")
    parser.add_argument("--max-debris", type=int, default=DEFAULT_MAX_DEBRIS, help="Replay max_debris value to store in each manifest")
    parser.add_argument("--max-periapsis-km", type=float, default=DEFAULT_MAX_PERIAPSIS_KM, help="LEO periapsis filter in km (default: 2000)")
    parser.add_argument("--sample-seconds", type=int, default=900, help="Sampling interval across the horizon in seconds (default: 900)")
    parser.add_argument("--horizon-hours", type=float, default=24.0, help="Forward search horizon in hours (default: 24)")
    parser.add_argument("--same-shell-a-km", type=float, default=120.0, help="Semi-major-axis shell prefilter in km (default: 120)")
    parser.add_argument("--same-shell-i-deg", type=float, default=6.0, help="Inclination shell prefilter in deg (default: 6)")
    parser.add_argument("--anchor-shell-a-km", type=float, default=80.0, help="Shell width in km for anchor congestion probing (default: 80)")
    parser.add_argument("--anchor-shell-i-deg", type=float, default=4.0, help="Inclination width in deg for anchor congestion probing (default: 4)")
    parser.add_argument("--phase-window-deg", type=float, default=45.0, help="Mean-anomaly window in deg for close-track density probing (default: 45)")
    parser.add_argument("--raan-window-deg", type=float, default=15.0, help="RAAN window in deg for close-track density probing (default: 15)")
    parser.add_argument("--diversity-a-km", type=float, default=40.0, help="Minimum semi-major-axis separation for diverse operator picks when possible (default: 40)")
    parser.add_argument("--diversity-i-deg", type=float, default=2.0, help="Minimum inclination separation for diverse operator picks when possible (default: 2)")
    parser.add_argument("--backend-cdm-cmd", default="", help="Optional backend command for live CDM feedback during mining")
    parser.add_argument("--backend-cdm-api-base", default=DEFAULT_API_BASE, help="Backend base URL for live CDM feedback (default: http://localhost:8000)")
    parser.add_argument("--backend-cdm-startup-timeout", type=float, default=20.0, help="Startup timeout in seconds for backend CDM feedback (default: 20)")
    parser.add_argument("--feedback-file", default="", help="Optional JSON file carrying learned miner feedback weights")
    parser.add_argument("--feedback-output", default="", help="Optional JSON file to write updated miner feedback weights")
    parser.add_argument("--satellite-mode", choices=("synthetic", "catalog"), default="catalog", help="Manifest satellite mode (default: catalog)")
    parser.add_argument("--warmup-steps", type=int, default=DEFAULT_WARMUP_STEPS, help="Warmup steps stored in manifest (default: 3)")
    parser.add_argument("--warmup-step-seconds", type=int, default=DEFAULT_WARMUP_STEP_SECONDS, help="Warmup step seconds stored in manifest (default: 60)")
    parser.add_argument("--encounter-cluster-hours", type=float, default=3.0, help="Time bucket used to cluster close opportunities into a replay window (default: 3)")
    parser.add_argument("--encounter-lead-seconds", type=int, default=7200, help="How far ahead of the chosen encounter cluster the replay should start (default: 7200)")
    parser.add_argument("--backend-precheck-max-debris", type=int, default=1500, help="Reduced debris cap used for miner backend precheck manifests (default: 1500)")
    parser.add_argument("--backend-rank-cmd", default="", help="Optional backend command for live ranking of emitted manifests")
    parser.add_argument("--backend-rank-api-base", default=DEFAULT_API_BASE, help="Backend base URL for live ranking (default: http://localhost:8000)")
    parser.add_argument("--backend-rank-extra-steps", type=int, default=1, help="Extra steps for live manifest ranking (default: 1)")
    parser.add_argument("--backend-rank-extra-step-seconds", type=int, default=300, help="step_seconds for extra live ranking steps (default: 300)")
    parser.add_argument("--backend-rank-startup-timeout", type=float, default=20.0, help="Startup timeout in seconds for live ranking backend runs (default: 20)")
    return parser.parse_args()


def same_shell_metric(anchor: OmmRecord, other: OmmRecord, same_shell_a_km: float, same_shell_i_deg: float) -> float:
    da = abs(anchor.a_km - other.a_km) / max(1.0, same_shell_a_km)
    di = abs(math.degrees(anchor.i_rad - other.i_rad)) / max(0.1, same_shell_i_deg)
    de = abs(anchor.e - other.e) / 0.01
    return da + di + de


def angle_delta_deg(a_rad: float, b_rad: float) -> float:
    delta = math.degrees(a_rad - b_rad)
    delta = (delta + 180.0) % 360.0 - 180.0
    return abs(delta)


def shell_bucket_key(record: OmmRecord, shell_a_km: float, shell_i_deg: float) -> tuple[int, int]:
    return (
        int(record.a_km // max(1.0, shell_a_km)),
        int(math.degrees(record.i_rad) // max(0.1, shell_i_deg)),
    )


def build_shell_buckets(records: list[OmmRecord], shell_a_km: float, shell_i_deg: float) -> dict[tuple[int, int], list[OmmRecord]]:
    buckets: dict[tuple[int, int], list[OmmRecord]] = {}
    for record in records:
        buckets.setdefault(shell_bucket_key(record, shell_a_km, shell_i_deg), []).append(record)
    return buckets


def anchor_density(record: OmmRecord,
                   buckets: dict[tuple[int, int], list[OmmRecord]],
                   shell_a_km: float,
                   shell_i_deg: float,
                   phase_window_deg: float,
                   raan_window_deg: float) -> tuple[int, int]:
    shell_count = 0
    phase_count = 0
    a_bin, i_bin = shell_bucket_key(record, shell_a_km, shell_i_deg)
    for da_bin in (-1, 0, 1):
        for di_bin in (-1, 0, 1):
            for other in buckets.get((a_bin + da_bin, i_bin + di_bin), []):
                if other.norad_id == record.norad_id:
                    continue
                if abs(record.a_km - other.a_km) > shell_a_km:
                    continue
                if abs(math.degrees(record.i_rad - other.i_rad)) > shell_i_deg:
                    continue
                shell_count += 1
                if angle_delta_deg(record.M_rad, other.M_rad) <= phase_window_deg and angle_delta_deg(record.raan_rad, other.raan_rad) <= raan_window_deg:
                    phase_count += 1
    return shell_count, phase_count


def anchor_prefilter_score(shell_density_count: int, phase_density_count: int) -> float:
    return phase_density_count * 18.0 + shell_density_count * 0.9


def prefilter_payload_anchors(payloads: list[OmmRecord],
                              buckets: dict[tuple[int, int], list[OmmRecord]],
                              shell_a_km: float,
                              shell_i_deg: float,
                              phase_window_deg: float,
                              raan_window_deg: float) -> list[AnchorPrefilterSummary]:
    prefiltered: list[AnchorPrefilterSummary] = []
    for payload in payloads:
        shell_density_count, phase_density_count = anchor_density(
            payload,
            buckets,
            shell_a_km,
            shell_i_deg,
            phase_window_deg,
            raan_window_deg,
        )
        prefiltered.append(
            AnchorPrefilterSummary(
                payload=payload,
                pre_score=anchor_prefilter_score(shell_density_count, phase_density_count),
                shell_density_count=shell_density_count,
                phase_density_count=phase_density_count,
                family_tag=family_tag(payload),
                shell_tag=shell_tag(payload),
            )
        )
    prefiltered.sort(
        key=lambda item: (
            -item.pre_score,
            -item.phase_density_count,
            -item.shell_density_count,
            item.payload.norad_id,
        )
    )
    return prefiltered


def select_anchor_candidates(prefiltered: list[AnchorPrefilterSummary],
                             target_count: int,
                             feedback_weights: FeedbackWeights) -> list[AnchorPrefilterSummary]:
    if not prefiltered:
        return []

    selected: list[AnchorPrefilterSummary] = []
    seen: set[str] = set()
    target = max(1, target_count)

    def append_candidate(candidate: AnchorPrefilterSummary) -> None:
        if candidate.payload.norad_id in seen or len(selected) >= target:
            return
        selected.append(candidate)
        seen.add(candidate.payload.norad_id)

    unique_shell_reps: list[AnchorPrefilterSummary] = []
    seen_shells: set[str] = set()
    for candidate in prefiltered:
        if candidate.shell_tag in seen_shells:
            continue
        unique_shell_reps.append(candidate)
        seen_shells.add(candidate.shell_tag)

    seed_target = min(target, max(4, target // 2))
    for candidate in unique_shell_reps:
        append_candidate(candidate)
        if len(selected) >= seed_target:
            break

    if len(selected) < seed_target:
        seen_families = {candidate.family_tag for candidate in selected}
        for candidate in prefiltered:
            if candidate.family_tag in seen_families:
                continue
            append_candidate(candidate)
            seen_families.add(candidate.family_tag)
            if len(selected) >= seed_target:
                break

    while len(selected) < target:
        best_candidate: AnchorPrefilterSummary | None = None
        best_score = -float("inf")
        family_counts = {item.family_tag: sum(1 for chosen in selected if chosen.family_tag == item.family_tag) for item in selected}
        shell_counts = {item.shell_tag: sum(1 for chosen in selected if chosen.shell_tag == item.shell_tag) for item in selected}

        for candidate in prefiltered:
            norad_id = candidate.payload.norad_id
            if norad_id in seen:
                continue
            family_penalty = family_counts.get(candidate.family_tag, 0) * 18.0 * feedback_weights.family_balance_multiplier
            shell_penalty = shell_counts.get(candidate.shell_tag, 0) * 14.0 * feedback_weights.shell_balance_multiplier
            diversity_bonus = 0.0
            if family_counts.get(candidate.family_tag, 0) == 0:
                diversity_bonus += 16.0 * feedback_weights.diversity_bonus_multiplier
            if shell_counts.get(candidate.shell_tag, 0) == 0:
                diversity_bonus += 16.0 * feedback_weights.diversity_bonus_multiplier
            score = candidate.pre_score + diversity_bonus - family_penalty - shell_penalty
            if score > best_score:
                best_score = score
                best_candidate = candidate

        if best_candidate is None:
            break

        selected.append(best_candidate)
        seen.add(best_candidate.payload.norad_id)

    return selected


def family_tag(record: OmmRecord) -> str:
    name = record.object_name.upper()
    for label in ("STARLINK", "ONEWEB", "HULIANWANG", "COSMOS", "LEMUR", "IRIDIUM", "ORBCOMM"):
        if label in name:
            return label
    if record.object_type == "DEBRIS":
        return "DEBRIS"
    if record.object_type == "ROCKET BODY":
        return "ROCKET_BODY"
    return "OTHER"


def shell_tag(record: OmmRecord) -> str:
    a_bucket = int(round(record.a_km / 25.0) * 25)
    i_bucket = int(round(math.degrees(record.i_rad) / 2.0) * 2)
    return f"a{a_bucket}_i{i_bucket}"


def load_ground_stations(path: pathlib.Path) -> list[GroundStation]:
    if not path.exists():
        return []

    stations: list[GroundStation] = []
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            try:
                stations.append(
                    GroundStation(
                        station_id=str(row["Station_ID"]).strip(),
                        station_name=str(row["Station_Name"]).strip(),
                        lat_rad=math.radians(float(row["Latitude"])),
                        lon_rad=math.radians(float(row["Longitude"])),
                        alt_km=float(row["Elevation_m"]) / 1000.0,
                        min_el_rad=math.radians(float(row["Min_Elevation_Angle_deg"])),
                    )
                )
            except (KeyError, ValueError):
                continue
    return stations


def load_feedback_weights(path_value: str) -> FeedbackWeights:
    if not path_value:
        return FeedbackWeights()

    path = pathlib.Path(path_value).expanduser().resolve()
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return FeedbackWeights()
    except json.JSONDecodeError:
        return FeedbackWeights()

    if not isinstance(raw, dict):
        return FeedbackWeights()

    return FeedbackWeights(
        near_100_multiplier=float(raw.get("near_100_multiplier", 1.0) or 1.0),
        near_250_multiplier=float(raw.get("near_250_multiplier", 1.0) or 1.0),
        near_500_multiplier=float(raw.get("near_500_multiplier", 1.0) or 1.0),
        los_multiplier=float(raw.get("los_multiplier", 1.0) or 1.0),
        family_balance_multiplier=float(raw.get("family_balance_multiplier", 1.0) or 1.0),
        shell_balance_multiplier=float(raw.get("shell_balance_multiplier", 1.0) or 1.0),
        diversity_bonus_multiplier=float(raw.get("diversity_bonus_multiplier", 1.0) or 1.0),
    )


def write_feedback_weights(path_value: str, weights: FeedbackWeights) -> None:
    if not path_value:
        return
    path = pathlib.Path(path_value).expanduser().resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(feedback_weights_dict(weights), indent=2) + "\n", encoding="utf-8")


def gmst_rad(unix_epoch_s: float) -> float:
    jd = unix_epoch_s / 86400.0 + 2440587.5
    d = jd - 2451545.0
    t = d / 36525.0
    gmst_deg = 280.46061837 + 360.98564736629 * d + 0.000387933 * t * t - (t * t * t) / 38710000.0
    gmst_deg = math.fmod(gmst_deg, 360.0)
    if gmst_deg < 0.0:
        gmst_deg += 360.0
    return math.radians(gmst_deg)


def eci_to_ecef(eci_km: tuple[float, float, float], unix_epoch_s: float) -> tuple[float, float, float]:
    angle = gmst_rad(unix_epoch_s)
    c = math.cos(angle)
    s = math.sin(angle)
    return (
        c * eci_km[0] + s * eci_km[1],
        -s * eci_km[0] + c * eci_km[1],
        eci_km[2],
    )


def geodetic_to_ecef(lat_rad: float, lon_rad: float, alt_km: float) -> tuple[float, float, float]:
    a = 6378.137
    f = 1.0 / 298.257223563
    e2 = f * (2.0 - f)
    s_lat = math.sin(lat_rad)
    c_lat = math.cos(lat_rad)
    s_lon = math.sin(lon_rad)
    c_lon = math.cos(lon_rad)
    n = a / math.sqrt(1.0 - e2 * s_lat * s_lat)
    return (
        (n + alt_km) * c_lat * c_lon,
        (n + alt_km) * c_lat * s_lon,
        (n * (1.0 - e2) + alt_km) * s_lat,
    )


def elevation_angle_rad(sat_ecef_km: tuple[float, float, float], station: GroundStation) -> float:
    gs = geodetic_to_ecef(station.lat_rad, station.lon_rad, station.alt_km)
    rel_x = sat_ecef_km[0] - gs[0]
    rel_y = sat_ecef_km[1] - gs[1]
    rel_z = sat_ecef_km[2] - gs[2]
    s_lat = math.sin(station.lat_rad)
    c_lat = math.cos(station.lat_rad)
    s_lon = math.sin(station.lon_rad)
    c_lon = math.cos(station.lon_rad)
    east = -s_lon * rel_x + c_lon * rel_y
    north = -s_lat * c_lon * rel_x - s_lat * s_lon * rel_y + c_lat * rel_z
    up = c_lat * c_lon * rel_x + c_lat * s_lon * rel_y + s_lat * rel_z
    horiz = math.sqrt(east * east + north * north)
    return math.atan2(up, horiz)


def los_summary(series: list[tuple[float, float, float]],
                epochs: list[float],
                stations: list[GroundStation]) -> tuple[int, list[str], str | None, float]:
    if not stations:
        return 0, [], None, 0.0

    los_sample_count = 0
    station_ids: set[str] = set()
    best_station: str | None = None
    best_el_rad = -math.pi / 2.0

    for epoch_s, sat_eci in zip(epochs, series):
        sat_ecef = eci_to_ecef(sat_eci, epoch_s)
        sample_visible = False
        for station in stations:
            el = elevation_angle_rad(sat_ecef, station)
            if el >= station.min_el_rad:
                sample_visible = True
                station_ids.add(station.station_id)
                if el > best_el_rad:
                    best_el_rad = el
                    best_station = station.station_id
        if sample_visible:
            los_sample_count += 1

    best_elevation_deg = math.degrees(best_el_rad) if best_station is not None else 0.0
    return los_sample_count, sorted(station_ids), best_station, best_elevation_deg


def choose_target_epoch(args: argparse.Namespace, first_epoch_s: float) -> tuple[float, str]:
    class EpochArgs:
        timestamp = args.timestamp
        api_base = args.api_base

    return replay_choose_target_epoch(EpochArgs, first_epoch_s)


def candidate_threat_pool(anchor: OmmRecord,
                          records: list[OmmRecord],
                          limit: int,
                          same_shell_a_km: float,
                          same_shell_i_deg: float) -> list[OmmRecord]:
    scored: list[tuple[float, OmmRecord]] = []
    for record in records:
        if record.norad_id == anchor.norad_id:
            continue
        metric = same_shell_metric(anchor, record, same_shell_a_km, same_shell_i_deg)
        if metric > 6.0:
            continue
        scored.append((metric, record))
    scored.sort(key=lambda item: (item[0], item[1].norad_id))
    if limit > 0:
        scored = scored[:limit]
    return [record for _, record in scored]


def sampled_epochs(target_epoch_s: float, horizon_hours: float, sample_seconds: int) -> list[float]:
    horizon_s = max(sample_seconds, int(round(horizon_hours * 3600.0)))
    step = max(60, sample_seconds)
    epochs = list(range(0, horizon_s + 1, step))
    return [target_epoch_s + float(offset) for offset in epochs]


def position_series(record: OmmRecord,
                    epochs: list[float],
                    cache: dict[str, list[tuple[float, float, float]]]) -> list[tuple[float, float, float]]:
    cached = cache.get(record.norad_id)
    if cached is not None:
        return cached
    series = [propagate_record(record, epoch)[0] for epoch in epochs]
    cache[record.norad_id] = series
    return series


def point_distance_km(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    dz = a[2] - b[2]
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def min_distance_event(anchor_series: list[tuple[float, float, float]],
                       threat_series: list[tuple[float, float, float]],
                       epochs: list[float]) -> tuple[float, float]:
    best = float("inf")
    best_epoch_s = epochs[0] if epochs else 0.0
    for epoch_s, sat_pos, threat_pos in zip(epochs, anchor_series, threat_series):
        dist = point_distance_km(sat_pos, threat_pos)
        if dist < best:
            best = dist
            best_epoch_s = epoch_s
    return best, best_epoch_s


def close_approach_bonus(miss_km: float) -> float:
    if not math.isfinite(miss_km):
        return 0.0

    bonus = max(0.0, 1000.0 - miss_km) / 10.0
    if miss_km < 100.0:
        bonus += (100.0 - miss_km) * 3.0
    if miss_km < 20.0:
        bonus += 400.0 + (20.0 - miss_km) * 35.0
    if miss_km < 5.0:
        bonus += 600.0 + (5.0 - miss_km) * 180.0
    if miss_km < 1.0:
        bonus += 1200.0 + (1.0 - miss_km) * 500.0
    return bonus


def refine_min_distance_event(anchor: OmmRecord,
                              threat: OmmRecord,
                              coarse_epoch_s: float,
                              horizon_start_s: float,
                              horizon_end_s: float,
                              coarse_step_s: int,
                              coarse_miss_km: float | None = None) -> tuple[float, float]:
    best_epoch_s = coarse_epoch_s
    best_miss_km = coarse_miss_km if coarse_miss_km is not None and math.isfinite(coarse_miss_km) else float("inf")
    refinement_plan: list[tuple[float, float]] = [
        (max(10.0, float(coarse_step_s) / 12.0), max(120.0, float(coarse_step_s))),
        (1.0, 30.0),
    ]

    if best_miss_km <= 10.0:
        refinement_plan.append((0.2, 5.0))
    if best_miss_km <= 2.0:
        refinement_plan.append((0.05, 1.0))

    for step_s, half_window_s in refinement_plan:
        start_epoch_s = max(horizon_start_s, best_epoch_s - half_window_s)
        end_epoch_s = min(horizon_end_s, best_epoch_s + half_window_s)
        epoch_s = start_epoch_s
        while epoch_s <= end_epoch_s + 1.0e-6:
            sat_pos = propagate_record(anchor, epoch_s)[0]
            threat_pos = propagate_record(threat, epoch_s)[0]
            miss_km = point_distance_km(sat_pos, threat_pos)
            if miss_km < best_miss_km:
                best_miss_km = miss_km
                best_epoch_s = epoch_s
            epoch_s += float(step_s)

    return best_miss_km, best_epoch_s


def score_payload(anchor: OmmRecord,
                  records: list[OmmRecord],
                  epochs: list[float],
                  cache: dict[str, list[tuple[float, float, float]]],
                  stations: list[GroundStation],
                  feedback_weights: FeedbackWeights,
                  shell_density_count: int,
                  phase_density_count: int,
                  sample_seconds: int,
                  threat_limit: int,
                  same_shell_a_km: float,
                  same_shell_i_deg: float) -> PayloadThreatSummary:
    pool = candidate_threat_pool(anchor, records, threat_limit, same_shell_a_km, same_shell_i_deg)
    anchor_series = position_series(anchor, epochs, cache)
    los_sample_count, los_station_ids, best_station_id, best_elevation_deg = los_summary(anchor_series, epochs, stations)

    top_debris: list[ThreatOpportunity] = []
    critical_count = 0
    warning_count = 0
    close_count = 0
    near_100_count = 0
    near_250_count = 0
    near_500_count = 0
    best_miss = float("inf")
    best_miss_epoch_s = epochs[0] if epochs else 0.0
    horizon_start_s = epochs[0] if epochs else 0.0
    horizon_end_s = epochs[-1] if epochs else 0.0

    for record in pool:
        threat_series = position_series(record, epochs, cache)
        miss_km, tca_epoch_s = min_distance_event(anchor_series, threat_series, epochs)
        if miss_km <= 250.0 and horizon_end_s > horizon_start_s:
            miss_km, tca_epoch_s = refine_min_distance_event(
                anchor,
                record,
                tca_epoch_s,
                horizon_start_s,
                horizon_end_s,
                sample_seconds,
                miss_km,
            )
        if miss_km < best_miss:
            best_miss = miss_km
            best_miss_epoch_s = tca_epoch_s
        if miss_km < 1.0:
            critical_count += 1
        if miss_km < 5.0:
            warning_count += 1
        if miss_km < 20.0:
            close_count += 1
        if miss_km < 100.0:
            near_100_count += 1
        if miss_km < 250.0:
            near_250_count += 1
        if miss_km < 500.0:
            near_500_count += 1
        top_debris.append(
            ThreatOpportunity(
                norad_id=record.norad_id,
                min_miss_km=miss_km,
                tca_epoch_s=tca_epoch_s,
            )
        )

    top_debris.sort(key=lambda item: (item.min_miss_km, item.tca_epoch_s, item.norad_id))
    top_debris = top_debris[:16]

    top3 = [item.min_miss_km for item in top_debris[:3]]
    avg_top3_miss_km = sum(top3) / len(top3) if top3 else float("inf")

    closeness_bonus = close_approach_bonus(best_miss)
    richness_bonus = 0.0
    richness_bonus += near_100_count * 55.0 * feedback_weights.near_100_multiplier
    richness_bonus += near_250_count * 14.0 * feedback_weights.near_250_multiplier
    richness_bonus += near_500_count * 4.0 * feedback_weights.near_500_multiplier
    if math.isfinite(avg_top3_miss_km):
        richness_bonus += close_approach_bonus(avg_top3_miss_km) * 0.45
    los_bonus = (los_sample_count * 2.0 + len(los_station_ids) * 5.0 + min(best_elevation_deg, 45.0) / 5.0) * feedback_weights.los_multiplier
    density_bonus = shell_density_count * 1.2 + phase_density_count * 6.0
    score = critical_count * 3000.0 + warning_count * 450.0 + close_count * 30.0 + closeness_bonus + richness_bonus + los_bonus + density_bonus

    return PayloadThreatSummary(
        payload=anchor,
        score=score,
        min_miss_km=best_miss,
        best_miss_epoch_s=best_miss_epoch_s,
        avg_top3_miss_km=avg_top3_miss_km,
        critical_count=critical_count,
        warning_count=warning_count,
        close_count=close_count,
        near_100_count=near_100_count,
        near_250_count=near_250_count,
        near_500_count=near_500_count,
        los_sample_count=los_sample_count,
        los_station_ids=los_station_ids,
        best_station_id=best_station_id,
        best_elevation_deg=best_elevation_deg,
        family_tag=family_tag(anchor),
        shell_tag=shell_tag(anchor),
        shell_density_count=shell_density_count,
        phase_density_count=phase_density_count,
        top_debris=top_debris,
    )


def unique_priority_debris(summaries: list[PayloadThreatSummary],
                           limit: int,
                           preferred: list[str] | None = None) -> list[str]:
    ordered: list[ThreatOpportunity] = []
    seen: set[str] = set()
    result: list[str] = []

    for norad_id in preferred or []:
        if norad_id in seen:
            continue
        seen.add(norad_id)
        result.append(norad_id)

    for summary in summaries:
        for opportunity in summary.top_debris:
            if opportunity.norad_id in seen:
                continue
            seen.add(opportunity.norad_id)
            ordered.append(opportunity)
    ordered.sort(key=lambda item: (item.min_miss_km, item.tca_epoch_s, item.norad_id))
    for item in ordered:
        if len(result) >= max(0, limit):
            break
        result.append(item.norad_id)
    return result[: max(0, limit)]


def diverse_from_selected(candidate: PayloadThreatSummary,
                          selected: list[PayloadThreatSummary],
                          diversity_a_km: float,
                          diversity_i_deg: float) -> bool:
    for existing in selected:
        if abs(candidate.payload.a_km - existing.payload.a_km) < diversity_a_km and \
           abs(math.degrees(candidate.payload.i_rad - existing.payload.i_rad)) < diversity_i_deg:
            return False
    return True


def select_diverse_payloads(ranked: list[PayloadThreatSummary],
                            start_index: int,
                            operator_sats: int,
                            feedback_weights: FeedbackWeights,
                            diversity_a_km: float,
                            diversity_i_deg: float) -> list[PayloadThreatSummary]:
    if not ranked:
        return []

    target = max(1, operator_sats)
    anchor = ranked[start_index % len(ranked)]
    selected = [anchor]
    seen = {anchor.payload.norad_id}
    ordered_candidates = ranked[start_index + 1:] + ranked[:start_index]

    while len(selected) < target:
        best_candidate: PayloadThreatSummary | None = None
        best_adjusted_score = -float("inf")
        family_counts = {summary.family_tag: sum(1 for item in selected if item.family_tag == summary.family_tag) for summary in selected}
        shell_counts = {summary.shell_tag: sum(1 for item in selected if item.shell_tag == summary.shell_tag) for summary in selected}

        for candidate in ordered_candidates:
            if candidate.payload.norad_id in seen:
                continue
            diverse_ok = diverse_from_selected(candidate, selected, diversity_a_km, diversity_i_deg)
            family_penalty = family_counts.get(candidate.family_tag, 0) * 18.0 * feedback_weights.family_balance_multiplier
            shell_penalty = shell_counts.get(candidate.shell_tag, 0) * 14.0 * feedback_weights.shell_balance_multiplier
            diversity_bonus = (20.0 if diverse_ok else 0.0) * feedback_weights.diversity_bonus_multiplier
            richness_bonus = candidate.near_100_count * 40.0 + candidate.near_250_count * 8.0 + candidate.near_500_count * 2.0
            richness_bonus += candidate.phase_density_count * 5.0 + candidate.shell_density_count * 0.8
            adjusted_score = candidate.score + diversity_bonus + richness_bonus - family_penalty - shell_penalty
            if adjusted_score > best_adjusted_score:
                best_adjusted_score = adjusted_score
                best_candidate = candidate

        if best_candidate is None:
            break

        selected.append(best_candidate)
        seen.add(best_candidate.payload.norad_id)

    return selected


def encounter_windows(summaries: list[PayloadThreatSummary],
                      cluster_hours: float,
                      lead_seconds: int,
                      first_epoch_s: float) -> list[EncounterWindow]:
    bucket_s = max(1800, int(round(cluster_hours * 3600.0)))
    buckets: dict[int, list[tuple[PayloadThreatSummary, ThreatOpportunity]]] = {}

    for summary in summaries:
        for opportunity in summary.top_debris[:8]:
            if not math.isfinite(opportunity.min_miss_km):
                continue
            bucket = int(opportunity.tca_epoch_s // bucket_s)
            buckets.setdefault(bucket, []).append((summary, opportunity))

    windows: list[EncounterWindow] = []
    for bucket, items in buckets.items():
        start_epoch_s = bucket * bucket_s
        end_epoch_s = start_epoch_s + bucket_s
        cluster_peak_epoch_s = min(items, key=lambda item: (item[1].min_miss_km, item[1].tca_epoch_s))[1].tca_epoch_s
        cluster_min_miss = min(item[1].min_miss_km for item in items)
        focus_satellite_norad = sorted({item[0].payload.norad_id for item in items})
        focus_debris_norad = [item[1].norad_id for item in sorted(items, key=lambda item: (item[1].min_miss_km, item[1].tca_epoch_s, item[1].norad_id))]
        focus_debris_norad = list(dict.fromkeys(focus_debris_norad))[:24]

        cluster_score = 0.0
        for summary, opportunity in items:
            cluster_score += close_approach_bonus(opportunity.min_miss_km)
            cluster_score += summary.phase_density_count * 2.0 + summary.shell_density_count * 0.15

        target_epoch_s = max(first_epoch_s, cluster_peak_epoch_s - max(0, lead_seconds))
        windows.append(
            EncounterWindow(
                target_epoch_s=target_epoch_s,
                cluster_start_epoch_s=start_epoch_s,
                cluster_end_epoch_s=end_epoch_s,
                cluster_peak_epoch_s=cluster_peak_epoch_s,
                cluster_score=cluster_score,
                min_miss_km=cluster_min_miss,
                focus_satellite_norad=focus_satellite_norad,
                focus_debris_norad=focus_debris_norad,
            )
        )

    windows.sort(
        key=lambda item: (
            -item.cluster_score,
            item.min_miss_km,
            -len(item.focus_satellite_norad),
            item.target_epoch_s,
        )
    )
    return windows


def choose_encounter_window(windows: list[EncounterWindow], chosen_norad_set: set[str]) -> EncounterWindow | None:
    candidates: list[tuple[float, int, float, float, EncounterWindow]] = []
    for window in windows:
        overlap = sum(1 for norad_id in window.focus_satellite_norad if norad_id in chosen_norad_set)
        if overlap <= 0:
            continue
        candidates.append((window.min_miss_km, -overlap, -window.cluster_score, window.target_epoch_s, window))
    if not candidates:
        return None
    candidates.sort(key=lambda item: (item[0], item[1], item[2], item[3]))
    return candidates[0][4]


def scenario_manifest(args: argparse.Namespace,
                      default_target_epoch_s: float,
                      ranked: list[PayloadThreatSummary],
                      feedback_weights: FeedbackWeights,
                      start_index: int,
                      windows: list[EncounterWindow]) -> dict[str, Any]:
    chosen = select_diverse_payloads(
        ranked,
        start_index,
        args.operator_sats,
        feedback_weights,
        args.diversity_a_km,
        args.diversity_i_deg,
    )

    operator_norads = [summary.payload.norad_id for summary in chosen]
    recommended_ground_stations = sorted({station_id for summary in chosen for station_id in summary.los_station_ids})
    chosen_norad_set = set(operator_norads)
    encounter_window = choose_encounter_window(windows, chosen_norad_set)
    target_epoch_s = encounter_window.target_epoch_s if encounter_window else default_target_epoch_s
    target_timestamp = iso8601_utc(target_epoch_s)
    preferred_priority = encounter_window.focus_debris_norad if encounter_window else []
    priority_debris = unique_priority_debris(chosen, args.priority_debris, preferred_priority)
    watch_satellites = [
        (f"SAT-{norad_id}" if args.satellite_mode == "catalog" else f"SAT-LOCAL-{idx:02d}")
        for idx, norad_id in enumerate(operator_norads[: min(3, len(operator_norads))], start=1)
    ]
    if encounter_window:
        focus_watch = [norad_id for norad_id in encounter_window.focus_satellite_norad if norad_id in chosen_norad_set]
        if focus_watch:
            watch_satellites = [
                (f"SAT-{norad_id}" if args.satellite_mode == "catalog" else f"SAT-LOCAL-{operator_norads.index(norad_id) + 1:02d}")
                for norad_id in focus_watch[: min(3, len(focus_watch))]
            ]

    return {
        "scenario_id": f"{args.scenario_prefix}_{start_index + 1:02d}",
        "description": "Mined strict real-data replay preset ranked by natural nearby traffic over the configured horizon.",
        "catalog_sources": [pathlib.Path(args.data).name],
        "target_epoch": target_timestamp,
        "operator_selection": {
            "mode": args.satellite_mode,
            "count": len(operator_norads),
            "norad_ids": operator_norads,
        },
        "filters": {
            "max_debris": args.max_debris,
            "max_periapsis_km": args.max_periapsis_km,
        },
        "warmup": {
            "steps": args.warmup_steps,
            "step_seconds": args.warmup_step_seconds,
        },
        "replay": {
            "data": args.data,
            "api_base": args.api_base,
            "satellite_mode": args.satellite_mode,
            "operator_sats": len(operator_norads),
            "priority_debris_norad": priority_debris,
        },
        "watch_satellites": watch_satellites,
        "mining": {
            "horizon_hours": args.horizon_hours,
            "sample_seconds": args.sample_seconds,
            "payload_candidates": args.payload_candidates,
            "threat_candidates": args.threat_candidates,
            "same_shell_a_km": args.same_shell_a_km,
            "same_shell_i_deg": args.same_shell_i_deg,
            "anchor_shell_a_km": args.anchor_shell_a_km,
            "anchor_shell_i_deg": args.anchor_shell_i_deg,
            "phase_window_deg": args.phase_window_deg,
            "raan_window_deg": args.raan_window_deg,
            "diversity_a_km": args.diversity_a_km,
            "diversity_i_deg": args.diversity_i_deg,
            "feedback_weights": feedback_weights_dict(feedback_weights),
            "priority_debris_norad": priority_debris,
            "groundstations": args.groundstations,
            "recommended_ground_stations": recommended_ground_stations,
            "encounter_cluster_hours": args.encounter_cluster_hours,
            "encounter_lead_seconds": args.encounter_lead_seconds,
            "anchor_norad_id": chosen[0].payload.norad_id if chosen else None,
            "anchor_rank_index": start_index,
            "encounter_window": None if encounter_window is None else {
                "target_epoch": iso8601_utc(encounter_window.target_epoch_s),
                "cluster_start_epoch": iso8601_utc(encounter_window.cluster_start_epoch_s),
                "cluster_end_epoch": iso8601_utc(encounter_window.cluster_end_epoch_s),
                "cluster_peak_epoch": iso8601_utc(encounter_window.cluster_peak_epoch_s),
                "cluster_score": round(encounter_window.cluster_score, 3),
                "min_miss_km": round(encounter_window.min_miss_km, 6),
                "focus_satellite_norad": encounter_window.focus_satellite_norad,
                "focus_debris_norad": encounter_window.focus_debris_norad,
            },
            "selected_payload_summaries": [
                {
                    "norad_id": summary.payload.norad_id,
                    "object_name": summary.payload.object_name,
                    "score": round(summary.score, 3),
                    "min_miss_km": None if not math.isfinite(summary.min_miss_km) else round(summary.min_miss_km, 6),
                    "best_miss_epoch": iso8601_utc(summary.best_miss_epoch_s),
                    "avg_top3_miss_km": None if not math.isfinite(summary.avg_top3_miss_km) else round(summary.avg_top3_miss_km, 6),
                    "critical_count": summary.critical_count,
                    "warning_count": summary.warning_count,
                    "close_count": summary.close_count,
                    "near_100_count": summary.near_100_count,
                    "near_250_count": summary.near_250_count,
                    "near_500_count": summary.near_500_count,
                    "los_sample_count": summary.los_sample_count,
                    "los_station_ids": summary.los_station_ids,
                    "best_station_id": summary.best_station_id,
                    "best_elevation_deg": round(summary.best_elevation_deg, 3),
                    "family_tag": summary.family_tag,
                    "shell_tag": summary.shell_tag,
                    "shell_density_count": summary.shell_density_count,
                    "phase_density_count": summary.phase_density_count,
                    "top_debris": [
                        {
                            "norad_id": item.norad_id,
                            "min_miss_km": round(item.min_miss_km, 6),
                            "tca_epoch": iso8601_utc(item.tca_epoch_s),
                        }
                        for item in summary.top_debris[:8]
                    ],
                }
                for summary in chosen
            ],
        },
        "notes": [
            "This manifest is mined from real catalog geometry; no synthetic orbit generation is used.",
            "Priority debris NORADs are moved to the front of replay ingestion so the replay cap does not hide the nearest natural threats.",
            "Replay start time is biased toward the strongest mined encounter cluster so the backend sees close natural traffic earlier in the run.",
        ],
    }


def backend_cdm_signal(summary: dict[str, Any]) -> float:
    predicted = float(summary.get("predicted_conjunction_count", 0) or 0)
    active = float(summary.get("active_cdm_warnings", 0) or 0)
    pending = float(summary.get("burns_pending", 0) or 0)
    dropped = float(summary.get("burns_dropped", 0) or 0)
    history = float(summary.get("history_conjunction_count", 0) or 0)
    collisions_avoided = float(summary.get("collisions_avoided", 0) or 0)
    avoidance_fuel = float(summary.get("avoidance_fuel_consumed_kg", 0.0) or 0.0)
    predicted_critical = float(summary.get("predicted_critical_count", 0) or 0)
    predicted_near_1km = float(summary.get("predicted_near_1km_count", 0) or 0)
    predicted_near_5km = float(summary.get("predicted_near_5km_count", 0) or 0)
    predicted_watch_hits = float(summary.get("predicted_watch_satellite_hits", 0) or 0)
    predicted_fail_open = float(summary.get("predicted_fail_open_count", 0) or 0)
    predictive_alignment_gap = summary.get("predictive_alignment_gap_km")
    heuristic_predictive_gap = summary.get("heuristic_predictive_gap_km")
    signal = predicted * 150.0 + active * 110.0 + pending * 60.0 + history * 15.0
    signal -= dropped * 30.0
    signal += collisions_avoided * 150.0
    signal -= min(avoidance_fuel, 250.0) * 0.3
    signal += predicted_critical * 220.0
    signal += predicted_near_1km * 90.0
    signal += predicted_near_5km * 30.0
    signal += predicted_watch_hits * 25.0
    signal += predicted_fail_open * 35.0
    if predictive_alignment_gap is not None:
        signal += max(0.0, 20.0 - float(predictive_alignment_gap)) * 6.0
    elif heuristic_predictive_gap is not None:
        signal += max(0.0, 20.0 - float(heuristic_predictive_gap)) * 4.0
    return signal


def clamp_weight(value: float) -> float:
    return max(0.5, min(3.0, value))


def build_backend_precheck_manifest(manifest: dict[str, Any], precheck_max_debris: int) -> dict[str, Any]:
    clone = json.loads(json.dumps(manifest))
    filters = clone.setdefault("filters", {})
    replay = clone.setdefault("replay", {})
    current_max_debris = int(filters.get("max_debris", replay.get("max_debris", precheck_max_debris)) or precheck_max_debris)
    precheck_limit = min(current_max_debris, max(64, precheck_max_debris))
    filters["max_debris"] = precheck_limit
    replay["max_debris"] = precheck_limit
    clone["scenario_id"] = f"{clone.get('scenario_id', 'precheck')}_precheck"
    notes = clone.get("notes")
    if isinstance(notes, list):
        notes.append("This temporary manifest is used only for miner backend precheck with a reduced debris cap.")
    return clone


def run_backend_precheck(manifest: dict[str, Any],
                         backend_cmd: str,
                         api_base: str,
                         startup_timeout: float,
                         extra_steps: int,
                         extra_step_seconds: int,
                         precheck_max_debris: int) -> dict[str, Any] | None:
    if not backend_cmd:
        return None

    tmp_path = pathlib.Path("/tmp") / f"{manifest.get('scenario_id', 'strict_manifest')}_precheck.json"
    precheck_manifest = build_backend_precheck_manifest(manifest, precheck_max_debris)
    tmp_path.write_text(json.dumps(precheck_manifest, indent=2) + "\n", encoding="utf-8")

    proc = start_backend(backend_cmd, startup_timeout, api_base)
    try:
        result = evaluate_manifest(
            argparse.Namespace(
                manifest=str(tmp_path),
                api_base=api_base,
                extra_steps=extra_steps,
                extra_step_seconds=extra_step_seconds,
                output="",
            )
        )
    finally:
        stop_backend(proc)

    result["runtime_score"] = runtime_score(result)
    result["backend_cdm_score"] = backend_cdm_signal(result)
    result["combined_rank_score"] = round(result["runtime_score"] + result["backend_cdm_score"], 3)
    return result


def adapt_feedback_weights(weights: FeedbackWeights, ranking_results: list[dict[str, Any]]) -> FeedbackWeights:
    if not ranking_results:
        return weights

    count = float(len(ranking_results))
    avg_predicted = sum(float(item.get("predicted_conjunction_count", 0) or 0) for item in ranking_results) / count
    avg_active = sum(float(item.get("active_cdm_warnings", 0) or 0) for item in ranking_results) / count
    avg_predicted_critical = sum(float(item.get("predicted_critical_count", 0) or 0) for item in ranking_results) / count
    avg_predicted_near_1km = sum(float(item.get("predicted_near_1km_count", 0) or 0) for item in ranking_results) / count
    avg_predicted_near_5km = sum(float(item.get("predicted_near_5km_count", 0) or 0) for item in ranking_results) / count
    avg_near_100 = sum(float(item.get("manifest_near_100_total", 0) or 0) for item in ranking_results) / count
    avg_near_250 = sum(float(item.get("manifest_near_250_total", 0) or 0) for item in ranking_results) / count
    avg_near_500 = sum(float(item.get("manifest_near_500_total", 0) or 0) for item in ranking_results) / count
    avg_family_diversity = sum(float(item.get("manifest_family_diversity", 0) or 0) for item in ranking_results) / count
    avg_shell_diversity = sum(float(item.get("manifest_shell_diversity", 0) or 0) for item in ranking_results) / count
    avg_phase_density = sum(float(item.get("manifest_phase_density_total", 0) or 0) for item in ranking_results) / count
    avg_los_ready = sum(float(item.get("los_ready_satellites", 0) or 0) for item in ranking_results) / count

    updated = FeedbackWeights(**feedback_weights_dict(weights))

    if avg_predicted <= 0.0 and avg_active <= 0.0 and avg_predicted_near_1km <= 0.0:
        updated.near_250_multiplier = clamp_weight(updated.near_250_multiplier * (1.12 if avg_near_250 < 1.0 else 1.06))
        updated.near_500_multiplier = clamp_weight(updated.near_500_multiplier * (1.08 if avg_near_500 < 2.0 else 1.04))
        if avg_phase_density < 3.0:
            updated.near_500_multiplier = clamp_weight(updated.near_500_multiplier * 1.03)
    else:
        updated.near_100_multiplier = clamp_weight(updated.near_100_multiplier * (1.16 if avg_predicted_critical > 0.0 or avg_predicted_near_1km > 0.0 else 1.08))
        updated.near_250_multiplier = clamp_weight(updated.near_250_multiplier * (1.08 if avg_predicted_near_5km > 0.0 else 1.04))

    if avg_los_ready < 2.0:
        updated.los_multiplier = clamp_weight(updated.los_multiplier * 1.06)

    if avg_family_diversity < 2.5:
        updated.family_balance_multiplier = clamp_weight(updated.family_balance_multiplier * 1.08)
    if avg_shell_diversity < 2.5:
        updated.shell_balance_multiplier = clamp_weight(updated.shell_balance_multiplier * 1.08)

    if avg_predicted <= 0.0 and avg_near_250 < 1.5:
        updated.diversity_bonus_multiplier = clamp_weight(updated.diversity_bonus_multiplier * 1.05)

    return updated


def main() -> None:
    args = parse_args()
    feedback_weights = load_feedback_weights(args.feedback_file)
    data_path = pathlib.Path(args.data).expanduser().resolve()
    records, first_epoch_s = parse_catalog(data_path, args.max_periapsis_km)
    default_target_epoch_s, _ = choose_target_epoch(args, first_epoch_s)

    payloads = sorted(
        (record for record in records if record.is_payload),
        key=lambda record: record.norad_id,
    )
    station_path = pathlib.Path(args.groundstations).expanduser().resolve() if args.groundstations else pathlib.Path()
    stations = load_ground_stations(station_path) if args.groundstations else []
    shell_buckets = build_shell_buckets(records, args.anchor_shell_a_km, args.anchor_shell_i_deg)
    prefiltered = prefilter_payload_anchors(
        payloads,
        shell_buckets,
        args.anchor_shell_a_km,
        args.anchor_shell_i_deg,
        args.phase_window_deg,
        args.raan_window_deg,
    )
    selected_anchor_prefilters = select_anchor_candidates(
        prefiltered,
        max(1, args.payload_candidates),
        feedback_weights,
    )
    anchors = [item.payload for item in selected_anchor_prefilters]
    prefilter_by_norad = {item.payload.norad_id: item for item in selected_anchor_prefilters}
    epochs = sampled_epochs(default_target_epoch_s, args.horizon_hours, args.sample_seconds)

    print(f"[info] parsed payload anchors: {len(anchors)}")
    print(f"[info] mining horizon: {args.horizon_hours:.1f} h with {len(epochs)} samples")
    print(f"[info] ground stations loaded: {len(stations)}")
    print(f"[info] feedback weights: {feedback_weights_dict(feedback_weights)}")

    cache: dict[str, list[tuple[float, float, float]]] = {}
    ranked: list[PayloadThreatSummary] = []
    for idx, anchor in enumerate(anchors, start=1):
        summary = score_payload(
            anchor,
            records,
            epochs,
            cache,
            stations,
            feedback_weights,
            prefilter_by_norad[anchor.norad_id].shell_density_count,
            prefilter_by_norad[anchor.norad_id].phase_density_count,
            args.sample_seconds,
            args.threat_candidates,
            args.same_shell_a_km,
            args.same_shell_i_deg,
        )
        ranked.append(summary)
        miss_text = "--" if not math.isfinite(summary.min_miss_km) else f"{summary.min_miss_km:.3f} km"
        print(
            f"[mine {idx}/{len(anchors)}] {anchor.norad_id} {anchor.object_name} | "
            f"score={summary.score:.1f} min_miss={miss_text} "
            f"critical={summary.critical_count} warning={summary.warning_count} close={summary.close_count} "
            f"near100={summary.near_100_count} near250={summary.near_250_count} near500={summary.near_500_count} "
            f"los_samples={summary.los_sample_count} shell_density={summary.shell_density_count} phase_density={summary.phase_density_count} "
            f"family={summary.family_tag} shell={summary.shell_tag}"
        )

    ranked.sort(key=lambda summary: (-summary.score, summary.min_miss_km, summary.payload.norad_id))
    windows = encounter_windows(ranked, args.encounter_cluster_hours, args.encounter_lead_seconds, first_epoch_s)
    if windows:
        best_window = windows[0]
        print(
            f"[encounter] target={iso8601_utc(best_window.target_epoch_s)} peak={iso8601_utc(best_window.cluster_peak_epoch_s)} "
            f"min_miss={best_window.min_miss_km:.3f}km satellites={len(best_window.focus_satellite_norad)} debris={len(best_window.focus_debris_norad)}"
        )
    else:
        print(f"[encounter] no clustered encounter window found; falling back to {iso8601_utc(default_target_epoch_s)}")

    output_dir = pathlib.Path(args.output_dir).expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    candidate_target = max(1, args.candidate_scenarios or args.top_scenarios)
    candidate_target = min(candidate_target, len(ranked))
    top_n = min(max(1, args.top_scenarios), candidate_target)
    emitted = 0
    emitted_paths: list[pathlib.Path] = []
    seen_signatures: set[tuple[str, ...]] = set()
    for rank in range(len(ranked)):
        if emitted >= candidate_target:
            break
        manifest = scenario_manifest(args, default_target_epoch_s, ranked, feedback_weights, rank, windows)
        signature = tuple(sorted(manifest["operator_selection"]["norad_ids"]))
        if signature in seen_signatures:
            continue
        seen_signatures.add(signature)
        manifest["scenario_id"] = f"{args.scenario_prefix}_{emitted + 1:02d}"
        if args.backend_rank_cmd:
            precheck = run_backend_precheck(
                manifest,
                args.backend_rank_cmd,
                args.backend_rank_api_base,
                args.backend_rank_startup_timeout,
                0,
                args.backend_rank_extra_step_seconds,
                args.backend_precheck_max_debris,
            )
            if precheck is not None:
                manifest.setdefault("mining", {})["backend_precheck"] = {
                    "max_debris": min(args.max_debris, max(64, args.backend_precheck_max_debris)),
                    "predicted_conjunction_count": precheck.get("predicted_conjunction_count"),
                    "active_cdm_warnings": precheck.get("active_cdm_warnings"),
                    "predicted_min_miss_km": precheck.get("predicted_min_miss_km"),
                    "predictive_alignment_gap_km": precheck.get("predictive_alignment_gap_km"),
                    "heuristic_predictive_gap_km": precheck.get("heuristic_predictive_gap_km"),
                    "predictive_cdm_threshold_km": precheck.get("predictive_cdm_threshold_km"),
                    "effective_collision_threshold_km": precheck.get("effective_collision_threshold_km"),
                    "runtime_score": precheck.get("runtime_score"),
                    "backend_cdm_score": precheck.get("backend_cdm_score"),
                    "combined_rank_score": precheck.get("combined_rank_score"),
                }
                print(
                    f"[precheck] {manifest['scenario_id']} predicted={precheck.get('predicted_conjunction_count', 0)} "
                    f"active={precheck.get('active_cdm_warnings', 0)} min_miss={precheck.get('predicted_min_miss_km')} "
                    f"gap={precheck.get('predictive_alignment_gap_km')}"
                )
        output_path = output_dir / f"{manifest['scenario_id']}.json"
        output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"[write] {output_path}")
        emitted_paths.append(output_path)
        emitted += 1

    summary_path = output_dir / f"{args.scenario_prefix}_summary.json"
    summary_payload = {
        "default_target_epoch": iso8601_utc(default_target_epoch_s),
        "candidate_manifest_count": emitted,
        "requested_top_scenarios": top_n,
        "feedback_weights": feedback_weights_dict(feedback_weights),
        "backend_precheck_max_debris": args.backend_precheck_max_debris,
        "encounter_windows": [
            {
                "target_epoch": iso8601_utc(window.target_epoch_s),
                "cluster_peak_epoch": iso8601_utc(window.cluster_peak_epoch_s),
                "cluster_score": round(window.cluster_score, 3),
                "min_miss_km": round(window.min_miss_km, 6),
                "focus_satellite_norad": window.focus_satellite_norad,
                "focus_debris_norad": window.focus_debris_norad,
            }
            for window in windows[: min(6, len(windows))]
        ],
        "top_payloads": [
            {
                "norad_id": summary.payload.norad_id,
                "object_name": summary.payload.object_name,
                "score": round(summary.score, 3),
                "min_miss_km": None if not math.isfinite(summary.min_miss_km) else round(summary.min_miss_km, 6),
                "best_miss_epoch": iso8601_utc(summary.best_miss_epoch_s),
                "avg_top3_miss_km": None if not math.isfinite(summary.avg_top3_miss_km) else round(summary.avg_top3_miss_km, 6),
                "critical_count": summary.critical_count,
                "warning_count": summary.warning_count,
                "close_count": summary.close_count,
                "near_100_count": summary.near_100_count,
                "near_250_count": summary.near_250_count,
                "near_500_count": summary.near_500_count,
                "los_sample_count": summary.los_sample_count,
                "los_station_ids": summary.los_station_ids,
                "best_station_id": summary.best_station_id,
                "best_elevation_deg": round(summary.best_elevation_deg, 3),
                "family_tag": summary.family_tag,
                "shell_tag": summary.shell_tag,
                "shell_density_count": summary.shell_density_count,
                "phase_density_count": summary.phase_density_count,
            }
            for summary in ranked[:top_n]
        ],
    }
    summary_path.write_text(json.dumps(summary_payload, indent=2) + "\n", encoding="utf-8")
    print(f"[write] {summary_path}")

    if args.backend_rank_cmd and emitted_paths:
        ranking_results: list[dict[str, Any]] = []
        for idx, manifest_path in enumerate(emitted_paths, start=1):
            print(f"[backend-rank {idx}/{len(emitted_paths)}] {manifest_path}")
            proc = start_backend(
                args.backend_rank_cmd,
                args.backend_rank_startup_timeout,
                args.backend_rank_api_base,
            )
            try:
                result = evaluate_manifest(
                    argparse.Namespace(
                        manifest=str(manifest_path),
                        api_base=args.backend_rank_api_base,
                        extra_steps=args.backend_rank_extra_steps,
                        extra_step_seconds=args.backend_rank_extra_step_seconds,
                        output="",
                    )
                )
            finally:
                stop_backend(proc)
            result["runtime_score"] = runtime_score(result)
            result["backend_cdm_score"] = backend_cdm_signal(result)
            result["combined_rank_score"] = round(result["runtime_score"] + result["backend_cdm_score"], 3)
            ranking_results.append(result)
            manifest_doc = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest_doc["backend_cdm_evaluation"] = {
                "predicted_conjunction_count": result.get("predicted_conjunction_count"),
                "predicted_critical_count": result.get("predicted_critical_count"),
                "predicted_near_1km_count": result.get("predicted_near_1km_count"),
                "predicted_near_5km_count": result.get("predicted_near_5km_count"),
                "predicted_fail_open_count": result.get("predicted_fail_open_count"),
                "predicted_watch_satellite_hits": result.get("predicted_watch_satellite_hits"),
                "predicted_min_miss_km": result.get("predicted_min_miss_km"),
                "predicted_avg_miss_km": result.get("predicted_avg_miss_km"),
                "active_cdm_warnings": result.get("active_cdm_warnings"),
                "runtime_score": result.get("runtime_score"),
                "backend_cdm_score": result.get("backend_cdm_score"),
                "combined_rank_score": result.get("combined_rank_score"),
            }
            manifest_path.write_text(json.dumps(manifest_doc, indent=2) + "\n", encoding="utf-8")
            print(
                f"  -> score={result['runtime_score']} backend_cdm={result['backend_cdm_score']} predicted={result['predicted_conjunction_count']} "
                f"history={result['history_conjunction_count']} pending_burns={result['burns_pending']}"
            )

        ranking_results.sort(
            key=lambda item: (
                -float(item.get("combined_rank_score", 0.0)),
                -float(item.get("backend_cdm_score", 0.0)),
                -float(item.get("runtime_score", 0.0)),
                -float(item.get("predicted_conjunction_count", 0) or 0),
                -float(item.get("burns_pending", 0) or 0),
                str(item.get("scenario_id") or item.get("manifest") or ""),
            )
        )

        updated_feedback = adapt_feedback_weights(feedback_weights, ranking_results)
        feedback_weights = updated_feedback

        backend_summary = {
            "backend_cmd": args.backend_rank_cmd,
            "api_base": args.backend_rank_api_base,
            "extra_steps": args.backend_rank_extra_steps,
            "extra_step_seconds": args.backend_rank_extra_step_seconds,
            "feedback_weights": feedback_weights_dict(updated_feedback),
            "results": ranking_results,
            "selected_top_scenarios": ranking_results[:top_n],
        }
        backend_summary_path = output_dir / f"{args.scenario_prefix}_backend_ranking.json"
        backend_summary_path.write_text(json.dumps(backend_summary, indent=2) + "\n", encoding="utf-8")
        print(f"[write] {backend_summary_path}")

        if args.feedback_output:
            write_feedback_weights(args.feedback_output, updated_feedback)
            print(f"[write] {pathlib.Path(args.feedback_output).expanduser().resolve()}")


if __name__ == "__main__":
    main()
