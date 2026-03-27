#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
from pathlib import Path
import re

from benchmark_compare import PROFILE_DEFAULTS, resolve_output_path, write_text_report


DELTA_PATTERN = re.compile(
    r"^([a-z0-9_]+)_delta=(\d+\.\d+) -> (\d+\.\d+) \(([+-]?\d+\.\d+), ([+-]?\d+\.\d+)%\)$",
    re.M,
)
FINGERPRINT_PATTERN = re.compile(r"^final_state_fingerprint_current=(0x[0-9a-f]+)$", re.M)
CORRECTNESS_PATTERN = re.compile(r"^correctness_status=(\w+)$", re.M)


def parse_output(output: str) -> dict[str, object]:
    metrics = {
        match.group(1): {
            "baseline": float(match.group(2)),
            "current": float(match.group(3)),
            "absolute_delta": float(match.group(4)),
            "percent_delta": float(match.group(5)),
        }
        for match in DELTA_PATTERN.finditer(output)
    }
    correctness_match = CORRECTNESS_PATTERN.search(output)
    fingerprint_match = FINGERPRINT_PATTERN.search(output)
    return {
        "correctness": correctness_match.group(1) if correctness_match else None,
        "fingerprint": fingerprint_match.group(1) if fingerprint_match else None,
        "metrics": metrics,
    }


def summarize_metric(results: list[dict[str, object]], key: str) -> dict[str, float]:
    current_values = [result["metrics"][key]["current"] for result in results]
    percent_values = [result["metrics"][key]["percent_delta"] for result in results]
    return {
        "current_mean": statistics.mean(current_values),
        "current_min": min(current_values),
        "current_max": max(current_values),
        "current_stdev": statistics.pstdev(current_values),
        "percent_mean": statistics.mean(percent_values),
        "percent_min": min(percent_values),
        "percent_max": max(percent_values),
    }


