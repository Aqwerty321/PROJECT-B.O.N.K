#!/usr/bin/env python3
"""
Evaluate a strict replay manifest against a live local backend.

This is a local-only helper for ranking or checking strict real-data replay
presets. It assumes the backend has been started fresh for the scenario.
"""

from __future__ import annotations

import argparse
import json
import pathlib
from types import SimpleNamespace
from typing import Any

from replay_data_catalog import (
    DEFAULT_API_BASE,
    DEFAULT_BATCH_SIZE,
    DEFAULT_DATA_PATH,
    DEFAULT_MAX_DEBRIS,
    DEFAULT_MAX_PERIAPSIS_KM,
    DEFAULT_OPERATOR_SATS,
    DEFAULT_SATELLITE_MODE,
    DEFAULT_TIMESTAMP,
    DEFAULT_WARMUP_STEPS,
    DEFAULT_WARMUP_STEP_SECONDS,
    apply_manifest_defaults,
    build_objects,
    chunked,
    choose_target_epoch,
    http_json,
    parse_catalog,
    print_manifest_summary,
    print_operator_summary,
    print_priority_debris_summary,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Evaluate a strict replay manifest against a live backend",
    )
    parser.add_argument("--manifest", required=True, help="Path to the strict replay manifest JSON")
    parser.add_argument("--api-base", default=DEFAULT_API_BASE, help="Backend base URL (default: http://localhost:8000)")
    parser.add_argument("--extra-steps", type=int, default=0, help="Additional simulate/step calls after manifest warmup (default: 0)")
    parser.add_argument("--extra-step-seconds", type=int, default=300, help="step_seconds for each extra step (default: 300)")
    parser.add_argument("--output", default="", help="Optional path to write the evaluation JSON summary")
    return parser.parse_args()


def manifest_namespace(manifest_path: str, api_base: str) -> SimpleNamespace:
    ns = SimpleNamespace(
        manifest=manifest_path,
        data=DEFAULT_DATA_PATH,
        api_base=api_base,
        timestamp=DEFAULT_TIMESTAMP,
        operator_sats=DEFAULT_OPERATOR_SATS,
        satellite_mode=DEFAULT_SATELLITE_MODE,
        operator_norad=[],
        max_debris=DEFAULT_MAX_DEBRIS,
        priority_debris_norad=[],
        max_periapsis_km=DEFAULT_MAX_PERIAPSIS_KM,
        batch_size=DEFAULT_BATCH_SIZE,
        warmup_steps=DEFAULT_WARMUP_STEPS,
        warmup_step_seconds=DEFAULT_WARMUP_STEP_SECONDS,
        dry_run=False,
    )
    ns = apply_manifest_defaults(ns)
    ns.api_base = api_base
    return ns


