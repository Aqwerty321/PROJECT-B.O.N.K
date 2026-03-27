#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


PERFORMANCE_KEYS = {
    "tick_ms_mean",
    "tick_ms_median",
    "tick_ms_p95",
    "phase_propagation_ms_mean",
    "phase_broad_ms_mean",
    "phase_narrow_precomp_ms_mean",
    "phase_narrow_sweep_ms_mean",
    "phase_other_ms_mean",
}


PROFILE_DEFAULTS = {
    "smoke": {
        "benchmark_target": "phase3_tick_benchmark",
        "sat_count": 50,
        "deb_count": 10000,
        "warmup_ticks": 5,
        "measure_ticks": 10,
        "step_seconds": 30.0,
    },
    "default": {
        "benchmark_target": "phase3_tick_benchmark",
        "sat_count": 50,
        "deb_count": 10000,
        "warmup_ticks": 10,
        "measure_ticks": 40,
        "step_seconds": 30.0,
    },
    "long": {
        "benchmark_target": "phase3_tick_benchmark",
        "sat_count": 50,
        "deb_count": 10000,
        "warmup_ticks": 20,
        "measure_ticks": 100,
        "step_seconds": 30.0,
    },
    "stress": {
        "benchmark_target": "phase3_tick_benchmark",
        "sat_count": 100,
        "deb_count": 20000,
        "warmup_ticks": 10,
        "measure_ticks": 40,
        "step_seconds": 30.0,
    },
    "real-smoke": {
        "benchmark_target": "real_data_scenario_gen",
        "data_path": "data.txt",
        "sat_count": 50,
        "deb_count": 10000,
        "warmup_ticks": 5,
        "measure_ticks": 10,
        "step_seconds": 30.0,
    },
    "real-long": {
        "benchmark_target": "real_data_scenario_gen",
        "data_path": "data.txt",
        "sat_count": 50,
        "deb_count": 10000,
        "warmup_ticks": 10,
        "measure_ticks": 30,
        "step_seconds": 30.0,
    },
}


