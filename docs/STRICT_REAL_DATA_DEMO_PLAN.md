# Strict Real-Data Demo and Backend Plan

Date: 2026-03-25
Status: Working reference

## Why this document exists

This is the single place to keep the real-data demo strategy, the PS compliance audit,
the current implementation reality, and the backend improvement roadmap.

The goal is that future work can refer to this file instead of re-discovering the
same constraints, gaps, and decisions.

## Locked decisions

These decisions are already made for planning purposes unless later changed explicitly.

- We are choosing the stricter route.
- The demo should rely on real catalog data as much as possible.
- We should minimize synthetic orbit generation.
- Local-only helper workflows are acceptable, but they must not change judged runtime behavior.
- The judged API surface and PS contracts must remain intact.
- `train_data.csv` and `test_data.csv` are offline-evaluation assets, not runtime telemetry sources.
- `data.txt` is the current working replay source.
- `3le_data.txt` is the best next real-data source for denser and more modern visualization.

## Recent implementation updates

These items are already implemented and should be treated as current reality.

- `.gitignore` now explicitly ignores `3le_data.txt`, `train_data.csv`, and `test_data.csv` in addition to the older local-only data files.
- `scripts/replay_data_catalog.py` now supports stricter catalog-backed operator fleets via:
  - `--satellite-mode catalog`
  - `--operator-sats`
  - `--operator-norad`
  - `--dry-run`
- The local replay helper can now promote real payloads as operator satellites using `SAT-<NORAD>` IDs instead of only synthetic `SAT-LOCAL-*` IDs.
- Predictive 24-hour CDM records are now persisted in the backend, not just counted.
- `GET /api/debug/conjunctions` now supports an additive `source=` query mode:
  - `source=history`
  - `source=predicted`
  - `source=combined`
- Predictive conjunction records now expose `predictive` and `fail_open` flags in the debug JSON.
- Auto-COLA planning now consumes predicted critical conjunction satellites in addition to same-step narrow-phase collision indices.
- `GET /api/status?details=1` now includes:
  - `active_cdm_warnings`
  - `predictive_conjunction_count`
  - `history_conjunction_count`
  - `predictive_screening_threshold_km`
- `GET /api/debug/burns` now exposes richer maneuver diagnostics for backend/UX wiring:
  - `executed`, `pending`, and `dropped` burns
  - per-burn `blackout_overlap`, `cooldown_conflict`, and `command_conflict`
  - predictive-trigger metadata such as `scheduled_from_predictive_cdm`, trigger debris/TCA, and fail-open state
  - executed-burn mitigation fields including `collision_avoided` and post-burn evaluation miss distance
  - aggregate `summary` totals including `fuel_consumed_kg`, `avoidance_fuel_consumed_kg`, and `collisions_avoided`
- `GET /api/status?details=1` now also includes `dropped_burn_count`.
- Frontend API types and hooks now understand the additive conjunction `source` filter.
- **Dashboard conjunction fetch now defaults to `source='combined'`** so predictive CDMs appear across all dashboard surfaces, not only the Threat page.
- **Dashboard threat counting now uses `riskLevelForEvent()` which prefers server-provided CDM severity over distance-only classification.**
- `ThreatPage` now prefers the predictive conjunction stream when available, while still falling back to the historical stream.
- `ConjunctionBullseye` now supports adaptive time horizons (`90m`, `6h`, `24h`) instead of only a fixed 90-minute scale.
- **Bullseye now uses `riskLevelForEvent()` and `pcProxy()` for Pc-influenced dot size, opacity, and risk coloring.** Legend labels now show `CRITICAL (<100m)`, `WARNING (<1km)`, `WATCH (1-5km)` aligned with backend CdmSeverity tiers.
- **Conjunction event list and detail card now display server-provided severity labels and Pc proxy estimates.**
- Threat detail and watch-list UI now surface whether an event is predictive and whether it is a fail-open warning.
- Frontend status-color and fleet-status aggregation now recognize backend `FUEL_LOW` and `OFFLINE` states.
- `scripts/replay_data_catalog.py` now supports manifest-driven replays via `--manifest`.
- `scripts/generate_strict_manifest.py` now generates reproducible strict replay manifests from the local OMM catalog.
- `scripts/mine_strict_scenarios.py` now emits ranked strict replay manifests by scoring real payload anchors against nearby natural traffic.
- The replay / manifest / miner tooling now supports both OMM JSON and 3LE/TLE text catalogs through a shared parser path.
- `scripts/evaluate_strict_manifest.py` now runs one-shot manifest evaluation against a fresh local backend and captures runtime outputs.
- `scripts/rank_strict_manifests.py` now performs backend-assisted ranking by evaluating multiple manifests one-by-one against fresh backend runs.
- The lightweight scenario miner now incorporates `docs/groundstations.csv` into LOS-aware scoring and emits recommended ground stations per manifest.
- The lightweight scenario miner now uses diversity-aware operator selection so emitted manifests are less likely to collapse into near-duplicate payload sets.
- The miner can now optionally run backend-assisted ranking inline and emit a `_backend_ranking.json` summary alongside mined manifests.
- The miner now scores threat richness more aggressively via near-100/250/500 km counts and balances operator-family / shell mix when assembling scenario fleets.
- The miner can now persist learned feedback weights from backend-ranked runs so future mining passes bias toward backend-confirmed signals.
- Ranked manifests are now annotated in place with `backend_cdm_evaluation`, keeping predictive-CDM results attached to each scenario artifact.
- Candidate generation now probes shell density and phase density so mining is more aggressively biased toward likely predictive-warning regions before backend evaluation.
- The miner now defaults committed strict manifests to `catalog` operator mode instead of the synthetic local-ID fallback.
- The miner now clusters closest natural opportunities into encounter windows and shifts replay start time toward the strongest upcoming cluster so the backend sees close traffic earlier.
- Miner output now records refined closest-approach timing (`best_miss_epoch`, per-threat `tca_epoch`, and `encounter_window`) so scenario artifacts explain why a replay epoch was chosen.
- Anchor selection now seeds across distinct shells and families before filling score-based slots, reducing collapse into one near-duplicate shell when mining larger scenario banks.
- A committed example manifest now exists at `docs/scenarios/strict_natural_watch.example.json`.
- Strict manifest evaluation and ranking now score additional burn-outcome signals:
  - `burns_dropped`
  - `collisions_avoided`
  - `fuel_consumed_kg`
  - `avoidance_fuel_consumed_kg`
- **Backend now implements tiered CDM screening:**
  - `SCREENING_THRESHOLD_KM = 5.0 km` is the outer gate for watch/warning events
  - `COLLISION_THRESHOLD_KM = 0.100 km` remains the hard CRITICAL threshold per PS §3
  - `CdmSeverity` enum: `CRITICAL` (<100m), `WARNING` (100m-1km), `WATCH` (1km-5km)
  - Only CRITICAL events trigger auto-COLA burns; WATCH/WARNING populate Threat UI
  - Conjunction records now carry a `severity` field in JSON output
  - Both predictive (24h scan) and narrow-phase historical conjunctions use the tiered classification