def evaluate_manifest(args: argparse.Namespace) -> dict[str, Any]:
    replay_args = manifest_namespace(args.manifest, args.api_base)
    data_path = pathlib.Path(replay_args.data).expanduser().resolve()
    records, first_epoch_s = parse_catalog(data_path, replay_args.max_periapsis_km)
    target_epoch_s, target_timestamp = choose_target_epoch(replay_args, first_epoch_s)
    objects, selected_payloads, debris_count = build_objects(
        records,
        target_epoch_s,
        replay_args.satellite_mode,
        replay_args.operator_sats,
        replay_args.max_debris,
        replay_args.operator_norad,
        replay_args.priority_debris_norad,
    )

    print_manifest_summary(replay_args)
    print(f"[info] replay timestamp: {target_timestamp}")
    print_operator_summary(selected_payloads, replay_args.satellite_mode)
    print_priority_debris_summary(replay_args.priority_debris_norad)
    print(f"[info] injecting {len(selected_payloads)} satellites + {debris_count} debris objects")

    total_processed = 0
    for batch in chunked(objects, max(1, replay_args.batch_size)):
        payload = {"timestamp": target_timestamp, "objects": batch}
        ack = http_json("POST", f"{replay_args.api_base}/api/telemetry", payload)
        total_processed += int(ack.get("processed_count", 0))

    for step_idx in range(replay_args.warmup_steps):
        step = http_json(
            "POST",
            f"{replay_args.api_base}/api/simulate/step",
            {"step_seconds": replay_args.warmup_step_seconds},
        )
        print(
            f"[warmup {step_idx + 1}/{replay_args.warmup_steps}] "
            f"new_timestamp={step.get('new_timestamp')} "
            f"collisions={step.get('collisions_detected')} "
            f"maneuvers={step.get('maneuvers_executed')}"
        )

    for step_idx in range(args.extra_steps):
        step = http_json(
            "POST",
            f"{replay_args.api_base}/api/simulate/step",
            {"step_seconds": args.extra_step_seconds},
        )
        print(
            f"[extra {step_idx + 1}/{args.extra_steps}] "
            f"new_timestamp={step.get('new_timestamp')} "
            f"collisions={step.get('collisions_detected')} "
            f"maneuvers={step.get('maneuvers_executed')}"
        )

    snapshot = http_json("GET", f"{replay_args.api_base}/api/visualization/snapshot")
    burns = http_json("GET", f"{replay_args.api_base}/api/debug/burns")
    history = http_json("GET", f"{replay_args.api_base}/api/debug/conjunctions?source=history")
    predicted = http_json("GET", f"{replay_args.api_base}/api/debug/conjunctions?source=predicted")
    status = http_json("GET", f"{replay_args.api_base}/api/status?details=1")
    metrics = status.get("internal_metrics", {})
    predicted_events = predicted.get("conjunctions", []) if isinstance(predicted, dict) else []
    manifest_data = getattr(replay_args, "manifest_data", {}) or {}
    mining = manifest_data.get("mining", {}) if isinstance(manifest_data, dict) else {}
    selected_payload_summaries = mining.get("selected_payload_summaries", []) if isinstance(mining, dict) else []
    los_ready_satellites = sum(1 for item in selected_payload_summaries if isinstance(item, dict) and int(item.get("los_sample_count", 0) or 0) > 0)
    near_100_total = sum(int(item.get("near_100_count", 0) or 0) for item in selected_payload_summaries if isinstance(item, dict))
    near_250_total = sum(int(item.get("near_250_count", 0) or 0) for item in selected_payload_summaries if isinstance(item, dict))
    near_500_total = sum(int(item.get("near_500_count", 0) or 0) for item in selected_payload_summaries if isinstance(item, dict))
    family_diversity = len({str(item.get("family_tag") or "") for item in selected_payload_summaries if isinstance(item, dict)})
    shell_diversity = len({str(item.get("shell_tag") or "") for item in selected_payload_summaries if isinstance(item, dict)})
    shell_density_total = sum(int(item.get("shell_density_count", 0) or 0) for item in selected_payload_summaries if isinstance(item, dict))
    phase_density_total = sum(int(item.get("phase_density_count", 0) or 0) for item in selected_payload_summaries if isinstance(item, dict))
    predicted_miss_values = [float(item.get("miss_distance_km", 0.0) or 0.0) for item in predicted_events if isinstance(item, dict)]
    predicted_distinct_satellites = len({str(item.get("satellite_id") or "") for item in predicted_events if isinstance(item, dict)})
    predicted_distinct_debris = len({str(item.get("debris_id") or "") for item in predicted_events if isinstance(item, dict)})
    predicted_fail_open_count = sum(1 for item in predicted_events if isinstance(item, dict) and bool(item.get("fail_open")))
    predicted_critical_count = sum(1 for item in predicted_events if isinstance(item, dict) and (bool(item.get("collision")) or float(item.get("miss_distance_km", 9999.0) or 9999.0) <= 0.1))
    predicted_near_1km_count = sum(1 for item in predicted_events if isinstance(item, dict) and float(item.get("miss_distance_km", 9999.0) or 9999.0) <= 1.0)
    predicted_near_5km_count = sum(1 for item in predicted_events if isinstance(item, dict) and float(item.get("miss_distance_km", 9999.0) or 9999.0) <= 5.0)
    predicted_min_miss_km = min(predicted_miss_values) if predicted_miss_values else None
    predicted_avg_miss_km = (sum(predicted_miss_values) / len(predicted_miss_values)) if predicted_miss_values else None
    watch_satellites = manifest_data.get("watch_satellites", []) if isinstance(manifest_data, dict) else []
    watch_satellite_set = set(watch_satellites)
    predicted_watch_hits = len({str(item.get("satellite_id") or "") for item in predicted_events if isinstance(item, dict) and str(item.get("satellite_id") or "") in watch_satellite_set})

    summary = {
        "scenario_id": manifest_data.get("scenario_id") if isinstance(manifest_data, dict) else None,
        "manifest": str(pathlib.Path(args.manifest).expanduser().resolve()),
        "api_base": replay_args.api_base,
        "target_timestamp": target_timestamp,
        "telemetry_processed": total_processed,
        "satellites_visible": len(snapshot.get("satellites", [])),
        "debris_visible": len(snapshot.get("debris_cloud", [])),
        "burns_pending": len(burns.get("pending", [])),
        "burns_executed": len(burns.get("executed", [])),
        "burns_dropped": len(burns.get("dropped", [])),
        "collisions_avoided": ((burns.get("summary", {}) if isinstance(burns, dict) else {}).get("collisions_avoided", 0)),
        "fuel_consumed_kg": ((burns.get("summary", {}) if isinstance(burns, dict) else {}).get("fuel_consumed_kg", 0.0)),
        "avoidance_fuel_consumed_kg": ((burns.get("summary", {}) if isinstance(burns, dict) else {}).get("avoidance_fuel_consumed_kg", 0.0)),
        "history_conjunction_count": history.get("count", 0),
        "predicted_conjunction_count": predicted.get("count", 0),
        "active_cdm_warnings": metrics.get("active_cdm_warnings"),
        "pending_burn_queue": metrics.get("pending_burn_queue"),
        "dropped_burn_count_metric": metrics.get("dropped_burn_count"),
        "predictive_conjunction_count_metric": metrics.get("predictive_conjunction_count"),
        "history_conjunction_count_metric": metrics.get("history_conjunction_count"),
        "pending_recovery_requests": metrics.get("pending_recovery_requests"),
        "watch_satellites": watch_satellites,
        "recommended_ground_stations": mining.get("recommended_ground_stations", []) if isinstance(mining, dict) else [],
        "los_ready_satellites": los_ready_satellites,
        "manifest_near_100_total": near_100_total,
        "manifest_near_250_total": near_250_total,
        "manifest_near_500_total": near_500_total,
        "manifest_family_diversity": family_diversity,
        "manifest_shell_diversity": shell_diversity,
        "manifest_shell_density_total": shell_density_total,
        "manifest_phase_density_total": phase_density_total,
        "predicted_distinct_satellites": predicted_distinct_satellites,
        "predicted_distinct_debris": predicted_distinct_debris,
        "predicted_fail_open_count": predicted_fail_open_count,
        "predicted_critical_count": predicted_critical_count,
        "predicted_near_1km_count": predicted_near_1km_count,
        "predicted_near_5km_count": predicted_near_5km_count,
        "predicted_watch_satellite_hits": predicted_watch_hits,
        "predicted_min_miss_km": predicted_min_miss_km,
        "predicted_avg_miss_km": predicted_avg_miss_km,
    }
    return summary


def main() -> None:
    args = parse_args()
    summary = evaluate_manifest(args)
    print("[evaluation]")
    print(json.dumps(summary, indent=2))

    if args.output:
        output_path = pathlib.Path(args.output).expanduser().resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(f"[info] wrote evaluation summary to {output_path}")


if __name__ == "__main__":
    main()
