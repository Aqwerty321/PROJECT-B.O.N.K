#!/usr/bin/env python3
"""
Generate a strict real-data replay manifest from local catalog data.

This is a lightweight bridge between the current replay helper and the future
full scenario miner. It does not discover natural conjunctions yet; instead it
produces a reproducible manifest with a real payload-derived operator fleet that
can be refined later.
"""

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any

from replay_data_catalog import (
    DEFAULT_API_BASE,
    DEFAULT_DATA_PATH,
    DEFAULT_MAX_DEBRIS,
    DEFAULT_MAX_PERIAPSIS_KM,
    DEFAULT_OPERATOR_SATS,
    DEFAULT_SATELLITE_MODE,
    DEFAULT_WARMUP_STEP_SECONDS,
    DEFAULT_WARMUP_STEPS,
    choose_target_epoch,
    iso8601_utc,
    parse_catalog,
    select_operator_payloads,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a strict real-data replay manifest from a local OMM or 3LE/TLE catalog",
    )
    parser.add_argument(
        "--data",
        default=DEFAULT_DATA_PATH,
        help="Path to a local OMM JSON or 3LE/TLE text file (default: 3le_data.txt)",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Path to write the JSON manifest",
    )
    parser.add_argument(
        "--scenario-id",
        default="strict_real_data_manifest",
        help="Scenario identifier to embed in the manifest",
    )
    parser.add_argument(
        "--description",
        default="Strict real-data replay manifest generated from local OMM catalog data.",
        help="Scenario description text",
    )
    parser.add_argument(
        "--timestamp",
        default="",
        help="Optional target timestamp (ISO-8601 UTC); defaults to the first catalog epoch",
    )
    parser.add_argument(
        "--satellite-mode",
        choices=("synthetic", "catalog"),
        default=DEFAULT_SATELLITE_MODE,
        help="Operator fleet representation mode (default: synthetic)",
    )
    parser.add_argument(
        "--operator-sats",
        type=int,
        default=DEFAULT_OPERATOR_SATS,
        help="Number of payloads to promote into the operator fleet (default: 8)",
    )
    parser.add_argument(
        "--operator-norad",
        action="append",
        default=[],
        metavar="NORAD_ID",
        help="Explicit PAYLOAD NORAD ID to include; may be repeated",
    )
    parser.add_argument(
        "--max-debris",
        type=int,
        default=DEFAULT_MAX_DEBRIS,
        help="Max catalog objects to inject as DEBRIS (default: 5000)",
    )
    parser.add_argument(
        "--max-periapsis-km",
        type=float,
        default=DEFAULT_MAX_PERIAPSIS_KM,
        help="LEO filter on periapsis altitude in km (default: 2000)",
    )
    parser.add_argument(
        "--api-base",
        default=DEFAULT_API_BASE,
        help="Backend base URL to store in the manifest (default: http://localhost:8000)",
    )
    parser.add_argument(
        "--warmup-steps",
        type=int,
        default=DEFAULT_WARMUP_STEPS,
        help="Warmup step count for the replay manifest (default: 3)",
    )
    parser.add_argument(
        "--warmup-step-seconds",
        type=int,
        default=DEFAULT_WARMUP_STEP_SECONDS,
        help="Warmup step size in seconds (default: 60)",
    )
    return parser.parse_args()


def build_manifest(args: argparse.Namespace) -> dict[str, Any]:
    data_path = pathlib.Path(args.data).expanduser().resolve()
    records, first_epoch_s = parse_catalog(data_path, args.max_periapsis_km)
    payloads = sorted(
        (record for record in records if record.is_payload),
        key=lambda record: (record.e, record.periapsis_alt_km, record.norad_id),
    )
    selected = select_operator_payloads(payloads, args.operator_sats, args.operator_norad)

    class EpochArgs:
        timestamp = args.timestamp
        api_base = args.api_base

    target_epoch_s, target_timestamp = choose_target_epoch(EpochArgs, first_epoch_s)
    del target_epoch_s

    watch_satellites = [
        (f"SAT-{record.norad_id}" if args.satellite_mode == "catalog" else f"SAT-LOCAL-{idx:02d}")
        for idx, record in enumerate(selected[: min(3, len(selected))], start=1)
    ]

    return {
        "scenario_id": args.scenario_id,
        "description": args.description,
        "catalog_sources": [str(data_path.name)],
        "target_epoch": target_timestamp,
        "operator_selection": {
            "mode": args.satellite_mode,
            "count": len(selected),
            "norad_ids": [record.norad_id for record in selected],
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
            "operator_sats": len(selected),
        },
        "watch_satellites": watch_satellites,
        "generated": {
            "generator": "scripts/generate_strict_manifest.py",
            "catalog_epoch": iso8601_utc(first_epoch_s),
        },
        "notes": [
            "This manifest captures a reproducible real-data replay preset.",
            "It does not yet guarantee a naturally interesting conjunction scene; that remains future scenario-miner work.",
        ],
    }


def main() -> None:
    args = parse_args()
    manifest = build_manifest(args)

    output_path = pathlib.Path(args.output).expanduser().resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"[info] wrote manifest to {output_path}")
    print(f"[info] scenario_id: {manifest['scenario_id']}")
    print(f"[info] target_epoch: {manifest['target_epoch']}")
    print(f"[info] operator_count: {manifest['operator_selection']['count']}")
    print(f"[info] watch_satellites: {', '.join(manifest['watch_satellites']) if manifest['watch_satellites'] else '--'}")


if __name__ == "__main__":
    main()