def render_markdown_report(payload: dict[str, object]) -> str:
    lines = [
        "# Benchmark Envelope Report",
        "",
        f"- Profile: {payload['profile']}",
        f"- Runs: {payload['runs']}",
        f"- Baseline ref: {payload['baseline_ref']}",
        f"- Current build dir: {payload['current_build_dir']}",
        f"- Correctness status: {payload['correctness_status']}",
        f"- Fingerprints: {', '.join(payload['fingerprints'])}",
        "",
        "## Per-Run Tick Summary",
        "",
        "| Run | Mean Current | Mean Delta | Median Current | Median Delta | P95 Current | P95 Delta |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for run in payload["run_summaries"]:
        mean = run["metrics"]["tick_ms_mean"]
        median = run["metrics"]["tick_ms_median"]
        p95 = run["metrics"]["tick_ms_p95"]
        lines.append(
            f"| {run['run']} | {mean['current']:.3f} | {mean['percent_delta']:+.2f}% | "
            f"{median['current']:.3f} | {median['percent_delta']:+.2f}% | "
            f"{p95['current']:.3f} | {p95['percent_delta']:+.2f}% |"
        )

    lines.extend([
        "",
        "## Metric Summaries",
        "",
        "| Metric | Current Mean | Current Min | Current Max | Current Stdev | Percent Mean | Percent Min | Percent Max |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])

    for key, summary in sorted(payload["metric_summaries"].items()):
        lines.append(
            f"| {key} | {summary['current_mean']:.3f} | {summary['current_min']:.3f} | "
            f"{summary['current_max']:.3f} | {summary['current_stdev']:.3f} | "
            f"{summary['percent_mean']:+.2f}% | {summary['percent_min']:+.2f}% | {summary['percent_max']:+.2f}% |"
        )

    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run benchmark_compare.py multiple times and summarize the local timing envelope."
    )
    parser.add_argument("--repo-root", default=Path(__file__).resolve().parents[1], type=Path)
    parser.add_argument("--profile", choices=sorted(PROFILE_DEFAULTS), default="real-long")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--current-build-dir", default="build-benchmark-release")
    parser.add_argument("--baseline-ref", default="HEAD")
    parser.add_argument("--report-json", help="Write the envelope summary to this JSON file")
    parser.add_argument("--report-md", help="Write the envelope summary to this Markdown file")
    parser.add_argument(
        "compare_args",
        nargs=argparse.REMAINDER,
        help="Additional arguments forwarded to benchmark_compare.py after '--'",
    )
    args = parser.parse_args()

    if args.runs < 1:
        raise SystemExit("--runs must be >= 1")

    repo_root = args.repo_root.resolve()
    compare_script = repo_root / "scripts" / "benchmark_compare.py"
    python = Path(sys.executable)

    forwarded_args = list(args.compare_args)
    if forwarded_args and forwarded_args[0] == "--":
        forwarded_args = forwarded_args[1:]

    base_command = [
        str(python),
        str(compare_script),
        "--profile",
        args.profile,
        "--baseline-ref",
        args.baseline_ref,
        "--current-build-dir",
        args.current_build_dir,
        *forwarded_args,
    ]

    runs: list[dict[str, object]] = []
    for run_idx in range(args.runs):
        completed = subprocess.run(
            base_command,
            cwd=repo_root,
            text=True,
            capture_output=True,
        )
        parsed = parse_output(completed.stdout)
        run_record = {
            "run": run_idx + 1,
            "exit_code": completed.returncode,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
            **parsed,
        }
        runs.append(run_record)

        if completed.returncode != 0:
            print(f"run={run_idx + 1} exit={completed.returncode}")
            if completed.stdout:
                print(completed.stdout.rstrip())
            if completed.stderr:
                print(completed.stderr.rstrip(), file=sys.stderr)
            return completed.returncode

        if parsed["correctness"] != "MATCH":
            print(f"run={run_idx + 1} correctness_status={parsed['correctness']}")
            return 1

    metric_keys = sorted(runs[0]["metrics"].keys())
    summaries = {key: summarize_metric(runs, key) for key in metric_keys}
    fingerprints = sorted({run["fingerprint"] for run in runs})
    payload = {
        "profile": args.profile,
        "runs": args.runs,
        "baseline_ref": args.baseline_ref,
        "current_build_dir": str((repo_root / args.current_build_dir).resolve()),
        "correctness_status": "ALL_MATCH",
        "fingerprints": fingerprints,
        "run_summaries": [
            {
                "run": run["run"],
                "correctness": run["correctness"],
                "fingerprint": run["fingerprint"],
                "metrics": run["metrics"],
            }
            for run in runs
        ],
        "metric_summaries": summaries,
    }

    print("PROJECTBONK benchmark envelope")
    print(f"profile={args.profile}")
    print(f"runs={args.runs}")
    print(f"baseline_ref={args.baseline_ref}")
    print(f"current_build_dir={repo_root / args.current_build_dir}")
    print(f"correctness_status=ALL_MATCH")
    print(f"fingerprints={','.join(fingerprints)}")

    for run in runs:
        mean = run["metrics"].get("tick_ms_mean")
        median = run["metrics"].get("tick_ms_median")
        p95 = run["metrics"].get("tick_ms_p95")
        print(
            "run_summary="
            f"run:{run['run']},"
            f"mean_current:{mean['current']:.3f},mean_pct:{mean['percent_delta']:+.2f}%,"
            f"median_current:{median['current']:.3f},median_pct:{median['percent_delta']:+.2f}%,"
            f"p95_current:{p95['current']:.3f},p95_pct:{p95['percent_delta']:+.2f}%"
        )

    for key in metric_keys:
        summary = summaries[key]
        print(
            f"summary_{key}="
            f"current_mean:{summary['current_mean']:.3f},"
            f"current_min:{summary['current_min']:.3f},"
            f"current_max:{summary['current_max']:.3f},"
            f"current_stdev:{summary['current_stdev']:.3f},"
            f"pct_mean:{summary['percent_mean']:+.2f}%,"
            f"pct_min:{summary['percent_min']:+.2f}%,"
            f"pct_max:{summary['percent_max']:+.2f}%"
        )

    report_stem = f"benchmark-envelope-{args.profile}-runs{args.runs}"
    json_report_path = resolve_output_path(args.report_json, report_stem + ".json", repo_root)
    md_report_path = resolve_output_path(args.report_md, report_stem + ".md", repo_root)

    if json_report_path is not None:
        json_report_path.parent.mkdir(parents=True, exist_ok=True)
        json_report_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"report_json={json_report_path}")

    if md_report_path is not None:
        write_text_report(md_report_path, render_markdown_report(payload))
        print(f"report_md={md_report_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())