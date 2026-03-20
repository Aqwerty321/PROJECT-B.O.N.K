#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build}"
SUMMARY_JSON="${2:-$BUILD_DIR/phase1_evidence_baseline_summary.json}"

# Benchmark profile defaults (kept small enough for fast iteration, large enough
# for stable comparison trends).
BENCH_RUNS="${BENCH_RUNS:-3}"
BENCH_SATELLITES="${BENCH_SATELLITES:-50}"
BENCH_DEBRIS="${BENCH_DEBRIS:-10000}"
BENCH_WARMUP_TICKS="${BENCH_WARMUP_TICKS:-10}"
BENCH_MEASURE_TICKS="${BENCH_MEASURE_TICKS:-30}"
BENCH_STEP_S="${BENCH_STEP_S:-30}"

if [[ ! "$BUILD_DIR" = /* ]]; then
  BUILD_DIR="$ROOT_DIR/${BUILD_DIR#./}"
fi
if [[ ! "$SUMMARY_JSON" = /* ]]; then
  SUMMARY_JSON="$ROOT_DIR/${SUMMARY_JSON#./}"
fi

mkdir -p "$(dirname "$SUMMARY_JSON")"

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

FALSE_NEG_LOG="$TMP_DIR/false_negative.log"
OBSERVABILITY_LOG="$TMP_DIR/observability.log"
BENCH_CSV="$TMP_DIR/benchmark.csv"

printf 'phase1_evidence_step=narrow_phase_false_negative\n'
"$ROOT_DIR/scripts/narrow_phase_false_negative_gate.sh" "$BUILD_DIR" | tee "$FALSE_NEG_LOG"

printf 'phase1_evidence_step=narrow_phase_uncertainty_observability\n'
"$ROOT_DIR/scripts/narrow_phase_uncertainty_observability_gate.sh" "$BUILD_DIR" | tee "$OBSERVABILITY_LOG"

printf 'phase1_evidence_step=phase3_tick_benchmark\n'
cmake --build "$BUILD_DIR" --target phase3_tick_benchmark

: > "$BENCH_CSV"
for mode in 0 1; do
  for run in $(seq 1 "$BENCH_RUNS"); do
    BENCH_LOG="$TMP_DIR/bench_mode${mode}_run${run}.log"
    PROJECTBONK_BROAD_I_NEIGHBOR_FILTER="$mode" \
      "$BUILD_DIR/phase3_tick_benchmark" \
      "$BENCH_SATELLITES" \
      "$BENCH_DEBRIS" \
      "$BENCH_WARMUP_TICKS" \
      "$BENCH_MEASURE_TICKS" \
      "$BENCH_STEP_S" > "$BENCH_LOG"

    python3 - <<'PY' "$BENCH_LOG" "$BENCH_CSV" "$mode" "$run"
import re
import sys

log_path, csv_path, mode, run = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
text = open(log_path, "r", encoding="utf-8").read()

def read_num(name: str) -> float:
    m = re.search(rf'^{re.escape(name)}=(\S+)$', text, re.M)
    if not m:
        raise SystemExit(f"missing_metric={name} in {log_path}")
    return float(m.group(1))

row = {
    "mode": mode,
    "run": run,
    "tick_ms_mean": read_num("tick_ms_mean"),
    "tick_ms_median": read_num("tick_ms_median"),
    "tick_ms_p95": read_num("tick_ms_p95"),
    "broad_candidates_total": int(read_num("broad_candidates_total")),
    "broad_pairs_considered_total": int(read_num("broad_pairs_considered_total")),
    "narrow_pairs_checked_total": int(read_num("narrow_pairs_checked_total")),
}

with open(csv_path, "a", encoding="utf-8") as f:
    f.write(
        f"{row['mode']},{row['run']},{row['tick_ms_mean']:.6f},{row['tick_ms_median']:.6f},"
        f"{row['tick_ms_p95']:.6f},{row['broad_candidates_total']},"
        f"{row['broad_pairs_considered_total']},{row['narrow_pairs_checked_total']}\n"
    )
PY
  done
done

python3 - <<'PY' "$FALSE_NEG_LOG" "$OBSERVABILITY_LOG" "$BENCH_CSV" "$SUMMARY_JSON" \
  "$BENCH_RUNS" "$BENCH_SATELLITES" "$BENCH_DEBRIS" "$BENCH_WARMUP_TICKS" "$BENCH_MEASURE_TICKS" "$BENCH_STEP_S"
import json
import re
import sys
from collections import defaultdict

(
    false_neg_log,
    observability_log,
    bench_csv,
    summary_json,
    bench_runs,
    bench_sats,
    bench_debris,
    bench_warmup,
    bench_measure,
    bench_step_s,
) = sys.argv[1:11]

false_neg_text = open(false_neg_log, "r", encoding="utf-8").read()
obs_text = open(observability_log, "r", encoding="utf-8").read()

def read_required(text: str, key: str) -> str:
    m = re.search(rf'^{re.escape(key)}=(\S+)$', text, re.M)
    if not m:
        raise SystemExit(f"missing_required_metric={key}")
    return m.group(1)

fn_total = int(read_required(false_neg_text, "false_negative_sats_total"))
fn_result = read_required(false_neg_text, "narrow_phase_false_negative_gate_result")
obs_result = read_required(obs_text, "narrow_phase_uncertainty_observability_gate_result")
reason_codes_ok = read_required(obs_text, "reason_codes_strict_enum_validation")
moid_shadow_rejected_total = int(read_required(obs_text, "narrow_moid_shadow_rejected_pairs_total"))
promoted_total = int(read_required(obs_text, "narrow_uncertainty_promoted_pairs_total"))
full_refined_total = int(read_required(obs_text, "narrow_full_refined_pairs_total"))

runs = defaultdict(list)
with open(bench_csv, "r", encoding="utf-8") as f:
    for line in f:
        mode_s, run_s, mean_s, med_s, p95_s, cand_s, pairs_s, narrow_s = line.strip().split(",")
        runs[int(mode_s)].append(
            {
                "run": int(run_s),
                "tick_ms_mean": float(mean_s),
                "tick_ms_median": float(med_s),
                "tick_ms_p95": float(p95_s),
                "broad_candidates_total": int(cand_s),
                "broad_pairs_considered_total": int(pairs_s),
                "narrow_pairs_checked_total": int(narrow_s),
            }
        )

for mode in (0, 1):
    if len(runs[mode]) == 0:
        raise SystemExit(f"missing_benchmark_runs_for_mode={mode}")


def avg(mode: int, key: str) -> float:
    vals = [r[key] for r in runs[mode]]
    return sum(vals) / float(len(vals))

base_mean = avg(0, "tick_ms_mean")
opt_mean = avg(1, "tick_ms_mean")
base_cand = avg(0, "broad_candidates_total")
opt_cand = avg(1, "broad_candidates_total")

mean_delta_pct = 0.0 if base_mean == 0.0 else ((opt_mean - base_mean) / base_mean) * 100.0
cand_delta_pct = 0.0 if base_cand == 0.0 else ((opt_cand - base_cand) / base_cand) * 100.0

status = "PASS"
if fn_result != "PASS" or fn_total != 0:
    status = "FAIL"
if obs_result != "PASS" or reason_codes_ok != "PASS":
    status = "FAIL"

payload = {
    "schema_version": 1,
    "status": status,
    "gates": {
        "narrow_phase_false_negative": {
            "result": fn_result,
            "false_negative_sats_total": fn_total,
        },
        "narrow_phase_uncertainty_observability": {
            "result": obs_result,
            "reason_codes_strict_enum_validation": reason_codes_ok,
            "narrow_uncertainty_promoted_pairs_total": promoted_total,
            "narrow_full_refined_pairs_total": full_refined_total,
            "narrow_moid_shadow_rejected_pairs_total": moid_shadow_rejected_total,
        },
    },
    "benchmark": {
        "profile": {
            "runs_per_mode": int(bench_runs),
            "satellites": int(bench_sats),
            "debris": int(bench_debris),
            "warmup_ticks": int(bench_warmup),
            "measure_ticks": int(bench_measure),
            "step_seconds": float(bench_step_s),
        },
        "mode_0": {
            "runs": runs[0],
            "avg_tick_ms_mean": avg(0, "tick_ms_mean"),
            "avg_tick_ms_p95": avg(0, "tick_ms_p95"),
            "avg_broad_candidates_total": avg(0, "broad_candidates_total"),
            "avg_narrow_pairs_checked_total": avg(0, "narrow_pairs_checked_total"),
        },
        "mode_1": {
            "runs": runs[1],
            "avg_tick_ms_mean": avg(1, "tick_ms_mean"),
            "avg_tick_ms_p95": avg(1, "tick_ms_p95"),
            "avg_broad_candidates_total": avg(1, "broad_candidates_total"),
            "avg_narrow_pairs_checked_total": avg(1, "narrow_pairs_checked_total"),
        },
        "deltas_mode1_minus_mode0": {
            "avg_tick_ms_mean_pct": mean_delta_pct,
            "avg_broad_candidates_total_pct": cand_delta_pct,
        },
    },
}

with open(summary_json, "w", encoding="utf-8") as f:
    json.dump(payload, f, indent=2)
    f.write("\n")

print(f"phase1_evidence_status={status}")
print(f"phase1_evidence_summary_json={summary_json}")

if status != "PASS":
    raise SystemExit(1)
PY

echo "phase1 evidence baseline: PASS"
