# CASCADE Final Video Demo Script

Target length: 4:45 to 5:00

Audience: National Space Hackathon 2026 judges

Goal: Show that CASCADE satisfies the problem statement end-to-end: physics-aware backend, autonomous collision avoidance, recovery logic, fuel accounting, required dashboard modules, Dockerized deployment, and strong alignment to the judging criteria.

## Recording Notes

- Keep the browser at a 16:9 desktop resolution, ideally `1600x900` or `1920x1080`.
- Keep terminal text zoomed in enough to be readable on video.
- Use the live product, not static screenshots.
- Do not rush clicks. Leave 1 to 2 seconds after each route switch so the judge can read the page.
- Use the canonical live demo path: `./scripts/run_ready_demo.sh`.
- If the backend is already up, still show the command you use so the flow feels reproducible.

## Demo Structure

1. Problem and system promise
2. Deployment and submission readiness
3. Live autonomous scenario setup
4. Command deck overview
5. Ground track and threat geometry
6. Burn decision and recovery logic
7. Efficiency, fleet posture, and close

## 0:00 - 0:25 Opening

### What to show

- Start on the repo root in terminal.
- Briefly show `README.md` and the project name.

### What to say

"This is CASCADE, our Autonomous Constellation Manager for the National Space Hackathon 2026 problem statement on orbital debris avoidance and constellation management.

The challenge is not just detecting collisions. We have to ingest high-frequency ECI telemetry, predict conjunctions up to 24 hours ahead, autonomously schedule evasion and recovery burns, account for propellant, respect ground-station constraints, and present the whole operational picture in a frontend that stays usable under load."

### Cue

- Point to the project title and keep the repo root visible for a second.

## 0:25 - 0:50 Deployment Readiness

### What to show

- In terminal, show the core submission path:

```bash
docker build -t cascade:local .
docker run --rm -p 8000:8000 cascade:local
```

- If already running, show:

```bash
curl -s http://localhost:8000/api/status | python3 -m json.tool
```

### What to say

"Our submission is packaged exactly the way the PS requires: a root Dockerfile based on Ubuntu 22.04, binding to port 8000, with the backend APIs and the frontend served from the same runtime.

The required API surface is exposed on this port: telemetry ingestion, maneuver scheduling, simulation step, and the optimized visualization snapshot endpoint."

### Cue

- Pause on the `api/status` JSON long enough for judges to see the service is live.

## 0:50 - 1:35 Live Scenario Setup

### What to show

- Run:

```bash
./scripts/run_ready_demo.sh
```

- Let the terminal print the key steps:
  - docker restart
  - replay catalog from `3le_data.txt`
  - inject critical for `SAT-67060`
  - inject critical for `SAT-67061`
  - simulation stepping
  - readiness check

### What to say

"For the live demo, we use a deterministic script so the judges can see a real end-to-end sequence instead of a hand-waved storyboard.

This script restarts the stack, replays our hot catalog dataset, injects two calibrated critical conjunctions against real tracked spacecraft, advances the simulation clock, and then verifies whether autonomous avoidance actually happened.

So what you are about to see is not a mocked UI state. It is generated from the live backend that the judges can also run."

### Cue

- As the script runs, verbally point out `SAT-67060` and `SAT-67061`.
- When the readiness check prints, pause briefly.

## 1:35 - 2:10 Command Deck

### What to show

- Open browser at `http://localhost:8000/#/command`.
- Keep the full command page on screen first.
- Slowly move the mouse over the top score strip, then the three columns.

### What to say

"We redesigned the command page as an executive ops board, not just another map. That was deliberate.

The top strip aligns directly to the PS evaluation criteria: safety, fuel, uptime, algorithmic speed, UI state, and autonomy.

The left column shows trust state and the operator action queue. The center shows the next threat and the next autonomous decision. The right column summarizes fleet readiness and autonomy proof.

This page answers the questions a mission lead or a judge actually cares about: can I trust the system, what matters next, and is autonomy working?"

### Cue

- Hover the Trust State panel.
- Hover the Action Queue.
- Hover the Next Threat and Autonomous Decision panels.

## 2:10 - 2:45 Ground Track Page

### What to show

- Click `Track`.
- Keep the full map visible.
- Click one satellite to focus it if the view is not already focused.

### What to say

"This is the Ground Track module required by the PS. It shows the full constellation on a Mercator map, historical trail for the last 90 minutes, predicted trajectory for the next 90 minutes, and the live terminator line for eclipse context.

