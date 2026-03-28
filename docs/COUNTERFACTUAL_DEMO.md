# Counterfactual Demo Guide

Date: 2026-03-28
Goal: compare the same encounter set with mitigation intentionally suppressed versus the normal CASCADE path.

## Why this exists

The standard ready-demo path proves CASCADE works. The counterfactual path proves CASCADE matters.

It runs two scenarios back-to-back:

1. **Suppressed intervention** — the system is forced to require more lead time than the injected encounter allows, so no credited auto-COLA response is expected.
2. **Normal intervention** — the default runtime handles the exact same encounter set.

## Command

```bash
./scripts/run_counterfactual_demo.sh
```

## What the script does

For each case it:

1. starts a fresh container,
2. replays the 3LE demo catalog,
3. injects the same two credited critical encounters,
4. advances the clock through the same five-step sequence,
5. queries the readiness and burn-debug endpoints,
6. prints a side-by-side comparison table.

## Expected interpretation

The exact numbers may vary slightly with local timing, but the meaningful signal should be:

- suppressed case has fewer or zero credited avoids,
- normal case has more credited avoids,
- normal case shows predictive burns and post-mitigation clearance,
- the report prints a positive avoid-delta.

## How suppression works

The suppressed case runs the backend with:

```text
PROJECTBONK_AUTO_COLA_MIN_LEAD_S=3600
```

The injected demo encounters occur about 0.18 hours ahead, so the backend sees the threat but should not have enough allowed lead time to perform the credited mitigation path.

## Judge narration

Suggested line:

"This is the same scene twice. On the left we intentionally make auto-intervention infeasible by policy. On the right we let CASCADE operate normally. The difference between those two outcomes is the value of the system, not just the elegance of the visualization."

## Related assets

- `scripts/run_ready_demo.sh`
- `scripts/check_demo_readiness.py`
- `docs/DEMO_STORYBOARD.md`
- `docs/PS_EVIDENCE_INDEX.md`