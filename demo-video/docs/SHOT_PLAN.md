# CASCADE Demo Video Shot Plan

This plan is aligned to the narration in `../docs/DEMO_VOICEOVER_SCRIPT.md` at the repo root.

## Production Approach

Use Motion Canvas for:
- titles, transitions, labels, callouts, timelines, and cinematic framing
- simplified system diagrams and animated overlays
- compositing screenshots or captured clips from the actual app

Current implementation status:
- Scenes 1 through 8 now exist as paced first-pass animations in `src/scenes/`
- Real dashboard screenshots are captured with Playwright into `public/reference/`
- Scenes 5, 6, 7, and 8 already use those captured references

Do not recreate the full frontend pixel-by-pixel unless needed. For the app sections, it is faster and cleaner to animate captured UI stills or short recordings inside Motion Canvas.

## Scene Map

| Scene | Time | File | Purpose |
|---|---:|---|---|
| 1 | 00:00-00:35 | `src/scenes/s01-problem-idea.tsx` | Set up the orbital congestion problem and introduce CASCADE |
| 2 | 00:35-01:10 | `src/scenes/s02-cascade-loop.tsx` | Explain the detect -> decide -> evade -> recover loop |
| 3 | 01:10-02:00 | `src/scenes/s03-implementation.tsx` | Explain the backend pipeline and why it is safe |
| 4 | 02:00-02:35 | `src/scenes/s04-frontend-overview.tsx` | Introduce the mission console and major views |
| 5 | 02:35-03:15 | `src/scenes/s05-real-data-track.tsx` | Show the real catalog setup and Ground Track view |
| 6 | 03:15-03:50 | `src/scenes/s06-threat-detection.tsx` | Show the threat injection and Threat view |
| 7 | 03:50-04:30 | `src/scenes/s07-autonomous-response.tsx` | Show step execution, burn choice, and recovery |
| 8 | 04:30-05:00 | `src/scenes/s08-verification-close.tsx` | Show proof output, efficiency, fleet health, and closing claim |

## Shot Breakdown

### Scene 1 - Problem and Idea

Goal:
- make the danger feel immediate
- introduce CASCADE as an autonomous collision avoidance system

Shots:
1. Empty space and Earth silhouette fade in.
2. Orbit rings and debris markers build rapidly until the shell feels crowded.
3. Hazard text lands: more objects, more close approaches, more risk.
4. CASCADE title card resolves over the orbital field.

Motion Canvas notes:
- mostly vector graphics, no app capture needed
- use slow camera drift and layered parallax
- keep the palette space-blue, alert amber, and critical red

Assets needed:
- optional CASCADE wordmark
- optional Earth texture or stylized vector globe

### Scene 2 - CASCADE Loop

Goal:
- explain that CASCADE is more than a warning system
- communicate the end-to-end loop simply

Shots:
1. A warning-only card appears and is visually crossed out.
2. Four-step loop animates in: detect, decide, evade, recover.
3. Data flows through the loop with small tokens or packets.
4. Closing card: autonomous decision system for constellation safety.

Motion Canvas notes:
- use clean infographic motion, not cinematic chaos
- this scene should feel explanatory and confident

Assets needed:
- no external assets required

### Scene 3 - Implementation Overview

Goal:
- show the technical depth without overwhelming the viewer
- highlight conservative safety logic and real constraints

Shots:
1. Telemetry payload enters the pipeline.
2. Pipeline blocks animate in sequence: propagation, broad phase, narrow phase, CDM, COLA, recovery.
3. Safety rule callout: uncertain pairs escalate rather than getting dropped.
4. Constraint strip appears: fuel, cooldown, thrust cap, ground-station visibility.
5. Performance card lands with the reference tick-time figure.

Motion Canvas notes:
- use modular boxes and connector lines
- one highlighted line should visually emphasize zero-false-negative thinking

Assets needed:
- optional backend metric numbers from the README

### Scene 4 - Frontend Overview

Goal:
- show that the UI is operational, not cosmetic
- introduce the six route-level views quickly

Shots:
1. Dashboard frame slides in.
2. Six route cards appear in a grid or carousel.
3. Each route gets a one-line role label.
4. The layout resolves into a mission-console mosaic.

Motion Canvas notes:
- use screenshots or stylized panels for each route
- this is a montage scene, so keep cuts quick and readable

Assets needed:
- screenshots for `#/command`, `#/track`, `#/threat`, `#/burn-ops`, `#/evasion`, `#/fleet-status`

### Scene 5 - Real Data on Ground Track

Goal:
- prove the environment is dense and realistic
- show the actual app view rather than only abstract graphics

Shots:
1. Terminal command types in for catalog replay.
2. Ground Track screenshot or clip animates into frame.
3. Callouts point to constellation markers, debris density, trails, forecast arcs, and terminator.
4. A short caption lands: real catalog, real traffic picture.

Motion Canvas notes:
- best done with a captured app still or short clip plus animated callouts
- use zoom boxes and labeled leader lines rather than rebuilding the view from scratch

Assets needed:
- terminal capture or reconstructed command text
- Ground Track screenshot or short recording

### Scene 6 - Threat Detection

Goal:
- show that critical conjunctions are surfaced clearly
- connect threat geometry to operator understanding

Shots:
1. Threat injection cue or alert pulse appears.
2. Threat Assessment screenshot or clip swaps in.
3. Critical points on the bullseye are highlighted.
4. Labels identify severity, time-to-TCA, miss distance, and focused satellite.

Motion Canvas notes:
- use animated rings and pulses on top of the captured bullseye view
- emphasize urgency without making the scene too noisy

Assets needed:
- Threat view screenshot or short recording

### Scene 7 - Autonomous Response

Goal:
- show the system making a decision, not just reporting a threat
- highlight burn logic and recovery planning

Shots:
1. Simulation clock advances.
2. Decision cards cycle through candidate maneuver logic.
3. Burn Operations screenshot or clip appears with timeline overlays.
4. Callouts identify trigger debris, delta-v, upload station, and post-mitigation result.
5. Recovery track or recovery label closes the loop.

Motion Canvas notes:
- this is the most important scene in the whole video
- use a mix of system-diagram overlays and real UI capture
- give the burn decision extra screen time

Assets needed:
- Burn Operations screenshot or short recording
- optional command-center still for context

### Scene 8 - Verification and Close

Goal:
- show proof, not just visuals
- end with a strong value statement

Shots:
1. Terminal verdict appears: ready.
2. Debug or proof output highlights avoided collisions.
3. Evasion and Fleet Status views appear side by side.
4. Final closing card lands: collision awareness into collision avoidance.

Motion Canvas notes:
- keep the ending clean and decisive
- let the machine-verification text read clearly before the final lockup

Assets needed:
- readiness-check output
- debug burn output
- screenshots for `#/evasion` and `#/fleet-status`

## Asset Checklist

Place these inside `public/reference/` before building final scenes:

- `command-overview.png`
- `ground-track.png` or `ground-track.mp4`
- `threat-view.png` or `threat-view.mp4`
- `burn-ops.png` or `burn-ops.mp4`
- `evasion-view.png`
- `fleet-status.png`
- `readiness-output.png`
- `burn-debug-output.png`

## Scene Build Order

Recommended implementation order:

1. Scene 1 - opening title and orbital problem framing
2. Scene 5 - Ground Track with real capture
3. Scene 6 - Threat view overlays
4. Scene 7 - Burn decision and response
5. Scene 8 - proof and closing
6. Scene 2 - system loop explainer
7. Scene 3 - backend pipeline explainer
8. Scene 4 - frontend overview montage

This order gets the most demo-critical visuals working first.