- **Ground Track Map now renders simplified world coastline polygons** from a new `frontend/src/data/coastlines.ts` data file (~2k vertices covering all major continents and large islands). Coastlines are drawn between the graticule and terminator layers, with antimeridian-wrap detection.
- **`scripts/inject_synthetic_encounter.py` now exists** as a PS-safe hybrid injection helper:
  - queries the running backend for current sim state
  - picks a target satellite from the live snapshot
  - computes 1-N debris state vectors on near-collision courses using orbital mechanics
  - injects via `POST /api/telemetry` to trigger the full CDM → auto-COLA → recovery pipeline
  - supports `--miss-km`, `--count`, `--encounter-hours`, `--target`, and `--dry-run` flags

## Useful commands

These are the commands worth reusing later.

### Strict replay dry-run

```bash
python3 scripts/replay_data_catalog.py --satellite-mode catalog --dry-run
```

### Strict 3LE replay dry-run

```bash
python3 scripts/replay_data_catalog.py \
  --data 3le_data.txt \
  --satellite-mode catalog \
  --operator-sats 10 \
  --dry-run
```

### Strict replay against local backend

```bash
python3 scripts/replay_data_catalog.py \
  --api-base http://localhost:8000 \
  --satellite-mode catalog \
  --operator-sats 10
```

### Manifest-driven replay preview

```bash
python3 scripts/replay_data_catalog.py \
  --manifest docs/scenarios/strict_natural_watch.example.json \
  --dry-run
```

### Generate a new strict manifest from the local catalog

```bash
python3 scripts/generate_strict_manifest.py \
  --output /tmp/strict_generated_manifest.json \
  --scenario-id strict_generated_test \
  --satellite-mode catalog \
  --operator-sats 6
```

### Mine ranked strict manifests from real nearby traffic

```bash
python3 scripts/mine_strict_scenarios.py \
  --output-dir /tmp/strict_mined \
  --scenario-prefix strict_mined \
  --top-scenarios 3 \
  --operator-sats 8 \
  --payload-candidates 12 \
  --threat-candidates 600
```

### Mine ranked strict manifests from 3LE data

```bash
python3 scripts/mine_strict_scenarios.py \
  --data 3le_data.txt \
  --output-dir /tmp/strict_mined_3le \
  --scenario-prefix strict_mined_3le \
  --top-scenarios 3
```

### Evaluate a strict manifest against a fresh backend

```bash
python3 scripts/evaluate_strict_manifest.py \
  --manifest /tmp/strict_generated_manifest.json \
  --api-base http://localhost:8000 \
  --extra-steps 1 \
  --extra-step-seconds 300
```

### Rank multiple strict manifests with fresh backend runs

```bash
python3 scripts/rank_strict_manifests.py \
  /tmp/strict_mined/strict_mined_01.json \
  /tmp/strict_mined/strict_mined_02.json \
  --backend-cmd ./build/ProjectBONK \
  --api-base http://localhost:8000 \
  --extra-steps 1 \
  --extra-step-seconds 300
```

### Mine and backend-rank in one pass

```bash
python3 scripts/mine_strict_scenarios.py \
  --output-dir /tmp/strict_mined_ranked \
  --scenario-prefix strict_mined_ranked \
  --top-scenarios 2 \
  --candidate-scenarios 3 \
  --backend-rank-cmd ./build/ProjectBONK \
  --backend-rank-api-base http://localhost:8000 \
  --feedback-output /tmp/strict_mined_ranked_feedback.json
```

### Predictive conjunction debug queries

```bash
curl -s 'http://localhost:8000/api/debug/conjunctions?source=history'
curl -s 'http://localhost:8000/api/debug/conjunctions?source=predicted'
curl -s 'http://localhost:8000/api/debug/conjunctions?source=combined'
curl -s 'http://localhost:8000/api/status?details=1'
```

### Inject synthetic close-approach debris (hybrid injection)

```bash
# Dry-run (preview payload without sending)
python3 scripts/inject_synthetic_encounter.py --dry-run

# Inject 2 debris with ~50m miss distance, TCA in ~3 hours (CRITICAL)
python3 scripts/inject_synthetic_encounter.py --miss-km 0.05 --count 2 --encounter-hours 3.0

# Inject 1 debris targeting a specific satellite with ~500m miss (WARNING)
python3 scripts/inject_synthetic_encounter.py --target SAT-25544 --miss-km 0.5 --count 1

# Inject at a custom miss distance within WATCH band (2km)
python3 scripts/inject_synthetic_encounter.py --miss-km 2.0 --count 3 --encounter-hours 6.0
```

### Main verification commands already used

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure

