# CASCADE Demo Video Workspace

This directory is a standalone Motion Canvas workspace for the evaluation video.

It is intentionally isolated from the main application code:
- do not import code from `../frontend`
- do not import code from `../src`
- copy any needed screenshots, screen recordings, logos, or audio into `public/`

## Commands

```bash
npm install
npm run install:browsers
npm start
npm run build
npm run capture:refs
npm run sync:refs
npm run prepare:refs
npm run render:preview
```

## Important Files

- `docs/SHOT_PLAN.md` - shot-by-shot production plan
- `src/project.ts` - Motion Canvas project entry
- `src/scenes/` - scene placeholders matching the planned video structure
- `public/reference/` - exported app stills and proof artifacts to animate
- `public/audio/` - voice-over and music scratch files
- `scripts/capture-reference.mjs` - Playwright-based reference capture helper
- `scripts/sync-reference-data.mjs` - converts proof artifacts into Motion Canvas source data
- `scripts/prepare-demo-assets.mjs` - one-command scenario prep, capture, and sync flow

## Recommended Workflow

1. Run `npm run prepare:refs` to stage the backend demo, capture current dashboard screenshots, and sync the proof data used by the scenes.
2. Record or refine the voice-over using `docs/DEMO_VOICEOVER_SCRIPT.md` from the repo root.
3. Replace or polish scene animation logic in `src/scenes/`.
4. Render test passes locally with `npm run render:preview` before assembling the final export.

## Reference Capture

With the backend running on `http://localhost:8000`, you can refresh the app screenshots only:

```bash
npm run capture:refs
```

This uses Playwright to capture the main dashboard routes directly into `public/reference/` and verifies the expected route heading and primary panel before each screenshot.

To refresh both the screenshots and the proof data used in Scenes 7 and 8:

```bash
npm run sync:refs
```

To rebuild the full staged demo state, refresh proof artifacts, and capture everything in one pass:

```bash
npm run prepare:refs
```

If this is your first capture on a fresh machine, install the Playwright browser once:

```bash
npm run install:browsers
```

`npm run render:preview` now starts the local Motion Canvas dev server automatically unless `MOTION_CANVAS_START_SERVER=0` is set.
