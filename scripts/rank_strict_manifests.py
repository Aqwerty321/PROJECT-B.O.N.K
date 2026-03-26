#!/usr/bin/env python3
"""
Rank strict replay manifests by evaluating each one against a fresh local backend.

This is a local-only helper. It starts the backend once per manifest so each
scenario is measured from a clean runtime state.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shlex
import subprocess
import time
import urllib.error
import urllib.request
from typing import Any

from evaluate_strict_manifest import evaluate_manifest


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Rank strict replay manifests against fresh local backend runs",
    )
    parser.add_argument(
        "manifests",
        nargs="*",
        help="Manifest files to rank. If omitted, use --manifests-dir.",
    )
    parser.add_argument(
        "--manifests-dir",
        default="",
        help="Directory of manifest JSON files to rank",
    )
    parser.add_argument(
        "--backend-cmd",
        default="./build/ProjectBONK",
        help="Backend command to launch for each evaluation (default: ./build/ProjectBONK)",
    )
    parser.add_argument(
        "--api-base",
        default="http://localhost:8000",
        help="Backend base URL (default: http://localhost:8000)",
    )
    parser.add_argument(
        "--extra-steps",
        type=int,
        default=1,
        help="Additional simulate/step calls after manifest warmup (default: 1)",
    )
    parser.add_argument(
        "--extra-step-seconds",
        type=int,
        default=300,
        help="step_seconds for each extra step (default: 300)",
    )
    parser.add_argument(
        "--startup-timeout",
        type=float,
        default=20.0,
        help="Seconds to wait for backend startup per manifest (default: 20)",
    )
    parser.add_argument(
        "--output",
        default="",
        help="Optional path to write the ranking JSON summary",
    )
    return parser.parse_args()


def collect_manifests(args: argparse.Namespace) -> list[pathlib.Path]:
    paths: list[pathlib.Path] = []
    for raw in args.manifests:
        path = pathlib.Path(raw).expanduser().resolve()
        if path.is_file():
            paths.append(path)
    if args.manifests_dir:
        root = pathlib.Path(args.manifests_dir).expanduser().resolve()
        if root.is_dir():
            paths.extend(sorted(root.glob("*.json")))
    unique: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for path in paths:
        if path in seen:
            continue
        seen.add(path)
        unique.append(path)
    if not unique:
        raise SystemExit("[error] no manifest files provided")
    return unique


def wait_for_backend(api_base: str, timeout_s: float) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(f"{api_base}/api/status", timeout=2) as resp:
                if resp.status == 200:
                    return
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError):
            time.sleep(0.25)
    raise RuntimeError(f"backend did not become ready within {timeout_s:.1f}s")


def start_backend(command: str, startup_timeout: float, api_base: str = "http://localhost:8000") -> subprocess.Popen[str]:
    argv = shlex.split(command)
    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True)
    try:
        wait_for_backend(api_base, startup_timeout)
    except Exception:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        raise
    return proc


def stop_backend(proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def runtime_score(summary: dict[str, Any]) -> float:
    predicted = float(summary.get("predicted_conjunction_count", 0) or 0)
    history = float(summary.get("history_conjunction_count", 0) or 0)
    active = float(summary.get("active_cdm_warnings", 0) or 0)
    pending = float(summary.get("burns_pending", 0) or 0)
    executed = float(summary.get("burns_executed", 0) or 0)
    dropped = float(summary.get("burns_dropped", 0) or 0)
    recovery = float(summary.get("pending_recovery_requests", 0) or 0)
    collisions_avoided = float(summary.get("collisions_avoided", 0) or 0)
    fuel_consumed = float(summary.get("fuel_consumed_kg", 0.0) or 0.0)
    avoidance_fuel_consumed = float(summary.get("avoidance_fuel_consumed_kg", 0.0) or 0.0)
    satellites = float(summary.get("satellites_visible", 0) or 0)
    debris = float(summary.get("debris_visible", 0) or 0)
    los_ready = float(summary.get("los_ready_satellites", 0) or 0)
    ground_stations = float(len(summary.get("recommended_ground_stations", []) or []))
    near_100 = float(summary.get("manifest_near_100_total", 0) or 0)
    near_250 = float(summary.get("manifest_near_250_total", 0) or 0)
    near_500 = float(summary.get("manifest_near_500_total", 0) or 0)
    family_diversity = float(summary.get("manifest_family_diversity", 0) or 0)
    shell_diversity = float(summary.get("manifest_shell_diversity", 0) or 0)
    shell_density = float(summary.get("manifest_shell_density_total", 0) or 0)
    phase_density = float(summary.get("manifest_phase_density_total", 0) or 0)
    predicted_critical = float(summary.get("predicted_critical_count", 0) or 0)
    predicted_near_1km = float(summary.get("predicted_near_1km_count", 0) or 0)
    predicted_near_5km = float(summary.get("predicted_near_5km_count", 0) or 0)
    predicted_watch_hits = float(summary.get("predicted_watch_satellite_hits", 0) or 0)
    predicted_distinct_satellites = float(summary.get("predicted_distinct_satellites", 0) or 0)
    predicted_fail_open = float(summary.get("predicted_fail_open_count", 0) or 0)
    predicted_min_miss = summary.get("predicted_min_miss_km")

    score = 0.0
    score += predicted * 120.0
    score += active * 80.0
    score += history * 20.0
    score += pending * 60.0
    score += executed * 40.0
    score -= dropped * 30.0
    score += recovery * 25.0
    score += collisions_avoided * 140.0
    score -= min(fuel_consumed, 500.0) * 0.15
    score -= min(avoidance_fuel_consumed, 250.0) * 0.25
    score += min(debris, 10000.0) / 500.0
    score += min(satellites, 20.0) * 2.0
    score += los_ready * 10.0
    score += ground_stations * 5.0
    score += near_100 * 35.0
    score += near_250 * 12.0
    score += near_500 * 4.0
    score += family_diversity * 6.0
    score += shell_diversity * 6.0
    score += shell_density * 0.4
    score += phase_density * 6.0
    score += predicted_critical * 180.0
    score += predicted_near_1km * 80.0
    score += predicted_near_5km * 25.0
    score += predicted_watch_hits * 20.0
    score += predicted_distinct_satellites * 10.0
    score += predicted_fail_open * 30.0
    if predicted_min_miss is not None:
        score += max(0.0, 10.0 - float(predicted_min_miss)) * 12.0
    return round(score, 3)


def main() -> None:
    args = parse_args()
    manifests = collect_manifests(args)
    results: list[dict[str, Any]] = []

    for index, manifest_path in enumerate(manifests, start=1):
        print(f"[rank {index}/{len(manifests)}] {manifest_path}")
        proc = start_backend(args.backend_cmd, args.startup_timeout, args.api_base)
        try:
            summary = evaluate_manifest(
                argparse.Namespace(
                    manifest=str(manifest_path),
                    api_base=args.api_base,
                    extra_steps=args.extra_steps,
                    extra_step_seconds=args.extra_step_seconds,
                    output="",
                )
            )
        finally:
            stop_backend(proc)

        summary["runtime_score"] = runtime_score(summary)
        results.append(summary)
        print(
            f"  -> score={summary['runtime_score']} predicted={summary['predicted_conjunction_count']} "
            f"history={summary['history_conjunction_count']} pending_burns={summary['burns_pending']}"
        )

    results.sort(
        key=lambda item: (
            -float(item.get("runtime_score", 0.0)),
            -float(item.get("predicted_conjunction_count", 0) or 0),
            -float(item.get("burns_pending", 0) or 0),
            str(item.get("scenario_id") or item.get("manifest") or ""),
        )
    )

    output = {
        "backend_cmd": args.backend_cmd,
        "api_base": args.api_base,
        "extra_steps": args.extra_steps,
        "extra_step_seconds": args.extra_step_seconds,
        "results": results,
    }
    print("[ranking]")
    print(json.dumps(output, indent=2))

    if args.output:
        output_path = pathlib.Path(args.output).expanduser().resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
        print(f"[info] wrote ranking summary to {output_path}")


if __name__ == "__main__":
    main()
