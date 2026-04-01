# CASCADE Demo Video Workspace

This directory is a standalone Motion Canvas workspace for the evaluation video.

It is intentionally isolated from the main application code:
- do not import code from `../frontend`
- do not import code from `../src`
- copy any needed screenshots, screen recordings, logos, or audio into `public/`

## Commands

```bash
npm install
npm start
npm run build
npm run capture:refs
```

## Important Files

- `docs/SHOT_PLAN.md` - shot-by-shot production plan
- `src/project.ts` - Motion Canvas project entry
- `src/scenes/` - scene placeholders matching the planned video structure
- `public/reference/` - exported app stills and clips to animate
- `public/audio/` - voice-over and music scratch files
- `scripts/capture-reference.mjs` - Playwright-based reference capture helper

## Recommended Workflow

1. Export app screenshots or short screen captures into `public/reference/`.
2. Record or refine the voice-over using `docs/DEMO_VOICEOVER_SCRIPT.md` from the repo root.
3. Replace the placeholder scenes in `src/scenes/` with the final animation logic.
4. Render test passes locally before assembling the final export.

## Reference Capture

With the backend running on `http://localhost:8000`, refresh the app screenshots used by Motion Canvas:

```bash
npm run capture:refs
```

This uses Playwright to capture the main dashboard routes directly into `public/reference/`.
