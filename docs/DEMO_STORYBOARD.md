# CASCADE 5-Minute Demo Storyboard

Date: 2026-03-28
Goal: keep the live judging flow under five minutes while making the system's technical edge obvious.

## Demo objective

Show one complete, believable story:

1. real catalog data is loaded,
2. a dangerous conjunction appears,
3. CASCADE autonomously plans and executes mitigation,
4. the result is machine-verified,
5. the dashboard ties the runtime behavior back to the scoring criteria.

## Pre-demo setup

Run this before judges arrive:

```bash
git lfs pull
docker compose up --build -d
./scripts/run_ready_demo.sh
```

Fallback proof paths:

- If live demo timing drifts: keep `./scripts/run_ready_demo.sh` ready in another terminal.
- If a judge asks "what changes if the system does nothing?": run `./scripts/run_counterfactual_demo.sh`.
- If a judge asks about rendering/performance evidence: run `./scripts/capture_fps_evidence.sh` after the ready-demo state is up.

## Live script

### 0:00-0:30 — Problem framing

Say:

"CASCADE is an autonomous LEO constellation manager. It ingests real catalog traffic, screens conjunctions with a conservative zero-false-negative pipeline, plans evasion burns under propulsion and ground-station constraints, and then recovers the satellite back toward its assigned slot."

Show:

- `#/command`
- overall fleet posture
- object counts
- active warning counts

### 0:30-1:15 — Real-data scene

Say:

"This is not a toy scene. We prime the system with a real 3LE catalog, promote real payloads into the operator fleet, and let the backend propagate both fleet assets and debris through the same runtime."

Show:

- `#/track`
- the constellation markers
- debris density
- trail and forecast overlays
- terminator overlay

If needed, narrate the command that produced it:

```bash
python3 scripts/replay_data_catalog.py --data 3le_data.txt --api-base http://localhost:8000 --satellite-mode catalog --operator-sats 10
```

### 1:15-2:00 — Threat appears

Say:

"We then inject two calibrated future critical encounters. The point is not a canned animation. The point is that the backend sees them through the same predictive conjunction machinery it uses for any other threat."

Show:

- `#/threat`
- critical dots on the bullseye
- severity legend
- time-to-TCA framing

### 2:00-3:00 — Autonomous response

Say:

"Once we advance the clock, CASCADE takes over. It promotes the threat, picks an auto-COLA burn, validates command timing and constraints, executes mitigation, and then schedules recovery behavior."

Show:

- `#/burn-ops`
- predictive burn markers
- blackout/conflict state
- decision explainer panel

Say explicitly:

"This panel shows why the burn fired: trigger debris, predicted miss distance, delta-v cost, upload station, and post-mitigation result."

### 3:00-3:45 — Machine-verified result

Say:

"We do not ask the judges to trust the visualization alone. The ready-demo script finishes with a machine-readable readiness report and burn-level mitigation evidence."

Show terminal output from:

```bash
python3 scripts/check_demo_readiness.py --api-base http://localhost:8000
curl -s http://localhost:8000/api/debug/burns | python3 -m json.tool
```

Call out:

- `verdict: ready`
- `collisions_avoided >= 2`
- `collision_avoided: true`
- improved mitigation miss distance

### 3:45-4:30 — Scorecard close

Say:

"The dashboard closes the loop against the hackathon rubric: safety, fuel, uptime, speed, visualization, and code quality are all tied back to live runtime metrics."

Show:

- `#/scorecard`
- weighted readiness
- live payload
- safety proof
- latency signal

### 4:30-5:00 — Final differentiator

Say:

"If you want the strongest proof that CASCADE matters, we can run the exact same encounter set in counterfactual mode, where intervention is intentionally suppressed. The delta between those two runs is the value of the system."

Point to:

- `./scripts/run_counterfactual_demo.sh`
- `docs/COUNTERFACTUAL_DEMO.md`

## One-line answers for likely judge questions

- Why should we trust the safety claim?
  - "Because the pipeline is tested by dedicated false-negative gates and the live demo ends in machine-checked mitigation evidence, not screenshots alone."
- Is this real-data backed?
  - "Yes. The replay flow uses a real catalog source and the ready-demo path starts from that data before the synthetic encounter is introduced."
- What is the main differentiator?
  - "We do not just detect conjunctions; we autonomously mitigate them, recover afterward, and can demonstrate the counterfactual where the same encounter goes unmanaged."
- How do you prove the UI is not cosmetic?
  - "The scorecard, burn explainer, and FPS evidence all pull from live runtime state and recorded proof commands in the repo."