This page is about spatial awareness. It answers where every spacecraft is, where it has been, and where it is going.

We kept this role separate from the command page so the dashboard is operationally clear instead of redundant."

### Cue

- Circle the trail and predicted path with your mouse.
- Pause on the terminator overlay.

## 2:45 - 3:25 Threat Deck

### What to show

- Click `Threat`.
- Let the Bullseye, encounter queue, and selected encounter card be visible.
- Click the critical encounter for `SAT-67060` or `SAT-67061`.

### What to say

"This is the conjunction analysis view. The bullseye keeps the selected spacecraft at the center. Radius represents time to closest approach, and angle represents approach direction.

On the right, the queue groups encounters by spacecraft and collapses repeated samples so the operator sees the threat picture, not a flood of duplicate rows.

What matters here is that we do not just flag a collision. We expose miss distance, approach speed, timing, stream provenance, and fail-open state so the threat model is auditable."

### Cue

- Click a critical queue item.
- Pause with the selected encounter details visible.

## 3:25 - 4:05 Burn Ops

### What to show

- Click `Burn Ops`.
- Keep the decision focus cards and timeline visible.
- If a counterfactual panel is available, show it briefly.

### What to say

"Once a critical conjunction is predicted below the hard threshold, CASCADE autonomously computes the evasion burn in the RTN frame, converts that command back into ECI, validates timing and line-of-sight constraints, and tracks fuel depletion using Tsiolkovsky-consistent mass updates.

This page shows the actual maneuver timeline, the burn window, upload station, lead time to TCA, and the post-burn outcome when evaluated.

The important point is that our backend does not stop at avoidance. It also schedules recovery logic so the satellite returns to its mission slot and protects constellation uptime, which is a separate PS scoring axis."

### Cue

- Point at one predictive burn record.
- Pause on delta-v, upload station, and outcome.

## 4:05 - 4:30 Evasion + Fleet Status

### What to show

- Click `Evasion`.
- Then click `Fleet Status`.

### What to say

"The Evasion deck turns the mission into measurable proof: fuel consumed versus collisions avoided. This is how we make fuel efficiency visible instead of just claiming it.

The Fleet Status page closes the loop with engine diagnostics, queue depth, slot integrity, and fuel watchlist posture for the entire constellation.

Together, these two pages connect the backend math to the actual judging axes: safety, fuel, uptime, speed, and UI clarity."

### Cue

- On Evasion, point to avoided collisions and fuel.
- On Fleet Status, pause on mission status and the fuel watchlist.

## 4:30 - 4:50 Final Proof in Terminal

### What to show

- Return to terminal.
- Show the readiness output from `run_ready_demo.sh` or run:

```bash
python3 scripts/check_demo_readiness.py --api-base http://localhost:8000
```

- If needed, also show:

```bash
curl -s http://localhost:8000/api/debug/burns | python3 -m json.tool
```

### What to say

"To close the loop, we finish on backend evidence, not only frontend visuals.

In this reference demo, CASCADE confirms live avoided collisions, tracked autonomous burn execution, and fuel consumption from the same runtime the UI is reading.

This keeps the whole story deterministic and reproducible for judging."

### Cue

- Hold on the confirmed avoid rows or readiness verdict.

## 4:50 - 5:00 Closing

### What to say

"CASCADE is our answer to the orbital debris challenge: a Dockerized autonomous constellation manager that predicts conjunctions efficiently, plans evasions and recoveries under operational constraints, tracks fuel and uptime, and surfaces the mission state through a focused operational dashboard.

Thank you."

## Backup Short Lines

Use these if you need to compress time while recording:

- "Ground Track is for spatial awareness. Command is for operational decisions."
- "Threat explains why an encounter matters. Burn Ops explains what autonomy did about it."
- "Evasion and Fleet Status tie the backend decisions back to the PS scoring criteria."
- "Everything shown here is driven by the live Dockerized backend on port 8000."

## Recording Checklist

- Backend running on `http://localhost:8000`
- `./scripts/run_ready_demo.sh` succeeds
- Browser opens on `#/command`
- `Track`, `Threat`, `Burn Ops`, `Evasion`, `Fleet Status` all load before recording
- Terminal font size is readable
- Browser zoom is readable at 16:9
- Do not scroll too quickly on any page
- End on proof, not only on visuals