def run_command(command: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(
        command,
        cwd=str(cwd) if cwd else None,
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout


def metric_as_float(metrics: dict[str, str], key: str) -> float | None:
    value = metrics.get(key)
    if value is None:
        return None
    return float(value)


def read_cmake_build_type(build_dir: Path) -> str | None:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.exists():
        return None
    for line in cache_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("CMAKE_BUILD_TYPE:STRING="):
            return line.split("=", 1)[1].strip()
    return None


def ensure_configured(source_dir: Path, build_dir: Path) -> None:
    existing_build_type = read_cmake_build_type(build_dir)
    if existing_build_type is not None:
        if existing_build_type != "Release":
            raise RuntimeError(
                f"existing build directory {build_dir} uses CMAKE_BUILD_TYPE={existing_build_type}; "
                "use a dedicated Release build directory for benchmarking"
            )
        return

    run_command([
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
    ])


def build_benchmark(source_dir: Path, build_dir: Path, target_name: str) -> Path:
    ensure_configured(source_dir, build_dir)
    run_command([
        "cmake",
        "--build",
        str(build_dir),
        "--target",
        target_name,
        "-j",
    ])
    executable = build_dir / target_name
    if not executable.exists():
        raise FileNotFoundError(f"benchmark executable not found: {executable}")
    return executable


def parse_metrics(output: str) -> dict[str, str]:
    metrics: dict[str, str] = {}
    for line in output.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        metrics[key.strip()] = value.strip()
    return metrics


def run_benchmark(executable: Path, args: argparse.Namespace) -> dict[str, str]:
    command = [str(executable)]
    if args.benchmark_target == "real_data_scenario_gen":
        command.extend([
            str(args.data_path),
            str(args.sat_count),
            str(args.deb_count),
            str(args.warmup_ticks),
            str(args.measure_ticks),
            str(args.step_seconds),
        ])
    else:
        command.extend([
            str(args.sat_count),
            str(args.deb_count),
            str(args.warmup_ticks),
            str(args.measure_ticks),
            str(args.step_seconds),
            str(args.seed),
            str(args.start_epoch_s),
        ])

    output = run_command(command)
    metrics = parse_metrics(output)
    if "final_state_fingerprint" not in metrics:
        raise RuntimeError("benchmark output missing final_state_fingerprint")
    return metrics


def compare_metrics(baseline: dict[str, str], current: dict[str, str]) -> list[str]:
    mismatches: list[str] = []
    all_keys = sorted(set(baseline) | set(current))
    for key in all_keys:
        if key in PERFORMANCE_KEYS:
            continue
        if baseline.get(key) != current.get(key):
            mismatches.append(
                f"{key}: baseline={baseline.get(key)!r} current={current.get(key)!r}"
            )
    return mismatches


def fmt_delta(baseline_value: str, current_value: str) -> str:
    baseline = float(baseline_value)
    current = float(current_value)
    delta = current - baseline
    pct = 0.0 if baseline == 0.0 else (delta / baseline) * 100.0
    return f"{baseline:.3f} -> {current:.3f} ({delta:+.3f}, {pct:+.2f}%)"


def build_delta_table(baseline: dict[str, str], current: dict[str, str]) -> dict[str, dict[str, float]]:
    deltas: dict[str, dict[str, float]] = {}
    for key in sorted(PERFORMANCE_KEYS):
        baseline_value = metric_as_float(baseline, key)
        current_value = metric_as_float(current, key)
        if baseline_value is None or current_value is None:
            continue
        absolute_delta = current_value - baseline_value
        pct_delta = 0.0 if baseline_value == 0.0 else (absolute_delta / baseline_value) * 100.0
        deltas[key] = {
            "baseline": baseline_value,
            "current": current_value,
            "absolute_delta": absolute_delta,
            "percent_delta": pct_delta,
        }
    return deltas


def write_text_report(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json_report(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def render_markdown_report(payload: dict[str, object]) -> str:
    baseline_metrics = payload["baseline_metrics"]
    current_metrics = payload["current_metrics"]
    delta_metrics = payload["delta_metrics"]
    mismatches = payload["mismatches"]

    lines = [
        "# Benchmark Comparison Report",
        "",
        f"- Generated UTC: {payload['generated_at_utc']}",
        f"- Baseline ref: {payload['baseline_ref']}",
        f"- Benchmark target: {payload['benchmark_target']}",
        f"- Profile: {payload['profile']}",
        f"- Scenario: {payload['sat_count']} sats / {payload['deb_count']} debris / step {payload['step_seconds']} s",
        f"- Warmup ticks: {payload['warmup_ticks']}",
        f"- Measure ticks: {payload['measure_ticks']}",
        f"- Correctness status: {payload['correctness_status']}",
    ]

    if payload.get("data_path"):
        lines.append(f"- Data source: {payload['data_path']}")
    if payload.get("seed") is not None:
        lines.append(f"- Seed: {payload['seed']}")

    lines.extend([
        "",
        "## Correctness",
        "",
        f"- Baseline fingerprint: {baseline_metrics['final_state_fingerprint']}",
        f"- Current fingerprint: {current_metrics['final_state_fingerprint']}",
    ])

    if mismatches:
        lines.append("- Mismatches detected:")
        for mismatch in mismatches:
            lines.append(f"  - {mismatch}")
    else:
        lines.append("- All non-timing metrics matched exactly.")

    lines.extend([
        "",
        "## Performance Deltas",
        "",
        "| Metric | Baseline | Current | Delta | Percent |",
        "|---|---:|---:|---:|---:|",
    ])

    for key in sorted(delta_metrics):
        values = delta_metrics[key]
        lines.append(
            f"| {key} | {values['baseline']:.3f} | {values['current']:.3f} | {values['absolute_delta']:+.3f} | {values['percent_delta']:+.2f}% |"
        )

    return "\n".join(lines) + "\n"


def resolve_output_path(path_value: str | None, default_name: str, repo_root: Path) -> Path | None:
    if path_value is None:
        return None
    path = Path(path_value)
    if not path.is_absolute():
        path = repo_root / path
    if path.suffix:
        return path
    return path / default_name


def apply_profile_defaults(args: argparse.Namespace) -> None:
    defaults = PROFILE_DEFAULTS[args.profile]
    for key, value in defaults.items():
        if getattr(args, key) is None:
            setattr(args, key, value)


def create_baseline_worktree(repo_root: Path, baseline_ref: str, temp_root: Path) -> tuple[Path, Path]:
    worktree_dir = temp_root / "baseline-worktree"
    build_dir = temp_root / "baseline-build"
    run_command([
        "git",
        "-C",
        str(repo_root),
        "worktree",
        "add",
        "--detach",
        str(worktree_dir),
        baseline_ref,
    ])
    shutil.copy2(repo_root / "tools" / "phase3_tick_benchmark.cpp", worktree_dir / "tools" / "phase3_tick_benchmark.cpp")
    shutil.copy2(repo_root / "tools" / "real_data_scenario_gen.cpp", worktree_dir / "tools" / "real_data_scenario_gen.cpp")
    return worktree_dir, build_dir


def remove_worktree(repo_root: Path, worktree_dir: Path) -> None:
    subprocess.run(
        ["git", "-C", str(repo_root), "worktree", "remove", "--force", str(worktree_dir)],
        check=True,
        text=True,
        capture_output=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and compare benchmark targets for baseline vs current code."
    )
    parser.add_argument("--repo-root", default=Path(__file__).resolve().parents[1], type=Path)
    parser.add_argument("--baseline-ref", default="HEAD")
    parser.add_argument("--benchmark-target", choices=["phase3_tick_benchmark", "real_data_scenario_gen"])
    parser.add_argument("--current-build-dir", default="build-benchmark-release")
    parser.add_argument("--profile", choices=sorted(PROFILE_DEFAULTS), default="default")
    parser.add_argument("--sat-count", type=int)
    parser.add_argument("--deb-count", type=int)
    parser.add_argument("--warmup-ticks", type=int)
    parser.add_argument("--measure-ticks", type=int)
    parser.add_argument("--step-seconds", type=float)
    parser.add_argument("--seed", type=int, default=20260317)
    parser.add_argument("--start-epoch-s", type=float, default=1773292800.0)
    parser.add_argument("--data-path")
    parser.add_argument("--report-json", help="Write a JSON comparison report to this file or directory")
    parser.add_argument("--report-md", help="Write a Markdown comparison report to this file or directory")
    parser.add_argument("--keep-worktree", action="store_true")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    apply_profile_defaults(args)
    current_build_dir = (repo_root / args.current_build_dir).resolve()

    if args.data_path is not None:
        data_path = Path(args.data_path)
        if not data_path.is_absolute():
            data_path = repo_root / data_path
        args.data_path = str(data_path.resolve())

    temp_root_path = Path(tempfile.mkdtemp(prefix="projectbonk-benchmark-"))
    worktree_dir = temp_root_path / "baseline-worktree"

    try:
        baseline_worktree, baseline_build_dir = create_baseline_worktree(
            repo_root,
            args.baseline_ref,
            temp_root_path,
        )

        current_executable = build_benchmark(repo_root, current_build_dir, args.benchmark_target)
        baseline_executable = build_benchmark(baseline_worktree, baseline_build_dir, args.benchmark_target)

        baseline_metrics = run_benchmark(baseline_executable, args)
        current_metrics = run_benchmark(current_executable, args)
        mismatches = compare_metrics(baseline_metrics, current_metrics)
        delta_metrics = build_delta_table(baseline_metrics, current_metrics)
        correctness_status = "MISMATCH" if mismatches else "MATCH"
        generated_at_utc = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
        report_stem = f"benchmark-compare-{args.profile}-{generated_at_utc.replace(':', '').replace('-', '')}"

        payload: dict[str, object] = {
            "generated_at_utc": generated_at_utc,
            "baseline_ref": args.baseline_ref,
            "benchmark_target": args.benchmark_target,
            "profile": args.profile,
            "sat_count": args.sat_count,
            "deb_count": args.deb_count,
            "warmup_ticks": args.warmup_ticks,
            "measure_ticks": args.measure_ticks,
            "step_seconds": args.step_seconds,
            "correctness_status": correctness_status,
            "mismatches": mismatches,
            "baseline_metrics": baseline_metrics,
            "current_metrics": current_metrics,
            "delta_metrics": delta_metrics,
        }
        if args.benchmark_target == "real_data_scenario_gen":
            payload["data_path"] = args.data_path
        else:
            payload["seed"] = args.seed
            payload["start_epoch_s"] = args.start_epoch_s

        json_report_path = resolve_output_path(args.report_json, report_stem + ".json", repo_root)
        md_report_path = resolve_output_path(args.report_md, report_stem + ".md", repo_root)
        if json_report_path is not None:
            write_json_report(json_report_path, payload)
        if md_report_path is not None:
            write_text_report(md_report_path, render_markdown_report(payload))

        print("PROJECTBONK benchmark comparison")
        print(f"baseline_ref={args.baseline_ref}")
        print(f"benchmark_target={args.benchmark_target}")
        print(f"profile={args.profile}")
        if args.benchmark_target == "real_data_scenario_gen":
            print(f"data_path={args.data_path}")
        else:
            print(f"seed={args.seed}")
        print(f"objects={args.sat_count + args.deb_count}")
        print(f"final_state_fingerprint_baseline={baseline_metrics['final_state_fingerprint']}")
        print(f"final_state_fingerprint_current={current_metrics['final_state_fingerprint']}")
        if json_report_path is not None:
            print(f"report_json={json_report_path}")
        if md_report_path is not None:
            print(f"report_md={md_report_path}")

        if mismatches:
            print("correctness_status=MISMATCH")
            for mismatch in mismatches:
                print(f"mismatch={mismatch}")
            return 1

        print("correctness_status=MATCH")
        for key in sorted(delta_metrics):
            print(f"{key}_delta={fmt_delta(baseline_metrics[key], current_metrics[key])}")
        return 0
    finally:
        if args.keep_worktree:
            print(f"kept_worktree={worktree_dir}")
        else:
            if worktree_dir.exists():
                remove_worktree(repo_root, worktree_dir)
            shutil.rmtree(temp_root_path, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())