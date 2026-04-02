import {Img, Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, sequence, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {makeCalloutBadge, makeBottomBar, makeLeader} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 2 — Ground Track deep dive (~45s)
 *
 * Full-bleed Ground Track screenshot with slow ken-burns pan.
 * Callouts highlight constellation markers, debris density,
 * trails, forecast arcs, and the terminator overlay.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const appImg = createRef<Img>();
  const dimOverlay = createRef<Rect>();
  const callouts = createRefArray<Layout>();
  const leaders = createRefArray<any>();
  const bottomBar = createRef<Layout>();

  /* ── full-bleed screenshot ───────────────────────────────────────── */
  view.add(
    <Img
      ref={appImg}
      src={referenceAssets.track}
      width={stage.width}
      height={stage.height}
      opacity={0}
      scale={1.12}
      position={[40, 20]}
    />,
  );

  /* ── dim overlay ─────────────────────────────────────────────────── */
  view.add(
    <Rect ref={dimOverlay} width={stage.width + 200} height={stage.height + 200} fill={'#040814'} opacity={0} />,
  );

  /* ── callout definitions ─────────────────────────────────────────── */
  const specs: Array<{
    label: string;
    accent: string;
    position: [number, number];
    width: number;
    points: [[number, number], [number, number], [number, number]];
  }> = [
    {
      label: 'Operator fleet — real catalog payloads',
      accent: palette.primary,
      position: [-560, -380],
      width: 430,
      points: [[-200, -200], [-360, -310], [-400, -310]],
    },
    {
      label: 'Dense debris environment',
      accent: palette.critical,
      position: [550, -380],
      width: 360,
      points: [[300, -160], [440, -310], [480, -310]],
    },
    {
      label: '90-min historical trails + forecast arcs',
      accent: palette.accent,
      position: [-560, 350],
      width: 460,
      points: [[-180, 160], [-370, 290], [-400, 290]],
    },
    {
      label: 'Day / night terminator overlay',
      accent: palette.warning,
      position: [540, 350],
      width: 400,
      points: [[260, 180], [430, 290], [460, 290]],
    },
  ];

  for (const spec of specs) {
    view.add(makeCalloutBadge({
      ref: callouts,
      label: spec.label,
      accent: spec.accent,
      position: spec.position,
      width: spec.width,
    }));
    view.add(makeLeader({points: spec.points, accent: spec.accent, ref: leaders, opacity: 0.85}));
  }

  view.add(makeBottomBar({ref: bottomBar, text: 'Ground Track — Mercator projection with coastlines, graticule, and real orbital traffic'}));

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Screenshot fades in with slow pan (0s - 4s)
  yield* all(
    appImg().opacity(1, 1.5),
    appImg().scale(1.0, 15.0),   // very slow zoom-out over entire scene
    appImg().position([0, 0], 15.0),
  );
  yield* waitFor(2.5);

  // Phase 2: Dim slightly and show callouts one at a time (4s - 30s)
  yield* dimOverlay().opacity(0.3, 0.6);
  yield* bottomBar().opacity(1, 0.5);
  yield* waitFor(1.0);

  // Stagger callouts with breathing room
  for (let i = 0; i < callouts.length; i++) {
    yield* callouts[i].opacity(1, 0.5);
    yield* leaders[i].end(1, 0.4);
    yield* waitFor(3.5);
  }

  // Phase 3: Hold (30s - 38s)
  yield* waitFor(4.0);

  // Phase 4: Fade out (38s - 40s)
  yield* all(
    appImg().opacity(0, 1.2),
    dimOverlay().opacity(0, 1.0),
    bottomBar().opacity(0, 0.8),
    ...callouts.map(c => c.opacity(0, 0.8)),
    ...leaders.map(l => l.opacity(0, 0.8)),
  );
  yield* waitFor(0.3);
});