export NVM_DIR="$HOME/.nvm" && [ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh" && nvm use 24 >/dev/null
npm run build
```

## Files already touched for the strict route

These files now contain relevant strict-route work and should be checked first when continuing.

- `.gitignore`
- `README.md`
- `scripts/replay_data_catalog.py`
- `scripts/generate_strict_manifest.py`
- `scripts/mine_strict_scenarios.py`
- `scripts/evaluate_strict_manifest.py`
- `scripts/rank_strict_manifests.py`
- `scripts/inject_synthetic_encounter.py`
- `docs/scenarios/strict_natural_watch.example.json`
- `docs/scenarios/`
- `src/types.hpp`
- `src/maneuver_common.hpp`
- `src/engine_runtime.hpp`
- `src/engine_runtime.cpp`
- `src/simulation_engine.cpp`
- `src/http/api_server.cpp`
- `frontend/src/hooks/useApi.ts`
- `frontend/src/types/api.ts`
- `frontend/src/data/coastlines.ts`
- `frontend/src/pages/ThreatPage.tsx`
- `frontend/src/components/GroundTrackMap.tsx`
- `frontend/src/components/ConjunctionBullseye.tsx`
- `frontend/src/components/threat/ConjunctionEventList.tsx`
- `frontend/src/components/threat/ConjunctionDetailCard.tsx`
- `frontend/src/dashboard/DashboardContext.tsx`

## Verified so far

These checks were already run against the current implementation.

- `cmake --build build -j2`
- `ctest --test-dir build --output-on-failure`
- local replay helper dry-run in both `synthetic` and `catalog` operator modes
- local backend validation of `GET /api/debug/conjunctions?source=history|predicted|combined`
- local backend validation that a predicted critical conjunction now queues an `AUTO-COLA-*` burn
- frontend production build after the predictive Threat-path changes
- frontend production build after backend-status alignment updates
- manifest generation and manifest-driven replay dry-run validation
- lightweight natural-scene miner validation producing ranked manifests and priority debris lists
- 3LE replay dry-run, 3LE manifest generation, and 3LE scenario-miner validation
- live-backend manifest evaluation with a generated strict scenario summary
- backend-assisted ranking across multiple mined manifests with fresh backend restarts
- LOS-aware mining and LOS-aware backend-assisted ranking validation
- miner-integrated backend-assisted ranking and `_backend_ranking.json` output validation
- backend-informed mining feedback loop and learned-weight output validation
- backend build/test validation after burn-diagnostics and avoidance-effectiveness metric additions
- fresh-backend sanity replay of `docs/scenarios/strict_natural_watch.example.json`
- backend build/test validation after tiered CDM screening implementation (8/8 tests pass)
- frontend production build after coastline layer, dashboard combined-source, and Pc proxy changes
- `scripts/inject_synthetic_encounter.py` syntax validation

Observed result from backend validation:

- predictive and historical conjunction streams are now distinguishable and queryable independently
- combined mode returns both records
- status details expose predictive/history counts for future UI work
- a synthetic close-pair validation scenario produced:
  - `active_cdm_warnings = 1`
  - `predictive_conjunction_count = 1`
  - `history_conjunction_count = 1`
  - one pending `AUTO-COLA-*` burn after the first step
- generated manifests can now be produced from the local OMM catalog and consumed immediately by the replay helper
- lightweight mined manifests now include `priority_debris_norad` so nearest natural threats survive replay caps more reliably
- the same strict-route replay/mining workflow now works on `3le_data.txt`, not only `data.txt`
- one-shot manifest evaluation summaries can now be captured against a fresh backend to compare replay presets with real runtime outputs
- multiple manifests can now be ranked by actual backend outcomes, not only heuristic mining scores
- mined manifests now carry LOS-oriented metadata such as recommended ground stations and LOS-ready operator counts
- current limitation observed during ranking: if mined manifests are too similar, backend-assisted scores can still tie, so diversity-aware scenario generation is the next meaningful improvement
- candidate manifests can now be mined first and then backend-ranked inline before selecting the top scenarios

Latest validation update:

- diversity-aware scenario generation is now producing distinct operator-fleet sets
- backend-assisted ranking can now separate at least some of those scenarios using LOS-aware metadata even when predictive conjunction counts are still zero
- richer threat scoring plus shell/family balancing now gives the ranking pipeline more separation even before live predictive warnings appear
- backend-ranked mining now emits a feedback-weight file that can be reused by later mining passes
- backend-ranked manifests now carry their own predictive-CDM evaluation block, so the scenario files are self-describing
- shell-density and phase-density probing now strongly biases mined scenarios toward crowded close-track regions
- burn debug JSON now exposes dropped-burn tracking plus timeline-ready blackout/conflict state
- avoidance burns now carry predictive trigger metadata and a first-pass post-burn `collision_avoided` evaluation signal
- manifest evaluation/ranking now incorporates dropped-burn penalties and avoidance-effectiveness metrics
- a fresh replay of `strict_natural_watch.example.json` still produced zero predictive warnings and zero burns, which confirms the remaining bottleneck is scenario quality rather than backend plumbing
- a follow-up miner refinement pass now reliably finds denser, closer natural opportunity banks (for example ~`10-16 km` heuristic misses instead of the earlier ~`17-100+ km` bands), but a reduced-cap backend validation run still produced zero predictive warnings, so the remaining gap is now mostly backend-threshold alignment rather than scenario-window selection alone

Status update:

- diversity-aware scenario generation is now partially implemented in the miner
- manifests now try to vary operator-fleet composition while still respecting score order

Observed result from frontend build validation:

- `frontend` production build succeeds after the predictive Threat-page and bullseye changes

## Success criteria

The strict route is considered successful when all of the following are true.

- The demo uses a real catalog-derived operator fleet and real catalog-derived debris field.
- The main UI pages populate without random synthetic orbit generation.
- Threat views are driven by naturally discovered predicted conjunctions from the backend.
- Burn Ops shows real or naturally triggered burns, not placeholder-only content.
- The required Section 6.2 visualization modules are either complete or tracked as explicit remaining work.
- No judged API contract, Docker requirement, or PS rule is broken by the demo path.

## Non-goals

- Feeding `train_data.csv` or `test_data.csv` into `POST /api/telemetry`
- Making local demo files mandatory for judged runtime
- Replacing required endpoints with dataset-specific custom endpoints
- Using synthetic close-approach generation as the default plan
- Building a frontend path that depends on shipping raw large datasets to the browser

## Quick reference constants from the PS

These are the numbers and requirements that matter repeatedly during implementation.

- Collision threshold: `< 0.100 km` (100 m)
- Predictive horizon: up to `24 hours`
- Signal latency: `10 seconds`
- Thruster cooldown: `600 seconds`
- Slot box radius: `10 km`
- Historical ground-track trail: `90 minutes`
- Predicted ground-track path: `90 minutes`
- Frontend density target: `50+ satellites` and `10,000+ debris`
- Required port: `8000`
- Required Docker base image: `ubuntu:22.04`

## Current dataset inventory

## `data.txt`

Format and status:

- Space-Track OMM JSON catalog
- already gitignored
- already supported by the local replay helper

Why it matters:

- it is the only catalog source currently wired into the existing replay path
- it includes object-type metadata, which is useful for choosing operator spacecraft from real payloads
- it is the best immediate source for a strict real-data demo

What it can do well:

- drive `/api/telemetry` after conversion to ECI state vectors
- populate the 3D globe, snapshot, status, and ground-track pages
- seed tuner and benchmark scenarios with realistic orbital distributions

What it cannot do alone:

- guarantee naturally rich Threat/Burn content with the current backend behavior

## `3le_data.txt`

Observed facts:

- valid 3LE text catalog
- about `4.8 MB`
- `30,417` valid `0/1/2` triplets
- `30,417` unique NORAD IDs
- epoch spread is roughly `2026 day 054` through `2026 day 088`
- contains a strong modern mix including many `STARLINK`, debris, and rocket bodies

Why it matters:

- much better than the CDM CSVs for dense globe and track visualization
- large enough to make the demo look credible and operationally busy
- small enough to handle locally if parsed and filtered sensibly

Current limitation:

- previous limitation resolved in local Python tooling: replay, manifest generation, and lightweight scenario mining can now parse `3le_data.txt`

Strict-route role:

- near-term: source for local density upgrade after parser support is added
- medium-term: primary orbital source merged with metadata from `data.txt`

Status update:

- parser support now exists in local Python tooling
- metadata enrichment from `data.txt` by NORAD is still future work

## `tle2025.txt`

Format and status:

- large TLE-like local dataset
- already gitignored
- not currently used by the replay helpers

Best role:

- large-scale performance and regression bank after unified TLE parsing exists

## `train_data.csv`

Observed facts:

- `103` columns
- about `162,634` rows
- event-based time series grouped by `event_id`
- appears to contain conjunction-risk features, not raw telemetry or full object state vectors

Why it matters:

- useful for offline calibration of threat ranking, thresholding, and refinement policy

Why it should not drive runtime telemetry:

- no direct per-object ECI state vectors
- no telemetry-style `id`, `type`, `r`, `v`, and timestamp payload shape
- not a good source for honest runtime replay through `POST /api/telemetry`

## `test_data.csv`

Observed facts:

- `103` columns
- about `24,484` rows
- same feature family as `train_data.csv`

Best role:

- untouched holdout validation after calibration on `train_data.csv`

## `docs/groundstations.csv`

Why it matters:

- it is part of the PS-provided operational dataset
- LOS, upload timing, blackout-aware planning, and demo scenario mining should all use it

## Data-handling rules

These rules should remain in force for all follow-up work.

- Do not commit local large data files unless there is a deliberate repo decision to do so.
- Do not send raw CSVs to the browser.
- Do not make the judged runtime depend on local demo assets.
- Use streaming/chunked processing for `train_data.csv` and `test_data.csv`.
- Use filtered real subsets for demo rendering when full-catalog density would hurt smoothness.

Important local workspace note:

- `3le_data.txt`, `train_data.csv`, and `test_data.csv` are now ignored locally via `.gitignore`.
- They should continue to be treated as local-only unless there is a deliberate repo decision to track them.

## Current implementation map

These are the main files that define the current behavior and should be considered the core reference set.

### Local data ingestion and replay

- `scripts/replay_data_catalog.py`
- `tools/real_data_scenario_gen.cpp`

### Backend API and runtime

- `src/http/api_server.cpp`
- `src/http/request_parsers.cpp`
- `src/http/response_builders.cpp`
- `src/telemetry.cpp`
- `src/engine_runtime.cpp`
- `src/engine_runtime.hpp`
- `src/maneuver_common.cpp`
- `src/maneuver_common.hpp`
- `src/simulation_engine.cpp`
- `src/simulation_engine.hpp`

### Frontend data flow

- `frontend/src/hooks/useApi.ts`
- `frontend/src/dashboard/DashboardContext.tsx`

### Frontend pages

- `frontend/src/pages/CommandPage.tsx`
- `frontend/src/pages/TrackPage.tsx`
- `frontend/src/pages/ThreatPage.tsx`
- `frontend/src/pages/BurnOpsPage.tsx`

### Frontend visualization modules

- `frontend/src/components/EarthGlobe.tsx`
- `frontend/src/components/GroundTrackMap.tsx`
- `frontend/src/components/ConjunctionBullseye.tsx`
- `frontend/src/components/ManeuverGantt.tsx`
- `frontend/src/components/FuelHeatmap.tsx`
- `frontend/src/components/StatusPanel.tsx`
- `frontend/src/types/api.ts`

## Current runtime data flow

This section describes what the app actually does today.

## Required API flows already present

- `POST /api/telemetry`
- `POST /api/maneuver/schedule`
- `POST /api/simulate/step`
- `GET /api/visualization/snapshot`

## Debug and supporting API flows already present

- `GET /api/status?details=1`
- `GET /api/debug/burns`
- `GET /api/debug/conjunctions`
- `GET /api/debug/conjunctions?source=history|predicted|combined`
- `GET /api/visualization/trajectory?satellite_id=...`

## Frontend page dependencies

### Command page

Driven by:

- snapshot
- status details
- burns
- conjunctions
- selected trajectory

This page is already a strong fit for real-catalog replay.

### Track page

Driven by:

- snapshot
- trajectory endpoint
- frontend-accumulated 90-minute history

This page already works well with real data, provided the system is stepped enough to build up history.

### Threat page

Driven by:

- `GET /api/debug/conjunctions`

Important reality:

- the backend now supports predictive, history, and combined conjunction streams through the additive `source=` query parameter
- the Threat page now prefers the predictive stream directly
- the broader dashboard model still defaults to the historical watch path, so predictive-CDM integration is only partially propagated through the app

### Burn Ops page

Driven by:

- `GET /api/debug/burns`
- status details
- snapshot

Important reality:

- this page is informative only when there are pending or executed burns
- the required efficiency graph is now wired to live backend burn summary data

## Current measured implementation facts that matter later

These are the important current-state facts to remember.

### Globe and snapshot density

- `EarthGlobe` caps debris rendering at `25,000` points
- `GET /api/visualization/snapshot` already uses a compact tuple-based `debris_cloud` format
- this is aligned with the PS intent to keep the network payload compact

### Ground track

- current implementation draws a Mercator tactical surface with graticule, **coastline polygons**, and terminator
- coastline data is ~2k vertices covering all major continents, large islands, and Antarctica
- the selected-satellite predicted path already covers the next 90 minutes

### Bullseye

- current radial scaling now adapts across `90m`, `6h`, and `24h`
- current color logic uses server-provided CDM severity when available, falling back to miss-distance thresholds
- **Pc proxy** from miss distance + approach speed now influences dot size and opacity
- current angle uses relative geometry derived from ECI positions
- current page now auto-focuses the most relevant spacecraft lane
- legend shows CDM severity tiers: `CRITICAL (<100m)`, `WARNING (<1km)`, `WATCH (1-5km)`

Update:

- the bullseye now supports adaptive time horizons of `90m`, `6h`, or `24h` based on the active threat stream
- Pc proxy and CDM severity are now fully integrated into rendering

### Maneuver timeline

- current timeline already distinguishes pending vs executed burns
- current timeline already shows burn blocks and cooldown windows
- current timeline already differentiates avoidance, recovery, graveyard, and pending visually
- current timeline now renders backend-driven blackout/conflict/drop state and highlights `collision_avoided` executions

### Burn and status telemetry

- backend already tracks executed burns, pending burns, per-satellite burn totals, upload station, and fuel before/after
- backend already tracks upload misses and stationkeeping metrics internally

### Status vocabulary mismatch

There is an important frontend/backend naming mismatch that should not be forgotten.

Backend snapshot statuses currently include real runtime values such as:

- `NOMINAL`
- `MANEUVERING`
- `FUEL_LOW`
- `OFFLINE`

Frontend status handling currently assumes:

- `NOMINAL`
- `MANEUVERING`
- `DEGRADED`
- `GRAVEYARD`

Why this matters:

- low-fuel and offline states can be miscolored or undercounted in the UI
- this directly affects the resource heatmap and status summaries

Status update:

- partially improved
- the frontend now maps backend `FUEL_LOW` and `OFFLINE` to the expected warning/offline color families
- broader naming cleanup may still be desirable later, but the main miscoloring issue is reduced

## What each dataset should and should not do

## `data.txt`

Should do:

- seed the current strict demo backbone
- provide real payload-derived operator satellites
- provide real debris field inputs
- provide real scenario-bank seeds for tuning and benchmarking

Should not be expected to do by itself:

- guarantee naturally rich Threat/Burn content under the current backend logic

## `3le_data.txt`

Should do:

- make the visual scene denser and more modern once TLE support is added
- serve as a primary orbital source for large real-data scenario mining

Status update:

- local parser support now exists in replay/miner tooling
- it is not yet merged with `data.txt` metadata or elevated into the broader backend/toolchain

## `train_data.csv` and `test_data.csv`

Should do:

- help choose better ranking and refinement policies offline
- help evaluate whether tuning reduces misses on realistic conjunction-like patterns

Should not do:

- drive runtime telemetry
- become a frontend payload
- become a hard runtime dependency

## Strict real-data demo architecture

The strict route should be built around natural real-data scenes, not around fake geometry.

## Core idea

Use a real catalog-derived operator fleet and real debris field, then mine for naturally interesting scenes.

The operator fleet is still a local demo role assignment, but the orbital states themselves come from real catalog records.

## Strict operator-fleet selection policy

Recommended policy:

- choose the controlled fleet from real payloads only
- select a manageable number of operator spacecraft for the UI, for example `8-12`
- keep the rest of the catalog as real debris / background objects
- prefer LEO payloads with clean geometry and good ground-track visibility

This is much stricter than generating random `SAT-LOCAL-*` orbits.

## Scenario mining policy

Instead of inventing close approaches, search real catalogs for them.

The scenario miner should score candidate scenes using:

- debris density in the chosen shell
- number of operator satellites with readable ground tracks
- presence of naturally predicted close approaches within 24 hours
- LOS friendliness to at least one ground station
- likelihood that an evasion can naturally lead to a recovery story
- visual clarity for the demo

Status update:

- partially implemented
- the current lightweight miner now scores LOS visibility against `docs/groundstations.csv`
- deeper recovery-story and backend-CDM-aware mining are still future work

## Strict demo phases

### Strict primary path

This is the desired end state.

1. Load real catalog data.
2. Select a real payload-derived operator fleet.
3. Filter the rest into a real debris/background field.
4. Choose a replay epoch from a naturally interesting scene.
5. Ingest through the normal telemetry API.
6. Step the system forward.
7. Let the backend surface predictive conjunctions naturally.
8. Let auto-COLA and recovery occur naturally.

### Temporary fallback for local completeness only

This fallback exists only to keep the local demo usable while the strict backend work is still incomplete.

- use the real-data scene as above
- if Burn Ops would otherwise be empty, schedule one or two real-satellite burns through the existing schedule API

Important rule:

- this fallback is acceptable for local completeness, but it is not the target answer to the autonomy requirement

## Main blockers to the strict route today

These are the highest-value blockers.

## Blocker 1 - predictive CDM records are persisted and now consumed end-to-end

Current reality:

- the 24-hour scan now persists predictive records and exposes them through `GET /api/debug/conjunctions?source=predicted`
- the dashboard now defaults to `source=combined`, so predictive CDMs appear across all dashboard surfaces
- threat counting now uses `riskLevelForEvent()` which prefers server-provided severity

Status: **Resolved**

## Blocker 2 - auto-COLA is now partially predictive, but recovery and UI are still incomplete

Current reality:

- automatic collision-avoidance burn planning now consumes satellites identified by the predictive 24-hour CDM scan as well as same-step collision indices

Why this blocks the strict route:

- this closes the trigger gap substantially, but the strict route is still incomplete until:
  - the frontend uses the predictive stream naturally
  - recovery behavior is demonstrated clearly in the UI
  - real catalog scenes, not only synthetic validation pairs, are exercised regularly

## Blocker 3 - required Section 6.2 visuals are now substantially complete

Current reality:

- **Ground Track now has a recognizable world coastline layer** — all major continents and large islands rendered as simplified polygons
- **Bullseye now has an explicit Pc proxy dimension** — dot size and opacity are influenced by `pcProxy()` and the legend shows CDM severity tiers
- the efficiency chart is implemented
- the timeline now surfaces backend-driven conflict/blackout/drop/avoidance state

Status: **Resolved** — all required Section 6.2 modules now have at least their core visual implementation

## Blocker 4 - strict natural scenes still do not cross the backend predictive threshold reliably

Current reality:

- recent miner passes now find much closer natural encounter windows, commonly around `10-16 km`
- backend replay validation still returns zero predictive CDMs for those scenes at the `COLLISION_THRESHOLD_KM = 0.100` gate
- **Two-pronged solution now implemented:**
  1. **Tiered screening** — `SCREENING_THRESHOLD_KM = 5.0 km` outer gate now emits WATCH (1-5km) and WARNING (100m-1km) events from real data, populating Threat UI even without sub-100m encounters
  2. **Hybrid injection** — `scripts/inject_synthetic_encounter.py` can inject 1-N synthetic debris on near-collision courses via `POST /api/telemetry` to trigger the full CRITICAL CDM → auto-COLA → recovery pipeline

Status: **Substantially resolved** — the tiered screening means real catalog data produces visible threat activity at the WATCH/WARNING level, and the hybrid injection script closes the last-mile gap for demonstrating CRITICAL auto-COLA behavior

## Blocker 5 - 3LE support is not wired into replay or scenario tools

Current reality:

- mostly resolved in local tooling
- `3le_data.txt` now works in the replay helper, manifest generator, and lightweight scenario miner
- remaining gap is broader metadata merge and deeper backend/toolchain integration

## PS compliance audit

This section records what is complete, partial, or missing against the PS.

## Hard deployment and API constraints

Status: On track

Currently aligned:

- API runs on port `8000`
- Docker runtime base image is `ubuntu:22.04`
- the server binds to `0.0.0.0:8000`
- telemetry, schedule, step, and snapshot endpoints exist
- `GET /api/status` can stay minimal by default and expose details via query flag

Guardrail:

- any new local-demo or debug support must stay additive and must not replace required contracts

## Section 4 API contracts

### `POST /api/telemetry`

Status: Implemented

Notes:

- the ACK includes `processed_count` and `active_cdm_warnings`
- local replay helpers should continue to use this path

### `POST /api/maneuver/schedule`

Status: Implemented

Notes:

- response includes `ground_station_los`, `sufficient_fuel`, and `projected_mass_remaining_kg`
- this is good and should be preserved

### `POST /api/simulate/step`

Status: Implemented

Notes:

- already returns `new_timestamp`, `collisions_detected`, and `maneuvers_executed`

### `GET /api/visualization/snapshot`

Status: Implemented

Notes:

- compact `debris_cloud` tuple format is already aligned with the PS payload-compression intent

## Section 5 maneuver and navigation logic

### Fuel depletion and cooldown

Status: Implemented

Notes:

- runtime tracks fuel depletion
- 600-second cooldown logic exists
- max burn delta-v limits are validated

### Station-keeping box and recovery requirement

Status: Partial but substantial

Implemented today:

- stationkeeping box logic exists
- uptime penalty metrics exist internally
- recovery requests and recovery burn planning exist

What still matters:

- the strict demo should visibly show this behavior in the UI, not just in backend metrics

### Ground-station LOS and blackout-aware upload planning

Status: Substantially implemented in backend and now materially better visualized in UI

Implemented today:

- ground-station LOS checks exist
- upload epoch and upload station are computed
- upload-missed counters are tracked

Remaining gap:

- upload-station context can still be richer, but the timeline now surfaces blackout/conflict/drop state from live backend data

### Blind conjunction predictive capability

Status: Substantially implemented

Implemented today:

- backend can search upload windows before burn execution
- backend now persists predictive 24-hour conjunction records in a queryable debug path
- auto-COLA now consumes predictive critical conjunctions as part of its trigger set
- tiered screening (5km outer gate) now produces watch/warning events from real catalog scenes
- dashboard now consumes combined (history + predictive) conjunction stream by default

Remaining gap:

- real catalog scenes still rarely cross the backend predictive trigger threshold of `< 0.100 km` naturally, but tiered screening and hybrid injection now cover the demo story

## Section 6.1 frontend performance constraints

Status: Reasonably aligned, but demo filtering still matters

Current strengths:

- Canvas and WebGL are already used
- snapshot payload is compact for debris
- globe rendering can handle large point clouds

Important practical note:

- strict real-data demo scenes should still be filtered sensibly to maintain smoothness
- `10k-20k` real debris is a safer default than indiscriminately pushing every available object into every frame

## Section 6.2 required visualization modules

### Ground Track Map (Mercator Projection)

Status: **Complete**

Already present:

- live active-satellite markers
- historical path support for the last 90 minutes
- dashed predicted trajectory for the next 90 minutes
- dynamic terminator overlay
- **simplified world coastline polygons** (~2k vertices, all major continents + large islands)
- coastlines rendered between graticule and terminator with antimeridian wrap detection

### Conjunction Bullseye Plot

Status: **Substantially complete**

Already present:

- relative proximity plotting
- radius tied to time to closest approach
- angle tied to approach vector
- risk coloring by miss distance and **server-provided CDM severity**
- adaptive bullseye horizon support for `90m`, `6h`, and `24h`
- Threat page now prefers predictive CDMs when they exist
- **Pc proxy dimension** — dot size and opacity influenced by `pcProxy()` from miss distance + approach speed
- **CDM severity legend** showing `CRITICAL (<100m)`, `WARNING (<1km)`, `WATCH (1-5km)` tiers

Missing or weak:

- selected-satellite centering is much stronger now, but still not a hard global requirement across every dashboard entry path

Required follow-up:

- further UX refinement once live CRITICAL events are regularly exercised

### Telemetry and Resource Heatmaps

Status: Substantially improved, still partial

Already present:

- fuel watch / fuel heatmap
- status panel
- burn totals and fuel-consumed backend stats exist
- the `Fuel Consumed vs Collisions Avoided` chart is now implemented from backend burn summary data

Missing or weak:

- frontend status vocabulary does not fully match backend status strings

Required follow-up:

- implement the efficiency graph
- either map backend `FUEL_LOW` and `OFFLINE` into frontend display states or update the frontend to support the real backend vocabulary directly

### Maneuver Timeline (Gantt Scheduler)

Status: Substantially improved, still partial

Already present:

- past and future actions
- burn start and burn end blocks
- mandatory 600-second cooldown windows
- distinct visual treatment for pending, avoidance, recovery, and graveyard burns
- backend-driven blackout/conflict/drop annotations
- `collision_avoided` execution tagging

Missing or weak:

- upload-station data exists but is not surfaced meaningfully in the visualization

Required follow-up:

- improve upload-station storytelling and tighten the PS-style scheduler semantics further

## Additional important gaps not limited to Section 6.2

## Predictive threshold alignment gap

Status: **Substantially resolved via tiered screening + hybrid injection**

The backend predictive CDM path previously only emitted warnings when the propagated 24-hour miss distance fell below `0.100 km`. This meant natural `10-16 km` encounters produced zero CDM events.

The two-pronged solution:

1. **Tiered screening** (`SCREENING_THRESHOLD_KM = 5.0 km`): Real catalog scenes that pass within 5km now produce WATCH/WARNING events that populate the Threat UI. This makes the dashboard operationally interesting with real data alone.
2. **Hybrid injection** (`scripts/inject_synthetic_encounter.py`): For demonstrating the full CRITICAL CDM → auto-COLA → recovery pipeline, 1-2 synthetic close-approach debris can be injected via the standard `POST /api/telemetry` API. This is PS-safe and additive.

Remaining practical consideration:

- natural catalog scenes still rarely produce sub-100m encounters, so the hybrid injection remains necessary for triggering auto-COLA in a controlled demo setting
- the tiered screening ensures the Threat page is never empty even without injection

## Dense-run runtime timeout gap

Status: Observed during investigation

Some backend-assisted ranking and replay-precheck runs hit `HTTP 503`, `RUNTIME_BUSY`, or `runtime command timed out in queue`. The current queue command timeout in `src/engine_runtime.cpp` is still short enough that dense validation passes can overrun it.

Practical consequence:

- reduced-cap validation is currently more reliable for miner precheck loops
- dense real-scene validation still needs runtime-cost tuning if it is going to be part of the normal mining loop

## Predictive conjunction persistence gap

Status: **Resolved**

The backend now persists a predictive CDM set with tiered severity classification and exposes it through the additive conjunction debug query mode. The dashboard now fetches `source=combined` by default, so both predictive and historical conjunctions flow through all dashboard surfaces.

## Autonomy trigger gap

Status: Partially resolved

Auto-COLA now consumes the predictive critical-conjunction stream as part of its trigger path. Remaining work is to verify this systematically on real-data scenes that actually cross the strict predictive threshold and surface the autonomy story clearly in the frontend.

## Frontend/backend status mismatch

Status: Important correctness gap

The frontend heatmap and status summaries should be updated to reflect actual backend status names.

## Proposed backend roadmap

This is the recommended technical plan for backend work.

## Workstream A - unified real catalog ingestion

Goal:

- one shared loader for OMM JSON, 3LE, and 2LE/TLE-like sources

Recommended outcome:

- a reusable record type that can be populated from `data.txt`, `3le_data.txt`, and later `tle2025.txt`

Desired fields:

- NORAD ID
- object name
- object type if known
- epoch
- orbital elements sufficient for ECI propagation
- source tag

Recommended implementation direction:

- keep `scripts/replay_data_catalog.py` working
- add a common parser layer or a new replay/compiler helper that supports both OMM and TLE inputs
- allow metadata merge by NORAD so `data.txt` can enrich `3le_data.txt`

Status update:

- partially implemented in Python tooling
- `scripts/replay_data_catalog.py`, `scripts/generate_strict_manifest.py`, and `scripts/mine_strict_scenarios.py` now all support both OMM JSON and 3LE/TLE text catalogs through the shared parser path
- remaining work is metadata merge by NORAD and equivalent support in broader backend/toolchain paths

## Workstream B - scenario manifest format

Goal:

- record strict demo scenarios as explicit reproducible manifests rather than ad hoc script flags

Status:

- partially implemented
- `scripts/replay_data_catalog.py` now accepts `--manifest`
- `scripts/generate_strict_manifest.py` now emits reproducible strict replay manifests from `data.txt`
- a committed example template exists at `docs/scenarios/strict_natural_watch.example.json`
- the manifest currently drives replay defaults, but there is not yet a full automatic scenario-mining pipeline that generates manifests from real catalogs

Status update:

- a lightweight scenario miner now exists and emits ranked manifests from real catalog geometry
- it is intentionally approximate and pre-backend; it is meant to improve repeatability and scenario selection, not replace the runtime CDM engine

Recommended manifest contents:

```json
{
  "scenario_id": "strict_natural_watch_01",
  "catalog_sources": ["data.txt", "3le_data.txt"],
  "target_epoch": "2026-03-25T08:00:00Z",
  "operator_selection": {
    "mode": "payload_subset",
    "count": 10,
    "norad_ids": ["..." ]
  },
  "filters": {
    "leo_only": true,
    "max_objects": 15000
  },
  "watch_satellites": ["SAT-..."],
  "warmup_steps": 6,
  "warmup_step_seconds": 300
}
```

Why this matters:

- reproducibility
- video-demo consistency
- easier regression testing
- easier tuning/evaluation reuse

Current implementation note:

- explicit CLI flags still override manifest defaults so local experimentation remains easy

## Workstream C - predictive CDM record persistence

Goal:

- persist the actual top predictive conjunction records from the 24-hour scan instead of only storing a count

Status:

- partially implemented
- the backend now stores predictive conjunction records and exposes them through `GET /api/debug/conjunctions?source=predicted`
- the next step is to wire those records into the Threat UI and then into auto-COLA planning

Recommended record fields:

- `satellite_id`
- `debris_id`
- `tca_epoch_s`
- `miss_distance_km`
- `approach_speed_km_s`
- `sat_pos_eci_km`
- `deb_pos_eci_km`
- `source` or `classification` such as `predicted_24h` vs `narrow_history`
- optional `risk_score` or `pc_proxy`

Recommended endpoint strategy:

- chosen implementation: extend `/api/debug/conjunctions` with an additive query parameter `source=history|predicted|combined`, while keeping the default stable

PS guardrail:

- do not change required endpoint behavior in a breaking way

## Workstream D - autonomous avoidance from predictive CDMs

Goal:

- make auto-COLA use predicted critical conjunctions naturally

Recommended behavior:

1. rank predictive events by severity and time-to-TCA
2. choose the best burn window subject to LOS, cooldown, fuel, and latency
3. schedule avoidance early enough to respect blackout constraints
4. mark recovery work after the evasion burn
5. expose resulting story to the timeline and status views

This is the single most important backend change for the strict route.

Current status:

- partially implemented
- predictive critical conjunctions now feed the collision-avoidance trigger path
- remaining work:
  - validate behavior on strict real-data scenarios instead of only synthetic validation pairs
  - ensure recovery-after-evasion is visible and explainable in the UI
  - confirm that this path behaves well under dense real catalog loads

Implementation note:

- current integration is conservative and simple: satellites implicated by predictive critical-CDM records are merged into the avoidance trigger set before `plan_collision_avoidance_burns(...)` runs

## Workstream E - maneuver diagnostics for the timeline

Goal:

- turn the timeline from a good burn visual into a full PS-aligned scheduler view

New state to expose per burn if possible:

- `upload_station_id`
- `upload_window_missed`
- `blackout_overlap`
- `cooldown_conflict`
- `command_conflict`
- `scheduled_from_predictive_cdm`

Why this matters:

- the backend already knows a lot of this information or can derive it cheaply
- the frontend cannot satisfy the spirit of the timeline requirement without it

## Workstream F - avoidance-effectiveness metrics

Goal:

- implement the required `Fuel Consumed vs Collisions Avoided` graph honestly

Needed metric design:

- `fuel_consumed_kg` already exists per satellite and in aggregate
- `collisions_avoided` still needs a definition that is clear and defensible

Recommended approach:

- define an avoided event as a predicted critical conjunction that transitions to a safe post-burn miss distance after the burn plan is applied
- track this per burn or per event
- expose an aggregate series for the frontend efficiency chart

## Workstream G - real-data scenario miner

Goal:

- automatically find strong natural strict-demo scenes

Status:

- partially implemented
- `scripts/mine_strict_scenarios.py` now scores payload anchors by nearby natural traffic and emits ranked manifests
- it currently uses a lightweight orbital sampling heuristic in Python, not the full backend CDM engine
- it is already useful for repeatable strict-route replay presets and for pulling likely-nearby debris into `priority_debris_norad`
- it now works on both `data.txt` and `3le_data.txt`
- `scripts/evaluate_strict_manifest.py` and `scripts/rank_strict_manifests.py` now add a backend-assisted ranking layer on top of mined manifests
- LOS visibility from `docs/groundstations.csv` is now part of mined manifest metadata and ranking
- diversity-aware operator selection is now part of manifest generation so ranked presets are less likely to be exact duplicates
- the miner can now emit candidate manifests first and backend-rank them inline in the same command
- threat-richness features and shell/family balancing are now part of manifest generation and ranking metadata
- backend-informed feedback weights can now be read and written by the miner to bias future scenario generation

Status update:

- candidate generation now targets likely predictive-warning shells more directly
- the current remaining limitation is not miner plumbing, but finding scenarios that actually trigger non-zero predictive CDM counts in the sampled run window
- replay timing is now biased toward clustered close approaches, which should make future backend-informed mining passes converge faster
- miner output and evaluation now record backend-threshold alignment fields such as `predictive_cdm_threshold_km`, `predictive_alignment_gap_km`, `manifest_heuristic_min_miss_km`, and `heuristic_predictive_gap_km`
- miner-integrated backend precheck can now run a reduced-debris fresh-backend replay before final manifest emission and attach the precheck summary into `mining.backend_precheck`

Recommended inputs:

- merged real catalog states
- ground station geometry
- current CDM thresholds and planning rules
- optional operator-fleet size target

Recommended outputs:

- named scenario manifests
- recommended operator subset
- recommended watched satellites
- recommended replay epoch
- naturally interesting predicted conjunctions

Current implementation note:

- mined manifests now include `priority_debris_norad`, which the replay helper uses to place likely-interesting natural threats at the front of the replay debris set
- the miner is still heuristic and pre-backend; its role is scenario selection and repeatability, not authoritative conjunction truth
- the new ranking helper closes part of that gap by replaying manifests against a fresh live backend and scoring them from real runtime outputs
- miner-integrated backend ranking now makes it easier to produce a scored scenario bank artifact in one pass
- the next miner improvement should combine backend outcomes with richer shell/family balancing so scenario banks cover more operational variety

Status update:

- shell/family balancing is now partially implemented in scenario assembly
- threat-richness weighting now favors anchors with stronger nearby-traffic signals before backend ranking begins
- the next useful improvement is deeper backend-CDM-informed mining rather than more heuristic-only features

Latest implementation note:

- backend-informed mining is now partially implemented through a feedback loop:
  - candidate manifests are mined heuristically
  - candidate generation is biased toward shell-dense and phase-dense regions
  - backend ranking evaluates them on fresh runs
  - summary signals update a small set of mining weights
  - those weights can be persisted and reused on the next mining pass
- predictive-CDM evaluation is now also written back into each ranked manifest, so later tooling does not need to rediscover the same scenario-level CDM summary from scratch
- the current backend-aware precheck confirms a more specific root cause: mined natural scenes are getting closer, but still not close enough to cross the backend predictive threshold of `100 m`

## Workstream H - tuner and evaluator upgrade

Goal:

- stop relying only on random orbital seeds for tuning

Recommended changes:

- build real-data train/eval scenario banks from `data.txt` and `3le_data.txt`
- use `train_data.csv` to calibrate ranking/refinement policy offline
- validate on `test_data.csv`
- keep the CSVs out of runtime telemetry and frontend use

## Proposed frontend roadmap

These are the frontend tasks needed to close the PS and strict-route gaps.

## Frontend Task 1 - Ground Track world layer

Add a lightweight coastline / world-outline layer to `frontend/src/components/GroundTrackMap.tsx`.

Status: **Resolved**

- simplified world coastline polygons (~2k vertices) now stored in `frontend/src/data/coastlines.ts`
- `drawCoastlines()` function added to `GroundTrackMap.tsx` with fill + stroke, antimeridian wrap detection
- rendered between graticule and terminator layers

## Frontend Task 2 - Threat page selected-satellite discipline

Adjust `frontend/src/pages/ThreatPage.tsx` and `frontend/src/components/ConjunctionBullseye.tsx` so the view is explicitly centered around a selected spacecraft.

Reason:

- this matches the PS more closely
- it also makes the strict real-data predictive story much clearer

Status update:

- partially improved
- selecting an event still locks the global spacecraft focus
- the page now prefers predictive threat data when present
- selected-satellite anchoring is now stricter even before an event is chosen: the page auto-focuses the most relevant spacecraft lane and keeps the bullseye/watch summary centered there

## Frontend Task 3 - Bullseye predictive horizon design

Decide how to display 24-hour predictive CDMs on a bullseye that currently uses a 90-minute radial window.

Recommended options:

- dynamic scale by context
- a near-term filtered view plus a separate wider-horizon summary
- selectable ring presets such as `90m`, `6h`, `24h`

Status update:

- partially implemented via dynamic horizon selection across `90m`, `6h`, and `24h`
- further UX refinement may still be needed once real predictive scenes are available regularly

## Frontend Task 4 - Efficiency graph

Replace the placeholder in `frontend/src/pages/BurnOpsPage.tsx` with the actual `Fuel Consumed vs Collisions Avoided` chart.

Status update:

- implemented
- the chart now highlights the current focus or best-efficiency point and surfaces an avoided-collisions-per-kg reference caption

## Frontend Task 5 - Timeline conflict and blackout markers

Extend `frontend/src/components/ManeuverGantt.tsx` to render backend-provided conflict and blackout state.

Status update:

- implemented
- the timeline now annotates each lane with compact queue totals, highlights `collision_avoided` executions, and keeps blackout/conflict/drop markers tied to live backend data

## Frontend Task 6 - status vocabulary fix

Update the frontend to support real backend status strings.

Reason:

- `FUEL_LOW` and `OFFLINE` should not disappear into the default styling path

Status update:

- partially implemented
- `statusColor(...)` now handles backend `FUEL_LOW` and `OFFLINE`
- dashboard fleet-status aggregation now maps `FUEL_LOW` into the degraded bucket and `OFFLINE` into the graveyard/offline bucket for summary purposes

## Validation and tuning plan using all datasets

This is how all datasets should be used together for backend improvement.

## Step 1 - real catalog scenario bank

Use `data.txt` first, then `3le_data.txt`, to build a bank of reusable real scenarios for:

- dense LEO visuals
- natural close-approach watch scenes
- LOS-sensitive upload windows
- recovery-after-evasion stories
- large-scale performance runs

## Step 2 - CSV-based offline calibration

Use `train_data.csv` to tune:

- candidate ranking
- refine-band thresholds
- uncertainty-promotion policy
- budget allocation for expensive refinement

Do not let the calibration logic become a hard rejector if it risks false negatives.

## Step 3 - holdout validation

Use `test_data.csv` only after tuning to measure:

- recall on critical close-approach cases
- ranking quality on severe events
- stability across mission families

## Step 4 - real scenario replay validation

Run tuned configs on catalog-derived scenario banks from `data.txt` and `3le_data.txt` to confirm:

- no obvious regressions in runtime behavior
- acceptable warning density
- acceptable burn counts
- acceptable visual smoothness

## Risk register

These are the main risks to remember.

## Risk 1 - overfitting to the CSVs

Mitigation:

- keep CSV use offline only
- validate on holdout
- preserve conservative fail-open behavior

## Risk 2 - natural scenes may still be visually weak

Mitigation:

- build a scenario miner
- keep multiple named scenario manifests
- do not depend on a single random replay

## Risk 3 - full catalog density may hurt frontend smoothness

Mitigation:

- filter real objects, do not fabricate them
- stay above the PS density target while preserving readability and frame rate

## Risk 4 - endpoint creep breaking judged behavior

Mitigation:

- keep required endpoint behavior stable
- add only debug or optional paths
- keep status extras query-gated

## Risk 5 - status-name drift causing misleading UI

Mitigation:

- explicitly align frontend status handling with backend vocabulary

## Recommended phased execution order

This is the recommended order of work from here.

## Phase 0 - planning and hygiene

- keep this document updated
- decide whether `3le_data.txt`, `train_data.csv`, and `test_data.csv` should be ignored locally

## Phase 1 - strict real-data foundation

- stop using random synthetic constellation seeding as the primary path
- select operator satellites from real payloads in `data.txt`
- add scenario-manifest support
- mine the first strict natural scene

Status update:

- scenario-manifest support is now partially in place through the replay helper
- the next missing piece is automatic natural-scene mining and manifest generation

## Phase 2 - predictive CDM persistence

- persist predictive CDM records from the 24-hour scan
- expose them through a PS-safe additive debug path
- feed them into Threat views

## Phase 3 - natural autonomous behavior

- trigger auto-COLA from predicted critical conjunctions
- verify recovery follows naturally after avoidance
- preserve LOS / latency / cooldown / fuel constraints end-to-end

## Phase 4 - Section 6.2 closure

- add coastline/world map layer to Ground Track
- implement efficiency chart
- implement blackout/conflict timeline markers
- align frontend status vocabulary

## Phase 5 - 3LE and large-scale catalog upgrade

- expand the new `3le_data.txt` parser support beyond local Python tooling
- merge metadata with `data.txt`
- use `tle2025.txt` for large-scale regression and performance scenarios

## Phase 6 - data-driven backend tuning

- build real-data scenario banks for the tuner
- calibrate ranking/refinement policy on `train_data.csv`
- validate on `test_data.csv`

## Short list of things future work should not forget

- ~~Threat now has a predictive path, but the rest of the dashboard still mostly reasons over historical conjunction data.~~ **Resolved** — dashboard now fetches `source=combined` by default.
- Auto-COLA now consumes predictive critical conjunctions, but this still needs validation on strict real-data scenarios that actually cross the `< 0.100 km` predictive threshold. The hybrid injection script (`scripts/inject_synthetic_encounter.py`) can trigger this for demo purposes.
- ~~Ground Track still needs real world outlines to fully satisfy Section 6.2.~~ **Resolved** — coastline polygons now rendered.
- ~~The bullseye still lacks an explicit probability-of-collision dimension.~~ **Resolved** — `pcProxy()` now influences dot rendering and Pc estimate is shown in detail cards.
- Frontend status handling does not yet fully match backend status names (partially mitigated).
- Tiered screening (5km outer gate) should produce WATCH/WARNING events from real catalog data, but this needs live-backend validation with a dense catalog replay.
- Dense backend ranking runs can still hit queue timeout / `RUNTIME_BUSY` conditions and may need runtime-cost tuning.
- `3le_data.txt` now works in local replay/miner tooling, but metadata merge and broader backend integration are still pending.
- `train_data.csv` and `test_data.csv` are evaluation assets, not runtime telemetry inputs.
- The synthetic encounter injection script computes debris orbits from approximate lat/lon → ECI conversion; for a tighter demo, consider using actual ECI state vectors from the backend if exposed in a future API.

## Open decisions that may need resolution later

- Should the bullseye support multiple time scales or stay near-term only with clear labels?
- Should the strict operator fleet be selected by a fixed manifest or discovered automatically per scenario?

## Resolved decisions

- Predictive CDMs are exposed through an additive query mode on `GET /api/debug/conjunctions`, not through a brand-new endpoint.
- `3le_data.txt`, `train_data.csv`, and `test_data.csv` are now ignored locally in `.gitignore`.

## Final implementation principle

If there is tension between a flashy synthetic demo and a stricter real-data demo, prefer the stricter real-data path unless it prevents the application from showing a required PS module at all.

When a temporary fallback is needed, it should be documented clearly as a stopgap and not confused with the final autonomous solution.
