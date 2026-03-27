#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import math
import urllib.request
from typing import Any


def api_get_json(url: str) -> Any:
    with urllib.request.urlopen(url, timeout=15) as resp:
        return json.loads(resp.read().decode())


def fmt(value: float | int | None, digits: int = 2) -> str:
    if value is None:
        return "--"
    if isinstance(value, int):
        return str(value)
    if not math.isfinite(value):
        return "--"
    return f"{value:.{digits}f}"


def main() -> None:
    parser = argparse.ArgumentParser(description="Check live backend demo readiness")
    parser.add_argument("--api-base", default="http://localhost:8000", help="Backend base URL")
    args = parser.parse_args()

    burns = api_get_json(f"{args.api_base}/api/debug/burns")
    status = api_get_json(f"{args.api_base}/api/status?details=1")
    predicted = api_get_json(f"{args.api_base}/api/debug/conjunctions?source=predicted")

    executed = burns.get("executed", []) if isinstance(burns, dict) else []
    pending = burns.get("pending", []) if isinstance(burns, dict) else []
    dropped = burns.get("dropped", []) if isinstance(burns, dict) else []
    summary = burns.get("summary", {}) if isinstance(burns, dict) else {}
    metrics = status.get("internal_metrics", {}) if isinstance(status, dict) else {}
    predicted_events = predicted.get("conjunctions", []) if isinstance(predicted, dict) else []

    tracked_avoidance = [
        burn for burn in executed
        if burn.get("scheduled_from_predictive_cdm") and burn.get("trigger_debris_id")
    ]
    evaluated = [burn for burn in tracked_avoidance if burn.get("mitigation_evaluated")]
    avoided = [burn for burn in evaluated if burn.get("collision_avoided")]
    evaluated_unsuccessful = [burn for burn in evaluated if not burn.get("collision_avoided")]
    pending_eval = [burn for burn in tracked_avoidance if not burn.get("mitigation_evaluated")]
    critical_predicted = [
        event for event in predicted_events
        if str(event.get("severity") or "") == "critical"
    ]

    readiness_score = 0
    readiness_score += min(len(avoided), 5) * 30
    readiness_score += min(len(evaluated), 8) * 8
    readiness_score += min(len(critical_predicted), 10) * 4
    readiness_score += min(len(pending), 10) * 2
    readiness_score -= min(len(dropped), 10) * 6

    if summary.get("collisions_avoided", 0) >= 2:
        verdict = "ready"
    elif summary.get("collisions_avoided", 0) >= 1 or len(evaluated) >= 2:
        verdict = "usable"
    else:
        verdict = "weak"

    report = {
        "verdict": verdict,
        "readiness_score": readiness_score,
        "tick_count": status.get("tick_count"),
        "objects": status.get("object_count"),
        "predictive_conjunction_count": len(predicted_events),
        "predictive_critical_count": len(critical_predicted),
        "executed_burns": len(executed),
        "pending_burns": len(pending),
        "dropped_burns": len(dropped),
        "tracked_avoidance_burns": len(tracked_avoidance),
        "evaluated_avoidance_burns": len(evaluated),
        "successful_avoids": len(avoided),
        "evaluated_unsuccessful": len(evaluated_unsuccessful),
        "pending_evaluation": len(pending_eval),
        "fleet_collisions_avoided": summary.get("collisions_avoided", 0),
        "fuel_consumed_kg": summary.get("fuel_consumed_kg", 0.0),
        "avoidance_fuel_consumed_kg": summary.get("avoidance_fuel_consumed_kg", 0.0),
        "active_cdm_warnings": metrics.get("active_cdm_warnings"),
    }

    print("[demo-readiness]")
    print(json.dumps(report, indent=2))

    if avoided:
        print("[confirmed-avoids]")
        for burn in avoided[:5]:
            print(json.dumps({
                "satellite_id": burn.get("satellite_id"),
                "trigger_debris_id": burn.get("trigger_debris_id"),
                "trigger_miss_distance_km": burn.get("trigger_miss_distance_km"),
                "mitigation_miss_distance_km": burn.get("mitigation_miss_distance_km"),
                "delta_v_norm_km_s": burn.get("delta_v_norm_km_s"),
            }))
    else:
        print(
            "[note] No confirmed collision_avoided burns yet. "
            f"Evaluated={len(evaluated)} pending_eval={len(pending_eval)} predicted_critical={len(critical_predicted)}"
        )


if __name__ == "__main__":
    main()
