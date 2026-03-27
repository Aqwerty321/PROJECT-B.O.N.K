# Benchmark Workflow

This document describes the benchmark changes added for hot-path validation and regression safety.

## What Changed

The Phase 3 synthetic benchmark now emits deterministic correctness signals in addition to latency metrics.

- `tools/phase3_tick_benchmark.cpp` now accepts an explicit RNG seed and start epoch.
- The benchmark now prints a `final_state_fingerprint` plus aggregate end-state counters and sums.
- `scripts/benchmark_compare.py` now builds both baseline and current trees in Release, runs the same seeded scenario in both binaries, and refuses to compare against a non-Release current build directory.
- The compare script now supports named profiles and can write JSON and Markdown reports.

## Why This Exists

Pure timing numbers are not enough when optimizing the simulation engine. A useful benchmark for this repo must answer two separate questions:

1. Did the optimized code produce the same simulation result on the same input?
2. If yes, how much faster or slower is it in a comparable Release build?

The benchmark workflow is designed to answer both in one command.

## Benchmark Profiles

The compare script exposes the following profiles:

| Profile | Scenario | Purpose |
| --- | --- | --- |
| `smoke` | 50 sats / 10K debris / 5 warmup / 10 measure | Quick sanity check |
| `default` | 50 sats / 10K debris / 10 warmup / 40 measure | Normal development comparison |
| `long` | 50 sats / 10K debris / 20 warmup / 100 measure | Lower-noise measurement |
| `stress` | 100 sats / 20K debris / 10 warmup / 40 measure | Heavier scaling check |
| `real-smoke` | Real catalog, 50 sats / 10K debris / 5 warmup / 10 measure | Quick real-data sanity check |
| `real-long` | Real catalog, 50 sats / 10K debris / 10 warmup / 30 measure | Lower-noise real-data check |

You can still override any profile field on the command line.

## CI Coverage

GitHub Actions now runs:

```bash
python3 ./scripts/benchmark_compare.py \
  --profile smoke \
  --current-build-dir build \
  --report-json build/benchmark-reports \
  --report-md build/benchmark-reports
```

That CI step uses the already-configured Release `build/` directory, verifies deterministic output equivalence against baseline `HEAD`, and uploads the generated reports as workflow artifacts.

A separate workflow at `.github/workflows/benchmark-nightly.yml` runs `long` and `real-smoke` benchmark comparisons on a schedule or manual dispatch, and also runs a 5-sample `real-long` envelope job that uploads JSON and Markdown summary artifacts.

## Recommended Commands

### 1. Run the current benchmark only

```bash
cmake -S . -B build-benchmark-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-benchmark-release --target phase3_tick_benchmark -j$(nproc)
./build-benchmark-release/phase3_tick_benchmark 50 10000 10 40 30 20260317 1773292800
```

### 2. Compare baseline vs current with the default profile

```bash
python scripts/benchmark_compare.py
```

### 3. Run a lower-noise comparison and save reports

```bash
python scripts/benchmark_compare.py \
  --profile long \
  --report-json docs/benchmarks \
  --report-md docs/benchmarks
```

This writes timestamped report files into `docs/benchmarks/`.

### 4. Compare real catalog baseline vs current

```bash
python scripts/benchmark_compare.py \
  --profile real-smoke \
  --report-json docs/benchmarks \
  --report-md docs/benchmarks
```

By default this uses `data.txt`. You can override that with `--data-path /absolute/path/to/catalog.json`.

### 5. Measure a local timing envelope

```bash
python scripts/benchmark_envelope.py --profile real-long --runs 5
```

This reruns `benchmark_compare.py`, requires every run to return `MATCH`, and prints mean/min/max/stdev summaries for each timing metric. Use it when a single `real-long` run is too noisy to decide whether an optimization is real.

To save the envelope as artifacts:

```bash
python scripts/benchmark_envelope.py \
  --profile real-long \
  --runs 5 \
  --report-json docs/benchmarks \
  --report-md docs/benchmarks
```

## Output Interpretation

### Correctness

- `correctness_status=MATCH` means all non-timing metrics matched exactly.
- `final_state_fingerprint` is a deterministic hash over object IDs, types, statuses, state vectors, telemetry epochs, and derived orbital elements.
- If correctness fails, the script prints per-key mismatches and exits non-zero.

For real-catalog comparisons, the same fingerprint rule applies after the catalog has been parsed and loaded into the in-memory state store.

### Performance

- Timing deltas are always `current - baseline`.
- Negative values are improvements.
- For noisy local machines, prefer the `long` profile before deciding whether a change is a real regression.
- When local noise is high, prefer `scripts/benchmark_envelope.py --profile real-long --runs 5` over a single benchmark comparison.

## Important Caveat

Do not reuse the workspace `build/` directory for benchmark comparisons unless it is already a Release build. In this repo, `build/` may be cached as Debug during development, which invalidates performance comparisons. The compare script defaults to `build-benchmark-release` specifically to avoid that mistake.

## Verified Local Result

Verified on 2026-03-27 using:

```bash
python scripts/benchmark_compare.py --profile long
```

Observed result:

- correctness matched exactly
- mean tick improved from `27.589 ms` to `26.280 ms`
- p95 improved from `37.169 ms` to `31.958 ms`

## When To Use This

Use this workflow when:

- changing broad-phase candidate generation
- changing narrow-phase screening or MOID logic
- changing propagation hot paths
- validating an optimization before merging

Do not use this workflow as a substitute for the safety gates. Keep running the existing CTest and gate scripts for behavioral validation beyond the synthetic benchmark.
