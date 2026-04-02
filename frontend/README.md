# CASCADE Mission Console Frontend

React 19 / TypeScript / Vite SPA serving as the operational dashboard for the CASCADE engine. Renders 6 views via Canvas 2D and Three.js WebGL.

## Quick Start (Development)

```bash
# 1. Start the C++ backend with CORS enabled
cd ..
PROJECTBONK_CORS_ENABLE=true PROJECTBONK_CORS_ALLOW_ORIGIN=http://localhost:5173 ./build/ProjectBONK &
until curl -sf http://localhost:8000/api/status > /dev/null 2>&1; do sleep 0.5; done

# 2. Start Vite dev server (hot-reload on port 5173)
cd frontend
npm ci
npm run dev
# → Open http://localhost:5173
```

The Vite proxy forwards `/api/*` requests to the backend on port 8000 (configured in `vite.config.ts`).

## Load Real Data Into the Dashboard

With the backend running, replay a real Space-Track catalog to populate the views:

```bash
# In a separate terminal (from the repo root):
python3 scripts/replay_data_catalog.py \
  --data 3le_data.txt \
  --api-base http://localhost:8000 \
  --satellite-mode catalog \
  --operator-sats 10
```

Then navigate the dashboard views:
- `http://localhost:5173/#/command` — Fleet posture summary
- `http://localhost:5173/#/track` — Ground track with orbital trails
- `http://localhost:5173/#/threat` — Polar bullseye threat view
- `http://localhost:5173/#/burn-ops` — Maneuver Gantt timeline
- `http://localhost:5173/#/evasion` — Fuel-to-mitigation efficiency view
- `http://localhost:5173/#/fleet-status` — Fuel heatmaps, drift metrics

## Full End-to-End Demo

To demonstrate collision avoidance with the frontend visible, see the [main README demo path](../README.md#demo-path).

## Production Build

```bash
npm run build    # tsc + vite build → dist/
```

The production bundle is copied into the Docker image and served by the C++ engine at `/` — no separate web server needed.

---

*Built with Vite + React + TypeScript.*
