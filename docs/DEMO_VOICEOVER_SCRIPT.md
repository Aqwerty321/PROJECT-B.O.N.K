# CASCADE 5-Minute Demo Voice-Over Script

Estimated runtime: about 5 minutes

## 00:00-00:35 - Problem and Idea

Visual:
- CASCADE title reveal
- Earth view with orbital paths
- Debris density building around the planet

Voice-over:

"Low Earth orbit is getting more crowded every year. More satellites, more debris, and more close approaches mean a higher risk of a chain-reaction collision event. Our project, CASCADE - Collision Avoidance System for Constellation Automation, Detection and Evasion - is built to stop that. The goal is simple: detect dangerous encounters early, plan a safe response automatically, and keep the constellation operational."

## 00:35-01:10 - What CASCADE Does

Visual:
- Simple flow graphic: detect -> decide -> evade -> recover

Voice-over:

"The main idea behind CASCADE is that collision avoidance should not end at a warning. Many systems can raise an alert. CASCADE closes the loop. It ingests orbital traffic, evaluates conjunction risk, chooses a mitigation maneuver, checks that the maneuver is actually feasible, and then verifies the outcome. So this is not just a simulator, and not just a dashboard. It is an end-to-end autonomous decision system for constellation safety."

## 01:10-02:00 - Implementation Overview

Visual:
- Backend pipeline animation
- Telemetry input
- Propagation
- Filter cascade
- Threat confirmation
- Burn planner
- Recovery planner

Voice-over:

"Under the hood, the backend is a deterministic C++20 engine exposed through REST APIs. It ingests orbital telemetry, propagates objects forward in time, and runs a multi-stage collision screening pipeline. The early stages are fast filters that shrink the search space. The later stages perform narrow-phase refinement on suspicious pairs. The safety rule is conservative by design: if a case is uncertain, it is never silently dropped; it is escalated for deeper analysis, because the one error we cannot accept is a missed collision. Once a critical conjunction is confirmed, CASCADE plans an evasion burn while respecting fuel limits, thrust caps, cooldown windows, and ground-station visibility. After mitigation, it schedules recovery so the satellite can return toward its assigned orbital slot. In our reference scenario, this full tick runs in about 13 milliseconds for 50 satellites and 10,000 debris objects."

## 02:00-02:35 - Frontend Overview

Visual:
- Fast montage of the frontend routes
- `#/command`
- `#/track`
- `#/threat`
- `#/burn-ops`
- `#/evasion`
- `#/fleet-status`

Voice-over:

"On top of that engine, we built a mission console using React, TypeScript, Canvas, and WebGL. Each view explains a different part of the system. Command gives the overall fleet picture. Ground Track shows orbital positions, trails, and forecast paths. Threat Assessment turns conjunctions into an intuitive bullseye view. Burn Operations explains why a maneuver was chosen. Evasion quantifies fuel-to-mitigation efficiency. And Fleet Status shows overall health, diagnostics, and fuel posture across the constellation."

## 02:35-03:15 - Real Data Scene

Visual:
- Brief terminal replay command
- Focus on `#/track`
- Constellation markers
- Debris cloud
- Historical trails
- Forecast arcs
- Terminator overlay

Voice-over:

"For the demo, we start by loading a real orbital catalog. A subset of real payloads becomes the operator fleet, while the remaining objects form the surrounding traffic environment. On the Ground Track view, we can see the constellation, the debris cloud, historical trails, forecast arcs, and the day-night terminator. That immediately shows that CASCADE is working in a dense, realistic environment instead of a hand-crafted animation."

## 03:15-03:50 - Threat Detection

Visual:
- Switch to `#/threat`
- Highlight critical conjunctions
- Show time-to-closest-approach rings
- Show severity colors and focused satellite

Voice-over:

"Next, we inject calibrated future encounters so the decision loop becomes visible. In the Threat Assessment view, these appear as critical conjunctions with clear time-to-closest-approach, severity, and miss distance. Instead of a raw list of warnings, we get an operator-friendly picture of which spacecraft is at risk, how urgent the threat is, and which debris object is driving the event."

## 03:50-04:30 - Autonomous Response

Visual:
- Advance simulation clock
- Show `#/burn-ops`
- Pending and executed burns
- Upload station
- Delta-v values
- Recovery sequence

Voice-over:

"When we advance the simulation clock, CASCADE takes over autonomously. The backend evaluates maneuver candidates, picks a safe burn, validates command timing and physical constraints, and schedules the response. In Burn Operations, we can see the burn timeline, the trigger debris, the delta-v choice, the upload station, and the post-mitigation result. And importantly, CASCADE does not stop at avoidance. It also schedules recovery behavior so the satellite can return toward stable service after the threat is cleared."

## 04:30-05:00 - Verification and Close

Visual:
- Show readiness check output
- Show `#/evasion`
- Show `#/fleet-status`
- End on CASCADE title and key proof points

Voice-over:

"Finally, we verify the outcome instead of asking anyone to trust the visuals alone. The readiness check confirms that the demo is successful and that collisions were avoided, and the burn-level debug output confirms the mitigation in machine-readable form. That is what makes CASCADE different: it connects real-data ingestion, conservative conjunction screening, autonomous maneuver planning, recovery, and an explainable frontend in one system. In short, CASCADE turns collision awareness into collision avoidance."
