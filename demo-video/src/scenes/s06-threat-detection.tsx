import {Img, Layout, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {all, createRef, createRefArray, waitFor} from '@motion-canvas/core';

import {referenceAssets} from '../lib/assets';
import {makeCalloutBadge, makeBottomBar, makeLeader} from '../lib/primitives';
import {fonts, palette, stage} from '../lib/theme';

/**
 * Scene 6 — Evasion + Fleet Status tour (~30s)
 *
 * Side-by-side tour of the Evasion efficiency view and
 * Fleet Status health view. Quick but informative.
 */
export default makeScene2D(function* (view) {
  /* ── refs ─────────────────────────────────────────────────────────── */
  const evasionImg = createRef<Img>();
  const fleetImg = createRef<Img>();
  const dimOverlay = createRef<Rect>();
  const callouts = createRefArray<Layout>();
  const bottomBar1 = createRef<Layout>();
  const bottomBar2 = createRef<Layout>();

  /* ── Evasion view full-bleed ─────────────────────────────────────── */
  view.add(
    <Img
      ref={evasionImg}
      src={referenceAssets.evasion}
      width={stage.width}
      height={stage.height}
      opacity={0}
      scale={1.04}
    />,
  );

  /* ── Fleet Status full-bleed (behind evasion initially) ──────────── */
  view.add(
    <Img
      ref={fleetImg}
      src={referenceAssets.fleetStatus}
      width={stage.width}
      height={stage.height}
      opacity={0}
      scale={1.04}
    />,
  );

  view.add(
    <Rect ref={dimOverlay} width={stage.width + 200} height={stage.height + 200} fill={'#040814'} opacity={0} />,
  );

  /* ── callouts for evasion ────────────────────────────────────────── */
  view.add(makeCalloutBadge({ref: callouts, label: 'Fuel consumed vs collisions avoided', accent: palette.accent, position: [-500, -360], width: 460}));
  view.add(makeCalloutBadge({ref: callouts, label: 'Maneuver efficiency breakdown', accent: palette.warning, position: [500, -360], width: 420}));

  /* ── callouts for fleet status ───────────────────────────────────── */
  view.add(makeCalloutBadge({ref: callouts, label: 'Fleet-wide health diagnostics', accent: palette.primary, position: [-500, -360], width: 440}));
  view.add(makeCalloutBadge({ref: callouts, label: 'Per-satellite fuel & status', accent: palette.accent, position: [500, -360], width: 400}));

  view.add(makeBottomBar({ref: bottomBar1, text: 'Evasion View — fuel efficiency and avoidance effectiveness metrics'}));
  view.add(makeBottomBar({ref: bottomBar2, text: 'Fleet Status — constellation health, diagnostics, and fuel posture'}));

  /* ── animation ───────────────────────────────────────────────────── */

  // Phase 1: Evasion view (0s - 13s)
  yield* all(
    evasionImg().opacity(1, 1.2),
    evasionImg().scale(1.0, 10.0),
  );
  yield* dimOverlay().opacity(0.25, 0.4);
  yield* bottomBar1().opacity(1, 0.4);
  yield* waitFor(0.5);

  yield* callouts[0].opacity(1, 0.5);
  yield* waitFor(2.5);
  yield* callouts[1].opacity(1, 0.5);
  yield* waitFor(3.5);

  // Phase 2: Cross-dissolve to Fleet Status (13s - 15s)
  yield* all(
    evasionImg().opacity(0, 1.0),
    callouts[0].opacity(0, 0.6),
    callouts[1].opacity(0, 0.6),
    bottomBar1().opacity(0, 0.6),
    fleetImg().opacity(1, 1.2),
    fleetImg().scale(1.0, 10.0),
  );
  yield* bottomBar2().opacity(1, 0.4);
  yield* waitFor(1.0);

  // Phase 3: Fleet status callouts (15s - 24s)
  yield* callouts[2].opacity(1, 0.5);
  yield* waitFor(2.5);
  yield* callouts[3].opacity(1, 0.5);
  yield* waitFor(3.5);

  // Phase 4: Fade out (24s - 26s)
  yield* all(
    fleetImg().opacity(0, 1.2),
    dimOverlay().opacity(0, 1.0),
    bottomBar2().opacity(0, 0.8),
    callouts[2].opacity(0, 0.8),
    callouts[3].opacity(0, 0.8),
  );
  yield* waitFor(0.3);
});
